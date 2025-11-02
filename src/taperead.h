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

#ifndef __INC_TAPEREAD
#define __INC_TAPEREAD

#include "tape.h"

bool tape_peek_for_data (tape_state_t * const ts);

/* TOHv4 */
int tape_fire_acia_rxc (tape_state_t * const ts,
                         tape_vars_t * const tv,
                                ACIA * const acia,
                             int32_t   const acia_rx_divider,
                                bool   const emit_tapenoise,
                                bool * const throw_eof_out);

int tape_1200th_from_back_end (        bool   const strip_silence_and_leader,             /* turbo */
                               tape_state_t * const ts,
                                       bool   const awaiting_start_bit,
                                       bool   const enable_phantom_block_protection,      /* TOHv3.3 */
                                       char * const tone_1200th_out,                      /* must know about leader */
                                    int32_t * const updated_elapsed_1200ths_out_or_null);

bool tape_peek_eof (tape_state_t * const t);

int tape_read_1200th (tape_state_t * const ser,
                      bool const initial_scan_mode,
                      char * const value_out_or_null,
                      int32_t * const updated_elapsed_1200ths_out_or_null) ;

/* 'elapsed' value comes from the initial scan data */
int tape_update_playback_elapsed (tape_state_t * const ts, int32_t const elapsed_or_minus_one);

int tape_wp_init_blank_tape_if_needed (uint8_t *filetype_bits_inout);

#endif /* __INC_TAPEREAD */
