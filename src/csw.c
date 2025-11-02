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
  
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include "b-em.h"
#include "sysacia.h"
#include "csw.h"
#include "tape.h"

/* #define CSW_DEBUG_PRINT */

#define CSW_BODY_MAXLEN_RAW          (8 * 1024 * 1024)
/*#define CSW_MAXLEN_OVERALL           CSW_BODY_MAXLEN_RAW*/

#define CSW_HEADER_LEN 0x34

#define CSW_MAJOR_VERSION 2

#define CSW_RATE_MIN     8000
#define CSW_RATE_MAX     192000
#define CSW_RATE_DEFAULT 44100 /* might be compatibility problems if not 44.1K? */

#define CSW_MAX_PULSES (CSW_BODY_MAXLEN_RAW) /* good enough */

#define MAGIC "Compressed Square Wave\x1a"

static void populate_pulsetimes_from_rate (csw_state_t *csw);
static char pulse_get_bit_value (uint32_t pulse, double thresh_smps, double max_smps) ;

#ifdef CSW_DEBUG_PRINT
static void debug_csw_print (csw_state_t *csw);

static void debug_csw_print (csw_state_t *csw) {
    csw_header_t *hdr;
    hdr = &(csw->header);
    printf("csw:\n");
    printf("  version      =   %u.%u\n", hdr->version_maj, hdr->version_min);
    printf("  rate         =   %u\n",    hdr->rate);
    printf("  compressed   =   %u\n",    hdr->compressed);
    printf("  flags        =   %u\n",    hdr->flags);
    printf("  ext_len      =   0x%x\n",  hdr->ext_len);
}
#endif /*CSW_DEBUG_PRINT*/

/* TOHv3.2: bugfix, not enough brackets! */
#define CSW_VALID_RATE(rate)  (((rate) >= CSW_RATE_MIN) && ((rate) <= CSW_RATE_MAX))

