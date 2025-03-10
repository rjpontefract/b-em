#ifndef __INC_SOUND_H
#define __INC_SOUND_H

/* Source frequencies in Hz */

#define FREQ_SO  125000  // normal sound
#define FREQ_SID 31250   // BeebSID
#define FREQ_DD  44100   // disc drive noise
#define FREQ_M5  46875   // music 5000

/* Source buffer lengths in time samples */

#define BUFLEN_SO 8000   //  64ms @ 125KHz    (must be multiple of 8)
#define BUFLEN_DD 4410   // 100ms @ 44.1KHz
#define BUFLEN_M5  750   //  16ms @ 46.875KHz (must be multiple of 3)

extern size_t buflen_m5;

extern bool sound_internal, sound_beebsid, sound_dac;
extern bool sound_ddnoise, sound_tape;
extern bool sound_music5000, sound_filter, sound_paula;
extern bool sound_tape_relay; /* TOHv2 */

void sound_init(void);
void sound_poll(int cycles);

typedef struct {
    FILE *fp;
    bool rec_started;
    const char *prompt;
    uint8_t wav_type;
    uint8_t channels;
    uint32_t samp_rate;
    uint16_t bits_samp;
} sound_rec_t;

extern sound_rec_t sound_rec;
    
bool sound_start_rec(sound_rec_t *rec, const char *filename);
void sound_stop_rec(sound_rec_t *rec);

#endif
