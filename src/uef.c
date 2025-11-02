/*B-em v2.2 by Tom Walker
  UEF/HQ-UEF tape support*/
  
/* - TOHv3, TOHv4 overhaul by 'Diminished' */

#ifndef TAPE_H_UEF
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

#define TAPE_H_UEF

#include <zlib.h>
#include <stdio.h>
#include <ctype.h>
#include "b-em.h"
#include "sysacia.h"
#include "csw.h"
#include "uef.h"
#include "tape.h"

#define UEF_REJECT_IF_TRUNCATED
#define UEF_REJECT_IF_UNKNOWN_CHUNK

/*#define UEF_WRITE_REJECT_NONSTD_BAUD*/


static int chunks_decode (uint8_t *buf,
                          uint32_t len,
                          uef_state_t *uef,
                          uint8_t reject_if_truncated,
                          uint8_t reject_if_unknown_chunk);
static int uef_parse_global_chunks (uef_state_t *u) ;
static int chunk_verify_length (const uef_chunk_t * const c);
static int chunks_verify_lengths (uef_chunk_t *chunks, int32_t num_chunks);
static int compute_chunk_102_data_len (uint32_t chunk_len,
                                       uint8_t data0,
                                       uint32_t *len_bytes_out,
                                       uint32_t *len_bits_out);
static uint8_t compute_parity_bit (uint8_t data, uint8_t num_data_bits, char parity);
static bool valid_chunktype (uint16_t const t);
static int chunk_114_next_cyc (uef_bitsource_t *src,
                               uint32_t chunk_len,
                               uint32_t total_cycs,
                               uint8_t *data,
                               uint8_t *cyc_out);
static int reload_reservoir (uef_state_t *u, uint8_t baud300);
static float uef_read_float (uint8_t b[4]);
static uint64_t reservoir_make_value (uint16_t v, uint8_t baud300);
static void init_bitsource (uef_bitsource_t * const src, int32_t const nominal_baud);
static int
pre_parse_metadata_chunk (uef_chunk_t *chunk,
                          /* zero means no prior chunk 130: */
                          uint8_t num_tapes_from_prior_chunk_130,
                          /* likewise: */
                          uint8_t num_channels_from_prior_chunk_130,
                          uef_meta_t *meta_out);
static int uef_verify_meta_chunks (uef_state_t *u);
static uint8_t is_chunk_spent (uef_chunk_t *c, uef_bitsource_t *src);
static void uef_metadata_item_finish (uef_meta_t *m);
static int verify_utf8 (char *utf8, size_t utf8_len);
static int encode_utf8 (uint32_t c, uint8_t *utf8_out, uint8_t *len_bytes_out); /* TOHv4.2 */
static int chunk_3_parse_header (const uef_chunk_t * const c,
                                 uint16_t * const w_out,
                                 uint16_t * const h_out,
                                 uint8_t * const bpp_out,
                                 bool * const grey_out,
                                 uint32_t * const palette_pos_out);

#define UEF_MAX_FLOAT_GAP 36000.0f  /* ten hours */
#define UEF_BASE_FREQ (TAPE_1200_HZ)


/* TOHv4 */
int uef_get_117_payload_for_nominal_baud (int32_t const nominal_baud,
                                          const char ** const payload_out) {
    *payload_out = NULL;
    if (1200 == nominal_baud) {
        *payload_out = "\xb0\x04";
    } else if (600 == nominal_baud) {
        *payload_out = "\x58\x02"; /* warning: violates UEF 0.10 */
    } else if (300 == nominal_baud) {
        *payload_out = "\x2c\x01";
    } else if (150 == nominal_baud) {
        *payload_out = "\x96\x00"; /* warning: violates UEF 0.10 */
    } else if (75 == nominal_baud) {
        *payload_out = "\x4b\x00"; /* warning: violates UEF 0.10 */
    }
    if ( ( nominal_baud != 300 ) && ( nominal_baud != 1200 ) ) {
#ifdef UEF_WRITE_REJECT_NONSTD_BAUD
        log_warn("tape: uef: WARNING: rejecting nonstandard baud rate %d\n",
                 nominal_baud);
        return TAPE_E_UEF_SAVE_NONSTD_BAUD;
#else
        log_warn("tape: uef: WARNING: nonstandard baud rate %d breaks UEF 0.10 spec\n",
                 nominal_baud);
#endif
    }
    return TAPE_E_OK;
}

static void init_bitsource (uef_bitsource_t * const src, int32_t const nominal_baud) {
    memset(src, 0, sizeof(uef_bitsource_t));
    src->framing.num_data_bits   = 8;
    src->framing.parity          = 'N';
    src->framing.num_stop_bits   = 1;
    src->framing.nominal_baud    = nominal_baud;
    src->chunk_114_pulsewaves[0] = '?';
    src->chunk_114_pulsewaves[1] = '?';
}

static int consider_chunk (uef_state_t * const u,
                           bool * const contains_1200ths_out) {

    uint8_t cb;
    uef_chunk_t *c;
    uint16_t type;
    float f;
    uef_bitsource_t *src;
    
    c = u->chunks + u->cur_chunk;
    type = c->type;
    src = &(u->bitsrc);

/*printf("consider_chunk, type &%x\n", type);*/

    /* New chunk, so reset the bit source. IMPORTANT: This preserves
     * the 300 baud setting; everything else is reset. */
    init_bitsource(src, src->framing.nominal_baud); /* TOHv4.3: reset_bitsource() is gone */
    
            /* data chunks: len must be > 0: */
    cb =    (((0x100==type)||(0x102==type)||(0x104==type)) && (c->len > 0))
            /* carrier tone: num cycles must be > 0: */
         || ((0x110==type) && (tape_read_u16(c->data) != 0))
            /* dummy byte in &111 means there is always data, so: */
         || (0x111==type)
            /* integer gap: must have length > 0 */
         || ((0x112==type) && (tape_read_u16(c->data) != 0))
            /* overhaul v2: enforce (gap > 1/1200) */
         || ((0x116==type) && (uef_read_float (c->data) > (1.0/1200.0)))
         || ((0x114==type) && (tape_read_u24(c->data) != 0));
    
    /* chunk lengths were validated by chunk_verify_length() earlier,
     * so we should be fine to go ahead and access the data without
     * further length checks. (Yes, this may be stupid design.) */

    if (0x102 == type) {
        u->bitsrc.src_byte_pos = 1; /* skip first byte */
    } else if (0x104 == type) {
        /* This is a data chunk with arbitrary framing, so establish
         * the framing parameters before we start on the data;
         * reminder once again that these are parameters for
         * _decoding_the_UEF_. We do NOT program the ACIA with them.
         * Only an idiot would do that.
         */
        /* validate the framing spec */
        if ((c->data[0] != 8) && (c->data[0] != 7)) {
            log_warn("uef: chunk &104: illegal number of data bits (%u, should be 7 or 8)", c->data[0]);
            return TAPE_E_UEF_0104_NUM_BITS;
        } else if ((c->data[1] != 'N') && (c->data[1] != 'O') && (c->data[1] != 'E')) {
            log_warn("uef: chunk &104: illegal parity (&%x, should be &45, &4e or &4f)", c->data[1]);
            return TAPE_E_UEF_0104_NUM_BITS;
        } else if ((c->data[2] != 1) && (c->data[2] != 2)) {
            log_warn("uef: chunk &104: illegal number of stop bits (%u, should be 1 or 2)", c->data[2]);
            return TAPE_E_UEF_0104_NUM_STOPS;
        }
        /* use this framing for this chunk: */
        u->bitsrc.framing.num_data_bits = c->data[0];
        u->bitsrc.framing.parity        = c->data[1];
        /* MakeUEF < 2.4 has E and O mixed up */
        if (u->reverse_even_and_odd_parity) {
            if ('O'==u->bitsrc.framing.parity) {
              u->bitsrc.framing.parity = 'E';
            } else if ('E' == u->bitsrc.framing.parity) {
              u->bitsrc.framing.parity = 'O';
            }
        }
        u->bitsrc.framing.num_stop_bits = c->data[2];
        u->bitsrc.src_byte_pos = 3; /* skip header */
    } else if (0x114 == type) {
        if (('P' != c->data[3]) && ('W' != c->data[3])) {
            log_warn("uef: chunk &114: illegal pulse/wave char (%x): wanted P or W", c->data[3]);
            return TAPE_E_UEF_0114_BAD_PULSEWAVE_1;
        }
        if (('P' != c->data[4]) && ('W' != c->data[4])) {
            log_warn("uef: chunk &114: illegal pulse/wave char (%x): wanted P or W", c->data[4]);
            return TAPE_E_UEF_0114_BAD_PULSEWAVE_2;
        }
        if (('P' == c->data[3]) && ('P' == c->data[4])) {
            log_warn("uef: chunk &114: illegal pulse/wave char combination %c, %c", c->data[3], c->data[4]);

            /* TOHv3.2: this restriction has been relaxed. Permit <P, P> sequence,
             * even though it appears to violate the 0.10 UEF specification, as it
             * has been observed in the wild:
             *
             * https://www.stardot.org.uk/forums/viewtopic.php?p=425576#p425576
             */

            /* return TAPE_E_UEF_0114_BAD_PULSEWAVE_COMBO; */

        }
        c->chunk_114_total_cycs    = tape_read_u24(c->data); /* TOHv4.3 */
        u->bitsrc.chunk_114_pulsewaves[0] = c->data[3];
        u->bitsrc.chunk_114_pulsewaves[1] = c->data[4];
        u->bitsrc.src_byte_pos = 5; /* skip header */
    } else if (0x110 == type) {
        /* carrier tone */
        c->nodata_total_pre_2400ths = tape_read_u16(c->data); /* TOHv4.3: now on chunk, not bitsrc */
    } else if (0x111==type) {
        /* carrier tone + &AA + carrier tone */
        c->nodata_total_pre_2400ths = tape_read_u16(c->data);      /* TOHv4.3: now on chunk, not bitsrc */
        c->nodata_total_post_2400ths = tape_read_u16(c->data + 2); /* TOHv4.3: now on chunk, not bitsrc */
    } else if (0x112==type) {
        /* integer gap; UEF spec is wrong */
        c->nodata_total_pre_2400ths = tape_read_u16(c->data); /* TOHv4.3: now on chunk, not bitsrc  */
    } else if (0x116 == type) {
        /* float gap; make sure it isn't negative! */
        f = uef_read_float (c->data);
        if (f < 0.0f) {
            log_warn("uef: chunk &116 contains negative float gap!");
            return TAPE_E_UEF_0116_NEGATIVE_GAP;
        }
        if (f > UEF_MAX_FLOAT_GAP) {
            log_warn("uef: chunk &116 contains excessive float gap!");
            return TAPE_E_UEF_0116_HUGE_GAP;
        }
        c->nodata_total_pre_2400ths = (uint32_t) (0.5f + (f * UEF_BASE_FREQ * 2.0f));  /* TOHv4.3: now on chunk, not bitsrc  */
    }
    
    if (cb && is_chunk_spent(c, src)) {
        /* sanity check: we also call is_chunk_spent()
           on the new chunk. It is possible to end up
           with a discrepancy where this function claims that
           a chunk can provide some tapetime, but the call to
           is_chunk_spent() in reload_reservoir() returns TRUE.
           One situation where this arose was where a chunk
           116 (float gap) had a gap length of zero, so did
           not actually resolve to any number of 1200ths.
           This condition is checked separately now (above)
           but even so this sanity check has been added
           in case it happens some other way.
        */
        log_warn("uef: chunk &%x that should have contained tapetime "
                 "is somehow empty; skipping", type);
        cb = 0; /* as you were */
    }
    
    *contains_1200ths_out = cb;
    
    return TAPE_E_OK;
    
}

static float uef_read_float (uint8_t b[4]) {
    /* FIXME: implement FLOAT-reading properly
       (i.e. platform-independently) */
    /* avoid type-punning */
    union { uint8_t b[4]; float f; } u;
    memcpy(u.b, b, 4);
    return u.f;
}


