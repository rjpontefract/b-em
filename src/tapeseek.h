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

#ifndef __INC_TAPESEEK_H
#define __INC_TAPESEEK_H

#include "tape2.h"
#include "tapectrl.h"

typedef struct tape_state_s tape_state_t;

int tape_seek_absolute (tape_state_t * const ts, /* modified */
                        int32_t              time_1200ths_wanted,
                        int32_t        const duration_1200ths,
                        int32_t      * const time_1200ths_actual_out,
                        bool         * const eof_out,
                        bool         * const desynced_inout);

/* TOHv4.3: tape control */
int tape_get_duration_1200ths (tape_state_t * const ts,
                               int32_t * const duration_1200ths_out); /* invalid until initial scan */

int tape_rewind_2 (tape_state_t * const t,
                   tape_ctrl_window_t * const tcw,
                   bool const record_activated,
                   bool const tapectrl_opened);

/* -tapeseek command in debugger
 *  (runs in main thread) */
int tape_seek_for_debug (tape_state_t       * const ts,
                         tape_vars_t        * const tv,
                         double               const seek_fraction);

/*int tape_ffwd_to_end (tape_state_t * const ts);*/ /* TOHv4.3 */

int tapeseek_run_initial_scan (tape_state_t * const ts,
                               tape_interval_list_t * const iv_list_inout);

int read_ffwd_to_end (tape_state_t *t);

#endif /* __INC_TAPESEEK_H */
