#ifndef __INC_ACIA_H
#define __INC_ACIA_H

/* TOHv3: export some of these */
#define ACIA_STATREG_TXD_REG_EMP 0x02
#define ACIA_STATREG_CTS         0x08
#define ACIA_STATREG_FRAMING_ERR 0x10
#define ACIA_STATREG_PARITY_ERR  0x40

#define ACIA_CTRLREG_TXCTRL_1    0x20
#define ACIA_CTRLREG_TXCTRL_2    0x40
#define ACIA_CTRLREG_DIVIDE_1    0x1
#define ACIA_CTRLREG_DIVIDE_2    0x2

#define ACIA_CTRLREG_MASK_TXCTRL (ACIA_CTRLREG_TXCTRL_1 | ACIA_CTRLREG_TXCTRL_2) /* 0x60 */
#define ACIA_CTRLREG_MASK_DIVIDE (ACIA_CTRLREG_DIVIDE_1 | ACIA_CTRLREG_DIVIDE_2) /* 0x3 */

#define ACIA_TX_PIPELINE_LEN_MAX 5

/* begin beebjit */
/* (c) Chris Evans, under GPL */
enum {
  k_serial_acia_status_RDRF = 0x01,
  k_serial_acia_status_TDRE = 0x02,
  k_serial_acia_status_DCD =  0x04,
  k_serial_acia_status_CTS =  0x08,
  k_serial_acia_status_FE =   0x10,
  k_serial_acia_status_OVRN = 0x20,
  k_serial_acia_status_PE =   0x40,
  k_serial_acia_status_IRQ =  0x80,
};
/* end beebjit */


typedef struct acia ACIA;

#include "tape2.h" /* TOHv4: for BUILD_TAPE_INCOMPATIBLE_NEW_SAVE_STATES */


/* TOHv3.3: formerly in tape2.h, then uef.h, now it's here */
typedef struct serial_framing_s {

    char parity;           /* 'N', 'O', or 'E' */

    /* This is intended as a "nominal baud rate";
       it's approximate. So it holds e.g. 1200 baud,
       not 1201.9: */
    int32_t nominal_baud;          /* (1200 * 1024) / (ULA divider * ACIA divider) */

    uint8_t num_data_bits;
    uint8_t num_stop_bits;

} serial_framing_t;

#include "tape2.h" /* for BUILD_TAPE_DEV_MENU */

struct acia {

    uint8_t line_cts; /* CTS status, e.g. from serial ULA */
    uint8_t line_dcd; /* TOHv2, TOHv3.3 */

    uint8_t control_reg;
    uint8_t status_reg;
    uint8_t status_reg_for_read; /* TOHv3.3: from beebjit */
    uint8_t rx_data_reg;
    uint8_t tx_data_reg;

    /* TOHv3.3: from beebjit: */
    // uint32_t rx_shift_reg_count;
    uint8_t rx_shift_reg_count; /* TOHv4: use u8 instead of u32 for savestate */
    uint8_t rx_shift_reg;

    /* TOHv4: used int8 rather than int from beebjit
     * (simplifies save states) */
    int8_t rx_sr_overflow;
    int8_t rx_sr_parity_error;
    int8_t rx_sr_framing_error;
    int8_t state;
    int8_t parity_accumulator; /* for RX */

    /* TOHv4.1: parity TX support was missing! */
    /* This is bitflipped whenever there is a 1-bit, so it will be
     * nonzero if an odd number of 1-bits have happened */
    uint8_t tx_odd_ones;

    int32_t rxc_count; /* TOHv4: for clock division */
    int32_t txc_count; /* TOHv4: for clock division */

    /* TOHv3.3: Callbacks now return an error code */
    int (*set_params)(ACIA *acia, uint8_t val);
/*    void (*rx_hook)(ACIA *acia, uint8_t byte); */ /* not needed */
    int (*tx_hook)(ACIA *acia, uint8_t byte);
    int (*tx_end)(ACIA *acia);     /* serial-to-file uses this */
    int (*reset_hook)(ACIA *acia);
    void *udata;                   /* contains tape_state_t for tape system */