static void uef_metadata_item_finish (uef_meta_t *m) {
    if ((0x120 == m->type) && (m->data.position_marker != NULL)) {
        free(m->data.position_marker);
    } else if ((0x131 == m->type) && (m->data.start_of_tape_side.description != NULL)) {
        free(m->data.start_of_tape_side.description);
    }
}


void uef_metadata_list_finish (uef_meta_t metadata_list[UEF_MAX_METADATA],
                               uint32_t fill) {
    uint32_t i;
    for (i=0; i < fill; i++) {
        uef_metadata_item_finish(metadata_list + i);
    }
    memset(metadata_list, 0, sizeof(uef_meta_t) * UEF_MAX_METADATA);
}

/* called from uef_load_file(), to make an initial
 * verification pass across all chunks and find out
 * if it's worth persisting with this UEF file */
static int uef_verify_meta_chunks (uef_state_t *u) {
    uint32_t i;
    uef_tape_set_info_t tsi;
    memset (&tsi, 0, sizeof(uef_tape_set_info_t));
    for (i=0; i < u->num_chunks; i++) {
        uef_chunk_t *c;
        uef_meta_t m;
        int e;
        c = u->chunks + i;
        memset(&m, 0, sizeof(uef_meta_t));
        e = pre_parse_metadata_chunk (c, tsi.num_tapes, tsi.num_channels, &m);
        if (TAPE_E_OK != e) { return e; }
        if (m.is_valid && (0x130 == m.type)) {
            /* keep current working chunk 130 specification
             * (note that this chunk type doesn't contain any heap memory) */
            tsi = m.data.tape_set_info;
        }
        uef_metadata_item_finish(&m);
    }
    return TAPE_E_OK;
}


/* we will let the caller clean up metadata_list on error;
 * it's a tiny bit neater;
 * 
 * if chunk was not a metadata-type chunk, then meta_out->is_valid
 * will be 0; otherwise 1 */
static int
pre_parse_metadata_chunk (uef_chunk_t *chunk,
                          /* zero means no prior chunk 130: */
                          uint8_t num_tapes_from_prior_chunk_130,
                          /* likewise: */
                          uint8_t num_channels_from_prior_chunk_130,
                          uef_meta_t *meta_out) {
                      
    size_t desclen;
    
    memset(meta_out, 0, sizeof(uef_meta_t));
        
    /* once again, chunk lengths should have been verified by
     * chunk_verify_length() earlier */
    if (0x115 == chunk->type) { /* phase change */
        meta_out->data.phase = tape_read_u16(chunk->data);
        if (meta_out->data.phase > 360) {
            log_warn("uef: phase change: illegal value %u", meta_out->data.phase);
            return TAPE_E_UEF_0115_ILLEGAL;
        }
        meta_out->is_valid = 1;
    } else if (0x120 == chunk->type) { /* position marker text */
        meta_out->data.position_marker = malloc(1 + chunk->len);
        if (NULL == meta_out->data.position_marker) {
            log_warn("uef: could not allocate position marker metadata");
            //~ metadata_finish(metadata_list, *fill_inout);
            return TAPE_E_MALLOC;
        }
        /* null-terminate: */
        meta_out->data.position_marker[chunk->len] = '\0';
        memcpy(meta_out->data.position_marker,
               chunk->data,
               chunk->len);
        meta_out->is_valid = 1;
    } else if (0x130 == chunk->type) { /* tape set info */
        if (chunk->data[0] > 4) {
            log_warn("uef: tape set info: illegal vocabulary (max. 4): %u", chunk->data[0]);
            return TAPE_E_UEF_0130_VOCAB;
        }
        meta_out->data.tape_set_info.vocabulary   = chunk->data[0];
        if ((chunk->data[1] > 127) || (0 == chunk->data[1])) {
            log_warn("uef: tape set info: illegal number of tapes (1<=nt<=127): %u", chunk->data[1]);
            return TAPE_E_UEF_0130_NUM_TAPES;
        }
        /* this acts as a limit for future chunk &131s (will be
         * passed back into this function on future calls to it): */
        meta_out->data.tape_set_info.num_tapes    = chunk->data[1];
        if (0 == chunk->data[2]) {
            log_warn("uef: tape set info: illegal (zero) number of channels");
            return TAPE_E_UEF_0130_NUM_CHANNELS;
        }
        /* this acts as a limit for future chunk &131s (will be
         * passed back into this function on future calls to it): */
        meta_out->data.tape_set_info.num_channels = chunk->data[2];
        meta_out->is_valid = 1;
    } else if (0x131 == chunk->type) { /* start of tape side */
        if (127 == (chunk->data[0] & 0x7f)) {
            log_warn("uef: tape set info: bad tape ID %u", (chunk->data[0] & 0x7f));
            return TAPE_E_UEF_0131_TAPE_ID;
        } else if (    (num_tapes_from_prior_chunk_130 > 0)
                    && ((chunk->data[0] & 0x7f) >= num_tapes_from_prior_chunk_130)) {
            log_warn("uef: tape set info: tape ID exceeds prior max.: %u vs. %u",
                     (chunk->data[0] & 0x7f), num_tapes_from_prior_chunk_130 - 1);
            return TAPE_E_UEF_0131_TAPE_ID_130_LIMIT;
        }
        meta_out->data.start_of_tape_side.tape_id     = chunk->data[0] & 0x7f;
        meta_out->data.start_of_tape_side.is_side_B   = (chunk->data[0] & 0x80) ? 1 : 0;
        if (0xff == chunk->data[1]) {
            log_warn("uef: tape set info: bad channel ID 255");
            return TAPE_E_UEF_0131_CHANNEL_ID;
        } else if (    (num_channels_from_prior_chunk_130 > 0)
                    && (chunk->data[1] >= num_channels_from_prior_chunk_130)) {
            log_warn("uef: tape set info: tape ID exceeds prior max.: %u vs. %u",
                     chunk->data[1], num_channels_from_prior_chunk_130 - 1);
            return TAPE_E_UEF_0131_CHANNEL_ID_130_LIMIT;
        }
        meta_out->data.start_of_tape_side.channel_id  = chunk->data[1];
        meta_out->data.start_of_tape_side.description = malloc(1 + (chunk->len-2));
        if (NULL == meta_out->data.start_of_tape_side.description) {
            return TAPE_E_MALLOC;
        }
        meta_out->data.start_of_tape_side.description[chunk->len-2] = '\0';
        memcpy (meta_out->data.start_of_tape_side.description,
                chunk->data + 2,
                chunk->len  - 2);
        desclen = strlen(meta_out->data.start_of_tape_side.description);
        /* UEF spec constrains len to 255 chars max */
        if (desclen > 255) {
            log_warn("uef: tape set info: description exceeds 255 chars (%zu)",
                     desclen);
            return TAPE_E_UEF_0131_DESCRIPTION_LONG;
        }
        meta_out->is_valid = 1;
    } else if (0x117 == chunk->type) { /* NEW: baud rate */
        meta_out->data.baud = tape_read_u16(chunk->data);
        if ((meta_out->data.baud != 300) && (meta_out->data.baud != 1200)) {
            log_warn("uef: data encoding format change: bad baud %u",
                     meta_out->data.baud);
            return TAPE_E_UEF_0117_BAD_RATE;
        }
        meta_out->is_valid = 1;
    }
    
    if (meta_out->is_valid) {
        meta_out->type = chunk->type;
    }
    return TAPE_E_OK;
}



static uint8_t compute_parity_bit (uint8_t data, uint8_t num_data_bits, char parity) {

    uint8_t n, num_ones;
    
    for (n=0, num_ones = 0; n < num_data_bits; n++) {
        num_ones += (data & 1);
        data = (data >> 1) & 0x7f;
    }
    
    if (num_ones & 1) {
        /* have odd */
        if ('E' == parity) { /* want even */
            return 1;
        }
    } else {
        /* have even */
        if ('O' == parity) { /* want odd */
            return 1;
        }
    }
    
    return 0;
    
}


static int chunk_114_next_cyc (uef_bitsource_t *src,
                               uint32_t chunk_len,
                               uint32_t total_cycs,
                               uint8_t *data,
                               uint8_t *cyc_out) {
                            
    uint8_t v, b;

    /* Chunk is finished when all the cycles in the 24-bit value
     * from bytes 0-2 have been consumed. */
    if (src->chunk_114_consumed_cycs >= total_cycs) {
        return TAPE_E_UEF_CHUNK_SPENT;
    }
    
    /* This shouldn't happen, unless the 24-bit value was full of
     * lies and sin. */
    if (src->src_byte_pos >= chunk_len) {
        log_warn("uef: Chunk &114 number-of-cycles field is wrong? (&%x)", total_cycs);
        return TAPE_E_UEF_0114_BAD_NUM_CYCS;
    }
    
    v = data[src->src_byte_pos];
    
    /* get next cycle-bit; cycles are MSB first */
    b = (v >> (7 - src->chunk_114_src_bit_pos)) & 1;
    
    /* cycle is consumed: */
    (src->chunk_114_consumed_cycs)++;
    (src->chunk_114_src_bit_pos)++;
    
    *cyc_out = b;
    
    if (src->chunk_114_src_bit_pos >= 8) {
        (src->src_byte_pos)++;
        src->chunk_114_src_bit_pos = 0;
    }
    
    return TAPE_E_OK;
    
}


static uint8_t is_chunk_spent (uef_chunk_t *c, uef_bitsource_t *src) {
    uint16_t type;
    uint32_t bits102, bytes102;
    int e;
    type = c->type;
    if ((0x100==type)||(0x104==type)) {
        return (src->src_byte_pos >= c->len);
    } else if (0x102==type) {
        e = compute_chunk_102_data_len (c->len, c->data[0], &bytes102, &bits102);
        if (TAPE_E_OK != e) { return 1; }
        return (src->src_byte_pos - 1) >= bytes102;
    } else if (0x114==type) {
        return (src->chunk_114_consumed_cycs >= c->chunk_114_total_cycs);     /* TOHv4.3: totals now on chunk */
    } else if (0x111==type) {
        return (src->nodata_consumed_post_2400ths >= c->nodata_total_post_2400ths); /* TOHv4.3: totals now on chunk */
    } else if ((0x110==type)||(0x112==type)||(0x116==type)) {
        return (src->nodata_consumed_pre_2400ths >= c->nodata_total_pre_2400ths);   /* TOHv4.3: totals now on chunk */
    }
    /* other chunk types don't contain any bits */
    return 1;
}


/* Take a set of bits and turn them into a set of 1/1200s tones.
 * At 1200 baud, these will be the same thing, but at 300 baud,
 * one bit becomes four 1/1200s tones, represented by four
 * bits in the output uint64_t. */
static uint64_t reservoir_make_value (uint16_t v, uint8_t baud300) {
    uint64_t x, four;
    uint8_t n;
    if ( ! baud300 ) { return v; }
    for (n=0, x=0; n < 16; n++) {
        four = (((uint64_t) 15) << (n*4));
        x |= ((v>>n)&1) ? four : 0;
    }
    return x;
}

/* TOHv4.3 */
void uef_clear_chunk (uef_chunk_t * const c) {
    uint8_t *data;
    uint32_t alloc;
    /* preserve these */
    data = c->data;
    alloc = c->alloc;
    /* memset */
    memset(c, 0, sizeof(uef_chunk_t));
    /* restore them */
    c->data = data;
    c->alloc = alloc;
}


