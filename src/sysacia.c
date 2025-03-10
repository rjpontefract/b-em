/*B-em v2.2 by Tom Walker
  6850 ACIA emulation*/

#include <stdio.h>
#include "b-em.h"
#include "6502.h"
#include "acia.h"
#include "serial.h"
#include "tape.h"

FILE *sysacia_fp = NULL;

static int sysacia_reset(ACIA *acia);

/* TOHv3.3 */
static int sysacia_reset(ACIA *acia) {
  tape_state_t *ts;
  int e;
  ts = (tape_state_t *) acia->udata;
  e = tape_handle_acia_master_reset(ts);
  /*
  if (sysacia_fp != NULL) {*/ /* serial-to-file is active? */
      /*acia_ctsoff(acia);
  }
  */
  return e;
}

static int sysacia_tx_hook(ACIA *acia, uint8_t data)
{
    
    int e;
    tape_state_t *ts;
    
    ts = (tape_state_t *) acia->udata;
    
    /* This callback is only used for RS423 with a TX sink.
       Tape doesn't use it; it uses a full-fat shift-register
       ACIA implementation instead, so return now if no sink or
       if tape mode. */
    if ( ( ! ( ts->ula_ctrl_reg & 0x40 ) ) || ! ts->ula_have_serial_sink ) {
        return 0;
    }

    e = 0;
    
    if (NULL != sysacia_fp) {
        e = fputc(data, sysacia_fp);
    }
    
    /* TOHv4: hack: consume byte immediately; avoid polling for this
       serial-to-file RS423 case. */
    acia_hack_consume_tx_byte_immediately(acia);

    return (e == EOF) ? -1 : 0;

}

static int sysacia_tx_end(ACIA *acia)
{
    if (NULL != sysacia_fp) {
         fflush(sysacia_fp);
    }
    return TAPE_E_OK; /* TOHv3.3: callbacks return error code */
}

 ACIA sysacia = {
    .set_params    = NULL, //sysvia_set_params,
/*    .rx_hook    = tape_receive,*/
    .tx_hook       = sysacia_tx_hook,
    .tx_end        = sysacia_tx_end,
    .reset_hook    = sysacia_reset, /* TOHv3.3 */
    .udata         = &tape_state,
    .name          = "sysacia",     /* TOHv4 */
    .tx_no_polling = 0              /* TOHv4 */
 };

void sysacia_rec_stop(void)
{
    if (sysacia_fp) {
        fclose(sysacia_fp);
        sysacia_fp = NULL;
/*        acia_ctson(&sysacia);*/
        tape_state.ula_have_serial_sink = 0; /* TOHv4: hack */
        serial_push_dcd_cts_lines_to_acia(&sysacia, &tape_state);
    }
}



FILE *sysacia_rec_start(/*ACIA *acia, tape_state_t *ts,*/ const char *filename)
{
/*printf("sysacia_rec_start: %s\n", filename);*/
    FILE *fp = fopen(filename, "wb");
    if (fp) {
        sysacia_fp = fp;
/*        acia_ctsoff(&sysacia);*/
        tape_state.ula_have_serial_sink = 1; /* TOHv4: hack */
        serial_push_dcd_cts_lines_to_acia(&sysacia, &tape_state);
    }
    else
        log_error("unable to open %s for writing: %s", filename, strerror(errno));
    return fp;
}

