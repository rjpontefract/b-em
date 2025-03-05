/*B-em v2.2 by Tom Walker
  6850 ACIA emulation*/

/* - TOHv4 overhaul by 'Diminished' */

/* some code (mc6850_... functions)
   taken from beebjit, (c) Chris Evans, under GPL */

#include <stdio.h>
#include "b-em.h"
#include "6502.h"
#include "acia.h"

/* Status register flags */
#include "tape2.h" // error codes

/* control register flags (now named in TOHv3) */
#define CTRL_DIVSEL_1   0x1
#define CTRL_DIVSEL_2   0x2
#define CTRL_WORDSEL_1  0x4
#define CTRL_WORDSEL_2  0x8
#define CTRL_WORDSEL_3 0x10
/* TOHv3: renamed and moved transmit control flags into acia.h */
/*#define CTRL_TXCTRL_1  0x20
#define CTRL_TXCTRL_2  0x40*/
#define CTRL_IRQ       0x80


/* ********** BEGIN BEEBJIT CODE **********
   mc6850_... functions and these enums are taken from beebjit,
   (c) Chris Evans, under GPL */
static uint8_t mc6850_read(ACIA *acia, uint8_t reg);
static void mc6850_write(ACIA *acia, uint8_t reg, uint8_t val);
static void mc6850_set_DCD(ACIA *acia, uint8_t is_DCD);
static int mc6850_receive(ACIA *acia, uint8_t byte);
static int mc6850_receive_bit (ACIA *acia, uint8_t bit);
static void mc6850_reset(ACIA *acia);
void mc6850_set_CTS(ACIA *acia, uint8_t is_CTS);
static void mc6850_update_irq_and_status_read(ACIA *acia);
enum {
  k_serial_acia_control_clock_divider_mask = 0x03,
  k_serial_acia_control_clock_divider_64 = 0x02,
  k_serial_acia_control_8_bits = 0x10,
  k_serial_acia_control_TCB_mask = 0x60,
  k_serial_acia_control_RIE = 0x80,
};
enum {
  k_serial_acia_TCB_RTS_and_TIE =   0x20,
  k_serial_acia_TCB_no_RTS_no_TIE = 0x40,
};
enum {
  k_mc6850_state_null = 0,
  k_mc6850_state_need_start = 1,
  k_mc6850_state_need_data = 2,
  k_mc6850_state_need_parity = 3,
  k_mc6850_state_need_stop = 4,
};
/* ********** END BEEBJIT CODE ********** */

static int run_tx_shift_register (ACIA *acia, serial_framing_t *f, char *bit_out);
static int wp3_cp_tx_datareg_to_shiftreg (ACIA *a);
static void reset_tx_shift_register(ACIA *a) ;
static int acia_tx_shift (ACIA *acia, serial_framing_t *f);
static int poll_maybe_cp_datareg_to_shiftreg (ACIA *a);

/* glue */
static void state_6502_set_irq_level (int level) {
  if (level) {
    interrupt |= 4;
  } else {
    interrupt &= ~4;
  }
//  printf("state_6502_set_irq_level: 6502 interrupt now %x\n", interrupt);
}

#include "sysacia.h"

/* TOHv4: used to implement no-poll mode for serial-to-file */
void acia_hack_consume_tx_byte_immediately (ACIA *acia) {
  acia->tx_data_reg_loaded  = 0;
  acia->tx_shift_reg_loaded = 0;
  acia->tx_shift_reg_shift  = 0;
  acia->status_reg |= k_serial_acia_status_TDRE;
  mc6850_update_irq_and_status_read(acia);
}

/* glue */
uint8_t acia_read (ACIA *acia, uint16_t addr) {
  return mc6850_read(acia, addr & 1);
}

/* glue */
void acia_write (ACIA *acia, uint16_t addr, uint8_t val) {
    mc6850_write (acia, addr & 1, val);
}