static int reload_reservoir (uef_state_t *u, uint8_t baud300) {

    int e;
    uef_bitsource_t *src;
    uint8_t v;
    uef_chunk_t *chunk;
    uint16_t pb;
    uint8_t cycs[8];
    uint32_t saved_consumed_cycs;
    uint32_t saved_src_bit_pos;
    uint32_t saved_src_byte_pos;
    uint16_t type;
    uint16_t nbits;
    uint32_t bits102, bytes102;
    
    /* cur_chunk starts at -1; force consideration of chunk 0 */
    if (u->cur_chunk < 0) {
        return TAPE_E_UEF_CHUNK_SPENT;
    }
    
    e = TAPE_E_OK;
    src = &(u->bitsrc);
    
    chunk = u->chunks + u->cur_chunk;
            
    if (u->cur_chunk >= u->num_chunks) {
        return TAPE_E_EOF;
    }
    
    /* Empty the reservoir. */
    src->reservoir_pos = 0;
    src->reservoir_len = 0;
    
    if (is_chunk_spent (chunk, src)) {
        return TAPE_E_UEF_CHUNK_SPENT;
    }
    
    type = chunk->type;

    /* Chunks &100, &102 and &104 are easy -- we'll just move one
     * more byte through the source data. This nets us one frame
     * for &100 and &104, and eight bits for chunk &102 (for 102, these
     * are explicit bits, so not a frame -- it's just a fragment of
     * the bitstream, which may include start, stop and parity
     * bits). */
    if ( (0x100 == type) || (0x104 == type) ) {
        /* get byte from input chunk and frame it */
        v = chunk->data[src->src_byte_pos];
        nbits = 1; /* start bit */
        src->reservoir_1200ths = (((uint16_t) v) << 1);
        nbits += src->framing.num_data_bits; /* data bits */
        /* parity */
        if (src->framing.parity != 'N') {
            pb = compute_parity_bit (v,
                                     src->framing.num_data_bits,
                                     src->framing.parity);
            pb <<= nbits;
            src->reservoir_1200ths |= pb; /* install parity bit */
            nbits++;
        }
        /* stop 1 */
        src->reservoir_1200ths |= (((uint64_t)1) << nbits);
        nbits++;
        if (2 == src->framing.num_stop_bits) {
            nbits++;
            src->reservoir_1200ths |= (((uint64_t) 1) << nbits);
        }
        src->reservoir_len = nbits;
        (src->src_byte_pos)++;
        
        if (baud300) {
            /* quadruple up the reservoir */
            src->reservoir_1200ths = reservoir_make_value (src->reservoir_1200ths, 1);
            src->reservoir_len *= 4;
        }
        
    } else if (0x102==type) {
        /* explicit bits */
        /* get byte from input chunk and don't frame it */
        e = compute_chunk_102_data_len (chunk->len, chunk->data[0], &bytes102, &bits102);
        if (TAPE_E_OK != e) { return e; }
        if ((src->src_byte_pos - 1) == (bytes102 - 1)) {
            /* final byte; may be incomplete */
            bits102 -= ((src->src_byte_pos - 1) * 8);
            src->reservoir_len = bits102;
        } else {
            src->reservoir_len = 8;
        }
        v = chunk->data[src->src_byte_pos];
        src->reservoir_1200ths = v & 0xff;
        (src->src_byte_pos)++;
        
        if (baud300) {
            /* quadruple up the reservoir */
            src->reservoir_1200ths = reservoir_make_value (src->reservoir_1200ths, 1);
            src->reservoir_len *= 4;
        }
        
    } else if (0x114==type) {
    
        /* Squawk, or maybe actually data. */
    
        do {

            /* Get the first cycle. It determines what we do next.
             * (src is updated with advanced bit/byte positions) */
            e = chunk_114_next_cyc (src, chunk->len, chunk->chunk_114_total_cycs, chunk->data, cycs + 0);
            if (TAPE_E_OK != e) { return e; }

            /* Look at the cycle and figure out how many more bits
             * we need to pull from the chunk to complete one output
             * bit. Remember, a 1200-baud 0-bit means one 1200 Hz cycle;
             * a 1200-baud 1-bit means two 2400 Hz cycles.
             * 
             * first cycle   baud rate   extra cycs needed   total cycs
             *   0             1200        0                   1
             *   1             1200        1                   2
             *   0              300        3                   4
             *   1              300        7                   8
             */
            
            /* Before pulling any extra cycle, back up the bitsource
             * state. We might need to revert it, if the output bit 
             * turns out to be ambiguous and we need to re-synchronise.
             */
             
            saved_consumed_cycs = src->chunk_114_consumed_cycs;
            saved_src_bit_pos   = src->chunk_114_src_bit_pos;
            saved_src_byte_pos  = src->src_byte_pos;
            
            /* Pull any cycle from the chunk that makes up the rest of
             * this output bit. If cycs[0] is a 2400 Hz cycle, then
             * we need to pull another cycle to make up the full
             * 1/1200. */
            if (cycs[0]) {
                e = chunk_114_next_cyc (src, chunk->len, chunk->chunk_114_total_cycs, chunk->data, cycs + 1);
                if (TAPE_E_OK != e) { return e; }
            }
            
            /* Now we have 1-2 cycles in cycs[]. Examine this data
             * and see how fidelitous the bit is. */
            if ((cycs[0]) && ( ! cycs[1])) {
                /* Bit is ambiguous. Restore the source state, throw out
                 * this cycle, and resynchronise on the next cycle. */
                src->chunk_114_consumed_cycs = saved_consumed_cycs;
                src->chunk_114_src_bit_pos   = saved_src_bit_pos;
                src->src_byte_pos            = saved_src_byte_pos;
            } else {
              break;
            }
            
        } while (1);
        
        /* For &114, we only do one 1200th at a time; place a single 1200th
         * into the reservoir. */
        src->reservoir_1200ths     = cycs[0] ? 1 : 0;
        src->reservoir_len = 1;
        
        /* 114 is cycle-explicit; so we don't do the quadrupling with 300 baud. */
        
    } else if (0x110 == type) {
    
        /* leader -- send one 1200th only */
    
        src->reservoir_1200ths = 0x1;
        src->reservoir_len = 1;
        (src->nodata_consumed_pre_2400ths) += 2;
        
        /* leader chunks count cycles, not bits or anything, so
         * they are unaffected by 300 baud. */
        
    } else if (0x111 == type) {
    
        /* leader + &AA + leader */
        if (0 == src->chunk_111_state) {
            /* pre-&AA leader */
            if (src->nodata_consumed_pre_2400ths >= chunk->nodata_total_pre_2400ths) {
                src->chunk_111_state = 1; /* advance state */
            } else {
                src->reservoir_1200ths = 0x1;
                src->reservoir_len = 1;
                (src->nodata_consumed_pre_2400ths)+=2; /* 2 cycs, 1 bit */
            }
        } else if (1 == src->chunk_111_state) {
            /* dummy byte; framed as 8N1 */
            src->reservoir_1200ths = 0x354;
            src->reservoir_len = 10;
            src->chunk_111_state = 2;  /* advance state */
        } else {
            /* post-&AA leader */
            src->reservoir_1200ths = 0x1;
            src->reservoir_len = 1;
            (src->nodata_consumed_post_2400ths) += 2; /* 2 cycs, 1 bit */
        }
        
        /* As above, leader chunks count cycles, not bits;
         * they are unaffected by 300 baud. The dummy byte is always
         * 1200 baud, because MOS always writes it that way. */
        
    } else if ((0x112==type)||(0x116==type)) {
        src->silence = 1; /* overrides reservoir */
        src->reservoir_len = 1;
        /* TOHv3.2: fixed doubled silent duration; now consume silence twice as fast */
        (src->nodata_consumed_pre_2400ths)+=2;
    }

    return TAPE_E_OK;
    
}

bool uef_peek_eof (const uef_state_t * const uef) {
    return (uef->cur_chunk >= uef->num_chunks);
}

/* TOHv4.3 */
void uef_force_eof (uef_state_t * const uef) {
    uef->cur_chunk = uef->num_chunks;
}

static int accrue_metadata_til_tapetime (uef_state_t * const u,
                                         bool const initial_scan,
                                         int32_t * const updated_elapsed_1200ths_out_or_null,
                                         uef_meta_t *metadata_list,
                                         uint32_t * const metadata_fill_out) {

    int e;
    bool chunk_contains_bits;
    
    chunk_contains_bits = false;
    e = TAPE_E_OK;

    /* loop to find a chunk that actually contains some bits,
       collecting up any intervening metadata chunks in the process */
    while ( (TAPE_E_OK == e) && ! chunk_contains_bits ) {
    
        uef_meta_t meta;
        tape_interval_t *t, *t_prev;
        int32_t now;

        (u->cur_chunk)++; /* initially goes -1 to 0 */

        if (u->cur_chunk >= u->num_chunks) {
            /*log_info("uef: EOF");*/ /* on EOF, metadata is NOT freed */
            return TAPE_E_EOF;
        }
        
        t = &(u->chunks[u->cur_chunk].elapsed);
        
        if (initial_scan) {
            t->pos_1200ths     = 0;
            t->sub_pos_4800ths = 0;
        }
        /* TOHv4.3: */
        if (u->cur_chunk < 1) {
            if (initial_scan) {
                t->start_1200ths = 0;
            }
            if (updated_elapsed_1200ths_out_or_null != NULL) {
                *updated_elapsed_1200ths_out_or_null = 0;
            }
        } else {
            t_prev = &(u->chunks[u->cur_chunk-1].elapsed);
            now = t_prev->start_1200ths + t_prev->pos_1200ths;
    /*printf("now = %d + %d\n", t_prev->start_1200ths, t_prev->pos_1200ths);*/
#ifdef BUILD_TAPE_SANITY
            /* has the chunk's start time previously been written?
               if so, make sure that our recalculated start time matches it */
            if ((t->start_1200ths != 0) && (t->start_1200ths != now)) {
                log_warn("tape: UEF: BUG: read 1200th: chunk #%d (type &%x); old chunk start time (%d) does not match new (%d)\n",
                         u->cur_chunk, u->chunks[u->cur_chunk].type, t->start_1200ths, now);
                return TAPE_E_BUG;
            }
#endif
            if (initial_scan) {
                t->start_1200ths = now;
            }
            if (updated_elapsed_1200ths_out_or_null != NULL) {
                *updated_elapsed_1200ths_out_or_null = now;
            }
        }
        chunk_contains_bits     = false;
        /* this ought to receive the tape set info from the previous
         * chunk &130 for validation -- but it ought to have been
         * checked already on load by uef_verify_meta_chunks(),
         * so I don't think it's really necessary to check it again;
         * => just pass 0,0 for the tape set info */
        e = pre_parse_metadata_chunk (u->chunks + u->cur_chunk, 0, 0, &meta);
        if (TAPE_E_OK != e) { return e; }
        if (meta.is_valid) { /* chunk is a metadata chunk, => no bits */
            /* we handle baud rate changes here and now */
            if (0x117 == meta.type) {
                u->bitsrc.framing.nominal_baud = meta.data.baud;
                if ( ! initial_scan ) { /* suppress initial scan logspam */
                    log_info("uef: &117: baud change on tape: %u\n", meta.data.baud);
                }
            }
            if (metadata_list != NULL) {
                /* there is some interesting metadata on this chunk,
                 * so bag it up and return it */
                if ( *metadata_fill_out >= UEF_MAX_METADATA)  {
                    log_warn ("uef: too many metadata chunks between data chunks");
                    return TAPE_E_UEF_TOO_MANY_METADATA_CHUNKS;
                }
                metadata_list[*metadata_fill_out] = meta;
                (*metadata_fill_out)++;
            }
        } else {
            e = consider_chunk (u, &chunk_contains_bits);
            if (TAPE_E_OK != e) { return e; }
        }
    } /* keep trying chunks until we get some bits */
    return TAPE_E_OK;
}


/* Non-data (metadata) chunks that are encountered in the course of
 * finding the next actual data chunk are returned in metadata_list.
 * Caller must offload all this metadata every time this function is
 * called; the next call to this function that advances the chunk will
 * wipe the contents of metadata_list.
 * Caller must also call metadata_finish() on metadata_list,
 * REGARDLESS OF WHETHER THIS FUNCTION SUCCEEDS OR FAILS.
 */