int csw_load_file (const char *fn, csw_state_t *csw)
{
    int e;
    uint8_t *buf;
    uint32_t len;
    uint8_t *body;
    
    buf = NULL;
    body = NULL;

    memset(csw, 0, sizeof(csw_state_t));

    e = tape_load_file (fn, 0, &buf, &len);
    if (TAPE_E_OK != e) { return e; }

    do { /* try { */

        uint32_t header_ext_len;
        uint32_t body_len, raw_body_len;
        uint32_t start_of_data;
        int trunc;
        uint32_t np, n;
        uint32_t header_num_pulses;
        
        body = NULL;
        body_len = 0;
        raw_body_len = 0;
        start_of_data = 0;
        e = TAPE_E_OK;

        /* file needs to be at least HEADER_LEN long.
           (used to be HEADER_LEN + one pulse, but we want a no-pulses
           CSW to be valid, so the restriction has been loosened) */
        if (len < (/*1 +*/ CSW_HEADER_LEN)) {
            log_warn("csw: header is truncated: '%s'", fn);
            e = TAPE_E_CSW_HEADER_TRUNCATED;
            break;
        }
        
        if (0 != memcmp (MAGIC, buf, strlen(MAGIC))) {
            log_warn("csw: bad magic: '%s'", fn);
            e = TAPE_E_CSW_BAD_MAGIC;
            break;
        }
        
        csw->header.version_maj = buf[0x17];
        csw->header.version_min = buf[0x18];
        if (csw->header.version_maj != CSW_MAJOR_VERSION) {
            log_warn("csw: unknown major version %u: '%s'", csw->header.version_maj, fn);
            e = TAPE_E_CSW_BAD_VERSION;
            break;
        }
        if ((csw->header.version_min != 0) && (csw->header.version_min != 1)) {
            log_warn("csw: unknown minor version %u: '%s'", csw->header.version_min, fn);
            e = TAPE_E_CSW_BAD_VERSION;
            break;
        }
        if (1 == csw->header.version_min) {
            log_warn("csw: minor version 1: CSW violates spec: '%s'", fn);
        }
        
        csw->header.rate = tape_read_u32 (buf + 0x19);
        if ( ! CSW_VALID_RATE(csw->header.rate) ) {
            log_warn("csw: bad sample rate %d: '%s'", csw->header.rate, fn);
            e = TAPE_E_CSW_BAD_RATE;
            break;
        }
        
        /* read fill, and make alloc = fill */
        header_num_pulses = tape_read_u32(buf + 0x1d);
        
        /* blank (no-pulses) CSWs are now allowed; this restriction has been loosened */
        if ((header_num_pulses > CSW_MAX_PULSES)) { /* || (0 == header_num_pulses)) {*/
            log_warn("csw: bad number of pulses in header (%d): '%s'", header_num_pulses, fn);
            e = TAPE_E_CSW_HEADER_NUM_PULSES;
            break;
        }
        
        if ((buf[0x21] != 1) && (buf[0x21] != 2)) {
            log_warn("csw: bad compression value (%d): '%s'", buf[0x21], fn);
            e = TAPE_E_CSW_COMP_VALUE;
            break;
        }
        csw->header.compressed = (buf[0x21] == 2);
        
        if (buf[0x22] & 0xf8) {
            log_warn("csw: bad flags value (0x%x): '%s'", buf[0x22], fn);
            e = TAPE_E_CSW_BAD_FLAGS;
            break;
        }
        /* Yuck. Someone out there has created non-standard CSWs with
           an illegal version of 2.1, flags set to an illegal value,
           and an illegal non-empty header extension. Permit these, but
           you know who you are. */
        if ((buf[0x22] != 0) && (buf[0x22] != 1)) {
            log_warn("csw: illegal flags (&%x): CSW violates spec: '%s'", buf[0x22], fn);
        }
        csw->header.flags = buf[0x22];
        
        /* consume remaining header */
        csw->header.ext_len = buf[0x23];
        header_ext_len = csw->header.ext_len; /* 32-bit edition */
        if (header_ext_len != 0) {
            log_warn("csw: hdr. ext. len. is nonzero (&%x); "
                     "may be an illegal CSW that abuses hdr. ext. for "
                     "purposes of graffiti", header_ext_len);
            if ((CSW_HEADER_LEN + header_ext_len) > len) { /* TOHv3.2: again, 0 pulses is OK */
                log_warn("csw: file truncated during hdr. ext. (&%x > len (&%x)) : '%s'",
                         (CSW_HEADER_LEN + header_ext_len), len, fn);
                e = TAPE_E_CSW_HEADER_TRUNCATED;
                break;
            }
        }
        
        start_of_data = (CSW_HEADER_LEN + header_ext_len);

        raw_body_len = len - start_of_data;

#ifdef CSW_DEBUG_PRINT
        debug_csw_print(csw);
#endif

        if (0 == raw_body_len) {

            log_warn("csw: WARNING: CSW body is empty!");

        } else if (csw->header.compressed) {

            e = tape_decompress (&body, &body_len, buf + start_of_data, raw_body_len);
            if (TAPE_E_OK != e) { break; }
                        
        } else {
            /* uncompressed; just copy */
            if (raw_body_len >= CSW_BODY_MAXLEN_RAW) {
                log_warn("csw: raw body is too large (%u): '%s'", raw_body_len, fn);
                e = TAPE_E_CSW_BODY_LARGE;
                break;
            }
            body = buf + start_of_data;
            body_len = raw_body_len;
        }
        
        /* parse pulses */
        for (n=0, np=0, trunc=0; /*, pulse_acc_16m_smps=0;*/
             (TAPE_E_OK == e) && (n < body_len);
             n++, np++) {

            uint32_t pulse;

            if (0 == body[n]) {
                if ((n + 4) >= body_len) {
                    log_warn("csw: truncated? '%s'", fn);
                    trunc = 1;
                    break;
                }
                pulse = tape_read_u32(body + n + 1);
                /* TOHv3.2: we're going to be strict about this; if the pulse len
                   is < 256 then there's no need for the five-byte extended pulse
                   format -- it could have been done in one byte instead. I'm going to
                   reject five-byte pulses with values < 256, and codify that with
                   a unit test. This also prevents sneaking in a zero-duration
                   pulse this way, as they are of the devil. */
                if (pulse < 256) {
                    log_warn("csw: 5-byte CSW pulse but duration < 256 (%u): '%s'\n", pulse, fn);
                    e = TAPE_E_CSW_LONGPULSE_UNDER_256;
                    break;
                }
                n+=4;
            } else {
                pulse = body[n];
            }

            /* insert the pulse */
            e = csw_append_pulse (csw, pulse, NULL);

        }

        if (TAPE_E_OK != e) { break; }
        
        if (csw->pulses_fill != header_num_pulses) {
            log_warn("csw: pulses in body (%u) does not match value in header (%u): '%s'",
                     csw->pulses_fill, header_num_pulses, fn);
            e = TAPE_E_CSW_PULSES_MISMATCH;
            break;
        }
        
        if (trunc) { break; }
        
        populate_pulsetimes_from_rate(csw);
    
    } while (0); /* catch { } */
    
    free(buf);
    /* TOHv3.2: fixed potential free(NULL) */
    if (csw->header.compressed && (body != NULL)) { free(body); }
    
    return e;
    
}