/* TOHv4: do RX clock divider */
int acia_poll_rxc (ACIA *acia,
                   uint8_t *fire_rxc_out,
                   int32_t *divider_out) {
                   
    uint8_t cr3;

    *divider_out      = 0; /* cat, pigeons */
    cr3 = 3 & acia->control_reg;

    *fire_rxc_out = 0;

    if (0x0 == cr3) {
        *divider_out = 1;     /* if tape selected then 19200 baud */
    } else if (0x1 == cr3) {
        *divider_out = 16;    /* if tape selected then  1200 baud */
    } else if (0x2 == cr3) {
        *divider_out = 64;    /* if tape selected then   300 baud */
    } else {
        log_warn("acia: BUG: acia_poll_rxc: bad control_reg &%x\n", acia->control_reg);
        return TAPE_E_BUG;
    }

    if (acia->rxc_count >= *divider_out) {
        acia->rxc_count = 0;
        *fire_rxc_out   = 1; /* fire divided clock */
     }

    (acia->rxc_count)++;

    return TAPE_E_OK;

}

void acia_get_framing (uint8_t ctrl_reg, serial_framing_t *f) {

    if ((ctrl_reg & 0x1c) == 0) {
        f->num_data_bits = 7;
        f->num_stop_bits = 2;
        f->parity = 'E';
    } else if ((ctrl_reg & 0x1c) == 4) {
        f->num_data_bits = 7;
        f->num_stop_bits = 2;
        f->parity = 'O';
    } else if ((ctrl_reg & 0x1c) == 8) {
        f->num_data_bits = 7;
        f->num_stop_bits = 1;
        f->parity = 'E';
    } else if ((ctrl_reg & 0x1c) == 0xc) {
        f->num_data_bits = 7;
        f->num_stop_bits = 1;
        f->parity = 'O';
    } else if ((ctrl_reg & 0x1c) == 0x10) {
        f->num_data_bits = 8;
        f->num_stop_bits = 2;
        f->parity = 'N';
    } else if ((ctrl_reg & 0x1c) == 0x14) {
        f->num_data_bits = 8;
        f->num_stop_bits = 1;
        f->parity = 'N';
    } else if ((ctrl_reg & 0x1c) == 0x18) {
        f->num_data_bits = 8;
        f->num_stop_bits = 1;
        f->parity = 'E';
    } else if ((ctrl_reg & 0x1c) == 0x1c) {
        f->num_data_bits = 8;
        f->num_stop_bits = 1;
        f->parity = 'O';
    }

 }


int acia_run_tx_shift_register(ACIA *acia, serial_framing_t *f, char *bit_out) {
    return run_tx_shift_register(acia,f,bit_out);
 }

static int run_tx_shift_register (ACIA *acia, serial_framing_t *f, char *bit_out) {

    int e;

    if (acia->tx_shift_reg_loaded) {
        e = acia_tx_shift (acia, f);
        if (TAPE_E_OK != e) { return e; }
        if (NULL != bit_out) {
            *bit_out = (acia->tx_shift_reg_value & 1) ? '1' : '0';
        }
     }

    e = poll_maybe_cp_datareg_to_shiftreg(acia);

    return e;

}


static int acia_tx_shift (ACIA *acia, serial_framing_t *f) {

#ifdef BUILD_TAPE_SANITY
    if ((8!=f->num_data_bits)&&(7!=f->num_data_bits)) {
        log_warn("acia: BUG: framing illegal (%u data bits)", f->num_data_bits);
        return TAPE_E_BUG;
     }
#endif

    if ( (acia->tx_shift_reg_shift > 0) )  {
        acia->tx_shift_reg_value >>= 1;
    }

    if (acia->tx_shift_reg_shift < (1 + f->num_data_bits)) {
        (acia->tx_shift_reg_shift)++;
    } else if (acia->tx_shift_reg_shift == (1 + f->num_data_bits)) { /* frame done */
        acia->tx_shift_reg_loaded = 0;
        (acia->tx_shift_reg_shift) = 0;
    } else {
        log_warn("acia: illegal shift %d", acia->tx_shift_reg_shift);
        return TAPE_E_BUG;
     }

    return TAPE_E_OK;

}

void acia_dcdhigh(ACIA *acia) {
  mc6850_set_DCD(acia, 1);
}

void acia_dcdlow(ACIA *acia) {
  mc6850_set_DCD(acia, 0);
}



#include "tape.h"