int uef_read_1200th (uef_state_t * const u,
                     char * const out_1200th_or_null,
                     uef_meta_t * const metadata_list, /* caller must call uef_metadata_list_finish() */
                     bool const initial_scan,
                     uint32_t * const metadata_fill_out,
                     /* TOHv4.3: usually returns -1, but may return accurate timestamp: */
                     int32_t * const updated_elapsed_1200ths_out_or_null) {

    /* TODO: this function really should output 'L' to denote
     * leader tone, rather than '1'. Currently the CSW code is
     * being used to convert the output of this function from
     * '1' to 'L' if leader is detected, but there's no reason
     * why 'L' shouldn't be outputted directly from here if we
     * know it came from a leader UEF chunk. */

    int e;
    uef_bitsource_t *src;
    uint16_t shift;

    if (NULL != metadata_fill_out) {
      *metadata_fill_out = 0;
    }

    if (NULL != out_1200th_or_null )                 { *out_1200th_or_null  = '\0';               }
    if (NULL != updated_elapsed_1200ths_out_or_null) { *updated_elapsed_1200ths_out_or_null = -1; }

    if (u->cur_chunk >= u->num_chunks) {
        /*log_info("uef: EOF");*/
        return TAPE_E_EOF; /* on EOF, metadata is NOT freed */
    }
    
    src = &(u->bitsrc);

    if (src->reservoir_pos >= src->reservoir_len) {
        /* reservoir empty, refill it */
        e = reload_reservoir (u,
                              /* NOT the baud setting in the ACIA!
                               * It's the 300 baud setting for the UEF!
                               * Not the same thing at all! */
                              (300 == u->bitsrc.framing.nominal_baud));
        if ((TAPE_E_OK != e) && (TAPE_E_UEF_CHUNK_SPENT != e)) { return e; }
        /* reload_reservoir() may fail with "chunk spent",
         * in which case we need to get a new chunk and try again: */
        if (TAPE_E_UEF_CHUNK_SPENT == e) {
            e = TAPE_E_OK;
            /* chunk spent! free any metadata from previous chunk */
            if (NULL != metadata_list) {
                uef_metadata_list_finish (metadata_list, *metadata_fill_out);
            }
            e = accrue_metadata_til_tapetime (u,
                                              initial_scan,
                                              updated_elapsed_1200ths_out_or_null,
                                              metadata_list,
                                              metadata_fill_out);
            if (TAPE_E_OK != e) { return e; }
            /* OK, new chunk with more bits, so try refilling the
             * reservoir again: */
            e = reload_reservoir (u,
                                  /* NOT the 300 baud setting in the ACIA!
                                   * It's the 300 baud setting for the UEF loader!
                                   * Not the same thing at all! */
                                  300 == u->bitsrc.framing.nominal_baud);
            /* if it fails again then give up */
            if (TAPE_E_OK != e) { return e; }
            
        } /* endif (new chunk needed) */
        
    } /* endif (reservoir empty) */

    /* Reservoir contains something; emit 1/1200th of a second
     * from the reservoir */
    
    /* src->silence (maybe set by above reload_reservoir call)
     * overrides the reservoir contents: */
    if (out_1200th_or_null != NULL) {
        if ( ! src->silence ) {
            shift = src->reservoir_pos;
            *out_1200th_or_null = (0x1 & (src->reservoir_1200ths >> shift)) ? '1' : '0';
        } else {
            *out_1200th_or_null = 'S';
        }
    }

    (src->reservoir_pos)++;

    /* TOHv4.3, for initial scan */
    if (initial_scan) {
        u->chunks[u->cur_chunk].elapsed.pos_1200ths++;
    }
    
    return TAPE_E_OK;

}

/* TOHv4.3 */
int uef_get_elapsed_1200ths (const uef_state_t * const u,
                             int32_t * const time_1200ths_out) {
    tape_interval_t elapsed;
    int e;
    *time_1200ths_out = -1;
    if (0==u->num_chunks) {
        return TAPE_E_OK;
    }
    if (u->cur_chunk < 0) { /* cur_chunk = -1 is legal */
        *time_1200ths_out = 0;
        return TAPE_E_OK;
    }
    if (u->cur_chunk >= u->num_chunks) {
        /* beyond end of tape -- just return the duration */
        e = uef_get_duration_1200ths(u, time_1200ths_out);
        return e;
    }
    elapsed = u->chunks[u->cur_chunk].elapsed;
    *time_1200ths_out = elapsed.start_1200ths + elapsed.pos_1200ths;
    return TAPE_E_OK;
}


/* TOHv4.3 */
int uef_get_duration_1200ths (const uef_state_t * const uef,
                              int32_t * const duration_1200ths_out) {

    tape_interval_t *et;

    *duration_1200ths_out = -1;

    if (0 == uef->num_chunks) { return TAPE_E_OK; }

    et = &(uef->chunks[uef->num_chunks - 1].elapsed);
    *duration_1200ths_out = et->start_1200ths + et->pos_1200ths;

    return TAPE_E_OK;

}


int uef_detect_magic (      uint8_t   * const buf,
                            uint32_t    const buflen,
                      const char      * const filename,
                            bool        const show_errors) {
    uint32_t magic_len;
    magic_len = 0xff & (1 + strlen(TAPE_UEF_MAGIC)); /* size_t -> uint32_t */
    if (buflen < (magic_len + 2)) {
        if (show_errors) {
            log_warn("uef: detect magic: file is too short: '%s'", filename);
        }
        return TAPE_E_UEF_BAD_HEADER;
    }
    if ( 0 != memcmp (TAPE_UEF_MAGIC, buf, magic_len) ) {
        if (show_errors) {
            log_warn("uef: detect magic: bad magic for file: '%s'", filename);
        }
        return TAPE_E_UEF_BAD_MAGIC;
    }
    return TAPE_E_OK;
}

static bool my_isdigit (char c) {
    return (c >= '0') && (c <= '9');
}

int uef_load_file (const char *fn, uef_state_t *uef) {

    size_t magic_len;
    uint32_t len;
    uint8_t *buf;
    int e;
    uint8_t reject_if_truncated;
    uint8_t reject_if_unknown_chunk;
    uint32_t n;

#ifdef UEF_REJECT_IF_TRUNCATED
    reject_if_truncated = 1;
#else
    reject_if_truncated = 0;
#endif

#ifdef UEF_REJECT_IF_UNKNOWN_CHUNK
    reject_if_unknown_chunk = 1;
#else
    reject_if_unknown_chunk = 0;
#endif

    uef_finish(uef);

    /* TOHv3.2: now, if tape_load_file() detects UEF magic at start of file,
       it will silently ignore the decompression flag supplied by the caller
       and skip decompression. This eliminates the stupid "load the whole
       file twice" bodge present in prior versions. */
    e = tape_load_file(fn, 1, &buf, &len);
    if (TAPE_E_OK != e) {
        log_warn("uef: could not load file: '%s'", fn);
        return e;
    }
    
    magic_len = 1 + strlen(TAPE_UEF_MAGIC);
    /*if (len < (magic_len + 2)) {
        log_warn("uef: file is too short: '%s'", fn);
        free(buf);
        return TAPE_E_UEF_BAD_HEADER;
    }
    if ( 0 != memcmp (MAGIC, buf, magic_len) ) { */
    
    if (uef_detect_magic (buf, len, fn, 1)) { /* 1 = show errors */
        free(buf);
        return TAPE_E_UEF_BAD_MAGIC;
    }
    
    uef->version_minor = buf[0 + magic_len];
    uef->version_major = buf[1 + magic_len];
    log_info("uef: header OK: version %u.%u: '%s'",
             uef->version_major, uef->version_minor, fn);

    do {
    
        e = chunks_decode(buf + magic_len + 2,
                          len - (magic_len + 2),
                          uef,
                          reject_if_truncated,
                          reject_if_unknown_chunk);
        free(buf);
        buf = NULL;
        if (TAPE_E_OK != e) { break; }
        
        e = chunks_verify_lengths (uef->chunks, uef->num_chunks);
        if (TAPE_E_OK != e) { break; }
        
        e = uef_parse_global_chunks(uef);
        if (TAPE_E_OK != e) { break; }
        
        e = uef_verify_meta_chunks(uef);
        if (TAPE_E_OK != e) { break; }
        
    } while (0);
    
    if (TAPE_E_OK != e) {
        uef_finish(uef);
        return e;
    }
        
    /*
     * Today's haiku:
     * 
     * Log origin chunks
     * Text may lack termination
     * We'll need a copy.
     */
    for (n=0; n < uef->globals.num_origins; n++) {
    
        char *min, *maj;
        char *s;
        uef_origin_t *o;
        size_t z, dp, a;
        int maj_i, min_i;
                
        o = uef->globals.origins + n;
        
/* memcpy(o->data, "MakeUEF V10.10.", o->len = 15); */

        s = malloc(1 + o->len);
        if (NULL == s) {
            uef_finish(uef);
            return TAPE_E_MALLOC;
        }
        
        s[o->len] = '\0';
        memcpy(s, o->utf8, o->len);
        
        len = 0x7fffffff & strlen(s);
        
        /* dispel curses */
        /* FIXME: log message assumes non-UTF8 */
        for (z=0; z < len; z++) {
            //if (!isprint(s[z])) {
            /* TOHv4.3-a2: bugfix for test "Chunk &0, origin: legal UTF-8:"
             * Don't use isprint() any more. */
            if ((s[z]&0x80) || (((unsigned char)s[z])<0x20) || ((unsigned char)s[z])>0x7e) {
                s[z]='?';
            }
        }
        
        log_info("uef: origins[%u]: \"%s\"", n, s);
        
        /* hunt for MakeUEF < 2.4, which has even and odd
         * parity mixed up for chunk &104. NOTE: we only record
         * the FIRST instance of any MakeUEF version origin chunk; if
         * multiples show up, later ones are ignored.
         * TODO: add some unit tests to probe this behaviour. */
        if ( (len >= 12) && (0 == strncmp (s, "MakeUEF V", 9) ) ) {
             
            if (uef->globals.have_makeuef_version) {
                log_warn("uef: multiple MakeUEF version origin chunks; ignoring later ones");
            } else {
                /* found MakeUEF version origin chunk */
                for (z=9, dp=0; z < len; z++) {
                    if ('.'==s[z]) {
                        dp = z;
                        break;
                    }
                }
                if ((dp > 9) && (dp < (len-1)))  { /* found decimal point */
                    /* null terminate DP */
                    s[dp] = '\0';
                    maj = s + 9;
                    min = s + dp + 1;
                    for (a=0; (a < strlen(maj)) && my_isdigit(maj[a]); a++);
                    if (a!=strlen(maj)) { continue; }
                    for (a=0; (a < strlen(min)) && my_isdigit(min[a]); a++);
                    if (0==a) { continue; }
                    maj_i = atoi(maj);
                    min_i = atoi(min);
                    uef->globals.have_makeuef_version = 1;
                    uef->globals.makeuef_version_major = maj_i;
                    uef->globals.makeuef_version_minor = min_i;
                    log_info("uef: MakeUEF detected: version %d.%d", maj_i, min_i);
                    uef->reverse_even_and_odd_parity = ((maj_i < 2) || ((2==maj_i)&&(min_i<4)));
                    if (uef->reverse_even_and_odd_parity) {
                        log_warn("uef: Work around MakeUEF < 2.4 (%d.%d) parity bug: swap chunk &104 even and odd", maj_i, min_i);
                    }
                }
            } /* endif first makeuef origin chunk */
        } /* endif makeuef origin chunk*/
        
        free(s);
        
    } /* next origin chunk */
    
    return e;
    
}

#define UEF_CHUNK_DELTA 1000 /* TOHv4.3: increased from 10 to 1000 */

