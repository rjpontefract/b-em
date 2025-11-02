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

#include "b-em.h"
#include "acia.h"
#include "led.h"
#include "tape.h"
#include "serial.h"
#include "tapenoise.h"   /* for sound_tape flag */
#include "uef.h"
#include "csw.h"
#include "sysacia.h"
#include "tibet.h"
#include "gui-allegro.h" /* TOHv3: for greying-out menu items */
#include "main.h"        /* for shutdown codes */
#include "tapewrite.h"
#include "taperead.h"
#include <ctype.h>

/* VARIABLES */

/* saveable state lives here: */
tape_state_t tape_state;
/* other tape variables go here (e.g. config settings): */
tape_vars_t tape_vars;
/* legacy tape variables */
int tapeledcount; /*,tapelcount;*/
/* dummy variable, so a valid char * pointer may be passed to snprintf() on Unix
 * when emulating windows's scprintf() */
char tape_dummy_for_unix_scprintf;

/* should be called by the reset callback */
int tape_handle_acia_master_reset (tape_state_t *t) {

    int e;
    
    e = TAPE_E_OK;
    
    if (NULL == t) {
      log_warn("BUG: tape_handle_acia_master_reset(NULL)\n");
      return TAPE_E_BUG;
    }

/*log_warn("tape_handle_acia_master_reset: phase is %u\n", t->w_uef_serial_phase);*/
    
    /* if reset occurs during a frame */
    if ((t->filetype_bits & TAPE_FILETYPE_BITS_UEF) && (t->w_uef_serial_phase != 0)) {
        /* if UEF, finish the frame */
        /* phase=1 : start bit sent
         * phase=2 : bit 0 sent
         * ...
         * phase=9 : bit 7 sent
         */
        /* TOHv3.2: actually push partial frames out to the UEF properly */
        e = tape_uef_flush_incomplete_frame (&(t->uef),
                                             t->tallied_1200ths,
                                             &(t->w_uef_serial_phase),
                                             &(t->w_uef_serial_frame),
                                             &(t->w_uef_tmpchunk));
    }
    
    return e;
       
}

/* for tape catalogue */
   
/* WARNING: on error, caller is expected to clean up "out" */
int tape_state_clone_and_rewind (tape_state_t *out, tape_state_t *in) {

    int e;
    
    e = TAPE_E_OK;
    
    memcpy(out, in, sizeof(tape_state_t));
    
    /* wipe all of the structures on the target, and any pointers
       they might have duplicated */
    memset(&(out->w_uef_tmpchunk), 0, sizeof(uef_chunk_t));
    memset(&(out->tibet),          0, sizeof(tibet_t)    );
    memset(&(out->uef),            0, sizeof(uef_state_t));
    memset(&(out->csw),            0, sizeof(csw_state_t));

    /* TOHv4.1: had horrible situation where this didn't get cloned,
     *          and so when findfilenames_new() did its thing, it would
     *          free the tibet_data_pending span on the live tape_state_t
     *          object instead. Erk. */
    e = tibet_clone_span (&(out->tibet_data_pending), &(in->tibet_data_pending));
    
    /* TOHv3: modified for simultaneous file types */
    if ((TAPE_FILETYPE_BITS_TIBET & in->filetype_bits) && (in->tibet.priv != NULL)) {
        e = tibet_clone (&(out->tibet), &(in->tibet));
        if (TAPE_E_OK == e) {
            e = tibet_rewind(&(out->tibet));
        }
    }
    if ((TAPE_E_OK == e) && (TAPE_FILETYPE_BITS_CSW & in->filetype_bits)) {
        e = csw_clone (&(out->csw), &(in->csw));
        if (TAPE_E_OK == e) {
            csw_rewind (&(out->csw));
        }
    }
    if ((TAPE_E_OK == e) && (TAPE_FILETYPE_BITS_UEF & in->filetype_bits)) {
        e = uef_clone (&(out->uef), &(in->uef));
        if (TAPE_E_OK == e) {
            uef_rewind (&(out->uef));
        }
    }
    
    if (TAPE_E_OK != e) {
        log_warn("tape: clone-and-rewind: error, code %u\n", e);
        /* caller must handle error, e.g. calling tape_state_finish() */
    }
    
    return e;
    
}



