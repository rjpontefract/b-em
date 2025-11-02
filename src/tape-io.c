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
#include "tape-io.h"

#include <zlib.h>

#define TAPE_FILE_MAXLEN            (32 * 1024 * 1024) /* TOHv4.3: doubled for psycho 114 chunk test */
#define TAPE_MAX_DECOMPRESSED_LEN   (64 * 1024 * 1024)

static int tibet_load_file (tape_state_t * const ts,
                            tape_vars_t * const tv,
                            const char * const fn,
                            bool const decompress,
                            tibet_t * const t);

static int csw_load (tape_state_t * const ts,
                     tape_vars_t * const tv,
                     const char * const fn);

static int tibet_load_2 (tape_state_t * const ts,
                         tape_vars_t * const tv,
                         const char * const fn,
                         bool decompress);

static int uef_load (tape_state_t * const ts,
                     tape_vars_t * const tv,
                     const char * const fn);

static int tibet_load (tape_state_t * const ts,
                       tape_vars_t * const tv,
                       const char * const fn) ;

static int tibetz_load (tape_state_t * const ts,
                        tape_vars_t * const tv,
                        const char * const fn);

static struct
{
    char *ext;
    /* TOHv4.3: changed prototype, return int error code, pass ts and tv */
    int (*load)(tape_state_t * const ts, tape_vars_t * const tv, const char * const fn);
    void (*close)();
}
loaders[]=
{
    /* TOHv2: strange mixed-metaphor here
     * (terminated array & numeric limit) */

    /* TOHv3: individual close-functions are gone;
     * rely on universal tape_state_finish() now */
#define TAPE_NUM_LOADERS 4  /* TOHv2 */
    {"UEF",    uef_load,    NULL},
    {"CSW",    csw_load,    NULL},
    {"TIBET",  tibet_load,  NULL},  /* TOH */
    {"TIBETZ", tibetz_load, NULL},  /* TOH */
    {NULL, NULL, NULL} /* TOHv2 */
};

static int tibet_load_file (tape_state_t * const ts,
                            tape_vars_t * const tv,
                            const char * const fn,
                            bool const decompress,
                            tibet_t * const t) {

    int e;
    char *buf;
    uint32_t len;

    len = 0;
    buf = NULL;

    e = tape_load_file (fn, decompress, (uint8_t **) &buf, &len);
    if (TAPE_E_OK != e) { return e; }

    e = tibet_decode (buf, len, t);
    if (TAPE_E_OK != e) {
        log_warn("tape: error (code %d) decoding TIBET file '%s'", e, fn);
        free(buf);
        return e;
    }

    free(buf);
    buf = NULL;

    return TAPE_E_OK;

}

static int csw_load (tape_state_t * const ts,
                     tape_vars_t * const tv,
                     const char * const fn) {

    int e;

    /* TOHv4.3: do not touch ACIA or ULA */
    ts->filetype_bits = TAPE_FILETYPE_BITS_CSW;
    tape_state_init_alter_menus(ts, tv);

    e = csw_load_file (fn, &(ts->csw));
    if (TAPE_E_OK != e) {
        log_warn("tape: could not load CSW file (code %d): '%s'", e, fn);
    } else {
        e = tape_load_successful (ts, tv, (char *)fn);
    }
    return e;
}



/* this may already have set the shutdown exit code;
 * so don't propagate errors from this */
int tape_load(tape_state_t * const ts,
              tape_vars_t * const tv,
              const ALLEGRO_PATH * const fn)
{
    int e; /* TOHv4.3 */
    int c = 0;
    const char *p, *cpath;
    if (NULL == fn) { /* TOHv4.3 */
        log_warn("tape: BUG: load: NULL filename supplied");
        return TAPE_E_BUG;
    }
    cpath = al_path_cstr(fn, ALLEGRO_NATIVE_PATH_SEP);
    if ('\0' == cpath[0]) { return TAPE_E_OK; }
    /* ignore blanks */
    p = al_get_path_extension(fn);
    if ((NULL==p) || ('\0'==*p)) {
        log_warn("tape: load: filename has no extension");
        return TAPE_E_BLANK_EXTENSION;
    }
    if (*p == '.') { p++; }
    log_info("tape: Loading %s %s", cpath, p);
    while (loaders[c].ext)
    {
        if (!strcasecmp(p, loaders[c].ext))
        {
            /* TOHv4.3: returns a code now */
            e = loaders[c].load(ts, tv, cpath);
            /* TOHv4.3: now here rather than duplicated in uef_load, csw_load etc. */
            tape_handle_exception(ts,
                                  tv,
                                  e,
                                  tv->testing_mode & TAPE_TEST_QUIT_ON_EOF,
                                  tv->testing_mode & TAPE_TEST_QUIT_ON_ERR,
                                  true); /* alter menus on failure */
            return TAPE_E_OK;
        }
        c++;
    }
    log_warn("tape: load: no loader found for file extension: %s\n", p);
    return TAPE_E_UNK_EXT;
}