static int chunks_decode (uint8_t *buf,
                          uint32_t len,
                          uef_state_t *uef,
                          uint8_t reject_if_truncated,
                          uint8_t reject_if_unknown_chunk) {

    uint32_t pos;
    uint32_t chunklen;
    tape_interval_t elapsed;
    
    chunklen = 0;
    
    elapsed.start_1200ths = 0;
    elapsed.pos_1200ths           = 0;
    elapsed.sub_pos_4800ths       = 0;
    
    if (NULL != uef->chunks) {
        free(uef->chunks);
        uef->chunks = NULL;
    }
    
    for (pos=0, uef->num_chunks=0, uef->num_chunks_alloc=0;
         pos < len;
         pos += (6 + chunklen)) {
         
        uint16_t type;
        int e;
        
        /* ensure type and chunklen are in-bounds */
        /* TOHv4.3-a3: relaxed from >= to >, allow zero-length chunks in principle */
        if ((pos + 6) > len) {
            log_warn("uef: chunk #%d: truncated chunk header\n", uef->num_chunks+1);
            if (reject_if_truncated) {
                uef_finish(uef);
                return TAPE_E_UEF_TRUNCATED;
            } else {
                break;
            }
        }

        type = tape_read_u16(buf + pos);
        chunklen = tape_read_u32(buf + pos + 2);

        if ( ! valid_chunktype(type) ) {
            log_warn("uef: unknown chunk type &%x", type);
            if (reject_if_unknown_chunk) {
                uef_finish(uef);
                return TAPE_E_UEF_UNKNOWN_CHUNK;
            } else {
                /* prevent this chunk from making it into the list */
                continue;
            }
        }
        
        /* ensure data is in-bounds */
        if ((pos + 6 + chunklen) > len) {
            log_warn("uef: chunk #%d: truncated chunk body (pos + 6 = &%x, chunklen = &%x, buflen = &%x)\n",
                     uef->num_chunks+1, pos + 6, chunklen, len);
            if (reject_if_truncated) {
                uef_finish(uef);
                return TAPE_E_UEF_TRUNCATED;
            } else {
                break;
            }
        }
        
        /* TOHv4.2: add offset */
        /* TOHv4.3: add elapsed fields (both 0 until initial scan runs)
                    new 3x cycs fields will be filled in later, on parse, so they're 0 here */
        e = uef_store_chunk (uef, buf + pos + 6, type, chunklen, pos, 0, 0, 0, &elapsed);
        if (TAPE_E_OK != e) { return e; }
        
    }

    // if (0==uef->num_chunks) {
    //     return TAPE_E_BUG;
    // }
    
    /*
    int x;
    for (x=0; x < uef->num_chunks; x++) {
        int z;
        uef_chunk_t *c;
        c = uef->chunks + x;
        printf("\n\ntype &%x len &%x\n\n", c->type, c->len);
        for (z=0; z < c->len; z++) {
            printf("%02x", c->data[z]);
        }
    }
    */
    
    return TAPE_E_OK;
    
}



static bool valid_chunktype (uint16_t const t) {
    return    (0==t)||(1==t)||(3==t)||(5==t)||(6==t)||(7==t)||(8==t)||(9==t)||(10==t)
           || (0x100==t)||(0x101==t)||(0x102==t)||(0x104==t)||(0x110==t)||(0x111==t)
           || (0x112==t)||(0x116==t)||(0x113==t)||(0x114==t)||(0x115==t)||(0x117==t)
           || (0x120==t)||(0x130==t)||(0x131==t);
}

/* integrity check */
int uef_verify_timestamps (const uef_state_t * const uef) {
    int32_t i;
    for (i=0; i < (uef->num_chunks - 1); i++) {
        uef_chunk_t *c1, *c2;
        tape_interval_t *iv1, *iv2;
        int32_t end;
        c1 = uef->chunks + i;
        c2 = c1 + 1;
        iv1 = &(c1->elapsed);
        iv2 = &(c2->elapsed);
        end = (iv1->start_1200ths + iv1->pos_1200ths);
        if (end != iv2->start_1200ths) {
            log_warn("uef: BUG: timestamp integrity failure: chunks[%d]/t&%x = (%d + %d) = %d but [%d]/t&%x's start is %d\n",
                     i, c1->type, iv1->start_1200ths, iv1->pos_1200ths, end, i+1, c2->type, iv2->start_1200ths);
            return TAPE_E_BUG;
        }
    }
    /*printf("verify_timestamps: %d chunks OK\n", i);*/
    return TAPE_E_OK;
}

#define UEF_CHUNKLEN_MAX 0xffffff /* shrug */

/* TOHv3: exported now */
int uef_store_chunk (          uef_state_t * const uef,
                     const         uint8_t * const buf,
                                  uint16_t   const type,
                                  uint32_t   const len,
                                  uint32_t   const offset,              /* TOHv4.2: add offset */
                                  uint32_t   const nodata_pre_2400ths,  /* TOHv4.3: needed for chunks &110, 111, 112, 116 */
                                  uint32_t   const nodata_post_2400ths, /* TOHv4.3: needed for chunk &111 only */
                                  uint32_t   const cycs_114,            /* TOHv4.3: chunk &114's 24-bit length field */
                     const tape_interval_t * const elapsed) {

    uef_chunk_t *chunk;
    int32_t newsize;
    uef_chunk_t *p;

#ifdef BUILD_TAPE_SANITY
    if (elapsed->start_1200ths < -1) {
        log_warn("uef: BUG: store chunk (type &%x): elapsed start_1200ths is duff (%d)", type, elapsed->start_1200ths);
        return TAPE_E_BUG;
    }
    if (elapsed->pos_1200ths < 0) {
        log_warn("uef: BUG: store chunk (type &%x): elapsed pos_1200ths is duff (%d)", type, elapsed->pos_1200ths);
        return TAPE_E_BUG;
    }
#endif

    /* TOHv4.3 now permits zero-length chunks in principle. */
    
    /* enforce some arbitrary maximum chunk length */
    if (len > UEF_CHUNKLEN_MAX) {
        log_warn("uef: oversized chunk #%d, at &%x bytes", uef->num_chunks, len);
        return TAPE_E_UEF_OVERSIZED_CHUNK;
    }
    
    if (uef->num_chunks >= uef->num_chunks_alloc) {
        newsize = uef->num_chunks_alloc + UEF_CHUNK_DELTA;
        p = realloc(uef->chunks, newsize * sizeof(uef_chunk_t));
        if (NULL == p) {
            log_warn("uef: could not reallocate chunks");
            /*uef_finish(uef);*/ /* ? */
            return TAPE_E_MALLOC;
        }
        uef->chunks = p;
        uef->num_chunks_alloc = newsize;
    }
    
    chunk = uef->chunks + uef->num_chunks;
   
    memset(chunk, 0, sizeof(uef_chunk_t)); /* TOHv4.3 */
    chunk->type = type;
    chunk->len  = len;
    chunk->offset = offset;
    chunk->data = malloc (len + 1); /* alloc an extra byte, for strings */
    chunk->alloc = len;             /* TOHv3 */
    chunk->data[len] = '\0';
    chunk->elapsed = *elapsed;      /* TOHv4.3 */
    if (chunk->elapsed.start_1200ths < 0) {
        chunk->elapsed.start_1200ths = 0;
    }
    /* TOHv4.3: the caller must provide these fields,
     * in order for seeking to work on generated UEF files. */
    chunk->nodata_total_pre_2400ths  = nodata_pre_2400ths;
    chunk->nodata_total_post_2400ths = nodata_post_2400ths;
    chunk->chunk_114_total_cycs      = cycs_114;

    if (NULL == chunk->data) {
        log_warn("uef: could not allocate chunk data for chunk #%d", uef->num_chunks);
        return TAPE_E_MALLOC;
    }
    
    memcpy(chunk->data, buf, len);
    
    (uef->num_chunks)++;

#ifdef BUILD_TAPE_SANITY
    /*e = verify_timestamps(uef);*/ /* TOHv4.3-a4 */
    /*
    if (TAPE_E_OK != e) {
        debug_tape_ls_uef(&tape_state);
    }
    */
    /*if (TAPE_E_OK != e) { return e; }*/
#endif
    
    return TAPE_E_OK;
    
}


#define UEF_LOG_GLOBAL_CHUNKS