int findfilenames_new (tape_state_t * const ts,
                       bool const show_ui_window,
                       bool const filter_phantoms) {

    int e;
    int32_t n;
    char fn[11];
    char s[256];
    uint32_t load, exec;
    uint32_t file_len;
    tape_state_t tclone;
    ACIA acia_tmp;
#ifdef BUILD_TAPE_CAT_TIMESTAMPS
    int32_t last_time_1200ths;
    int32_t file_start_time_1200ths;
#endif
    
    load = 0;
    exec = 0;
    file_len = 0;
#ifdef BUILD_TAPE_CAT_TIMESTAMPS
    last_time_1200ths = -1;
    file_start_time_1200ths = -1;
#endif
    
    /* get a clone of the live tape state,
       so we don't interfere with any ongoing cassette operation: */
    e = tape_state_clone_and_rewind (&tclone, ts);
    
    /* and make a suitable ACIA */
    memset(&acia_tmp, 0, sizeof(ACIA));
    acia_init(&acia_tmp);
    acia_tmp.control_reg = 0x14; // 8N1
    
    while ( TAPE_E_OK == e)  {
    
        int32_t limit;
        uint16_t blknum;
        uint16_t blklen;
        uint8_t final;
        uint8_t empty;
    
        memset(fn, 0, 11);
        
        limit = 29; /* initial byte limit, will be updated once block len is known */
        
        blknum = 0;
        blklen = 0;
        final = 0;
        empty = 0;
        load = 0;
        exec = 0;
        memset (s, 0, 256);
        
        /* process one block: */
        for (n=0; (n < limit) && (TAPE_E_OK == e); n++) {
        
            uint8_t k;
            char cat_1200th;
            uint8_t value;
            int32_t time_1200ths_or_minus_one; /* TOHv4.3 */
#ifdef BUILD_TAPE_CAT_TIMESTAMPS
            int hrs, mins, secs;
#endif
            
            /* assume 8N1/1200.
             * 
             * MOS always uses 8N1; there is no concept of
             * a "filename" without 8N1. However, the baud rate
             * could be 300 rather than 1200. A job for another day
             * will be to try both baud rates, and
             * pick the one that produces the more meaningful outcome,
             * although this gets tougher for mixed-baud tapes.
             * 
             * For UEF chunks &100, &102 and &104 we could bypass
             * tape_read_poll_...() and rifle through the UEF
             * file itself; this would be baud-agnostic. It wouldn't
             * help with chunk &114, nor with CSW or TIBET, though. */

            e = tape_1200th_from_back_end (false, /* don't strip silence and leader */
                                           &tclone,
                                           acia_rx_awaiting_start(&acia_tmp), /* awaiting start bit? */
                                           filter_phantoms,   /* enable */
                                           &cat_1200th,
                                           &time_1200ths_or_minus_one);
            if (TAPE_E_OK != e) { break; }

#ifdef BUILD_TAPE_CAT_TIMESTAMPS
            if (time_1200ths_or_minus_one > -1) {
                last_time_1200ths = time_1200ths_or_minus_one;
            } else {
                last_time_1200ths++; /* no updated time from back-end, so just increment */
            }
#endif

            /* we don't need tclone.tallied_1200ths, so don't call update_playback_elapsed on it */

            /* just assume 1200 baud for now, then
               (i.e. 1/1200th = 1 ACIA RX bit): */
            e = acia_receive_bit_code (&acia_tmp, cat_1200th);

            if (TAPE_E_OK != e) { break; }
            
            if ( ! acia_rx_frame_ready(&acia_tmp) ) {
                n--;
                continue; /* frame not ready, go around again */
            }
            
            /* frame ready */
            
            value = acia_read(&acia_tmp, 0); // status reg
            value = acia_read(&acia_tmp, 1); // data reg

            if (0 == n) { /* 0 */
                /* need sync byte */
                if ('*' != value) {
                    n--; /* try again */
                }
            } else if (n<12) { /* 1-11 */
                if ((11==n) && (value!=0)) {
                    /* no NULL terminator found; abort, resync */
                    n=-1;
                } else {
                    fn[n-1] = (char) value;
                    /* handle terminator */
                    if (0 == value) {
                        memset(fn + n, 0, 11 - n);
                        n = 11; /* filename complete */
                    /* sanitise non-printable characters */
                    } else if ( ! isprint(value) ) {
                        fn[n-1] = '?';
                    }
                }
            } else if (n<16) { /* 12-15 */
                load = (load >> 8) & 0xffffff;
                load |= (((uint32_t) value) << 24);
            } else if (n<20) { /* 16-19 */
                exec = (exec >> 8) & 0xffffff;
                exec |= (((uint32_t) value) << 24);
            } else if (n<22) { /* 20-21 */
                blknum = (blknum >> 8) & 0xff;
                blknum |= (((uint16_t) value) << 8);
#ifdef BUILD_TAPE_CAT_TIMESTAMPS
                if (0 == blknum) {
                    /* record file start time */
                    file_start_time_1200ths = last_time_1200ths;
                }
#endif
            } else if (n<24) { /* 22-23 */
                blklen = (blklen >> 8) & 0xff;
                blklen |= (((uint16_t) value) << 8);
            } else if (n<25) { /* 24 */
                if (blklen < 0x100) {
                    final = 1;
                }
                if (0x80 & value) {
                    /* 'F' flag */
                    final = 1;
                }
                if (0x40 & value) {
                    /* 'E' flag */
                    empty = 1;
                }
            } else if (n<29) { /* 25-28 */
#ifdef BUILD_TAPE_CAT_NEED_NEXT_ADDR_ZERO
                /* As a sanity check, make sure that the next file address bytes
                   are all zero. If they're not, we'll skip this, because it's
                   likely not actually a file. Some protection schemes will
                   obviously break this.
                   (Ideally we'd check the header CRC instead) */
                if (value != 0) {
                    file_len = 0;
                    break; /* abort */
                }
#endif
                if (28 == n) {
                    /* add block length to file total */
                    file_len += (uint32_t) blklen;
                    if (final) {
                        for (k=0; k < 10; k++) {
                            /* for compatibility with what CSW does */
                            if (fn[k] == '\0') { fn[k] = ' '; }
                        }
#ifdef BUILD_TAPE_CAT_TIMESTAMPS
                        to_hours_minutes_seconds (file_start_time_1200ths, &hrs, &mins, &secs);
                        sprintf(s, "%d:%02d:%02d %s Size %04X Load %08X Run %08X", hrs, mins, secs, fn, file_len, load, exec);
#else
                        sprintf(s, "%s Size %04X Load %08X Run %08X", fn, file_len, load, exec);
#endif
                        /* TOHv3: For tape testing, don't bother to spawn the UI window.
                           It can lead to a race condition when shutdown happens
                           immediately on end-of-tape. Besides, the user will never
                           see it. */
                        if (show_ui_window) {
                            cataddname(s);
                        } else {
                            /* TOHv3: for -tapetest: print to stdout;
                               wanted to use stderr, but difficulties
                               capturing output from it under Windows? */
#ifdef BUILD_TAPE_CAT_TIMESTAMPS
                            fprintf(stdout, "tapetest:%d:%02d:%02d %s %04x %08x %08x\n", hrs, mins, secs, fn, file_len, load, exec);
#else
                            fprintf(stdout, "tapetest:%s %04x %08x %08x\n", fn, file_len, load, exec);
#endif
                        }
                        file_len = 0;
                        memset(fn, 0, 11);
                    }
                    /* skip remainder of block: H.CRC2 + data + D.CRC2 (but not if file empty) */
                    limit += 2;
                    if ( ! empty ) {
                        limit += (blklen + 2);
                    }
                }
            } else {
                /* actual block data here */
                /* ... skip byte ... */
            }
            
        } /* next byte in block */
        
    } /* next block */
    
    /* artificially force b-em shutdown, if TAPE_TEST_QUIT_ON_EOF;
       this is for automated testing via -tapetest on CL */
    /* TOHv3.2: errors now take precedence over EOF condition */
    tape_handle_exception (&tclone,
                           NULL,
                           e,
                           tape_vars.testing_mode & TAPE_TEST_QUIT_ON_EOF,
                           false,
                           false); /* don't alter menus */
    /* free clone */
    tape_state_finish(&tclone, 0); /* 0 = DO NOT alter menus since this is a clone */
    
    return e;
    
}

