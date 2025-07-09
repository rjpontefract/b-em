/*B-em v2.2 by Tom Walker
  Serial ULA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "led.h"
#include "serial.h"
#include "sysacia.h"
#include "tape.h"
#include "tapenoise.h"
#include "acia.h"

/* TOHv4 */
const int16_t serial_divider_by_bits[8] = { 1, 16, 4, 128, 2, 64, 8, 256 };

void serial_reset()
{
    /*Dunno what happens to this on reset*/
    tape_state.ula_ctrl_reg =0; // serial_transmit_rate = serial_recive_rate=0;

    tape_stop_motor(&tape_state); /* TOHv3.2: move motor variable into tape.c */

    tape_state.start_bit_wait_count_1200ths = 0;
    tape_state.ula_dcd_tape = 0; /* TOHv3.3 */
    
    serial_push_dcd_cts_lines_to_acia(&sysacia, &tape_state); /* TOHv3.3 (from beebjit) */
//
}


void serial_write (uint16_t addr, uint8_t val)
{

        tape_state.ula_ctrl_reg = val;
        
        int new_motor = val & 0x80;
        if ( new_motor && ! tape_state.ula_motor ) { /* TOHv3.2 */
            log_debug("serial: cassette motor on");
            tape_start_motor(&tape_state, tape_vars.strip_silence_and_leader); /* TOHv3.2 */
        }
        else if ( ( ! new_motor ) && tape_state.ula_motor ) { /* TOHv3.2 */
            log_debug("serial: cassette motor off");
            tape_stop_motor(&tape_state);     /* TOHv3.2 */
        }
        
        /* TOHv4: now precalculate dividers and thresholds on register writes,
           to reduce workload in 6502.c */
        serial_recompute_dividers_and_thresholds (tape_vars.overclock,
                                                  tape_state.ula_ctrl_reg,
                                                  &(tape_state.ula_rx_thresh_ns),
                                                  &(tape_state.ula_tx_thresh_ns),
                                                  &(tape_state.ula_rx_divider),
                                                  &(tape_state.ula_tx_divider));

        serial_push_dcd_cts_lines_to_acia(&sysacia, &tape_state);

}

/* TOHv4 */
/* called by 6502.c on both edges of TXC to the ACIA */
int serial_2txc_clock_for_tape (tape_state_t *ts,
                                tape_vars_t *tv,
                                ACIA *acia,
                                uint8_t emit_tapenoise) {

    int e;
    uint8_t bitclk;
    int32_t acia_tx_divider;
    char bit_out;
    int64_t ns_per_bit;
    serial_framing_t f;
    uint8_t silent;

    acia_tx_divider = 0;
    bit_out         = 0;
    
    /* FIXME? don't want to make an expensive cross-module call into acia.c?
       We could make this more efficient by using three lookup arrays
       for bits/parity/stops instead */
    acia_get_framing (acia->control_reg, &f);

    /* TDRE delay is handled in here */
    e = acia_poll_2txc (acia, &bit_out, &acia_tx_divider);

    if (0 == ts->ula_tx_divider) { return TAPE_E_OK; }

    bitclk = 1;
    if (TAPE_E_ACIA_TX_BITCLK_NOT_READY == e) { /* no bit available yet */
        bitclk = 0;
        e = TAPE_E_OK; /* not an error */
    }

    /* we will need to work out how many tones there are per bit */
    ns_per_bit = (13 * acia_tx_divider * ts->ula_tx_divider * 1000) / 16;

    if (bitclk) {
        e = acia_run_tx_shift_register(acia, &f, &bit_out);
        if (TAPE_E_OK != e) { return e; }
    }

    if ( bitclk && ts->ula_motor ) {

        silent = ((acia->control_reg & ACIA_CTRLREG_MASK_TXCTRL) == ACIA_CTRLREG_TXCTRL_2);

        e = tape_write_bitclk (ts,
                               acia,
                               bit_out,
                               ns_per_bit,
                               silent,
                               emit_tapenoise && tv->record_activated,
                               tv->record_activated,
                               tv->save_always_117,
                               tv->save_prefer_112,
                               tv->save_do_not_generate_origin_on_append);
        if (TAPE_E_OK != e) { return e; }
    }

    return e;
}

