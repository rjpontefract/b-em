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

#ifndef __INC_UEF_H
#define __INC_UEF_H

#include "tape2.h"
#include "acia.h" /* for serial_framing stuff */
#include "tapeseek.h"

/* maximum number of metadata chunks that will be stored in between
 * actual bit-containing chunks */
#define UEF_MAX_METADATA 128

#define TAPE_UEF_MAGIC "UEF File!"

/* Impose arbitrary limit. In fact each list of global chunks
 * (origins, instructions, inlay scans etc.) is dynamically
 * allocated, so can be any size. See uef_parse_global_chunks(). */
#define TAPE_UEF_MAX_GLOBAL_CHUNKS       128



typedef struct uef_chunk_s {

    uint16_t type;
    uint8_t *data;
    uint32_t len;
    uint32_t offset;               /* TOHv4.2 */

    /* for bureaucratic use */
    uint32_t alloc;
    uint32_t num_data_bytes_written;
    tape_interval_t elapsed;   /* TOHv4.3 */
    /* former bitsrc fields moved onto chunk in TOHv4.3;
       express leader/silence durations, and chunk 114's num. cycles */
    uint32_t nodata_total_pre_2400ths;   /* chunk 110, 111, 112, 116 */
    uint32_t nodata_total_post_2400ths;  /* chunk 111 only */
    uint32_t chunk_114_total_cycs;       /* 24-bit value, &114's first 3 bytes */

} uef_chunk_t;

typedef struct uef_rom_hint_s {
    uint8_t *data;
    uint32_t len;
} uef_rom_hint_t;

typedef struct uef_origin_s {
    /* careful: may not be null-terminated */
    char *utf8;
    uint32_t len;
} uef_origin_t;

typedef struct uef_tape_set_info_s {
    /* chunk &130 */
    uint8_t vocabulary;   /* 0-4 */
    uint8_t num_tapes;    /* 1-127 */
    uint8_t num_channels; /* 1-255 */
} uef_tape_set_info_t;

typedef struct uef_start_of_tape_side_s {
    /* chunk &131; a prior chunk &130 sets the limits for this */
    /* 0-126 */
    uint8_t tape_id;
    uint8_t is_side_B;
    /* 0-254, although expect 0 and 1 to be L and R: */
    uint8_t channel_id; 
    /* NOTE: properly malloced and null-terminated;
     * doesn't just point into uef data, so it MUST BE FREED
     * once done with: */
    char *description; 
} uef_start_of_tape_side_t;

/* UNION */
typedef union uef_meta_u {
    uint16_t phase; /* chunk &115 */
    /* NOTE: properly malloced and null-terminated;
     * doesn't just point into uef data, so it MUST BE FREED
     * once done with: */
    char *position_marker; /* chunk &120 */
    uef_tape_set_info_t tape_set_info; /* chunk &130 */
    /* similarly, this must be destroyed once it's done with: */
    uef_start_of_tape_side_t start_of_tape_side; /* chunk &131 */
    uint16_t baud; /* NEW: chunk &117 */
} uef_meta_u_t;

typedef struct uef_meta_s {
    /* the generating chunk type.
     * currently supported:
     * &115 -- phase change
     * &117 -- baud rate
     * &120 -- position marker
     * &130 -- tape set info 
     * &131 -- start of tape side */
    uint16_t type;
    uef_meta_u_t data;
    uint8_t is_valid;
} uef_meta_t;

typedef struct uef_instructions_s {
    /* careful: may not be null-terminated: */
    char *utf8;
    uint32_t len;
} uef_instructions_t;

typedef struct uef_inlay_scan_s {
    uint16_t w;
    uint16_t h;
    uint8_t bpp;
    bool grey;
    uint32_t body_len;
    char *body;    /* points into UEF chunk */
    char *palette; /* points into UEF chunk */
} uef_inlay_scan_t;