static void populate_pulsetimes_from_rate (csw_state_t *csw) {
    double d;
    /* "three-halves" threshold: */
    d = (   (((double) csw->header.rate) / TAPE_1200_HZ)
          + (((double) csw->header.rate) / (2.0 * TAPE_1200_HZ))   ) / 4.0;
    csw->thresh_smps_perfect = d;
    d = (((double) csw->header.rate) / TAPE_1200_HZ);
    csw->len_1200th_smps_perfect = d;
}


void csw_finish (csw_state_t *csw) {
    if (NULL != csw->pulses) {
        free(csw->pulses);
    }
    memset(csw, 0, sizeof(csw_state_t));
}



bool csw_peek_eof (const csw_state_t * const csw) {
    return (csw->cur_pulse >= csw->pulses_fill);
}


int csw_read_1200th (csw_state_t * const csw,
                     char * const out_1200th_or_null,
                     bool initial_scan,
                     int32_t * const elapsed_out_or_null) { /* TOHv4.3: return elapsed time if available (or -1) */

    char b;
    tape_interval_t *i_prev, *i_cur;
    
    b = '?';

    if (NULL != elapsed_out_or_null) {
        *elapsed_out_or_null = 0;
    }

    /* TOHv4.3 */
    if (NULL == csw->pulses)  { return TAPE_E_EOF; }
    if (csw->pulses_fill < 1) { return TAPE_E_EOF; }

    i_prev = NULL;
    i_cur = &(csw->pulses[csw->cur_pulse].timespan);

    if (csw->cur_pulse>0) {
        i_prev = &(csw->pulses[csw->cur_pulse-1].timespan);
    }

    /* currently cOnsOOming silence? */
    if (csw->num_silent_1200ths > 0) {

        /* in the process of cOnsOOming silence
         * first of all, set up the elapsed 1200ths value to return */
        if (NULL != elapsed_out_or_null) {
            *elapsed_out_or_null = i_cur->start_1200ths + csw->cur_silence_1200th;
        }

        /* now decide whether it's finished yet */
        if (csw->cur_silence_1200th < (csw->num_silent_1200ths - 1)) {

            /* silence is not finished yet */

            (csw->cur_silence_1200th)++;

        } else {

            if (initial_scan) {
                i_cur->pos_1200ths = csw->num_silent_1200ths;
            }

            (csw->cur_pulse)++;

            /* discard any remainder; end silent period */
            csw->cur_silence_1200th = 0;
            csw->num_silent_1200ths = 0;

        }

        *out_1200th_or_null = 'S';
        return TAPE_E_OK;
    }

    /* TOHv4.3: we are not currently cOnsOOming silence */
    if (initial_scan && (NULL != i_prev)) {
        i_cur->start_1200ths = i_prev->start_1200ths + i_prev->pos_1200ths;
    }
    if (NULL != elapsed_out_or_null) {
        *elapsed_out_or_null = i_cur->start_1200ths; // + i_cur->pos_1200ths;
    }

    do {
    
        /* buffer for lookahead pulse values;
           - 2  elements used for a 0-tone (1/1200s)
           - 4  elements used for a 1-tone (1/1200s) */
           
        char v[4] = { 'X','X','X','X' };
        
        uint8_t n;
        uint32_t k;
        uint8_t lookahead;
        char q;
        int wins;
        tape_interval_t *i;
        csw_pulse_t *pulse_p;

        i_cur = &(csw->pulses[csw->cur_pulse].timespan);

        if (csw->cur_pulse >= csw->pulses_fill) {
            return TAPE_E_EOF;
        }

        /* convert the next 4 pulses into
           bit values that correspond to their length; so at 44.1K
           ~9 becomes '1', and ~18 becomes '0': */
        for (n=0, k = csw->cur_pulse;
             (n < 4) && (k < csw->pulses_fill);
             n++, k++) {

            v[n] = pulse_get_bit_value (csw->pulses[k].len_smps, csw->thresh_smps_perfect, csw->len_1200th_smps_perfect);

            /* TOHv4.3: Also, at the same time, populate timestamp information on these 4 chunks */
            i_prev = (k > 0) ? &(csw->pulses[k-1].timespan) : NULL;
            i = &(csw->pulses[k].timespan);

            /* pulse [k] takes its start time from pulse[k-1].
                *
                * Pulses for '1' and '0' will be shorter than one 1200th,
                * so most of these will have pos_1200ths=0; the final pulse
                * will get the duration of the whole bit when the bit is
                * committed. */
            if (initial_scan) {
                i->start_1200ths = (NULL == i_prev) ? 0 : (i_prev->start_1200ths + i_prev->pos_1200ths);
                i->pos_1200ths = 0;
            }

        } /* next of 4 pulses */

        /* see if enough pulses are in concordance (4 for '0', 2 for '1');
           test for '1' first, and then '0' afterwards;
           lookahead is halved after testing for '1': */

        pulse_p = csw->pulses + csw->cur_pulse;

        for (k=0, q='1', wins=0, lookahead=4; (k < 2); k++, lookahead >>= 1, q='0') {
            if (q == v[0]) { /* first value matches the wanted value, proceed ... */
                for (n=0; (n < lookahead); n++) {
                    if (q == v[n]) { wins++; }
                }
                if (wins < lookahead) {
                    break; /* insufficient correct-length pulses found in lookahead; failure */
                }
                csw->cur_pulse += lookahead;
                b = q; /* got it */
                break;
            }
        } /* if '1' failed, try '0' */

        /* it was '1' or '0', so break now */
        if (b==q) {
            /* update the timespan information for the FINAL pulse of the 1200th ONLY */
            if (initial_scan) {
                pulse_p[('0'==q)?1:3].timespan.pos_1200ths = 1;

            }

            break;
        }

        /* neither { '1', '1', X, X } nor { '0', '0', '0', '0' }
         * so, check for { 'S', X, X, X } */

        if ('S' == v[0]) {

            /* Beginning of silence. Set up ongoing silence processing. */

            csw->num_silent_1200ths = (int32_t) /*round*/((((double)pulse_p->len_smps) * TAPE_1200_HZ) / (double) (csw->header.rate)); /* round down */

            if (0 == csw->num_silent_1200ths) {
              //  csw->num_silent_1200ths = 1;
                    log_warn("csw: WARNING: very short silence (%u smps); skipping!", pulse_p->len_smps);
                    b = '?';
                    csw->num_silent_1200ths=0;
                    csw->cur_silence_1200th = 0;
                    (csw->cur_pulse)++;
                    continue;
            }

            csw->cur_silence_1200th = 1; /* cOnsOOm */

            *out_1200th_or_null = 'S';

            if (NULL != elapsed_out_or_null) {
                *elapsed_out_or_null = i_cur->start_1200ths + i_cur->pos_1200ths;
            }

            if (initial_scan) { //} && (i_cur != NULL)) {
                // (csw->pulses[csw->cur_pulse].timespan.pos_1200ths)++;
                (i_cur->pos_1200ths)++;
            }

            if (1==csw->num_silent_1200ths) {
                (csw->cur_pulse)++;
                csw->num_silent_1200ths=0;
                csw->cur_silence_1200th = 0;
            }

            return TAPE_E_OK;

        }

        /* give up; advance pulse and try again: */
        (csw->cur_pulse)++;
        if (csw->cur_pulse >= csw->pulses_fill) {
            if (out_1200th_or_null != NULL) {
                *out_1200th_or_null='\0';
            }
            return TAPE_E_EOF;
        }

    } while ('?' == b); /* Ambiguous pulse sequence, or short silence. Advance by one pulse. Try again. */

    /* this code is always for '0' or '1' */

    if (NULL != out_1200th_or_null) {
        *out_1200th_or_null = b;
    }
    if (csw->cur_pulse >= csw->pulses_fill) {
        return TAPE_E_EOF;
    }
    return TAPE_E_OK;
    
}


