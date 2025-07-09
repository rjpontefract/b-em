/*B-em v2.2 by Tom Walker
  CSW cassette support*/
  
/* TOHv3: rewrite, 'Diminished' */
  
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
static char pulse_get_bit_value (uint32_t pulse, uint32_t thresh_smps, uint32_t max_smps);

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
        
        /* compressed? */
        if (csw->header.compressed) {

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
        for (n=0, np=0, trunc=0;
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
                   pulse this way. Those things are of-the-devil. */
                if (pulse < 256) {
                    log_warn("csw: 5-byte CSW pulse but duration < 256 (%u): '%s'\n",
                             pulse, fn);
                    e = TAPE_E_CSW_LONGPULSE_UNDER_256;
                    break;
                }
                n+=4;
            } else {
                pulse = body[n];
            }
            e = csw_append_pulse(csw, pulse);
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
    csw->thresh_smps = (uint32_t) d; /* round down */
    d = (((double) csw->header.rate) / TAPE_1200_HZ);
    csw->len_1200th_smps = (uint32_t) (d + 0.5);
    csw->len_1200th_smps_perfect = d;
}


void csw_finish (csw_state_t *csw) {
    if (NULL != csw->pulses) {
        free(csw->pulses);
    }
    memset(csw, 0, sizeof(csw_state_t));
}



uint8_t csw_peek_eof (csw_state_t *csw) {
    return (csw->cur_pulse >= csw->pulses_fill);
}



int csw_read_1200th (csw_state_t *csw, char *out_1200th) {

    char b;
    
    b = '?';
    
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
    
        if (csw->cur_pulse >= csw->pulses_fill) {
            return TAPE_E_EOF;
        }
        
        /* convert the next 4 pulses into
           bit values that correspond to their length; so at 44.1K
           ~9 becomes '1', and ~18 becomes '0': */
        for (n=0, k = csw->cur_pulse;
             (n < 4) && (k < csw->pulses_fill);
             n++, k++) {
            v[n] = pulse_get_bit_value(csw->pulses[k], csw->thresh_smps, csw->len_1200th_smps);
        }
        
        lookahead = 4;

        /* test for '1' first, and then '0' afterwards;
           lookahead is halved after testing for '1': */
        for (k=0, q='1', wins=0; (k < 2); k++, lookahead >>= 1, q='0') {
            if (q == v[0]) { /* first value matches the wanted value, proceed ... */
                for (n=0; (n < lookahead); n++) {
                    if (q == v[n]) { wins++; }
                }
                if (wins < lookahead) {
                    break; /* insufficient correct-length pulses found in lookahead; failure */
                }
                csw->cur_pulse += lookahead;
                csw->sub_pulse = 0;
                b = q; /* got it */
                break;
            }
        }
        if (b==q) { break; }
        
        if ('S' == v[0]) {
            if (csw->sub_pulse >= csw->pulses[csw->cur_pulse]) {
                csw->sub_pulse = 0;
                (csw->cur_pulse)++;
            } else {
                csw->sub_pulse += csw->len_1200th_smps;
            }
            b = 'S';
            break;
        }
        
        /* give up; advance pulse and try again: */
        (csw->cur_pulse)++;
        
    } while (b == '?');
    
    *out_1200th = b;
    return TAPE_E_OK;
    
}


static char pulse_get_bit_value (uint32_t pulse, uint32_t thresh_smps, uint32_t max_smps) {
    if (pulse <= thresh_smps) {
        return '1';
    } else if (pulse < max_smps) {
        return '0';
    }
    return 'S';
}


int csw_clone (csw_state_t *out, csw_state_t *in) {
    memcpy(out, in, sizeof(csw_state_t));
    out->pulses = malloc(sizeof(uint32_t) * in->pulses_alloc); /* TOHv3 */
    if (NULL == out->pulses) {
        log_warn("csw: out of memory allocating CSW pulses clone");
        return TAPE_E_MALLOC;
    }
    /* ensure unused portion is zero: */
    memset(out->pulses, 0, sizeof(uint32_t) * in->pulses_alloc); /* TOHv3 */
    /* clone pulses */
    memcpy(out->pulses, in->pulses, sizeof(uint32_t) * in->pulses_fill);
    return TAPE_E_OK;
}


void csw_rewind (csw_state_t *csw) {
    csw->cur_pulse = 0;
    csw->sub_pulse = 0;
}


#define TAPE_CSW_ALLOC_DELTA 100000

/* TOHv3: */
int csw_append_pulse (csw_state_t *csw,
                      uint32_t pulse_len_smps) {
    uint32_t z;
    uint32_t *p;
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
        p = realloc(csw->pulses, z * sizeof(uint32_t));
        if (NULL == p) {
            log_warn("csw: out of memory enlarging CSW");
            return TAPE_E_MALLOC;
        }
        memset(p + csw->pulses_fill,
               0,
               sizeof(uint32_t) * TAPE_CSW_ALLOC_DELTA);
        csw->pulses = p;
        csw->pulses_alloc = z;
    }
    csw->pulses[csw->pulses_fill] = pulse_len_smps;
    (csw->pulses_fill)++;
    return TAPE_E_OK;
}


/* TOHv3: */
int csw_append_pulse_fractional_length (csw_state_t *csw,
                                        double pulse_len_smps) {

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

    return csw_append_pulse(csw, pulse);
    
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
int csw_append_leader (csw_state_t *csw, uint32_t num_1200ths) {
    uint32_t i;
    int e;
    csw_init_blank_if_necessary(csw);
    for (i=0, e=TAPE_E_OK; (TAPE_E_OK == e) && (i < (num_1200ths * 4)); i++) {
        e = csw_append_pulse_fractional_length (csw,
                                                csw->len_1200th_smps_perfect / 4.0);
    }
    return e;
}


/* TOHv3: */
int csw_append_silence (csw_state_t *csw, float len_s) {
    uint32_t smps;
    float sane;
    int e;
    /* we must do this now, as we need a valid sample rate: */
    csw_init_blank_if_necessary(csw);
    sane = (2.0f * TAPE_1200TH_IN_S_FLT);
    if (len_s < sane) {
        log_warn("csw: very short silence (%f s); using %f s instead", len_s, sane);
        len_s = sane;
    }
    /* we'll use a pair of pulses, as Quadbike does,
       in order to preserve the polarity. */
    smps = (uint32_t) ((len_s / 2.0f) * ((float) csw->header.rate));
    e = csw_append_pulse (csw, smps);     /* 1 = also update header->num_pulses */
    if (TAPE_E_OK == e) { e = csw_append_pulse(csw, smps); }
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
       frankly this is stupid, because readers should never
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
            if (csw->pulses[n] <= 255) {
                if (1 == pass) {
                    buf[pos] = 0xff & csw->pulses[n];
                }
                pos += 1;
            } else {
                if (1 == pass) {
                    buf[pos] = 0;
                    tape_write_u32(((uint8_t *) buf) + pos + 1, csw->pulses[n]); /* returns void */
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
                                0, /* use zlib encoding */
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

#ifdef BUILD_TAPE_READ_POS_TO_EOF_ON_WRITE
int csw_ffwd_to_end (csw_state_t *csw) {
    csw->cur_pulse = csw->pulses_fill;
    return TAPE_E_OK;
}
#endif