/* TOHv4 */
/* called by 6502.c when RXC to the ACIA fires */
int serial_rxc_clock_for_tape (tape_state_t *ts,
                               tape_vars_t *tv,
                               ACIA *acia,
                               uint8_t emit_tapenoise,
                               uint8_t *throw_eof_out) {
                               
    int32_t acia_rx_divider;
    uint8_t fire_acia_rxc;
    int e;
                               
    /* FIXME: the poll interface to the ACIA is stupid atm: */
    /* i)  client calls acia_poll_rxc() at incident RXC frequency to see
           if divided clock has fired;
       ii) if so, then the ACIA needs to receive a bit, so go fetch a bit
           from somewhere and call acia_receive_bit() with it
       
       a better approach might be to register a callback function,
       e.g. acia_register_rxc_bit_source(myfunc), where
       acia_poll_rxc() will maybe call "myfunc" to supply the next RX bit */

    /* clock ACIA with divided clock from serial ULA;
       is a bit needed from the tape? (also fetch divider) */
    acia_rx_divider = 0; /* compiler is stupid */
    e = acia_poll_rxc (acia, &fire_acia_rxc, &acia_rx_divider);
    if (TAPE_E_OK != e) { return e; }

    if (fire_acia_rxc) {

        /* get bit from tape, place in ACIA; preserve it for DCD; handle tapenoise */
        e = tape_fire_acia_rxc (ts, tv, acia, acia_rx_divider, emit_tapenoise, throw_eof_out);
        
    } /* endif (ACIA RX clock fires) */

    return e;

}


/* TOHv4 */
void serial_recompute_dividers_and_thresholds (uint8_t overclock_tape, /* OCing in ULA rather than ACIA */
                                               uint8_t ctrl_reg_value,
                                               int32_t *rx_thresh_ns_out,
                                               int32_t *tx_thresh_ns_out,
                                               int32_t *rx_divider_out,
                                               int32_t *tx_divider_out) {
                                               
    /* determine clock divider values for TXC and RXC */
    if ( ! ( ctrl_reg_value & 0x40) ) {
        *rx_divider_out = overclock_tape ? 6 : 64; /* tape fixes divider at 64 */
    } else {
        *rx_divider_out = serial_divider_by_bits[7 & (ctrl_reg_value>>3)];
    }
    *tx_divider_out = serial_divider_by_bits[7 & ctrl_reg_value];

    /* compute time thresholds for divided clocks */
    *rx_thresh_ns_out = (13 * *rx_divider_out * 1000) / 16;
    /* this must be 2TXC because of the 0.5-bits of TDRE delay requirement */
    *tx_thresh_ns_out = (13 * (*tx_divider_out) * 1000) / 32;
    
}


/* adapted from beebjit, (c) Chris Evans, under GPL: */
void serial_push_dcd_cts_lines_to_acia (ACIA *acia, tape_state_t *ts) {

    int cts;
    int dcd;
    /*int chow;
    static int cts_old=0;
      struct mc6850_struct* p_serial = p_serial_ula->p_serial;*/

    /* CTS. When tape is selected, CTS is always low (meaning active). For RS423,
     * it is high (meaning inactive) unless we've connected a virtual device on
     * the other end.
     */
    /*chow=0;*/

    dcd = 0;
    cts = 1;

    /*  if (ts->ula_rs423_mode) { */
    if (ts->ula_ctrl_reg & 0x40) {
        if (ts->ula_have_serial_sink) { /*p_serial_ula->handle_output != -1) {*/
            cts = 0;
        } else {
            cts = 1;
        }
    /*    chow = 1;*/
    } else {
        cts = 0; //0;
    /*    chow = 2;*/
    }

    /* DCD. In the tape case, it depends on a carrier tone on the tape.
     * For the RS423 case, AUG clearly states: "It will always be low when the
     * RS423 interface is selected".
     */
    if (ts->ula_ctrl_reg & 0x40) {
        dcd = 0;
    } else {
        dcd = ts->ula_dcd_tape;
    }

    /* push DCD line value through to ACIA */
    if (dcd) {
        acia_dcdhigh(acia);
    } else {
        acia_dcdlow(acia);
    }

    if (cts) {
        acia_ctson(acia);
    } else {
        acia_ctsoff(acia);
    }

}


uint8_t serial_read(uint16_t addr)
{
        /*Reading from this has the same effect as writing &FE*/
        serial_write(0, 0xFE);
        return 0;
}

