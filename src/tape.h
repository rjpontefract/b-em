/*
 *  B-Em
 *  This file (C) 2025 'Diminished'
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __INC_TAPE_H
#define __INC_TAPE_H

#include <allegro5/allegro_native_dialog.h>

#include "tape2.h"

#include "b-em.h"
#include "tibet.h"
#include "csw.h"
#include "uef.h"
#include "tapectrl.h"
#include "tape-io.h"

/* TOHv3: switch to bitfield for filetypes */
#define TAPE_FILETYPE_BITS_NONE    0
#define TAPE_FILETYPE_BITS_UEF     1
#define TAPE_FILETYPE_BITS_TIBET   2
#define TAPE_FILETYPE_BITS_CSW     4
#define TAPE_FILETYPE_BITS_WAV     8 /* TOHv4.2: add WAV */
#define TAPE_FILETYPES_ALL         (1|2|4|8)

/* for -tapetest CLI switch */
#define TAPE_TEST_QUIT_ON_EOF     1
#define TAPE_TEST_QUIT_ON_ERR     2
#define TAPE_TEST_CAT_ON_STARTUP  4
#define TAPE_TEST_CAT_ON_SHUTDOWN 8  /* TOHv4   */
#define TAPE_TEST_SLOW_STARTUP    16 /* TOHv4.1 */
#define TAPE_TEST_BAD_MASK        0xffffffc0

#ifdef _WIN32
#define TAPE_SNPRINTF(BUF,BUFSIZE,MAXCHARS,FMTSTG,...)  _snprintf_s((BUF),(BUFSIZE),(MAXCHARS),(FMTSTG),__VA_ARGS__)
#define TAPE_SCPRINTF(FMTSTG,...)                       _scprintf((FMTSTG),__VA_ARGS__)
#define TAPE_SSCANF                                     sscanf_s
#define TAPE_STRNCPY(DEST,BUFSIZE,SRC,LEN)              _mbsncpy_s((DEST),(BUFSIZE),(SRC),(LEN))
#else
#define TAPE_SNPRINTF(BUF,BUFSIZE,MAXCHARS,FMTSTG...)   snprintf((BUF),(BUFSIZE),FMTSTG)
#define TAPE_SCPRINTF(FMTSTG...)                        snprintf(&tape_dummy_for_unix_scprintf, 0, FMTSTG)
#define TAPE_SSCANF                                     sscanf
#define TAPE_STRNCPY(DEST,BUFSIZE,SRC,LEN)              strncpy((DEST),(SRC),(LEN))
#endif /* end ndef _WIN32 */

/* tape is loaded if only *one* of TIBET, CSW or UEF is selected.
   All three means a tape-from-scratch; none at all means no tape at all. */
/* TOHv4: now a macro
 * TOHv4.3: still used in 6502.c! */
#define TAPE_IS_LOADED(filetype_bits) \
  ((TAPE_FILETYPE_BITS_NONE != (filetype_bits)) && (TAPE_FILETYPES_ALL != (filetype_bits)))

#define TAPE_1200TH_IN_1MHZ_INT (64 * 13) /* the famous 832us */
#define TAPE_1200TH_IN_S_FLT    (((double) TAPE_1200TH_IN_1MHZ_INT) / 1000000.0)
#define TAPE_1200TH_IN_NS_INT   (1000 * TAPE_1200TH_IN_1MHZ_INT)
#define TAPE_1200TH_IN_2MHZ_INT (2 * TAPE_1200TH_IN_1MHZ_INT)

#define TAPE_1200_HZ     (1.0 / TAPE_1200TH_IN_S_FLT) /* 1201.9 Hz */
#define TAPE_FLOAT_ZERO  0.00001