#ifdef BUILD_TAPE_INCOMPATIBLE_NEW_SAVE_STATES
int acia_savestate_BEMSNAP4(ACIA *acia, FILE *f)
{

    uint8_t bytes[26];
    uint8_t u4[4];

    bytes[0]  = acia->control_reg;
    bytes[1]  = acia->status_reg;
    bytes[2]  = acia->line_cts;
    bytes[3]  = acia->line_dcd;
    bytes[4]  = acia->rx_data_reg;
    bytes[5]  = acia->tx_data_reg;
    bytes[6]  = acia->rx_shift_reg_count;
    bytes[7]  = acia->rx_shift_reg;
    bytes[8]  = (uint8_t) acia->rx_sr_overflow;
    bytes[9] = (uint8_t) acia->rx_sr_parity_error;
    bytes[10] = (uint8_t) acia->rx_sr_framing_error;
    bytes[11] = (uint8_t) acia->state;
    bytes[12] = (uint8_t) acia->parity_accumulator;
    tape_write_u32(u4, (uint32_t) acia->rxc_count);
    memcpy(bytes+13, u4, 4);
    tape_write_u32(u4, (uint32_t) acia->txc_count);
    memcpy(bytes+17, u4, 4);
    bytes[21] = acia->tx_shift_reg_value;
    bytes[22] = (uint8_t) acia->tx_shift_reg_shift;
    bytes[23] = acia->tx_shift_reg_loaded;
    bytes[24] = acia->tx_data_reg_loaded;
    bytes[25] = acia->tx_waiting_to_set_tdre;

    return (1==fwrite(bytes, sizeof(bytes), 1, f)) ? TAPE_E_OK : TAPE_E_SAVESTATE;

}


int acia_loadstate_BEMSNAP4(ACIA *acia, FILE *f)
{
    uint8_t bytes[26];
    int8_t s;

    if (1 != fread(bytes, sizeof(bytes), 1, f)) {
        log_warn("acia: load state: read failed: %s", strerror(errno));
        return TAPE_E_LOADSTATE;
    }
    
    if (bytes[2]&0xfe) {
        log_warn("acia: load state: state is corrupt @ line_cts");
        return TAPE_E_LOADSTATE;
    }
    if (bytes[3]&0xfe) {
        log_warn("acia: load state: state is corrupt @ line_dcd");
        return TAPE_E_LOADSTATE;
    }
    if (bytes[8]&0xfe) {
        log_warn("acia: load state: state is corrupt @ rx_sr_overflow");
        return TAPE_E_LOADSTATE;
    }
    if (bytes[9]&0xfe) {
        log_warn("acia: load state: state is corrupt @ rx_sr_parity_error");
        return TAPE_E_LOADSTATE;
    }
    if (bytes[10]&0xfe) {
        log_warn("acia: load state: state is corrupt @ rx_sr_framing_error");
        return TAPE_E_LOADSTATE;
    }
    s = (int8_t) bytes[11];
    if (    (s!=k_mc6850_state_null)
         && (s!=k_mc6850_state_need_start)
         && (s!=k_mc6850_state_need_data)
         && (s!=k_mc6850_state_need_parity)
         && (s!=k_mc6850_state_need_stop)) {
        log_warn("acia: load state: state is corrupt @ state");
        return TAPE_E_LOADSTATE;
    }
    if (bytes[22]>9) { /* illegal shift reg shift */
        log_warn("acia: load state: state is corrupt @ tx_shift_reg_shift (%u)", bytes[22]);
        return TAPE_E_LOADSTATE;
    }

    acia->control_reg            =           bytes[ 0];
    acia->status_reg             =           bytes[ 1];
    acia->line_cts               =           bytes[ 2] ? 1 : 0;
    acia->line_dcd               =           bytes[ 3] ? 1 : 0;
    acia->rx_data_reg            =           bytes[ 4];
    acia->tx_data_reg            =           bytes[ 5];
    acia->rx_shift_reg_count     =           bytes[ 6];
    acia->rx_shift_reg           =           bytes[ 7];
    acia->rx_sr_overflow         =           bytes[ 8] ? 1 : 0;
    acia->rx_sr_parity_error     =           bytes[ 9] ? 1 : 0;
    acia->rx_sr_framing_error    =           bytes[10] ? 1 : 0;
    acia->state                  =           s;
    acia->parity_accumulator     = ( int8_t) bytes[12];
    acia->rxc_count              = (int32_t) tape_read_u32(bytes+13); /* u32 -> i32 */
    acia->txc_count              = (int32_t) tape_read_u32(bytes+17); /* u32 -> i32 */
    acia->tx_shift_reg_value     =           bytes[21];
    acia->tx_shift_reg_shift     = ( int8_t) bytes[22];
    acia->tx_shift_reg_loaded    =           bytes[23] ? 1 : 0;
    acia->tx_data_reg_loaded     =           bytes[24] ? 1 : 0;
    acia->tx_waiting_to_set_tdre =           bytes[25] ? 1 : 0;
    
    mc6850_update_irq_and_status_read(acia);

    return TAPE_E_OK;

}
#else /* not def. BUILD_TAPE_INCOMPATIBLE_NEW_SAVE_STATES */
/* old BEMSNAP3 save states */
void acia_savestate(ACIA *acia, FILE *f)
{
    unsigned char bytes[2];
    bytes[0] = acia->control_reg;
    bytes[1] = acia->status_reg;
    fwrite(bytes, sizeof(bytes), 1, f);
}
#endif