typedef struct uef_globals_s {

    /* Note: Fields on globals just point into uef->data. Some
     * chunk types are only allowed once, in which case they have
     * a "have_..." flag to indicate that they have been found;
     * there is then a pointer for that chunk type that points into
     * uef->data. If these chunks occur multiple times, our strategy
     * will just be to ignore later ones.
     * 
     * Many global chunk types, though, may validly occur multiple
     * times. In this case, there will be a malloced list whose
     * elements point into multiple points in uef->data.
     * 
     * These lists of multiple entries will need to be freed when
     * the UEF is done. (None of the data that the lists or fields
     * pointed to should be freed.)
     */
     
    /* careful with these: they may not be null-terminated: */
    uef_origin_t *origins; /* &0 */
    uint32_t num_origins;
    
    /* careful with this: it may not be null-terminated: */
    uint32_t num_instructions; /* &1 */
    uef_instructions_t *instructions;
    
    /* raw chunk 3, no parsing done on it yet: */
    uint32_t num_inlay_scans;  /* &3 */ 
    uef_inlay_scan_t *inlay_scans;
    
    uint32_t num_target_machines; /* &5 */
    /* values; one byte; not pointers into UEF data;
     * note two independent nybbles (see UEF spec) */
    uint8_t *target_machines;
    
    uint8_t have_bit_mux; /* &6 */
    uint8_t bit_mux_info; /* value; one byte; not a pointer */
    
    /* raw chunks, no parsing done yet */
    uint8_t have_extra_palette;   /* &7 */
    uint8_t *extra_palette;
    uint32_t extra_palette_len;
    
    /* raw chunk, no parsing done yet */
    uef_rom_hint_t *rom_hints; /* &8 */
    uint32_t num_rom_hints;
    
    /* careful with this: it may not be null-terminated: */
    uint8_t have_short_title; 
    char *short_title;  /* &9 */
    uint32_t short_title_len;
    
    uint8_t have_visible_area;
    uint8_t *visible_area; /* &A */
    
    /* pulled out of origin chunk: */
    uint8_t have_makeuef_version;
    int makeuef_version_major;
    int makeuef_version_minor;
    
} uef_globals_t;