static int tibet_load (tape_state_t * const ts,
                       tape_vars_t * const tv,
                       const char * const fn) {
    return tibet_load_2(ts, tv, fn, false);
}

static int tibetz_load (tape_state_t * const ts,
                        tape_vars_t * const tv,
                        const char * const fn) {
    return tibet_load_2(ts, tv, fn, true);
}



/* FIXME: need to change the function pointer for the file type
 * loader prototype. needs to return int, and take ts and tv as args.
 * all three loaders will then need to have their prototypes changed. */
static int uef_load (tape_state_t * const ts,
                     tape_vars_t * const tv,
                     const char * const fn) {

    int e;
    int32_t baud;

    /* TOHv4.3: removed; if tape state is reset on tape replacement,
       bad things occur, so we can't do this any more: */
    /*tape_state_init (ts, tv, TAPE_FILETYPE_BITS_UEF, 1);*/

    /* instead we have to init some things, but not touch the ACIA or ULA  */
    ts->filetype_bits = TAPE_FILETYPE_BITS_UEF;
    tape_state_init_alter_menus(ts, tv);

    e = uef_load_file (fn, &(ts->uef));
    if (TAPE_E_OK != e) {
        log_warn("tape: could not load UEF file (code %d): '%s'", e, fn);
    } else {
        e = tape_load_successful (ts, tv, (char *) fn); /* TOHv4.3: handle errors properly */
        if (TAPE_E_OK != e) { return e; }
        ts->w_uef_was_loaded = 1;
        ts->w_uef_origin_written = 0;
        baud = 1200;
        if (ts->uef.num_chunks>0) {
            uef_scan_backwards_for_chunk_117 (&(ts->uef), ts->uef.num_chunks-1, &baud);
            if (0 == baud) { baud = 1200; }
            ts->w_uef_prevailing_baud = baud;
        }
    }
    return e;
}

/* FIXME: need to change the function pointer for the file type
 * loader prototype. needs to return int, and take ts and tv as args.
 * all three loaders will then need to have their prototypes changed. */
static int tibet_load_2 (tape_state_t * const ts,
                         tape_vars_t * const tv,
                         const char *fn,
                         bool decompress) {
    int e;

    /* TOHv4.3: do not touch ACIA or ULA */
    ts->filetype_bits = TAPE_FILETYPE_BITS_TIBET;
    tape_state_init_alter_menus(ts, tv);

    e = tibet_load_file (ts, tv, fn, decompress, &(ts->tibet));
    if (TAPE_E_OK != e) {
        log_warn("tape-io: could not load TIBET file (code %d): '%s'", e, fn);
    } else {
        e = tape_load_successful(ts, tv, (char *)fn); /* TOHv4.3: handle errors prop'ly */
    }
    return e;
}