void to_hours_minutes_seconds (int32_t   const elapsed_1200ths,
                               int32_t * const h_out,
                               int32_t * const m_out,
                               int32_t * const s_out) {
    int32_t secs;
    secs = (int32_t) (elapsed_1200ths * TAPE_1200TH_IN_S_FLT);
    *s_out = secs   % 60;
    *m_out = secs   / 60;
    *h_out = *m_out / 60;
    *m_out %= 60;
}


void tape_handle_exception (tape_state_t * const ts,
                            tape_vars_t  * const tv, /* TOHv3.2: may be NULL */
                            int  const error_code,
                            bool const eof_fatal,
                            bool const err_fatal,
                            bool const not_tapecat_mode) { /* false if called from findfilenames_new */

    int sx;
    bool tape_broken;
    bool tcw_opened;

    if (TAPE_E_OK == error_code) { return; }

    /* if (ts->disabled_due_to_error && not_tapecat_mode) { */
    if ((TAPE_E_OK != ts->prior_exception) && not_tapecat_mode) {
        log_warn("tape: BUG: handling exception when tape is already disabled!");
        return;
    }

    sx = SHUTDOWN_OK;
    tcw_opened = false;

    if (TAPE_E_EXPIRY == error_code) {
        sx = SHUTDOWN_EXPIRED; /* TOHv4-rc1 */
    /* TOHv4.1: distinct file-not-found code */
    } else if (TAPE_E_FOPEN == error_code) {
        /* TOHv4.1 */
        sx = SHUTDOWN_FOPEN;
        log_warn("tape: tape load failure");
    } else if (TAPE_E_EOF == error_code) {
        sx = SHUTDOWN_TAPE_EOF; /* TOHv4.1 */
    } else if (error_code != TAPE_E_OK) { /* generic error */
        log_warn("tape: code %d; disabling tape! (Eject tape to clear.)", error_code);
        sx = SHUTDOWN_TAPE_ERROR;
    }

    /* TOHv4.1: rework */
    tape_broken = (SHUTDOWN_OK != sx) && (SHUTDOWN_TAPE_EOF != sx);

#ifdef BUILD_TAPE_TAPECTRL
    if (NULL != tv) { /* tv will be null if we're doing tape cat */
        tcw_opened = tv->tapectrl_opened;
    }
#endif

    if ( tape_broken ) {
        /* errors which are fatal to the tape system */
        tape_state_finish(ts, not_tapecat_mode); /*  1 = alter menus  */
        if (tv != NULL) {
            tape_set_record_activated(ts, tv, NULL, false, tcw_opened); /* ignore errors */
            gui_set_record_mode(false);
#ifdef BUILD_TAPE_TAPECTRL
            if (tcw_opened) {
                tapectrl_set_record(&(tv->tapectrl), false, 0);
            }
#endif
        }
        ts->prior_exception = error_code;
#ifdef BUILD_TAPE_TAPECTRL
        if (tcw_opened) {
            tapectrl_to_gui_msg_error (&(tv->tapectrl), true, true, error_code);
        }
#endif
    }

    if (    err_fatal
         && ( (SHUTDOWN_TAPE_ERROR == sx) || (SHUTDOWN_FOPEN == sx) ) ) {
        quitting = true; /* (global) */
        set_shutdown_exit_code(sx);
    } else if (eof_fatal && (SHUTDOWN_TAPE_EOF == sx)) {
        quitting = true; /* (global) */
        set_shutdown_exit_code(sx);
    } else if (SHUTDOWN_EXPIRED == sx) { /* ALWAYS fatal */
        quitting = true; /* (global) */
        set_shutdown_exit_code(sx);
    }

    if (error_code != TAPE_E_EOF) {
        ts->prior_exception = error_code; /* TOHv4.3: keep this so it can be sent to tapectrl */
    }

}



