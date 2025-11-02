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
#include <string.h>

#include "tape2.h"
#include "tape-interval.h"

#include "logging.h"

void tape_interval_list_finish (tape_interval_list_t * const iv_list_inout) {
    if (NULL == iv_list_inout) { return; }
    if (NULL != iv_list_inout->list) {
        free(iv_list_inout->list);
    }
    memset(iv_list_inout, 0, sizeof(tape_interval_list_t));
}

int tape_interval_list_clone (tape_interval_list_t * const out,
                              const tape_interval_list_t * const in) {
    tape_interval_t *tmp;
    /*finish_interval_list(out);*/
    tmp = NULL;
    if (in->alloc > 0) {
        if (in->fill > in->alloc) { /* TOHv4.3-a3 sanity */
            log_warn("tape-interval: clone interval list: BUG: in->fill(%d) > in->alloc(%d)\n", in->fill, in->alloc);
            return TAPE_E_MALLOC;
        }
        tmp = malloc(in->alloc * sizeof(tape_interval_t));
        if (NULL == tmp) {
            log_warn("tapectrl: ERROR: out of memory cloning intervals list");
            return TAPE_E_MALLOC;
        }
        memset (tmp, 0, in->alloc * sizeof(tape_interval_t));
        memcpy (tmp, in->list, in->fill * sizeof(tape_interval_t));
    } else if (in->fill > 0) {
        log_warn("tape-interval: clone interval list: in->fill > 0 but in->alloc == 0");
        return TAPE_E_BUG;
    }
    out->alloc = in->alloc;
    out->fill  = in->fill;
    out->list  = tmp;
    return TAPE_E_OK;
}


int tape_interval_list_append (tape_interval_list_t  * const iv_list_inout,
                               const tape_interval_t * const to_append,
                               bool const deduce_start_time) {

    int32_t newsize;
    tape_interval_t *tmp, *prev, *cur;

    if (NULL == iv_list_inout) {
        log_warn("tape: BUG: append to intervals list: iv_list_inout is NULL");
        return TAPE_E_BUG;
    }

    if (iv_list_inout->alloc <= iv_list_inout->fill) {
        newsize = iv_list_inout->alloc + 100;
        tmp = realloc(iv_list_inout->list, sizeof(tape_interval_t) * newsize);
        if (NULL == tmp) {
            log_warn("tape: initial scan: appending to interval list: out of memory");
            return TAPE_E_MALLOC;
        }
        /* zero out the next interval */
        memset((tmp + iv_list_inout->fill),
               0,
               sizeof(tape_interval_t) * (newsize - iv_list_inout->fill));
        iv_list_inout->alloc = newsize;
        iv_list_inout->list = tmp;
    }

    /*
printf("[%d]: %s; (%d + %d)\n",
        *num_intervals_inout,
        interval_get_name_from_type(to_append->type),
        to_append->start_1200ths,
        to_append->pos_1200ths);
        */

    iv_list_inout->list[iv_list_inout->fill] = *to_append;

    if (deduce_start_time) {
        cur = iv_list_inout->list + iv_list_inout->fill;
        if (iv_list_inout->fill > 0) {
            prev = cur - 1;
            cur->start_1200ths = prev->start_1200ths + prev->pos_1200ths;
        } else {
            cur->start_1200ths = 0;
        }
    }

    iv_list_inout->fill++;

    return TAPE_E_OK;

}

/* will be used both for recording and for playback (i.e. initial scan) */

/* CAUTION: remember to call append_to_interval_list() with iv_tmp_inout
 *          after this function returns, in order to add the last remaining
 *          piece to the interval list! */
int tape_interval_list_send_1200th (tape_interval_list_t * const iv_list_inout,
                                    char tone, /* not const! */
                                    bool const free_list_on_error) {
    int e;
    tape_interval_list_decstate_t *dec_p;

    e = TAPE_E_OK;

#ifdef BUILD_TAPE_SANITY
    if ( ! TAPE_TONECODE_IS_LEGAL(tone) ) {
        log_warn("tape: BUG: send_1200th_to_interval_list: bad tone (&%x)", tone);
        return TAPE_E_BUG;
    }
#endif

    dec_p = &(iv_list_inout->decstate);

    if ('1'==tone) {
        if (dec_p->leader_detect_1200ths >= TAPE_CRUDE_LEADER_DETECT_1200THS) {
            tone = 'L';
        } else {
            dec_p->leader_detect_1200ths++;
        }
    } else {
        dec_p->leader_detect_1200ths = 0;
    }

    /* if interval type has not been decided yet, commit to one now */
    if (TAPE_INTERVAL_TYPE_PENDING == dec_p->wip.type) {
        dec_p->wip.type = tape_interval_type_from_tone(tone);
    }

    /* end of this interval? */
    if (    ('\0' != dec_p->prev_tone)
         && (    tape_interval_type_from_tone(dec_p->prev_tone)
              != tape_interval_type_from_tone(tone) )) {

        e = tape_interval_list_append (iv_list_inout, &(dec_p->wip), true);
        if (TAPE_E_OK != e) {
            if (free_list_on_error) {
                tape_interval_list_finish(iv_list_inout);
            }
            return e;
        }
        /* wipe WIP to prepare for the next interval */
        memset(&(dec_p->wip), 0, sizeof(tape_interval_t));
        dec_p->wip.type = TAPE_INTERVAL_TYPE_PENDING; /* (=0, so not strictly needed) */
    }

    iv_list_inout->decstate.wip.pos_1200ths++;
    iv_list_inout->decstate.prev_tone = tone;

    return TAPE_E_OK;

}


const char * tape_interval_name_from_type (uint8_t const type) {
    if (TAPE_INTERVAL_TYPE_SILENCE == type) {
        return "SILENT";
    } else if (TAPE_INTERVAL_TYPE_DATA == type) {
        return "  data";
    }
    return "leader";
}

uint8_t tape_interval_type_from_tone (char const tone) {
    if ('S' == tone) {
        return TAPE_INTERVAL_TYPE_SILENCE;
    } else if ('L' == tone) {
        return TAPE_INTERVAL_TYPE_LEADER;
    }
    return TAPE_INTERVAL_TYPE_DATA;
}

bool tape_interval_list_integrity_check (const tape_interval_list_t * const list) {
    int32_t i;
    for (i=0; i < (list->fill - 1); i++) {
        tape_interval_t *iv_a, *iv_b;
        iv_a = list->list + i;
        iv_b = iv_a + 1;
        if ((iv_a->start_1200ths + iv_a->pos_1200ths) != iv_b->start_1200ths) {
            log_warn("tape-interval: BUG: list integrity failure: list[%d]=(%d + %d), but [%d]'s start is %d",
                     i, iv_a->start_1200ths, iv_a->pos_1200ths, i+1, iv_b->start_1200ths);
            return TAPE_E_BUG;
        }
    }
/*printf("tape_interval_list_integrity_check: %d intervals, OK.\n", i);*/
    return TAPE_E_OK;
}