void serial_loadstate(FILE *f)
{
        serial_reset(); /* TOHv4 */
        serial_write(0, getc(f)); /* recomputes divider and threshold fields */
}

#define TAPE_BLIPTICKS_BEFORE_DCD_BLIP_FAST 217
#define TAPE_BLIPTICKS_BEFORE_DCD_BLIP_SLOW 1083

#ifdef BUILD_TAPE_INCOMPATIBLE_NEW_SAVE_STATES
void serial_savestate_BEMSNAP4 (FILE *f)
{
        uint8_t bytes[24 + TAPE_TONES300_BUF_LEN]; /* i.e. [24 + 4] */
        uint8_t b4[4];
        uint8_t v;
        tape_state_t *ts;
        int e;
        
        v = TAPE_TONES300_BUF_LEN; /* 4 */
        ts = &(tape_state);
        
        bytes[0] = ts->ula_ctrl_reg;
        bytes[1] = ts->ula_prevailing_rx_bit_value;
        memcpy(2+bytes, ts->tones300, v);
        bytes[2+v] = 0x7f & ts->tones300_fill;
        tape_write_u32(b4, (uint32_t) ts->ula_rx_ns);
        memcpy(3+v+bytes, b4, 4);
        tape_write_u32(b4, (uint32_t) ts->ula_tx_ns);
        memcpy(7+v+bytes, b4, 4);
        bytes[11+v] = ts->ula_dcd_tape;
        tape_write_u32(b4, (uint32_t) ts->ula_dcd_2mhz_counter);
        memcpy(12+v+bytes, b4, 4);
        tape_write_u32(b4, (uint32_t) ts->ula_dcd_blipticks);
        memcpy(16+v+bytes, b4, 4);
        tape_write_u32(b4, (uint32_t) ts->ula_rs423_taperoll_2mhz_counter);
        memcpy(20+v+bytes, b4, 4);
        
        e = (1==fwrite(bytes, sizeof(bytes), 1, f)) ? TAPE_E_OK : TAPE_E_SAVESTATE;
        
        if (TAPE_E_OK != e) {
                log_warn("serial: save state: fwrite failed");
        }
        
        /* return e; */
  
}
void serial_loadstate_BEMSNAP4(FILE *f)
{

        uint8_t bytes[24 + TAPE_TONES300_BUF_LEN]; /* i.e. [24 + 4] */
        int32_t i;
        uint8_t v;
        tape_state_t *ts;
        long q;
        
        v = TAPE_TONES300_BUF_LEN; /* 4 */
        ts = &(tape_state);

        q = ftell(f);

        if (1 != fread(bytes, sizeof(bytes), 1, f)) {
                log_warn("serial: load state: read failed: %s", strerror(errno));
                return; /* TAPE_E_LOADSTATE; */
        }

        if ( ! TAPE_TONECODE_IS_LEGAL(bytes[1]) ) {
                log_warn("serial: load state @ pos &%lx: error @ ula_prevailing_rx_bit_value", 1+q);
                return; /* TAPE_E_LOADSTATE; */
        }
        ts->ula_prevailing_rx_bit_value = bytes[1];

        for (i=0; i < v; i++) {
                if ( ('\0' != bytes[i+1]) && ! TAPE_TONECODE_IS_LEGAL(bytes[i+1]) ) { /* TOHv4-rc6, -rc7: fixes */
                        log_warn("serial: load state @ pos &%lx: error @ tones300 (tonecode &%x)", 1+q+i, bytes[i+1]);
                        return; /* TAPE_E_LOADSTATE; */
                }
                ts->tones300[i] = bytes[i+1];
        }
        
        if (bytes[2+v] >= v) {
                log_warn("serial: load state: error @ tones300_fill");
                return; /* TAPE_E_LOADSTATE; */
        }
        ts->tones300_fill = bytes[2+v];
        
        /* (this will be validated in a moment) */
        ts->ula_rx_ns    = 0x7fffffff & tape_read_u32(3+v+bytes);
        ts->ula_tx_ns    = 0x7fffffff & tape_read_u32(7+v+bytes);
        
        ts->ula_dcd_tape = (bytes[11+v] ? 1 : 0);
        
        ts->ula_dcd_2mhz_counter = 0x7fffffff & tape_read_u32(12+v+bytes);
        if (ts->ula_dcd_2mhz_counter > TAPE_DCD_BLIP_TICKS_2MHZ) {
                log_warn("serial: load state: error @ ula_dcd_2mhz_counter");
                return; /* TAPE_E_LOADSTATE; */
        }
        
        ts->ula_dcd_blipticks = 0x7fffffff & tape_read_u32(16+v+bytes);
        if (ts->ula_dcd_blipticks > (1 + TAPE_BLIPTICKS_BEFORE_DCD_BLIP_SLOW)) {
                log_warn("serial: load state: error @ ula_dcd_blipticks");
                return; /* TAPE_E_LOADSTATE; */
        }
        
        ts->ula_rs423_taperoll_2mhz_counter = 0x7fffffff & tape_read_u32(20+v+bytes);
        if (ts->ula_rs423_taperoll_2mhz_counter > TAPE_1200TH_IN_2MHZ_INT) {
                log_warn("serial: load state: error @ ula_rs423_taperoll_2mhz_counter");
                return; /* TAPE_E_LOADSTATE; */
        }

        /* this will recompute divider and threshold fields */
        serial_write(0, bytes[0]);
        
        /* NOW we can sanity-check the ula_Xx_ns fields against
           their thresholds; we couldn't do this before we had
           calculated thresholds. */
        if (ts->ula_rx_ns > ts->ula_rx_thresh_ns) {
                log_warn("serial: load state: error ula_rx_ns(%d) > ula_rx_thresh_ns(%d)",
                         ts->ula_rx_ns, ts->ula_rx_thresh_ns);
                return; /* TAPE_E_LOADSTATE; */
        }
        if (ts->ula_tx_ns > ts->ula_tx_thresh_ns) {
                log_warn("serial: load state: error @ ula_tx_ns");
                return; /* TAPE_E_LOADSTATE; */
        }
        
}
#else
void serial_savestate(FILE *f)
{
        putc(tape_state.ula_ctrl_reg, f);
}
#endif