static int uef_parse_global_chunks (uef_state_t *u) {

    uint32_t nrh, no, ni, nis, ntm;
    int32_t n;
    int e;
    uef_globals_t *g;
    
    e = TAPE_E_OK;
    
    /* deal with &00xx chunks, which apply to the entire file,
     * and copy properties onto the uef_state_t struct.
     * 
     * UEF doesn't always specify what to do if several instances of
     * these chunks are present.
     * 
     * We allow chunks 0, 1, 3, 5, 8 in multiplicity.
     * 6, 7, 9 and &A will be singletons.
     */
     
    g = &(u->globals); /* save some typing */
    memset (g, 0, sizeof(uef_globals_t));
     
    /* perform an initial pass, so we know how many of each
     * multichunk exists. */
    for (n=0; n < u->num_chunks; n++) {
        if (0x0 == u->chunks[n].type) { (g->num_origins)++;         }
        if (0x1 == u->chunks[n].type) { (g->num_instructions)++;    }
        if (0x3 == u->chunks[n].type) { (g->num_inlay_scans)++;     }
        if (0x5 == u->chunks[n].type) { (g->num_target_machines)++; }
        if (0x8 == u->chunks[n].type) { (g->num_rom_hints)++;       }
    }
    
    /* arbitrary sanity limit TAPE_UEF_MAX_GLOBAL_CHUNKS for all lists */
    if (g->num_origins > TAPE_UEF_MAX_GLOBAL_CHUNKS) {
        log_warn("uef: too many origin chunks; found %u, limit is %u.",
                 g->num_origins, TAPE_UEF_MAX_GLOBAL_CHUNKS);
        return TAPE_E_UEF_GLOBAL_CHUNK_SPAM;
    }
    
    if (g->num_instructions > TAPE_UEF_MAX_GLOBAL_CHUNKS) {
        log_warn("uef: too many instructions chunks; found %u, limit is %u.",
                 g->num_instructions, TAPE_UEF_MAX_GLOBAL_CHUNKS);
        return TAPE_E_UEF_GLOBAL_CHUNK_SPAM;
    }
    
    if (g->num_inlay_scans > TAPE_UEF_MAX_GLOBAL_CHUNKS) {
        log_warn("uef: too many inlay scan chunks; found %u, limit is %u.",
                 g->num_inlay_scans, TAPE_UEF_MAX_GLOBAL_CHUNKS);
        return TAPE_E_UEF_GLOBAL_CHUNK_SPAM;
    }
    
    if (g->num_target_machines > TAPE_UEF_MAX_GLOBAL_CHUNKS) {
        log_warn("uef: too many target machine chunks; found %u, limit is %u.",
                 g->num_target_machines, TAPE_UEF_MAX_GLOBAL_CHUNKS);
        return TAPE_E_UEF_GLOBAL_CHUNK_SPAM;
    }
    
    if (g->num_rom_hints > TAPE_UEF_MAX_GLOBAL_CHUNKS) {
        log_warn("uef: too many ROM hint chunks; found %u, limit is %u.",
                 g->num_rom_hints, TAPE_UEF_MAX_GLOBAL_CHUNKS);
        return TAPE_E_UEF_GLOBAL_CHUNK_SPAM;
    }

    if ((TAPE_E_OK == e) && (g->num_origins > 0)) {
        g->origins = malloc(sizeof(uef_origin_t) * g->num_origins);
        if (NULL == g->origins) {
            log_warn("uef: could not allocate origins list");
            e = TAPE_E_MALLOC;
        }
    }
    if ((TAPE_E_OK == e) && (g->num_instructions > 0)) {
        g->instructions = malloc(sizeof(uef_instructions_t) * g->num_instructions);
        if (NULL == g->instructions) {
            log_warn("uef: could not allocate instructions list");
            e = TAPE_E_MALLOC;
        }
    }
    if ((TAPE_E_OK == e) && (g->num_inlay_scans > 0)) {
        g->inlay_scans = malloc(sizeof(uef_inlay_scan_t) * g->num_inlay_scans);
        if (NULL == g->inlay_scans) {
            log_warn("uef: could not allocate inlay_scans list");
            e = TAPE_E_MALLOC;
        }
    }
    if ((TAPE_E_OK == e) && (g->num_target_machines > 0)) {
        g->target_machines = malloc(g->num_target_machines); /* one byte each */
        if (NULL == g->target_machines) {
            log_warn("uef: could not allocate target_machines list");
            e = TAPE_E_MALLOC;
        }
    }
    if ((TAPE_E_OK == e) && (g->num_rom_hints > 0)) {
        g->rom_hints = malloc(sizeof(uef_rom_hint_t) * g->num_rom_hints);
        if (NULL == g->rom_hints) {
            log_warn("uef: could not allocate rom hints");
            e = TAPE_E_MALLOC;
        }
    }
    
    if (TAPE_E_OK == e) {
        log_info("uef: multichunk counts: &0/%u; &1/%u; &3/%u; &5/%u; &8/%u\n",
                 g->num_origins, g->num_instructions, g->num_inlay_scans,
                 g->num_target_machines, g->num_rom_hints);
    }
    
    for (n=0, no=0, ni=0, nis=0, ntm=0, nrh=0;
         (TAPE_E_OK == e) && (n < u->num_chunks);
         n++) {
        uef_chunk_t *c;
        uint8_t log_it;
        uint8_t nyb_hi, nyb_lo;
        /* TOHv4.3 */
        uef_inlay_scan_t *scan;
        uint32_t palette_pos, expected;
        c = u->chunks + n;
        log_it = 0;
        if ( 0x0 == c->type ) {
            e = verify_utf8((char *) c->data, c->len);
            if (TAPE_E_OK != e) {
                log_warn("uef: bad UTF-8 in origin chunk!");
                break;
            }
            g->origins[no].utf8 = (char *) c->data;
            g->origins[no].len  = c->len;
            no++;
            log_it = 1;
        } else if ( 0x1 == c->type )  {
            e = verify_utf8((char *) c->data, c->len);
            if (TAPE_E_OK != e) {
                log_warn("uef: bad UTF-8 in instructions chunk!");
                break;
            }
            g->instructions[ni].utf8 = (char *) c->data;
            g->instructions[ni].len  = c->len;
            ni++;
            log_it = 1;
        } else if ( 0x3 == c->type ) {
            scan = g->inlay_scans + nis;
            e = chunk_3_parse_header(c,
                                     &(scan->w),
                                     &(scan->h),
                                     &(scan->bpp),
                                     &(scan->grey),
                                     &palette_pos); /* 0 or 5 */
            if (TAPE_E_UEF_INLAY_SCAN_BPP == e) {
                /* permit inlay scans with unsupported formats */
                e = TAPE_E_OK;
            }
            if (TAPE_E_OK != e) { break; }

            expected = (scan->w * scan->h * (scan->bpp/8));

            if (5 == palette_pos) {
                expected += 773;
            } else {
                expected +=   5;
            }

            /* compute expected chunk len */
            if (expected > c->len) {
                log_warn("uef: chunk 3 expected len (%u) mismatches actual (%u)",
                         expected, c->len);
                e = TAPE_E_UEF_CHUNKDAT_0003;
            }
            scan->body = (char *) (c->data + ((5 == palette_pos) ? 773 : 5));
            scan->palette = NULL;
            if (palette_pos > 0) {
                scan->palette = (char *) (c->data + palette_pos);
            }
            nis++;
            log_it = 1;
        } else if ( 0x5 == c->type ) {
            /* validate this */
            if (c->len != 1) {
                log_warn("uef: target machine chunk is wrong length %u", c->len);
                e = TAPE_E_UEF_CHUNKLEN_0005;
                break;
            }
            nyb_hi = (c->data[0]>>4)&0xf;
            nyb_lo = c->data[0]&0xf;
            if ((nyb_hi>0x4) || (nyb_lo>0x2)) {
                log_warn("uef: invalid target machine chunk");
                e = TAPE_E_UEF_CHUNKDAT_0005;
                break;
            }
            g->target_machines[ntm] = c->data[0]; /* value not pointer */
            ntm++;
            log_it = 1;
        } else if ( 0x6 == c->type ) {
            if (g->have_bit_mux) {
                log_warn("uef: multiple bit multiplexing information chunks; ignoring later ones");
            } else {
                /* not sure quite how to validate this, but it
                 * probably shouldn't be larger than 4, or smaller than 1 */
                if ((c->data[0] > 4) || (c->data[0] < 1)) {
                    log_warn("uef: invalid bit multiplexing information chunk");
                    e = TAPE_E_UEF_CHUNKDAT_0006;
                    break;
                }
                g->bit_mux_info = c->data[0];
                g->have_bit_mux = 1;
                log_it = 1;
            }
        } else if ( 0x7 == c->type ) {
            if (g->have_extra_palette) {
                log_warn("uef: multiple extra palette chunks; ignoring later ones");
            } else {
                /* again, not parsed yet */
                g->extra_palette = c->data;
                g->extra_palette_len = c->len;
                g->have_extra_palette = 1;
                log_it = 1;
            }
        } else if ( 0x8 == c->type ) {
            g->rom_hints[nrh].data = c->data;
            g->rom_hints[nrh].len  = c->len;
            log_it = 1;
            nrh++;
        } else if ( 0x9 == c->type ) {
            if (g->have_short_title) {
                log_warn("uef: multiple short title chunks; ignoring later ones");
            } else {
                g->short_title = (char *) c->data;
                g->short_title_len = c->len;
                g->have_short_title = 1;
                log_it = 1;
            }
        } else if ( 0xa == c->type ) {
            if (g->have_visible_area) {
                log_warn("uef: multiple visible area chunks; ignoring later ones");
            } else {
                g->visible_area = c->data;
                g->have_visible_area = 1;
                log_it = 1;
            }
        }
#ifdef UEF_LOG_GLOBAL_CHUNKS
        if (log_it) {
            log_info("uef: \"global\" chunk type &%x, len %u", c->type, c->len);
        }
#endif
        
/* arbitrary hard sanity limit on multi chunk repeats */
#define MAX_METADATA_MULTICHUNKS 10000
        
        if (no >= MAX_METADATA_MULTICHUNKS) {
            log_warn("uef: excessive number of origin chunks; aborting");
            e = TAPE_E_UEF_EXCESS_0000;
        } else if (ni >= MAX_METADATA_MULTICHUNKS) {
            log_warn("uef: excessive number of instructions chunks; aborting");
            e = TAPE_E_UEF_EXCESS_0001;
        } else if (nis >= MAX_METADATA_MULTICHUNKS) {
            log_warn("uef: excessive number of inlay scan chunks; aborting");
            e = TAPE_E_UEF_EXCESS_0003;
        } else if (ntm >= MAX_METADATA_MULTICHUNKS) {
            log_warn("uef: excessive number of target machine chunks; aborting");
            e = TAPE_E_UEF_EXCESS_0005;
        } else if (nrh >= MAX_METADATA_MULTICHUNKS) {
            log_warn("uef: excessive number of rom hint chunks; aborting");
            e = TAPE_E_UEF_EXCESS_0008;
        }
    } /* next chunk */

    if (TAPE_E_OK != e) {
        if (NULL != g->origins)         { free(g->origins);         }
        if (NULL != g->instructions)    { free(g->instructions);    }
        if (NULL != g->inlay_scans)     { free(g->inlay_scans);     }
        if (NULL != g->target_machines) { free(g->target_machines); }
        if (NULL != g->rom_hints)       { free(g->rom_hints);       }
        g->origins         = NULL;
        g->instructions    = NULL;
        g->inlay_scans     = NULL;
        g->target_machines = NULL;
        g->rom_hints       = NULL;
    }
    
    return e;
    
}


static int chunk_3_parse_header (const uef_chunk_t * const c,
                                 uint16_t * const w_out,
                                 uint16_t * const h_out,
                                 uint8_t * const bpp_out,
                                 bool * const grey_out,
                                 uint32_t * const palette_pos_out) {
    uint32_t len_body;
    uint32_t i;
    *palette_pos_out = 0;
    *w_out = -1;
    *h_out = -1;
    *bpp_out = 0xff;
    *grey_out = false;
    if (c->len < 5) {
        log_warn("uef: chunk type &3 has bad length (%u, need >=5)", c->len);
        return TAPE_E_UEF_CHUNKLEN_0003;
    }
    /* compute predicted size */
    /* bugfix: the caller needs these values even if this function fails, so */
    *w_out = tape_read_u16(c->data+0);
    *h_out = tape_read_u16(c->data+2);
    *bpp_out = 0x7f & c->data[4]; /* BPP */
    *grey_out = 0x80 & c->data[4]; /* greyscale? */
    /* keep this simple for now and enforce BPP = 8, 16, 24 or 32 */
    if ((*bpp_out!=8)&&(*bpp_out!=16)&&(*bpp_out!=24)&&(*bpp_out!=32)) {
        log_warn("uef: chunk type &3 has bizarre BPP value &%u", *bpp_out);
        return TAPE_E_UEF_INLAY_SCAN_BPP;
    }
    /* TOHv4.3: reject non-8bpp for now */
    if (*bpp_out != 8) {
        log_warn("uef: inlay scan: Only 8 bpp supported for now, found %u. Ignoring this scan.", *bpp_out);
        return TAPE_E_UEF_INLAY_SCAN_BPP;
    }
    len_body = (*w_out) * (*h_out) * ((*bpp_out) / 8);
    if (0 == len_body) {
        log_warn("uef: chunk type &3 has a pixel size of 0");
        return TAPE_E_UEF_INLAY_SCAN_ZERO;
    }
    i = 5;
    if ((8==(*bpp_out)) && ! (*grey_out)) {
        *palette_pos_out = i;
        i += 768; /* palette */
    }
    if (c->len != (i + len_body)) {
        log_warn("uef: chunk type &3 has bad length (%u, expected %u)", c->len, (i + len_body));
        return TAPE_E_UEF_CHUNKLEN_0003;
    }
    return TAPE_E_OK;
}


