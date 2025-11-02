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

#include "tapewrite.h"
#include "acia.h"
#include "gui-allegro.h"

#define SERIAL_STATE_AWAITING_START 0
#define SERIAL_STATE_BITS           1
#define SERIAL_STATE_PARITY         2
#define SERIAL_STATE_AWAITING_STOP  3

static int check_pending_tibet_span_type (tape_state_t *t, uint8_t type);

static void framing_to_string (char fs[4], const serial_framing_t * const f);

static int tape_write_end_data (tape_state_t *t);

static int tape_write_start_data (tape_state_t * const t,
                                  bool const always_117,
                                  const serial_framing_t * const f);

static int tape_write_silence_2 (tape_state_t * const t,
                                 double len_s,
                                 bool const silence112,
                                 int32_t const silence_start_elapsed_1200ths);

static int tape_write_data_1200th  (tape_state_t * const ts,
                                    tape_ctrl_window_t * const tcw,
                                    tape_interval_list_t * const iv_list_inout,
                                    const serial_framing_t * const f,
                                    bool const tapenoise_active,
                                    char const value,
                                    uint32_t * const since_last_tone_sent_to_gui_inout,
                                    bool const  tapectrl_opened);

static int tape_write_leader (tape_state_t * const t, uint32_t num_1200ths, bool const populate_elapsed);

static int
wp_bitclk_start_data_if_needed (tape_state_t * const ts,
                                /* need to provide baud rate + framing,
                                 f or TIBET hints, UEF &114 header, etc: */
                                const serial_framing_t * const f,
                                bool const always_117,
                                bool const record_is_pressed);

static int wp_bitclk_accumulate_leader (tape_state_t         * const ts,
                                        tape_ctrl_window_t   * const tcw,
                                        tape_interval_list_t * const iv_list_inout,
                                        bool                   const tapenoise_active,
                                        int64_t                const ns_per_bit,
                                        bool                   const record_is_pressed,
                                        bool                 * const reset_shift_reg_out,
                                        uint32_t             * const since_last_tone_sent_to_gui_inout,
                                        bool                   const tapectrl_opened);

static int wp_end_data_section_if_ongoing (tape_state_t * const t,
                                           bool const record_is_pressed,
                                           bool * const reset_shift_register_out);

static int tape_write_prelude (tape_state_t * const ts,
                               bool const record_is_pressed,
                               bool const no_origin_chunk_on_append);

static int wp_flush_accumulated_leader_maybe (tape_state_t * const t,
                                              bool const record_is_pressed,
                                              bool const populate_elapsed);

static int wp_bitclk_output_data (tape_state_t *ts,
                                  tape_ctrl_window_t *tcw, /* TOHv4.3 */
                                  tape_interval_list_t * const iv_list_inout,
                                  ACIA *acia,
                                  const serial_framing_t * const f,
                                  uint32_t *since_last_tone_sent_to_gui_inout, /* limit to_gui message rate */
                                  uint8_t tapenoise_active,
                                  int64_t ns_per_bit,
                                  uint8_t record_is_pressed,
                                  uint8_t always_117,
                                  bool const tapectrl_opened);

static int wp_bitclk_handle_silence (tape_state_t         * const ts,
                                     tape_ctrl_window_t   * const tcw,     /* TOHv4.3 */
                                     bool                   const silent,
                                     bool                   const tapenoise_active,
                                     bool                   const record_is_pressed,
                                     int64_t                const ns_per_bit,
                                     bool                   const silence112,
                                     bool                   const tapectrl_opened,
                                     tape_interval_list_t * const iv_list_inout,
                                     bool                 * const reset_shift_register_out,
                                     uint32_t             * const since_last_tone_sent_to_gui_inout);

//

int tape_flush_pending_piece (tape_state_t * const ts,
                              ACIA         * const acia_or_null,
                              bool           const silence112,
                              bool           const populate_elapsed) {

    bool reset_shift_reg;
    int e;
    int32_t duration;

    e = TAPE_E_OK;

    /* if (ts->disabled_due_to_error) { return TAPE_E_OK; } */ /* TOHv4.3-a3 */
    if (TAPE_E_OK != ts->prior_exception) { return TAPE_E_OK; } /* TOHv4.3-a3 */

    e = tape_get_duration_1200ths (ts, &duration);
    if (TAPE_E_OK != e) { return e; }

/* printf("tape_flush_pending_piece: leader_ns = %"PRId64", silence_ns = %"PRId64"\n",
          ts->w_accumulate_leader_ns, ts->w_accumulate_silence_ns);
*/

    if (ts->w_accumulate_leader_ns > 0) {
        e = wp_flush_accumulated_leader_maybe (ts, true, populate_elapsed);
        ts->w_accumulate_leader_ns = 0;
    } else if (ts->w_accumulate_silence_ns > TAPE_1200TH_IN_NS_INT) {
        e = tape_write_silence_2 (ts,
                                  ((double) ts->w_accumulate_silence_ns) / 1000000000.0,
                                  silence112,
                                  duration); /* TOHv4.3 */
        ts->w_accumulate_silence_ns = 0;
        /* TOHv4.3: bugfix? kludge? force EOF
         * (REC is/was pressed, so forcing EOF is correct behaviour here?) */
        ts->uef.cur_chunk = ts->uef.num_chunks;
    } else {
        reset_shift_reg = 0;
        e = wp_end_data_section_if_ongoing (ts, true, &reset_shift_reg);
        if (acia_or_null != NULL) {
            acia_hack_tx_reset_shift_register(acia_or_null);
        } else {
         /*   log_warn("tape: WARNING: ignoring reset_shift_reg=1 from wp_end_data_section_if_ongoing");*/
        }
    }

    if (ts->filetype_bits & TAPE_FILETYPE_BITS_UEF) {
        uef_clear_chunk(&(ts->w_uef_tmpchunk));
    }

    return e;

}

#include "taperead.h"

/* TOHv4: Want to be called at the bit rate.
   For each tick, either have leader, silence, or data.
   Leader and silence packets will have to be expanded out into
   the appropriate number of 1200ths. */