static char pulse_get_bit_value (uint32_t pulse, double thresh_smps, double max_smps) {
    double pulse_f;
    pulse_f = (double) pulse;
    if (pulse_f <= thresh_smps) {
        return '1';
    } else if (pulse_f < max_smps) {
        return '0';
    }
    return 'S';
}


int csw_clone (csw_state_t *out, csw_state_t *in) {
    memcpy(out, in, sizeof(csw_state_t));
    out->pulses = malloc(sizeof(csw_pulse_t) * in->pulses_alloc); /* TOHv3 */
    if (NULL == out->pulses) {
        log_warn("csw: out of memory allocating CSW pulses clone");
        return TAPE_E_MALLOC;
    }
    /* ensure unused portion is zero: */
    memset(out->pulses, 0, sizeof(csw_pulse_t) * in->pulses_alloc); /* TOHv3 */
    /* clone pulses */
    memcpy(out->pulses, in->pulses, sizeof(csw_pulse_t) * in->pulses_fill);
    return TAPE_E_OK;
}


void csw_rewind (csw_state_t *csw) {
    csw->cur_pulse = 0;
    csw->cur_silence_1200th = 0;
    csw->num_silent_1200ths = 0;
}


#define TAPE_CSW_ALLOC_DELTA 100000

/* TOHv3: */
int csw_append_pulse (csw_state_t * const csw,
                      uint32_t const pulse_len_smps,
                      const tape_interval_t * const interval_or_null) { /* TOHv4.3: interval arg */
    uint32_t z;
    csw_pulse_t *p;
    int e;
    /* sanity */
    if (0 == pulse_len_smps) {
        /* nope */
        log_warn("csw: BUG: attempting to append zero-length pulse");
        return TAPE_E_CSW_WRITE_NULL_PULSE;
    }
    e = csw_init_blank_if_necessary(csw);
    if (TAPE_E_OK != e) { return e; }
    /*
      for CSW, we must keep the value in the header consistent with the
      actual buffer fill at all times, because the thing may be cloned at any time
      for tape catalogue.
    */
    if (csw->pulses_fill >= csw->pulses_alloc) {
        z = csw->pulses_fill + TAPE_CSW_ALLOC_DELTA;
        p = realloc(csw->pulses, z * sizeof(csw_pulse_t));
        if (NULL == p) {
            log_warn("csw: out of memory enlarging CSW");
            return TAPE_E_MALLOC;
        }
        memset(p + csw->pulses_fill, /* wipe new portion */
               0,
               sizeof(csw_pulse_t) * TAPE_CSW_ALLOC_DELTA);
        csw->pulses = p;
        csw->pulses_alloc = z;
    }
    csw->pulses[csw->pulses_fill].len_smps = pulse_len_smps;
    if (NULL == interval_or_null) {
        memset(&(csw->pulses[csw->pulses_fill].timespan),
               0,
               sizeof(tape_interval_t));
    } else {
        csw->pulses[csw->pulses_fill].timespan = *interval_or_null;
    }
    (csw->pulses_fill)++;
    return TAPE_E_OK;
}


