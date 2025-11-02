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

#ifndef __INC_TAPEWRITE_H
#define __INC_TAPEWRITE_H

#include "tape.h"

int tape_flush_pending_piece (tape_state_t * const ts,
                              ACIA * const acia_or_null,
                              bool const silence112,
                              bool const populate_elapsed);

int tape_write_bitclk (tape_state_t * const ts,
                       tape_ctrl_window_t * const tcw, /* TOHv4.3 */
                       tape_interval_list_t * const iv_list_inout, /* TOHv4.3 */
                       ACIA * const acia,
                       char const bit,
                       int64_t const ns_per_bit,
                       bool const tapenoise_write_enabled,
                       bool const record_is_pressed,
                       bool const always_117,
                       bool const silence112,
                       bool const no_origin_chunk_on_append,
                       bool const tapectrl_opened, /* TOHv4.3 */
                       uint32_t * const since_last_tone_sent_to_gui_inout);

int tape_uef_flush_incomplete_frame (uef_state_t * const uef_inout,
                                         int32_t   const tallied_1200ths,
                                         uint8_t * const serial_phase_inout,
                                         uint8_t * const serial_frame_inout,
                                     uef_chunk_t * const chunk_inout); /* in practice this is ts->w_uef_tmpchunk */

int tape_wp_init_blank_tape_if_needed (uint8_t * const filetype_bits_inout);

#endif /* __INC_TAPEWRITE_H */