/* NOTE: motor is guaranteed to be running */
int tape_write_bitclk (        tape_state_t * const ts,
                         tape_ctrl_window_t * const tcw,           /* TOHv4.3 */
                       tape_interval_list_t * const iv_list_inout, /* TOHv4.3 */
                                       ACIA * const acia,
                                       char   const bit,
                                    int64_t   const ns_per_bit,
                                       bool   const tapenoise_write_enabled,
                                       bool   const record_is_pressed,
                                       bool   const always_117,
                                       bool   const silence112,
                                       bool   const no_origin_chunk_on_append,
                                       bool   const tapectrl_opened,           /* TOHv4.3 */
                                   uint32_t * const since_last_tone_sent_to_gui_inout) {

    bool have_leader;
    int e;
    serial_framing_t f;
    bool reset_shift_reg;
    bool had_data = 0;
    bool silent;

    if (0 == ns_per_bit) {
        log_warn("tape: BUG: ns_per_bit is zero!");
        return TAPE_E_BUG;
    }

    had_data = tape_peek_for_data(ts);
    silent = ('S' == bit);

    /* do some housekeeping: */
    e = tape_write_prelude (ts, record_is_pressed, no_origin_chunk_on_append);
    if (TAPE_E_OK != e) { return e; }

    /*
       handle silence:
       - if silent:
         - flush data section, if this silence interrupts one;
         - generate silent tapenoise (potentially multiple 1200ths)
       - or, if not silent:
         - write silent section if one has just ended
    */
    reset_shift_reg = 0;
    e = wp_bitclk_handle_silence (ts,
                                  tcw, /* TOHv4.3 */
                                  silent,
                                  tapenoise_write_enabled,
                                  record_is_pressed,
                                  ns_per_bit,
                                  silence112,
                                  tapectrl_opened, /* TOHv4.3 */
                                  iv_list_inout,
                                  &reset_shift_reg,
                                  since_last_tone_sent_to_gui_inout);
    if (TAPE_E_OK != e)  { return e; }

    /* Hack?? reset tx shiftreg if a partial frame needs flushing to the UEF.
       Shouldn't be necessary?? */
    if (reset_shift_reg) { acia_hack_tx_reset_shift_register(acia); }

    /* handle leader:
       - write data section if leader interrupts one;
       - or, write leader section if one has just ended;
       - if leader and tapenoise active, then send a leader tapenoise 1200th */
    have_leader = ! acia->tx_shift_reg_loaded;

    if ( have_leader && ! silent ) {
        /* also sends potentially multiple 1200ths of tapenoise */
        e = wp_bitclk_accumulate_leader (ts,
                                         tcw, /* TOHv4.3 */
                                         iv_list_inout,
                                         record_is_pressed && tapenoise_write_enabled,
                                         ns_per_bit,
                                         record_is_pressed,
                                         &reset_shift_reg,
                                         since_last_tone_sent_to_gui_inout,
                                         tapectrl_opened);
        if (TAPE_E_OK != e)  { return e; }
    }

    /* Hack?? reset tx shiftreg if a partial frame needs flushing to the UEF.
       Shouldn't be necessary?? */
    if (reset_shift_reg) { acia_hack_tx_reset_shift_register(acia); }

    if ( silent || ! have_leader ) {
        /* end of leader section? */
        e = wp_flush_accumulated_leader_maybe (ts, record_is_pressed, true);
        if (TAPE_E_OK != e) { return e; }
    }

    /* Silence and leader 1200ths & tapenoise are done;
       data still remains: */
    if ( ( ! silent ) && ( ! have_leader ) ) {

        acia_get_framing (acia->control_reg, &f);
        f.nominal_baud = (int32_t) (0x7fffffff & ((TAPE_1200TH_IN_NS_INT * 1200) / ns_per_bit));

        e = wp_bitclk_output_data (ts,
                                   tcw,
                                   iv_list_inout,
                                   acia,
                                   &f,
                                   since_last_tone_sent_to_gui_inout, /* limit to_gui message rate */
                                   record_is_pressed && tapenoise_write_enabled,
                                   ns_per_bit,
                                   record_is_pressed,
                                   always_117,
                                   tapectrl_opened);
        if (TAPE_E_OK != e) { return e; }

    }

    /* need to maybe un-grey Catalogue Tape in the Tape GUI menu? */
    if (tape_peek_for_data(ts) != had_data) { /* suddenly we have data, as it's just been written */
        gui_alter_tape_menus_2();
    }


    return e;

}

/* TOHv3.2 */
/* This situation (an incomplete TX frame) is quite easy to provoke
 * if you issue fast CRs to the "RECORD then RETURN" prompt. The
 * dummy byte will be interrupted and this function will be called
 * to clean up the mess.
 *
 * Another way would be to start writing some data blocks
 * and then spam the REC button. */

int tape_uef_flush_incomplete_frame (uef_state_t * const uef_inout,
                                         int32_t   const tallied_1200ths,
                                         uint8_t * const serial_phase_inout,
                                         uint8_t * const serial_frame_inout,
                                     uef_chunk_t * const chunk_inout) { /* in practice this is ts->w_uef_tmpchunk */

    uint8_t i;
    int e;

    /*
    printf ("tape_uef_flush_incomplete_frame (phase=%u, frame=0x%02x)\n",
            *serial_phase_inout, *serial_frame_inout);
    */

    if ((*serial_phase_inout >= 2) && (*serial_phase_inout <= 8)) {
        for (i=0; i < (9 - *serial_phase_inout); i++) {
            *serial_frame_inout >>= 1;
        }
    }

    /* Bafflingly, the needed value here is 16 to make the
     * duration per the stripes equal to the duration per pieces
     * (UEF chunks in this case). I don't know why it's 16.
     * This code never runs for 300 baud saving. */

    /* chunk_inout->elapsed.pos_1200ths += 16; */

    e = uef_append_byte_to_chunk (chunk_inout, *serial_frame_inout);
    if (TAPE_E_OK == e) {
        chunk_inout->elapsed.pos_1200ths = tallied_1200ths - chunk_inout->elapsed.start_1200ths;
        e = uef_store_chunk(uef_inout,
                            chunk_inout->data,
                            chunk_inout->type,
                            chunk_inout->len,
                            0, /* TOHv4.2: dummy offset */
                            0,
                            0,
                            0, /* although this is a data chunk, we never write UEF &114, so this field is always 0 */
                            &(chunk_inout->elapsed));
    }

    uef_clear_chunk(chunk_inout);

    *serial_phase_inout = 0;
    *serial_frame_inout = 0;

    return e;

}

static int wp_flush_accumulated_leader_maybe (tape_state_t * const ts,
                                              bool const record_is_pressed,
                                              bool const populate_elapsed) {
    int e;
    /* handle the end of a leader section */
    if ( ts->w_accumulate_leader_ns > 0 ) {
        if (record_is_pressed) {
/* printf("flushing %"PRId64" ms accumulated leader\n", ts->w_accumulate_leader_ns / 1000000); */
            e = tape_write_leader (ts, ts->w_accumulate_leader_ns / TAPE_1200TH_IN_NS_INT, populate_elapsed);
            if (TAPE_E_OK != e) { return e; }
        }
        ts->w_accumulate_leader_ns = 0;
    }
    return TAPE_E_OK;
}