int tape_load_file (const char *fn, uint8_t decompress, uint8_t **buf_out, uint32_t *len_out) {

    FILE *f;
    uint32_t pos, alloc;
    uint8_t *buf;
    int e;
    uint8_t *buf2;
    uint32_t buf2_len;

    e = TAPE_E_OK;

    buf = NULL;
    buf2 = NULL;
    buf2_len = 0;

    if (NULL == (f = fopen (fn, "rb"))) {
        log_warn("tape: Unable to open file '%s': %s", fn, strerror(errno));
        return TAPE_E_FOPEN;
    }

    pos = 0;
    alloc = 0;

#define TAPE_FILE_ALLOC_DELTA (1024 * 1024)

    while ( ! feof(f) && ! ferror(f) ) {
        uint32_t chunk;
        uint32_t newsize;
        long num_read;
        uint8_t *ptr;
        chunk = 1024;
        /* ask for 1024 bytes */
        if ((pos + chunk) >= TAPE_FILE_MAXLEN) {
            log_warn("tape: File is too large: '%s' (max. %d)", fn, TAPE_FILE_MAXLEN);
            e = TAPE_E_FILE_TOO_LARGE;
            break;
        }
        if ((pos + chunk) >= alloc) {
            newsize = pos + chunk + TAPE_FILE_ALLOC_DELTA;
            ptr = realloc(buf, newsize);
            if (NULL == ptr) {
                log_warn("tape: Failed to grow file buffer: '%s'", fn);
                e = TAPE_E_MALLOC;
                break;
            }
            alloc = newsize;
            buf = ptr;
        }
        num_read = fread (buf+pos, 1, chunk, f);
        if (ferror(f) || (num_read < 0)) {
            log_warn("tape: Stream error reading file '%s': %s", fn, strerror(errno));
            e = TAPE_E_FREAD;
            break;
        }
        pos += (uint32_t) (0x7fffffff & num_read);
    }

    fclose(f);

    if (TAPE_E_OK != e) {
        free(buf);
        return e;
    }

    /* TOHv3.2: avoid attempting decompression, if uncompressed UEF is detected */
    e = uef_detect_magic (buf, pos, fn, 0); /* 0 = quiet, no errors */
    if (TAPE_E_OK == e) { decompress = 0; }
    e = TAPE_E_OK; /* trap errors */

    if (decompress) {
        e = tape_decompress (&buf2, &buf2_len, buf, pos & 0x7fffffff);
        free(buf);
        if (TAPE_E_OK != e) { return e; }
        log_info("tape_decompress: %u -> %u\n", (uint32_t) (pos & 0x7fffffff), buf2_len);
        pos = buf2_len;
        buf = buf2;
    }

    *buf_out = buf;
    *len_out = pos;

    return TAPE_E_OK;

}

#define UNZIP_CHUNK 256
#define DECOMP_DELTA (100 * 1024)

int tape_decompress (uint8_t **out, uint32_t *len_out, uint8_t *in, uint32_t len_in) {

    /* if this returns E_OK, then 'in' will have been invalidated;
     * otherwise, 'in' is still valid, and must be freed by the caller. */

    uint8_t buf[UNZIP_CHUNK];
    size_t alloc;
    z_stream strm;
    uint32_t pos;

    *out = NULL;
    strm.zalloc   = Z_NULL;
    strm.zfree    = Z_NULL;
    strm.opaque   = Z_NULL;
    strm.next_in  = (unsigned char *) in;
    strm.avail_in = len_in;

    /* OR 32 allows us to decompress both zlib (for CSW)
       and gzip (for UEF and TIBETZ): */
    if ( inflateInit2 (&strm, 15 | 32) < 0 ) {
        log_warn("tape: could not decompress data; zlib init failed.");
        return TAPE_E_ZLIB_INIT;
    }

    alloc = 0;
    pos = 0;

    do {

        int zerr;
        uint32_t piece_len;
        uint32_t newsize;
        uint8_t *ptr;

        strm.avail_out = UNZIP_CHUNK;
        strm.next_out  = buf;

        zerr = inflate (&strm, Z_NO_FLUSH);

        switch (zerr) {
            case Z_OK:
            case Z_STREAM_END:
            case Z_BUF_ERROR:
                break;
            default:
                inflateEnd (&strm);
                log_warn ("tape: could not decompress data; zlib code %d", zerr);
                if (NULL != *out) { free(*out); }
                *out = NULL;
                return TAPE_E_ZLIB_DECOMPRESS;
        }

        piece_len = (UNZIP_CHUNK - strm.avail_out);

        ptr = NULL;

        if ((piece_len + pos) >= alloc) {
            newsize = piece_len + pos + DECOMP_DELTA;
            /* prevent shenanigans */
            if (newsize >= TAPE_MAX_DECOMPRESSED_LEN) {
                log_warn ("tape: decompressed size is too large\n");
                inflateEnd (&strm);
                if (*out != NULL) { free(*out); }
                *out = NULL;
                return TAPE_E_DECOMPRESSED_TOO_LARGE;
            }
            ptr = realloc (*out, newsize);
            if (NULL == ptr)  {
                log_warn ("tape: could not decompress data; realloc failed\n");
                inflateEnd (&strm);
                if (*out != NULL) { free(*out); }
                *out = NULL;
                return TAPE_E_MALLOC;
            }
            *out = ptr;
            alloc = newsize;
        }

        memcpy (*out + pos, buf, piece_len);
        pos += piece_len;

    } while (strm.avail_out == 0);

    inflateEnd (&strm);

    *len_out = pos;

    return TAPE_E_OK;

}


#include "tapewrite.h"


