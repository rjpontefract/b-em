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

#include "taperead.h"

/* 'elapsed' value comes from the initial scan data */
int tape_update_playback_elapsed (tape_state_t * const ts, int32_t const elapsed_or_minus_one) {
    /* for PLAYBACK, not RECORDING */
    if (elapsed_or_minus_one != -1) {
        /* Don't use the predicted, new value. Stick with the old one. */
        ts->tallied_1200ths = elapsed_or_minus_one;
    } else {
        /* -1 means we don't have an exact value from the tape back end, so
         * we update the tallied value */
        (ts->tallied_1200ths)++;
    }
    return TAPE_E_OK;
}

#include "tapenoise.h"


int tape_fire_acia_rxc (tape_state_t * const ts,
                         tape_vars_t * const tv,
                                ACIA * const acia,
                             int32_t   const acia_rx_divider,
                                bool   const emit_tapenoise,
                                bool * const throw_eof_out) {

    int64_t ns_per_bit;
    bool bit_ready;
    int32_t time_1200ths_or_minus_one, total_1200ths;
    char bit_in;
    int e, m;
    bool fire;     /* TOHv4.3 */
    uint8_t strip; /* TOHv4.2 */

    e = TAPE_E_OK;

    /* both dividers have fired, so the ACIA now NEEDS A BIT from the appropriate tape back end;
       however, the back ends emit 1200ths of TONE, not bits, so need to convert
       for 300 baud, and possibly also for 19200 baud if want to try to support
       whatever happens if you try to feed 1200/2400 tone to ~19.2 KHz decoding;
       (I really need to test this on hardware) */

    /* 1200 baud is 832016ns; if double-divided ACIA clock tick is faster than this, then
       behaviour is "undefined" (specifically, for 19.2 KHz); nevertheless, the actual
       hardware must do *something*; ideally, an emulator would duplicate the hardware's
       exact sequence of e.g. framing errors and partial bytes; also I am wondering if
       it is possible to craft a custom stream that decodes successfully at 19.2 KHz */

#define EIGHT_THREE_TWO (13 * 1000 * 64)
    ns_per_bit = (EIGHT_THREE_TWO * acia_rx_divider) / 16;
    bit_in     = '1';
    bit_ready  = false;

    /* TOHv4.2: Don't activate "strip silence and leader" mode until the motor
       has been running for ~0.6s. With overclock + strip modes
       enabled, MOS can easily miss the first block under common circumstances
       if this is not done. */
#define HOLDOFF_1200THS 800
    strip = (ts->strip_silence_and_leader_holdoff_1200ths >= HOLDOFF_1200THS) ? tv->strip_silence_and_leader : 0;

    /* Increment strip_silence_and_leader_holdoff by number of 1200ths. */
    if (ts->strip_silence_and_leader_holdoff_1200ths < HOLDOFF_1200THS) {
        (ts->strip_silence_and_leader_holdoff_1200ths) += (ns_per_bit / EIGHT_THREE_TWO);
    }

    time_1200ths_or_minus_one = -1;
    total_1200ths = -1;
    fire = false;

    /* TOHv4.3: support 19200 baud tape consumption as well as 1200 */
    if ( (832000 == ns_per_bit) || (52000 == ns_per_bit) ) {

        /* Note that when SAVEing using MOS, the RX side of things
         * will (apropos of nothing) be switched into 19200 baud mode.
         * So ns_per_bit will be 52000 while MOS is doing tape TX.
         * 15 out of 16 ticks here can not generate an EOF. */

        fire = (832000 == ns_per_bit) || (ts->hack_19200ths_consumed >= 15);

        if (52000 == ns_per_bit) { /* 19200 baud */
            ts->hack_19200ths_consumed++;
            bit_in = ts->hack_19200ths_last_value;
            if ('\0' == bit_in) {
                bit_in = 'S'; /* hack */
            }
        }

        if (fire) { /* every 1200th, for 1200 and 19200 baud modes */

            ts->hack_19200ths_consumed = 0;

            /* 1201.92 baud (or sixteen ~19200ths) */
            e = tape_1200th_from_back_end (strip,
                                           ts,
                                           acia_rx_awaiting_start(acia), /* from shift reg state */
                                           ! tv->permit_phantoms,
                                           &bit_in,
                                           &time_1200ths_or_minus_one); /* TOHv4.3 */
            if (TAPE_E_EOF == e) {
                *throw_eof_out = true;
                e = TAPE_E_OK;
            }

            if ( TAPE_E_OK != e ) { return e; }

            /* don't update elapsed time if tape has ended
             * [OLD: bugfix: also exclude record mode, which uses
             *  tallied_1200ths for its own purpose] */
            if ( ! *throw_eof_out ) {
                e = tape_update_playback_elapsed(ts, time_1200ths_or_minus_one); /* TOHv4.3 */
            }
            if (TAPE_E_OK != e) { return e; }

            /* TOHv4-rc6: now gated by ula_motor; tackle Atic-Atac loader tapenoise after BREAK bug */
            if ( emit_tapenoise && ts->ula_motor) {
                tapenoise_send_1200 (bit_in, &(ts->tapenoise_no_emsgs));
            }
            ts->tones300_fill = 0;

            ts->hack_19200ths_last_value = bit_in; /* TOHv4.3 */

        }

        ts->ula_prevailing_rx_bit_value = bit_in;
        bit_ready = true;

    } else if (3328000 == ns_per_bit) {

        fire = true; /* actually fires four times, but who's counting */

        /* 300.48 baud, so must consume four 1200ths;
           note that this is always done regardless of whether
           it makes a valid bit or not ... */

        for (m=0; m < TAPE_TONES300_BUF_LEN; m++) {

            char tone, ta, tb;

            e = tape_1200th_from_back_end (tv->strip_silence_and_leader,
                                            ts,
                                            acia_rx_awaiting_start(acia), // from shift reg state
                                            ! tv->permit_phantoms,
                                            &tone,
                                            &time_1200ths_or_minus_one); /* TOHv4.3 */
            if (TAPE_E_EOF == e) {
                *throw_eof_out = true;
                e = TAPE_E_OK;
            }
            if (TAPE_E_OK != e) { return e; }

            /* TOHv4.3: bugfix for *TAPE3 playing 1200 baud section tapenoise at 300 baud */
            if (emit_tapenoise && ts->ula_motor) {
                tapenoise_send_1200 (tone, &(ts->tapenoise_no_emsgs));
            }

            /* don't update elapsed time if tape has ended */
            if ( ! *throw_eof_out ) {
                tape_update_playback_elapsed(ts, time_1200ths_or_minus_one); /* TOHv4.3 */
            }
            if (TAPE_E_OK != e) { return e; }

            ts->ula_prevailing_rx_bit_value = tone;

            ts->tones300[ts->tones300_fill] = tone;

            tb = ('L' == tone)            ? '1' : tone;
            ta = ('L' == ts->tones300[0]) ? '1' : ts->tones300[0];

            /* does the new 1200th match tones[0] ? */
            if (ta != tb) {
                /* logging removed -- too spammy for some tapes */
                /*
                log_info("tape: warning: fuzzy 300-baud bit: [ %c %c %c %c ]; resynchronising",
                         ts->tones300[0],
                         (ts->tones300[1]!=0)?ts->tones300[1]:' ',
                         (ts->tones300[2]!=0)?ts->tones300[2]:' ',
                         (ts->tones300[3]!=0)?ts->tones300[3]:' ');
                */
                /* shift down and resync */
                ts->tones300[0] = tone;
                ts->tones300[1] = '\0';
                ts->tones300[2] = '\0';
                ts->tones300[3] = '\0';
                ts->tones300_fill = 1;
            } else if ( (TAPE_TONES300_BUF_LEN - 1) == ts->tones300_fill ) {
                /* bit ready */
                bit_ready = true;
                bit_in = ts->tones300[0];
                ts->tones300_fill = 0;
            } else {
                (ts->tones300_fill)++;
            }


        }

    } else {
        log_warn("tape: BUG: RX: bad ns_per_bit (%"PRId64")", ns_per_bit);
        e = TAPE_E_BUG;
    }

    if (TAPE_E_OK != e) { return e; }

    /* TOHv4.3 */
    e = tape_get_duration_1200ths (ts, &total_1200ths);
    if ((TAPE_E_OK == e) && bit_ready) {
        e = acia_receive_bit_code (acia, bit_in);
    }

#ifdef BUILD_TAPE_TAPECTRL
    if ((TAPE_E_OK == e) && tv->tapectrl_opened && fire) { /* TOHv4.3-a4: now gated by 'fire' */

        /* TOHv4.3-a4: this code will execute either every 1/1200th or 1/300th of a second.
         * It will go every 1/1200th even when RX 19200 baud is selected. */

        /* will lock+unlock mutex */
        e = send_tone_to_tapectrl_maybe (&(tv->tapectrl),
                                         ts->tallied_1200ths,
                                         total_1200ths,
                                         bit_in,
                                         &(tv->since_last_tone_sent_to_gui));

        /* will also lock+unlock mutex */
        if ((TAPE_E_OK == e) && (*throw_eof_out != tv->previous_eof_value) ) {/*&& ! tv->record_activated) {*/
/*
printf("tapectrl_to_gui_msg_eof(%u): %s:%s(%d) [ns_per_bit = %"PRId64", ts->hack_19200ths_consumed=%d, t=%lf]\n",
*throw_eof_out, __FILE__, __func__, __LINE__, ns_per_bit, ts->hack_19200ths_consumed, al_get_time());
*/
            tapectrl_to_gui_msg_eof(&(tv->tapectrl), true, true, *throw_eof_out);
        }

    }

    if (fire) {
        tv->previous_eof_value = *throw_eof_out;
    }
#endif /* BUILD_TAPE_TAPECTRL */

    if (TAPE_E_OK != e) { return e; }
    return e;
}