int tape_state_init_alter_menus (tape_state_t * const ts, /* not const, because of tibet priv horribleness */
                                  tape_vars_t * const tv) {

    int32_t pos_1200ths;
    bool rec;
    int e;

    e = TAPE_E_OK;
    pos_1200ths = 0;
    
    rec = tape_is_record_activated(tv);

#ifdef BUILD_TAPE_TAPECTRL
    if (rec) {
        tape_get_duration_1200ths(ts, &pos_1200ths);
    }
    if (tv->tapectrl_opened) {
        /* tapectrl_set_record() will eventually send a TIME message
         * to the tapectrl. Before this occurs, we need to make sure
         * that the inlay scans are nullified, so that the duration
         * isn't suddenly zero but with an active stripe set. */
        e = tapectrl_to_gui_msg_stripes(&(tv->tapectrl), true, true, NULL);
        tapectrl_set_record(&(tv->tapectrl), rec, pos_1200ths);
    }
#endif
    gui_alter_tape_menus(ts->filetype_bits);
    gui_set_record_mode(rec);
    return e;
}


/* tv may be NULL: */
void tape_state_init (tape_state_t *t,
                      tape_vars_t *tv,
                      uint8_t filetype_bits,
                      bool alter_menus) {
                      
    /* TOHv3.2: we don't touch Record Mode any more. If -record is
       specified on the command line along with -tape (for auto-appending),
       we don't want tape_state_init() or tape_state_finish() to cancel it. */
    
    tape_state_finish(t, alter_menus); /* alter menus (grey-out Eject Tape, etc.) */
    t->filetype_bits = filetype_bits;
    
    if (alter_menus) {
        tape_state_init_alter_menus(t, tv);
    }
    t->w_uef_prevailing_baud = 1200; /* TOHv4 */
    
    /* TOHv4 */
    /* This is a hack to make sure that the initial cached
       values for dividers and thresholds that are stored on tape_state
       are set to sane values, before MOS makes the first write to
       the serial ULA's control register. */
    serial_recompute_dividers_and_thresholds (tv->overclock,
                                              0x64, //tape_state.ula_ctrl_reg,
                                              &(t->ula_rx_thresh_ns),
                                              &(t->ula_tx_thresh_ns),
                                              &(t->ula_rx_divider),
                                              &(t->ula_tx_divider));

}


void tape_ejected_by_user (tape_state_t * const ts,
                           tape_vars_t * const tv,
                           ACIA * const acia) {

    bool tcw_opened;

    tcw_opened = false;
#ifdef BUILD_TAPE_TAPECTRL
    tcw_opened = tv->tapectrl_opened;
#endif

    /* cancel record mode */
    tape_set_record_activated(ts, tv, acia, 0, tcw_opened);
    gui_set_record_mode(false);

#ifdef BUILD_TAPE_TAPECTRL
    if (tcw_opened) {
        /* FIXME: there is some needless locking and unlocking in this sequence */
        tapectrl_set_record(&(tv->tapectrl), false, false);
        tapectrl_eject(&(tv->tapectrl));                 /* TOHv4.3 */
        tape_interval_list_finish(&(tv->interval_list)); /* TOHv4.3 */
    }
#endif

    tape_state_finish(ts, true); /* true = update menus */

}