static int
wp_bitclk_start_data_if_needed (tape_state_t * const ts,
                                /* need to provide baud rate + framing,
                                   f or TIBET hints, UEF &114 header, etc: */
                                const serial_framing_t * const f,
                                bool const always_117,
                                bool const record_is_pressed) {

    int e;

    e = TAPE_E_OK;

#ifdef BUILD_TAPE_SANITY
    /* TOHv4.3-a4: UEF: w_must_end_data implies there is some Beeb data
     * on the tmpchunk. If the tmpchunk type is 0 (the origin chunk),
     * this is clearly not the case and something has gone wrong. */
    if (    (ts->filetype_bits & TAPE_FILETYPE_BITS_UEF)
         && (0 == ts->w_uef_tmpchunk.type)
         && ts->w_must_end_data) {
        /* shouldn't happen */
        log_warn("tapewrite: BUG: UEF tmpchunk type 0 (origin) but ts->w_must_end_data is set!");
        return TAPE_E_BUG;
    }

    if ( ! ts->w_must_end_data ) {
        /* this means that nothing should contain any data --
         * enforce this */
        if ( (ts->filetype_bits & TAPE_FILETYPE_BITS_UEF) && (ts->w_uef_tmpchunk.type != 0) ) {
            log_warn("tapewrite: BUG: w_must_end_data is 0 but w_uef_tmpchunk.type is nonzero (&%x)",
                        ts->w_uef_tmpchunk.type);
            return TAPE_E_BUG;
        }
        if (ts->filetype_bits & TAPE_FILETYPE_BITS_CSW) {
            /* nothing to do? */
        }
        if ( (ts->filetype_bits & TAPE_FILETYPE_BITS_TIBET) && (ts->tibet_data_pending.type != TIBET_SPAN_INVALID) ) {
            log_warn("tapewrite: BUG: w_must_end_data is 0 but tibet_data_pending.type is nonzero (&%x)",
                        ts->tibet_data_pending.type);
            return TAPE_E_BUG;
        }
    }
#endif

    /* Beware: record_is_pressed may be activated at any time,
       including in the middle of a block. Additionally, there
       is a further pathological case where record_is_pressed
       gets toggled multiple times during the course of a
       single block. We have to know when we're supposed to call
       tape_write_start_data() under such circumstances. */
/*printf("wp_bitclk_start_data_if_needed: w_must_end_data = %u, tmpchunk type = &%x\n", ts->w_must_end_data, ts->w_uef_tmpchunk.type);*/
    if ( record_is_pressed && ! ts->w_must_end_data ) {
        ts->w_must_end_data = 1;
        e = tape_write_start_data (ts, always_117, f);
        if (TAPE_E_OK != e) { return e; }
    }

    return e;

}



/* calls gated by ( ( ! silent ) && ( ! have_leader ) ) */
static int wp_bitclk_output_data (tape_state_t *ts,
                                  tape_ctrl_window_t *tcw, /* TOHv4.3 */
                                  tape_interval_list_t * const iv_list_inout,
                                  ACIA *acia,
                                  const serial_framing_t * const f,
                                  uint32_t *since_last_tone_sent_to_gui_inout, /* limit to_gui message rate */
                                  uint8_t tapenoise_active,
                                  int64_t ns_per_bit,
                                  uint8_t record_is_pressed,
                                  uint8_t always_117,
                                  bool const tapectrl_opened) {

    int e;
    int64_t i, num_1200ths;
    char p;
    uint8_t stop_bit_position; /* TOHv4.1 */

    e = TAPE_E_OK;

    /*
       This is another hackish consequence of the fact that emulated
       tape hardware is only really half-duplex. Scenario:

       - Record & append to tape is DISABLED (read mode)
       - Tape noise generation is ENABLED
       - a SAVE operation begins

       In this situation, rather than the SAVE operation producing
       corresponding tape noise as might be expected, the tape noise
       generator will instead PLAY BACK the loaded tape while
       the SAVE occurs (remember, no actual writing to tape takes
       place because REC mode is OFF -- in order to get tape
       noise from the SAVE operation, REC mode needs to be
       enabled). */

    /* TOHv3.3: added extra call to read_ffwd_to_end() to stop receive overflows */
    /*e = read_ffwd_to_end(ts);*/

    /* TOHv4.3-a3: cancel any EOF */
    /*if (TAPE_E_EOF == e) { e = TAPE_E_OK; }*/
    if (TAPE_E_OK != e) { return e; }

    /* setup: handle TIBET hints, UEF baud chunk &117,
       UEF chunk &114 header information: */
    e = wp_bitclk_start_data_if_needed (ts, f, always_117, record_is_pressed);
    if (TAPE_E_OK != e) { return e; }

    ts->w_accumulate_bit_periods_ns += ns_per_bit;
    num_1200ths = ts->w_accumulate_bit_periods_ns / TAPE_1200TH_IN_NS_INT;
    /* consume any whole 1200ths */
    ts->w_accumulate_bit_periods_ns -= (num_1200ths * TAPE_1200TH_IN_NS_INT);

    stop_bit_position = 1 + f->num_data_bits + ((f->parity != 'N') ? 1 : 0); /* TOHv4.1 */

    /* FIXME: I don't think this 0-case should ever happen?
       wp_bitclk_accumulate_leader() should have
       been called instead of this one? */
    if (0 == acia->tx_shift_reg_loaded) {

        log_warn("tape: BUG: wp_bitclk_output_data called with shift reg not loaded");
        return TAPE_E_BUG;

    } else if (0 == acia->tx_shift_reg_shift) { /* TOHv4.1 */
        acia->tx_odd_ones = 0;
    } else if (acia->tx_shift_reg_shift < (f->num_data_bits + 1)) { /* nom. 9 */

        /* Start bit not sent yet? Send it now. Start bit is deferred,
           in order to simulate the correct TX pipelining behaviour.
           (This won't jeopardise tapenoise; double bit will just be
           sequenced in the tapenoise ringbuffer.) */
        if (1 == acia->tx_shift_reg_shift) {
            for (i=0;
                 record_is_pressed && (TAPE_E_OK == e) && (i < num_1200ths);
                 i++) {
                e = tape_write_data_1200th (ts,
                                            tcw,
                                            iv_list_inout,
                                            f,
                                            tapenoise_active,
                                            '0',
                                            since_last_tone_sent_to_gui_inout,
                                            tapectrl_opened);
            }
        }

        /* data bit */
        for (i=0;
             record_is_pressed && (TAPE_E_OK == e) && (i < num_1200ths);
             i++) {
            e = tape_write_data_1200th (ts,
                                        tcw,
                                        iv_list_inout,
                                        f,
                                        tapenoise_active,
                                        (acia->tx_shift_reg_value & 1) ? '1' : '0',
                                        since_last_tone_sent_to_gui_inout,
                                        tapectrl_opened);
        }

        if (acia->tx_shift_reg_value & 1) {
            acia->tx_odd_ones = ! acia->tx_odd_ones;
        }

    /* TOHv4.1: parity code was completely missing. LOL */
    } else if ((f->parity != 'N') && (acia->tx_shift_reg_shift == (f->num_data_bits + 1))) {
        p = '0';
        if (    ( ('E' == f->parity) && (   acia->tx_odd_ones ) )
             || ( ('O' == f->parity) && ( ! acia->tx_odd_ones ) ) ) {
            p = '1';
        }
        /* send parity bit */
        for (i=0;
             record_is_pressed && (TAPE_E_OK == e) && (i < num_1200ths);
             i++) {
            e = tape_write_data_1200th (ts,
                                        tcw,
                                        iv_list_inout,
                                        f,
                                        tapenoise_active,
                                        p,
                                        since_last_tone_sent_to_gui_inout,
                                        tapectrl_opened);
        }
    } else if (acia->tx_shift_reg_shift >= stop_bit_position) { /* TOHv4.1: dedicated stop_bit_position */

        /* stop bit(s) */
        for (i=0;
             record_is_pressed && (TAPE_E_OK == e) && (i < num_1200ths);
             i++) {
            e = tape_write_data_1200th (ts,
                                        tcw,
                                        iv_list_inout,
                                        f,
                                        tapenoise_active,
                                        '1',
                                        since_last_tone_sent_to_gui_inout,
                                        tapectrl_opened);
        }

    }
    /* else {
        log_warn("tape: BUG: tx_shift_reg_shift insane value %u", acia->tx_shift_reg_shift);
        return TAPE_E_BUG;
    }*/

    return e;

}