int tape_1200th_from_back_end (        bool   const strip_silence_and_leader,              /* turbo */
                               tape_state_t * const ts,
                                       bool   const awaiting_start_bit,
                                       bool   const enable_phantom_block_protection,       /* TOHv3.3 */
                                       char * const tone_1200th_out,                       /* must know about leader */
                                    int32_t * const updated_elapsed_1200ths_out_or_null) { /* usually returns -1, but sometimes a timestamp */

    int e;
    bool inhibit_start_bit_for_phantom_block_protection; /* TOHv3.2 */

    *tone_1200th_out = '\0';
    inhibit_start_bit_for_phantom_block_protection = false;
    if (updated_elapsed_1200ths_out_or_null != NULL) {
        *updated_elapsed_1200ths_out_or_null  = -1;
    }

    /* read 1/1200 seconds of tone from the selected tape back-end */

    do { /* TOHv3.2: only loops back if (strip_silence_and_leader) */

        e = tape_read_1200th (ts, 0, tone_1200th_out, updated_elapsed_1200ths_out_or_null);
        if (TAPE_E_EOF == e) {
            *tone_1200th_out = 'S';
            ts->leader_skip_1200ths = 0; /* TOHv4.2 */
            return TAPE_E_EOF;
        }
        if (e != TAPE_E_OK) { return e; }

        /* TOHv4.2 */
        /* Leader skip complication:
         * If have only seen a very short duration of leader 'L' so far,
         * convert those tones back into '1's so they aren't leader-skipped.
         * The aim is for a leader-skipped signal to contain a short run
         * of '1's between blocks. This will allow enough of a breather
         * that MOS can get its house in order with an overclocked ACIA.
         * Go back to using 'L' after inserting a few 1200ths to make MOS
         * happy. If enabled, the leader skip code can then do its thing
         * with impunity.
         *
         * https://www.stardot.org.uk/forums/viewtopic.php?p=450605#p450605
         */
        if (('0' == *tone_1200th_out) || ('S' == *tone_1200th_out)) {
            ts->leader_skip_1200ths = 0;
        } else if (('L' == *tone_1200th_out) && (ts->leader_skip_1200ths < 10)) {
            (ts->leader_skip_1200ths)++;
            *tone_1200th_out = '1';
        }

        /* handle some conditions concerning leader: */
        if ( awaiting_start_bit ) {
            /* perform leader detection if not already done,
             * to assist the tape noise generator: */
            if (    (ts->start_bit_wait_count_1200ths > TAPE_CRUDE_LEADER_DETECT_1200THS)
                 && ('1' == *tone_1200th_out)) {
                *tone_1200th_out = 'L';
            }
            /* If enabled, prevent "squawks" being mis-detected as
             * the start of genuine MOS blocks.
             *
             * Real hardware tends to reject these phantom blocks,
             * because smashing the analogue circuitry with a signal
             * immediately following a long silence doesn't do wonders
             * for its ability to detect a start bit correctly. It *is*
             * possible to see these fake blocks on a Model B using a
             * high quality digital tone source, and judicious volume
             * settings.
             *
             * This check will make sure a certain duration of
             * continuous tone has prevailed before a start bit may be
             * recognised by the tape system.
             *
             * We don't do this if using the strip_silence_and_leader
             * speed hack.
             *
             */
#define SQUAWKPROT_1200THS_SINCE_SILENT 100
            if ( ! strip_silence_and_leader ) {
                if ('S' == *tone_1200th_out) {
                    ts->num_1200ths_since_silence = 0;
                    /* avoid possible overflow when incrementing this: */
                } else if (ts->num_1200ths_since_silence < SQUAWKPROT_1200THS_SINCE_SILENT) {
                    (ts->num_1200ths_since_silence)++;
                    /* enforce >= ~100 of 1200ths of leader tone after any silence */
                    inhibit_start_bit_for_phantom_block_protection = true;
                }
            }
            /* otherwise, start bit is always eligible */
            (ts->start_bit_wait_count_1200ths)++;
        } else {
            ts->start_bit_wait_count_1200ths = 0;
        }

        if (('0' == *tone_1200th_out) || ('S' == *tone_1200th_out)) {
          ts->start_bit_wait_count_1200ths = 0; /* nix leader identification on '0' or 'S' */
        }

    } while (    (('L' == *tone_1200th_out) || ('S' == *tone_1200th_out))
              && strip_silence_and_leader
              && (TAPE_E_OK == e)); /* TOHv3.2; loop to strip silence and leader */

    if (e != TAPE_E_OK) { return e; }

    /* TOHv3.3: new run-time enable_phantom_block_protection variable */
    if (inhibit_start_bit_for_phantom_block_protection && enable_phantom_block_protection) {
        *tone_1200th_out = 'L';
    }

    return TAPE_E_OK;

}