void tape_state_finish (tape_state_t *t, bool alter_menus) {

    /* TOHv3: updated for simultaneous output formats */
    if (TAPE_FILETYPE_BITS_UEF & t->filetype_bits) {
        uef_finish(&(t->uef));
    }
    if (TAPE_FILETYPE_BITS_CSW & t->filetype_bits) {
        csw_finish(&(t->csw));
    }
    if (TAPE_FILETYPE_BITS_TIBET & t->filetype_bits) {
        tibet_finish(&(t->tibet));
    }
    if (t->w_uef_tmpchunk.data != NULL) {
        free(t->w_uef_tmpchunk.data);
    }

    /* TOHv4-rc2: no longer just zero out the whole state;
     *            turns out that is hoodlum behaviour. Be more selective */

    /*memset(t, 0, sizeof(tape_state_t));*/
    if (t->tibet_data_pending.tones != NULL) {
        free(t->tibet_data_pending.tones);
    }
    memset(&(t->tibet_data_pending), 0, sizeof(tibet_span_t));
    t->w_must_end_data = 0;

    if (alter_menus) {
        /* set displayed Eject path to blank: */
        gui_alter_tape_eject(NULL);
        /* simultaneously show all file types for saving since all
         * are available on a blank tape: */
        gui_alter_tape_menus(TAPE_FILETYPES_ALL);
    }
    t->filetype_bits = TAPE_FILETYPES_ALL;
    t->w_accumulate_silence_ns     = 0;
    t->w_accumulate_leader_ns      = 0;
    memset(&(t->w_uef_tmpchunk), 0, sizeof(uef_chunk_t));
    t->w_uef_origin_written        = 0;
    t->w_uef_prevailing_baud       = 1200;
    t->w_uef_serial_frame          = 0;
    t->w_uef_serial_phase          = 0;
    t->w_uef_enc100_shift_value    = 0;
    t->w_uef_enc100_shift_amount   = 0;
    t->w_uef_was_loaded            = 0;
    t->tones300_fill               = 0;
    t->tallied_1200ths             = 0;         /* TOHv4.3    */
    t->ula_prevailing_rx_bit_value = 'S';
    t->prior_exception             = TAPE_E_OK; /* TOHv4.3-a3 */

}

bool tape_is_record_activated (const tape_vars_t * const tv) {
    return tv->record_activated;
}

int tape_set_record_activated (tape_state_t * const t,
                               tape_vars_t  * const tv,
                               ACIA         * const acia_or_null,
                               bool           const value,
                               bool           const tapectrl_opened) {

    int32_t baud;
    int e;
    /* TOHv4.3 */
    tape_ctrl_window_t *tcw;
    bool tcw_opened;
    int32_t dur;

    e = TAPE_E_OK;

/*printf("\n\ntape_set_record_activated: %x\n", value);*/

    if (NULL == tv) {
        log_warn("tape: BUG: tape_set_record_activated passed NULL tape_vars_t\n");
        return TAPE_E_BUG;
    } else if (NULL == t) {
        log_warn("tape: BUG: tape_set_record_activated passed NULL tape_state_t\n");
        return TAPE_E_BUG;
    }

    tcw = NULL;
    tcw_opened = false;
#ifdef BUILD_TAPE_TAPECTRL
    tcw = &(tv->tapectrl);
    tcw_opened = tv->tapectrl_opened;
#endif

    if ( (false == value) && (false != tv->record_activated) ) {
        /* recording finished; flush any pending data */
        e = tape_flush_pending_piece (t,
                                      acia_or_null,
                                      tv->save_prefer_112,
                                      true); /* populate timestamp information */
        if (TAPE_E_OK != e) { return e; }
        e = tape_rewind_2(t, tcw, false, tcw_opened);
    } else {
        /* work out current baud status when record is activated,
         * so we know whether we need a chunk &117 for the first data
         * chunk to be appended; set value on tape_state_t */
        baud = 1200;
        if ( (t->filetype_bits & TAPE_FILETYPE_BITS_UEF) && (t->uef.num_chunks>0) ) {
            uef_scan_backwards_for_chunk_117 (&(t->uef), t->uef.num_chunks-1, &baud);
            if (0 == baud) { baud = 1200; }
        }
        t->w_uef_prevailing_baud = baud;
    }

    tv->record_activated = value;

    /* TOHv4.3-a2 */
    dur = 0;

    // if ( ! t->disabled_due_to_error ) {
    if ( TAPE_E_OK == t->prior_exception ) {
        e = tape_get_duration_1200ths(t, &dur);
        if (TAPE_E_OK != e) { return e; }
    }

#ifdef BUILD_TAPE_TAPECTRL
    /* TOHv4.3-a2: bugfix
     * TOHv4.3-a3: getting a lot of test crashes here, had to add the mutex check too */
    if (tapectrl_opened && (tcw->mutex != NULL)) {
        tapectrl_set_gui_rapid_value_time (tcw, false, true, t->tallied_1200ths); //, dur);
    }
#endif

    /* TOHv4.3: set up initial value of tallied_1200ths from prior duration */
    /* this is needed for (rec enabled -> *tape -> *cat) correct functioning */
    if (value) {
        t->tallied_1200ths = dur;
    } else {
        t->tallied_1200ths = 0;
        /* Also, this is the FINALO PIECE. */
#ifdef BUILD_TAPE_TAPECTRL
/* printf("FINALO PIECE: tv->interval_list.decstate.wip.type = %u\n", tv->interval_list.decstate.wip.type); */
        if (tv->interval_list.decstate.wip.type != TAPE_INTERVAL_TYPE_PENDING) { /* make sure the WIP is real */

/*printf("tape_set_record_activated(): appending interval wip (%d + %d), interval type \"%s\"\n",
       tv->interval_list.decstate.wip.start_1200ths,
       tv->interval_list.decstate.wip.pos_1200ths,
       tape_interval_name_from_type(tv->interval_list.decstate.wip.type));*/

            e = tape_interval_list_append (&(tv->interval_list),
                                            &(tv->interval_list.decstate.wip),
                                            true); /* derive wip start time from previous list entry */
            if (TAPE_E_OK != e) { return e; }
        }
        memset(&(tv->interval_list.decstate.wip), 0, sizeof(tape_interval_t));
        /* bugfix: forgot to send the updated intervals list over to the tapectrl window. */
        if (tcw_opened) {
            e = tapectrl_to_gui_msg_stripes (tcw, true, true, &(tv->interval_list));
        }
        if (TAPE_E_OK != e) { return e; }

#ifdef BUILD_TAPE_SANITY
        /* various timestamp integrity checks ... */
        e = tape_interval_list_integrity_check(&tv->interval_list);
        if (TAPE_E_OK != e) { return e; }
        if (t->filetype_bits & TAPE_FILETYPE_BITS_UEF) {
            e = uef_verify_timestamps(&(t->uef));
        }
        if (TAPE_E_OK != e) { return e; }
#endif


#endif
    }

// tape_interval_list_t *ivl;
// tibet_priv_t *priv;
// tibet_span_t *span;
// tape_interval_t *iv;


// ivl = &(tv->interval_list);
// if (ivl->fill>0) {
//     iv = ivl->list + ivl->fill - 1;
//     priv = (tibet_priv_t *) t->tibet.priv;
//     span = priv->dd_new_spans + priv->dd_new_num_spans - 1;
//     printf("durations: per ivl (%d + %d = %d), per timestamp (%d + %d = %d)\n",
//             iv->start_1200ths,
//             iv->pos_1200ths,
//             iv->start_1200ths + iv->pos_1200ths,
//             span->timespan.start_1200ths,
//             span->timespan.pos_1200ths,
//             span->timespan.start_1200ths + span->timespan.pos_1200ths);
// }

    return e;

}