int tape_generate_and_save_output_file (tape_state_t *ts, /* TOHv4.2: pass this in */
                                        uint8_t filetype_bits,
                                        uint8_t silence112,
                                        char *path,
                                        uint8_t compress,
                                        ACIA *acia_or_null) {

    char *tape_op_buf, *bufz;
    FILE *f;
    int e;
    size_t z, tape_op_buf_len, bufz_len;
    uint8_t tibet, uef, csw;
    int32_t duration; /* TOHv4.3 */

    e = TAPE_E_OK;
    tape_op_buf = NULL;
    f = NULL;
    tape_op_buf_len = 0;
    bufz = NULL;
    bufz_len = 0;

    e = tape_get_duration_1200ths(ts, &duration); /* TOHv4.3 */
    if (TAPE_E_OK != e) { return e; }

    /* make sure any pending pieces are appended before saving */
    e = tape_flush_pending_piece (ts, acia_or_null, silence112, true);
    if (TAPE_E_OK != e) { return e; }

    tibet = (TAPE_FILETYPE_BITS_TIBET == filetype_bits);
    uef   = (TAPE_FILETYPE_BITS_UEF   == filetype_bits);
    csw   = (TAPE_FILETYPE_BITS_CSW   == filetype_bits);

    do {

        if (tibet) {
            e = tibet_build_output (&(ts->tibet),
                                    &tape_op_buf,
                                    &tape_op_buf_len);
        } else if (uef) {
            e = uef_build_output (&(ts->uef),
                                  &tape_op_buf,
                                  &tape_op_buf_len);
        } else if (csw) {
            e = csw_build_output (&(ts->csw),
                                  compress,
                                  &tape_op_buf,
                                  &tape_op_buf_len);
        } else {
            log_warn("tape: write: BUG: state failure");
            e = TAPE_E_BUG;
        }

        if (TAPE_E_OK != e) { break; }

        if (NULL == tape_op_buf) {
            log_warn("tape: write: BUG: generated tape output is NULL!");
            e = TAPE_E_BUG;
        } else if (0 == tape_op_buf_len) {
            log_warn("tape: write: BUG: generated tape output has zero length!");
            e = TAPE_E_BUG;
        }

        if (TAPE_E_OK != e) { break; }

        if (compress && (tibet || uef)) {
            /* TIBET and UEF are just gzipped */
            e = tape_zlib_compress (tape_op_buf,
                                    tape_op_buf_len,
                                    true, /* use gzip encoding, boy howdy */
                                    &bufz,
                                    &bufz_len);
            if (TAPE_E_OK != e) { break; }
            /* transparently replace original buffer */
            free(tape_op_buf);
            tape_op_buf     = bufz;
            tape_op_buf_len = bufz_len;
        }

        f = fopen(path, "wb");

        if (NULL == f) {
            log_warn("tape: could not open file for saving: %s", path);
            e = TAPE_E_SAVE_FOPEN;
            break;
        }

        if ( (NULL != tape_op_buf) && (TAPE_E_OK == e) ) {
            z = fwrite(tape_op_buf, tape_op_buf_len, 1, f);
            if (z != 1) {
                log_warn("tape: fwrite failed saving to file: %s", path);
                e = TAPE_E_SAVE_FWRITE;
            } else {
                log_info("tape: saved: %s", path);
            }
        }

    } while (0);

    if (tape_op_buf != NULL) {
        free(tape_op_buf);
        tape_op_buf = NULL;
    }

    if (NULL != f) {
        fclose(f);
    }

    if (TAPE_E_OK != e) {
        log_warn("tape: failed to save output file (code %u)\n", e);
    }

    return e;

}