typedef struct tape_state_s {

    /* reminder! anything on this struct which is a pointer to heap memory
     * needs to be deep-copied in tape_state_clone_and_rewind(). */

    /* STATE VARIABLES */

    /* TOHv3: filetype is now a bitfield */
    uint8_t filetype_bits;

    /* format-specific state: */
    tibet_t tibet;
    uef_state_t uef;
    csw_state_t csw;

    /* TIBET data spans are built here,
       before being sent to the TIBET interface
       to be included in the spans list: */
    tibet_span_t tibet_data_pending;

    // vars for local byte decoding:
    uint32_t num_1200ths_since_silence;
    uint32_t start_bit_wait_count_1200ths;
    uint8_t bits_count;

    uint8_t tapenoise_no_emsgs;     /* prevent "ring buffer full" spam */
    uint8_t tape_finished_no_emsgs; /* TOHv2: prevent "tape finished" spam */

    /* WRITING (w_...) : */
    uint8_t w_must_end_data; /* data pending, e.g. on w_uef_tmpchunk */

    /* used to accumulate bit periods, until we have some 1200ths to send;
       bit periods will be converted into tones (1200ths) */
    int64_t w_accumulate_bit_periods_ns;

    /* meanwhile this is used to accumulate all the silence and leader
       in the current span (note that w_accumulate_bit_periods_ns
       is included in these values) */
    int64_t w_accumulate_silence_ns;
    int64_t w_accumulate_leader_ns;

    uef_chunk_t w_uef_tmpchunk;
    uint8_t w_uef_origin_written;  /* TOHv3.2 */

    /* we scan backwards for &117s and update this value according
     * to the first chunk &117 we find, whenever
     *   i)  a new UEF is loaded; or
     *   ii) Record Mode is activated.
     * This allows the save code to know whether it needs to
     * insert &117 to change the prevailing baud or not.
     */
    int32_t w_uef_prevailing_baud; /* TOHv4 */

    /* for decoding the ACIA's serial stream, to convert back into bytes,
       for writing UEF. ALL UEF data gets passed through this mechanism! */
    uint8_t w_uef_serial_frame;
    uint8_t w_uef_serial_phase;

    uint16_t w_uef_enc100_shift_value;
    uint8_t w_uef_enc100_shift_amount; /* max 16 */

    /* needed so we know whether we're appending UEF (and therefore
       whether we need to grey out "don't emit origin" in the UEF
       save options submenu) */
    uint8_t w_uef_was_loaded; /* TOHv3.2 */

    bool ula_motor;       /* TOHv3.3: moved here from tape_vars_t above */
    int32_t ula_rx_ns;    /* TOHv4 */
    int32_t ula_tx_ns;    /* TOHv4 */
    uint8_t ula_ctrl_reg; /* TOHv4 (moved from serial.c) */

    /* whether or not this value makes it to the ACIA
       depends* on whether we're in tape or RS423 mode: */
    bool ula_dcd_tape;              /* TOHv3.3 */

    int32_t ula_dcd_2mhz_counter;            /* TOHv4 */
    int32_t ula_dcd_blipticks;               /* TOHv4 */
    int32_t ula_rs423_taperoll_2mhz_counter; /* TOHv4 */

    int32_t tape_noise_counter;     /* TOHv4 */

#define TAPE_TONES300_BUF_LEN 4

    /* for decoding 300 baud from its component 1200ths */
    char tones300[TAPE_TONES300_BUF_LEN];
    int8_t tones300_fill;

    /* In the interests of efficiency, the DCD part of the
       tape interface is clocked at a different rate to the
       RX part. This means that we cannot necessarily feed
       a "fresh" bit from the tape into the DCD state machine.
       So, we have to persist the most recently obtained bit
       value, and supply that to the DCD logic. */
    char ula_prevailing_rx_bit_value;

    /* TOHv4: pre-computed when control register is written;
       see serial_recompute_dividers_and_thresholds() */
    int32_t ula_rx_thresh_ns;
    int32_t ula_tx_thresh_ns;
    int32_t ula_rx_divider;
    int32_t ula_tx_divider;

    /* TOHv4: hack for serial-to-file mode.
       The correct architecture here would be a list
       of pointers to functions that should be called
       one after the other to sink a byte of TX serial data.
       For now we just have this flag */
    uint8_t ula_have_serial_sink;

    /* TOHv4.2:
       Don't engage "strip mode" until 0.6s of tape
       has played since the last MOTOR ON. MOS can miss
       the start of a block under common conditions
       if overclock + strip are both enabled. This should
       mitigate that. */
    uint32_t strip_silence_and_leader_holdoff_1200ths;
    uint32_t leader_skip_1200ths;

    /* not sure if this should be on tape_vars_t rather than tape_state_t,
     * but tape_state_t already has an internal stream pointer, so another
     * time value shouldn't do any additional harm.
     * This is also used for recording */

    /* FIXME: rename to tallied_1200ths */
    int32_t tallied_1200ths; /* TOHv4.3 */

    /* tally count of subsequent '1' tones in this variable,
     * crude RS423 leader detect */
    int rs423_leader_detect;  /* TOHv4.3 */

    /* TOHv4.3: hack, 19.2 kbps read */
    char hack_19200ths_last_value;
    int hack_19200ths_consumed;

    /* TOHv4.3: allow communicating failure code to tapectrl
     *          if tapectrl window is opened after the error */
    int prior_exception;

} tape_state_t;


#include "tape-io.h"