int tape_load_successful (tape_state_t * const ts,
                          tape_vars_t * const tv,
                          const char * const path) {
    
    int32_t total_1200ths; /* TOHv4.3 */
    int e;
    tape_ctrl_window_t *tcw;
    bool tcw_opened;
    tape_interval_list_t *iv_list;
#ifdef BUILD_TAPE_TAPECTRL
    tape_ctrl_msg_from_gui_t from_gui;
#endif

    /* TOHv4.3: check removed: record mode on load is a valid configuration if -tape and -record provided on CL */
    /*
    if (tv->record_activated) {
        log_warn("tape: load_successful: BUG: record mode is activated");
        return TAPE_E_BUG;
    }*/

    /* tape_load_ui() in gui-allegro.c should already have called
     * tape_state_finish() which will reset this itself, but it is
     * done again here anyway */
    ts->prior_exception = TAPE_E_OK;
    
    gui_alter_tape_eject (path);
    gui_alter_tape_menus(ts->filetype_bits);
    
    e = TAPE_E_OK;

#ifdef BUILD_TAPE_TAPECTRL
    tcw = &(tv->tapectrl);
    tcw_opened = tv->tapectrl_opened;
    iv_list = &(tv->interval_list);
#else
    tcw = NULL;
    tcw_opened = false;
    iv_list = NULL;
#endif

    /* TOHv4.3: now have to build the time map for the back-ends,
       which entails reading the entire tape on load :( */

    /* scan the tape */
    e = tapeseek_run_initial_scan (ts, iv_list);

    if (TAPE_E_OK != e) {
        log_warn("tape: ERROR: initial scan failed\n");
        tape_handle_exception (ts,
                               &tape_vars,
                               e,
                               tape_vars.testing_mode & TAPE_TEST_QUIT_ON_EOF,
                               tape_vars.testing_mode & TAPE_TEST_QUIT_ON_ERR,
                               true); /* alter menus */
        return TAPE_E_OK; /* exception handled */
    }
    if (TAPE_E_OK != e) { return e; }
    total_1200ths = 0;
    e = tape_get_duration_1200ths (ts, &total_1200ths);
    if (total_1200ths > 0) {
        log_info("Tape scan complete (%.1lf minutes, %d intervals); rewinding ...",
                 total_1200ths / (60.0 * TAPE_1200_HZ),
#ifdef BUILD_TAPE_TAPECTRL
                 tv->interval_list.fill);
#else
                 0);
#endif
    } else {
        log_warn("We ain't brought no hose.");
    }

    tape_rewind_2(ts, tcw, tv->record_activated, tcw_opened);

#ifdef BUILD_TAPE_TAPECTRL
    /* hack: self-insert a THREAD_STARTED message into the from_gui queue.
     * This will send a full update sequence to the tapectrl. */
    if (tcw_opened) {
        from_gui.type = TAPECTRL_FROM_GUI_THREAD_STARTED;
        from_gui.ready = true;
        TAPECTRL_LOCK_MUTEX(tcw->mutex);
        if (tcw->display != NULL) {
            tapectrl_queue_from_gui_msg (tcw, &from_gui);
        }
        TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
    }
#endif

    /* trap error or EOF */

    return e;
    
}




/* TOHv3 */
void tape_write_u32 (uint8_t b[4], uint32_t v) {
    b[0] = v & 0xff;
    b[1] = (v >> 8) & 0xff;
    b[2] = (v >> 16) & 0xff;
    b[3] = (v >> 24) & 0xff;
}

