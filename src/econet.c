/*
 * This file is part of B-Em, a BBC Micro Emulator by Sarah Walker
 * and provides support for Econet, Acorn's low-cost network.
 *
 * A version of this file was originally part of BeebEm and has
 * been ported to B-Em by Steve Fosdick, 2021.  The original is:
 *
 * Copyright (C) 2004  Rob O'Donnell
 * Copyright (C) 2005  Mike Wyatt
 *
 * AUN support was added (to BeebEm) by Rob in Jun/Jul 2009.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

/*
 * Architecture overview
 * =====================
 *
 * Econet is Acorn's HDLC-based LAN.  The physical chip in a BBC Micro is
 * the Motorola MC6854 ADLC (Advanced Data Link Controller), memory-mapped
 * at $FEA0-$FEA3.  The CPU pushes TX bytes into the ADLC TX FIFO (via
 * econet_write_register) and reads RX bytes from the RX FIFO (via
 * econet_read_register / econet_read_rxreg).  The ADLC asserts NMI
 * (through the system VIA shift-register flag) when received data is
 * ready or a transmit slot is free.
 *
 * On the wire, every Econet data transfer uses a 4-way handshake:
 *   Scout → ScoutAck → Data → DataAck
 * AUN (Acorn Universal Networking) collapses this to a single UDP
 * packet plus a UDP ACK.  This module translates between the 4-way
 * sequence the BBC ROM expects and the simpler AUN UDP exchange.  The
 * current translation state is tracked by the 'fourwaystage' variable
 * (enum fourway / FWS_*).
 *
 * Traffic flow (transmit):
 *   CPU → ADLC TX FIFO → BeebTx buffer → (AUN UDP) → network peer
 * Traffic flow (receive):
 *   network peer → (AUN UDP) → EconetRx buffer → BeebRx buffer
 *   → ADLC RX FIFO → CPU
 *
 * The main periodic entry point is econet_poll(), called every 128 CPU
 * cycles from otherstuff_poll() in 6502.c.  Each call runs:
 *   econet_update_head()  — snapshot old status, handle control-reg changes
 *   econet_rx_tx()        — drain one TX byte or push one RX byte
 *   econet_update_tail()  — recalculate status bits, fire NMI if needed
 *
 * econet_state_changed() lets 6502.c fire an extra immediate poll after
 * the CPU reads a byte from the FIFO, matching BeebEm's throughput
 * optimisation.
 */

/*
 * The following conditional compilation is to deal with differences
 * between WINSOCK and the original Berkeley networking as used in
 * Unix-like systems.
 */
#ifdef WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
typedef u_long in_addr_t;
#define inet_aton(str, addr) inet_pton(AF_INET, str, addr)
#define local_ipaddr(a) (a.S_un.S_addr)
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
typedef int SOCKET;
#define INVALID_SOCKET -1
#define SOCKET_ERROR   -1
#define closesocket close
#define WSAGetLastError() (long)errno
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSACleanup()
#define SOCKADDR struct sockaddr
#define local_ipaddr(a) (a.s_addr)
#endif

#include "b-em.h"
#include "model.h"
#include "cmos.h"
#include "6502.h"
#include "econet.h"
#include "via.h"
#include "sysvia.h"
#include <ctype.h>

bool EconetEnabled;     /* Enable hardware */
bool EconetNMIenabled;  /* 68B54 -> NMI enabled. (IC97) */

/*
 * AUN packet header (8 bytes, always at the start of every UDP datagram).
 *
 * Byte 0: type    — one of the AUN_TYPE_* codes below.  Distinguishes
 *                   broadcasts, unicasts, immediate operations and ACKs.
 * Byte 1: port    — Econet destination port number (equivalent to a TCP port).
 * Byte 2: cb      — control byte / flags from the Econet frame.
 * Byte 3: pad     — retransmission count; typically 0.
 * Bytes 4-7: handle — 32-bit little-endian sequence number used to match ACKs
 *                     back to their originating packet.
 *
 * The AUN payload (the actual Econet data) immediately follows this header.
 */

struct aunhdr {
    uint8_t type;               /* AUN magic protocol byte */
#define AUN_TYPE_BROADCAST  1  /* 'type' from NetBSD aund.h */
#define AUN_TYPE_UNICAST    2
#define AUN_TYPE_ACK        3
#define AUN_TYPE_NACK       4
#define AUN_TYPE_IMMEDIATE  5
#define AUN_TYPE_IMM_REPLY  6

    uint8_t port;  /* dest port */
#define EC_PORT_PS_STATUS_ENQ   0x9f
#define EC_PORT_PS_STATUS_REPLY 0x9e
#define EC_PORT_PS_JOB          0xd1

    uint8_t cb;       /* flag */
    uint8_t pad;      /* retrans */
    uint32_t handle;  /* 4 byte sequence little-endian. */
};

static unsigned long ec_sequence = 0;  /* AUN sequence. */

/*
 * Four-way handshake FSM (fourwaystage / enum fourway).
 *
 * Real Econet uses a 4-step exchange for every data transfer:
 *   1. Scout       — sender announces intent to transmit to a specific station.
 *   2. ScoutAck    — receiver signals it is ready.
 *   3. Data        — sender transmits the payload frame.
 *   4. DataAck     — receiver acknowledges the data.
 *
 * AUN over UDP elides the scout exchange; it sends a single UDP datagram and
 * expects a single ACK.  This FSM bridges the gap so that the BBC ROM, which
 * expects the full 4-way sequence, sees correct ADLC signalling at each step.
 *
 * Transmit path (BBC → network):
 *   FWS_IDLE → FWS_SCOUTSENT (scout consumed, fake ack scheduled)
 *            → FWS_SCACKRCVD (fake ack delivered, BBC sends data frame)
 *            → FWS_DATASENT  (data sent as AUN UDP; or station-0 stub path)
 *            → FWS_IDLE      (fake final ack delivered)
 *
 * Receive path (network → BBC):
 *   FWS_IDLE → FWS_SCOUTRCVD (fake scout injected from AUN datagram)
 *            → FWS_SCACKSENT (BBC sent scout ack, brief delay)
 *            → FWS_DATARCVD  (AUN payload delivered as data frame)
 *            → FWS_IDLE
 *
 * FWS_WAIT4IDLE is an error/flush state: FlagFill is asserted and the FSM
 * waits for the medium to go idle before accepting new traffic.
 *
 * FWS_IMMSENT / FWS_IMMRCVD handle Econet IMMEDIATE commands (port 0),
 * which bypass the normal 4-way and are delivered as AUN_TYPE_IMMEDIATE.
 */

enum fourway {
    FWS_IDLE,
    FWS_SCOUTSENT,  /* BBC micro sent scout (on Econet, suppressed on AUN). */
    FWS_SCACKRCVD,  /* Fake scout ack created for BBC micro to receive. */
    FWS_DATASENT,   /* BBC micro data packet sent to AUN. */
    FWS_WAIT4IDLE,
    FWS_SCOUTRCVD,  /* Data has arrived on AUN, faking scout to BBC micro. */
    FWS_SCACKSENT,  /* BBC micro has send scout ack, running a timer. */
    FWS_DATARCVD,   /* Deliver the (delayed) AUN packet to the BBC micro. */
    FWS_IMMSENT,    /* immediate scout packet sent on AUN */
    FWS_IMMRCVD
};

static enum fourway fourwaystage;

static const char fws_names[][10] = {
    "IDLE",
    "SCOUTSENT",
    "SCACKRCVD",
    "DATASENT",
    "WAIT4IDLE",
    "SCOUTRCVD",
    "SCACKSENT",
    "DATARCVD",
    "IMMSENT",
    "IMMRCVD"
};

/* Headers for traditional Econet */

struct shorteconethdr {
    uint8_t deststn;
    uint8_t destnet;
    uint8_t srcstn;
    uint8_t srcnet;
};

struct longeconetpacket {
    uint8_t deststn;
    uint8_t destnet;
    uint8_t srcstn;
    uint8_t srcnet;
    uint8_t cb;
    uint8_t port;
};

/*
 * What is the biggest packet required?  The MC6854 has 3 byte FIFOs.
 * There is no wait for an end of data before transmission starts. Data
 * is sent immediately it's put into the first slot.
 *
 * Does Econet send multiple packets for big transfers, or just one
 * huge packet?  What's MTU on econet? Depends on clock speed but it's
 * big (e.g. 100K).  As we are using UDP, we will use a larger buffer
 * buffer and accept data into this, then send it periodically.  We
 * will accept incoming data similarly, and dribble it back into the
 * emulated 68B54.
 *
 * We should thus never suffer underrun errors....
 * -- we do actually flag an underrun, if data exceeds the size of the buffer.
 * -- sniffed AUN between live arcs seems to max out at 1288 bytes (1280+header)
 * -- bigger packers ARE possible - UDP fragments & reassembles transparently.. doh..
 *
 * 64K max.. can't see any transfers being neeeded larger than this too often!
 * (and that's certainly larger than acorn bridges can cope with.)
 */

#define ETHERNETBUFFERSIZE 65536

/* Defintion for the AUN packets sent over UDP. */

struct aunpacket {
    union {
        uint8_t raw[8];
        struct aunhdr ah;
    };
    union {
        uint8_t buff[ETHERNETBUFFERSIZE];
        struct shorteconethdr eh;
    };
    volatile unsigned int Pointer;
    volatile unsigned int BytesInBuffer;
    unsigned long inet_addr;
    unsigned int port;
    unsigned int deststn;
    unsigned int destnet;
};

/*
 * Packet buffers — two distinct layers.
 *
 * Network-side (struct aunpacket):
 *   EconetTx — assembled outgoing AUN UDP datagram (8-byte AUN header + payload).
 *   EconetRx — most recently received AUN UDP datagram, ready to be presented to
 *              the BBC ROM as an Econet frame.
 *
 * BBC-side (struct econetpacket):
 *   BeebTx   — bytes the CPU has written to the ADLC TX FIFO, accumulated until
 *              TxLast is seen, then sent as a single AUN UDP datagram.
 *   BeebRx   — AUN payload reformatted with the traditional Econet 4-byte header
 *              (dest/src stn+net) prepended; dribbled byte-by-byte into the ADLC
 *              RX FIFO by econet_rx_data().
 *
 * The Pointer field is a write (or read) cursor; BytesInBuffer is the total
 * number of valid bytes currently held.
 */

/* buffers used to construct packets for sending out via UDP */

static struct aunpacket EconetRx;
static struct aunpacket EconetTx;

/* buffers used to construct packets sent to/received from bbc micro */

struct econetpacket {
    union {
        struct longeconetpacket eh;
        uint8_t buff[ETHERNETBUFFERSIZE + 12];
    };
    volatile unsigned int Pointer;
    volatile unsigned int BytesInBuffer;
};

static struct econetpacket BeebTx;
static struct econetpacket BeebRx;
static unsigned lastrxlen = 0;

uint8_t BeebTxCopy[6];             /* size of longeconetpacket structure */
static uint8_t BeebTxScoutExt[4];  /* extended-scout extra bytes (NOTIFY/printer, ports 0x83-0x85) */

/* Networking Table */

/* Holds data from econet.cfg file */
struct ECOLAN {  /* what we we need to find a beeb? */
    struct ECOLAN *next;
    uint8_t station;
    uint8_t network;
    uint16_t port;
    struct in_addr inet_addr;
};

struct AUNTAB {
    struct AUNTAB *next;
    struct in_addr inet_addr;
    uint8_t network;
};

static struct ECOLAN *networks;  /* list of my friends! :-) */
static struct AUNTAB *aunnet;    /* AUNmap file for guess mode. */
static struct AUNTAB *myaunnet;  /* aunnet entry that I match. */

static int inmask, outmask;

/* Configuration Options.
 * These, among others, are overridden in econet.cfg
 * (see econet_read_netfile())
 */
static bool confAUNmode = false;  /* Use AUN style networking */
static bool confLEARN = false;    /* Add receipts from unknown hosts to network table */
static bool confSTRICT = false;   /* Assume network ip=stn number when sending to unknown hosts */
static unsigned int FourWayStageTimeout = 15625;
static bool MassageNetworks = false;  /* massage network numbers on send/receive (add/sub 128) */

/* Station Configuration settings:
 * You specify station number on command line.
 * This allows multiple different instances of the emulator to be run
 * and to communicate with each other.  Note that you STILL need to
 * have them all listed in econet.cfg so each one knows where the
 * others are.
 */
static uint8_t EconetStationNumber = 0;    /* default Station Number */
static unsigned int EconetListenPort = 0;  /* default Listen port */
static unsigned long EconetListenIP = 0x0100007f;
/* IP settings: */
static SOCKET UdpSocket = INVALID_SOCKET;  /* Single UDP socket. */
static bool SocketOpen = false;            /* Used to flag line up and clock running */

/* Flag Fill.
 *
 * A receiving station goes into flag fill mode while it is processing,
 * a message.  This stops other stations sending messages that may
 * interfere with the four-way handshake. That means from the emulated
 * perspective of this BBC micro, the remote end would flag-fill
 * between an outgoing scout and the acknowledgement and between an
 * outgoing data packet and its acknowledgement.  The BBC micro itself
 * would flag fill between receiving a flag acknowledgement and sending
 * the data packet.
 *
 * Attempting to notify evey station using IP messages when flag fill
 * goes active/inactive would be complicated and would suffer from
 * timing issues due to network latency, so a pseudo flag fill
 * algorithm is emulated.  We assume that the receiving station will go
 * into flag fill when we send a message or when we see a message
 * destined for another station.  We cancel flag fill when we receive a
 * message as the other station must have cancelled flag fill.  In
 * order to cancel flag fill after the last message of a four-way
 * handshake we time it out - which is not ideal as we do not want to
 * delay new messages any longer that we have to - but it will have
 * to do for now!
 */

static bool FlagFillActive;                         /* Flag fill state */
static unsigned long EconetFlagFillTimeoutTrigger;  /* Trigger point for flag fill */
static unsigned long EconetFlagFillTimeout = 256;   /* Cycles for flag fill timeout */

/* In the BeebEm version the time between network bytes was
 * configurable but in B-Em we have hooked the function otherstuff_poll
 * which fixes the interval at 128 CPU cycles.  The original BeebEm
 * comment follows...
 *
 * Frequency between network actions.
 * max 250Khz network clock. 2MHz system clock. one click every 8 cycles.
 * say one byte takes about 8 clocks, receive a byte every 64 cpu cycyes?
 * (The reason for "about" 8 clocks is that as this a continuous syncronous tx,
 * there are no start/stop bits, however to avoid detecting a dead line as ffffff
 * zeros are added and removed transparently if you get more than five "1"s
 * during data transmission - more than 5 are flags or errors)
 * 6854 datasheet has max clock frequency of 1.5MHz for the B version.
 * 64 cycles seems to be a bit fast for 'netmon' prog to keep up - set to 128.
 */

/*
 * BeebEm has a central, incrementing counter which can be used to
 * implement timeouts.  B-Em does not so this module keeps its own
 * counter.  This ticks in units of 128 CPU cycles.
 */
static unsigned long EconetCycles = 0;

/* Other timeouts using the above cycke counter */

static unsigned long EconetSCACKtrigger;            /* trigger point for scout ack */
static unsigned long EconetSCACKtimeout = 4;        /* cycles to delay before sending ack to scout (aun mode only) */
static unsigned long EconetTxByteTrigger;           /* next EconetCycles at which a TX byte may be drained */
static unsigned long EconetTimeBetweenBytes = 128;  /* drain pace (EconetCycles units = 128 CPU cycles each) */
static unsigned long EconetWait4IdleTrigger;
static unsigned long EconetWait4IdleTimeout = 12;
static unsigned long Econet4Wtrigger;
/* Watchdog for AUN immediate commands (port=0): AUN-aware hosts such as PiFS
 * normally reply with a real IMM_REPLY straight away, but if nothing replies
 * in time this fires a fake one so ANFS can proceed to the 4-way login
 * instead of stalling for FourWayStageTimeout (~3.2s) and seeing
 * "No reply from station". */
static unsigned long EconetFakeImmReplytrigger;
static uint8_t       EconetFakeImmReplySrcStn;
static uint8_t       EconetFakeImmReplySrcNet;

/* After the fake final ACK completes the outgoing 4-way, the NFS ROM waits
 * for the file server to reply with the operation result via an incoming
 * 4-way.  EconetFakeResponsePending flags that this response needs to be
 * injected once FWS_IDLE is restored; EconetFakeResponseActive flags that
 * the fake scout has been delivered and FWS_SCACKSENT should use the saved
 * fake data instead of EconetRx.
 *
 * PiFS can push multiple 1024-byte data blocks in rapid succession (e.g.
 * during a LOAD).  We hold up to ECONET_RESPQ_DEPTH blocks in a circular
 * queue and promote them one at a time into the active Response1 slot.
 * The buffer is sized to hold a full 1024-byte AUN payload so no data is
 * silently truncated. */
#define ECONET_RESP_BUFF_MAX  4096
#define ECONET_RESPQ_DEPTH    8

struct econet_queued_resp {
    uint8_t  buff[ECONET_RESP_BUFF_MAX];
    int      len;
    uint8_t  reply_port;
    uint8_t  cb;
    uint8_t  src_stn;
    uint8_t  src_net;
    uint32_t handle;
};

static bool EconetFakeResponsePending;
static bool EconetFakeResponseActive;
/* Set when FWS_SCACKSENT delivers an injected fake FS response (rather than
 * data genuinely received over AUN).  The FS already got its real AUN ACK
 * when the response was queued, so the NFS ROM's own FWS_DATARCVD final ACK
 * must not be sent back over the wire — it would arrive as a spurious
 * Acknowledge with sequence 0. */
static bool EconetFakeResponseSuppressAck;
static uint8_t EconetFakeResponseBuff[ECONET_RESP_BUFF_MAX];
static int EconetFakeResponseLen;
static uint8_t EconetFakeResponseReplyPort;
static uint8_t EconetFakeResponseCb;
static uint8_t EconetFakeResponseSrcStn;
static uint8_t EconetFakeResponseSrcNet;
/* AUN handle of a queued extended-scout (NOTIFY/printer) transaction, saved
 * so the eventual FWS_DATARCVD final ACK carries the sender's own handle and
 * is recognised as the reply to their original packet. */
static uint32_t EconetFakeResponseHandle;

/* Circular queue of FS responses waiting to be promoted to Response1. */
static struct econet_queued_resp EconetRespQ[ECONET_RESPQ_DEPTH];
static int EconetRespQHead = 0;
static int EconetRespQTail = 0;
static int EconetRespQLen  = 0;

/* Set when we've sent data and received only the bridge's immediate AUN ACK
 * (not the actual FS response yet).  In FWS_IDLE we keep FlagFillActive true
 * so ANFS2 doesn't time out while the FS processes the request and the reply
 * travels back through the Econet bridge. */
static bool EconetWaitingForBridgeResp = false;

/* Identifies the most recently processed AUN_TYPE_UNICAST (sender + port +
 * sequence handle), so a retransmitted duplicate (sender's ACK-wait timer
 * fired before our ACK arrived) can be re-ACKed without being delivered to
 * the Beeb a second time. */
static uint8_t  EconetLastUnicastStn;
static uint8_t  EconetLastUnicastNet;
static uint8_t  EconetLastUnicastPort;
static uint32_t EconetLastUnicastHandle;
static bool     EconetLastUnicastValid;

/* Minimum cycle counter before the Response1 fake scout is injected.  A
 * delay of 500 polls gives ANFS time to update its NMI dispatch vector
 * ($0406/$0407) to accept scouts on the file-data port before the next
 * block arrives. */
static unsigned long EconetFakeResponseInjectAfter = 0;

/* Device and temp copy */