/* TOHv3: */
int csw_append_pulse_fractional_length (csw_state_t * const csw,
                                        double const pulse_len_smps,
                                        const tape_interval_t * const interval_or_null) { /* TOHv4.3: interval arg */

    uint32_t pulse;
    int e;
    
    e = csw_init_blank_if_necessary(csw);
    if (TAPE_E_OK != e) { return e; }

    /* round down */
    pulse = (uint32_t) pulse_len_smps;

    /* jitter it, if accumulated error exceeds limit */
    if (csw->accumulated_error_smps > 0.5) {
        pulse += 1;
    }
    
    /* update the accumulated error */
    csw->accumulated_error_smps += (pulse_len_smps - ((double) pulse));

    return csw_append_pulse(csw, pulse, interval_or_null);
    
}



/* TOHv3: */
int csw_init_blank_if_necessary (csw_state_t *csw) {

    csw_header_t *h;

    /* use the rate field in the header to determine
       whether we have an existing valid CSW header or not */
    if ( CSW_VALID_RATE (csw->header.rate) ) { return TAPE_E_OK; }
    
    memset(csw, 0, sizeof(csw_state_t));
    h = &(csw->header);
    
    h->rate        = CSW_RATE_DEFAULT; /* 44.1KHz */
    h->compressed  = 1;
    h->ext_len     = 0;
    h->version_maj = CSW_MAJOR_VERSION;
    h->version_min = 0;
    
    populate_pulsetimes_from_rate(csw);
    
    return TAPE_E_OK;
    
}