static int tape_write_prelude (tape_state_t * const ts,
                               bool const record_is_pressed,
                               bool const no_origin_chunk_on_append) {

    int e;
    tape_interval_t elapsed;
    int32_t duration;

    e = TAPE_E_OK;

    if (record_is_pressed) {
        e = read_ffwd_to_end(ts);
        if (TAPE_E_OK != e) { return e; }
    }

    /* make sure that the first thing that gets written
       to a UEF stream is an origin chunk. Note that we are
       writing this directly to the UEF on the tape_state_t,
       whereas data will be written to the staging chunk,
       t->w_uef_tmpchunk (for now).
       */

    if (      ( ts->filetype_bits & TAPE_FILETYPE_BITS_UEF )
         &&     record_is_pressed
         && !   ts->w_uef_origin_written ) {

       e = uef_get_duration_1200ths(&(ts->uef), &duration);
       if (TAPE_E_OK != e) { return e; }

       if (-1 == duration) { duration = 0; } /* TOHv4.3-a4 */

        /* TOHv4.3 */
        elapsed.pos_1200ths = 0; /* use the start time from tmpchunk, but cancel any tone */
        elapsed.start_1200ths = duration;
/*printf("tape_write_prelude: UEF: origin: timestamp (%d + %d)\n", elapsed.start_1200ths, elapsed.pos_1200ths);*/

        if ( ! no_origin_chunk_on_append ) {
            e = uef_store_chunk(&(ts->uef),
                                (uint8_t *) VERSION_STR,
                                0,                      /* origin, chunk &0000     */
                                strlen(VERSION_STR)+1,  /* include the \0          */
                                0,                      /* TOHv4.2: dummy offset   */
                                0,                      /* TOHv4.3: no cycle information ... */
                                0,
                                0,
                                &elapsed);              /* TOHv4.3 */
            if (TAPE_E_OK != e) { return e; }
        }

        ts->w_uef_origin_written = 1; /* don't try again, mark it written even if disabled */

    }

    /* TOHv4.1: now gated by record_is_pressed */
    if (record_is_pressed) {
        e = tape_wp_init_blank_tape_if_needed (&(ts->filetype_bits)); /*, record_is_pressed);*/
        if (TAPE_E_OK != e) { return e; }
    }

    return e;

}

#include "tapenoise.h"
#include "tapeseek.h"

/* NOT used for silence or leader!
 * You probably wanted tape_write_bitclk().
 *
 * REC is guaranteed to be pressed ... */