/* FIXME: t's data is not const because of TIBET's stupid priv nonsense */
bool tape_peek_eof (tape_state_t * const t) {

    bool r;

    /* changed for simultaneous filetypes

       Things start to get potentially thorny here, because we
       could have parallel representations that disagree on precisely
       when the EOF is. We will operate on the principle that
       if we have simultaneous filetypes (because we're writing
       to output and user hasn't picked what kind of file we want
       to save yet), just use UEF for reading back. */

    r = true;

    if (TAPE_FILETYPE_BITS_UEF & t->filetype_bits) {
        r = uef_peek_eof(&(t->uef));
    } else if (TAPE_FILETYPE_BITS_CSW & t->filetype_bits) {
        r = csw_peek_eof(&(t->csw));
    } else if (TAPE_FILETYPE_BITS_TIBET & t->filetype_bits) {
        r = 0xff & tibet_peek_eof(&(t->tibet));
    }

    return r;

}

#include "6502.h"

int tape_read_1200th (tape_state_t * const ser,
                      bool const initial_scan_mode,
                      char * const value_out_or_null,
                      int32_t * const updated_elapsed_1200ths_out_or_null) { /* only valid when -1 not returned */

    int e;
    uef_meta_t meta[UEF_MAX_METADATA];
    uint32_t meta_len;
    uint32_t m;
    int32_t d;
    int32_t hour, min, sec; /* TOHv4.3 */

    hour = 0; /* TOHv4.3 */
    min = 0;
    sec = 0;

    e = TAPE_E_OK;

    if (NULL != updated_elapsed_1200ths_out_or_null) {
        *updated_elapsed_1200ths_out_or_null = -1;
    }

    /* TOHv3: Again the complication:
       Saved data where the user hasn't picked an output format yet exists
       as three parallel copies of itself. In this situation we use UEF
       as the copy to read from, mainly because we might have written
       UEF metadata into it which we would like to read back; we don't
       have as useful metadata in the other file types. (TIBET allows timestamps
       etc., but UEF's potential metadata is significantly richer.) */

    /* so, we check for UEF first. */
    if (TAPE_FILETYPE_BITS_UEF & ser->filetype_bits) {
        meta_len = 0;
        e = uef_read_1200th (&(ser->uef),
                             value_out_or_null,
                             meta,
                             initial_scan_mode, /* TOHv4.3 */
                             &meta_len,
                             updated_elapsed_1200ths_out_or_null); /* only valid if -1 not returned */
        /* TODO: Any UEF metadata chunks that punctuate actual data
         * chunks are accumulated on meta by the above call. Currently
         * this includes chunks &115 (phase change), &120 (position marker),
         * &130 (tape set info), and &131 (start of tape side). At
         * present, nothing is done with these chunks and they are
         * simply destroyed again here. For now we'll just log them */
        if ((TAPE_E_OK == e) || (TAPE_E_EOF == e)) { /* throws EOF */
            for (m=0; m < meta_len; m++) {
                if (meta[m].type != 0x117) { /* only baud rate is currently used */
                    log_info("uef: unused metadata, chunk &%x\n", meta[m].type);
                }
            }
            uef_metadata_list_finish (meta, meta_len);
            // UEF_METADATA_LIST_FINISH (meta, meta_len); /* TOHv4.3: now a macro */
        }
    } else if (TAPE_FILETYPE_BITS_CSW & ser->filetype_bits) {
        e = csw_read_1200th (&(ser->csw),
                             value_out_or_null,
                             initial_scan_mode,
                             updated_elapsed_1200ths_out_or_null);
    } else if (TAPE_FILETYPE_BITS_TIBET & ser->filetype_bits) {
        /* The TIBET reference decoder has a built-in facility
         * for decoding 300 baud; however, it is not used here,
         * so zero is always passed to it. We always request
         * 1/1200th tones from the TIBET back-end (and all other
         * back-ends). 300 baud decoding is now done by b-em itself,
         * regardless of the back-end in use. */
        e = tibet_read_bit (&(ser->tibet),
                            0, /* always 1/1200th tones */
                            initial_scan_mode, /* TOHv4.3 */
                            value_out_or_null);
    } else if (TAPE_FILETYPE_BITS_NONE == ser->filetype_bits) {
        e = TAPE_E_EOF;
    } else {
        log_warn("tape: BUG: unknown internal filetype: &%x", ser->filetype_bits);
        return TAPE_E_BUG;
    }

    if (TAPE_E_EOF == e) {
        if ( ! ser->tape_finished_no_emsgs ) {
            if (initial_scan_mode) { /* TOHv4.3: */
                tape_get_duration_1200ths(ser, &d);
                to_hours_minutes_seconds(d, &hour, &min, &sec);
                log_info("tape: duration %d (%d:%02d:%02d)", d, hour, min, sec);
            } else {
                log_warn("tape: tape finished");
            }
        }
        ser->tape_finished_no_emsgs = 1; /* suppress further messages */
    } else {
        ser->tape_finished_no_emsgs = 0;
    }

    return e;

}