/* TOHv3: */
int csw_append_leader (csw_state_t * const csw,
                       uint32_t const num_1200ths,
                       int32_t start_1200ths) {
    uint32_t j;
    int e;
    tape_interval_t i;
    csw_init_blank_if_necessary(csw);
    i.start_1200ths = start_1200ths;
    for (j=0, e=TAPE_E_OK; (TAPE_E_OK == e) && (j < (num_1200ths * 4)); j++) {
        if (3==(j&3)) { /* TOHv4.3: update interval as we go */
            i.pos_1200ths = 1;
        } else {
            i.pos_1200ths = 0;
        }
        e = csw_append_pulse_fractional_length (csw,
                                                csw->len_1200th_smps_perfect / 4.0,
                                                &i); /* TOHv4.3: timespan */
        if (3==(j&3)) { /* TOHv4.3: update interval as we go */
            i.start_1200ths++;
        }
    }
    return e;
}


/* TOHv3: */
int csw_append_silence (csw_state_t * const csw,
                        float len_s,
                        const tape_interval_t * const interval_or_null) {
    uint32_t smps;
    float sane;
    int e;
    tape_interval_t i1, i2;
    /* we must do this now, as we need a valid sample rate: */
    csw_init_blank_if_necessary(csw);
    sane = (2.0f * TAPE_1200TH_IN_S_FLT);
    if (len_s < sane) {
        log_warn("csw: very short silence (%f s); using %f s instead", len_s, sane);
        len_s = sane;
    }
    /* We'll use a pair of pulses, as Quadbike does,
       in order to preserve the polarity.
       (TOHv4.3: We'll have to split the supplied interval.) */
    memset(&i1, 0, sizeof(tape_interval_t));
    memset(&i2, 0, sizeof(tape_interval_t));
    if (NULL != interval_or_null) {
        i1 = *interval_or_null;
        i1.pos_1200ths /= 2;
        i2.start_1200ths = i1.start_1200ths + i1.pos_1200ths;
        i2.pos_1200ths = interval_or_null->pos_1200ths - i1.pos_1200ths; /* remainder to interval 2 */
    }
    smps = (uint32_t) ((len_s / 2.0f) * (float) csw->header.rate);
    e = csw_append_pulse (csw, smps, &i1);
    if (TAPE_E_OK == e) { e = csw_append_pulse (csw, smps, &i2); }
    return e;
}

#include "b-em.h" /* for VERSION_STR */