/* TOHv3.2: introduced this struct
*
* Gets rid of the messy tape global variables.
*
* You should be able to swap to a different tape without
* needing to touch any of the data on this struct. If you
* are adding a variable that will need altering or clearing
* when the loaded tape is changed or ejected, then it
* belongs on tape_state_t rather than tape_vars_t. This is
* for configuration stuff.
*/
typedef struct tape_vars_s {

    /* SETTINGS VARIABLES */

    /* TOHv4: mechanism for -expiry option: */
    bool expired_quit;
    int64_t testing_expire_ticks;  /* measured in "otherstuff" ticks */

    bool overclock;                /* a.k.a. fasttape */

    bool strip_silence_and_leader; /* TOHv3.2: another speed hack */

    bool record_activated;         /* TOHv3 */

    ALLEGRO_PATH *save_filename;   /* TOHv3 */
    ALLEGRO_PATH *load_filename;

    /* TOHv3: TAPE_TEST_xxx bitfields: */
    uint8_t testing_mode;

    /* TOHv3.2 */
    bool save_always_117;
    bool save_prefer_112;

    /* TOHv3.2, for knowing when to emit &117 */
    bool cur_baud300;

    tape_shutdown_save_t quitsave_config;       /* TOHv3 */

    bool save_do_not_generate_origin_on_append; /* TOHv3.2 */

    bool permit_phantoms;
    bool disable_debug_memview;

    bool wav_use_phase_shift; /* TOHv4.2 */

    /* all new in TOHv4.3 */
#ifdef BUILD_TAPE_TAPECTRL
    tape_ctrl_window_t tapectrl;
    bool tapectrl_allow_resize;
    float tapectrl_ui_scale;
    /* allows mainthread to skip locking mutex to check to see whether display exists or not */
    bool tapectrl_opened;
    uint32_t since_last_tone_sent_to_gui; /* reduce msg rate to GUI */
    uint32_t since_last_eof_sent_to_gui;  /* same */
    /* suppress seek desync error message spam */
    bool desync_message_printed;
    /* used to detect change in EOF status, so msg can be sent to tapectrl GUI */
    bool previous_eof_value;
    /* support seeker bar striping feature */
    tape_interval_list_t interval_list;
#endif

} tape_vars_t;

extern int tapeledcount;
extern char tape_dummy_for_unix_scprintf;

/* saveable state lives here: */
extern tape_state_t tape_state;

/* other tape variables go here (e.g. config settings): */
extern tape_vars_t tape_vars;

uint32_t tape_read_u32 (uint8_t *in);
uint16_t tape_read_u16 (const uint8_t * const in) ;
uint32_t tape_read_u24 (uint8_t *in);
void tape_write_u32 (uint8_t b[4], uint32_t v); /* TOHv3 */
void tape_write_u16 (uint8_t b[2], uint16_t v); /* TOHv3 */

#include "acia.h"

int findfilenames_new (tape_state_t * const ts,
                       bool const show_ui_window,
                       bool const filter_phantoms);

void tape_state_init (tape_state_t *t,
                      tape_vars_t *tv,
                      uint8_t filetype_bits,
                      bool alter_menus);
                      
void tape_state_finish (tape_state_t *t, bool alter_menus);

int tape_state_clone_and_rewind (tape_state_t *out, tape_state_t *in);

int tape_handle_acia_master_reset (tape_state_t *t);

void tape_init (tape_state_t *t, tape_vars_t *tv);

int tape_stop_motor (tape_state_t * const ts,
                     tape_ctrl_window_t * const tcw,
                     /*bool const silence112,*/
                     bool const record_activated,
                     bool const tapectrl_opened); /* TOHv4.3 */

int tape_start_motor (tape_state_t * const ts,
                      tape_ctrl_window_t * const tcw,
                      bool const also_force_dcd_high,
                      bool const tapectrl_opened); /* TOHv4.3 */

int tape_set_record_activated (tape_state_t * const t,
                               tape_vars_t  * const tv,
                               ACIA         * const acia_or_null,
                               bool           const value,
                               bool           const tapectrl_opened);

bool tape_is_record_activated (const tape_vars_t * const tv);

/* exported in TOHv4 */
void tape_handle_exception (tape_state_t * const ts,
                            tape_vars_t  * const tv, /* TOHv3.2: may be NULL */
                            int  const error_code,
                            bool const eof_fatal,
                            bool const err_fatal,
                            bool const not_tapecat_mode);

int tape_state_init_alter_menus (tape_state_t * const ts, /* not const, because of tibet priv horribleness */
                                 tape_vars_t  * const tv);

void tape_ejected_by_user(tape_state_t * const ts, tape_vars_t * const tv, ACIA * const acia) ;

void to_hours_minutes_seconds (int32_t   const elapsed_1200ths,
                               int32_t * const h_out,
                               int32_t * const m_out,
                               int32_t * const s_out);

int tape_load_successful (tape_state_t * const ts,
                          tape_vars_t  * const tv,
                          const char * const path) ;

int tape_rs423_eat_1200th (tape_state_t * const ts,
                           tape_vars_t  * const tv,
                           ACIA         * const acia,
                           bool           const record_activated, /* TOHv4.3 */
                           bool           const emit_tapenoise,
                           bool         * const throw_eof_out);

#endif /* __INC_TAPE_H */