void acia_loadstate(ACIA *acia, FILE *f)
{
    unsigned char bytes[2];
    fread(bytes, sizeof(bytes), 1, f);
    acia->control_reg = bytes[0];
    acia->status_reg = bytes[1];
    mc6850_update_irq_and_status_read(acia);
}


void acia_hack_tx_reset_shift_register (ACIA *a) {
    /*log_warn("WARNING! acia_hack_tx_reset_shift_register()");*/
    reset_tx_shift_register(a);
}

static void reset_tx_shift_register(ACIA *a) {
    a->tx_shift_reg_shift   = 0;
    a->tx_shift_reg_loaded  = 0;
    a->tx_shift_reg_value   = 0; /* TOHv3.2 */
}

int acia_receive_bit (ACIA *acia, uint8_t bit) {
  return mc6850_receive_bit (acia, bit);
}

int acia_receive_bit_code (ACIA *acia, char code) {
  uint8_t bit;
  /*if ((code != '1') && (code != '0') && (code != 'S') && (code != 'L')) {*/
  if ( ! TAPE_TONECODE_IS_LEGAL(code) ) {
    log_warn("acia: BUG: acia_receive_bit_code: bad code: %x", code);
    return TAPE_E_BUG;
  }
/* putchar(code);fflush(stdout); */
  bit = (code == '0') ? 0 : 1;
  return mc6850_receive_bit (acia, bit);
}


uint8_t acia_rx_awaiting_start (ACIA *a) {
  return (k_mc6850_state_need_start == a->state);
}

uint8_t acia_rx_frame_ready (ACIA *a) {
  return (a->state == k_mc6850_state_need_stop);
}


void acia_init (ACIA *acia) {

    acia->line_cts                   = 0;
    acia->line_dcd                   = 0;
    acia->control_reg                = 0;
    acia->status_reg                 = 0;
    acia->status_reg_for_read        = 0;
    acia->rx_data_reg                = 0;
    acia->tx_data_reg                = 0;
    acia->rx_shift_reg_count         = 0;
    acia->rx_sr_overflow             = 0;
    acia->rx_sr_parity_error         = 0;
    acia->rx_sr_framing_error        = 0;
    acia->state                      = k_mc6850_state_need_start;
    acia->parity_accumulator         = 0;
    acia->rxc_count                  = 0;
    acia->txc_count                  = 0;
    acia->tx_shift_reg_value         = 0;
    acia->tx_shift_reg_shift         = 0;
    acia->tx_shift_reg_loaded        = 0;
    acia->tx_waiting_to_set_tdre     = 0;
    acia->msg_printed_recv_buf_full  = 0;
#ifdef BUILD_TAPE_DEV_MENU
    acia->corrupt_next_read          = 0;
    acia->misframe_next_read         = 0;
    acia->gen_parity_error_next_read = 0;
#endif

    mc6850_reset(acia);

}


/* TOHv3.3: moved here from tape.c */
/* synced to tx bitclk */

static int
wp3_cp_tx_datareg_to_shiftreg (ACIA *acia) {

/*printf("wp3_cp_tx_datareg_to_shiftreg\n");*/

    /* yes; copy data reg into shift reg */
    acia->tx_shift_reg_value  = acia->tx_data_reg;
    acia->tx_shift_reg_loaded = 1;
    acia->tx_shift_reg_shift  = 0;
    
    /* empty data reg */
    acia->tx_data_reg_loaded = 0;

    /* TOHv4 */
    acia->tx_waiting_to_set_tdre = 1;
    
    mc6850_update_irq_and_status_read(acia);

    return TAPE_E_OK;
    
}