/* TOHv3: */
int csw_build_output (csw_state_t *csw,
                      uint8_t compress,
                      char **out,
                      size_t *len_out) {

    uint8_t pass;
    size_t pos, zbuf_len;
    char *buf, *zbuf;
    int e;
    uint8_t header[CSW_HEADER_LEN];
    size_t pos_compress_flag;
    uint8_t verstr[16];
    size_t verlen;
    
    buf = NULL;
    zbuf = NULL;
    zbuf_len = 0;

    /* if generating a completely blank (no pulses) CSW, the CSW won't have a
       header yet! */
    csw_init_blank_if_necessary(csw);
    
    pos=strlen(MAGIC);
    memcpy(header, MAGIC, pos);
    header[pos++] = csw->header.version_maj;
    header[pos++] = csw->header.version_min;
    tape_write_u32(header + pos, csw->header.rate);
    pos+=4;
    tape_write_u32(header + pos, csw->pulses_fill);
    pos+=4;
    pos_compress_flag = pos; /* TOHv4: defer compress value for now */
    pos++;
    header[pos++] = csw->header.flags;
    header[pos++] = 0; /* hdr ext len */
    /* needs to be null-terminated (apparently), although
       this is pointless, because readers should never
       assume that the terminator may be relied upon */
    memcpy(verstr, "               ", 16);
    verlen = strlen(VERSION_STR);
    if (verlen > 15) { verlen = 15; }
    memcpy(verstr, VERSION_STR, verlen); /* do NOT copy null terminator from VERSION_STR */
    memcpy(header+pos, verstr, 16);
    
    for (pass=0, pos=0, *len_out=0, *out=NULL, e=TAPE_E_OK;
         (pass < 2) && (csw->pulses_fill > 0); /* TOHv3.2: prevent catastrophe if no pulses */
         pass++) {

        uint32_t n;

        for (n=0, pos=0; n < csw->pulses_fill; n++) {
            if (csw->pulses[n].len_smps <= 255) {
                if (1 == pass) {
                    buf[pos] = 0xff & csw->pulses[n].len_smps;
                }
                pos += 1;
            } else {
                if (1 == pass) {
                    buf[pos] = 0;
                    tape_write_u32(((uint8_t *) buf) + pos + 1, csw->pulses[n].len_smps); /* returns void */
                }
                pos += 5;
            }
        }
        if (0 == pass) {
            buf = malloc(pos);
            if (NULL == buf) {
                log_warn("csw: build CSW output: out of memory (1)");
                return TAPE_E_MALLOC;
            }
        }
    }
    
    /* TOHv3.2: if the payload is empty, force type-1 CSW,
     * rather than messing about trying to compress zero bytes */
    if ( (0 == pos) || ( ! compress ) ) {
        if (pos > 0) { /* TOHv4: avoid malloc(0) */
            /* no compression or no data => just copy it */
            zbuf = malloc(pos);
            if (NULL == zbuf) {
                log_warn("csw: build CSW output: out of memory (2)");
                e = TAPE_E_MALLOC;
            } else {
                zbuf_len = pos;
                memcpy(zbuf, buf, zbuf_len);
            }
        }
        header[pos_compress_flag] = 1; /* TOHv4: deferred compress flag */
    } else {
        /* compress it */
        e = tape_zlib_compress (buf,
                                pos,
                                false, /* use zlib encoding */
                                &zbuf,
                                &zbuf_len);
        header[pos_compress_flag] = 2; /* TOHv4: deferred compress flag */
    }
    
    
    /* we're finished with uncompressed buffer now */
    free(buf);
    
    if (TAPE_E_OK != e) {
        if (zbuf != NULL) { free(zbuf); }
        return e;
    }
    
    /* lame */
    *out = malloc(zbuf_len + CSW_HEADER_LEN);
    if (NULL == *out) {
        log_warn("csw: build CSW output: out of memory (3)");
        if (zbuf != NULL) { free(zbuf); } /* TOHv4: NULL check */
        return TAPE_E_MALLOC;
    }
    memcpy(*out, header, CSW_HEADER_LEN);
    if (zbuf_len > 0) { /* TOHv4: zbuf_len > 0 check */
        memcpy(*out + CSW_HEADER_LEN, zbuf, zbuf_len);
    }
    *len_out = zbuf_len + CSW_HEADER_LEN;
    
    if (zbuf != NULL) { free(zbuf); } /* TOHv4: NULL check */
    
    return e;

}

int csw_ffwd_to_end (csw_state_t *csw) {
    csw->cur_pulse = csw->pulses_fill;
    return TAPE_E_OK;
}

/* TOHv4.3 */
void csw_get_duration_1200ths (const csw_state_t * const csw,
                               int32_t * const dur_1200ths_out) {
    tape_interval_t *interval;
    *dur_1200ths_out = 0;
    if (csw->pulses_fill <= 0) { return; }
    interval = &(csw->pulses[csw->pulses_fill-1].timespan);
    *dur_1200ths_out = interval->start_1200ths + interval->pos_1200ths;

}

/* TOHv4.3, for seeking */
int csw_change_current_pulse (csw_state_t * const csw, uint32_t const pulse_ix) {
    if (pulse_ix >= csw->pulses_fill) {
        log_warn("csw: BUG: seek: pulse_ix (%u) >= csw->pulses_fill (%u)", pulse_ix, csw->pulses_fill);
        return TAPE_E_BUG;
    }
    csw->cur_pulse = pulse_ix;
    csw->cur_silence_1200th = 0;
    csw->num_silent_1200ths = 0;
    return TAPE_E_OK;
}