typedef struct uef_bitsource_s {

    /* This is going to work by having a local, pre-framed buffer which
     * contains some 1/1200s tones. When a tone is needed, we'll shift
     * a bit out of the buffer and send it to the ACIA.
     * 
     * When the buffer is empty, we get more data from the source chunk
     * and re-fill the pre-framed buffer.
     * 
     * For chunks &100 and &104, we take 7 or 8 bits out of the source
     * chunk, frame them with start, stop and parity bits, and place
     * either 10 or 11 1200th-tones into the pre-framed buffer (a.k.a.
     * "reservoir"). If 300 baud is selected with chunk &117, this
     * becomes 40 or 44 1200th-tones. Note that the 300 baud setting
     * of the ACIA has absolutely no effect on this; it is purely
     * decided by UEF chunks.
     * 
     * For chunk &102, the framing is explicit in the chunk, so we can
     * just take 8 bits out of the chunk and place them directly into
     * the reservoir (or quadruple those up to 32 bits, if at 300 baud).
     * 
     * All the above chunk types can take data from the source chunk
     * one byte at a time -- no sub-byte resolution is needed at source.
     * 
     * This is not true of chunk &114. Chunk &114 contains cycles, not
     * bits. At 1200 baud, an output 1-bit is two source bits. An
     * output 0-bit is one source bit. At 300 baud, an output 1-bit
     * is eight source bits, and a 0-bit is four source bits. For this
     * chunk type, we will take 1 to 8 bits from the source and place
     * just a single 1200th-tone into the reservoir. This will
     * necessitate a source sub-byte position, which we don't need for
     * the other chunk types. Chunk &114 uniquely does not care about
     * the currently-selected UEF baud rate.
     * 
     * We also have the other chunk types that contain either leader,
     * gap, or <leader + &AA + leader>. These (&110, &111, &112, &116)
     * don't have any source data. We will mostly just be supplying
     * a single bit (i.e. two 2400 Hz cycles) to the reservoir in these
     * cases. Chunk &111 (leader + dummy byte) will send a single
     * 1200th-tone to the reservoir during the pre-leader and
     * post-leader sections; the actual dummy byte will be placed as
     * 8N1 into the reservoir, so that part only will load 10 bits. */
     
    /* Complicated, yes? This is why you should use TIBET instead. */
     
    uint8_t silence;       /* for gaps; one bit's worth of silence */

    uint64_t reservoir_1200ths;    /* atom pairs on their way to the ACIA */
    int8_t reservoir_len;  /* total atom pairs in value */
    int8_t reservoir_pos;  /* num atom pairs already sent to ACIA */

    uint32_t src_byte_pos;      /* chunk 100, 102, 104, 114 */
    
    /* lengths of current leader or gap: */
    uint32_t nodata_consumed_pre_2400ths;  /* chunk 110, 111, 112, 116:  */
    uint32_t nodata_consumed_post_2400ths; /* chunk 111 only */
    
    uint8_t chunk_111_state;    /* 0 = pre-leader, 1 = &AA, 2 = post-leader */
    
    uint32_t chunk_114_src_bit_pos;   /* chunk 114 only, 0 to 7 */
    uint32_t chunk_114_consumed_cycs; /* count of total consumed cycles */
    char chunk_114_pulsewaves[2];     /* &114 bytes 4 & 5, 'P' / 'W' */
    
    /*
     * Here follows the framing for the *UEF decoder*.
     * 
     * This is *not* the same thing as the framing that is currently
     * programmed into the ACIA. We *do not* allow metadata in a UEF
     * file to go around merrily reprogramming the ACIA. That's just
     * nonsense.
     * 
     * 8N1-type framing values here are derived from the start of &104
     * chunks, and used to form the correct bitstream to send to the
     * ACIA for that chunk type only. There is also the baud300 flag,
     * which will be set by an instance of chunk &117. This one is used
     * for all data chunks.
     * 
     * Chunk &102 encodes bits, not 1/1200-second tones. At
     * 1200 baud, these are the same thing, but at 300 baud, they are
     * not.
     * 
     * This means that even if there were widespread implementation of
     * UEF chunk &102, there is still no guaranteed way to take an
     * arbitrary stream from a tape and encode it into a UEF file
     * without first having to perform some reverse-engineering of its
     * various framings. Viper's Ultron is an example; it has four
     * blocks at 8N1/300. The agent that builds the UEF needs to be
     * aware that these blocks are at 300 baud, or it will incorrectly
     * populate the &102 chunk with quadrupled bits, i.e. "1111"
     * instead of "1", and "0000" instead of "0". The UEF file would
     * also need to contain some &117 chunks to inform the UEF decoder
     * that 300 baud needs to be selected for the decoding of that &102
     * chunk.
     * 
     * It is stupid: The tape contains "1111"; chunk &102 contains "1";
     * chunk &117 tells the UEF decoder that 300 baud is selected;
     * the UEF decoder sends "1111" to the ACIA, and we are finally
     * back where we started!
     * 
     * Only chunk &114 permits
     * a direct representation of *cycles* on the tape, not *bits*, and
     * it (very generously) uses a 24-bit number to denote the number
     * of cycles in the squawk, making it easily expansive enough to
     * contain any sensible amount of data. You can get about 800K of
     * Beeb data into a single &114 chunk:
     * 
     * 2^24 cycles / 2 cycs/bit (worst case) = 2^23 bits;
     * 2^23 bits / 10 bits/frame = 839K.
     */

    serial_framing_t framing;

} uef_bitsource_t;