static int tape_write_data_1200th  (tape_state_t * const ts,
                                    tape_ctrl_window_t * const tcw,
                                    tape_interval_list_t * const iv_list_inout,
                                    const serial_framing_t * const f,
                                    bool const tapenoise_active,
                                    char const value,
                                    uint32_t * const since_last_tone_sent_to_gui_inout,
                                    bool const tapectrl_opened) {

    int e;
    uint8_t v;
    int32_t duration;

    e = tape_get_duration_1200ths(ts, &duration);
    if (TAPE_E_OK != e) { return e; }

    /* 1. TAPE NOISE */
    if (tapenoise_active) {
        tapenoise_send_1200(value, &(ts->tapenoise_no_emsgs));
    }

#ifdef BUILD_TAPE_TAPECTRL
    /* note: this will lock+unlock mutex */
    if (tapectrl_opened) {
        e = send_tone_to_tapectrl_maybe (tcw,
                                         ts->tallied_1200ths,
                                         ts->tallied_1200ths,
                                         value,
                                         since_last_tone_sent_to_gui_inout);
    }

    /* TOHv4.3 */
    /* add this to the stripes ... */
    if (iv_list_inout != NULL) {
        e = tape_interval_list_send_1200th (iv_list_inout, value, false); /* false = no free on error */
        if (TAPE_E_OK != e) { return e; }
    }
#endif

    v = (value=='0') ? 0 : 1;

    do { /* try { */

        double pulse;
        uint8_t i, num_pulses;
        bool have_uef_bit;
        uint8_t bit_value;
        uint8_t total_num_bits;
        uint16_t mask;
        uint8_t len;
        const char *payload117;
        tape_interval_t elapsed, interval;

        /* 2. TIBET */
        if (ts->filetype_bits & TAPE_FILETYPE_BITS_TIBET) { /* TIBET enabled? */
            for (i=0; (TAPE_E_OK == e) && (i < 2); i++) { /* write two tonechars */
                e = tibet_append_tonechar (&(ts->tibet), v?'.':'-', &(ts->tibet_data_pending));
            }
            if (TAPE_E_OK != e) { break; }
        }

        /* 3. UEF */
        if ( ts->filetype_bits & TAPE_FILETYPE_BITS_UEF ) {

            have_uef_bit = false;

            /* Have to decode incoming stream of 1200ths to make
               chunks &100 and &104. Need to respect wacky baud rates
               in order to do that. */

            mask = 0;
            if (1200 == f->nominal_baud) {
                mask = 1;
                len = 1;
            } else if (600 == f->nominal_baud) {
                mask = 3;
                len = 2;
            } else if (300 == f->nominal_baud) {
                mask = 0xf;
                len = 4;
            } else if (150 == f->nominal_baud) {
                mask = 0xff;
                len = 8;
            } else if (75 == f->nominal_baud) {
                mask = 0xffff;
                len = 16;
            } else {
                /* TOHv4.1: downgraded from bug to warning */
                log_warn("tape: WARNING: illegal tx baud rate %d", f->nominal_baud);
                ts->w_uef_tmpchunk.len = 0;
                break;
            }

            (ts->w_uef_enc100_shift_value) <<= 1;
            (ts->w_uef_enc100_shift_value)  |= v;
            (ts->w_uef_enc100_shift_value)  &= mask;
            (ts->w_uef_enc100_shift_amount)++;

            /* check that the frame we have so far is uniform */
            for (i=0; i < ts->w_uef_enc100_shift_amount; i++) {
                if (    (((ts->w_uef_enc100_shift_value) >> i) & 1)
                     != (ts->w_uef_enc100_shift_value & 1)          ) {
                    log_warn("tape: BUG? UEF chunk &100/&104 bit-assembly shift register desync!");
                    /* resynchronise; keep the new wayward bit and discard anything older */
                    (ts->w_uef_enc100_shift_amount)   = 1;
                    (ts->w_uef_enc100_shift_value)  >>= i;
                    (ts->w_uef_enc100_shift_value)   &= 1;
                    break;
                }
            }

            if (ts->w_uef_enc100_shift_amount == len) {
                /* bit ready */
                have_uef_bit = true;
                bit_value    = ts->w_uef_enc100_shift_value & 1;
                (ts->w_uef_enc100_shift_amount) = 0;
            }

            if (have_uef_bit) {

                total_num_bits = 1 + f->num_data_bits
                                   + ((f->parity == 'N') ? 0 : 1)
                                   + f->num_stop_bits;

                if (0 == ts->w_uef_serial_phase) {
                    /* expect start bit */
                    if (0 == bit_value) {
                        ts->w_uef_serial_frame = 0;
                        (ts->w_uef_serial_phase)++;
                    }
                    /* don't advance phase if start bit not found */
                } else if (ts->w_uef_serial_phase <= f->num_data_bits) {
                    (ts->w_uef_serial_frame) >>= 1;
                    ts->w_uef_serial_frame |= (bit_value ? 0x80 : 0);
                    (ts->w_uef_serial_phase)++;
                } else if (ts->w_uef_serial_phase == (total_num_bits - 1)) {

                    /* (second) stop bit; frame is ready */

                    if (7 == f->num_data_bits) {
                        /* TOHv4.1: hack: fix UEF bug with 7-bit frames;
                        *           one extra bitshift is required in this case. */
                        (ts->w_uef_serial_frame) >>= 1; /* one ping only */
                    }

                    ts->w_uef_serial_phase = 0; /* expect next start bit */

                    if (ts->w_uef_prevailing_baud != f->nominal_baud) {

                        ts->w_uef_prevailing_baud = f->nominal_baud; /* acknowledge the change */
                        /*printf("new baud %u\n", f->baud300 ? 300 : 1200);*/

                        if (0 == ts->w_uef_tmpchunk.num_data_bytes_written) {
                            /* OK. Nothing written yet in this tmpchunk, so we can ram a chunk 117
                               directly into the UEF stream with no ill effects. Ram */
                            /*printf("uef: new baud %u; OK to insert &117, no data written yet\n", f->baud300 ? 300 : 1200);*/
                            payload117 = NULL;
                            e = uef_get_117_payload_for_nominal_baud (f->nominal_baud, &payload117);
                            if (TAPE_E_OK != e) { break; }
                            /* dovetail against previous chunk */
                            elapsed.start_1200ths = duration;
                            elapsed.pos_1200ths = 0;
                            e = uef_store_chunk (&(ts->uef),
                                                 (uint8_t *) payload117,
                                                 0x117,
                                                 2,
                                                 0,          /* TOHv4.2: dummy offset */
                                                 0,
                                                 0,
                                                 0,          /* TOHv4.3: chunk &114 never used for writing, so 0 here */
                                                 &elapsed);  /* TOHv4.3 */
                            if (TAPE_E_OK != e) { break; }
                            /* update both tmpchunk and display tallied time with duration value (i.e. last saved UEF chunk) */
                            ts->w_uef_tmpchunk.elapsed.start_1200ths = elapsed.start_1200ths;
                            ts->tallied_1200ths = elapsed.start_1200ths;
                        } else {
                            /* TODO: a baud change occurred during a data chunk.
                               Not yet supported for UEF.
                               This needs the current data chunk to be flushed, a baud chunk &117 to be inserted,
                               and then another data chunk begun that contains the remaining data from
                               this point onwards.
                               ( Workaround: Use TIBET, insert "end","/baud 300","data" manually, then tibetuef.php. ) */
                            log_warn("uef: BUG: baud change (%u) mid-chunk is not implemented!", f->nominal_baud);
                        }
                    }

                    /* write byte into temporary chunk */
                    e = uef_append_byte_to_chunk (&(ts->w_uef_tmpchunk), ts->w_uef_serial_frame);
                    if (TAPE_E_OK != e) { break; }
                    (ts->w_uef_tmpchunk.num_data_bytes_written)++;
                } else { /* parity or first of two stop bits */
                    (ts->w_uef_serial_phase)++;
                }

            } /* endif (have_uef_bit) */

        } /* endif UEF */

        /* 4. CSW */
        if ( ts->filetype_bits & TAPE_FILETYPE_BITS_CSW ) {

            e = csw_init_blank_if_necessary(&(ts->csw));
            if (TAPE_E_OK != e) { return e; }

            pulse = ts->csw.len_1200th_smps_perfect / 2.0;
            if (v) { pulse /= 2.0; }

            /* memset(&interval, 0, sizeof(tape_interval_t)); */
            interval.pos_1200ths = 0;
            interval.start_1200ths = duration;
            num_pulses = v?4:2;

            /* At 1200 Hz, we will make 2 pulses of durations 0 and 1 1200th.
             * At 2400 Hz, we will make 4 pulses with durations 0, 0, 0 and 1 1200th. */
            for (i=0; (TAPE_E_OK == e) && (i < num_pulses); i++) {
                if (i==(num_pulses-1)) { /* TOHv4.3 */
                    interval.pos_1200ths = 1;
                }
                e = csw_append_pulse_fractional_length (&(ts->csw), pulse, &interval);
            }

            if (TAPE_E_OK != e) { break; }

        }

    } while (0); /* catch */

    /* finally */

    if (TAPE_E_OK != e) {
        ts->w_uef_tmpchunk.len = 0;
    } else {
        (ts->tallied_1200ths)++; /* TOHv4.3 */
    }

    return e;

}


static int check_pending_tibet_span_type (tape_state_t *t, uint8_t type) {
    if (t->tibet_data_pending.type != type) {
        log_warn("tape: write: warning: TIBET: pending span has type %u, should be %u",
                 t->tibet_data_pending.type, type);
        return TAPE_E_BUG;
    }
    return TAPE_E_OK;
}



