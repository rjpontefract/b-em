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

#ifndef __INC_TAPE_INTERVAL
#define __INC_TAPE_INTERVAL

#include <stdbool.h>
#include <stdint.h>

#define TAPE_INTERVAL_TYPE_PENDING 0 /* TOHv4.3-a3 */
#define TAPE_INTERVAL_TYPE_SILENCE 1
#define TAPE_INTERVAL_TYPE_LEADER  2
#define TAPE_INTERVAL_TYPE_DATA    3

/* TOHv4.3: attach to UEF chunks, TIBET spans, CSW pulses ...
 *          - ALSO used for the seeker bar stripes */
typedef struct tape_interval_s {
    uint8_t type; /* TAPE_INTERVAL_TYPE_..., used for bar stripes only */
    int32_t start_1200ths;
    int32_t pos_1200ths;
    /* "private", for counting 4800ths
     * (TIBET: 'P' pulse is s/4800; normal tonechar is s/2400): */
    uint8_t sub_pos_4800ths;
} tape_interval_t;

/* decoder state */
/* FIXME: rename to ..._list_decoder_s */
typedef struct tape_interval_list_decstate_s {

    int32_t leader_detect_1200ths;
    char prev_tone;
    tape_interval_t wip;

} tape_interval_list_decstate_t;

typedef struct tape_interval_list_s {

    tape_interval_t *list;
    int32_t alloc;
    int32_t fill;

    /* current decoder state */
    tape_interval_list_decstate_t decstate;

} tape_interval_list_t;

int tape_interval_list_append (tape_interval_list_t * const iv_list_inout,
                               const tape_interval_t * const to_append,
                               bool const deduce_start_time);

int tape_interval_list_send_1200th (tape_interval_list_t * const iv_list_inout,
                                    char tone, /* not const! */
                                    bool const free_list_on_error);

uint8_t tape_interval_type_from_tone (char const tone);

const char * tape_interval_name_from_type (uint8_t const type);

void tape_interval_list_finish (tape_interval_list_t * const iv_list_inout); /* TOHv4.3 */

int tape_interval_list_clone (tape_interval_list_t * const out,
                              const tape_interval_list_t * const in);

bool tape_interval_list_integrity_check (const tape_interval_list_t * const list); /* TOHv4.3-a4 */

#endif