volatile struct MC6854 ADLC;
static uint8_t old_status1, old_status2;
static bool EconetStateChanged = false;

/* Station/network that sent the most recently received AUN unicast scout.
 * Saved at FWS_IDLE→FWS_SCOUTRCVD so the FWS_SCACKSENT data-delivery step
 * can correctly fill BeebRx.eh.srcstn / srcnet.  Using EconetTx.deststn
 * there was wrong: it reflects the destination of OUR last TX, not the
 * sender of the incoming packet. */
static uint8_t rx_scout_srcstn, rx_scout_srcnet;

static const uint8_t powers[4] = { 1, 2, 4, 8 };

uint8_t irqcause;    /* flagto indicate cause of irq sr1b7 */
uint8_t sr1b2cause;  /* flagto indicate cause of irq sr1b2 */

static void econet_free_networks(void)
{
    struct ECOLAN *ptr1 = networks;
    while (ptr1) {
        struct ECOLAN *ptr2 = ptr1->next;
        free(ptr1);
        ptr1 = ptr2;
    }
    networks = NULL;
}

static void econet_free_aunmap(void)
{
    struct AUNTAB *ptr1 = aunnet;
    while (ptr1) {
        struct AUNTAB *ptr2 = ptr1->next;
        free(ptr1);
        ptr1 = ptr2;
    }
    aunnet = NULL;
}

/*
 * econet_read_netfile — load station and network configuration from disk.
 *
 * Reads econet.cfg, which maps (network, station) pairs to IP address + UDP port,
 * and populates the 'networks' linked list.  Each line can also set global options
 * such as AUNMODE, LEARN, STRICT and the flag-fill / timeout parameters.
 *
 * Also reads AUNmap.cfg, which maps network numbers to IP subnets so that AUN
 * mode can resolve a destination network number to a real IP address without
 * requiring a full per-station entry in econet.cfg.
 *
 * Called from econet_reset() so that the configuration is refreshed on Break.
 */
static void econet_read_netfile(void)
{
    /* read econet.cfg file into network table */
    ALLEGRO_PATH *path = find_cfg_file("econet", ".cfg");
    if (path) {
        const char *cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
        FILE *EcoCfg = fopen(cpath, "rt");
        if (EcoCfg) {
            econet_free_networks();
            char EcoNameBuf[256];
            unsigned lineno = 0;
            while (fgets(EcoNameBuf, sizeof(EcoNameBuf)-1, EcoCfg)) {
                lineno++;
                char *EcoName = EcoNameBuf;
                log_debug("Econet: ConfigFile %s", EcoName);
                int ch = *EcoName;
                while (ch == ' ' || ch == '\t')
                    ch = *++EcoName;
                if (ch != '#') {
                    if (isdigit(ch)) {
                        char *end;
                        uint8_t network = strtol(EcoName, &end, 10);
                        if (end > EcoName) {
                            EcoName = end;
                            uint8_t station = strtol(EcoName, &end, 10);
                            if (end > EcoName) {
                                EcoName = end;
                                ch = *EcoName;
                                while (ch && isspace(ch))
                                    ch = *++EcoName;
                                char *EcoPtr = EcoName;
                                while (ch && (ch == '.' || isdigit(ch)))
                                    ch = *++EcoPtr;
                                if (EcoPtr > EcoName) {
                                    *EcoPtr++ = 0;
                                    struct in_addr iaddr;
                                    if (inet_aton(EcoName, &iaddr)) {
                                        unsigned int port = strtol(EcoPtr, &end, 10);
                                        if (end > EcoPtr) {
                                            log_debug("Econet: ConfigFile Net %i Stn %i IP %s Port %i", network, station, inet_ntoa(iaddr), port);
                                            struct ECOLAN *entry = malloc(sizeof(struct ECOLAN));
                                            if (entry) {
                                                entry->next = networks;
                                                entry->station = station;
                                                entry->network = network;
                                                entry->inet_addr = iaddr;
                                                entry->port = port;
                                                networks = entry;
                                            }
                                            else {
                                                log_error("econet: out of memory for network table");
                                                break;
                                            }
                                        }
                                        else
                                            log_warn("Econet: %s, line %u: missing or invalid port number", cpath, lineno);
                                    }
                                    else
                                        log_warn("Econet: %s, line %u: invalid IP address '%s'", cpath, lineno, EcoName);
                                }
                                else
                                    log_warn("Econet: %s, line %u: missing IP address", cpath, lineno);
                            }
                            else
                                log_warn("Econet: %s, line %u: invalid station number '%s'", cpath, lineno, EcoName);
                        }
                        else
                            log_warn("Econet: %s, line %u: invalid network number '%s'", cpath, lineno, EcoName);
                    }
                    else {
                        char *EcoPtr = EcoName;
                        do {
                            if (isupper(ch))
                                ch = tolower(ch);
                            *EcoPtr = ch;
                            ch = *++EcoPtr;
                        } while (ch && !isspace(ch));

                        if (ch) {
                            bool bad = false;
                            *EcoPtr++ = 0;
                            int value = atoi(EcoPtr);
                            if (strcmp("aunmode", EcoName) == 0)
                                confAUNmode = (value != 0);
                            else if (strcmp("learn", EcoName) == 0)
                                confLEARN = (value != 0);
                            else if (strcmp("aunstrict", EcoName) == 0)
                                confSTRICT = (value != 0);
                            else if (strcmp("flagfilltimeout", EcoName) == 0)
                                EconetFlagFillTimeout = (value);
                            else if (strcmp("scacktimeout", EcoName) == 0)
                                EconetSCACKtimeout = (value);
                            else if (strcmp("fourwaytimeout", EcoName) == 0)
                                FourWayStageTimeout = (value);
                            else if (strcmp("massagenets", EcoName) == 0)
                                MassageNetworks = (value != 0);
                            else if (strcmp("singlesocket", EcoName) == 0)
                                ; /* Windows-specific, ignored */
                            else if (strcmp("timebetweenbytes", EcoName) == 0)
                                EconetTimeBetweenBytes = (value);
                            else {
                                bad = true;
                                log_warn("Econet: unrecognised option name %s in config file %s", EcoName, cpath);
                            }
                            if (!bad)
                                log_debug("Econet: Config option %s flag %i", EcoName, value);
                        }
                    }
                }
            }
            fclose(EcoCfg);

            if (MassageNetworks) {
                inmask = 255;
                outmask = 0;
            }
            else {
                inmask = 127;
                outmask = 128;
            }
        }
        else
            log_error("Econet: Failed to open configuration file %s", cpath);
        al_destroy_path(path);
    }
    else
        log_error("Econet: unable to find Econet configuration file econet.cfg");

    if (confAUNmode) {  /* don't bother reading file if not using AUN. */
        if ((path = find_cfg_file("AUNmap", ".cfg"))) {
            const char *cpath = al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP);
            log_debug("Econet: loading AUNmap from %s", cpath);
            FILE *EcoCfg = fopen(cpath, "rt");
            if (EcoCfg) {
                econet_free_aunmap();
                char EcoNameBuf[256];
                unsigned lineno = 0;
                while (fgets(EcoNameBuf, sizeof(EcoNameBuf), EcoCfg)) {
                    lineno++;
                    char *EcoName = EcoNameBuf;
                    int ch = *EcoName;
                    while (ch && isspace(ch))
                        ch = *++EcoName;
                    char *EcoSep = EcoName;
                    while (ch && !isspace(ch))
                        ch = *++EcoSep;
                    if (ch) {
                        *EcoSep = 0;
                        if (!strcasecmp(EcoName, "AddMap")) {
                            while (ch && isspace(ch))
                                ch = *++EcoSep;
                            if (ch) {
                                EcoName = EcoSep;
                                while (ch && (isdigit(ch) || ch == '.'))
                                    ch = *EcoSep++;
                                if (ch && EcoSep > EcoName) {
                                    EcoSep[-1] = 0;
                                    struct in_addr iaddr;
                                    if (inet_aton(EcoName, &iaddr)) {
                                        while (ch && isspace(ch))
                                            ch = *EcoSep++;
                                        if (ch) {
                                            unsigned net = strtol(EcoSep - 1, NULL, 10);
                                            log_debug("Econet: AUNmap Net %i IP %s ", net, inet_ntoa(iaddr));
                                            struct AUNTAB *entry = malloc(sizeof(struct AUNTAB));
                                            if (entry) {
                                                uint32_t haddr = ntohl(iaddr.s_addr) & 0xffffff00;
                                                entry->next = aunnet;
                                                entry->inet_addr.s_addr = htonl(haddr);
                                                entry->network = net;
                                                aunnet = entry;
                                                /* note which network we are a part of.. this wont work on first run as listenip not set! */
                                                if (haddr == (ntohl(EconetListenIP) & 0xffffff00)) {
                                                    myaunnet = entry;
                                                    log_debug("Econet: ..and that's the one we're in");
                                                }
                                            }
                                            else {
                                                log_error("econet: out of memory for AUN map");
                                                break;
                                            }
                                        }
                                        else
                                            log_warn("Econet: %s, line %u: missing network", cpath, lineno);
                                    }
                                    else
                                        log_warn("Econet: %s, line %u: invalid IP address '%s'", cpath, lineno, EcoName);
                                }
                                else
                                    log_warn("Econet: %s, line %u: missing IP address", cpath, lineno);
                            }
                            else
                                log_warn("Econet: %s, line %u: missing IP address", cpath, lineno);
                        }
                    }
                }
                fclose(EcoCfg);
            }
            else
                log_error("Econet: unable to open AUN map %s: %s", cpath, strerror(errno));
        }
        else
            log_error("Econet: unable to find AUN map AUNmap.cfg");
    }
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

#ifdef _DEBUG

void econet_adlc_debug(void)
{
    log_debug("ADLC: Ctl:%02X %02X %02X %02X St:%02X %02X TXptr:%01x rx:%01x rxb: %u FF:%d IRQc:%02x SR2c:%02x PC:%04x 4W:%s",
            (int)ADLC.control1, (int)ADLC.control2, (int)ADLC.control3, (int)ADLC.control4,
            (int)ADLC.status1, (int)ADLC.status2, (int)ADLC.txfptr, ADLC.rxfptr, (int)(BeebRx.BytesInBuffer-BeebRx.Pointer), FlagFillActive ? 1 : 0,
            (int)irqcause, (int)sr1b2cause, (int)pc, fws_names[fourwaystage]);
}
#else
static inline void econet_adlc_debug(void) {}
#endif

static const char *econet_socket_errstr(void)
{
#ifdef WIN32
    static char err[20];
    snprintf(err, sizeof(err), "error %d", WSAGetLastError());
    return err;
#else
    return strerror(errno);
#endif
}

/*
 * econet_reset — bring the ADLC and all associated state back to a clean start.
 *
 * Asserts RxReset + TxReset in CR1, zeros all status and FIFO state, resets the
 * FSM to FWS_IDLE, and discards any queued fake responses.  Closes the UDP socket
 * if one was open, then (if Econet is enabled) re-reads the config files and opens
 * a fresh non-blocking UDP socket bound to this station's port.
 *
 * Called on power-on and on Break so the network is re-initialised without
 * restarting the emulator.
 */
void econet_reset(void)
{
    if (EconetEnabled)
        log_debug("Econet: reset, hardware enabled");
    else
        log_debug("Econet: reset, hardware disabled");

    /* hardware operations:
     * set RxReset and TxReset */
    ADLC.control1 = ADLC_CTL1_RX_RESET|ADLC_CTL1_TX_RESET;
    /* reset TxAbort, RTS, LoopMode, DTR */
    ADLC.control4 = 0;
    ADLC.control2 = 0;
    ADLC.control3 = 0;

    /* clear all status conditions */
    ADLC.status1 = 0;  /* cts - clear to send line input (no collissions talking udp) */
    ADLC.status2 = 0;  /* dcd - no clock (until sockets initialised and open) */
    ADLC.sr2pse = 0;

    /* software stuff: */
    EconetRx.Pointer = 0;
    EconetRx.BytesInBuffer = 0;
    EconetTx.Pointer = 0;
    EconetTx.BytesInBuffer = 0;

    BeebRx.Pointer = 0;
    BeebRx.BytesInBuffer = 0;
    BeebTx.Pointer = 0;
    BeebTx.BytesInBuffer = 0;

    fourwaystage = FWS_IDLE;  /* used for AUN mode translation stage. */

    ADLC.rxfptr = 0;
    ADLC.rxap = 0;
    ADLC.rxffc = 0;
    ADLC.txfptr = 0;
    ADLC.txftl = 0;

    ADLC.idle = 1;
    ADLC.cts = 0;

    irqcause = 0;
    sr1b2cause = 0;

    FlagFillActive = false;
    EconetFlagFillTimeoutTrigger = 0;
    EconetTxByteTrigger = 0;
    EconetRespQHead = EconetRespQTail = EconetRespQLen = 0;
    EconetFakeResponseInjectAfter = 0;
    EconetFakeResponsePending = false;
    EconetFakeResponseActive = false;
    EconetFakeResponseSuppressAck = false;
    EconetLastUnicastValid = false;
    EconetSCACKtrigger = 0;
    EconetWait4IdleTrigger = 0;
    EconetFakeImmReplytrigger = 0;
    Econet4Wtrigger = 0;

    /* kill anything that was in use */
    if (SocketOpen) {
        closesocket(UdpSocket);
        SocketOpen = false;
    }

    /* Stop here if not enabled */
    if (!EconetEnabled)
        return;

    /* Read in econet.cfg.  Done here so can refresh it on Break. */
    econet_read_netfile();

    /*----------------------*/
#ifdef WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        log_error("Econet: WSAStartup failed: error %ld", WSAGetLastError());
        return;
    }