    /* TOHv3: internal shift register state */
    uint8_t tx_shift_reg_value;
    int8_t  tx_shift_reg_shift;   /* current amount of shift */
    uint8_t tx_shift_reg_loaded;  /* is the shift reg loaded? */
    
    uint8_t tx_data_reg_loaded;

    /* TOHv3.3: moved from tape vars; implement hoglet delay */
    uint8_t tx_waiting_to_set_tdre;

    uint8_t msg_printed_recv_buf_full;

#ifdef BUILD_TAPE_DEV_MENU
    uint8_t corrupt_next_read;
    uint8_t misframe_next_read;
    uint8_t gen_parity_error_next_read;
#endif

    const char name[8];
    unsigned intnum;
    
    /* TOHv4: for Music 2000, we simplify things by
       introducing this setting that acknowledges TDRE
       immediately on data write, obliviating the need
       for any TX-side polling.
       
       If TX data is consumed bytewise from an ACIA's client
       rather than bitwise, and you do not need a realistically
       delayed TDRE, then set this flag; you avoid having to
       call acia_poll_2txc() at all. */
    uint8_t tx_no_polling;
    
};

void acia_init (ACIA *a); /* TOHv3.2 */

#ifdef BUILD_TAPE_INCOMPATIBLE_NEW_SAVE_STATES
int acia_savestate_BEMSNAP4(ACIA *acia, FILE *f);
int acia_loadstate_BEMSNAP4(ACIA *acia, FILE *f);
#endif

void acia_savestate(ACIA *acia, FILE *f);
void acia_loadstate(ACIA *acia, FILE *f);


uint8_t acia_read (ACIA *acia, uint16_t addr);
void acia_write (ACIA *acia, uint16_t addr, uint8_t val) ;


void acia_dcdhigh(ACIA *acia);
void acia_dcdlow(ACIA *acia);


uint8_t acia_rx_awaiting_start (ACIA *a); /* TOHv3.3 */
int acia_receive_bit_code (ACIA *acia, char code);
int acia_receive_bit (ACIA *acia, uint8_t bit);

uint8_t acia_rx_frame_ready (ACIA *a) ;
uint8_t acia_transmit (ACIA *acia);

/* TOHv3.3: from tape.c */
void acia_get_framing (uint8_t ctrl_reg, serial_framing_t *f);
void acia_hack_tx_reset_shift_register (ACIA *a);

void acia_ctson(ACIA *acia);
void acia_ctsoff(ACIA *acia);

/* TOHv4: */
/* Call this at RXC rate and it will do the clock division
   and set *fire_rxc_out=1 when a bit is required.
   When this happens, client should call acia_receive_bit()
   to supply the next bit. The status register may then be
   examined to discover when the completed frame is ready. */
int acia_poll_rxc (ACIA *acia,
                   uint8_t *fire_rxc_out,
                   int32_t *divider_out);

/* Meanwhile call this at 2TXC; it will deal with TDRE and
   operate the TX shift register. Bits that fall out of the
   shift register become available here if bit_or_zero_out
   is nonzero (which happens on every other call).
   Note that bit_or_zero_out uses ASCII values '1' and '0'
   rather than binary 1 and 0 (and '\0' if the bit is not
   ready yet). */
int acia_poll_2txc (ACIA *acia,
                    char *bit_or_zero_out, /* will be zero half the time */
                    int32_t *tx_divider_out);

int acia_run_tx_shift_register(ACIA *acia, serial_framing_t *f, char *bit_out);

/* TOHv4, for no-poll serial-to-file */
void acia_hack_consume_tx_byte_immediately (ACIA *acia);

/* TOHv4-rc2: exposed */
void acia_update_irq_and_status_read(ACIA *acia);

#endif