int tape_zlib_compress (char   * const source_c,
                        size_t   const srclen,
                        bool     const use_gzip_encoding,
                        char  ** const dest_out,
                        size_t * const destlen_out) {

    int ret, flush;
    z_stream strm;
    size_t alloced;
    uint8_t *source;

    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree  = Z_NULL;
    strm.opaque = Z_NULL;

    alloced = 0;

    source = (uint8_t *) source_c;

    /* TOHv3.2: added refusal to compress empty buffer */
    if ((0 == srclen) || (srclen > 0x7fffffff)) {
        log_warn("tape: write: compress: BUG: srclen is insane (%zu). Aborting.", srclen);
        return TAPE_E_BUG;
    }

    if (use_gzip_encoding) {
        ret = deflateInit2 (&strm,
                            Z_BEST_COMPRESSION, /* Z_DEFAULT_COMPRESSION */
                            Z_DEFLATED,
                            15 + 16, /* windowbits 15, +16 for gzip encoding */
                            8,
                            Z_DEFAULT_STRATEGY);
    } else {
        ret = deflateInit  (&strm, Z_BEST_COMPRESSION);
    }
    if (ret != Z_OK) {
        return TAPE_E_SAVE_ZLIB_INIT;
    }

    /* compress until end of file */
    strm.avail_in = 0x7fffffff & srclen;
    flush = Z_FINISH;

    strm.next_in = source;

    /* run deflate() on input until output buffer not full, finish
       compression if all of source has been read in */
    do {

        char *dest2;

        if (alloced <= *destlen_out) { /* realloc if no space left */
            dest2 = realloc(*dest_out, alloced = (*destlen_out ? (*destlen_out * 2) : srclen));
            if (NULL == dest2) {
                log_warn("tape: write: compress: Failed to grow zlib buf (newsize %zu).", *destlen_out);
                deflateEnd(&strm);
                /* TOHv3.2: avoid free(NULL) */
                if (*dest_out != NULL) { free(*dest_out); }
                *dest_out = NULL;
                return TAPE_E_MALLOC;
            }
            *dest_out = dest2;
        }
        strm.avail_out = 0x7fffffff & (alloced - *destlen_out);   /* bytes available in output buffer */
        strm.next_out = (uint8_t *) (*dest_out + *destlen_out);   /* current offset in output buffer */
        ret = deflate(&strm, flush);                              /* no bad return value */
        *destlen_out += (alloced - *destlen_out) - strm.avail_out;

    } while (strm.avail_out == 0);

    /* clean up */
    deflateEnd(&strm);

    if ( ( strm.avail_in != 0 ) || ( ret != Z_STREAM_END ) ) {     /* all input will be used */
        if (strm.avail_in != 0) {
            log_warn("tape: write: compress: zlib compression failed: strm.avail_in != 0.");
        } else {
            log_warn("tape: write: compress: zlib compression failed (code %u).", ret);
        }
        free(*dest_out);
        *dest_out = NULL;
        return TAPE_E_SAVE_ZLIB_COMPRESS;
    }

    return TAPE_E_OK;

}

#include "tapenoise.h"

int tape_save_on_shutdown (tape_state_t *ts, /* TOHv4.2: now passed in */
                           uint8_t record_is_pressed,
                           uint8_t *our_filetype_bits_inout, /* the MULTIPLE types we have available */
                           uint8_t silence112,
                           uint8_t wav_use_phase_shift,
                           tape_shutdown_save_t *c) {        /* c->filetype_bits is the SINGLE type requested by the user */

    int e;
    tape_interval_t elapsed;

    if (NULL == c->filename) { return TAPE_E_OK; }

    e = tape_wp_init_blank_tape_if_needed (our_filetype_bits_inout);
    if (TAPE_E_OK != e) { return e; }

    /* TOHv4-rc2: add an origin chunk if there isn't one */
    /* FIXME: this code should be on uef.c */
    if ((c->filetype_bits & TAPE_FILETYPE_BITS_UEF) && (0 == ts->uef.num_chunks) && record_is_pressed) {
        elapsed.start_1200ths = 0;
        elapsed.pos_1200ths = 0;
        e = uef_store_chunk(&(ts->uef),
                            (uint8_t *) VERSION_STR,
                            0, /* origin, chunk &0000 */
                            strlen(VERSION_STR)+1, /* include the \0 */
                            0,  /* TOHv4.2: dummy offset */
                            0,
                            0,
                            0,
                            &elapsed);
        if (TAPE_E_OK != e) { return e; }
    }

    if (    ((TAPE_FILETYPE_BITS_WAV==c->filetype_bits) && (*our_filetype_bits_inout != TAPE_FILETYPE_BITS_NONE))
         || TAPE_FILETYPE_BITS_NONE != (*our_filetype_bits_inout & c->filetype_bits)) {
        if (c->filetype_bits & TAPE_FILETYPE_BITS_WAV) { /* TOHv4.2 */
            e = tapenoise_write_wav(ts, c->filename, wav_use_phase_shift);
        } else {
            e = tape_generate_and_save_output_file (ts, /* TOHv4.2: now passed in */
                                                    c->filetype_bits,
                                                    silence112,
                                                    c->filename,
                                                    c->do_compress,
                                                    NULL);
        }
        if (TAPE_E_OK == e) {
            log_info("tape: -tapesave: saved file: %s", c->filename);
        } else {
            log_warn("tape: -tapesave: error saving file: %s", c->filename);
        }
    } else {
        log_warn ("tape: -tapesave error: data does not exist in desired format (have &%x, want &%x)",
                  *our_filetype_bits_inout, c->filetype_bits);
    }
    return TAPE_E_OK;
}