typedef struct uef_state_s {

    /* global data */
    uint8_t version_major;
    uint8_t version_minor;
    
    /* origin chunks, etc., which apply to entire file: */
    uef_globals_t globals;
    
    int32_t cur_chunk;
    uef_bitsource_t bitsrc;
    
    uef_chunk_t *chunks;
    int32_t num_chunks;
    int32_t num_chunks_alloc;
    
    uint8_t reverse_even_and_odd_parity;
    
} uef_state_t;

/* TOHv4.2 */
typedef struct uef_unicode_string {
  uint32_t *text;
  size_t len;
  size_t alloc;
} uef_unicode_string_t;

void uef_close(void);

/* void uef_poll(void); */

void uef_findfilenames(void);

void uef_clear_chunk (uef_chunk_t * const c); /* TOHv4.3 */

int uef_get_elapsed_1200ths (const uef_state_t * const u,
                             int32_t * const time_1200ths_out);

int uef_read_1200th (uef_state_t * const u,
                     char * const out_1200th_or_null,
                     uef_meta_t * const metadata_list, /* caller must call uef_metadata_list_finish() */
                     bool const initial_scan,
                     uint32_t * const metadata_fill_out,
                     /* TOHv4.3: usually returns -1, but may return accurate timestamp: */
                     int32_t * const updated_elapsed_1200ths_out_or_null);

void uef_finish (uef_state_t *u);

int uef_clone (uef_state_t * const out, const uef_state_t * const in);

void uef_rewind (uef_state_t * const u);

int uef_load_file (const char *fn, uef_state_t *uef);

bool uef_peek_eof (const uef_state_t * const uef);

void uef_force_eof (uef_state_t * const uef); /* TOHv4.3 */

void uef_metadata_list_finish (uef_meta_t metadata_list[UEF_MAX_METADATA],
                               uint32_t fill);

int uef_store_chunk (      uef_state_t     * const uef,
                     const uint8_t         * const buf,
                           uint16_t          const type,
                           uint32_t          const len,
                           uint32_t          const offset,                       /* TOHv4.2: add offset */
                           /* extra metadata: */
                           uint32_t          const nodata_pre_2400ths,           /* TOHv4.3: needed for chunks &110, 111, 112, 116 */
                           uint32_t          const nodata_post_2400ths,          /* TOHv4.3: needed for chunk &111 only */
                           uint32_t          const cycs_114,                     /* TOHv4.3: chunk &114's 24-bit length field */
                     const tape_interval_t * const elapsed);

int uef_append_byte_to_chunk (uef_chunk_t * const c, uint8_t const b);

int uef_build_output (uef_state_t *u, char **out, size_t *len_out);

int uef_detect_magic (      uint8_t   * const buf,
                            uint32_t    const buflen,
                      const char      * const filename,
                            bool        const show_errors);

int uef_ffwd_to_end (uef_state_t * const u);

void uef_scan_backwards_for_chunk_117 (uef_state_t *u,
                                       int32_t start_chunk_num, /* now passed in */
                                       int32_t *prevailing_nominal_baud_out);

int uef_get_117_payload_for_nominal_baud (int32_t const  nominal_baud,
                                          const char ** const payload_out);

int unicode_string_append (uef_unicode_string_t *u, char *buf); /* TOHv4.2 */

void unicode_string_finish (uef_unicode_string_t *u); /* TOHv4.2 */

int unicode_string_to_utf8 (uef_unicode_string_t *u,
                            char **utf8_out,
                            size_t *chars_out,
                            size_t *bytes_out,
                            uint8_t line_break); /* TOHv4.2 */

int uef_seek (uef_state_t *u,
              int32_t time_1200ths_wanted,
              int32_t *time_1200ths_actual_out) ; /* TOHv4.3 */

int uef_get_duration_1200ths (const uef_state_t * const uef,
                              int32_t * const duration_1200ths_out);  /* TOHv4.3 */

int uef_change_current_chunk (uef_state_t * const uef, int32_t const chunk_ix);

int uef_verify_timestamps (const uef_state_t * const uef); /* TOHv4.3-a4 */

#endif /* __INC_UEF_H */