static int poll_maybe_cp_datareg_to_shiftreg (ACIA *acia) {

    int e;
    
    /* tx data pipelining, part two */
    if (    ( ! acia->tx_shift_reg_loaded )   /* if shift reg is emptied ... */
         &&     acia->tx_data_reg_loaded  ) { /* and data reg is able to replenish it ... */
        
        e = wp3_cp_tx_datareg_to_shiftreg (acia);
        if (TAPE_E_OK != e)  { return e; }
        
    }
    
    return TAPE_E_OK;

}


void acia_ctson(ACIA *acia)
{
    mc6850_set_CTS(acia, 1);
}

void acia_ctsoff(ACIA *acia)
{
    mc6850_set_CTS(acia, 0);
}


/* TOHv4 */
                     
int acia_poll_2txc (ACIA *acia,
                    char *bit_or_zero_out, /* will be zero half the time */
                    int32_t *tx_divider_out) {
                     
    uint8_t half_bit;
    uint8_t full_bit;
    int e;
    
    e = TAPE_E_OK;
                     
    /* polled at 2x baud speed */

    half_bit         = 0;
    full_bit         = 0;
    *tx_divider_out  = 64;
    *bit_or_zero_out = 0;
    
    if (0 == (3 & acia->control_reg)) {
        *tx_divider_out = 1;
    } else if (1 == (3 & acia->control_reg) ) {
        *tx_divider_out = 16;
    }

    /* if the divider has been changed, txc_count may now
       be out of bounds; scale it to the new divider, but
       keep its entropy */
    while (acia->txc_count > (2 * *tx_divider_out)) {
        acia->txc_count -= (2 * *tx_divider_out);
    }
    
    if (acia->txc_count == (2 * *tx_divider_out)) { /* every 2 half-bits */
        full_bit = 1;
        half_bit = 1;
        acia->txc_count = 0;
    } else if (acia->txc_count == *tx_divider_out) { /* every half-bit */
        half_bit = 1;
    }
    
    /* wait for one half-bit before acknowledging TDRE */
    if (half_bit && acia->tx_waiting_to_set_tdre) {
        acia->tx_waiting_to_set_tdre =  0;
        acia->status_reg             |= k_serial_acia_status_TDRE;
        mc6850_update_irq_and_status_read(acia);
    }
    
    (acia->txc_count)++;
    
    if ( ! full_bit ) {
        e = TAPE_E_ACIA_TX_BITCLK_NOT_READY;
    }
    
    return e;
    
}

/* TOHv4-rc2 */
void acia_update_irq_and_status_read(ACIA *acia) {
    mc6850_update_irq_and_status_read(acia);
}



/* ********** REMAINING CODE IS TAKEN FROM BEEBJIT **********
                  (c) Chris Evans, under GPL
   ********************************************************** */

 
