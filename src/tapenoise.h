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

#ifndef __INC_TAPENOISE_H
#define __INC_TAPENOISE_H

#include "sound.h"
#include "tape.h"

void tapenoise_init(ALLEGRO_EVENT_QUEUE *queue);
void tapenoise_close(void);
void tapenoise_motorchange(int stat);
void tapenoise_streamfrag(void);
void tapenoise_send_1200 (char tone_1200th, uint8_t *no_emsgs_inout);
void tapenoise_play_ringbuf (void);
void tapenoise_activated_hook(void); /* TOHv4.1-rc9 */
int tapenoise_write_wav (tape_state_t *tape_state_live,
                         char *fn,
                         uint8_t use_phase_shift); /* TOHv4.2 */

#endif