/* TOHv3 */
void tape_write_u16 (uint8_t b[2], uint16_t v) {
    b[0] = v & 0xff;
    b[1] = (v >> 8) & 0xff;
}

uint32_t tape_read_u32 (uint8_t *in) {
    uint32_t u;
    u =   (  (uint32_t)in[0])
        | ((((uint32_t)in[1]) << 8) & 0xff00)
        | ((((uint32_t)in[2]) << 16) & 0xff0000)
        | ((((uint32_t)in[3]) << 24) & 0xff000000);
    return u;
}

uint32_t tape_read_u24 (uint8_t *in) {
    uint32_t u;
    u =   (  (uint32_t)in[0])
        | ((((uint32_t)in[1]) << 8) & 0xff00)
        | ((((uint32_t)in[2]) << 16) & 0xff0000);
    return u;
}

uint16_t tape_read_u16 (const uint8_t * const in) {
    uint16_t u;
    u =   (  (uint16_t)in[0])
        | ((((uint16_t)in[1]) << 8) & 0xff00);
    return u;
}


/* TOHv3 */
void tape_init (tape_state_t *ts, tape_vars_t *tv) {
    tv->record_activated         = 0;
    // tapelcount                   = 0;
    tapeledcount                 = 0;
    tv->overclock                = false;
    tv->save_filename            = NULL;
    tv->load_filename            = NULL;
    tv->strip_silence_and_leader = 0;
#ifdef BUILD_TAPE_TAPECTRL
#ifdef BUILD_TAPE_TAPECTRL_DOUBLE_SIZE
    tv->tapectrl_ui_scale        = 2.0f;
#else
    tv->tapectrl_ui_scale        = 1.0f;
#endif
#endif
}



/* TOHv3.2: added explicit tape_start_motor(), tape_stop_motor(), tape_is_motor_running()
 * TOHv3.3: moved some stuff here from serial.c */
int tape_start_motor (tape_state_t * const ts,
                      tape_ctrl_window_t * const tcw,
                      bool const also_force_dcd_high,
                      bool const tapectrl_opened) {

    if (ts->ula_motor) { return TAPE_E_OK; }

    ts->ula_motor = 1;
    tapenoise_motorchange(1);
    led_update(LED_CASSETTE_MOTOR, 1, 0);
    if (also_force_dcd_high) {
        acia_dcdhigh(&sysacia);
    }
    /* TOHv4.2: reset this to ensure another ~0.6s
       of unskipped silence/leader plays out when the motor
       starts up. */
    ts->strip_silence_and_leader_holdoff_1200ths=0;

    /* TOHv4.3 */
#ifdef BUILD_TAPE_TAPECTRL
    if ( tapectrl_opened ) {
        tapectrl_to_gui_msg_motor(tcw, true, true, true);
    }
#endif

    return TAPE_E_OK;

}



int tape_stop_motor (tape_state_t * const ts,
                     tape_ctrl_window_t * const tcw,
                     /*bool const silence112,*/
                     bool const record_activated,
                     bool const tapectrl_opened) {

    int e;

    e = TAPE_E_OK;

    if ( ! ts->ula_motor ) { return TAPE_E_OK; } /* not running */
    ts->ula_motor = false;
    tapeledcount = 2; /* (formerly from serial.c) */
    tapenoise_motorchange(0);
    /* TOHv4.2: if motor is off, DCD counter is reset. See
     * https://www.stardot.org.uk/forums/viewtopic.php?p=456479#p456479
     * and
     * https://www.stardot.org.uk/forums/viewtopic.php?p=457271#p457271
     */
    ts->ula_dcd_blipticks = 0;

    /* TOHv4.3-a3: There used to be a call to tape_flush_pending_piece() here,
     * so that MOTOR OFF would flush data to the TX back-end (UEF, TIBET, CSW).
     * This is no longer done, meaning you can now accumulate several sessions
     * of e.g. silence into one single UEF chunk or TIBET span. Flushing only
     * occurs when REC mode is deactivated by the user. */

#ifdef BUILD_TAPE_TAPECTRL
    if (tapectrl_opened) {
        if (TAPE_E_OK != e) { return e; }
        e = tapectrl_set_gui_rapid_value_signal(tcw, true, false, 'S');
        if (TAPE_E_OK != e) { return e; }
        /* TOHv4.3: was having problems sometimes with motor off
         * from MOS extinguishing the "record mode" lamp. Don't
         * know why but maybe this will fix it. */
        tapectrl_to_gui_msg_record(tcw, false, false, record_activated);
        /* TOHv4.3 */
        tapectrl_to_gui_msg_motor(tcw, false, true, false);
    }
#endif
    return e;
}

/* This stays on tape.c rather than taperead or tapewrite,
 * because it deals with both record and playback situations. */

/* NOTE that this is called if record mode is enabled, as well as on playback! */
/* (tape is guaranteed to be rolling) */

/* FIXME: this sends about three different msg types to the GUI
 * and locks the mutex independently each time. See if it's possible to amalgamate
 * these into a single mutex session. */