static void
mc6850_update_irq_and_status_read(ACIA *acia) {

  int do_check_send_int;
  int do_check_receive_int;
  int do_fire_int;
  uint8_t acia_status_for_read;
  int do_fire_send_int = 0;
  int do_fire_receive_int = 0;

  do_check_send_int = (    (acia->control_reg & k_serial_acia_control_TCB_mask)
                        == k_serial_acia_TCB_RTS_and_TIE);
  if (do_check_send_int) {
    do_fire_send_int = !!(acia->status_reg & k_serial_acia_status_TDRE);
    do_fire_send_int &= !(acia->status_reg & k_serial_acia_status_CTS); // if high, inhibit IRQ
  }
  
  do_check_receive_int = !!(acia->control_reg & k_serial_acia_control_RIE);
  if (do_check_receive_int) {
    do_fire_receive_int = !!(acia->status_reg & k_serial_acia_status_RDRF);
    do_fire_receive_int |= !!(acia->status_reg & k_serial_acia_status_DCD);
    do_fire_receive_int |= !!(acia->status_reg & k_serial_acia_status_OVRN);
  }

  do_fire_int = (do_fire_send_int | do_fire_receive_int);

  /* Bit 7 of the control register must be high if we're asserting IRQ. */
  acia->status_reg &= ~k_serial_acia_status_IRQ;
  if (do_fire_int) {
    acia->status_reg |= k_serial_acia_status_IRQ;
  }

  state_6502_set_irq_level(do_fire_int);

  /* Update the raw read value for the status register. */
  acia_status_for_read = acia->status_reg;

  /* EMU MC6850: "A low CTS indicates that there is a Clear-to-Send from the
   * modem. In the high state, the Transmit Data Register Empty bit is
   * inhibited".
   */
  if (acia->status_reg & k_serial_acia_status_CTS) {
    acia_status_for_read &= ~k_serial_acia_status_TDRE;
  }

  /* If the "DCD went high" bit isn't latched, it follows line level. */
  /* EMU MC6850, more verbosely: "It remains high after the DCD input is
   * returned low until cleared by first reading the Status Register and then
   * Data Register or until a master reset occurs. If the DCD input remains
   * high after read status read data or master reset has occurred, the
   * interrupt is cleared, the DCD status bit remains high and will follow
   * the DCD input."
   * Note that on real hardware, only a read of data register seems required
   * to unlatch the DCD high condition.
   */
  if (acia->line_dcd && !acia->line_cts) {
    acia_status_for_read |= k_serial_acia_status_DCD;
  }

  /* EMU TODO: MC6850: "Data Carrier Detect being high also causes RDRF to
   * indicate empty."
   * Unclear if that means the line level, latched DCD status, etc.
   */

  acia->status_reg_for_read = acia_status_for_read;
}


static void
mc6850_set_DCD(ACIA *acia, uint8_t is_DCD) {
  /* The DCD low to high edge causes a bit latch, and potentially an IRQ.
   * DCD going from high to low doesn't affect the status register until the
   * bit latch is cleared by reading the data register.
   */
   
  if (is_DCD && !acia->line_dcd) {
    acia->status_reg |= k_serial_acia_status_DCD;
    mc6850_update_irq_and_status_read(acia);
  }
  
  acia->line_dcd = is_DCD;
 
}

void
mc6850_set_CTS(ACIA *acia, uint8_t is_CTS) {
  acia->status_reg &= ~k_serial_acia_status_CTS;
  if (is_CTS) {
    acia->status_reg |= k_serial_acia_status_CTS;
  }
  acia->line_cts = is_CTS;
  if (!is_CTS) {
    acia_dcdlow(acia);
  }
  mc6850_update_irq_and_status_read(acia);
}

int
mc6850_get_RTS(ACIA *acia) {
  if ((acia->control_reg & k_serial_acia_control_TCB_mask) ==
          k_serial_acia_TCB_no_RTS_no_TIE) {
    return 0;
  }
  if (acia->status_reg & k_serial_acia_status_RDRF) {
    return 0;
  }
  return 1;
}

static int
mc6850_receive(ACIA *acia, uint8_t byte) {

  if (acia->status_reg & k_serial_acia_status_RDRF) {
/*    log_do_log(k_log_serial, k_log_info, "receive buffer full"); */
    if ( ! acia->msg_printed_recv_buf_full ) {
      log_warn("acia (%s): receive buffer full", acia->name);
      acia->msg_printed_recv_buf_full = 1;
    }
  } else {
    acia->msg_printed_recv_buf_full = 0;
  }
  acia->status_reg |= k_serial_acia_status_RDRF;
  acia->status_reg &= ~k_serial_acia_status_FE;
  acia->status_reg &= ~k_serial_acia_status_PE;
  acia->rx_data_reg = byte;

  mc6850_update_irq_and_status_read(acia);
  
  if (acia->msg_printed_recv_buf_full) { return 0; }

  return 1;
  
}

static void
mc6850_clear_receive_state(ACIA *acia) {
  acia->rx_shift_reg = 0;
  acia->rx_shift_reg_count = 0;
  acia->parity_accumulator = 0;
  acia->rxc_count = 0;
  acia->rx_sr_parity_error = 0;
  acia->rx_sr_framing_error = 0;
}

static void
mc6850_transfer_sr_to_receive(ACIA *acia) {
  int ok = mc6850_receive(acia, acia->rx_shift_reg);
  if (!ok) {
    /* Overflow condition. Data wasn't read in time. Overflow condition will be
     * raised once the existing data is read.
     */
    acia->rx_sr_overflow = 1;
  } else {
    if (acia->rx_sr_parity_error) {
      acia->status_reg |= k_serial_acia_status_PE;
    }
    if (acia->rx_sr_framing_error) {
      acia->status_reg |= k_serial_acia_status_FE;
    }
  }
  mc6850_clear_receive_state(acia);

  mc6850_update_irq_and_status_read(acia);
}