/* used to determine whether "catalogue tape" should be greyed out */
#ifdef BUILD_TAPE_MENU_GREYOUT_CAT
bool tape_peek_for_data (tape_state_t * const ts) {

    uint8_t r;
    uint8_t have_tibet_data; /* don't use bool in tibet.c/.h */
    int e;

    r = 0;
    have_tibet_data = 0;

    /* if (ts->disabled_due_to_error) { */
    if (TAPE_E_OK != ts->prior_exception) {
        return 0;
    }

    if (    (ts->filetype_bits & TAPE_FILETYPE_BITS_CSW)
         && (ts->csw.pulses_fill > 0)) {
        r |= TAPE_FILETYPE_BITS_CSW; /* = 4 */
    }
    if (    (ts->filetype_bits & TAPE_FILETYPE_BITS_UEF)
         && (ts->uef.num_chunks > 0)) {
        r |= TAPE_FILETYPE_BITS_UEF; /* = 1 */
    }
    if (ts->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {
        e = tibet_have_any_data (&(ts->tibet), &have_tibet_data);
        if ((TAPE_E_OK == e) && have_tibet_data) {
            r |= TAPE_FILETYPE_BITS_TIBET; /* = 2 */
        }
    }

    return r;

}
#endif /* BUILD_TAPE_MENU_GREYOUT_CAT */


