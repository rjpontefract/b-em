#ifndef __INC__CSW_H
#define __INC__CSW_H

#include "tape2.h" /* for BUILD_TAPE_READ_POS_TO_EOF_ON_WRITE */

typedef struct csw_header_s {
    uint8_t compressed;
    uint8_t ext_len;
    uint8_t version_maj;
    uint8_t version_min;
    uint32_t rate;
    uint8_t flags;
} csw_header_t;

typedef struct csw_state_s {

    csw_header_t header;
    uint32_t *pulses;
    
    /* TOHv3: for write support: */
    uint32_t pulses_fill;
    uint32_t pulses_alloc;
    /* for jittering pulse lengths, to obtain correct output frequencies: */
    double accumulated_error_smps;
    
    /* comparison to this pulse length threshold must be
       p <= thresh : short pulse
       p >  thresh : long pulse */
    uint32_t thresh_smps;
    uint32_t len_1200th_smps;
    double len_1200th_smps_perfect;
    
    /* reader state: */
    uint32_t cur_pulse;
    uint32_t sub_pulse;
    
} csw_state_t;

int csw_load_file (const char *fn, csw_state_t *csw);
void csw_close(void);
void csw_poll(void);
void csw_findfilenames(void);
int csw_clone (csw_state_t *out, csw_state_t *in);
void csw_rewind (csw_state_t *csw);
int csw_read_1200th (csw_state_t *csw, char *out_1200th);
uint8_t csw_peek_eof (csw_state_t *csw);
void csw_finish (csw_state_t *csw);

/* TOHv3: */
int csw_append_pulse (csw_state_t *csw,
                      uint32_t pulse_len_smps);
int csw_append_pulse_fractional_length (csw_state_t *csw,
                                        double pulse_len_smps);
int csw_init_blank_if_necessary (csw_state_t *csw);
int csw_append_leader (csw_state_t *csw, uint32_t num_1200ths);
int csw_append_silence (csw_state_t *csw, float len_s);
/* TOHv3: */
int csw_build_output (csw_state_t *csw,
                      uint8_t compress,
                      char **out,
                      size_t *len_out);
#ifdef BUILD_TAPE_READ_POS_TO_EOF_ON_WRITE
int csw_ffwd_to_end (csw_state_t *csw);
#endif

#endif