static int mc6850_receive_bit (ACIA *acia, uint8_t bit) {

  int e;
  
  e = TAPE_E_OK;

  /*bit = (('0'==code) ? 0 : 1);*/

  switch (acia->state) {
  case k_mc6850_state_need_start:
    if (bit == 0) {
      if (acia->rx_shift_reg != 0) {
        log_warn("acia: BUG: start bit: rx_shift_reg != 0 (%x)", acia->rx_shift_reg);
        e = TAPE_E_BUG;
      } else if (acia->rx_shift_reg_count != 0) {
        log_warn("acia: BUG: start bit: rx_shift_reg_count != 0 (%u)", acia->rx_shift_reg_count);
        e = TAPE_E_BUG;
      } else if (acia->parity_accumulator != 0) {
        log_warn("acia: BUG: start bit: parity_accumulator != 0 (%d)", acia->parity_accumulator);
        e = TAPE_E_BUG;
      } else if (acia->rx_sr_parity_error != 0) {
        log_warn("acia: BUG: start bit: rx_sr_parity_error != 0 (%d)", acia->rx_sr_parity_error);
        e = TAPE_E_BUG;
      } else if (acia->rx_sr_framing_error != 0) {
        log_warn("acia: BUG: start bit: rx_sr_framing_error != 0 (%d)", acia->rx_sr_framing_error);
        e = TAPE_E_BUG;
      }
      acia->state = k_mc6850_state_need_data;
    }
    break;
  case k_mc6850_state_need_data:
    if (bit) {
      acia->rx_shift_reg |= (1 << acia->rx_shift_reg_count);
      acia->parity_accumulator = ! acia->parity_accumulator;
    }
    acia->rx_shift_reg_count++;
    if (acia->control_reg & k_serial_acia_control_8_bits) {
      if (acia->rx_shift_reg_count == 8) {
        if (acia->control_reg & 0x08) {
          acia->state = k_mc6850_state_need_parity;
        } else {
          acia->state = k_mc6850_state_need_stop;
        }
      }
    } else {
      if (acia->rx_shift_reg_count == 7) {
        acia->state = k_mc6850_state_need_parity;
      }
    }
    break;
  case k_mc6850_state_need_parity:
    if (bit) {
      acia->parity_accumulator = ! acia->parity_accumulator;
    }
    if (acia->parity_accumulator != ((acia->control_reg & 0x04) >> 2)) {
/*      log_do_log(k_log_serial, k_log_warning, "incorrect parity bit"); */
      acia->rx_sr_parity_error = 1;
    }
    acia->state = k_mc6850_state_need_stop;
    break;
  case k_mc6850_state_need_stop:
    if (bit != 1) {
/*      log_do_log(k_log_serial, k_log_warning, "incorrect stop bit"); */
      acia->rx_sr_framing_error = 1;
    }
    mc6850_transfer_sr_to_receive(acia);
    acia->state = k_mc6850_state_need_start;
    break;
  default:
    /*assert(0);*/
    log_warn("acia: BUG: rx bad state (%d)", acia->state);
    e = TAPE_E_BUG;
    break;
  }
  
  return e;
  
}

static void
mc6850_reset(ACIA *acia) {

  int is_CTS = !!(acia->status_reg & k_serial_acia_status_CTS);

  acia->rx_data_reg = 0;
  acia->tx_data_reg = 0;

  acia->state = k_mc6850_state_need_start;
  mc6850_clear_receive_state(acia);
  acia->rx_sr_overflow = 0;

  /* Set TDRE (transmit data register empty). Clear everything else. */
  acia->status_reg = k_serial_acia_status_TDRE;

  acia->control_reg = 0;

  /* Reset of the ACIA cannot change external line levels. Make sure any status
   * bits they affect are kept.
   */
  /* These calls call mc6850_update_irq_and_status_read(). */
  mc6850_set_DCD(acia, acia->line_dcd);
  mc6850_set_CTS(acia, is_CTS);
  
  reset_tx_shift_register(acia);
  acia->tx_data_reg_loaded = 0;
  
}