#endif

    /* Create a SOCKET for listening for incoming connection requests. */
    if ((UdpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        log_error("Econet: Failed to open listening socket: %s", econet_socket_errstr());
        WSACleanup();
        return;
    }
#ifdef WIN32
    unsigned long one = 1;
    if (ioctlsocket(UdpSocket, FIONBIO, &one)) {
        log_error("Econet: Failed to set non-blocking mode on socket: %s", econet_socket_errstr());
        closesocket(UdpSocket);
        WSACleanup();
        return;
    }
    /* On Windows, an ICMP "port unreachable" in response to an earlier
     * sendto() (e.g. a broadcast to a host with nothing listening on that
     * port) causes the *next* recvfrom() on this UDP socket to fail with
     * WSAECONNRESET (10054), even though UDP is connectionless and no
     * packets were actually lost. Disable that behaviour. */
    BOOL bNewBehavior = FALSE;
    DWORD dwBytesReturned = 0;
    if (WSAIoctl(UdpSocket, SIO_UDP_CONNRESET, &bNewBehavior, sizeof(bNewBehavior),
                  NULL, 0, &dwBytesReturned, NULL, NULL) == SOCKET_ERROR) {
        log_warn("Econet: Failed to disable SIO_UDP_CONNRESET: %s", econet_socket_errstr());
    }
#else
    int flags = fcntl(UdpSocket, F_GETFL);
    if (flags == -1) {
        log_error("Econet: Failed to get socket flags (non-blocking): %s", econet_socket_errstr());
        closesocket(UdpSocket);
        return;
    }
    else if (fcntl(UdpSocket, F_SETFL, flags | O_NONBLOCK) == -1) {
        log_error("Econet: Failed to set socket flags (non-blocking): %s", econet_socket_errstr());
        closesocket(UdpSocket);
        return;
    }
#endif

    /*----------------------*/
    /* The sockaddr_in structure specifies the address family,
     * IP address, and port for the socket that is being bound. */
    struct sockaddr_in service;
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = INADDR_ANY;  /* inet_addr("127.0.0.1"); */

    /* Already have a station num? Either from command line or a free one
     * we found on previous reset. */
    if (EconetStationNumber != 0) {
        log_debug("Econet: looking up existing station number %d", EconetStationNumber);
        /* Look up our port number in network config */
        for (struct ECOLAN *entry = networks; entry; entry = entry->next) {
            if (entry->station == EconetStationNumber) {
                EconetListenPort = entry->port;
                EconetListenIP = entry->inet_addr.s_addr;
                break;
            }
        }
        if (EconetListenPort != 0) {
            service.sin_port = htons(EconetListenPort);
            service.sin_addr.s_addr = EconetListenIP;
            if (bind(UdpSocket, (SOCKADDR *) & service, sizeof(service)) == SOCKET_ERROR) {
                log_error("Econet: Failed to bind local address %s:%u: %s", inet_ntoa(service.sin_addr), EconetListenPort, econet_socket_errstr());
                closesocket(UdpSocket);
                WSACleanup();
                return;
            }
        }
        else {
            log_error("Econet: Failed to find station %d in econet.cfg", EconetStationNumber);
            WSACleanup();
            return;
        }

    }
    else {
        /* Station number not specified, find first one not already in use. */
        log_debug("Econet: auto-allocating station number");
#ifndef WIN32
        struct ifaddrs *iflist = NULL;
        if (getifaddrs(&iflist) != 0) {
            log_error("Econet: Failed to enumerate network interfaces");
            WSACleanup();
            return;
        }
        in_addr_t loopback = htonl(INADDR_LOOPBACK);
        /* See if configured addresses match local IPs */
        for (struct ECOLAN *entry = networks; entry && EconetStationNumber == 0; entry = entry->next) {
            for (struct ifaddrs *ifa = iflist; ifa && EconetStationNumber == 0; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                    continue;
                in_addr_t ifaddr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
                if (entry->inet_addr.s_addr == loopback || entry->inet_addr.s_addr == ifaddr) {
                    service.sin_port = htons(entry->port);
                    service.sin_addr = entry->inet_addr;
                    if (bind(UdpSocket, (SOCKADDR *) & service, sizeof(service)) == 0) {
                        EconetListenPort = entry->port;
                        EconetListenIP = entry->inet_addr.s_addr;
                        EconetStationNumber = entry->station;
                    }
                }
            }
        }
        if (EconetListenPort == 0) {
            /* still can't find one ... strict mode? */
            if (confSTRICT && confAUNmode) {
                log_debug("Econet: No free hosts in table; trying automatic mode..");
                for (struct AUNTAB *entry = aunnet; entry && EconetStationNumber == 0; entry = entry->next) {
                    for (struct ifaddrs *ifa = iflist; ifa && EconetStationNumber == 0; ifa = ifa->ifa_next) {
                        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                            continue;
                        struct in_addr localaddr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
                        in_addr_t local = htonl(ntohl(local_ipaddr(localaddr)) & 0xffffff00);
                        if (entry->inet_addr.s_addr == local) {
                            service.sin_port = htons(32768);
                            service.sin_addr.s_addr = local_ipaddr(localaddr);
                            if (bind(UdpSocket, (SOCKADDR *) & service, sizeof(service)) == 0) {
                                myaunnet = entry;
                                struct ECOLAN *ecoent = malloc(sizeof(struct ECOLAN));
                                if (ecoent) {
                                    ecoent->next = networks;
                                    ecoent->inet_addr.s_addr = EconetListenIP = local_ipaddr(localaddr);
                                    ecoent->port = EconetListenPort = 32768;
                                    ecoent->station = EconetStationNumber = local_ipaddr(localaddr) >> 24;
                                    ecoent->network = entry->network;
                                    networks = ecoent;
                                }
                                else {
                                    log_error("econet: out of memory for network table");
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        freeifaddrs(iflist);
        if (EconetStationNumber == 0) {
            log_error("Econet: Failed to find free station/port to bind to");
            WSACleanup();
            return;
        }
#else
        char localhost[256];
        struct hostent *hent;

        /* Get localhost IP address */
        if (gethostname(localhost, 256) != SOCKET_ERROR && (hent = gethostbyname(localhost)) != NULL) {
            in_addr_t loopback = htonl(INADDR_LOOPBACK);
            /* See if configured addresses match local IPs */
            for (struct ECOLAN *entry = networks; entry && EconetStationNumber == 0; entry = entry->next) {
                /* Check address for each network interface/card */
                for (int a = 0; hent->h_addr_list[a] != NULL && EconetStationNumber == 0; ++a) {
                    if (entry->inet_addr.s_addr == loopback || entry->inet_addr.s_addr == *(in_addr_t *)hent->h_addr_list[a]) {
                        service.sin_port = htons(entry->port);
                        service.sin_addr = entry->inet_addr;
                        if (bind(UdpSocket, (SOCKADDR *) & service, sizeof(service)) == 0) {
                            EconetListenPort = entry->port;
                            EconetListenIP = entry->inet_addr.s_addr;
                            EconetStationNumber = entry->station;
                        }
                    }
                }
            }
            if (EconetListenPort == 0) {
                /* still can't find one ... strict mode? */
                if (confSTRICT && confAUNmode) {
                    log_debug("Econet: No free hosts in table; trying automatic mode..");
                    for (struct AUNTAB *entry = aunnet; entry && EconetStationNumber == 0; entry = entry->next) {
                        for (int a = 0; hent->h_addr_list[a] != NULL && EconetStationNumber == 0; ++a) {
                            struct in_addr localaddr;
                            memcpy(&localaddr, hent->h_addr_list[a], sizeof(struct in_addr));
                            in_addr_t local = htonl(ntohl(local_ipaddr(localaddr)) & 0xffffff00);
                            if (entry->inet_addr.s_addr == local) {
                                service.sin_port = htons(32768);
                                service.sin_addr.s_addr = local_ipaddr(localaddr);
                                if (bind(UdpSocket, (SOCKADDR *) & service, sizeof(service)) == 0) {
                                    myaunnet = entry;
                                    struct ECOLAN *ecoent = malloc(sizeof(struct ECOLAN));
                                    if (ecoent) {
                                        ecoent->next = networks;
                                        ecoent->inet_addr.s_addr = EconetListenIP = local_ipaddr(localaddr);
                                        ecoent->port = EconetListenPort = 32768;
                                        ecoent->station = EconetStationNumber = local_ipaddr(localaddr) >> 24;
                                        ecoent->network = entry->network;
                                        networks = ecoent;
                                    }
                                    else {
                                        log_error("econet: out of memory for network table");
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                if (EconetStationNumber == 0) {
                    log_error("Econet: Failed to find free station/port to bind to");
                    WSACleanup();
                    return;
                }
            }
        }
        else {
            log_error("Econet: Failed to resolve local IP address");
            WSACleanup();
            return;
        }
#endif
    }
    log_debug("Econet: Station number set to %d, port %d", EconetStationNumber, EconetListenPort);

    /* On Master the station number is read from CMOS so update it */
    if (MASTER)
        cmos_set(0xe, EconetStationNumber);

    /* this call is what allows broadcast packets to be sent: */
    int broadcast = 1;
    if (setsockopt(UdpSocket, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof broadcast) == -1) {
        log_error("Econet: Failed to set socket for broadcasts: %s", econet_socket_errstr());
        closesocket(UdpSocket);
        WSACleanup();
        return;
    }
    SocketOpen = true;
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/* read to FE18.. */

uint8_t econet_read_station(void)
{
    log_debug("Econet: Read Station %02X", (int)EconetStationNumber);
    return (EconetStationNumber);
}


/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/* write to FEA0-3 */

static void econet_tx_copy(int start)
{
    size_t size = BeebTx.Pointer - start;
    memcpy(EconetTx.buff, BeebTx.buff + start, size);
    EconetTx.Pointer = size;
}

/*
 * econet_machine_type — the byte a real NFS ROM would return as the first
 * byte of a MachinePeek (cb &88/&08) reply, for the currently emulated model.
 * Used when faking a MachinePeek reply ourselves (i.e. without injecting the
 * immediate into the 6502/ADLC and letting the ROM answer for real).
 */
static uint8_t econet_machine_type(void)
{
    if (MASTER)
        return 0x05;  /* BBC Master 128 (OS 3) */
    return 0x01;      /* BBC Model B / B+ (OS 1 or 2) */
}

/*
 * econet_machine_version — BCD MOS version for the currently emulated model,
 * as the third/fourth bytes of a MachinePeek reply: lo holds the fractional
 * part (e.g. 0x60 for ".60") and hi the integer part (e.g. 0x03 for "3."),
 * giving 3.60; the Master returns 4.25.
 */
static void econet_machine_version(uint8_t *lo, uint8_t *hi)
{
    if (MASTER) {
        *lo = 0x25;
        *hi = 0x04;
    } else {
        *lo = 0x60;
        *hi = 0x03;
    }
}

static void econet_set_wait4idle(const char *dir, const char *reason)
{
    fourwaystage = FWS_WAIT4IDLE;
    EconetWait4IdleTrigger = 0;
    FlagFillActive = true;
    EconetFlagFillTimeoutTrigger = EconetCycles + EconetFlagFillTimeout;
    log_debug("Econet(%s): Set FWS_WAIT4IDLE (%s)", dir, reason);
}

/*
 * econet_tx_data — drain one byte from the ADLC TX FIFO into BeebTx.
 *
 * Called each poll when TX is not in reset.  Rate-limits draining to one byte
 * per EconetTimeBetweenBytes poll ticks, simulating the ADLC's on-wire byte
 * rate so the BBC ROM doesn't overflow the FIFO.
 *
 * When the TxLast flag is set on the byte being drained (meaning the ROM has
 * finished writing the frame), the complete BeebTx buffer is dispatched as a
 * single AUN UDP datagram.  The specific AUN type and FSM transition depend on
 * the fourwaystage and destination:
 *   - FWS_IDLE: the frame is a scout; schedule a fake ScoutAck and advance to
 *     FWS_SCOUTSENT (or FWS_IMMSENT for IMMEDIATE commands).
 *   - FWS_SCACKRCVD: the frame is the data payload; send it as AUN_TYPE_UNICAST
 *     (or BROADCAST / IMMEDIATE) and advance to FWS_DATASENT.
 *   - Destination station 0 (file-server stub): re-broadcast the data over UDP
 *     and schedule a fake final ACK rather than waiting for a real one.
 */
static void econet_tx_data(void)
{
    if (ADLC.txfptr && EconetCycles >= EconetTxByteTrigger) {
        EconetTxByteTrigger = EconetCycles + EconetTimeBetweenBytes;
        log_debug("Econet(Tx): Write to FIFO noticed");
        bool TXlast = false;
        if (ADLC.txftl & powers[ADLC.txfptr - 1])
            TXlast = true;                               /* TxLast set */
        if (BeebTx.Pointer + 1 > sizeof(BeebTx.buff) ||  /* overflow IP buffer */
            (ADLC.txfptr > 4)) {                         /* overflowed fifo */
            ADLC.status1 |= ADLC_STA1_TX_UNDER;          /* set tx underrun flag */
            BeebTx.Pointer = 0;                          /* wipe buffer */
            BeebTx.BytesInBuffer = 0;
            ADLC.txfptr = 0;
            ADLC.txftl = 0;
            log_debug("Econet(Tx): TxUnderun!!");
        }
        else {
            BeebTx.buff[BeebTx.Pointer] = ADLC.txfifo[--ADLC.txfptr];
            BeebTx.Pointer++;
        }
        if (TXlast) {  /* TxLast set */
            log_debug("Econet(Tx): TXLast buff[0..7]=%02X %02X %02X %02X %02X %02X %02X %02X len=%u fws=%d",
                BeebTx.buff[0], BeebTx.buff[1], BeebTx.buff[2], BeebTx.buff[3],
                BeebTx.buff[4], BeebTx.buff[5], BeebTx.buff[6], BeebTx.buff[7],
                BeebTx.Pointer, fourwaystage);
            log_debug("Econet(Tx): TXLast set - Send packet to %02x %02x ", (unsigned int)(BeebTx.eh.destnet), (unsigned int)BeebTx.eh.deststn);

            /* first two bytes of BeebTx.buff contain the destination address
             * (or one zero byte for broadcast) */

            struct sockaddr_in RecvAddr;
            bool SendMe = false;
            int SendLen;
            struct ECOLAN *ecoent = networks;
            if (confAUNmode && (BeebTx.eh.deststn == 255 || BeebTx.eh.deststn == 0)) {  /* broadcast! */
                /* TODO something
                 * Somewhere that I cannot now find suggested that
                 * aun buffers broadcast packet, and broadcasts a simple flag. stations
                 * poll us to get the actual broadcast data ..
                 * Hmmm...
                 *
                 * ok, just send it to the local broadcast address.
                 * TODO lookup destnet in aunnet() and use proper ip address! */
                RecvAddr.sin_family = AF_INET;
                RecvAddr.sin_port = htons(32768);
                RecvAddr.sin_addr.s_addr = INADDR_BROADCAST;  /* ((EconetListenIP & 0x00FFFFFF) | 0xFF000000) ; */
                SendMe = true;
            }
            else {
                while (ecoent) {
                    /* does the packet match this network table entry? */
    /* SendMe = false;
     * // check for 0.stn and mynet.stn. */
                    /* aunnet wont be populted if not in aun mode, but we don't need to not check
                     * it because it won't matter.. */
                    if (ecoent->station == (unsigned int)(BeebTx.eh.deststn) &&
                       (ecoent->network == (unsigned int)(BeebTx.eh.destnet) || (myaunnet && ecoent->network == myaunnet->network))) {
                        SendMe = true;
                        break;
                    }
                    ecoent = ecoent->next;
                }
                /* guess address if not found in table */
                if (!SendMe && confSTRICT) {  /* didn't find it and allowed to guess */
                    log_debug("Econet(Tx): Send to unknown host; make assumptions & add entry!");
                    if (BeebTx.eh.destnet == 0 || (myaunnet && BeebTx.eh.destnet == myaunnet->network)) {
                        ecoent = malloc(sizeof(struct ECOLAN));
                        if (ecoent) {
                            /* myaunnet may not be set yet (e.g. AUNMap is parsed before
                             * EconetListenIP is known), even when destnet==0.  Fall back
                             * to our own listening address's /24 for the network prefix. */
                            uint32_t netprefix = myaunnet ? ntohl(myaunnet->inet_addr.s_addr)
                                                           : (ntohl(EconetListenIP) & 0xffffff00);
                            ecoent->next = networks;
                            ecoent->inet_addr.s_addr = htonl(netprefix | (BeebTx.eh.deststn & 0xff));
                            ecoent->port = 32768;  /* default AUN port */
                            ecoent->network = BeebTx.eh.destnet;
                            ecoent->station = BeebTx.eh.deststn;
                            networks = ecoent;
                            SendMe = true;
                        }
                        else
                            log_error("econet: out of memory for network table");
                    }
                    else {
                        for (struct AUNTAB *aunent = aunnet; aunent; aunent = aunent->next) {
                            if (aunent->network == BeebTx.eh.destnet) {
                                ecoent = malloc(sizeof(struct ECOLAN));
                                if (ecoent) {
                                    ecoent->next = networks;
                                    ecoent->inet_addr.s_addr = htonl(ntohl(aunent->inet_addr.s_addr) | (BeebTx.eh.deststn & 0xff));
                                    ecoent->port = 32768;  /* default AUN port */
                                    ecoent->network = BeebTx.eh.destnet;
                                    ecoent->station = BeebTx.eh.deststn;
                                    networks = ecoent;
                                    SendMe = true;
                                    log_debug("Econet(Tx): STRICT: net=%u stn=%u -> %s:%u (from AddMap)",
                                             BeebTx.eh.destnet, BeebTx.eh.deststn,
                                             inet_ntoa(ecoent->inet_addr), ecoent->port);
                                }
                                else
                                    log_error("econet: out of memory for network table");
                                break;
                            }
                        }
                        if (!ecoent)
                            log_debug("Econet(Tx): STRICT: no AddMap entry for net=%u stn=%u (aunnet=%s)",
                                     BeebTx.eh.destnet, BeebTx.eh.deststn, aunnet ? "populated" : "empty");
                    }
                }
                if (ecoent) {
                    RecvAddr.sin_family = AF_INET;
                    RecvAddr.sin_port = htons(ecoent->port);
                    RecvAddr.sin_addr = ecoent->inet_addr;
                }
            }

            /* TODO */
            log_debug("Econet(Tx): TXLast set - Send %d byte packet to %02x %02x (%s:%u)",
                      BeebTx.Pointer, (unsigned int)(BeebTx.eh.destnet), (unsigned int)BeebTx.eh.deststn, inet_ntoa(RecvAddr.sin_addr), (unsigned int)ntohs(RecvAddr.sin_port));
            log_dump("Econet(Tx): Econet packet: ", BeebTx.buff, BeebTx.Pointer);
    /*                  if (confAUNmode && fourwaystage != FWS_IDLE) {
                if (RecvAddr.sin_port != EconetTx.inet_addr ||
                    RecvAddr.sin_port != htons(EconetTx.port) ) {
                        EconetError("Erm.. trying to send somewhere while in the middle of talking to somewhere else.");
                }
            }
    */
            /* Send a datagram to the receiver */
            if (confAUNmode) {
                /* OK. Lets do AUN ...
                 * The beeb has given us a packet .. what is it? */
                SendMe = false;
                switch (fourwaystage) {
                    case FWS_SCACKRCVD:
                        log_debug("Econet(Tx): AUN mode, in state FWS_SCACKRCVD");
                        /* it came in response to our ack of a scout
                         * what we have /should/ be the data block ..
                         * CLUDGE WARNING is this a scout sent again immediately?? TODO fix this?!?! */
                        if (BeebTx.Pointer != sizeof(BeebTx.eh) || memcmp(BeebTx.buff, BeebTxCopy, sizeof(BeebTx.eh)) != 0) {  /* nope */
                            /* Extended-scout ports (NOTIFY 0x85, printer 0x83-0x84): prepend the
                             * 4 scout_ext bytes saved from the original scout frame so the AUN
                             * UNICAST payload is [scout_ext][data], as the receiver expects. */
                            if (EconetTx.ah.port == 0 && EconetTx.ah.cb >= 3 && EconetTx.ah.cb <= 5) {
                                int data_len = BeebTx.Pointer - 4;
                                if (data_len < 0) data_len = 0;
                                memcpy(EconetTx.buff, BeebTxScoutExt, 4);
                                memcpy(EconetTx.buff + 4, BeebTx.buff + 4, data_len);
                                EconetTx.Pointer = 4 + data_len;
                            } else {
                                econet_tx_copy(4);
                            }
                            fourwaystage = FWS_DATASENT;
                            log_debug("Econet(Tx): Set FWS_DATASENT");
                            SendMe = true;
                            SendLen = sizeof(EconetTx.ah) + EconetTx.Pointer;
                            break;
                        }  /* else fall through... */
                    case FWS_IDLE:
                        log_debug("Econet(Tx): AUN mode, in state FWS_IDLE");
                        /* not currently doing anything. */
                        memcpy(BeebTxCopy, BeebTx.buff, sizeof(BeebTx.eh));
                        /* maybe a long scout or a broadcast */
                        EconetTx.ah.cb = (unsigned int)(BeebTx.eh.cb) & 127;  /* | 128; */
                        EconetTx.ah.port = (unsigned int)(BeebTx.eh.port);
                        EconetTx.ah.pad = 0;
                        EconetTx.ah.handle = (ec_sequence += 4);
                        /* Save extended-scout bytes for NOTIFY/printer (PORT & 0x7F in [3..5]) */
                        if (EconetTx.ah.cb >= 3 && EconetTx.ah.cb <= 5 && BeebTx.Pointer >= 10)
                            memcpy(BeebTxScoutExt, BeebTx.buff + 6, 4);
                        else
                            memset(BeebTxScoutExt, 0, 4);

                        EconetTx.destnet = BeebTx.eh.destnet | outmask;  /* 30JUN */
                        EconetTx.deststn = BeebTx.eh.deststn;
                        econet_tx_copy(6);
                        if (EconetTx.deststn == 255) {
                            EconetTx.ah.type = AUN_TYPE_BROADCAST;
                            econet_set_wait4idle("Tx", "broadcast snt");
                            SendMe = true;  /* send packet ... */
                            SendLen = sizeof(EconetTx.ah) + 8;
                        }
                        else if (EconetTx.ah.port == 0 && (EconetTx.ah.cb < 2 || EconetTx.ah.cb > 5)) {
                            EconetTx.ah.type = AUN_TYPE_IMMEDIATE;
                            fourwaystage = FWS_IMMSENT;
                            log_debug("Econet(Tx): Set FWS_IMMSENT");
                            SendMe = true;  /* send packet ... */
                            SendLen = sizeof(EconetTx.ah) + EconetTx.Pointer;
                            /* Schedule a fallback fake IMM_REPLY in case nothing replies in
                             * time. Use a generous timeout so bridge-connected real FSes have
                             * time to send the genuine reply first — if it arrives, the
                             * fws==IMMSENT guard in the timer handler makes the fake a no-op. */
                            EconetFakeImmReplytrigger = EconetCycles + EconetFlagFillTimeout / 4;
                            EconetFakeImmReplySrcStn = EconetTx.deststn;
                            EconetFakeImmReplySrcNet = EconetTx.destnet;
                        }
                        else {
                            EconetTx.ah.type = AUN_TYPE_UNICAST;
                            fourwaystage = FWS_SCOUTSENT;
                            log_debug("Econet(Tx): Set FWS_SCOUTSENT");
                            /* dont send anything but set wait anyway */
                            EconetSCACKtrigger = EconetCycles + EconetSCACKtimeout;
                            log_debug("Econet(Tx): SCACKtimer set");
                            FlagFillActive = true;
                            EconetFlagFillTimeoutTrigger = EconetCycles + EconetFlagFillTimeout;
                            log_debug("Econet(Tx): FlagFill set (between tx scout and ack)");
                        }
                        break;
                    case FWS_SCOUTRCVD:
                        log_debug("Econet(Tx): AUN mode, in state FWS_SCOUTRCVD");
                        /* it's an ack for a scout which we sent the beeb. just drop it, but move on. */
                        fourwaystage = FWS_SCACKSENT;
                        log_debug("Econet(Tx): Set FWS_SCACKSENT");
                        EconetSCACKtrigger = EconetCycles + EconetSCACKtimeout;
                        log_debug("Econet(Tx): SCACKtimer set");
                        FlagFillActive = true;
                        EconetFlagFillTimeoutTrigger = EconetCycles + EconetFlagFillTimeout;
                        log_debug("Econet(Tx): FlagFill set (between rx scout ack and data)");
                        break;
                    case FWS_DATARCVD:
                        log_debug("Econet(Tx): AUN mode, in state FWS_DATARCVD");
                        /* this must be ack for data just receved
                         * now we really need to send an ack to the far AUN host...
                         * send header of last block received straight back.
                         * this ought to work, but only because the beeb can only talk to one machine at any time.. */
                        SendLen = sizeof(EconetRx.ah);
                        EconetTx.ah = EconetRx.ah;
                        EconetTx.ah.type = AUN_TYPE_ACK;
                        if (EconetFakeResponseSuppressAck) {
                            /* Injected fake response: the FS already got its
                             * real AUN ACK when the response was queued, so
                             * don't send this internally-generated ACK
                             * (sequence 0) back over the wire. */
                            EconetFakeResponseSuppressAck = false;
                            log_debug("Econet(Tx): suppressing spurious final ACK for injected FS response");
                        } else {
                            SendMe = true;
                        }
                        econet_set_wait4idle("Tx", "final ack sent");
                        break;
                    case FWS_IMMRCVD:
                        log_debug("Econet(Tx): AUN mode, in state FWS_IMMRCVD");
                        /* it's a reply to an immediate command we just had */
                        econet_set_wait4idle("Tx", "imm rcvd");
                        econet_tx_copy(4);
                        EconetTx.ah = EconetRx.ah;
                        EconetTx.ah.type = AUN_TYPE_IMM_REPLY;
                        SendMe = true;
                        SendLen = sizeof(EconetTx.ah) + EconetTx.Pointer;
                        break;
                    default:
                        econet_set_wait4idle("Tx", "invalid 4-way state");
                }
            }
            if (SendMe) {
                if (confAUNmode) {
                    if (EconetTx.ah.type == AUN_TYPE_BROADCAST) {
                        /* Stations on custom ports won't receive 255.255.255.255:32768.
                         * Send individually to each known station at its registered port. */
                        for (struct ECOLAN *bcast_ent = networks; bcast_ent; bcast_ent = bcast_ent->next) {
                            if (bcast_ent->station == EconetStationNumber)
                                continue;
                            struct sockaddr_in baddr;
                            baddr.sin_family = AF_INET;
                            baddr.sin_port = htons(bcast_ent->port);
                            baddr.sin_addr = bcast_ent->inet_addr;
                            log_debug("Econet(Tx): AUN type=%u port=%02X cb=%02X stn=%u->%u to %s:%u fws=%d",
                                EconetTx.ah.type, EconetTx.ah.port, EconetTx.ah.cb,
                                EconetStationNumber, EconetTx.deststn,
                                inet_ntoa(baddr.sin_addr), ntohs(baddr.sin_port), fourwaystage);
                            if (sendto(UdpSocket, (char *)&EconetTx, SendLen, 0, (SOCKADDR *)&baddr, sizeof(baddr)) == SOCKET_ERROR)
                                log_error("Econet(Tx): Failed to broadcast to stn=%u (%s:%u)",
                                    bcast_ent->station, inet_ntoa(bcast_ent->inet_addr), bcast_ent->port);
                        }
                    } else {
                    log_debug("Econet(Tx): AUN type=%u port=%02X cb=%02X stn=%u->%u to %s:%u fws=%d", EconetTx.ah.type, EconetTx.ah.port, EconetTx.ah.cb, EconetStationNumber, EconetTx.deststn, inet_ntoa(RecvAddr.sin_addr), ntohs(RecvAddr.sin_port), fourwaystage);
                    log_dump("Econet(Tx): AUN Packet: ", (uint8_t *)&EconetTx, SendLen);
                    if (sendto(UdpSocket, (char *)&EconetTx, SendLen, 0, (SOCKADDR *) &RecvAddr, sizeof(RecvAddr)) == SOCKET_ERROR) {
                        log_error("Econet(Tx): Failed to send packet to %02x %02x (%s:%u)",
                                  (unsigned int)(BeebTx.eh.destnet), (unsigned int)BeebTx.eh.deststn, inet_ntoa(RecvAddr.sin_addr), (unsigned int)ntohs(RecvAddr.sin_port));
                    }
                    }
                    FlagFillActive = true;
                    EconetFlagFillTimeoutTrigger = EconetCycles + EconetFlagFillTimeout;
                    log_debug("Econet(Tx): FlagFill set (packet sent)");
                }
                else {
                    log_debug("Econet(Tx): Sending a non-AUN packet, BeebTx.Pointer=%d", BeebTx.Pointer);
                    log_dump("Econet(Tx): BeebEm Packet: ", BeebTx.buff, BeebTx.Pointer);
                    if (sendto(UdpSocket, (char *)BeebTx.buff, BeebTx.Pointer, 0, (SOCKADDR *) &RecvAddr, sizeof(RecvAddr)) == SOCKET_ERROR) {
                        log_error("Econet(Tx): Failed to send packet to %02x %02x (%s:%u)",
                                  (unsigned int)(BeebTx.eh.destnet), (unsigned int)BeebTx.eh.deststn, inet_ntoa(RecvAddr.sin_addr), (unsigned int)ntohs(RecvAddr.sin_port));
                    }
                    /* If we have just sent a packet then then a real peer
                     * would probably go into flag-fill.  The exception is
                     * when the just sent a data ack packet which is the
                     * end of the four-way handshake.
                     */
                    if (BeebTx.Pointer != 4 || lastrxlen <= 6) {
                        FlagFillActive = true;
                        EconetFlagFillTimeoutTrigger = EconetCycles + EconetFlagFillTimeout;
                        log_debug("Econet(Tx): FlagFill set (packet sent)");
                    }
                    else
                        log_debug("Econet(Tx): skipping flag-fill");
                }
                BeebTx.Pointer = 0;  /* wipe buffer */
                BeebTx.BytesInBuffer = 0;
                econet_adlc_debug();
            }
        }
    }
}

/*
 * econet_rx_copy — copy EconetRx payload into BeebRx at a given byte offset.
 *
 * Strips the 8-byte AUN header (sizeof EconetRx.ah) from the received datagram
 * and places the remaining payload at BeebRx.buff[start].  Used to build a
 * traditional Econet frame in BeebRx: the caller writes the 4- or 6-byte Econet
 * header (dest/src stn+net, optional cb+port) into the leading bytes, then calls
 * this to append the AUN payload.
 */
static void econet_rx_copy(int start, int bytes)
{
    size_t size = bytes - sizeof(EconetRx.ah);
    memcpy(BeebRx.buff + start, EconetRx.buff, size);
    BeebRx.BytesInBuffer = size + start;
    lastrxlen = size;
}

static int rxdelay = -1;
static int post_release_log = 0;

/*
 * econet_rx_data — push the next RX byte into the ADLC FIFO, or fetch a new packet.
 *
 * Gate logic (early returns that block progress):
 *   - RTS asserted in CR2: the BBC micro is transmitting; do not inject received data.
 *   - rxdelay > 0: a brief hold-off after a new packet is queued; counts down each
 *     poll, then clears FlagFillActive when it reaches zero.
 *
 * If BeebRx has bytes remaining, one byte is pushed into the ADLC RX FIFO per call.
 * The Frame Valid (FV) bit is set on the last byte so the ROM knows the frame is
 * complete.
 *
 * When BeebRx is empty, recvfrom() is called on the UDP socket (non-blocking).
 * On success the AUN datagram is parsed: ACK/NACK packets update FSM state;
 * UNICAST/BROADCAST/IMMEDIATE packets are injected into the FSM as a fake scout
 * (FWS_SCOUTRCVD) and the full BeebRx frame is built with econet_rx_copy().
 *
 * The FWS_SCACKSENT → FWS_DATARCVD transition is also handled here (after the
 * EconetSCACKtrigger delay), delivering the saved AUN payload to the BBC.
 */
static void econet_rx_data(void)
{
    if (post_release_log > 0) {
        post_release_log--;
        log_debug("Econet(Rx): post-release[%d]: RTS=%d rxfp=%d BIB=%u ptr=%u FV=%d fws=%d ctl1=%02X ctl2=%02X",
            post_release_log,
            !!(ADLC.control2 & ADLC_CTL2_RTS),
            ADLC.rxfptr, BeebRx.BytesInBuffer, BeebRx.Pointer,
            !!(ADLC.status2 & ADLC_STA2_FRAME_VAL),
            fourwaystage, ADLC.control1, ADLC.control2);
    }
    if (ADLC.control2 & ADLC_CTL2_RTS) {
        static int rts_count = 0;
        if (rts_count++ == 0)
            log_debug("Econet(Rx): RTS asserted, blocking RX fws=%d ctl1=%02X ctl2=%02X", fourwaystage, ADLC.control1, ADLC.control2);
        return;
    }
    if (rxdelay >= 0) {
        if (rxdelay == 0) {
            log_debug("Econet(Rx): rx packet release, flag fill reset");
            FlagFillActive = false;
            rxdelay = -1;
            post_release_log = 20;
        }
        else {
            log_debug("Econet(Rx): holding off rx, rx delay=%u fws=%d", rxdelay, fourwaystage);
            rxdelay--;
            if (FlagFillActive && !EconetWaitingForBridgeResp)
                EconetFlagFillTimeoutTrigger = EconetCycles + EconetFlagFillTimeout;
            return;
        }
    }
    if (BeebRx.Pointer < BeebRx.BytesInBuffer) {
        /* something waiting to be given to the processor */
        if (ADLC.rxfptr < 3) {  /* space in fifo */
            log_debug("Econet(Rx): Time to give another byte to the beeb");
            ADLC.rxfifo[2] = ADLC.rxfifo[1];
            ADLC.rxfifo[1] = ADLC.rxfifo[0];
            ADLC.rxfifo[0] = BeebRx.buff[BeebRx.Pointer];
            ADLC.rxfptr++;
            ADLC.rxffc = (ADLC.rxffc << 1) & 7;
            ADLC.rxap = (ADLC.rxap << 1) & 7;
            if (BeebRx.Pointer == 0)
                ADLC.rxap |= 1;  /* 2 bytes? adr extention mode */
            BeebRx.Pointer++;
            if (BeebRx.Pointer >= BeebRx.BytesInBuffer) {
                ADLC.rxffc |= 1;
                BeebRx.Pointer = 0;
                BeebRx.BytesInBuffer = 0;
            }
        }
    }
    if (ADLC.rxfptr == 0) {
        /* still nothing in buffers (and thus nothing in Econetrx buffer) */
        ADLC.control1 &= ~ADLC_CTL1_RX_DISC;  /* reset discontinue flag */
        /* wait for cpu to clear FV flag from last frame received */
        if ((ADLC.status2 & ADLC_STA2_FRAME_VAL) && BeebRx.BytesInBuffer == 0) {
            static int fv_count = 0;
            static int fv_fws = -1;
            if (fv_count++ == 0 || fv_fws != fourwaystage) {
                fv_fws = fourwaystage;
                log_debug("Econet(Rx): gate blocked waiting for CPU to clear FV fws=%d ctl1=%02X ctl2=%02X s1=%02X s2=%02X",
                    fourwaystage, ADLC.control1, ADLC.control2, ADLC.status1, ADLC.status2);
            }
        }
        if (!(ADLC.status2 & ADLC_STA2_FRAME_VAL) && BeebRx.BytesInBuffer == 0) {
            if (confAUNmode && fourwaystage != FWS_IDLE && fourwaystage != FWS_IMMSENT && fourwaystage != FWS_DATASENT && fourwaystage != FWS_WAIT4IDLE) {
                static int blocked_state_count = 0;
                if (blocked_state_count++ == 0)
                    log_debug("Econet(Rx): recvfrom blocked by fourwaystage=%d", fourwaystage);
            }
            if (!confAUNmode || fourwaystage == FWS_IDLE || fourwaystage == FWS_IMMSENT || fourwaystage == FWS_DATASENT || fourwaystage == FWS_WAIT4IDLE) {
                log_debug("Econet(Rx): recvfrom gate open fws=%d", fourwaystage);
                /* Try and get another packet from network
                 * Check if packet is waiting without blocking */
                int RetVal;
                struct sockaddr_in RecvAddr;
                /* Read the packet */
                int sizRcvAdr = sizeof(RecvAddr);
                if (confAUNmode) {
                    RetVal = recvfrom(UdpSocket, (char *)EconetRx.raw, sizeof(EconetRx), 0, (SOCKADDR *) & RecvAddr, (socklen_t *)&sizRcvAdr);
                    EconetRx.BytesInBuffer = RetVal;
                }
                else {
                    RetVal = recvfrom(UdpSocket, (char *)BeebRx.buff, sizeof(BeebRx.buff), 0, (SOCKADDR *) & RecvAddr, (socklen_t *)&sizRcvAdr);
                }
                if (RetVal > 0) {
                    log_debug("Econet(Rx): Packet received, %u bytes from %s:%u fws=%d", (int)RetVal, inet_ntoa(RecvAddr.sin_addr), htons(RecvAddr.sin_port), fourwaystage);
                    if (confAUNmode) {
                        log_dump("Econet(Rx): AUN packet: ", EconetRx.raw, RetVal);

                        /* convert from AUN format
                         * find station number of sender */
                        struct ECOLAN *host;
                        bool foundhost = false;
                        struct ECOLAN *iponly_host = NULL;
                        for (host = networks; host; host = host->next) {
                            if (RecvAddr.sin_addr.s_addr == host->inet_addr.s_addr) {
                                if (RecvAddr.sin_port == htons(host->port)) {
                                    foundhost = true;
                                    break;
                                } else if (!iponly_host) {
                                    iponly_host = host;
                                }
                            }
                        }
                        /* AUN replies may come from a different source port (e.g. PiFS replies
                         * from port 10000+stn rather than 32768) — fall back to IP-only match. */
                        if (!foundhost && iponly_host) {
                            host = iponly_host;
                            foundhost = true;
                        }
                        if (!foundhost) {
                            /* packet from unknown host */
                            if (confLEARN) {
                                uint8_t station = ntohl(RecvAddr.sin_addr.s_addr) & 0xff;
                                uint8_t network = 0;
                                for (struct AUNTAB *aunent = aunnet; aunent; aunent = aunent->next) {
                                    if (aunent->inet_addr.s_addr == htonl(ntohl(RecvAddr.sin_addr.s_addr) & 0xffffff00)) {
                                        network = aunent->network;
                                        break;
                                    }
                                }
                                log_debug("Econet(Rx): Previusly unknown IP, Econet %u:%u", network, station);
                                for (struct ECOLAN *entry = networks; entry; entry = entry->next) {
                                    if (entry->network == network && entry->station == station) {
                                        host = entry;
                                        break;
                                    }
                                }
                                if (host) {
                                    log_debug("Econet(Rx): updating existing entry %s:%u", inet_ntoa(host->inet_addr), host->port);
                                    host->port = ntohs(RecvAddr.sin_port);
                                    host->inet_addr = RecvAddr.sin_addr;
                                    foundhost = true;
                                }
                                else {
                                    host = malloc(sizeof(struct ECOLAN));
                                    if (host) {
                                        host->next = networks;
                                        host->port = ntohs(RecvAddr.sin_port);
                                        host->inet_addr = RecvAddr.sin_addr;
                                        host->station = station;
                                        host->network = network;
                                        log_debug("Econet(Rx): adding new entry %u:%u -> %s:%u", network, station, inet_ntoa(host->inet_addr), host->port);
                                        foundhost = true;
                                    }
                                    else
                                        log_error("econet: out of memory for network table");
                                }
                            }
                            else {
                                /* ignore it.. */
                                log_debug("Econet(Rx): Packet ignored");
                            }
                        }

                        if (!foundhost) {              /* didn't find it in the table .. */
                            BeebRx.BytesInBuffer = 0;  /* ignore the packet */
                        }
                        else {
                            log_debug("Econet(Rx): AUN type=%u port=%02X cb=%02X from stn=%u fws=%d", EconetRx.ah.type, EconetRx.ah.port, EconetRx.ah.cb, host->station, fourwaystage);
                            if (EconetRx.ah.type == AUN_TYPE_UNICAST &&
                                EconetLastUnicastValid &&
                                host->station == EconetLastUnicastStn &&
                                host->network == EconetLastUnicastNet &&
                                EconetRx.ah.port == EconetLastUnicastPort &&
                                EconetRx.ah.handle == EconetLastUnicastHandle) {
                                /* Sender's ACK-wait timer fired before our ACK arrived and it
                                 * retransmitted the same Unicast.  Re-ACK it but don't deliver
                                 * it to the Beeb again — it's already been processed. */
                                struct aunhdr dup_ack;
                                memset(&dup_ack, 0, sizeof(dup_ack));
                                dup_ack.type   = AUN_TYPE_ACK;
                                dup_ack.port   = EconetRx.ah.port;
                                dup_ack.handle = EconetRx.ah.handle;
                                sendto(UdpSocket, (const char *)&dup_ack, sizeof(dup_ack), 0,
                                       (SOCKADDR *)&RecvAddr, sizeof(RecvAddr));
                                log_debug("Econet(Rx): duplicate unicast port=%02X seq=%08X from stn=%u re-ACKed, not reprocessed",
                                          EconetRx.ah.port, EconetRx.ah.handle, host->station);
                                BeebRx.BytesInBuffer = 0;
                            } else {
                            if (EconetRx.ah.type == AUN_TYPE_UNICAST) {
                                EconetLastUnicastValid  = true;
                                EconetLastUnicastStn    = host->station;
                                EconetLastUnicastNet    = host->network;
                                EconetLastUnicastPort   = EconetRx.ah.port;
                                EconetLastUnicastHandle = EconetRx.ah.handle;
                            }
                            /* TODO - many of these copies can use memcpy() */
                            switch (fourwaystage) {
                                case FWS_IDLE:
                                    log_debug("Econet(Rx): received in FWS_IDLE");
                                    /* we weren't doing anything when this packet came in. */
                                    BeebRx.eh.srcstn = host->station;
                                    BeebRx.eh.srcnet = host->network;
                                    BeebRx.eh.deststn = EconetStationNumber;  /* must be for us. */
                                    BeebRx.eh.destnet = 0;
                                    /* BeebRx.eh.deststn = EconetRx.eh.deststn ; // 30jun was EconetStationNumber; // must be for us.
                                     * BeebRx.eh.destnet = EconetRx.eh.destnet & inmask ; // 30jun was 0 */
                                    BeebRx.eh.cb = EconetRx.ah.cb | 128;
                                    BeebRx.eh.port = EconetRx.ah.port;
                                    switch (EconetRx.ah.type) {

                                        case AUN_TYPE_BROADCAST:
                                            BeebRx.eh.deststn = 255;  /* wasn't just for us.. */
                                            BeebRx.eh.destnet = 0;
                                            econet_rx_copy(6, RetVal);
                                            /* No econet_set_wait4idle here: AUN has no real bus
                                             * so broadcasts don't need a quiet period, and
                                             * entering WAIT4IDLE would cause subsequent packets
                                             * (e.g. *NOTIFY) to be dropped. */
                                            log_debug("Econet(Rx): stn=%u: bcast from stn=%u cb=%02X port=%02X BIB=%u "
                                                "data[6..13]=%02X %02X %02X %02X %02X %02X %02X %02X",
                                                EconetStationNumber, host->station,
                                                EconetRx.ah.cb, EconetRx.ah.port, BeebRx.BytesInBuffer,
                                                BeebRx.BytesInBuffer > 6 ? BeebRx.buff[6] : 0,
                                                BeebRx.BytesInBuffer > 7 ? BeebRx.buff[7] : 0,
                                                BeebRx.BytesInBuffer > 8 ? BeebRx.buff[8] : 0,
                                                BeebRx.BytesInBuffer > 9 ? BeebRx.buff[9] : 0,
                                                BeebRx.BytesInBuffer > 10 ? BeebRx.buff[10] : 0,
                                                BeebRx.BytesInBuffer > 11 ? BeebRx.buff[11] : 0,
                                                BeebRx.BytesInBuffer > 12 ? BeebRx.buff[12] : 0,
                                                BeebRx.BytesInBuffer > 13 ? BeebRx.buff[13] : 0);
                                            break;
                                        case AUN_TYPE_IMMEDIATE:
                                            if (EconetWaitingForBridgeResp) {
                                                /* IMMEDIATE arrives after a real-Econet bridge
                                                 * ACK'd our TX (e.g. machine-type query from the
                                                 * FS via PiEconet bridge).  NFS has RX disabled
                                                 * at this point so ADLC injection would be lost.
                                                 * Reply on the Beeb's behalf and stay in FWS_IDLE
                                                 * so the login UNICAST that follows gets queued. */
                                                struct { struct aunhdr ah; uint8_t d[4]; } rpl;
                                                memset(&rpl, 0, sizeof(rpl));
                                                rpl.ah.type   = AUN_TYPE_IMM_REPLY;
                                                rpl.ah.port   = EconetRx.ah.port;
                                                rpl.ah.cb     = EconetRx.ah.cb;
                                                rpl.ah.handle = EconetRx.ah.handle;
                                                rpl.d[0] = econet_machine_type();
                                                rpl.d[1] = 0x00;
                                                econet_machine_version(&rpl.d[2], &rpl.d[3]);
                                                sendto(UdpSocket, (const char *)&rpl, sizeof(rpl), 0,
                                                       (SOCKADDR *)&RecvAddr, sizeof(RecvAddr));
                                                log_debug("Econet(Rx): FWS_IDLE/bridge: auto-replied to IMMEDIATE cb=%02X from stn=%u net=%u",
                                                         EconetRx.ah.cb, host->station, host->network);
                                                BeebRx.BytesInBuffer = 0;
                                            } else {
                                                econet_rx_copy(6, RetVal);
                                                fourwaystage = FWS_IMMRCVD;
                                                log_debug("Econet(Rx): Set FWS_IMMRCVD");
                                            }
                                            break;
                                        case AUN_TYPE_UNICAST:
                                            /* If we already have a response queued for injection
                                             * (e.g. PiFS is streaming a second data packet while
                                             * we wait for the inject delay on the first), don't
                                             * start a new 4-way directly — ACK PiFS and hold it
                                             * in the Response2 slot for sequential delivery. */
                                            if (confAUNmode
                                                && EconetFakeResponseSrcStn != 0
                                                && host->station == EconetFakeResponseSrcStn
                                                && host->network == EconetFakeResponseSrcNet
                                                && !(EconetRx.ah.port == 0x00 && (EconetRx.ah.cb & 0x7F) >= 0x03 && (EconetRx.ah.cb & 0x7F) <= 0x05)) {
                                                /* Always queue UNICASTs from the known FS station so
                                                 * the 500-poll inject delay lets ANFS settle its
                                                 * dispatch table ($0406/$0407) back to 0000 before
                                                 * the fake scout arrives.  This covers both the
                                                 * "response1 still pending" case and the case where
                                                 * PiFS pushes the next data block (port=0x92) after
                                                 * the previous delivery already completed. */
                                                struct aunhdr hold_ack;
                                                memset(&hold_ack, 0, sizeof(hold_ack));
                                                hold_ack.type   = AUN_TYPE_ACK;
                                                hold_ack.port   = EconetRx.ah.port;
                                                hold_ack.handle = EconetRx.ah.handle;
                                                sendto(UdpSocket, (const char *)&hold_ack, sizeof(hold_ack), 0,
                                                       (SOCKADDR *)&RecvAddr, sizeof(RecvAddr));
                                                if (EconetRespQLen < ECONET_RESPQ_DEPTH) {
                                                    int plen = RetVal - (int)sizeof(EconetRx.ah);
                                                    if (plen < 0) plen = 0;
                                                    if (plen > ECONET_RESP_BUFF_MAX)
                                                        plen = ECONET_RESP_BUFF_MAX;
                                                    struct econet_queued_resp *q = &EconetRespQ[EconetRespQTail];
                                                    memcpy(q->buff, EconetRx.buff, plen);
                                                    q->len        = plen;
                                                    q->reply_port = EconetRx.ah.port;
                                                    q->cb         = EconetRx.ah.cb;
                                                    q->src_stn    = host->station;
                                                    q->src_net    = host->network;
                                                    EconetRespQTail = (EconetRespQTail + 1) % ECONET_RESPQ_DEPTH;
                                                    EconetRespQLen++;
                                                    /* Do NOT clear EconetWaitingForBridgeResp here.
                                                     * We keep FlagFillActive=true until the scout is
                                                     * actually injected so ANFS2 never sees an idle
                                                     * bus (which would cause it to clear $0406/$0407). */
                                                    log_debug("Econet(Rx): FWS_IDLE: unicast port=%02X len=%d from FS stn=%u queued (pending=%d qlen=%d)",
                                                        EconetRx.ah.port, plen, host->station, EconetFakeResponsePending, EconetRespQLen);
                                                } else {
                                                    log_debug("Econet(Rx): FWS_IDLE: unicast port=%02X from FS stn=%u dropped (response queue full qlen=%d)",
                                                        EconetRx.ah.port, host->station, EconetRespQLen);
                                                }
                                                BeebRx.BytesInBuffer = 0;
                                                break;
                                            }
                                            /* Normal path: no pending injection, treat as incoming
                                             * 4-way scout.
                                             *
                                             * Exception: if we're mid-transaction waiting for a
                                             * specific FS's response, UNICASTs from other stations
                                             * must be discarded.  They arrive every second from other
                                             * AUN hosts on the same bridged network and, if allowed
                                             * through, corrupt ANFS2's NMI dispatch vector
                                             * ($0406/$0407) so it can't receive the real FS reply.
                                             * Guard on EconetWaitingForBridgeResp OR a queued/pending
                                             * response — do NOT check EconetFakeResponseSrcStn alone,
                                             * because that stays set after a transaction completes and
                                             * would incorrectly block unrelated incoming scouts. */
                                            if (confAUNmode &&
                                                (EconetWaitingForBridgeResp ||
                                                 EconetFakeResponsePending  ||
                                                 EconetRespQLen > 0) &&
                                                EconetFakeResponseSrcStn != 0) {
                                                struct aunhdr discard_ack;
                                                memset(&discard_ack, 0, sizeof(discard_ack));
                                                discard_ack.type   = AUN_TYPE_ACK;
                                                discard_ack.port   = EconetRx.ah.port;
                                                discard_ack.handle = EconetRx.ah.handle;
                                                sendto(UdpSocket, (const char *)&discard_ack, sizeof(discard_ack), 0,
                                                       (SOCKADDR *)&RecvAddr, sizeof(RecvAddr));
                                                log_debug("Econet(Rx): FWS_IDLE: discarding unicast from stn=%u net=%u (waiting for FS stn=%u net=%u)",
                                                         host->station, host->network,
                                                         EconetFakeResponseSrcStn, EconetFakeResponseSrcNet);
                                                BeebRx.BytesInBuffer = 0;
                                                break;
                                            }
                                            fourwaystage = FWS_SCOUTRCVD;
                                            log_debug("Econet(Rx): Set FWS_SCOUTRCVD");
                                            rx_scout_srcstn = host->station;
                                            rx_scout_srcnet = host->network;
                                            if (EconetRx.ah.port == 0x00 &&
                                                (EconetRx.ah.cb & 0x7F) >= 0x03 && (EconetRx.ah.cb & 0x7F) <= 0x05 &&
                                                EconetRx.BytesInBuffer >= (int)(sizeof(EconetRx.ah) + 4)) {
                                                /* Extended scout: copy first 4 payload bytes after the
                                                 * 6-byte header so the ANFS ROM sees source address data. */
                                                memcpy(BeebRx.buff + sizeof(BeebRx.eh), EconetRx.buff, 4);
                                                BeebRx.BytesInBuffer = (int)(sizeof(BeebRx.eh) + 4);
                                            } else {
                                                BeebRx.BytesInBuffer = sizeof(BeebRx.eh);
                                            }
                                            break;
                                        default:
                                            /* ignore anything else */
                                            BeebRx.BytesInBuffer = 0;
                                    }
                                    BeebRx.Pointer = 0;
                                    break;
                                case FWS_IMMSENT:  /* it should be reply to an immediate instruction */
                                    log_debug("Econet(Rx): received in FWS_IMMSENT");
                                    if (host->station != EconetTx.deststn ||
                                        host->network != EconetTx.destnet) {
                                        log_debug("Econet(Rx): FWS_IMMSENT: ignoring packet from stn=%u net=%u (expected %u net=%u)",
                                                 host->station, host->network, EconetTx.deststn, EconetTx.destnet);
                                        BeebRx.BytesInBuffer = 0;
                                        break;
                                    }
                                    if (EconetRx.ah.type == AUN_TYPE_UNICAST) {
                                        /* Bridge-style hosts may send unsolicited data packets
                                         * (e.g. FS status on port=0x90) while we wait for the
                                         * IMM_REPLY.  Queue them rather than misdelivering them
                                         * to ANFS as the machine-type immediate reply. */
                                        if (EconetRespQLen < ECONET_RESPQ_DEPTH) {
                                            int plen = RetVal - (int)sizeof(EconetRx.ah);
                                            if (plen < 0) plen = 0;
                                            if (plen > ECONET_RESP_BUFF_MAX) plen = ECONET_RESP_BUFF_MAX;
                                            struct econet_queued_resp *q = &EconetRespQ[EconetRespQTail];
                                            memcpy(q->buff, EconetRx.buff, plen);
                                            q->len        = plen;
                                            q->reply_port = EconetRx.ah.port;
                                            q->cb         = EconetRx.ah.cb;
                                            q->src_stn    = host->station;
                                            q->src_net    = host->network;
                                            EconetRespQTail = (EconetRespQTail + 1) % ECONET_RESPQ_DEPTH;
                                            EconetRespQLen++;
                                            EconetFakeResponseSrcStn = host->station;
                                            EconetFakeResponseSrcNet = host->network;
                                            log_debug("Econet(Rx): FWS_IMMSENT: FS unicast port=%02X len=%d queued (qlen=%d)",
                                                     EconetRx.ah.port, plen, EconetRespQLen);
                                        } else {
                                            log_debug("Econet(Rx): FWS_IMMSENT: FS unicast port=%02X dropped (queue full)",
                                                     EconetRx.ah.port);
                                        }
                                        BeebRx.BytesInBuffer = 0;
                                        break;
                                    }
                                    /* AUN_TYPE_IMM_REPLY (or ACK) — genuine response */
                                    log_debug("Econet(Rx): FWS_IMMSENT: IMM_REPLY type=%u from stn=%u net=%u",
                                             EconetRx.ah.type, host->station, host->network);
                                    BeebRx.eh.srcstn = host->station;
                                    BeebRx.eh.srcnet = host->network;
                                    BeebRx.eh.deststn = EconetStationNumber;  /* must be for us. */
                                    BeebRx.eh.destnet = 0;
                                    econet_rx_copy(4, RetVal);
                                    BeebRx.Pointer = 0;
                                    econet_set_wait4idle("Rx", "imm reply from remote AUN host");
                                    break;
                                case FWS_DATASENT:
                                    /* we sent block of data, awaiting final ack.. */
                                    if (EconetRx.ah.type == AUN_TYPE_ACK || EconetRx.ah.type == AUN_TYPE_NACK) {
                                        /* are we expecting a (N)ACK ?
                                         * TODO check it is a (n)ack for packet we just sent!!, deal with naks!
                                         * construct a final ack for the beeb */
                                        BeebRx.eh.srcstn = host->station;
                                        BeebRx.eh.srcnet = host->network;
                                        BeebRx.eh.deststn = EconetStationNumber;  /* must be for us. */
                                        BeebRx.eh.destnet = 0;
                                        BeebRx.BytesInBuffer = 4;
                                        BeebRx.Pointer = 0;
                                        /* Extended-scout ports (NOTIFY 0x85, printer 0x83-0x84) never
                                         * generate a follow-up UNICAST reply — only FS operations do.
                                         * Skip the bridge-response wait to avoid a ~10x-timeout stall. */
                                        if (!(EconetTx.ah.port == 0 && EconetTx.ah.cb >= 3 && EconetTx.ah.cb <= 5)) {
                                            /* Record the ACK sender as the response source so that
                                             * the real UNICAST reply (sent as a separate packet by
                                             * bridge-style AUN hosts after processing the request)
                                             * is correctly identified and queued at WAIT4IDLE/IDLE. */
                                            EconetFakeResponseSrcStn = host->station;
                                            EconetFakeResponseSrcNet = host->network;
                                            /* The bridge ACKs our packet immediately before the real FS
                                             * has processed it.  Signal that we're waiting for the FS's
                                             * UNICAST reply so the IDLE loop can keep FlagFillActive.
                                             * Set the timeout deadline now — the keepalive uses this
                                             * as a one-shot; it does NOT refresh it each poll. */
                                            EconetWaitingForBridgeResp = true;
                                            log_debug("Econet(Rx): final ACK from stn=%u net=%u to us=%u (FakeRespSrc updated, WaitingForBridgeResp)",
                                                     host->station, host->network, EconetStationNumber);
                                            /* Override the ×1 timeout set by econet_set_wait4idle with ×10.
                                             * The FS on the far side of a real Econet bridge can take
                                             * 20-30+ real seconds to reply; ×1 fires too soon. */
                                            EconetFlagFillTimeoutTrigger = EconetCycles + EconetFlagFillTimeout * 10;
                                        } else {
                                            log_debug("Econet(Rx): final ACK from stn=%u net=%u (ext-scout port, no bridge wait)",
                                                     host->station, host->network);
                                        }
                                        econet_set_wait4idle("Rx", "aun ack rxd");
                                        break;
                                    } else if (EconetRx.ah.type == AUN_TYPE_IMMEDIATE &&
                                               host->station == EconetTx.deststn &&
                                               host->network == EconetTx.destnet) {
                                        /* FS is querying us (e.g. machine type) while our TX is outstanding.
                                         * Reply with IMM_REPLY and stay in FWS_DATASENT. */
                                        struct { struct aunhdr ah; uint8_t d[4]; } rpl;
                                        memset(&rpl, 0, sizeof(rpl));
                                        rpl.ah.type   = AUN_TYPE_IMM_REPLY;
                                        rpl.ah.port   = EconetRx.ah.port;
                                        rpl.ah.cb     = EconetRx.ah.cb;
                                        rpl.ah.handle = EconetRx.ah.handle;
                                        rpl.d[0] = econet_machine_type();
                                        rpl.d[1] = 0x00;
                                        econet_machine_version(&rpl.d[2], &rpl.d[3]);
                                        sendto(UdpSocket, (const char *)&rpl, sizeof(rpl), 0,
                                               (SOCKADDR *)&RecvAddr, sizeof(RecvAddr));
                                        log_debug("Econet(Rx): FWS_DATASENT: replied to immediate cb=%02X from stn=%u, staying DATASENT",
                                                 EconetRx.ah.cb, host->station);
                                        BeebRx.BytesInBuffer = 0;
                                        break;
                                    } else if (EconetRx.ah.type == AUN_TYPE_UNICAST &&
                                               host->station == EconetTx.deststn &&
                                               host->network == EconetTx.destnet) {
                                        /* FS sent reply data without a prior ACK (e.g. PiFS).
                                         * Send AUN ACK back, queue the payload, give ANFS the TX final-ACK. */
                                        struct aunhdr ack;
                                        ack.type   = AUN_TYPE_ACK;
                                        ack.port   = EconetRx.ah.port;
                                        ack.cb     = 0;
                                        ack.pad    = 0;
                                        ack.handle = EconetRx.ah.handle;
                                        sendto(UdpSocket, (const char *)&ack, sizeof(ack), 0,
                                               (SOCKADDR *)&RecvAddr, sizeof(RecvAddr));
                                        int plen = RetVal - (int)sizeof(EconetRx.ah);
                                        if (plen < 0) plen = 0;
                                        if (plen > ECONET_RESP_BUFF_MAX)
                                            plen = ECONET_RESP_BUFF_MAX;
                                        memcpy(EconetFakeResponseBuff, EconetRx.buff, plen);
                                        EconetFakeResponseLen     = plen;
                                        EconetFakeResponseReplyPort = EconetRx.ah.port;
                                        EconetFakeResponseCb      = EconetRx.ah.cb;
                                        EconetFakeResponseSrcStn  = host->station;
                                        EconetFakeResponseSrcNet  = host->network;
                                        EconetFakeResponsePending = true;
                                        BeebRx.eh.srcstn  = host->station;
                                        BeebRx.eh.srcnet  = host->network;
                                        BeebRx.eh.deststn = EconetStationNumber;
                                        BeebRx.eh.destnet = 0;
                                        BeebRx.BytesInBuffer = 4;
                                        BeebRx.Pointer = 0;
                                        log_debug("Econet(Rx): FWS_DATASENT: FS stn=%u sent reply port=%02X len=%d without ACK, queuing",
                                                 host->station, EconetRx.ah.port, plen);
                                        econet_set_wait4idle("Rx", "FS unicast in DATASENT: final ACK to ANFS");
                                        break;
                                    }     /* else unexpected packet - ignore it.TODO: queue it? */
                                default:  /* erm, what are we doing here? */
                                    if (EconetRx.ah.type == AUN_TYPE_BROADCAST) {
                                        /* Broadcasts are deliverable in any state — deliver and
                                         * don't disturb the existing four-way handshake state. */
                                        BeebRx.eh.deststn = 255;
                                        BeebRx.eh.destnet = 0;
                                        BeebRx.eh.srcstn = host->station;
                                        BeebRx.eh.srcnet = host->network;
                                        BeebRx.eh.cb = EconetRx.ah.cb | 128;
                                        BeebRx.eh.port = EconetRx.ah.port;
                                        econet_rx_copy(6, RetVal);
                                        BeebRx.Pointer = 0;
                                        log_debug("Econet(Rx): stn=%u: bcast(nonidle) from stn=%u cb=%02X port=%02X BIB=%u fws=%d "
                                            "data[6..13]=%02X %02X %02X %02X %02X %02X %02X %02X",
                                            EconetStationNumber, host->station,
                                            EconetRx.ah.cb, EconetRx.ah.port, BeebRx.BytesInBuffer, fourwaystage,
                                            BeebRx.BytesInBuffer > 6 ? BeebRx.buff[6] : 0,
                                            BeebRx.BytesInBuffer > 7 ? BeebRx.buff[7] : 0,
                                            BeebRx.BytesInBuffer > 8 ? BeebRx.buff[8] : 0,
                                            BeebRx.BytesInBuffer > 9 ? BeebRx.buff[9] : 0,
                                            BeebRx.BytesInBuffer > 10 ? BeebRx.buff[10] : 0,
                                            BeebRx.BytesInBuffer > 11 ? BeebRx.buff[11] : 0,
                                            BeebRx.BytesInBuffer > 12 ? BeebRx.buff[12] : 0,
                                            BeebRx.BytesInBuffer > 13 ? BeebRx.buff[13] : 0);
                                    } else if (fourwaystage == FWS_WAIT4IDLE &&
                                               EconetRx.ah.type == AUN_TYPE_UNICAST &&
                                               EconetRespQLen < ECONET_RESPQ_DEPTH &&
                                               host->station == EconetFakeResponseSrcStn &&
                                               host->network == EconetFakeResponseSrcNet) {
                                        /* PiFS sent a further UNICAST (e.g. file data) while
                                         * still in WAIT4IDLE.  ACK PiFS immediately and queue
                                         * the payload for sequential delivery to ANFS. */
                                        int plen = RetVal - (int)sizeof(EconetRx.ah);
                                        if (plen < 0) plen = 0;
                                        if (plen > ECONET_RESP_BUFF_MAX)
                                            plen = ECONET_RESP_BUFF_MAX;
                                        struct econet_queued_resp *q = &EconetRespQ[EconetRespQTail];
                                        memcpy(q->buff, EconetRx.buff, plen);
                                        q->len        = plen;
                                        q->reply_port = EconetRx.ah.port;
                                        q->cb         = EconetRx.ah.cb;
                                        q->src_stn    = host->station;
                                        q->src_net    = host->network;
                                        EconetRespQTail = (EconetRespQTail + 1) % ECONET_RESPQ_DEPTH;
                                        EconetRespQLen++;
                                        struct aunhdr ack2;
                                        memset(&ack2, 0, sizeof(ack2));
                                        ack2.type   = AUN_TYPE_ACK;
                                        ack2.port   = EconetRx.ah.port;
                                        ack2.handle = EconetRx.ah.handle;
                                        sendto(UdpSocket, (const char *)&ack2, sizeof(ack2), 0,
                                               (SOCKADDR *)&RecvAddr, sizeof(RecvAddr));
                                        /* Keep EconetWaitingForBridgeResp true here too; clearing
                                         * it is deferred to the scout inject so FlagFillActive
                                         * stays asserted and ANFS2 keeps its $0406/$0407 dispatch
                                         * pointer valid until the scout actually arrives. */
                                        log_debug("Econet(Rx): FWS_WAIT4IDLE: FS stn=%u queued port=%02X len=%d (qlen=%d)",
                                                 host->station, EconetRx.ah.port, plen, EconetRespQLen);
                                        BeebRx.BytesInBuffer = 0;
                                    } else if (fourwaystage == FWS_WAIT4IDLE &&
                                               EconetRx.ah.type == AUN_TYPE_IMMEDIATE &&
                                               host->station == EconetFakeResponseSrcStn &&
                                               host->network == EconetFakeResponseSrcNet) {
                                        /* FS is querying us (e.g. machine type) while we're
                                         * waiting for its UNICAST reply.  Reply with IM_REPLY
                                         * and stay in WAIT4IDLE. */
                                        struct { struct aunhdr ah; uint8_t d[4]; } rpl;
                                        memset(&rpl, 0, sizeof(rpl));
                                        rpl.ah.type   = AUN_TYPE_IMM_REPLY;
                                        rpl.ah.port   = EconetRx.ah.port;
                                        rpl.ah.cb     = EconetRx.ah.cb;
                                        rpl.ah.handle = EconetRx.ah.handle;
                                        rpl.d[0] = econet_machine_type();
                                        rpl.d[1] = 0x00;
                                        econet_machine_version(&rpl.d[2], &rpl.d[3]);
                                        sendto(UdpSocket, (const char *)&rpl, sizeof(rpl), 0,
                                               (SOCKADDR *)&RecvAddr, sizeof(RecvAddr));
                                        log_debug("Econet(Rx): FWS_WAIT4IDLE: replied to immediate cb=%02X from stn=%u, staying WAIT4IDLE",
                                                 EconetRx.ah.cb, host->station);
                                        BeebRx.BytesInBuffer = 0;
                                    } else if (fourwaystage == FWS_WAIT4IDLE &&
                                               EconetRx.ah.type == AUN_TYPE_UNICAST &&
                                               EconetRx.ah.port == 0x00 &&
                                               (EconetRx.ah.cb & 0x7F) >= 0x03 && (EconetRx.ah.cb & 0x7F) <= 0x05 &&
                                               EconetRespQLen < ECONET_RESPQ_DEPTH) {
                                        /* An extended-scout transaction (e.g. the next *NOTIFY
                                         * character) arrived from another station while we were
                                         * still finishing the previous one's fake handshake.
                                         * These arrive faster than the fake handshake settles,
                                         * so queue it for replay once we're back in FWS_IDLE
                                         * rather than dropping it. */
                                        int plen = RetVal - (int)sizeof(EconetRx.ah);
                                        if (plen < 0) plen = 0;
                                        if (plen > ECONET_RESP_BUFF_MAX)
                                            plen = ECONET_RESP_BUFF_MAX;
                                        struct econet_queued_resp *q = &EconetRespQ[EconetRespQTail];
                                        memcpy(q->buff, EconetRx.buff, plen);
                                        q->len        = plen;
                                        q->reply_port = EconetRx.ah.port;
                                        q->cb         = EconetRx.ah.cb;
                                        q->handle     = EconetRx.ah.handle;
                                        q->src_stn    = host->station;
                                        q->src_net    = host->network;
                                        EconetRespQTail = (EconetRespQTail + 1) % ECONET_RESPQ_DEPTH;
                                        EconetRespQLen++;
                                        log_debug("Econet(Rx): FWS_WAIT4IDLE: ext scout port=%02X cb=%02X from stn=%u queued (qlen=%d)",
                                                 EconetRx.ah.port, EconetRx.ah.cb, host->station, EconetRespQLen);
                                        BeebRx.BytesInBuffer = 0;
                                    } else {
                                        econet_set_wait4idle("Rx", "unexpected 4-way state, packet ignored");
                                    }
                                    break;
                            }
                            }
                        }
                    }
                    else {
                        log_dump("Econet(Rx): BeebEm packet: ", BeebRx.buff, RetVal);
                        BeebRx.BytesInBuffer = RetVal;
                        BeebRx.Pointer = 0;
                        lastrxlen = RetVal;
                    }

                    if ((BeebRx.eh.deststn == EconetStationNumber || BeebRx.eh.deststn == 255 || BeebRx.eh.deststn == 0) && BeebRx.BytesInBuffer > 0) {
                        if (RetVal == 6)
                            rxdelay = 10;
                        else
                            rxdelay = 4;
                    }
                    else if (!confAUNmode) {
                        /* Two other stations communicating on real Econet wire - assume flag fill.
                         * Not applicable in AUN mode: no shared bus, dropped/unknown packets
                         * (e.g. Docker broadcasts) must not trigger spurious flag fill. */
                        FlagFillActive = true;
                        EconetFlagFillTimeoutTrigger = EconetCycles + EconetFlagFillTimeout;
                        log_debug("Econet(Rx): FlagFill set - other station comms");
                    }
                }
                else if (RetVal == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
                    static int eagain_count = 0;
                    if (eagain_count < 5) {
                        eagain_count++;
                        log_debug("Econet(Rx): recvfrom called, no data (call %d) fws=%d BIB=%u FV=%d",
                            eagain_count, fourwaystage, BeebRx.BytesInBuffer,
                            !!(ADLC.status2 & ADLC_STA2_FRAME_VAL));
                    }
                }
                else if (RetVal == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
                    log_error("Econet(Rx): Failed to receive packet: %s", econet_socket_errstr());
            }

            /* this bit fakes the bits of the 4-way handshake that AUN doesn't do. */
            if (confAUNmode && EconetSCACKtrigger <= EconetCycles) {
                switch (fourwaystage) {
                    case FWS_SCOUTSENT:
                        /* just got a scout from the beeb, fake an acknowledgement. */
                        BeebRx.eh.deststn = EconetStationNumber;
                        BeebRx.eh.destnet = 0;
                        BeebRx.eh.srcstn = EconetTx.deststn;  /* use scout's dest as source of ack. */
                        BeebRx.eh.srcnet = EconetTx.destnet;  /* & inmask; //30jun */

                        BeebRx.BytesInBuffer = 4;
                        BeebRx.Pointer = 0;
                        fourwaystage = FWS_SCACKRCVD;
                        log_debug("Econet(Rx): fake scout ACK deststn=%u srcstn=%u fws->SCACKRCVD", BeebRx.eh.deststn, BeebRx.eh.srcstn);
                        FlagFillActive = false;
                        break;
                    case FWS_SCACKSENT:
                        /* beeb acked the scout we gave it, so give it the data AUN sent us earlier. */
                        BeebRx.eh.deststn = EconetStationNumber;  /* as it is data it must be for us */
                        BeebRx.eh.destnet = 0;
                        BeebRx.eh.srcstn = rx_scout_srcstn;
                        BeebRx.eh.srcnet = rx_scout_srcnet;
                        if (EconetFakeResponseActive) {
                            EconetFakeResponseActive = false;
                            if (EconetFakeResponseReplyPort == 0x00 &&
                                (EconetFakeResponseCb & 0x7F) >= 0x03 && (EconetFakeResponseCb & 0x7F) <= 0x05) {
                                /* Extended scout (NOTIFY/printer): deliver just the data
                                 * byte(s) after the 4-byte scout_ext prefix, matching the
                                 * live ext-scout data-delivery path.  The sender's own AUN
                                 * handle was preserved, so let the normal FWS_DATARCVD path
                                 * send the final ACK (don't suppress it). */
                                size_t data_len = EconetFakeResponseLen > 4 ? EconetFakeResponseLen - 4 : 0;
                                memcpy(BeebRx.buff + 4, EconetFakeResponseBuff + 4, data_len);
                                BeebRx.BytesInBuffer = 4 + (int)data_len;
                                log_debug("Econet(Rx): stn=%u: queued ext scout delivered BIB=%d fws->DATARCVD",
                                    EconetStationNumber, BeebRx.BytesInBuffer);
                            } else {
                            EconetFakeResponseSuppressAck = true;
                            memcpy(BeebRx.buff + 4, EconetFakeResponseBuff, EconetFakeResponseLen);
                            BeebRx.BytesInBuffer = 4 + EconetFakeResponseLen;
                            log_debug("Econet(Rx): filesvr stub stn=%u: fake FS response delivered BIB=%d ret=%02X fws->DATARCVD",
                                EconetStationNumber, BeebRx.BytesInBuffer, EconetFakeResponseBuff[0]);
                            /* Decode BRK error responses from the real FS for the log. */
                            if (EconetFakeResponseLen >= 2 && EconetFakeResponseBuff[0] == 0x00) {
                                char errtxt[32]; int etpos = 0;
                                for (int ei = 2; ei < (int)EconetFakeResponseLen && etpos < (int)sizeof(errtxt)-1; ei++) {
                                    uint8_t ec = EconetFakeResponseBuff[ei];
                                    if (ec == 0x0D || ec == 0x00) break;
                                    errtxt[etpos++] = (char)ec;
                                }
                                errtxt[etpos] = '\0';
                                log_debug("Econet(Rx): FS stn=%u error %u (0x%02X): \"%s\"",
                                    EconetFakeResponseSrcStn, EconetFakeResponseBuff[1],
                                    EconetFakeResponseBuff[1], errtxt);
                            }
                            }
                        } else {
                            if (EconetRx.ah.port == 0x00 &&
                                (EconetRx.ah.cb & 0x7F) >= 0x03 && (EconetRx.ah.cb & 0x7F) <= 0x05) {
                                /* Skip the 4-byte scout_ext prefix (already delivered in
                                 * the extended scout frame).  AUN payload layout for these
                                 * ports: [ext0..ext3][data...]; CB/PORT live in the AUN
                                 * header, not in the payload, so only 4 bytes are skipped. */
                                size_t payload_len = EconetRx.BytesInBuffer - sizeof(EconetRx.ah);
                                if (payload_len > 4) {
                                    memcpy(BeebRx.buff + 4, EconetRx.buff + 4, payload_len - 4);
                                    BeebRx.BytesInBuffer = 4 + (int)(payload_len - 4);
                                } else {
                                    BeebRx.BytesInBuffer = 4;
                                }
                            } else {
                                econet_rx_copy(4, EconetRx.BytesInBuffer);
                            }
                        }
                        BeebRx.Pointer = 0;
                        fourwaystage = FWS_DATARCVD;
                        FlagFillActive = false;
                        break;
                    default:
                        break;
                }
            }

            /* Watchdog fallback: if no real IMM_REPLY arrived in time, inject a
             * plausible machine-type reply so ANFS proceeds to the 4-way login
             * instead of stalling until FourWayStageTimeout. */
            if (confAUNmode && EconetFakeImmReplytrigger && EconetFakeImmReplytrigger <= EconetCycles) {
                EconetFakeImmReplytrigger = 0;
                if (fourwaystage == FWS_IMMSENT) {
                    log_debug("Econet(Rx): fake immediate reply from stn=%u net=%u fws->WAIT4IDLE (discarding %d queued background packets)",
                             EconetFakeImmReplySrcStn, EconetFakeImmReplySrcNet, EconetRespQLen);
                    /* Discard any background UNICASTs (FS traffic forwarded by the bridge)
                     * that queued up while waiting for the IMM_REPLY.  They pre-date the
                     * login request and would cause 22-second stalls if injected now. */
                    EconetRespQHead = EconetRespQTail = EconetRespQLen = 0;
                    memset(&EconetRx.ah, 0, sizeof(EconetRx.ah));
                    EconetRx.ah.type = AUN_TYPE_IMM_REPLY;
                    EconetRx.ah.cb   = 0x0B;  /* Level 3 FS machine type */
                    EconetRx.ah.port = 0x00;
                    EconetRx.buff[0] = 0x0B;
                    EconetRx.buff[1] = 0x00;
                    EconetRx.BytesInBuffer = (int)(sizeof(EconetRx.ah) + 2);
                    BeebRx.eh.srcstn  = EconetFakeImmReplySrcStn;
                    BeebRx.eh.srcnet  = EconetFakeImmReplySrcNet;
                    BeebRx.eh.deststn = EconetStationNumber;
                    BeebRx.eh.destnet = 0;
                    econet_rx_copy(4, EconetRx.BytesInBuffer);
                    BeebRx.Pointer = 0;
                    econet_set_wait4idle("Rx", "fake immediate reply from FS");
                }
            }

            /* Promote the next queued FS response to the active slot once the
             * previous one has been consumed (Response1 cleared, FSM back to IDLE).
             * A fallback delay of 50 polls guards against injecting before ANFS has
             * cleared its NMI dispatch vector ($0406/$0407); the inject gate below
             * fires as soon as $0406/$0407 == 0000, so the fallback rarely triggers. */
            if (confAUNmode && EconetRespQLen > 0 && !EconetFakeResponsePending && fourwaystage == FWS_IDLE) {
                struct econet_queued_resp *q = &EconetRespQ[EconetRespQHead];
                memcpy(EconetFakeResponseBuff, q->buff, q->len);
                EconetFakeResponseLen       = q->len;
                EconetFakeResponseReplyPort = q->reply_port;
                EconetFakeResponseCb        = q->cb;
                EconetFakeResponseSrcStn    = q->src_stn;
                EconetFakeResponseSrcNet    = q->src_net;
                EconetFakeResponseHandle    = q->handle;
                EconetFakeResponsePending   = true;
                EconetFakeResponseInjectAfter = EconetCycles + 50;
                EconetRespQHead = (EconetRespQHead + 1) % ECONET_RESPQ_DEPTH;
                EconetRespQLen--;
                log_debug("Econet(Rx): stn=%u: promoted from queue port=%02X len=%d inject after %lu (qlen now %d)",
                    EconetStationNumber, EconetFakeResponseReplyPort, EconetFakeResponseLen, EconetFakeResponseInjectAfter, EconetRespQLen);
            }

            /* Fake FS response: once WAIT4IDLE → FWS_IDLE, inject as soon as
             * $0406/$0407 == 0000 (ANFS NMI dispatch vector is idle) or the
             * fallback timer expires, whichever comes first. */
            if (confAUNmode && EconetFakeResponsePending && fourwaystage == FWS_IDLE
                && (!EconetFakeResponseInjectAfter || EconetFakeResponseInjectAfter <= EconetCycles
                    || (readmem(0x0406) == 0 && readmem(0x0407) == 0))) {
                EconetFakeResponsePending = false;
                EconetFakeResponseInjectAfter = 0;
                log_debug("Econet(Rx): stn=%u: injecting queued FS response scout port=%02X from stn=%u $0406/$0407=%04X fws->SCOUTRCVD",
                    EconetStationNumber, EconetFakeResponseReplyPort, EconetFakeResponseSrcStn,
                    (uint16_t)(readmem(0x0406) | (readmem(0x0407) << 8)));

                /* Pre-populate EconetRx so FWS_SCACKSENT delivers our fake data. */
                memset(&EconetRx.ah, 0, sizeof(EconetRx.ah));
                EconetRx.ah.type = AUN_TYPE_UNICAST;
                EconetRx.ah.port = EconetFakeResponseReplyPort;
                EconetRx.ah.cb = EconetFakeResponseCb;
                EconetRx.ah.handle = EconetFakeResponseHandle;
                memcpy(EconetRx.buff, EconetFakeResponseBuff, EconetFakeResponseLen);
                EconetRx.BytesInBuffer = sizeof(EconetRx.ah) + EconetFakeResponseLen;

                BeebRx.eh.deststn = EconetStationNumber;
                BeebRx.eh.destnet = 0;
                BeebRx.eh.srcstn = EconetFakeResponseSrcStn;
                BeebRx.eh.srcnet = EconetFakeResponseSrcNet;
                BeebRx.eh.cb = EconetFakeResponseCb | 0x80;
                BeebRx.eh.port = EconetFakeResponseReplyPort;
                BeebRx.Pointer = 0;

                if (EconetFakeResponseReplyPort == 0x00 &&
                    (EconetFakeResponseCb & 0x7F) >= 0x03 && (EconetFakeResponseCb & 0x7F) <= 0x05 &&
                    EconetFakeResponseLen >= 4) {
                    /* Extended scout (NOTIFY/printer): replay the saved scout_ext
                     * bytes, matching the live FWS_IDLE ext-scout path. */
                    memcpy(BeebRx.buff + sizeof(BeebRx.eh), EconetFakeResponseBuff, 4);
                    BeebRx.BytesInBuffer = (int)(sizeof(BeebRx.eh) + 4);
                } else {
                    BeebRx.BytesInBuffer = sizeof(BeebRx.eh);
                }

                rx_scout_srcstn = EconetFakeResponseSrcStn;
                rx_scout_srcnet = EconetFakeResponseSrcNet;

                EconetFakeResponseActive = true;
                /* Now the scout is in BeebRx — safe to drop FlagFill and the
                 * bridge-response wait flag simultaneously so ANFS2 sees the
                 * bus-busy→scout transition without an idle gap in between. */
                EconetWaitingForBridgeResp = false;
                fourwaystage = FWS_SCOUTRCVD;
                FlagFillActive = false;
            }


            if (BeebRx.BytesInBuffer > 0)
                log_dump("Econet(Rx): Econet packet: ", BeebRx.buff, BeebRx.BytesInBuffer);
        }
    }
}

/*
 * econet_update_head — first phase of each poll: snapshot status and act on CR changes.
 *
 * Saves ADLC status1/status2 into old_status1/old_status2 so that econet_update_tail()
 * can detect rising/falling edges.  Then scans the control registers for action bits:
 *   CR1b5 (RxDisc/RxAbort): discard the in-progress RX frame and reset to FWS_IDLE.
 *   CR1b6 (RxReset):        set when a TxAbort clears an in-flight TX (handled elsewhere).
 *   CR2b6 (CLR RxST):       clear RX status bits in SR2.
 *   CR2b7 (CLR TxST):       clear TX status bits in SR1.
 *   CR4b5 (TxAbort):        abort the current TX frame and return to FWS_IDLE.
 *
 * TxLast handling (the bit in txftl that marks the final FIFO byte) is processed
 * inside econet_tx_data(), not here.
 */
static void econet_update_head(void)
{
    /* save flags */
    old_status1 = ADLC.status1;
    old_status2 = ADLC.status2;

    /* When the CPU clears RTS (was set, now absent from CR2), the ~CTS line will rise
     * in update_tail and set NOT_CTS in status1.  This is intentional — the CPU chose
     * to stop transmitting — so it should not create a new irqcause entry.  Pre-populate
     * old_status1 with NOT_CTS so the rising-edge detection in update_tail sees no change. */
    if (!ADLC.cts && !(ADLC.control2 & ADLC_CTL2_RTS))
        old_status1 |= ADLC_STA1_NOT_CTS;

    /* okie dokie.  This is where the brunt of the ADLC emulation & network handling will happen. */

    /* look for control bit changes and take appropriate action */

    /* CR1b0 - Address Control - only used to select between register 2/3/4
     * no action needed here
     * CR1b1 - RIE - Receiver Interrupt Enable - Flag to allow receiver section to create interrupt.
     * no action needed here
     * CR1b2 - TIE - Transmitter Interrupt Enable - ditto
     * no action needed here
     * CR1b3 - RDSR mode. When set, interrupts on received data are inhibited.
     * unsupported - no action needed here
     * CR1b4 - TDSR mode. When set, interrupts on trasmit data are inhibited.
     * unsupported - no action needed here
     * CR1b5 - Discontinue - when set, discontinue reception of incoming data.
     * automatically reset this when reach the end of current frame in progress
     * automatically reset when frame aborted bvy receiving an abort flag, or DCD fails. */
    if (ADLC.control1 & ADLC_CTL1_RX_DISC) {
        log_debug("EconetPoll: RxABORT is set");
        BeebRx.Pointer = 0;
        BeebRx.BytesInBuffer = 0;
        ADLC.rxfptr = 0;
        ADLC.rxap = 0;
        ADLC.rxffc = 0;
        ADLC.control1 &= ~ADLC_CTL1_RX_DISC;  /* reset flag */
        fourwaystage = FWS_IDLE;
    }
    /* CR1b6 - RxRs - Receiver reset. set by cpu or when reset line goes low.
     * all receive operations blocked (bar dcd monitoring) when this is set.
     * see CR2b5
     * CR1b7 - TxRS - Transmitter reset. set by cpu or when reset line goes low.
     * all transmit operations blocked (bar cts monitoring) when this is set.
     * no action needed here; watch this bit elsewhere to inhibit actions */

    /* -----------------------*/
    /* CR2b0 - PSE - priotitised status enable - adjusts how status bits show up.
     * See sr2pse and code in status section
     * CR2b1 - 2byte/1byte mode.  set to indicate 2 byte mode. see trda status bit.
     * CR2b2 - Flag/Mark idle select. What is transmitted when tx idle. ignored here as not needed
     * CR2b3 - FC/TDRA mode - does status bit SR1b6 indicate 1=frame complete,
     * 0=tx data reg available. 1=frame tx complete.  see tdra status bit
     * CR2b4 - TxLast - byte just put into fifo was the last byte of a packet. */
    if (ADLC.control2 & ADLC_CTL2_TX_LAST) {  /* TxLast set */
        ADLC.txftl |= 1;                      /* set b0 - flag for fifo[0] */
        ADLC.control2 &= ~ADLC_CTL2_TX_LAST;  /* clear flag. */
    }

    /* CR2b5 - CLR RxST - Clear Receiver Status - reset status bits */
    if ((ADLC.control2 & ADLC_CTL2_RX_CLEAR) || (ADLC.control1 & ADLC_CTL1_RX_RESET)) {                                                    /* or rxreset */
        ADLC.control2 &= ~ADLC_CTL2_RX_CLEAR;                                                                                              /* clear this bit */
        ADLC.status1 &= ~(ADLC_STA1_S2RR|ADLC_STA1_FLAG_DET);                                                                              /* clear sr2rq, FD */
        ADLC.status2 &= ~(ADLC_STA2_FRAME_VAL|ADLC_STA2_INAC_IDLE|ADLC_STA2_ABORT|ADLC_STA2_FCS_ERR|ADLC_STA2_NOT_DCD|ADLC_STA2_RX_OVER);  /* clear FV, RxIdle, RxAbt, Err, OVRN, DCD */

        if ((ADLC.control2 & ADLC_CTL2_PSE) && ADLC.sr2pse) {  /* PSE active? */
            ADLC.sr2pse++;                                     /* Advance PSE to next priority */
            if (ADLC.sr2pse > 4)
                ADLC.sr2pse = 0;
        }
        else {
            ADLC.sr2pse = 0;
        }

        sr1b2cause = 0;                            /* clear cause of sr2b1 going up */
        if (ADLC.control1 & ADLC_CTL1_RX_RESET) {  /* rx reset,clear buffers. */
            BeebRx.Pointer = 0;
            BeebRx.BytesInBuffer = 0;
            ADLC.rxfptr = 0;
            ADLC.rxap = 0;
            ADLC.rxffc = 0;
            ADLC.sr2pse = 0;
        }
/* fourwaystage = FWS_IDLE;            // this really doesn't like being here. */
    }

    /* CR2b6 - CLT TxST - Clear Transmitter Status - reset status bits */
    if ((ADLC.control2 & ADLC_CTL2_TX_CLEAR) || (ADLC.control1 & ADLC_CTL1_TX_RESET)) {  /* or txreset */
        ADLC.control2 &= ~ADLC_CTL2_TX_CLEAR;                                            /* clear this bit */
        ADLC.status1 &= ~(ADLC_STA1_NOT_CTS|ADLC_STA1_TX_UNDER|ADLC_STA1_TDRAFC);        /* clear TXU , cts, TDRA/FC */
        if (ADLC.cts) {
            ADLC.status1 |= ADLC_STA1_NOT_CTS;  /* cts follows signal, reset high again */
            old_status1 |= ADLC_STA1_NOT_CTS;   /* don't trigger another interrupt instantly */
        }
        /* TX_CLEAR/TX_RESET is the CPU's acknowledgement of TX interrupt conditions.
         * NOT_CTS re-asserts immediately when cts is high (RTS cleared), so the
         * old_status1 trick above prevents a fresh rising-edge IRQ, but the original
         * NOT_CTS entry in irqcause is never cleared by the normal "bit went off" path.
         * Explicitly remove all TX-related irqcause bits here. */
        irqcause &= ~(ADLC_STA1_NOT_CTS|ADLC_STA1_TX_UNDER|ADLC_STA1_TDRAFC);
        if (ADLC.control1 & ADLC_CTL1_TX_RESET) {  /* tx reset,clear buffers. */
            BeebTx.Pointer = 0;
            BeebTx.BytesInBuffer = 0;
            ADLC.txfptr = 0;
            ADLC.txftl = 0;
        }
    }

    /* CR2b7 - RTS control - looks after RTS output line. ignored here.
     * but used in CTS logic
     * RTS gates TXD onto the econet bus. if not zero, no tx reaches it,
     * in the B+, RTS substitutes for the collision detection circuit. */

    /* -----------------------*/
    /* CR3 seems always to be all zero while debugging emulation.
     * CR3b0 - LCF - Logical Control Field Select. if zero, no control fields in frame, ignored.
     * CR3b1 - CEX - Extend Control Field Select - when set, control field is 16 bits. ignored.
     * CR3b2 - AEX - When set, address will be two bytes (unless first byte is zero). ignored here.
     * CR3b3 - 01/11 idle - idle transmission mode - ignored here,.
     * CR3b4 - FDSE - flag detect status enable.  when set, then FD (SR1b3) + interrupr indicated a flag
     * has been received. I don't think we use this mode, so ignoring it.
     * CR3b5 - Loop - Loop mode. Not used.
     * CR3b6 - GAP/TST - sets test loopback mode (when not in Loop operation mode.) ignored.
     * CR3b7 - LOC/DTR - (when not in loop mode) controls DTR pin directly. pin not used in a BBC B */

    /* -----------------------*/
    /* CR4b0 - FF/F - when clear, re-used the Flag at end of one packet as start of next packet. ignored.
     * CR4b1,2 - TX word length. 11=8 bits. BBC uses 8 bits so ignore flags and assume 8 bits throughout
     * CR4b3,4 - RX word length. 11=8 bits. BBC uses 8 bits so ignore flags and assume 8 bits throughout
     * CR4b5 - TransmitABT - Abort Transmission.  Once abort starts, bit is cleared. */
    if (ADLC.control4 & ADLC_CTL4_TX_ABORT) {  /* ABORT */
        log_debug("EconetPoll: TxABORT is set");
        ADLC.txfptr = 0;  /* reset fifo */
        ADLC.txftl = 0;   /* reset fifo flags */
        BeebTx.Pointer = 0;
        BeebTx.BytesInBuffer = 0;
        ADLC.control4 &= ~ADLC_CTL4_TX_ABORT;  /* reset flag. */
        fourwaystage = FWS_IDLE;
        log_debug("Econet: Set FWS_IDLE (abort)");
    }

    /* CR4b6 - ABTex - extend abort - adjust way the abort flag is sent.  ignore,
     * can affect timing of RTS output line (and thus CTS input) still ignored.
     * CR4b7 - NRZI/NRZ - invert data encoding on wire. ignore. */
}

/*
 * econet_rx_tx — thin dispatcher: drive TX and RX data paths, then update idle.
 *
 * Calls econet_tx_data() if the TX path is not in reset, and econet_rx_data() if
 * the RX path is not in reset.  After both, recomputes ADLC.idle: the line is
 * considered idle only when RX is not in reset, the RX FIFO is empty, Frame Valid
 * is clear, and BeebRx has no buffered data (including any active rxdelay).
 */
static void econet_rx_tx(void)
{
    /* Only do this bit occasionally as data only comes in from
     * line occasionally.
     * Trickle data between fifo registers and ip packets. */

    /* Transmit data */
    if (!(ADLC.control1 & ADLC_CTL1_TX_RESET))  /* tx reset off */
        econet_tx_data();
    /* Receive data */
    if (ADLC.control1 & ADLC_CTL1_RX_RESET) {
        static int rxreset_count = 0;
        if (rxreset_count++ == 0)
            log_debug("Econet(Rx): RX_RESET set, econet_rx_data skipped fws=%d ctl1=%02X ctl2=%02X",
                fourwaystage, ADLC.control1, ADLC.control2);
    } else
        econet_rx_data();

    /* Update idle status */
    if (!(ADLC.control1 & ADLC_CTL1_RX_RESET)     /* not rxreset */
        && !ADLC.rxfptr                           /* nothing in fifo */
        && !(ADLC.status2 & ADLC_STA2_FRAME_VAL)  /* no FV */
        && (BeebRx.BytesInBuffer == 0
        && rxdelay == -1)) {  /* nothing in ip buffer */
        ADLC.idle = true;
    }
    else {
        ADLC.idle = false;
    }
}

/*
 * econet_update_tail — second phase of each poll: recompute ADLC status and fire NMI.
 *
 * Recalculates all ADLC status bits from current hardware state:
 *   SR1: IRQ, NOT_CTS, TX_UNDER, TDRAFC (TX DR/FC), AP (address present), RDA.
 *   SR2: DCD, FV (frame valid), INAC_IDLE, RX_ABORT, RX_OFLOW.
 *   sr2pse: PSE (priority status encoding) — shadows the highest-priority SR2 event
 *           so subsequent SR2 reads return the next pending event in order.
 *
 * Detects rising edges on status bits (comparing against old_status1/2 saved by
 * econet_update_head) to update irqcause / sr1b2cause — the accumulator that
 * econet_read_rxreg() uses to determine when IRQ can be de-asserted.
 *
 * Also handles:
 *   - FlagFill timeout: clears FlagFillActive when EconetFlagFillTimeoutTrigger fires.
 *   - FWS_WAIT4IDLE → FWS_IDLE transition after EconetWait4IdleTimeout polls.
 *   - 4-way stage timeout (Econet4Wtrigger): resets FSM to FWS_IDLE on stall.
 *   - Fake scout/ACK injection timers (EconetSCACKtrigger, EconetFakeImmReplytrigger).
 */
static void econet_update_tail(void)
{
    /* Reset pseudo flag fill? */
    if (EconetFlagFillTimeoutTrigger <= EconetCycles && FlagFillActive) {
        FlagFillActive = false;
        log_debug("Econet: FlagFill timeout reset");
    }

    /* While in FWS_IDLE waiting for a bridge-connected FS to reply (the bridge
     * ACKs our packet immediately but the real Econet FS may take seconds to
     * respond), keep FlagFillActive so ANFS2 sees a busy bus and doesn't
     * time out with "No reply from station".  Use the one-shot deadline set at
     * bridge-ACK time; do NOT refresh it here or the timeout never fires. */
    if (confAUNmode && fourwaystage == FWS_IDLE && EconetWaitingForBridgeResp) {
        if (EconetFlagFillTimeoutTrigger > EconetCycles) {
            FlagFillActive = true;
        } else {
            /* Deadline passed without an FS response — give up so ANFS2
             * sees an idle bus and can report "No reply from station" itself. */
            EconetWaitingForBridgeResp = false;
            log_warn("Econet: bridge response wait timed out for stn=%u net=%u",
                     EconetFakeResponseSrcStn, EconetFakeResponseSrcNet);
        }
    }

    /* waiting for AUN to become idle? */
    if (confAUNmode && fourwaystage == FWS_WAIT4IDLE && BeebRx.BytesInBuffer == 0 && ADLC.rxfptr == 0 && ADLC.txfptr == 0) {
        if (EconetWait4IdleTrigger == 0) {
            log_debug("Econet: setting WAIT4IDLE timeout");
            EconetWait4IdleTrigger = EconetCycles + EconetWait4IdleTimeout;
        } else if (EconetWait4IdleTrigger <= EconetCycles) {
            log_debug("Econet: wait over, returning to FWS_IDLE");
            fourwaystage = FWS_IDLE;
            Econet4Wtrigger = 0;
            EconetWait4IdleTrigger = 0;
            FlagFillActive = false;
        }
    }

    /* timeout four way handshake - for when we get lost.. */
    if (Econet4Wtrigger == 0) {
        if (fourwaystage != FWS_IDLE)
            Econet4Wtrigger = EconetCycles + FourWayStageTimeout;
    }
    else if (Econet4Wtrigger <= EconetCycles) {
        EconetSCACKtrigger = 0;
        EconetWait4IdleTrigger = 0;
        EconetFakeImmReplytrigger = 0;
        EconetFakeResponsePending = false;
        EconetFakeResponseActive = false;
        EconetRespQHead = EconetRespQTail = EconetRespQLen = 0;
        EconetFakeResponseInjectAfter = 0;
        EconetWaitingForBridgeResp = false;
        Econet4Wtrigger = 0;
        fourwaystage = FWS_IDLE;
        log_warn("Econet: 4waystage timeout; Set FWS_IDLE");
        econet_adlc_debug();
    }

    /*--------------------------------------------------------------------------------------------*/
    /* Status bits need changing? */

    /* SR1b0 - RDA - received data available. */
    if (!(ADLC.control1 & ADLC_CTL1_RX_RESET)) {  /* rx reset off */
        if (ADLC.rxfptr && (ADLC.rxffc & (powers[ADLC.rxfptr - 1])))
            ADLC.status2 |= ADLC_STA2_FRAME_VAL;
        if ((ADLC.rxfptr && !(ADLC.control2 & ADLC_CTL2_2BYTE))           /* 1 byte mode */
            || ((ADLC.rxfptr > 1) && (ADLC.control2 & ADLC_CTL2_2BYTE)))  /* 2 byte mode */
        {
            ADLC.status1 |= ADLC_STA1_RDA;  /* set RDA copy */
            ADLC.status2 |= ADLC_STA2_RDA;
        }
        else {
            ADLC.status1 &= ~ADLC_STA1_RDA;
            ADLC.status2 &= ~ADLC_STA2_RDA;
        }
    }
    /* SR1b1 - S2RQ - set after SR2, see below
     * SR1b2 - LOOP - set if in loop mode. not supported in this emulation,
     * SR1b3 - FD - Flag detected. Hmm. */
    if (FlagFillActive)
        ADLC.status1 |= ADLC_STA1_FLAG_DET;
    else
        ADLC.status1 &= ~ADLC_STA1_FLAG_DET;

    /* SR1b4 - CTS - set by ~CTS line going up, and causes IRQ if enabled.
     * only cleared by cpu.
     * ~CTS is a NAND of DCD(clock present)(high if valid)
     * &  collission detection!
     * i.e. it's low (thus clear to send) when we have both DCD(clock)
     * present AND no collision on line and no collision.
     * cts will ALSO be high if there is no cable!
     * we will only bother checking against DCD here as can't have collisions.
     * but nfs then loops waiting for CTS high!
     * on the B+ there is (by default) no collission detection circuitary. instead S29
     * links RTS in it's place, thus CTS is a NAND of not RTS & not DCD
     * i.e. cts = ! ( !rts && !dcd ) all signals are active low.
     * there is a delay on rts going high after cr2b7=0 - ignore this for now.
     * cr2b7 = 1 means RTS low means not rts high means cts low
     * sockets true means dcd low means not dcd high means cts low
     * doing it this way finally works !!  great :-) :-) */

    if (SocketOpen && (ADLC.control2 & ADLC_CTL2_RTS)) {  /* clock + RTS */
        ADLC.cts = false;
        ADLC.status1 &= ~ADLC_STA1_NOT_CTS;
    }
    else {
        ADLC.cts = true;
    }

    /* and then set the status bit if the line is high! (status bit stays
     * up until cpu tries to clear it) (& still stays up if cts line still high) */

    if (!(ADLC.control1 & ADLC_CTL2_RTS) && ADLC.cts) {
        ADLC.status1 |= ADLC_STA1_NOT_CTS;  /* set CTS now */
    }

    /* SR1b5 - TXU - Tx Underrun. */
    if (ADLC.txfptr > 4) {  /* probably not needed */
        log_debug("Econet: TX Underrun - TXfptr %02x", (unsigned int)ADLC.txfptr);
        ADLC.status1 |= ADLC_STA1_TX_UNDER;
        ADLC.txfptr = 4;
    }

    /* SR1b6 TDRA flag - another complicated derivation */
    if (!(ADLC.control1 & ADLC_CTL1_TX_RESET)) {                               /* not txreset */
        if (!(ADLC.control2 & ADLC_CTL2_FR_COMP)) {                            /* tdra mode */
            if ((((ADLC.txfptr < 3) && !(ADLC.control2 & ADLC_CTL2_2BYTE))     /* space in fifo? */
                 || ((ADLC.txfptr < 2) && (ADLC.control2 & ADLC_CTL2_2BYTE)))  /* space in fifo? */
                && (!(ADLC.status1 & ADLC_STA1_NOT_CTS))                       /* clear to send is ok */
                && (!(ADLC.status2 & ADLC_STA2_NOT_DCD))) {                    /* DTR not high */

                if (!(ADLC.status1 & ADLC_STA1_TDRAFC)) {
                    log_debug("ADLC: set tdra");
                    ADLC.status1 |= ADLC_STA1_TDRAFC;  /* set Tx Reg Data Available flag. */
                }
            }
            else {
                if (ADLC.status1 & ADLC_STA1_TDRAFC) {
                    log_debug("ADLC: clear tdra");
                    ADLC.status1 &= ~ADLC_STA1_TDRAFC;  /* clear Tx Reg Data Available flag. */
                }
            }
        }
        else {                     /* FC mode */
            if (!(ADLC.txfptr)) {  /* nothing in fifo */
                if (!(ADLC.status1 & ADLC_STA1_TDRAFC)) {
                    log_debug("ADLC: set fc");
                    ADLC.status1 |= ADLC_STA1_TDRAFC;  /* set Tx Reg Data Available flag. */
                }
            }
            else {
                if (ADLC.status1 & ADLC_STA1_TDRAFC) {
                    log_debug("ADLC: clear fc");
                    ADLC.status1 &= ~ADLC_STA1_TDRAFC;  /* clear Tx Reg Data Available flag. */
                }
            }
        }
    }
    /* SR1b7 IRQ flag - see below */

    /* SR2b0 - AP - Address present */
    if (!(ADLC.control1 & ADLC_CTL1_RX_RESET)) {                       /* not rxreset */
        if (ADLC.rxfptr && (ADLC.rxap & (powers[ADLC.rxfptr - 1]))) {  /* ap bits set on fifo */
            ADLC.status2 |= ADLC_STA2_ADDR_PRES;
        }
        else {
            ADLC.status2 &= ~ADLC_STA2_ADDR_PRES;
        }
        /* SR2b1 - FV -Frame Valid - set in rx - only reset by ClearRx or RxReset */
        /* if (ADLC.rxfptr && (ADLC.rxffc & (powers[ADLC.rxfptr - 1]))) {
            ADLC.status2 |= ADLC_STA2_FRAME_VAL;
        } */
        /* SR2b2 - Inactive Idle Received - sets irq! */
        if (ADLC.idle && !FlagFillActive)
            ADLC.status2 |= ADLC_STA2_INAC_IDLE;
        else
            ADLC.status2 &= ~ADLC_STA2_INAC_IDLE;
    }
    /* SR2b3 - RxAbort - Abort received - set in rx routines above
     * SR2b4 - Error during reception - set if error flaged in rx routine.
     * SR2b5 - DCD */
    if (!SocketOpen) {                      /* is line down? */
        ADLC.status2 |= ADLC_STA2_NOT_DCD;  /* flag error */
    }
    else {
        ADLC.status2 &= ~ADLC_STA2_NOT_DCD;
    }
    /* SR2b6 - OVRN -receipt overrun. probably not needed */
    if (ADLC.rxfptr > 4) {
        ADLC.status2 |= ADLC_STA2_RX_OVER;
        ADLC.rxfptr = 4;
    }
    /* SR2b7 - RDA. As per SR1b0 - set above. */

    /* Handle PSE - only for SR2 Rx bits at the moment */
    int sr2psetemp = ADLC.sr2pse;
    if (ADLC.control2 & ADLC_CTL2_PSE) {
        if ((ADLC.sr2pse <= 1) && (ADLC.status2 & (ADLC_STA2_FRAME_VAL|ADLC_STA2_ABORT|ADLC_STA2_FCS_ERR|ADLC_STA2_NOT_DCD|ADLC_STA2_RX_OVER))) {  /* ERR, FV, DCD, OVRN, ABT */
            ADLC.sr2pse = 1;
            ADLC.status2 &= ~(ADLC_STA2_ADDR_PRES|ADLC_STA2_INAC_IDLE|ADLC_STA2_RDA);
        }
        else if ((ADLC.sr2pse <= 2) && (ADLC.status2 & ADLC_STA2_INAC_IDLE)) {  /* Idle */
            ADLC.sr2pse = 2;
            ADLC.status2 &= ~(ADLC_STA2_ADDR_PRES|ADLC_STA2_RDA);
        }
        else if ((ADLC.sr2pse <= 3) && (ADLC.status2 & ADLC_STA2_ADDR_PRES)) {  /* AP */
            ADLC.sr2pse = 3;
            ADLC.status2 &= ~ADLC_STA2_RDA;
        }
        else if (ADLC.status2 & ADLC_STA2_RDA) {  /* RDA */
            ADLC.sr2pse = 4;
            ADLC.status2 &= ~ADLC_STA2_FRAME_VAL;
        }
        else {
            ADLC.sr2pse = 0;  /* No relevant bits set */
        }

        /* Set SR1 RDA copy */
        if (ADLC.status2 & ADLC_STA2_RDA)
            ADLC.status1 |= ADLC_STA1_RDA;
        else
            ADLC.status1 &= ~ADLC_STA1_RDA;

    }
    else {  /* PSE inactive */
        ADLC.sr2pse = 0;
    }
    if (sr2psetemp != ADLC.sr2pse)
        log_debug("ADLC: PSE SR2Rx priority changed to %d", ADLC.sr2pse);

    /* If PSE has just started reporting a pending SR2 condition (priority 1-4)
     * that it wasn't reporting a moment ago, that's a new interrupt source
     * becoming visible to the CPU even if the raw status2 register value
     * didn't change this poll - e.g. PSE only just got enabled (CR2 write)
     * while INAC_IDLE was already latched from an earlier poll. Without this,
     * status2 == old_status2 below and S2RQ/IRQ never gets raised, leaving
     * the CPU waiting forever for an NMI that was never sent.
     *
     * Restrict this to the fake-4-way scout/data injection states: this same
     * PSE transition also happens harmlessly during normal econet idle
     * polling (e.g. at boot with no clock present), where forcing an extra
     * NMI here derails the boot ROM. */
    bool pse_newly_pending = (ADLC.control2 & ADLC_CTL2_PSE) && ADLC.sr2pse != 0 && ADLC.sr2pse != sr2psetemp
        && fourwaystage >= FWS_SCOUTRCVD && fourwaystage <= FWS_DATARCVD;

    /* Do we need to flag an interrupt? */
    if (ADLC.status1 != old_status1 || ADLC.status2 != old_status2 || pse_newly_pending) {  /* something changed */
        uint8_t tempcause, temp2;

        /* SR1b1 - S2RQ - Status2 request. New bit set in S2? */
        tempcause = ((ADLC.status2 ^ old_status2) & ADLC.status2) & ~ADLC_STA2_RDA;
        if (pse_newly_pending)
            tempcause |= ADLC.status2 & ~ADLC_STA2_RDA;

        if (!(ADLC.control1 & ADLC_CTL1_RIE)) {  /* RIE not set, */
            tempcause = 0;
        }

        if (tempcause) {  /* something got set */
            ADLC.status1 |= ADLC_STA1_S2RR;
            sr1b2cause = sr1b2cause | tempcause;
        }
        else if (!(ADLC.status2 & sr1b2cause)) {  /* cause has gone */
            ADLC.status1 &= ~ADLC_STA1_S2RR;
            sr1b2cause = 0;
        }

        /* New bit set in S1? */
        tempcause = ((ADLC.status1 ^ old_status1) & ADLC.status1) & ~ADLC_STA1_IRQ;

        if (!(ADLC.control1 & ADLC_CTL1_RIE)) {  /* RIE not set, */
            tempcause = tempcause & ~11;
        }
        if (!(ADLC.control1 & ADLC_CTL1_TIE)) {  /* TIE not set, */
            tempcause = tempcause & ~0x70;
        }

        if (tempcause) {                      /* something got set */
            irqcause = irqcause | tempcause;  /* remember which bit went high to flag irq */
            /* SR1b7 IRQ flag */
            ADLC.status1 |= ADLC_STA1_IRQ;
        }

        /* Bit cleared in S1? */
        temp2 = ((ADLC.status1 ^ old_status1) & old_status1) & ~ADLC_STA1_IRQ;
        if (temp2) {                       /* something went off */
            irqcause = irqcause & ~temp2;  /* clear flags that went off */
        }

        /* Also remove irqcause bits whose interrupt enable has since been cleared.
         * Without this, disabling TIE/RIE while a TX/RX status bit still sits in
         * irqcause leaves ADLC_STA1_IRQ permanently set.  Every FE38/FE3C toggle
         * then creates a fresh NMI rising edge while IRQ appears asserted. */
        if (!(ADLC.control1 & ADLC_CTL1_RIE))
            irqcause &= ~0x0B;
        if (!(ADLC.control1 & ADLC_CTL1_TIE))
            irqcause &= ~0x70;

        if (irqcause == 0) {
            ADLC.status1 &= ~ADLC_STA1_IRQ;
            log_debug("ADLC: IRQ cause cleared");
        }
        econet_adlc_debug();
    }
}

/*
 * econet_state_changed — throughput optimisation: signal that the FIFO moved.
 *
 * Returns true (and clears the flag) if econet_read_rxreg() ran since the last
 * call.  The 6502 main loop calls this immediately after each NMI-handler
 * instruction: if true, it fires an extra econet_poll() without waiting for
 * the next 128-cycle tick.  This replicates BeebEm's technique and ensures
 * RDA is re-evaluated (and the next NMI queued) as fast as the ROM can consume
 * bytes, rather than at the fixed 128-cycle poll cadence.
 */
bool econet_state_changed(void)
{
    if (EconetStateChanged) {
        EconetStateChanged = false;
        return true;
    }
    return false;
}

/*
 * econet_poll — periodic Econet tick, called every 128 CPU cycles.
 *
 * Increments EconetCycles (with wrap-around correction on all derived trigger
 * values), then if the socket is open runs the three-phase poll sequence:
 *   econet_update_head() → econet_rx_tx() → econet_update_tail()
 *
 * After the poll, if EconetNMIenabled and ADLC_STA1_IRQ is set, asserts NMI
 * by setting nmi bit 2 and mirroring sysvia.ifr bit 2 (the SR flag), which is
 * what the DNFS/ANFS NMI handler checks.  Clears the NMI bit when IRQ drops.
 */
void econet_poll(void)
{
    /* This timeout scheme is copied from BeebEm where it has central
     * support within the emulator.  It is foreign to B-Em, though, so
     * is implemented only within this module.
     */
    EconetCycles += 1;
    if (EconetCycles >= LONG_MAX) {
        /* This handles the counters wrapping around.  As they are
         * unsigned and we're checking against the MAX for the signed
         * type we catch the wrap with plenty of headroom
         */
        log_debug("Econet: wrap timing counters");
        EconetCycles -= LONG_MAX;
        if (EconetSCACKtrigger)
            EconetSCACKtrigger -= LONG_MAX;
        if (EconetFlagFillTimeoutTrigger)
            EconetFlagFillTimeoutTrigger -= LONG_MAX;
        if (Econet4Wtrigger)
            Econet4Wtrigger -= LONG_MAX;
        if (EconetTxByteTrigger)
            EconetTxByteTrigger -= LONG_MAX;
        if (EconetFakeImmReplytrigger)
            EconetFakeImmReplytrigger -= LONG_MAX;
    }
    /* Don't poll if failed to init sockets */
    if (SocketOpen) {
        old_status1 = ADLC.status1;
        old_status2 = ADLC.status2;
        econet_rx_tx();
        econet_update_tail();
    }
    if (EconetNMIenabled && ADLC.status1 & ADLC_STA1_IRQ) {
        if (!(nmi & 0x04)) {
            /* Elevate to warn during active data-frame delivery so we can see
             * what irqcause triggers NMIs mid-frame (e.g. NOT_CTS vs RDA). */
            if (BeebRx.Pointer > 0 && BeebRx.Pointer < BeebRx.BytesInBuffer)
                log_debug("Econet: NMI mid-frame irqcause=%02X s1=%02X s2=%02X ctl1=%02X ctl2=%02X ptr=%u bib=%u fws=%d",
                    irqcause, ADLC.status1, ADLC.status2, ADLC.control1, ADLC.control2,
                    BeebRx.Pointer, BeebRx.BytesInBuffer, fourwaystage);
            else
                log_debug("Econet: NMI raised, irqcause=%02X s1=%02X s2=%02X ctl1=%02X ctl2=%02X txfptr=%d", irqcause, ADLC.status1, ADLC.status2, ADLC.control1, ADLC.control2, ADLC.txfptr);
            /* Mirror System VIA SR flag so DNFS ROM's $963C NMI handler
             * sees bit 2 of $FE4D set and proceeds to the display dispatch.
             * On real hardware the VIA shift register fires for every byte
             * clocked through the ADLC, so SR is always set when the ADLC
             * NMI fires.  IER bit 2 is left at 0 (SR IRQ disabled) so this
             * does not generate a spurious 6502 IRQ. */
            sysvia.ifr |= 0x04;
            nmi |= 0x04;
        }
    }
    else
        nmi &= ~0x04;

    /* Track $0406/$0407 (ANFS NMI dispatch vector) changes so we can see when
     * ANFS updates it after processing Response1 (preparing for Response2). */
    static uint16_t last_0406 = 0xFFFF;
    uint16_t cur_0406 = (uint16_t)(readmem(0x0406) | (readmem(0x0407) << 8));
    if (cur_0406 != last_0406) {
        log_debug("Econet: $0406/$0407 changed %04X->%04X fws=%s pc=%04X", last_0406, cur_0406, fws_names[fourwaystage], pc);
        last_0406 = cur_0406;
    }

    /* Track $0D6C bit 6 (notification pending) changes. */
    static uint8_t last_0d6c = 0xFF;
    uint8_t cur_0d6c = readmem(0x0D6C);
    if (cur_0d6c != last_0d6c) {
        log_debug("Econet: $0D6C changed %02X -> %02X (bit6:%d->%d) fws=%s pc=%04X",
            last_0d6c == 0xFF ? 0 : last_0d6c, cur_0d6c,
            last_0d6c == 0xFF ? 0 : !!(last_0d6c & 0x40), !!(cur_0d6c & 0x40),
            fws_names[fourwaystage], pc);
        last_0d6c = cur_0d6c;
    }
}

/*
 * econet_read_rxreg — CPU reads one byte from the ADLC RX FIFO ($FEA2/$FEA3).
 *
 * Pops the top byte from ADLC.rxfifo, then immediately re-runs update_head and
 * update_tail so that status bits (especially RDA) are recalculated within the
 * same CPU instruction rather than waiting for the next 128-cycle poll.  This
 * is critical for multi-byte frames: if RDA is not re-evaluated promptly, the
 * NMI line stays de-asserted and the ROM never reads the remaining bytes.
 *
 * If the re-evaluated status clears ADLC_STA1_IRQ, the NMI line (nmi bit 2)
 * is dropped immediately.  This ensures the 6502 sees a clean 0→1 edge on the
 * NEXT IRQ event; without it, nmi stays set and no further NMIs fire.
 *
 * Sets EconetStateChanged to trigger an extra econet_poll() from the 6502 loop.
 */
static uint8_t econet_read_rxreg(void)
{
    if ((ADLC.control1 & ADLC_CTL1_RX_RESET) == 0) {  /* rxreset not set */
        if (ADLC.rxfptr) {
            uint8_t value = ADLC.rxfifo[--ADLC.rxfptr];
            econet_update_head();
            /* Snapshot status after the FIFO read so update_tail can detect
             * the falling edge of RDA (rxfptr may have dropped below threshold).
             * Without this, old_status is stale from the last econet_poll start,
             * causing irqcause.RDA to never clear and blocking further NMIs. */
            old_status1 = ADLC.status1;
            old_status2 = ADLC.status2;
            econet_update_tail();
            /* If reading this byte cleared the IRQ condition, de-assert the NMI
             * line immediately (while still inside the NMI handler). This lets
             * the 6502 see nmi=0 on RTI so the NEXT IRQ event produces a fresh
             * 0→1 rising edge and triggers another NMI. Without this, nmi stays
             * 0x04 permanently even after IRQ clears, deadlocking multi-byte RX. */
            if (!(ADLC.status1 & ADLC_STA1_IRQ))
                nmi &= ~0x04;
            EconetStateChanged = true;
            log_debug("ADLC: read register Receive Data, returned fifo: %02X", value);
            if (fourwaystage >= FWS_SCOUTRCVD && fourwaystage <= FWS_DATARCVD)
                log_debug("ADLC: RX fifo[%d]=%02X fws=%s pc=%04X", ADLC.rxfptr, value, fws_names[fourwaystage], pc);
            return value;
        }
        else
            log_debug("ADLC: read register Receive Data when FIFO is empty");
    }
    else
        log_debug("ADLC: read register Receive Data when receiver in reset state");
    return 0;
}

/*
 * econet_read_register — CPU read of ADLC registers at $FEA0-$FEA3.
 *
 * addr & 0x03 selects:
 *   0 → SR1 (Status Register 1): IRQ, NOT_CTS, TX_UNDER, TDRAFC, AP, RDA, etc.
 *   1 → SR2 (Status Register 2): FV, INAC_IDLE, RX_ABORT, DCD, etc.
 *         (Returns sr2pse when PSE is active so the ROM reads events in priority order.)
 *   2, 3 → RX data byte via econet_read_rxreg().
 */
uint8_t econet_read_register(uint8_t addr)
{
    econet_adlc_debug();

    switch (addr & 0x03) {
        case 0:
            log_debug("ADLC: read register Status1");
            return ADLC.status1;
        case 1:
            log_debug("ADLC: read register Status2");
            return ADLC.status2;
        default:
            return econet_read_rxreg();
    }
}

static void econet_write_txreg(uint8_t value, bool last)
{
    if ((ADLC.control1 & ADLC_CTL1_TX_RESET) == 0) {
        ADLC.txfifo[2] = ADLC.txfifo[1];
        ADLC.txfifo[1] = ADLC.txfifo[0];
        ADLC.txfifo[0] = value;
        ADLC.txfptr++;
        ADLC.txftl = ADLC.txftl << 1;
        if (last)
            ADLC.txftl |= 1;
        /* Don't drain this byte until EconetTimeBetweenBytes cycles have passed.
         * Prevents an immediate drain (when EconetTxByteTrigger==0) from causing
         * a new TDRA interrupt before the NMI handler's RTI, which would nest NMIs
         * and overflow the 6502 stack. */
        EconetTxByteTrigger = EconetCycles + EconetTimeBetweenBytes;
    } else {
        log_debug("ADLC: write TxData(%s)=%02X DROPPED (TX_RESET set) fws=%d s1=%02X s2=%02X", last ? "last" : "cont", value, fourwaystage, ADLC.status1, ADLC.status2);
    }
}

/*
 * econet_write_register — CPU write to ADLC registers at $FEA0-$FEA3.
 *
 * addr & 0x03 selects:
 *   0       → CR1 (Control Register 1): RxReset, TxReset, RIE, TIE, AC (reg bank select).
 *   1, AC=0 → CR2: RTS, CLR RxST, CLR TxST, TDSR, RDSR modes, PSE enable.
 *   1, AC=1 → CR3: loop/echo modes (largely unimplemented).
 *   2       → TX data (not-last); pushes byte into TX FIFO via econet_write_txreg().
 *   3, AC=0 → TX data (last); TxLast flag set — marks end of frame.
 *   3, AC=1 → CR4: TxAbort, ABTex, NRZI select.
 *
 * Writing CR1 triggers econet_update_head() + econet_update_tail() immediately
 * if the value changed (excluding the AC bit), so reset/enable transitions take
 * effect within the same CPU instruction.
 */
void econet_write_register(uint8_t addr, uint8_t Value)
{
    bool changed = true;
    switch (addr & 0x03) {
        case 0:
            log_debug("ADLC: write CR1=%02X (ctl1=%02X ctl2=%02X s1=%02X s2=%02X irq=%02X txfptr=%d fws=%d pc=%04X)", Value, ADLC.control1, ADLC.control2, ADLC.status1, ADLC.status2, irqcause, ADLC.txfptr, fourwaystage, pc);
            if ((ADLC.control1 & ~ADLC_CTL1_AC) == (Value & ~ADLC_CTL1_AC))
                changed = false;
            ADLC.control1 = Value;
            break;
        case 1:
            if (ADLC.control1 & ADLC_CTL1_AC) {
                log_debug("ADLC: write CR3=%02X (ctl1=%02X ctl2=%02X s1=%02X s2=%02X irq=%02X)", Value, ADLC.control1, ADLC.control2, ADLC.status1, ADLC.status2, irqcause);
                ADLC.control3 = Value;
            }
            else {
                log_debug("ADLC: write CR2=%02X (ctl1=%02X ctl2=%02X s1=%02X s2=%02X irq=%02X txfptr=%d fws=%d pc=%04X)", Value, ADLC.control1, ADLC.control2, ADLC.status1, ADLC.status2, irqcause, ADLC.txfptr, fourwaystage, pc);
                ADLC.control2 = Value;
            }
            break;
        case 2:
            log_debug("ADLC: write TxData(cont)=%02X (s1=%02X irq=%02X txfptr=%d)", Value, ADLC.status1, irqcause, ADLC.txfptr);
            econet_write_txreg(Value, false);
            break;
        case 3:
            if (ADLC.control1 & ADLC_CTL1_AC) {
                log_debug("ADLC: write CR4=%02X (ctl1=%02X ctl2=%02X s1=%02X s2=%02X irq=%02X)", Value, ADLC.control1, ADLC.control2, ADLC.status1, ADLC.status2, irqcause);
                ADLC.control4 = Value;
            }
            else {
                log_debug("ADLC: write TxData(last)=%02X (s1=%02X irq=%02X txfptr=%d)", Value, ADLC.status1, irqcause, ADLC.txfptr);
                econet_write_txreg(Value, true);
            }
            break;
    }
    if (changed) {
        econet_update_head();
        econet_update_tail();
    }
}

void econet_close(void)
{
    if (SocketOpen) {
        closesocket(UdpSocket);
        SocketOpen = false;
    }
#ifdef WIN32
    WSACleanup();
#endif
    econet_free_networks();
    econet_free_aunmap();
}