static int chunk_verify_length (const uef_chunk_t * const c) {

    uint32_t len_bytes, len_bits;
    uint32_t palette_pos; /* chunk 3 */
    uint16_t w,h;         /* chunk 3 */
    uint8_t bpp;          /* chunk 3 */
    bool grey;            /* chunk 3 */
    int e;
    
    /* let's set a catch-all sanity-based 5 MB upper limit on
     * all chunk types for now */
    if (c->len > 5000000) {
        log_warn("uef: chunk &%x length exceeds universal upper limit: %u bytes",
                 c->type, c->len);
        return TAPE_E_UEF_LONG_CHUNK;
    }
    
    if (0x0 == c->type) { /* origin information chunk */
        if (c->len < 1) {
            log_warn("uef: chunk type &0 has bad length (%u, want >=1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0000;
        }
    } else if (0x1 == c->type) { /* game instructions / manual or URL */
        /* TOHv4.3-a3: an empty instructions chunk is probably OK.
         * Permit len=0.
         * Can envisage a situation where some graphical UEF chunk
         * editor exists, and the "instructions" field is just left blank.
         */
        /*
        if (c->len < 1) {
            log_warn("uef: chunk type &1 has bad length (%u, want >=1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0001;
        }
        */
    } else if (0x3 == c->type) { /* inlay scan */
        e = chunk_3_parse_header (c, &w, &h, &bpp, &grey, &palette_pos);
        if (TAPE_E_UEF_INLAY_SCAN_BPP == e) {
            e = TAPE_E_OK; /* ignore unsupported inlay scan formats */
        }
        if (TAPE_E_OK != e) { return e; }
    } else if (0x5 == c->type) { /* target machine chunk */
        if (c->len != 1) {
            log_warn("uef: chunk type &5 has bad length (%u, want 1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0005;
        }
    } else if (0x6 == c->type) { /* bit multiplexing information */
        if (c->len != 1) {
            log_warn("uef: chunk type &6 has bad length (%u, want 1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0006;
        }
    } else if (0x7 == c->type) { /* extra palette */
        if (c->len < 3) {
            log_warn("uef: chunk type &7 has bad length (%u, want >=3)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0007;
        }
    } else if (0x8 == c->type) { /* ROM hint */
        if (c->len < 3) {
            log_warn("uef: chunk type &8 has bad length (%u, want >=3)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0008;
        }
    } else if (0x9 == c->type) { /* short title */
        if (c->len < 1) {
            log_warn("uef: chunk type &9 has bad length (%u, want >=1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0009;
        }
#define UEF_SHORT_TITLE_MAX_LEN 255 /* shrug. Would be nice if such numbers were in the UEF spec */
        if (c->len > UEF_SHORT_TITLE_MAX_LEN) {
            log_warn("uef: short title chunk &9 is too long (%u bytes)",
                     c->len);
            return TAPE_E_UEF_CHUNKLEN_0009;
        }
    } else if (0xa == c->type) { /* visible area */
        if (c->len != 8) {
            log_warn("uef: chunk type &a has bad length (%u, want 8)", c->len);
            return TAPE_E_UEF_CHUNKLEN_000A;
        }
    } else if (0x100 == c->type) { /* 8N1 chunk */
        if (c->len < 1) {
            log_warn("uef: chunk type &100 has bad length (%u, want >=1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0100;
        }
        /* TODO: multiplexed nonsense, chunk &101 */
    } else if (0x102 == c->type) { /* raw bits */
        if (c->len < 1) {
            log_warn("uef: chunk type &102 has bad length (%u, want >=1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0102;
        }
        e = compute_chunk_102_data_len (c->len, c->data[0], &len_bytes, &len_bits);
        if (TAPE_E_OK != e) { return e; }
        /* chunk len is data len plus one: */
        if (c->len != (len_bytes + 1)) {
            log_warn("uef: chunk type &102 has bad length (%u, expect %u)",
                     c->len, len_bytes + 1);
            return TAPE_E_UEF_CHUNKLEN_0102;
        }
    } else if (0x104 == c->type) { /* programmable framing */
        if (c->len < 3) {
            log_warn("uef: chunk type &104 has bad length (%u, want >=3)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0104;
        }
    } else if (0x110 == c->type) { /* leader */
        if (c->len != 2) {
            log_warn("uef: chunk type &110 has bad length (%u, want 2)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0110;
        }
    } else if (0x111 == c->type) { /* leader + &AA + leader */
        if (c->len != 4) {
            log_warn("uef: chunk type &111 has bad length (%u, want 4)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0111;
        }
    } else if (0x112 == c->type) { /* integer gap */
        if (c->len != 2) {
            log_warn("uef: chunk type &112 has bad length (%u, want 2)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0112;
        }
    } else if (0x116 == c->type) { /* float gap */
        if (c->len != 4) {
            log_warn("uef: chunk type &116 has bad length (%u, want 4)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0116;
        }
    } else if (0x113 == c->type) { /* baud (float) */
        if (c->len != 4) {
            log_warn("uef: chunk type &113 has bad length (%u, want 4)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0113;
        }
    } else if (0x114 == c->type) { /* arbitrary cycles */
        if (c->len < 6) {
            log_warn("uef: chunk type &114 has bad length (%u, want >=6)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0114;
        }
    } else if (0x115 == c->type) { /* phase change */
        if (c->len != 2) {
            log_warn("uef: chunk type &115 has bad length (%u, want 2)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0115;
        }
    } else if (0x117 == c->type) { /* baud */
        if (c->len != 2) {
            log_warn("uef: chunk type &117 has bad length (%u, want 2)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0117;
        }
    } else if (0x120 == c->type) { /* position marker text */
        if (c->len < 1) {
            log_warn("uef: chunk type &120 has bad length (%u, want >=1)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0120;
        }
    } else if (0x130 == c->type) {
        if (c->len != 3) {
            log_warn("uef: chunk type &130 has bad length (%u, want 3)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0130;
        }
    } else if (0x131 == c->type) { /* start of tape side */
        if ((c->len < 3) || (c->len > 258)) {
            log_warn("uef: chunk type &131 has bad length (%u, want 3-258)", c->len);
            return TAPE_E_UEF_CHUNKLEN_0131;
        }
    }
    
    return TAPE_E_OK;
    
}




int uef_clone (uef_state_t * const out, const uef_state_t * const in) {
    /* globals poses a problem here, because it's mostly a list of
     * pointers into in->chunks[]->data; we need globals fields to point
     * into out->chunks[]->data instead. The cleanest way to fix this is
     * probably just to zero out->globals, then run uef_parse_global_chunks()
     * on out, to rebuild the list.
     * rom_hints will also be allocated and populated separately for out.
     */
    int32_t n;
    int e;
    memcpy(out, in, sizeof(uef_state_t));
    memset(&(out->globals), 0, sizeof(uef_globals_t));
    out->chunks = malloc(sizeof(uef_chunk_t) * in->num_chunks);
    if (NULL == out->chunks) {
        log_warn("uef: could not allocate clone UEF chunks\n");
        return TAPE_E_MALLOC;
    }
    for (n=0; n < out->num_chunks; n++) {
        memcpy(out->chunks + n, in->chunks + n, sizeof(uef_chunk_t));
        out->chunks[n].data = NULL; /* no, that doesn't belong to you */
    }
    for (n=0; n < out->num_chunks; n++) {
        /* remember, we allocated one extra in case of strings */
        out->chunks[n].data = malloc(out->chunks[n].len + 1);
        if (NULL == out->chunks[n].data) {
            log_warn("uef: could not allocate clone UEF chunk data\n");
            uef_finish(out);
            return TAPE_E_MALLOC;
        }
        /* again, one extra */
        memcpy(out->chunks[n].data, in->chunks[n].data, out->chunks[n].len + 1);
    }
    e = uef_parse_global_chunks(out);
    if (TAPE_E_OK != e) {
        uef_finish(out);
    }
    return e;
}

void uef_rewind (uef_state_t * const u) {
    u->cur_chunk = -1;
    /* reset baud to 1200 */
    init_bitsource(&(u->bitsrc), 1200);
}

void uef_finish (uef_state_t *u) {
    int32_t n;
    if (NULL == u) { return; }
    /* The fields on u->globals just point into u->chunks[].data,
     * but these ones are multi and require lists: */
    if (NULL != u->globals.origins) {
        free(u->globals.origins);
    }
    if (NULL != u->globals.instructions) {
        free(u->globals.instructions);
    }
    if (NULL != u->globals.inlay_scans) {
        free(u->globals.inlay_scans);
    }
    if (NULL != u->globals.target_machines) {
        free(u->globals.target_machines);
    }
    if (NULL != u->globals.rom_hints) {
        free(u->globals.rom_hints);
    }
    for (n=0; n < u->num_chunks; n++) {
        if (NULL != u->chunks[n].data) {
            free(u->chunks[n].data);
        }
    }
    free(u->chunks);
    memset(u, 0, sizeof(uef_state_t));
}


static int chunks_verify_lengths (uef_chunk_t *chunks, int32_t num_chunks) {
    int32_t n;
    int e;
    e = TAPE_E_OK;
    for (n=0; (TAPE_E_OK == e) && (n < num_chunks); n++) {
        e = chunk_verify_length(chunks + n);
    }
    return e;
}


#define REJECT_WEIRD_CHUNK_102_FIRST_BYTE

static int compute_chunk_102_data_len (uint32_t chunk_len,
                                       uint8_t data0,
                                       uint32_t *len_bytes_out,
                                       uint32_t *len_bits_out) {
    uint32_t i;
    *len_bits_out  = 0;
    *len_bytes_out = 0;
    
    /* You had your chance! Don't say I didn't ask!
       https://stardot.org.uk/forums/viewtopic.php?p=391567 */
    
    if (data0 < 8) {
        /*log_info ("uef: chunk &102: interpretation A");*/
        data0 += 8;
    } else if (data0 < 16) {
        /*log_info ("uef: chunk &102: interpretation B");*/
    } else {
        log_warn ("uef: chunk &102: data[0] is weird (&%x, expect < &10)", data0);
#ifdef REJECT_WEIRD_CHUNK_102_FIRST_BYTE
        return TAPE_E_UEF_0102_WEIRD_DATA_0;
#endif
    }
    /* length of data in bits: */
    i = (chunk_len * 8) - data0;
    *len_bits_out = i;
    /* length of data in bytes: */
    if ((i % 8) == 0) {
        i = (i / 8);
    } else {
        i = (i / 8) + 1;
    }
    *len_bytes_out = i;
    return TAPE_E_OK;
}


/* TOHv3 */
int uef_append_byte_to_chunk (uef_chunk_t * const c, uint8_t const b) {
    uint32_t newsize;
    uint8_t *p;
    if ((NULL == c->data) && (c->alloc > 0)) {
        log_warn("uef: BUG: chunk alloc > 0 (%u) but buffer is NULL!", c->alloc);
        c->alloc = 0;
        return TAPE_E_BUG;
    }
    if (c->len >= c->alloc) {
#define TAPE_UEF_CHUNK_DATA_DELTA 1000
        newsize = c->len + TAPE_UEF_CHUNK_DATA_DELTA;
        p = realloc(c->data, newsize+1); /* +1 for string usefulness */
        if (NULL == p) {
            log_warn("uef: write: could not realloc UEF tmpchunk data buffer");
            return TAPE_E_MALLOC;
        }
        c->data = p;
        c->alloc = newsize;
    }
    c->data[c->len] = b;
    (c->len)++;
    return TAPE_E_OK;
}


/* TOHv3 */
int uef_build_output (uef_state_t *u, char **out, size_t *len_out) {

    uint32_t cn;
    uint8_t pass;
    int e;
    size_t hdrlen;
    
    e = TAPE_E_OK;
    
    *out = NULL;
    *len_out = 0;
    
    hdrlen = strlen(TAPE_UEF_MAGIC) + 3; /* +1 terminator, +2 version */
    
    for (pass=0; (TAPE_E_OK == e) && (pass < 2); pass++) {
    
        size_t pos;
        
        if (1 == pass) {
            *out = malloc(*len_out);
            if (NULL == *out) {
                e = TAPE_E_MALLOC;
                break;
            }
            memcpy(*out, TAPE_UEF_MAGIC"\x00\x0a\x00", hdrlen);
        }
        
        for (cn=0, pos=hdrlen; cn < u->num_chunks; cn++) {
            uef_chunk_t *c;
            uint8_t hdr[6];
            c = u->chunks + cn;
            /* sanity */
            if (c->len > UEF_CHUNKLEN_MAX) {
                log_warn("uef: write: BUG: chunk #%u has illegal length %u", cn, c->len);
                e = TAPE_E_BUG;
                break;
            }
            if ((0==c->type) && (0==c->len)) {
                log_warn("uef: write: BUG: Refusing to save chunk w/ix %d type &0 w/length 0", cn);
                e = TAPE_E_BUG;
                break;
            }
            if (1 == pass) {
                tape_write_u16(hdr,   c->type);
                tape_write_u32(hdr+2, c->len);
                memcpy((*out)+pos, hdr, 6);
                memcpy((*out)+pos+6, c->data, c->len);
            }
            pos += (6 + c->len);
        }
        *len_out = pos;
    }
    
    if ((TAPE_E_OK != e) && (*out != NULL)) {
        free(*out);
        *out = NULL;
        *len_out = 0;
    }
    
    return e;
    
}

int uef_ffwd_to_end (uef_state_t * const u) {

    uef_chunk_t *uc;
    int e,i;
    char dummy;

    /* force read pointer past end of file, EOF */

    /* TOHv4.3 edition: don't cheat */
    if (u->num_chunks < 1) { return TAPE_E_OK; }
    u->cur_chunk = u->num_chunks - 1;
    uc = u->chunks + u->cur_chunk;
    if (    (0x100 == uc->type)
         || (0x102 == uc->type)
         || (0x104 == uc->type)
         || (0x114 == uc->type) ) {
        u->bitsrc.src_byte_pos = uc->len;
    /* chunk 110, 112, 116 */
    } else if (    (0x116 == uc->type)
                || (0x112 == uc->type)
                || (0x110 == uc->type) ) {
        u->bitsrc.nodata_consumed_pre_2400ths  = uc->nodata_total_pre_2400ths;
    /* chunk 111 */
    } else if (0x111 == uc->type) {
        u->bitsrc.nodata_consumed_pre_2400ths  = uc->nodata_total_pre_2400ths;
        u->bitsrc.nodata_consumed_post_2400ths = uc->nodata_total_post_2400ths;
    }
    /* ensure EOF is provoked */
    for (i=0,e=0;(i<20)&&(TAPE_E_EOF != e);i++) {
        e = uef_read_1200th (u, &dummy, false, NULL, NULL, NULL);
    }
    if (TAPE_E_EOF != e) {
        log_warn("UEF: ffwd to end: BUG: eof has not successfully been provoked (code %d)", e);
        return TAPE_E_BUG;
    }
    return TAPE_E_OK;
}



/*
 * Currently, saving is always appending to the very end of the tape.
 * We search for chunk &117 backwards from the end of the tape, in order
 * to work out what the prevailing baud rate is, and therefore whether
 * we need to change it with a chunk &117.
 *
 * However, a general system that allows saving at an arbitrary point
 * in the tape will need to modify this function in order to determine
 * the prevailing baud rate at the current write pointer (which may or
 * may not be distinct from the read pointer). You'll need to scan
 * backwards from your write pointer, rather than from the end of the
 * tape. But frankly that will be the least of your worries if you
 * are serious about inserting audio at an arbitrary point in the tape. */
void uef_scan_backwards_for_chunk_117 (uef_state_t *u,
                                       int32_t start_chunk_num, /* now passed in */
                                       int32_t *prevailing_nominal_baud_out) {
    
    int32_t i;
    
    *prevailing_nominal_baud_out = 1200;

    for (i = start_chunk_num; (i >= 0) && (0x117 != u->chunks[i].type); i--)
    { }
        
    if (i >= 0) {
      *prevailing_nominal_baud_out = (int32_t) *((uint16_t *) u->chunks[i].data);
    }
    
}


#define UTF8_MODE_ASCII 1
#define UTF8_MODE_110   2
#define UTF8_MODE_1110  3
#define UTF8_MODE_11110 4
/* TOHv4.2: now decodes rather than just checking (for examine view) */
static int decode_utf8 (char *buf, size_t buf_len, uint8_t *bytes_decoded_out, int *err_out) {
    
    /* decodes one Unicode character
     (just one, any more in the buffer will be ignored) */

    /* returns 0 on failure; ecode is available in err_out */
    
    uint8_t i;
    size_t j;
    uint32_t m = UTF8_MODE_ASCII;
    uint32_t bc=0; /* byte count */
    uint8_t buffer[3];
    uint32_t tc=0;
    int r;
    
    r = TAPE_E_OK;
    if (NULL != err_out) { *err_out = 0; }
    
    for (j=0; j < buf_len; j++) {
        
        i = buf[j];
        
        if (!bc) {
            memset(buffer, 0, 3);
            if (!(i & 0x80)) {
                m = UTF8_MODE_ASCII; /* one byte */
                *bytes_decoded_out = 1;
            } else if ((i & 0xe0)==0xc0) {
                m = UTF8_MODE_110;   /* two bytes 1010 1100 */
                *bytes_decoded_out = 2;
            } else if ((i & 0xf0)==0xe0) {
                m = UTF8_MODE_1110;  /* three bytes */
                *bytes_decoded_out = 3;
            } else if ((i & 0xf8)==0xf0) {
                m = UTF8_MODE_11110; /* four bytes */
                *bytes_decoded_out = 4;
            } else {
                log_warn("uef: UTF-8: Decoding error [1] at offset 0x%x, byte 0x%x\n", tc, (uint32_t) i);
                r = TAPE_E_UEF_UTF8_DEC_1;
                break;
            }
        } else if ((i & 0xc0) != 0x80) {
            log_warn("uef: UTF-8: Decoding error [2] at offset 0x%x: "
                     "byte is 0x%x, mode is %u\n", tc, (uint32_t) (i&0xff), m);
            r = TAPE_E_UEF_UTF8_DEC_2;
            break;
        }
        
        switch (m) {
            case UTF8_MODE_ASCII: /* one byte total */
                return i;
            case UTF8_MODE_110: /* two bytes total */
                switch (bc) {
                    case 0:
                        buffer[0]=((char)(i & 0x1c))>>2;
                        buffer[1]=((char)(i & 0x03))<<6;
                        bc++;
                        break;
                    case 1:
                        buffer[1]|=((char)(i & 0x3f));
                        return ((buffer[0]<<8) & 0xff00) | buffer[1];
                        break;
                    default:
                        log_warn("uef: UTF-8: console_decode_utf8: Bug [3]\n");
                        r = TAPE_E_BUG;
                        break;
                }
                break;
            case UTF8_MODE_1110: /* three bytes total */
                switch (bc) {
                    case 0:
                        buffer[0]=((char)(i & 0x0f))<<4;
                        bc++;
                        break;
                    case 1:
                        buffer[0]|=((char)(i & 0x3c))>>2;
                        buffer[1]=((char)(i & 0x03))<<6;
                        bc++;
                        break;
                    case 2:
                        buffer[1]|=((char)(i & 0x3f));
                        return ((buffer[0]<<8) & 0xff00) | buffer[1];
                    default:
                        log_warn("uef: UTF-8: console_decode_utf8: Bug [4]\n");
                        r = TAPE_E_BUG;
                        break;
                }
                break;
            case UTF8_MODE_11110: /* four bytes total */
                switch (bc) {
                    case 0:
                        buffer[0]=((char)(i & 0x07))<<2;
                        bc++;
                        break;
                    case 1:
                        buffer[0]|=((char)(i & 0x30))>>4;
                        buffer[1]=((char)(i & 0x0f))<<4;
                        bc++;
                        break;
                    case 2:
                        buffer[1]|=((char)(i & 0x3c))>>2;
                        buffer[2]=((char)(i & 0x03))<<6;
                        bc++;
                        break;
                    case 3:
                        buffer[2]|=((char)(i & 0x3f));
                        return ((buffer[0]<<16) & 0xff0000) | ((buffer[1]<<8) & 0xff00) | buffer[2];
                    default:
                        log_warn("uef: UTF-8: console_decode_utf8: Bug [5]\n");
                        r = TAPE_E_BUG;
                        break;
                }
                break;
        }
        tc++;
    }

    /* errors are routed here so debugger can be easily attached on error */
    *err_out = r;
    return 0;

}

/* TOHv4.2 */
void unicode_string_finish (uef_unicode_string_t *u) {
  if (NULL == u) { return; }
  if (NULL == u->text) { return; }
  free(u->text);
  memset(u, 0, sizeof(uef_unicode_string_t));
}

/* TOHv4.2: now leverages unicode_string_append() */
int verify_utf8 (char *buf, size_t len) {
    int e;
    uef_unicode_string_t u;
    memset(&u, 0, sizeof(uef_unicode_string_t));
    e = unicode_string_append(&u, buf);
    unicode_string_finish(&u);
    return e;
}

/* TOHv4.2 */
int unicode_string_to_utf8 (uef_unicode_string_t *u, char **utf8_out, size_t *chars_out, size_t *bytes_out, uint8_t line_break) {
    size_t i;
    uint8_t pass;
    size_t j; /* output position */
    *utf8_out = NULL;
    j=0; i=0; /* clang */
    if (NULL == u) {
        log_warn("bug: unicode_string_to_utf8: NULL unicode string passed");
        return TAPE_E_BUG;
    }
    if (NULL == u->text) {
        log_warn("bug: unicode_string_to_utf8: NULL u->text");
        return TAPE_E_BUG;
    }
    if (NULL != chars_out) { *chars_out = 0; }
    if (NULL != bytes_out) { *bytes_out = 0; }
    for (pass = 0; pass < 2 ; pass++) {
        for (i=0, j=0; i < u->len; i++) {
            uint8_t len8;
            int e;
            char utf8[5] = {0,0,0,0,0};
            len8 = 0;
            e = encode_utf8 (u->text[i], (uint8_t *) utf8, &len8);
            if (TAPE_E_OK != e) { return e; }
            if (pass == 1) { memcpy(*utf8_out + j, utf8, len8); }
            j += len8;
        }
        if (pass == 0) {
            *utf8_out = malloc(2 + j); /* +1 for '\0', +1 for \n */
        }
    }
    if (NULL != bytes_out) { *bytes_out = j; }
    if (NULL != chars_out) { *chars_out = i; }
    if (line_break) {
        (*utf8_out)[j] = '\n';
        j++;
    }
    (*utf8_out)[j] = '\0';
    return TAPE_E_OK;
}

#define FMT_SZT            "zu"
#define UNICODE_MAXLEN     1000000
#define UNICODE_TERMINATOR 0

/* TOHv4.2 */
int unicode_string_append (uef_unicode_string_t *u, char *buf) {
  
    size_t i;
    size_t len;
    uint8_t pass;
    size_t j;
    int e;

    i=0; j=0; /* clang */

    if (NULL == u) {
        printf("BUG: unicode_string_append: NULL input\n");
        return TAPE_E_BUG;
    }

    if (NULL == buf) { return TAPE_E_OK; }

    len = strlen(buf);
    e = TAPE_E_OK;

    for (pass = 0; pass < 2; pass++) {

        uint8_t utf8len;

        /* i = srcpos, j = dstpos
           j begins at the prior len. scribbling prior terminator */
        for (i=0, j = u->len; i < len; j++, i += utf8len) {

            uint32_t x;

            utf8len = 0;
            e = TAPE_E_OK;
            x = decode_utf8 (buf + i, len - i, &utf8len, &e);

            if (TAPE_E_OK != e) {
                return e;
            } else if (0 == x) {
                // shouldn't happen
                log_warn("BUG: unexpected null byte encountered, src pos %" FMT_SZT ", dest pos %" FMT_SZT "\n",
                         i, j);
                return TAPE_E_UNICODE_UNEXPECTED_NULL;
            }
            if ((utf8len + j) >= UNICODE_MAXLEN) {
                printf("BUG: exceeded maximum length (%" FMT_SZT ")\n", (size_t) UNICODE_MAXLEN);
                e = TAPE_E_UNICODE_MAX_LEN;
                break;
            }
            if (1 == pass) { u->text[j] = x; }

        }

        if (TAPE_E_OK != e) { break; }

        if (0 == pass) {
            /* allocate at end of first pass */
            u->text = realloc(u->text, (sizeof(uint32_t) * (1 + j)));
        }

    } /* next pass */

    if (TAPE_E_OK == e) {
        u->text[j]   = UNICODE_TERMINATOR;
        u->len       = j;
        u->alloc     = j;
    }

    return e;
  
}

/* TOHv4.2 */
static int encode_utf8 (uint32_t c, uint8_t *utf8_out, uint8_t *len_bytes_out) {
    *len_bytes_out = 0;
    if (c < 0x80) {
        utf8_out[0] = c & 0x7f;
        *len_bytes_out = 1;
        return TAPE_E_OK;
    } else if (c < 0x800) {
        utf8_out[0] = 0xc0 | ((c >> 6) & 0x1f);
        utf8_out[1] = 0x80 | (c & 0x3f);
        *len_bytes_out = 2;
        return TAPE_E_OK;
    } else if (c < 0x10000) {
        utf8_out[0] = 0xe0 | ((c >> 12) & 0xf);
        utf8_out[1] = 0x80 | ((c >> 6) & 0x3f);
        utf8_out[2] = 0x80 | (c & 0x3f);
        *len_bytes_out = 3;
        return TAPE_E_OK;
    } else {
        utf8_out[0] = 0xf0 | ((c >> 18) & 0x7);
        utf8_out[1] = 0x80 | ((c >> 12) & 0x3f);
        utf8_out[2] = 0x80 | ((c >> 6)  & 0x3f);
        utf8_out[3] = 0x80 | (c & 0x3f);
        *len_bytes_out = 4;
        return TAPE_E_OK;
    }
    return TAPE_E_ENC_UTF8;
}

int uef_change_current_chunk (uef_state_t * const uef, int32_t const chunk_ix) {
    int32_t baud;
    if ((chunk_ix >= uef->num_chunks) || (chunk_ix < 0))  {
        log_warn("uef: BUG: uef_change_current_chunk to illegal chunk %d (%d available)",
                 chunk_ix, uef->num_chunks);
        return TAPE_E_BUG;
    }
    /* scan for most recent baud chunk */
    uef_scan_backwards_for_chunk_117(uef, chunk_ix, &baud);
    uef->cur_chunk = chunk_ix;
    init_bitsource (&(uef->bitsrc), baud);
    return TAPE_E_OK;
}

#endif