void
mc6850_power_on_reset(ACIA *acia) {
  acia_init(acia); /* TOHv4 */
}



#include "tape.h"
static uint8_t
mc6850_read(ACIA *acia, uint8_t reg) {

  if (reg == 0) {
    /* Status register. */
    /* TOHv4: double mischief */
    if (acia->misframe_next_read) {
      acia->misframe_next_read = 0;
      acia->status_reg_for_read |= k_serial_acia_status_FE;
    }
    if (acia->gen_parity_error_next_read) {
      acia->gen_parity_error_next_read = 0;
      acia->status_reg_for_read |= k_serial_acia_status_PE;
    }
    return acia->status_reg_for_read;
  } else {
    /* Data register. */
    /* TOHv4: mischief */
    if (acia->corrupt_next_read) {
      acia->corrupt_next_read = 0;
      acia->rx_data_reg = ~(acia->rx_data_reg);
    }
    acia->status_reg &= ~k_serial_acia_status_DCD;
    acia->status_reg &= ~k_serial_acia_status_OVRN;
    if (acia->rx_sr_overflow) {
      /*assert(acia->status_reg & k_serial_acia_status_RDRF);*/
      /* MC6850: "The Overrun does not occur in the Status Register until the
       * valid character prior to Overrun has been read.
       */
      acia->rx_sr_overflow = 0;
      acia->status_reg |= k_serial_acia_status_OVRN;
      /* RDRF remains asserted. */
    } else {
      acia->status_reg &= ~k_serial_acia_status_RDRF;
    }

    mc6850_update_irq_and_status_read(acia);
    return acia->rx_data_reg;
  }
}


static void
mc6850_write(ACIA *acia, uint8_t reg, uint8_t val) {
  int e;
  
  if (reg == 0) {
    /* Control register. */
    if ((val & 0x03) == 0x03) {
      /* Master reset. */
      /* EMU NOTE: the data sheet says, "Master reset does not affect other
       * Control Register bits.", however this does not seem to be fully
       * correct. If there's an interrupt active at reset time, it is cleared
       * after reset. This suggests it could be clearing CR, including the
       * interrupt select bits. We clear CR, and this is sufficient to get the
       * Frak tape to load.
       * (After a master reset, the chip actually seems to be dormant until CR
       * is written. One specific example is that TDRE is not indicated until
       * CR is written -- a corner case we do not yet implement.)
       */
      mc6850_reset(acia);
    } else {
      acia->control_reg = val;
    }

  } else {

    if (acia->tx_data_reg_loaded) {
      log_warn("acia: warning: data reg write: already loaded! (old %x, "
               "new %x, pc %x, statusreg %x, _for_read %x)",
               acia->tx_data_reg, val, pc,acia->status_reg, acia->status_reg_for_read);
    }
    
    acia->tx_data_reg = val;
    acia->tx_data_reg_loaded = 1;
    
    /* oof. */
    acia->status_reg &= ~k_serial_acia_status_TDRE;
    
#ifdef BUILD_TAPE_BUGGY_LUMPY_TDRE_DISTRIBUTION
    acia->txc_count = 0; /* synchronise, wreck TDRE delay distribution, introduce bug */
#endif
    
    /* TOHv4: tx_hook() is attached to the act
       of writing to the tx data register (rather than
       the data register being copied to the shift register,
       or any bits being shifted out of the shift register).
       This enables polling-free operation for simple ACIA
       use cases, like Music 2000, with acia->tx_no_polling set. */
       
    if (acia->tx_hook != NULL) {
      e = acia->tx_hook(acia, val); /* consume byte */
      if (0 != e) {
        log_warn("acia: warning: acia[%s]->tx_hook() returns code %d",
                 acia->name, e);
      }
    }
    
    if (acia->tx_no_polling) {
      /* TOHv4: Poll-free implementation for Music 2000.
         Acknowledge TDRE immediately; consume bytes
         immediately. Shift register is unused.
         Transmission is conducted via the tx_hook
         mechanism above. */
      acia->tx_data_reg_loaded = 0;
      acia->tx_shift_reg_loaded = 0;
      acia->tx_shift_reg_shift = 0;
      acia->status_reg |= k_serial_acia_status_TDRE;
    }
    
  }

  mc6850_update_irq_and_status_read(acia);
  
}

/* END BEEBJIT CODE */
