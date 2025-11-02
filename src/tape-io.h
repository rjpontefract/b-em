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

#ifndef __INC_TAPE_IO
#define __INC_TAPE_IO

typedef struct tape_shutdown_save_s {
    /* for -tapesave command-line option */
    char *filename; /* this memory belongs to argv[] */
    uint8_t filetype_bits;
    uint8_t do_compress;
} tape_shutdown_save_t;


#include "tape.h"

int tape_load(tape_state_t * const ts,
              tape_vars_t * const tv,
              const ALLEGRO_PATH * const fn);

int tape_load_file (const char *fn,
                    uint8_t decompress,
                    uint8_t **buf_out,
                    uint32_t *len_out);

int tape_decompress (uint8_t **out, uint32_t *len_out, uint8_t *in, uint32_t len_in);

int tape_generate_and_save_output_file (tape_state_t *ts, /* TOHv4.2: pass this in */
                                        uint8_t filetype_bits,
                                        uint8_t silence112,
                                        char *path,
                                        uint8_t compress,
                                        ACIA *acia_or_null);

int tape_zlib_compress (char   *         const source_c,
                        size_t           const srclen,
                        bool             const use_gzip_encoding,
                        char ** const dest_out,
                        size_t *         const destlen_out);

int tape_save_on_shutdown (tape_state_t *ts, /* TOHv4.2: now passed in */
                           uint8_t record_is_pressed,
                           uint8_t *our_filetype_bits_inout, /* the MULTIPLE types we have available */
                           uint8_t silence112,
                           uint8_t wav_use_phase_shift,
                           tape_shutdown_save_t *c);


#endif /* __INC_TAPE_IO */