int tape_rs423_eat_1200th (tape_state_t * const ts,
                           tape_vars_t  * const tv,
                           ACIA         * const acia,
                           bool           const record_activated, /* TOHv4.3 */
                           bool           const emit_tapenoise,
                           bool         * const throw_eof_out) {

    int e;
    char tone;
    int32_t total_1200ths; /* TOHv4.3 */
    int32_t elapsed_or_minus_one;
    tape_ctrl_window_t *tcw;
    bool tcw_opened;
    tape_interval_list_t *iv_list;
    uint32_t *since_last_tone_p;

    /* FIXME: should this not be a bug? */
    /* if ( ts->disabled_due_to_error ) { return TAPE_E_OK; } */
    if ( TAPE_E_OK != ts->prior_exception ) { return TAPE_E_OK; }

    /* consume tape 1200th */
    tone = '\0';

    /* TOHv4.3 */
    e = tape_get_duration_1200ths(ts, &total_1200ths);
    if (TAPE_E_OK != e) { return e; }

    elapsed_or_minus_one = -1;

    /* continue to advance through the tape even if RS423 selected */
    if ( ! record_activated ) {
        e = tape_1200th_from_back_end (false, ts, false, false, &tone, &elapsed_or_minus_one);
        if (TAPE_E_EOF == e) {
            *throw_eof_out = true;
            e = TAPE_E_OK; /* trap EOF */
        }
        if (TAPE_E_OK != e) { return e; }
    }

#ifdef BUILD_TAPE_TAPECTRL
    if ( record_activated ) {
        /* record mode; show tallied time */
        total_1200ths = ts->tallied_1200ths;
    } else if ( ! *throw_eof_out ) { /* don't update elapsed time if tape has ended */
        /* playback mode */
        /* increments ts->tallied_1200ths, if passed -1 */
        e = tape_update_playback_elapsed(ts, elapsed_or_minus_one);
    }
    if (TAPE_E_OK != e) { return e; }
#endif

    if ('\0' != tone) { ts->ula_prevailing_rx_bit_value = tone; } /* for DCD */

    /* TOHv4.3: add crude leader detection for RS423 side;
     * needed for tapectrl lamps, and also to fix tapenoise warble */
    if ('1' == tone) {
        if (ts->rs423_leader_detect > TAPE_CRUDE_LEADER_DETECT_1200THS /* =100 */) {
            tone = 'L';
        } else {
            (ts->rs423_leader_detect)++;
        }
    } else {
        ts->rs423_leader_detect = 0;
    }

    /* TOHv4.3: don't play RS423's RX audio when record mode is activated */
    if ( ! record_activated ) {
#ifdef BUILD_TAPE_TAPECTRL
        e = send_tone_to_tapectrl_maybe (&(tv->tapectrl),
                                         ts->tallied_1200ths,
                                         total_1200ths,
                                         tone,
                                         &(tv->since_last_tone_sent_to_gui));
        if (TAPE_E_OK != e) { return e; }
#endif
        /* TOHv4.1-rc9: bugfix: tape noise wasn't being sent in RS423+MOTOR1 case.
         * This caused problems with Ultron, because the tapenoise was being
         * played back out of the ringbuffer but it wasn't being replenished during
         * block "motor off" breaks, so eventually you would get drop-outs. */
        tapenoise_send_1200 (tone, &(ts->tapenoise_no_emsgs));
    }

    tcw = NULL;
    iv_list = NULL;
    tcw_opened = false;
    since_last_tone_p = NULL;
#ifdef BUILD_TAPE_TAPECTRL
    tcw = &(tv->tapectrl);
    iv_list = &(tv->interval_list);
    tcw_opened = tv->tapectrl_opened;
    since_last_tone_p = &(tv->since_last_tone_sent_to_gui);
#endif

    /* we also need to send s/1200 of silence to the TX back end */
    e = tape_write_bitclk (ts,
                           tcw,
                           iv_list,
                           acia,
                           'S',
                           TAPE_1200TH_IN_NS_INT,
                           emit_tapenoise && record_activated,
                           record_activated,
                           tv->save_always_117,
                           tv->save_prefer_112,
                           tv->save_do_not_generate_origin_on_append,
                           tcw_opened,
                           since_last_tone_p);
    if (TAPE_E_OK != e) { return e; }

    if (    (total_1200ths>=0)
         && (ts->tallied_1200ths > total_1200ths)
         && ! record_activated) {
        log_warn("tape: rs423_eat_1200th: BUG: tallied_1200ths (%d) > total_1200ths (%d)",
                 ts->tallied_1200ths, total_1200ths);
        return TAPE_E_BUG;
    }

#ifdef BUILD_TAPE_TAPECTRL
    if (tcw_opened) {
        /* this might lock and unlock the mutex */
        if ((TAPE_E_OK == e) && (*throw_eof_out != tv->previous_eof_value)) {
/*printf("tapectrl_to_gui_msg_eof(%u): %s:%s(%d)\n", true, __FILE__, __func__, __LINE__);*/
            tapectrl_to_gui_msg_eof(&(tv->tapectrl), true, true, true);
        }
    }
    tv->previous_eof_value = *throw_eof_out;
#endif

    return e;

}
