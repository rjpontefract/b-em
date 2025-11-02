#ifndef __INC_SERIAL_H
#define __INC_SERIAL_H

/* TOHv4 */
extern const int16_t serial_divider_by_bits[8];

void    serial_write(uint16_t addr, uint8_t val);
uint8_t serial_read (uint16_t addr);
void    serial_reset(void);

void serial_loadstate(FILE *f);

#include "tape2.h" /* for BUILD_TAPE_INCOMPATIBLE_NEW_SAVE_STATES */

#ifdef BUILD_TAPE_INCOMPATIBLE_NEW_SAVE_STATES
/* TOHv4, for BEMSNAP4 */
/* would like these to return error code, but
   the save_sect() pointer-to-function isn't
   currently compatible with that :( */
void serial_savestate_BEMSNAP4(FILE *f);
void serial_loadstate_BEMSNAP4(FILE *f);
#else
void serial_savestate(FILE *f);
#endif

/* fwdrefs */
typedef struct tape_state_s tape_state_t;
typedef struct acia ACIA; /* fwdref */

#define TAPE_DCD_BLIP_TICKS_2MHZ 423

/* from beebjit: */
void serial_push_dcd_cts_lines_to_acia (ACIA *acia, tape_state_t *ts);

void serial_poll_dcd_blipticks (char      const bit_value_or_nil,         /* '\0's are just ignored */
                                bool      const fast_dcd,                 /* used for both turbo read modes */
                                int32_t * const dcd_bliptick_count_inout, /* blip-periods counter */
                                bool    * const dcd_line_inout);          /* DCD line to be manipulated */

/* TOHv4 */
void serial_recompute_dividers_and_thresholds (bool overclock_tape, /* OCing now in ULA not ACIA */
                                               uint8_t ctrl_reg_value,
                                               int32_t *rx_thresh_ns_out,
                                               int32_t *tx_thresh_ns_out,
                                               int32_t *rx_divider_out,
                                               int32_t *tx_divider_out);

int serial_rxc_clock_for_tape (tape_state_t * const ts,
                               tape_vars_t  * const tv,
                               ACIA         * const acia,
                               bool           const emit_tapenoise,
                               bool           const record_activated,
                               bool         * const throw_eof_out);

int serial_2txc_clock_for_tape (tape_state_t *ts,
                                tape_vars_t *tv,
                                ACIA *acia,
                                uint8_t emit_tapenoise);

void serial_handle_dcd_tick (tape_state_t *ts,
                             tape_vars_t *tv,
                             ACIA *acia);

#endif
