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

#include "tapeseek.h"
#include "tape-interval.h"

/* TOHv4.3 */
static int tape_seek_main (tape_state_t * const ts,
                           int32_t const time_1200ths_wanted,
                           int32_t * const got_1200ths_out,
                           bool * const throw_eof_out,
                           bool * const desynced_inout);

static int change_current_piece (tape_state_t * const ts,
                                 int64_t const piece_ix,
                                 bool * const seek_desynced_out);

static int get_num_pieces (tape_state_t * const ts, int64_t * const out);

/* TOHv4.3 */
static int get_time_interval_for_piece (tape_state_t         * const ts,       /* not const -- TIBET might modify it */
                                        int64_t                const piece_ix, /* UEF chunk ix, TIBET span ix, CSW pulse ix */
                                        tape_interval_t * const interval_out);

#include "tape.h"

/* TOHv4.3 */
/* danger: value will not be valid until initial tape scan */
/* TODO: add a duration_is_valid flag to tape_state_t based on an EOF having been encountered at least once */
int tape_get_duration_1200ths (tape_state_t * const ts,
                               int32_t * const duration_1200ths_out) {
    int e;
    e = TAPE_E_OK;
    *duration_1200ths_out = -1;
    if ( TAPE_E_OK != ts->prior_exception ) {
        log_warn("tape: BUG: tape_get_duration_1200ths is called even though tape is disabled!");
        return TAPE_E_BUG;
    } else if (ts->filetype_bits & TAPE_FILETYPE_BITS_UEF) {
        e = uef_get_duration_1200ths(&(ts->uef), duration_1200ths_out);
    } else if (ts->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {
        e = tibet_get_duration(&(ts->tibet), duration_1200ths_out);
    } else if (ts->filetype_bits & TAPE_FILETYPE_BITS_CSW) {
        csw_get_duration_1200ths(&(ts->csw), duration_1200ths_out); /* void return */
    } else {
    }
    return e;
}


/* TOHv4.3 */
int tape_seek_absolute (tape_state_t * const ts, /* modified */
                             int32_t         time_1200ths_wanted,
                             int32_t   const duration_1200ths,
                             int32_t * const time_1200ths_actual_out,
                                bool * const eof_out,
                                bool * const desynced_inout) {

    int e;
    int32_t d;

    e = TAPE_E_OK;
    d = 0;

    if (eof_out != NULL) {
        *eof_out = false;
    }

/* printf("tape_seek_absolute: %d/%d\n", time_1200ths_wanted, duration_1200ths); */

#ifdef BUILD_TAPE_SANITY
    if (time_1200ths_wanted < 0) {
        log_warn("tape: seek: BUG: time_1200ths_wanted is duff (%d)", time_1200ths_wanted);
        return TAPE_E_BUG;
    }
#endif
    d = duration_1200ths;
    if (time_1200ths_wanted >= d) {
        time_1200ths_wanted = d - TAPE_SEEK_CLAMP_BACK_OFF_1200THS;
    } /* clamp */
    if (time_1200ths_wanted < 0) {
        time_1200ths_wanted = 0;
    }
    if (time_1200ths_actual_out != NULL) {
        *time_1200ths_actual_out = -1;
    }
    e = tape_seek_main (ts,
                        time_1200ths_wanted,
                        time_1200ths_actual_out,
                        eof_out,
                        desynced_inout);
    return e;
}

#include "taperead.h"


/* TOHv4.3 */
static int tape_seek_main (tape_state_t * const ts,
                           int32_t const time_1200ths_wanted,
                           int32_t * const got_1200ths_out,
                           bool * const throw_eof_out,
                           bool * const desynced_inout) {

    int32_t d;
    tape_interval_t interval, interval2;
    int e;
    int32_t t;
    bool my_eof;

    /* UEF uses int32_t for chunk index;
     * both CSW and TIBET use uint32_t for span or pulse index.
     *
     * We hence need a "position" variable which is capable of containing
     * both uint32_t and int32_t. Use int64_t. */
    int64_t len, orig_len, piece;

    d=0;
    my_eof = false;
    if (throw_eof_out != NULL) {
        *throw_eof_out = false;
    }

    e = tape_get_duration_1200ths(ts, &d);
    if (TAPE_E_OK != e) { return e; }

    if (time_1200ths_wanted > d) {
        log_warn("tape: BUG: requested seek time (%d) exceeds duration (%d)!\n", time_1200ths_wanted, d);
        return TAPE_E_BUG;
    }

    orig_len = -1;
    piece = -1;

    e = get_num_pieces(ts, &orig_len); /* if multiple filetypes, this will use UEF */
    if (TAPE_E_OK != e) { return e; }

    memset(&interval, 0, sizeof(tape_interval_t));

    if (orig_len > 1) {

        /* Do we actually need to seek on all three back-ends simultaneously?
         * We'll only ever read from one of them (UEF), and writes
         * are always appended to the end of the stream.
         *
         */

        /* coarse binary chop */
/*printf("seek: coarse   : ");*/
        for (len = orig_len / 4, piece = orig_len / 2;
            len > 0;
            len /= 2) {

            e = get_time_interval_for_piece (ts, piece, &interval); /* UEF, if multi */
            if (TAPE_E_OK != e) { return e; }

            if (interval.start_1200ths > time_1200ths_wanted) {
                piece -= len;
            } else {
                piece += len;
            }
/*printf("%"PRId64" ", piece);*/
        }
/*printf("\n");*/

        /* fine (part one): advance pieces until we find the wanted time */
/*printf("      fine, fwd: ");*/
        do {
            e = get_time_interval_for_piece(ts, piece, &interval); /* UEF, if multi */
            if (TAPE_E_OK != e) { return e; }
            interval2.start_1200ths = -1;
            if ((piece+1) < orig_len) {
                e = get_time_interval_for_piece(ts, piece+1, &interval2); /* UEF, if multi */
                // t1 = chks[start_chunk+1].elapsed.start_1200ths;
                if (TAPE_E_OK != e) { return e; }
            }
            if (    (interval2.start_1200ths > interval.start_1200ths)
                 && (interval2.start_1200ths <= time_1200ths_wanted) ) {
                /*
                log_warn("tape: seek: fixup: want time %d, piece #%"PRId64"/%"PRId64" has %d, #%"PRId64"/%"PRId64" has %d",
                        time_1200ths_wanted,
                        piece,
                        orig_len,
                        interval.start_1200ths,
                        piece+1,
                        orig_len,
                        interval2.start_1200ths); */
                piece++;
/*printf("%"PRId64" ", piece);*/
            } else {
                break;
            }
        } while (interval2.start_1200ths>0);
/*printf("\n");*/

        /* fine (part two): rewind pieces until we find wanted time
         * (rarely needed but issues near start of files) */
/*printf("      fine, rev: ");*/
        e = get_time_interval_for_piece(ts, piece, &interval); /* UEF, if multi */
        if (TAPE_E_OK != e) { return e; }
        while (interval.start_1200ths > time_1200ths_wanted) {
            piece--;
/*printf("%"PRId64" ", piece); */
            e = get_time_interval_for_piece(ts, piece, &interval); /* UEF, if multi */
            if (TAPE_E_OK != e) { return e; }
        }
/*printf("\n");*/

    }

    /* fudge: CSW, TIBET won't let you seek back to t=0 for some reason.
     * if requested time was 0, override the seek process and just force piece=0 */
    if ((0 == time_1200ths_wanted) || (piece < 0) /*TOHv4.3-a4*/) {
        piece = 0;
    }

    e = change_current_piece (ts, piece, desynced_inout);
    if (TAPE_E_OK != e) { return e; }

    e = get_time_interval_for_piece (ts, piece, &interval);
    if (TAPE_E_OK != e) { return e; }

    if (interval.start_1200ths < 0) {
        log_warn("tape: seek: BUG: get_time_interval_for_piece (%"PRId64"/%"PRId64") gave bad interval start %d",
                piece, orig_len, interval.start_1200ths);
        return TAPE_E_BUG;
    }

/* printf("SEEK: wanted %d, hit %d\n", time_1200ths_wanted, interval.start_1200ths); */

    for (t = interval.start_1200ths ;
         (TAPE_E_OK == e) && (t < time_1200ths_wanted);
         t++) {

        char tc;

        e = tape_1200th_from_back_end (false, ts, false, false, &tc, NULL);

        if (TAPE_E_OK != e) {
            // this shouldn't happen
            log_warn("tape: seek: time not found in piece #%"PRId64"/%"PRId64" (code %d); "
                     "sought %d 1200ths and read %d, interval (%d + %d), duration is %d\n",
                     piece,    /* partition piece (chunk, span, pulse) */
                     orig_len, /* total num. pieces (chunk, span, pulse) */
                     e,
                     time_1200ths_wanted,
                     t,
                     interval.start_1200ths,
                     interval.pos_1200ths,
                     d);
        }
    } /* next 1200th in interval */

    if (throw_eof_out != NULL) {
        *throw_eof_out = my_eof;
    }
    if (got_1200ths_out != NULL) {
        *got_1200ths_out = t;
    }

    return e;

}

/* TOHv4.3 */
/* beware: if multiple filetypes are available, only the UEF gets the seek.
 * This hopefully won't matter, as we read exclusively from the UEF on playback,
 * and always advance to the end of the tape when recording, so desync
 * shouldn't matter.
 */
static int change_current_piece (tape_state_t * const ts,
                                 int64_t const piece_ix,
                                 bool * const desynced_inout) {

    int e;
    uint8_t types, bits;

    e = TAPE_E_OK;

    if (piece_ix < 0) { /* TOHv4.3-a4 */
        log_warn("tapeseek: BUG: changing piece: piece_ix is illegal (%"PRId64")", piece_ix);
        return TAPE_E_BUG;
    }

    for (bits=ts->filetype_bits, types=0; bits; bits>>=1, types+=(1&bits))
        { }
/* printf("change_current_piece: %lld\n", piece_ix); */
    if (TAPE_FILETYPE_BITS_UEF & ts->filetype_bits) {
        /* Reset bitsource for arbitrary new chunk.
           Baud rate is reset to 1200. */
        e = uef_change_current_chunk(&(ts->uef), 0x7fffffff & piece_ix);
    } else if (TAPE_FILETYPE_BITS_CSW & ts->filetype_bits) {
        e = csw_change_current_pulse(&(ts->csw), 0xffffffff & piece_ix);
    } else if (TAPE_FILETYPE_BITS_TIBET & ts->filetype_bits) {
        e = tibet_change_current_span(&(ts->tibet), 0xffffffff & piece_ix);
    }
    /*
     * What we will do is to rewind both CSW and TIBET so that
     * they don't EOF if we happen to read from them.
     */
    if (types>2) { /* do we have more than just WAV + UEF? */
        if (TAPE_FILETYPE_BITS_CSW & ts->filetype_bits) {
            csw_rewind(&(ts->csw));
            if ( (desynced_inout != NULL) && ! *desynced_inout ) {
                log_warn("tape: change_current_piece: WARNING? seek desyncs across parallel copies");
            }
        }
        if (TAPE_FILETYPE_BITS_TIBET & ts->filetype_bits) {
            tibet_rewind(&(ts->tibet));
            if ( (desynced_inout != NULL) && ! *desynced_inout ) {
                log_warn("tape: change_current_piece: WARNING? seek desyncs across parallel copies");
            }
        }
        if (desynced_inout != NULL) {
            *desynced_inout = true; /* prevent further messages */
        }
    }

    return e;
}

/* TOHv4.3 */
static int get_num_pieces (tape_state_t * const ts, int64_t * const out) {
    uint32_t n;
    int e;
    if (TAPE_FILETYPE_BITS_UEF & ts->filetype_bits) {
        *out = ts->uef.num_chunks;    /* int32_t -> int64_t */
    } else if (TAPE_FILETYPE_BITS_CSW & ts->filetype_bits) {
        *out = ts->csw.pulses_fill;   /* uint32_t -> int64_t */
    } else if (TAPE_FILETYPE_BITS_TIBET & ts->filetype_bits) {
        n=0;
        e = tibet_get_num_spans(&(ts->tibet), &n);
        if (TAPE_E_OK != e) { return e; }
        *out = n;  /* uint32_t -> int64_t */
    }
    return TAPE_E_OK;
}

/* TOHv4.3 */
static int get_time_interval_for_piece (tape_state_t    * const ts,       /* not const -- TIBET might modify it */
                                        int64_t           const piece_ix, /* UEF chunk ix, TIBET span ix, CSW pulse ix */
                                        tape_interval_t * const interval_out) {
    int e;
    interval_out->start_1200ths = 0;
    interval_out->pos_1200ths = 0;
    if (piece_ix < 0) { /* TOHv4.3 */
        log_warn("tape: seek: BUG: bad piece_ix %"PRId64"\n", piece_ix);
        return TAPE_E_BUG;
    }
    if (TAPE_FILETYPE_BITS_UEF & ts->filetype_bits) {
        *interval_out = ts->uef.chunks[0x7fffffff & piece_ix].elapsed;
    } else if ((TAPE_FILETYPE_BITS_CSW & ts->filetype_bits) && (ts->csw.pulses != NULL)) {
        *interval_out = ts->csw.pulses[0xffffffff & piece_ix].timespan;
    } else if (TAPE_FILETYPE_BITS_TIBET & ts->filetype_bits) {
        e = tibet_get_time_interval_for_span(&(ts->tibet), 0xffffffff & piece_ix, interval_out);
        if (TAPE_E_OK != e) { return e; }
    }
    return TAPE_E_OK;
}


/* TOHv4.3 */
/* FIXME: why do we have both read_ffwd_to_end() and tape_ffwd_to_end()? */
/*
int tape_ffwd_to_end (tape_state_t * const ts) {
    int e;
    if (TAPE_FILETYPE_BITS_UEF & ts->filetype_bits) {
        e = uef_ffwd_to_end(&(ts->uef));
        if (TAPE_E_OK != e) { return e; }
    }
    if (TAPE_FILETYPE_BITS_CSW & ts->filetype_bits) {
        e = csw_ffwd_to_end(&(ts->csw));
        if (TAPE_E_OK != e) { return e; }
    }
    if (TAPE_FILETYPE_BITS_TIBET & ts->filetype_bits) {
        e = tibet_ffwd_to_end(&(ts->tibet));
        if (TAPE_E_OK != e) { return e; }
    }
    return TAPE_E_OK;
}
*/

int read_ffwd_to_end (tape_state_t *t) {
    int e;
    e = TAPE_E_OK;
    if (TAPE_FILETYPE_BITS_UEF & t->filetype_bits) {
        e = uef_ffwd_to_end (&(t->uef));
    }
    if ((TAPE_E_OK == e) && (TAPE_FILETYPE_BITS_CSW & t->filetype_bits)) {
        e = csw_ffwd_to_end (&(t->csw));
    }
    if ((TAPE_E_OK == e) && (TAPE_FILETYPE_BITS_TIBET & t->filetype_bits)) {
        e = tibet_ffwd_to_end (&(t->tibet));
    }
    t->tape_finished_no_emsgs = 1;
    return e;
}


int tapeseek_run_initial_scan (tape_state_t * const ts,
                               tape_interval_list_t * const iv_list_inout) {

    int e;
    char tone;
    tape_interval_t iv;
    bool at_eof;
#ifdef BUILD_TAPE_TAPECTRL
    int64_t np;
#endif
    int32_t leader_detect_1200ths;

    memset(&iv, 0, sizeof(tape_interval_t));

    if (NULL != iv_list_inout) {
        tape_interval_list_finish(iv_list_inout);
    }

    ts->csw.cur_silence_1200th = 0;
    ts->csw.num_silent_1200ths = 0;

    tone   = 'S';
    e      = TAPE_E_OK;
    at_eof = false;

    leader_detect_1200ths = 0;

    /* this process builds two (2) different interval lists;
     * - one goes on CSW pulse, UEF chunk, or TIBET span;
     * - the other goes on tape_vars (and is returned in the arguments of this function) */

    do {
        tone = '\0';
        e = tape_read_1200th  (ts,
                               true, /* initial-scan mode! build time map on ts */
                               // &initial_scan_csw_pulse_leftovers_smps,
                               &tone,
                               NULL);

        if ('L' == tone) {
            if (leader_detect_1200ths > TAPE_CRUDE_LEADER_DETECT_1200THS) {
                tone = 'L';
            } else {
                leader_detect_1200ths++;
            }
        } else {
            leader_detect_1200ths = 0;
        }

        if ( TAPE_E_EOF == e ) {
            at_eof = true;
            e = TAPE_E_OK;
        }

#ifdef BUILD_TAPE_TAPECTRL
        if ( ('\0' != tone) && ( TAPE_E_OK == e ) && ! at_eof && (NULL != iv_list_inout) ) {
            e = tape_interval_list_send_1200th (iv_list_inout, tone, true); /* free on error */
        }
#endif

    } while ( ( TAPE_E_OK == e ) && ! at_eof );

#ifdef BUILD_TAPE_TAPECTRL
    /* FINALO PIECE */
    if ((TAPE_E_OK == e) && (NULL != iv_list_inout)) {
        e = tape_interval_list_append (iv_list_inout,
                                       &(iv_list_inout->decstate.wip),
                                       true);
        memset(&(iv_list_inout->decstate.wip), 0, sizeof(tape_interval_t));
    }

    if (TAPE_E_OK != e) { return e; }

    if ((iv_list_inout != NULL) && (iv_list_inout->fill > 0)) {

        tape_interval_t foo, *bar;
        int32_t t_foo, t_bar, delta;

        memset(&foo, 0, sizeof(foo));

        np=0;
        get_num_pieces(ts, &np);
        if (np>0) {
            get_time_interval_for_piece(ts, np-1, &foo);
        }

        bar = iv_list_inout->list + iv_list_inout->fill - 1;
        t_foo = foo.start_1200ths  + foo.pos_1200ths;
        t_bar = bar->start_1200ths + bar->pos_1200ths;
        delta = t_bar - t_foo;

        /*
        printf("durations; per stripes: %d; per pieces: %d; delta %d (%f s)\n",
                t_bar,
                t_foo,
                delta,
                delta / TAPE_1200_HZ); //1201.92307692);
        */

        if (delta < 0) { delta *= -1; }

        /* Allow an off-by-one between these two ways of measuring duration.
         * At 300 baud, this off-by-one becomes an off-by-four.
         * Ignore discrepancies of 4 x 1200ths or shorter. Worse errors
         * are flagged as a bug. */
        if ((delta>0) && (delta<5)) {
            log_warn("tape: initial scan: WARNING: minor duration mismatch (%d)", delta);
        } else if (delta>0) {
            log_warn("tape: initial scan: BUG: severe duration mismatch (%d)", delta);
            return TAPE_E_BUG;
        }

    }
#endif /* BUILD_TAPE_TAPECTRL */

    ts->csw.cur_pulse = 0;

    return e;

}



int tape_rewind_2 (tape_state_t * const t,
                   tape_ctrl_window_t * const tcw,
                   bool const record_activated,
                   bool const tapectrl_opened) {
    int e;
    e = TAPE_E_OK;
    if (record_activated) { return e; } /* TOHv4.3 */
    if (TAPE_FILETYPE_BITS_CSW   & t->filetype_bits) {
        csw_rewind(&(t->csw));
    }
    if (TAPE_FILETYPE_BITS_TIBET & t->filetype_bits) {
        e = tibet_rewind(&(t->tibet));
    }
    if (TAPE_FILETYPE_BITS_UEF   & t->filetype_bits) {
        uef_rewind(&(t->uef));
    }
    t->tape_finished_no_emsgs = 0;   /* TOHv2: un-inhibit "tape finished" msg */
    t->tallied_1200ths = 0;  /* TOHv4.3 */
#ifdef BUILD_TAPE_TAPECTRL
    /* will lock mutex */
    if (tapectrl_opened && (tcw != NULL)) {
        tapectrl_to_gui_msg_error (tcw, true, false, TAPE_E_OK);
        tapectrl_set_gui_rapid_value_time  (tcw, false, true, 0); //, tcw->duration_1200ths);
    }
#endif
    return e;
}


/* -tapeseek command in debugger
 *  (runs in main thread) */
int tape_seek_for_debug (tape_state_t * const ts,
                         tape_vars_t  * const tv,
                         double         const seek_fraction) {

    int32_t duration;
    int e;
#ifdef BUILD_TAPE_TAPECTRL
    tape_ctrl_msg_from_gui_t from_gui;
    tape_ctrl_msg_to_gui_t to_gui;
    tape_ctrl_window_t *tcw;
#endif

    e = TAPE_E_OK;

    tape_get_duration_1200ths(ts, &duration);
    ts->tallied_1200ths = (duration * seek_fraction);

#ifdef BUILD_TAPE_TAPECTRL
    if (tv->tapectrl_opened) {
        tcw = &(tv->tapectrl);
        TAPECTRL_LOCK_MUTEX(tcw->mutex);
        if (NULL != tcw->display) {
            /* If building w/tapectrl, send seek messages both ways;
             * to the main thread, to the GUI. */
            memset(&from_gui, 0, sizeof(tape_ctrl_msg_from_gui_t));
            from_gui.type = TAPECTRL_FROM_GUI_SEEK;
            from_gui.data.seek.fraction = seek_fraction;
            from_gui.ready = true;
            memset(&to_gui, 0, sizeof(tape_ctrl_msg_to_gui_t));
            tapectrl_queue_from_gui_msg(tcw, &from_gui);
            tapectrl_set_gui_rapid_value_time (tcw, false, false, ts->tallied_1200ths); //, duration);
            /* fall through! */
        } else {
            /* if we don't have tapectrl, don't use the msg systems; just call this directly. */
            e = tape_seek_absolute (ts,
                                    duration * seek_fraction,
                                    duration,
                                    NULL,
                                    NULL,
                                    NULL);
        }
        /* is this code actually needed? */
        TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
        /* FUNCTION RETURNS HERE */
        return e;
    }
#endif

    /* Tapectrl not compiled in, or tapectrl window is closed.
     * Mutex not locked.
     * If we don't have tapectrl, don't use the msg queues; just call this directly. */
    e = tape_seek_absolute (ts,
                            duration * seek_fraction,
                            duration,
                            NULL,
                            NULL,
                            NULL);
    return e;

}