static int tape_write_end_data (tape_state_t *t) {

    int e;
    tape_interval_t *elapsed;

    e = TAPE_E_OK;

    elapsed = NULL;

    if (t->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {

        elapsed = &(t->tibet_data_pending.timespan);

        if (-1 == elapsed->start_1200ths) { elapsed->start_1200ths = 0; } /* TOHv4.3-a4 */

        // tape_get_duration_1200ths(t, &(elapsed->start_1200ths));
        elapsed->pos_1200ths = t->tallied_1200ths - elapsed->start_1200ths;
/*
printf("tape_write_end_data: TIBET: setting timespan=(%d + %d)=%d\n",
       elapsed->start_1200ths, elapsed->pos_1200ths, elapsed->start_1200ths+elapsed->pos_1200ths);
*/
        /* ensure we have a data span on the go */
        e = check_pending_tibet_span_type(t, TIBET_SPAN_DATA); /* an assert */
        /* TOHv4-rc2: Can get a non-data pending span type here if you
         * hit Record and Append in the middle of a block write
         * with an initially-blank tape. In this case such a data span is
         * empty anyway so we just skip it. */
        /* TOHv4.3-a3: There is a test for this now. */
        if (TAPE_E_OK == e) {
            e = tibet_append_data_span(&(t->tibet), &(t->tibet_data_pending));
        } else {
            /* TOHv4-rc2 */
            log_warn("tape: write: warning: TIBET: discarding partial data span");
            memset(&(t->tibet_data_pending), 0, sizeof(tibet_span_t));
            return TAPE_E_OK; /* TOHv4.3-a3: trap this error */
        }
        if (TAPE_E_OK != e) { return e; }
        memset(&(t->tibet_data_pending), 0, sizeof(tibet_span_t));
    }

/*printf("len=%u, tallied %u, elapsed start %d\n", t->w_uef_tmpchunk.len, t->tallied_1200ths, elapsed->start_1200ths);*/

    /* this is where the tmpchunk actually gets stored,
       so we need accurate elapsed data now */
    if (    (TAPE_E_OK == e)
         && (t->filetype_bits & TAPE_FILETYPE_BITS_UEF)
         && (t->w_uef_tmpchunk.len > 0) /* TOHv4.3-a3 */ ) {

        elapsed = &(t->w_uef_tmpchunk.elapsed);
        /* UEF: append tmpchunk to actual UEF struct */
        elapsed->pos_1200ths = t->tallied_1200ths - elapsed->start_1200ths;

/*printf("\n*********** tallied %d, start %d\n", t->tallied_1200ths, elapsed->start_1200ths);*/
        e = uef_store_chunk(&(t->uef),
                            t->w_uef_tmpchunk.data,
                            t->w_uef_tmpchunk.type,
                            t->w_uef_tmpchunk.len,
                            0,  /* TOHv4.2: dummy offset */
                            0,
                            0,
                            0,  /* TOHv4.3: type &114 never written so cycs_114 is 0 */
                            elapsed); /* TOHv4.3 */

    }

    uef_clear_chunk (&(t->w_uef_tmpchunk));

    return e;

}


static int tape_write_start_data (tape_state_t * const t,
                                  bool const always_117,
                                  const serial_framing_t * const f) {

    /* We don't insert TIBET hints for phase or speed any more.
     * Those are qualities of a real-world tape, not
     * one produced by an emulator. */

    int e;
    uint8_t i;
    uint8_t uef_chunk_104_framing[3];
    tibet_span_hints_t *h;
    const char *payload117;
    tape_interval_t elapsed; /* TOHv4.3 */
    int32_t dur; /* TOHv4.3-a4 */

    e = TAPE_E_OK;

    if (t->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {

        e = check_pending_tibet_span_type(t, TIBET_SPAN_INVALID); /* an assert */
        if (e != TAPE_E_OK) { return e; }

        memset(&(t->tibet_data_pending), 0, sizeof(tibet_span_t));
        t->tibet_data_pending.type = TIBET_SPAN_DATA;
        h = &(t->tibet_data_pending.hints);

        h->have_baud    = 1; /* avoid bool on tibet.c/.h */
        h->baud         = f->nominal_baud;
        h->have_framing = 1;
        framing_to_string(h->framing, f);

        dur = -1;
        tape_get_duration_1200ths(t, &dur);
        t->tibet_data_pending.timespan.pos_1200ths = 0;
        tape_state.tallied_1200ths = dur;  /* TOHv4.3 */

        t->tibet_data_pending.timespan.start_1200ths = dur;

    }

    /* UEF */
    if (t->filetype_bits & TAPE_FILETYPE_BITS_UEF) {

        /* TOHv4.3 */
        elapsed.start_1200ths = 0;
        tape_get_duration_1200ths(t, &(elapsed.start_1200ths));
        elapsed.pos_1200ths = 0;

        if (always_117) {

            payload117 = NULL;

            /* Well, UEF 0.10 states that the only values which are valid in
               baud chunk &117 are 300 and 1200. Furthermore, it's only
               a 16-bit field so theoretical baud rates > 38400 won't fit.
               However, we can save perfectly valid tapes at e.g. 75 baud
               (even though they can't be loaded back in). */

            e = uef_get_117_payload_for_nominal_baud (f->nominal_baud, &payload117);
            if (TAPE_E_OK != e) { return e; }

            /* If baud is valid for it, append baud chunk &117 */
            if (NULL != payload117) {
                e = uef_store_chunk(&(t->uef),
                                    (uint8_t *) payload117,
                                    0x117,
                                    2,
                                    0,  /* TOHv4.2: dummy offset */
                                    0,
                                    0,
                                    0,
                                    &elapsed);
                if (e != TAPE_E_OK) { return e; }
            }

            /*
             * We set the last used baud rate here, so the rate change detection
             * at byte level won't be triggered.
             */

            t->w_uef_prevailing_baud = f->nominal_baud;

        }

        /* clean up any prior tmpchunk */
        t->w_uef_tmpchunk.len = 0;
        t->w_uef_tmpchunk.elapsed = elapsed; /* TOHv4.3 */
        tape_state.tallied_1200ths = elapsed.start_1200ths;  /* TOHv4.3 */

        /* work out which chunk type to use, based on framing */
        if (    (  8 == f->num_data_bits)
             && (  1 == f->num_stop_bits)
             && ('N' == f->parity) ) {
            /* 8N1 => use chunk &100 */
            t->w_uef_tmpchunk.type = 0x100;
        } else {
            /* some other framing => use chunk &104 */
            t->w_uef_tmpchunk.type = 0x104;
        }

        /* if chunk &104, then build the chunk sub-header w/framing information */
        if ( 0x104 == t->w_uef_tmpchunk.type ) {

            uef_chunk_104_framing[0] = f->num_data_bits;
            uef_chunk_104_framing[1] = f->parity;
            uef_chunk_104_framing[2] = f->num_stop_bits;

            for (i=0; (TAPE_E_OK == e) && (i < 3); i++) {
                e = uef_append_byte_to_chunk(&(t->w_uef_tmpchunk),
                                             uef_chunk_104_framing[i]);
            }

        }

        /* on error, clean up tmpchunk */
        if (e != TAPE_E_OK) {
            t->w_uef_tmpchunk.len = 0;
            return e;
        }

    }

    return e;

}



/* TODO: no support yet for (leader+&AA+leader), chunk &111 */
static int tape_write_leader (tape_state_t * const t, uint32_t num_1200ths, bool const populate_elapsed) {

    int e;
    uint8_t num_2400ths_enc[2];
    tibet_span_hints_t hints;
    tape_interval_t elapsed;
    int32_t d;

    e = TAPE_E_OK;
    memset(&hints, 0, sizeof(tibet_span_hints_t));

    if (t->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {
        /* sanity: assert that no data is pending */
        e = check_pending_tibet_span_type (t, TIBET_SPAN_INVALID);
        if (e != TAPE_E_OK) { return e; }
    }

    /* this compromise may be necessary, because num_1200ths is computed
       as (num_4800ths / 4): */
    if (0 == num_1200ths) {
        num_1200ths = 1;
    }

    d=-1;
    // num_2400ths = 0x7fffffff;
    if (populate_elapsed) {
        tape_get_duration_1200ths(t, &d);
    }


    if (t->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {
        e = tibet_append_leader_span (&(t->tibet), d, 2 * num_1200ths, &hints);
        if (TAPE_E_OK != e) { return e; }
    }

    /* chunk &110 */
    if (t->filetype_bits & TAPE_FILETYPE_BITS_UEF) {

        /* TOHv4.3: elapsed */
        elapsed.start_1200ths = d;  /* chunk must dovetail to previous */
        elapsed.pos_1200ths = num_1200ths;

        tape_write_u16(num_2400ths_enc, 2 * num_1200ths);
        e = uef_store_chunk(&(t->uef), num_2400ths_enc, 0x110, 2, 0, num_1200ths * 2, 0, 0, &elapsed);  /* TOHv4.2: dummy offset */
        if (TAPE_E_OK != e) { return e; }

    }

    if (t->filetype_bits & TAPE_FILETYPE_BITS_CSW) {
        e = csw_append_leader(&(t->csw), num_1200ths, d);
    }

    return e;

}


/* TOHv3.2 (rc1 killer!): respect -tape112 switch (or GUI equivalent) */
static int tape_write_silence_2 (tape_state_t * const t,
                                 double len_s,
                                 bool const silence112,
                                 int32_t const silence_start_elapsed_1200ths) { /* TOHv4.3 */

    int e;
    tibet_span_hints_t hints;
    int32_t rem, num_2400ths;
    uint8_t buf[2];
    tape_interval_t elapsed;

    e = TAPE_E_OK;
    memset(&hints, 0, sizeof(tibet_span_hints_t));

/* printf("tape_write_silence_2: %lf\n", len_s); */

    if (len_s < TAPE_1200TH_IN_S_FLT) {
        log_warn("tape: WARNING: zero-length silence on write, adjusting to one cycle :/");
        /* hack, to prevent silence w/duration of zero 1200ths */
        len_s = TAPE_1200TH_IN_S_FLT;
    }

    /* FIXME: this is a little bit nasty, because the TIBET structs
       only allow expressing silence in 2400ths, whereas the
       text file itself allows silence with floating-point resolution */
    if (t->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {
        e = tibet_append_silent_span (&(t->tibet), len_s, silence_start_elapsed_1200ths, &hints);
        if (TAPE_E_OK != e) { return e; }
    }

    /* silence in UEFs ... */
    if (t->filetype_bits & TAPE_FILETYPE_BITS_UEF) {
        elapsed.start_1200ths = silence_start_elapsed_1200ths; /* TOHv4.3 */
        if (silence112) {
            /* one or more chunk &112s */
            num_2400ths = (int32_t) (0.5f + (len_s * TAPE_1200_HZ * 2.0));
            if (0 == num_2400ths) {
                log_warn ("tape: WARNING: silence as &112: gap is tiny (%f s); round up to 1/2400", len_s);
                num_2400ths = 1;
            }
            /* multiple chunks needed */
            for ( rem = num_2400ths; rem > 0; rem -= 65535 ) {
                tape_write_u16(buf, (rem > 65535) ? 65535 : rem);
                elapsed.pos_1200ths = (rem / 2);
                e = uef_store_chunk (&(t->uef),
                                     buf,
                                     0x112,
                                     2,
                                     0,            /* TOHv4.2: dummy offset */
                                     num_2400ths,  /* TOHv4.3: pre_cycs for silence (for seeking) */
                                     0,
                                     0,
                                     &elapsed);
                if (TAPE_E_OK != e) { return e; }
                elapsed.start_1200ths += elapsed.pos_1200ths;
            }
        } else {
            /* chunk &116 */
            /* FIXME: endianness? */
            union { float f; uint8_t b[4]; } u;
            u.f = len_s;
            num_2400ths = (int32_t) (0.5f + (len_s * TAPE_1200_HZ * 2.0));
            elapsed.pos_1200ths = (num_2400ths / 2);
            e = uef_store_chunk(&(t->uef),
                                u.b,
                                0x116,
                                4,
                                0,           /* TOHv4.2: dummy offset */
                                num_2400ths, /* TOHv4.3: pre_cycs for silence (for seeking) */
                                0,
                                0,
                                &elapsed);
            if (TAPE_E_OK != e) { return e; }
        }
    }

    if (t->filetype_bits & TAPE_FILETYPE_BITS_CSW) {
        elapsed.start_1200ths = silence_start_elapsed_1200ths; /* TOHv4.3-a4 */
        num_2400ths = (int32_t) (0.5f + (len_s * TAPE_1200_HZ * 2.0));
        elapsed.pos_1200ths = (num_2400ths / 2);
        e = csw_append_silence(&(t->csw), len_s, &elapsed);
    }

    return e;

}

/* TOHv4.1: removed record_activated arg; this function should
 *          now only ever be called if record mode is activated */
int tape_wp_init_blank_tape_if_needed (uint8_t * const filetype_bits_inout) {
    /* tape loaded? */
    if (TAPE_FILETYPE_BITS_NONE != *filetype_bits_inout) {
        return TAPE_E_OK; /* yes */
    }
    /* no tape loaded: initialise blank tape; all three/four
       file types simultaneously, since we don't know
       which one the user wants yet. */
    *filetype_bits_inout = TAPE_FILETYPES_ALL;
    log_info("tape: initialised blank tape");
    gui_alter_tape_menus(*filetype_bits_inout);
    return TAPE_E_OK;
}

#include "tapeseek.h"

/* tape.c */
static int wp_bitclk_handle_silence (tape_state_t         * const t,
                                     tape_ctrl_window_t   * const tcw,                 /* TOHv4.3 */
                                     bool                   const silent,
                                     bool                   const tapenoise_active,
                                     bool                   const record_is_pressed,
                                     int64_t                const ns_per_bit,
                                     bool                   const silence112,
                                     bool                   const tapectrl_opened,
                                     tape_interval_list_t * const iv_list_inout,       /* the master copy on tape_vars->interval_list, not its tapectrl clone */
                                     bool                 * const reset_shift_register_out,
                                     uint32_t             * const since_last_tone_sent_to_gui_inout) {

    int e;
    int64_t num_1200ths, i;
    int32_t duration; /* TOHv4.3 */

    e = TAPE_E_OK;

    /* handle TX silence logic:
       1. if silent, wipe the ACIA's tx data, send 'S' to tape noise
       2. if not silent, but previously silent, end section, flush the silence to tape
    */

    if ( silent ) {
        /* TOHv4.3 */
        /* silent section BEGINS? record current elapsed time */
        if (0 == t->w_accumulate_bit_periods_ns) {
            /* borrow this field, even though we don't use tmpchunk to make a silent chunk */
            t->w_uef_tmpchunk.elapsed.start_1200ths = t->tallied_1200ths;
        }
        e = wp_end_data_section_if_ongoing(t, record_is_pressed, reset_shift_register_out);
        if (TAPE_E_OK != e) { return e; }
        if (record_is_pressed) { /* only accumulate silence if record is pressed */
            t->w_accumulate_silence_ns += ns_per_bit;
        }
        t->w_accumulate_bit_periods_ns += ns_per_bit;
        num_1200ths = (t->w_accumulate_bit_periods_ns) / TAPE_1200TH_IN_NS_INT;
        /* consume, if > 0; leave any remainder on variable for next time */
        t->w_accumulate_bit_periods_ns -= (num_1200ths * TAPE_1200TH_IN_NS_INT);
        for (i=0; (i < num_1200ths); i++) { /* TOHv4: tones_per_bit loop */
            if (tapenoise_active) {
                tapenoise_send_1200 ('S', &(t->tapenoise_no_emsgs));
            }
            /* TOHv4.3 */
            if (record_is_pressed) {
                (t->tallied_1200ths)++;
#ifdef BUILD_TAPE_TAPECTRL
                e = tape_interval_list_send_1200th(iv_list_inout, 'S', false); /* no free on error */
#endif
            }
        }
        /* TOHv4.3 */
        if (record_is_pressed && tapectrl_opened) {
#ifdef BUILD_TAPE_TAPECTRL
            /* will lock+unlock mutex */
            send_tone_to_tapectrl_maybe (tcw,
                                         t->tallied_1200ths,
                                         t->tallied_1200ths,
                                         'S',
                                         //true,
                                         //true,
                                         since_last_tone_sent_to_gui_inout);
#endif

            if (TAPE_E_OK != e) { return e; }
        }
    } else if (t->w_accumulate_silence_ns > 0) {
        /* a prior silent section has ended, so we need to write it out */
        /* TOHv4.3: */
        e = tape_get_duration_1200ths(t, &duration);
        if (TAPE_E_OK != e) { return e; }
        if (record_is_pressed) {
            e = tape_write_silence_2 (t,
                                      ((double) t->w_accumulate_silence_ns) / 1000000000.0,
                                      silence112,
                                      duration);
            if (TAPE_E_OK != e) { return e; }
        }
        t->tallied_1200ths += (t->w_accumulate_silence_ns / TAPE_1200TH_IN_NS_INT); /* TOHv4.3 */
        t->w_accumulate_silence_ns = 0;
    }

    return e;

}


/* TOHv3.2: rework; flush partial frames out to UEF properly */
static int wp_end_data_section_if_ongoing (tape_state_t * const t,
                                           bool const record_is_pressed,
                                           bool * const reset_shift_register_out) {

    int e;

    if ( ! t->w_must_end_data ) { return TAPE_E_OK; }

    e = TAPE_E_OK;
    *reset_shift_register_out = 0;

    /* Make sure there isn't a half-finished frame kicking about
        on the UEF reservoir; this would end up being prepended to the start of
        the next block, with disastrous consequences. This can happen
        if the ACIA pipelining is wrong and the master reset that fires
        at the end of the block lets half a byte escape or something.
        Particular culprits: 300 baud, fast-tape writing. */

    if ((t->filetype_bits & TAPE_FILETYPE_BITS_UEF) && record_is_pressed && (t->w_uef_serial_phase != 0)) {

        log_warn("tape: WARNING: partial frame (phase=%u, expected 0)",
                    t->w_uef_serial_phase);

        e = tape_uef_flush_incomplete_frame (&(t->uef),
                                             t->tallied_1200ths,
                                             &(t->w_uef_serial_phase),
                                             &(t->w_uef_serial_frame),
                                             &(t->w_uef_tmpchunk));
        if (TAPE_E_OK != e) { return e; } /* TOHv4.3-a4: errors were being missed */
        t->w_uef_serial_phase = 0;

    }

    *reset_shift_register_out = 1;

    if (record_is_pressed) {
        e = tape_write_end_data(t);
    }


    t->w_must_end_data = 0;

    return e;

}


static int wp_bitclk_accumulate_leader (tape_state_t         * const t,
                                        tape_ctrl_window_t   * const tcw,
                                        tape_interval_list_t * const iv_list_inout,   /* this will be the one from tape_vars.interval_list */
                                        bool                   const tapenoise_active,
                                        int64_t                const ns_per_bit,
                                        bool                   const record_is_pressed,
                                        bool                 * const reset_shift_reg_out,
                                        uint32_t             * const since_last_tone_sent_to_gui_inout,
                                        bool                   const tapectrl_opened) {

    int32_t num_1200ths, i;
    int e;

    /* if there is nothing to output in the shift register,
     * then we have leader tone */

    /* end and write out any current data section */
    e = wp_end_data_section_if_ongoing (t, record_is_pressed, reset_shift_reg_out);
    if (TAPE_E_OK != e) { return e; } /* TOHv4.3: bugfix */
    if (record_is_pressed) { /* only accumulate leader if record is pressed */
        t->w_accumulate_leader_ns += ns_per_bit;
    }
    t->w_accumulate_bit_periods_ns += ns_per_bit;
    num_1200ths = t->w_accumulate_bit_periods_ns / TAPE_1200TH_IN_NS_INT;
    /* consume it; leave remainder */
    t->w_accumulate_bit_periods_ns -= (num_1200ths * TAPE_1200TH_IN_NS_INT);
    for (i=0; i < num_1200ths; i++) {
        if (tapenoise_active) {
            tapenoise_send_1200 ('L', &(t->tapenoise_no_emsgs));
        }
        /* TOHv4.3 */
#ifdef BUILD_TAPE_TAPECTRL
        /* No -- don't gate this with tapectrl_opened. The intervals list
         * on tape_vars must be updated regardless of the status of
         * the tapectrl. */
        if (record_is_pressed) { /* && tapectrl_opened) {*/
            e = tape_interval_list_send_1200th(iv_list_inout, 'L', false); /* no free on error */
        }
        if (TAPE_E_OK != e) { return e; }
#endif
    }
    /* TOHv4.3: update tapectrl window:
     * - set elapsed to duration
     * - leader to lamps*/
    if (record_is_pressed) { /* fix: needed this check or it would send
                                spurious TX 'L' to tapectrl during RX */
        t->tallied_1200ths += num_1200ths;
        if ((num_1200ths > 0) && tapectrl_opened) {
            /* will lock+unlock mutex */
#ifdef BUILD_TAPE_TAPECTRL
            send_tone_to_tapectrl_maybe (tcw,
                                         t->tallied_1200ths,
                                         t->tallied_1200ths,
                                         'L',
                                         since_last_tone_sent_to_gui_inout);
#endif
        }
    }
    return e;
}


static void framing_to_string (char fs[4], const serial_framing_t * const f) {
    fs[0] = (f->num_data_bits == 7) ? '7' : '8';
    fs[1] = f->parity;
    fs[2] = (f->num_stop_bits == 1) ? '1' : '2';
    fs[3] = '\0';
}



