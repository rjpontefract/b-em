#ifndef __INC_TAPENOISE_H
#define __INC_TAPENOISE_H

#include "sound.h"

void tapenoise_init(ALLEGRO_EVENT_QUEUE *queue);
void tapenoise_close(void);
void tapenoise_motorchange(int stat);
void tapenoise_streamfrag(void);
void tapenoise_send_1200 (char tone_1200th, uint8_t *no_emsgs_inout);
void tapenoise_play_ringbuf (void);
void tapenoise_activated_hook(void); /* TOHv4.1-rc9 */

#endif
