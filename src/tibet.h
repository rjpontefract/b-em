#ifndef __INC_TIBET_H
#define __INC_TIBET_H

#define TIBET_SPAN_INVALID 0
#define TIBET_SPAN_SILENT  1
#define TIBET_SPAN_LEADER  2
#define TIBET_SPAN_DATA    3 // also squawk

#define TIBET_ERR     log_warn
#define TIBET_FREE    free
#define TIBET_MALLOC  malloc
#define TIBET_REALLOC realloc

// TOHv3.2: version number is now expressed as integer pair
#define TIBET_VERSION_MAJOR 0
#define TIBET_VERSION_MINOR 5
//#define TIBET_VERSION "0.4"

#define TIBET_VERSION_MAX_LEN            11 // one decimal point + ten digits

#define TIBET_MAX_SPANS                  1000000

#include <stdint.h>
#include <stdio.h>

#include "tape2.h" /* for BUILD_TAPE_READ_POS_TO_EOF_ON_WRITE */

typedef struct tibet_span_hints_s {
  // these will be checked when the
  // span type arrives, and if any are incompatible
  // then an error will be generated; also allow us
  // to reject duplicates, and client must check them
  // to know which fields are valid
  char have_baud;
  char have_framing;
  char have_time;
  char have_phase;
  char have_speed;
  
  // hints:
  uint32_t baud;
  char framing[4]; // null-terminated
  float time;
  uint32_t phase;
  float speed;
} tibet_span_hints_t;


// attributes will be populated on here as we go,
// then if the span type arrives and they're not
// compatible, we reject the file at that point:
typedef struct tibet_span_s {

  uint32_t id;

  // type:
  char type;  // TIBET_SPAN_...
  char is_squawk;
  
  // 1200ths (for data/squawk):
  char *tones;
  uint32_t num_tones;
  uint32_t num_tones_alloc;
  
  /* for silent spans (OUTPUT ONLY [opo]) */
  float opo_silence_duration_secs;
  
  tibet_span_hints_t hints;
  
} tibet_span_t;


typedef struct tibet_priv_s {

  // dd_ = decoded data:
  tibet_span_t *dd_new_spans;
  uint32_t dd_new_num_spans;
  
  // TOHv3.2: string dd_version removed, now parse version to a pair of integers
  uint32_t dd_version_major;
  uint32_t dd_version_minor;
  //char *dd_version;
  
  // ds_ = decoding state:
  char ds_state;
  uint32_t ds_linenum;
  uint32_t ds_new_spans_alloc;
  uint8_t ds_decoded;
  uint8_t ds_have_version;
  
  // client state:
  uint32_t c_cur_span;
  uint32_t c_tone_pos;   // playback pointer for data and leader spans
  // TOH-3.2: playback pointer for silent spans
  // use double precision, as this is an accumulator
  double c_silence_pos_s;
  char c_prev_tonechar;
  
} tibet_priv_t;

typedef struct tibet_s {
  void *priv;
} tibet_t;

int tibet_decode     (char *buf, size_t len, tibet_t *tibet);
void tibet_finish    (tibet_t *tibet);

/*
int tibet_peek_span_type (tibet_t *t, uint8_t *span_type_out);
int tibet_peek_span_hint_framing_data_bits (tibet_t *t, uint8_t *num_data_bits_out);
int tibet_peek_span_hint_framing_parity (tibet_t *t, char *parity_out);
int tibet_peek_span_hint_framing_stop_bits (tibet_t *t, uint8_t *num_stop_bits_out);
int tibet_peek_span_hint_baud (tibet_t *t, uint32_t *baud_out);
int tibet_peek_span_id (tibet_t *d, uint32_t *span_id_out);
*/
int tibet_peek_eof (tibet_t *tp);

int tibet_have_any_data (tibet_t *t, uint8_t *have_data_out);

/* these modify client state and call stuff like c_get_span(),
 * so they're effectively also c_...() functions? */
int tibet_read_tonechar (tibet_t *t, char *value_out);
int tibet_read_bit (tibet_t *t, uint8_t baud300, char *bit_out);
int tibet_rewind (tibet_t *t);
int tibet_get_version (tibet_t *t, uint32_t *major_out, uint32_t *minor_out);
int tibet_clone (tibet_t *in, tibet_t *out);

/* made public in TOHv3 */
tibet_span_t *tibet_current_final_span (tibet_t *t);
/*int tibet_new_span (tibet_t *t);*/ /*, int type);*/

/* added in TOHv3 */
int tibet_build_output (tibet_t *t, char **opf, size_t *opf_len);
//int tibet_append_tonechar (tibet_t *t, char c);
int tibet_append_tonechar (tibet_t *t, char c, tibet_span_t *pending_span);
/*
int tibet_set_pending_data_span_hints (tibet_t *t, tibet_span_hints_t *h);
*/

/* new interface ... */
int tibet_append_leader_span (tibet_t *t, uint32_t num_2400ths, tibet_span_hints_t *hints);
int tibet_append_silent_span (tibet_t *t, float len_s, tibet_span_hints_t *hints);
int tibet_append_data_span (tibet_t *t, tibet_span_t *pending) ; /* pending = data+hints */

#ifdef BUILD_TAPE_READ_POS_TO_EOF_ON_WRITE
int tibet_ffwd_to_end (tibet_t *t);
#endif

int tibet_clone_span (tibet_span_t *out, tibet_span_t *in); // TOHv4.1

#endif /* __INC_TIBET_H */
