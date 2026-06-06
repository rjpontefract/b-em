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

#ifndef __INC__CSW_H
#define __INC__CSW_H

#include "tape2.h" /* for BUILD_TAPE_READ_POS_TO_EOF_ON_WRITE */
#include "tapeseek.h"

typedef struct csw_header_s {
    uint8_t compressed;
    uint8_t ext_len;
    uint8_t version_maj;
    uint8_t version_min;
    uint32_t rate;
    uint8_t flags;
} csw_header_t;

/* TOHv4.3 */
typedef struct csw_pulse_s {
    uint32_t len_smps;
    tape_interval_t timespan;
} csw_pulse_t;

typedef struct csw_state_s {

    csw_header_t header;
    csw_pulse_t *pulses;
    
    /* TOHv3: for write support: */
    uint32_t pulses_fill;
    uint32_t pulses_alloc;
    /* for jittering pulse lengths, to obtain correct output frequencies: */
    double accumulated_error_smps;
    
    /* comparison to this pulse length threshold must be
       p <= thresh : short pulse
       p >  thresh : long pulse */
    double thresh_smps_perfect;
    double len_1200th_smps_perfect;
    
    /* reader state: */
    uint32_t cur_pulse;

    int32_t num_silent_1200ths;
    int32_t cur_silence_1200th;
    
} csw_state_t;

int csw_load_file (const char *fn, csw_state_t *csw);
void csw_close(void);
void csw_poll(void);
void csw_findfilenames(void);
int csw_clone (csw_state_t *out, csw_state_t *in);
void csw_rewind (csw_state_t *csw);
int csw_read_1200th (csw_state_t * const csw,
                     char * const out_1200th_or_null,
                     bool initial_scan,
                     int32_t * const elapsed_out_or_null); /* TOHv4.3: return elapsed time if available (or -1) */
bool csw_peek_eof (const csw_state_t * const csw);
void csw_force_eof (csw_state_t * const csw);
void csw_finish (csw_state_t *csw);

/* TOHv3: */
int csw_append_pulse (csw_state_t * const csw,
                      uint32_t const pulse_len_smps,
                      const tape_interval_t * const interval_or_null);   /* TOHv4.3: interval arg */
int csw_append_pulse_fractional_length (csw_state_t *csw,
                                        double pulse_len_smps,
                                        const tape_interval_t * const interval_or_null) ; /* TOHv4.3: interval arg */
int csw_init_blank_if_necessary (csw_state_t *csw);
int csw_append_leader (csw_state_t * const csw,
                       uint32_t const num_1200ths,
                       int32_t start_1200ths); /* TOHv4.3 */
int csw_append_silence (csw_state_t * const csw,
                        float len_s,
                        const tape_interval_t * const interval_or_null);
/* TOHv3: */
int csw_build_output (csw_state_t *csw,
                      uint8_t compress,
                      char **out,
                      size_t *len_out);
int csw_ffwd_to_end (csw_state_t *csw);
void csw_get_duration_1200ths (const csw_state_t * const csw, int32_t * const dur_1200ths_out); /* TOHv4.3 */
int csw_change_current_pulse (csw_state_t * const csw, uint32_t const pulse_ix); /* TOHv4.3 */
#endif