void serial_handle_dcd_tick (tape_state_t *ts,
                             tape_vars_t *tv,
                             ACIA *acia) {
    if (ts->ula_motor) {
        serial_poll_dcd_blipticks (ts->ula_prevailing_rx_bit_value,
                                   tv->overclock || tv->strip_silence_and_leader, /* fast DCD? */
                                   &(ts->ula_dcd_blipticks),  /* DCD's blips counter UPDATED */
                                   &(ts->ula_dcd_tape));      /* incident line updated */
    } else {
        /* HACK: this probably should be updated at 2 MHz
           resolution, rather than at fire_dcd resolution,
           but it is less CPU intensive this way. Essentially
           means that when the motor is turned off, it will
           take up to ~211uS for DCD to change. */
        ts->ula_dcd_tape = 0; /* incident line updated */
    }
    serial_push_dcd_cts_lines_to_acia (acia, ts);
}




void serial_poll_dcd_blipticks (char bit_value_or_null,            /* nulls are just ignored */
                                uint8_t fast_dcd,
                                int32_t *dcd_bliptick_count_inout, /* blip-periods counter */
                                uint8_t *dcd_line_inout) {         /* DCD line to be manipulated */
                                
    int32_t ticks_until_blip;
    int32_t fast, slow;
    
    fast = TAPE_BLIPTICKS_BEFORE_DCD_BLIP_FAST;
    slow = TAPE_BLIPTICKS_BEFORE_DCD_BLIP_SLOW;
    
    ticks_until_blip = fast_dcd ? fast : slow; /* ? 217 : 1083; */
/*    ticks_until_blip = fast_dcd ? 92000 : 458000;*/ /* old 2 MHz numbers */

    *dcd_line_inout = 0; /* may be changed later */
    
    if (('0' == bit_value_or_null) || ('S' == bit_value_or_null)) {
        *dcd_bliptick_count_inout = 0;
    }
    
    if (*dcd_bliptick_count_inout < ticks_until_blip) {
        (*dcd_bliptick_count_inout)++;
    } else if (*dcd_bliptick_count_inout == ticks_until_blip) {
        *dcd_line_inout = 1;
        (*dcd_bliptick_count_inout)++;
/*printf("DCD blip\n");*/
    }
    /* dcd_bliptick_count_inout final value is (ticks_until_blip+1),
       where it will remain, until a '0' or 'S' resets it. */
    
}
