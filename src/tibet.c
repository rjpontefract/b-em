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
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "tibet.h"
#include "tape.h"

#include <math.h>

#define TIBET_ALLOW_WEIRD_BAUD_RATES /* TOHv4: 75, 150, 600 baud */

#define TIBET_OUTPUT_MAX_LEN (100 * 1024 * 1024)

#define TIBET_STATE_METADATA 0
#define TIBET_STATE_DATA     1

#define TIBET_K_TIBET   "tibet"
#define TIBET_K_SILENCE "silence"
#define TIBET_K_LEADER  "leader"
#define TIBET_K_BAUD    "/baud"
#define TIBET_K_FRAMING "/framing"
#define TIBET_K_TIME    "/time"
#define TIBET_K_PHASE   "/phase"
#define TIBET_K_SPEED   "/speed"
#define TIBET_K_DATA    "data"
#define TIBET_K_SQUAWK  "squawk"
#define TIBET_K_END     "end"

// private parsing functions

static int parse_line (tibet_priv_t *tp,
                       char *line,
                       size_t len,
                       tibet_span_t *pending_span,
                       tibet_span_hints_t *active_hints);
static int metadata_line (tibet_priv_t *tp,
                          char *word1,
                          char *word2,
                          tibet_span_t *pending_data_span,
                          tibet_span_hints_t *hints_inout);
static int data_line (tibet_priv_t *tp, char *word1, char *word2, tibet_span_t *pending_span);
static int sub_line (tibet_priv_t *tp, char *line, tibet_span_t *pending_data_span, tibet_span_hints_t *active_hints);
static uint8_t line_tok_and_trim (char *line, char **word2);
static int version (uint32_t linenum, char *word1, char *word2, uint32_t *major_out, uint32_t *minor_out); // TOHv3.2
static int verify_dup_version_and_reset_baud_and_framing (tibet_priv_t *tp,
                                                          char *v,
                                                          tibet_span_hints_t *hints);
static int
enforce_hint_compat_for_silent_spans (tibet_span_hints_t *active_hints,
                                      uint32_t linenum);
static int
enforce_hint_compat_for_leader_spans (tibet_span_hints_t *active_hints,
                                      uint32_t linenum);
static int append_data_span (tibet_priv_t *priv, tibet_span_t *pending);
static int append_silent_span (tibet_priv_t *tp, float len_s, int32_t start_1200ths, tibet_span_hints_t *hints);
static int append_leader_span (tibet_priv_t *tp,
                               int32_t start_1200ths, /* or -1, before initial scan */
                               uint32_t num_2400ths,
                               tibet_span_hints_t *hints);

static int parse_float (int linenum, char *v, float *f);
static int parse_int (int linenum, char* v, uint32_t* i);
static int parse_silence (tibet_priv_t *tp, char *v, tibet_span_hints_t *active_hints);
static int parse_leader (tibet_priv_t *tp,
                         char *v,
                         tibet_span_hints_t *active_hints);
static int parse_baud (tibet_priv_t *tp, char *v, tibet_span_hints_t *hints);
static int parse_framing (tibet_priv_t *tp, char *v, tibet_span_hints_t *hints);
static int parse_time (tibet_priv_t *tp, char *v, tibet_span_hints_t *hints);
static int parse_phase (tibet_priv_t *tp, char *v, tibet_span_hints_t *hints);
static int parse_speed (tibet_priv_t *tp, char *v, tibet_span_hints_t *hints);
static int begin_data (tibet_priv_t *tp, char is_squawk, tibet_span_t *pending_span, tibet_span_hints_t *hints);


static int append_tonechar (tibet_priv_t *tp, char c, tibet_span_t *span_pending) ;

// private client-facing functions

static int c_next_tonechar (tibet_priv_t *tp, uint8_t populate_elapsed, char *tc);
static int c_read_1200 (tibet_priv_t *t, uint8_t populate_elapsed, uint8_t *out_or_null, uint32_t *cur_span_out);
static int c_get_priv (tibet_t *t, tibet_priv_t **priv, uint8_t enforce_decoded);
static int c_get_span (tibet_priv_t *tp, uint8_t populate_elapsed, tibet_span_t **span);
static tibet_span_t *c_current_span (tibet_priv_t *tp);
static int c_new_span (tibet_priv_t *tp);
static int c_alloc_priv_if_needed(tibet_t *t);
static int c_set_version_if_missing (tibet_t *t);

/* TOHv4.3 */
static void reset_client_state(tibet_priv_t * const tp);
static void next_tonechar_do_elapsed (bool const populate_elapsed, tibet_span_t * const span);

static int parse_line (tibet_priv_t *tp, char *line, size_t len, tibet_span_t *pending_span, tibet_span_hints_t *active_hints) {
                              
  size_t eol;
  char *sub;
  int e;
  
  e = TAPE_E_OK;

  // remove comments
  for (eol=0; (eol < len) && ('#' != line[eol]); eol++)
    ;
  
  if (eol>0) { // skip blank lines
    // we'll need proper null-terminated buffers for the sub-lines,
    // because we'll want to printf() them etc., so we'll have to
    // malloc each one
    sub = TIBET_MALLOC (1 + eol);
    if (NULL == sub) {
      TIBET_ERR("TIBET: line %u: Could not malloc sub-line buffer.", tp->ds_linenum+1);
      return TAPE_E_MALLOC;
    }
    sub[eol] = '\0';
    memcpy(sub, line, eol);
    e = sub_line (tp, sub, pending_span, active_hints);
    /* ?? */
    TIBET_FREE(sub); /* from cornfield valgrind testing */
  }
  
  (tp->ds_linenum)++;
  return e;
  
}



static int sub_line (tibet_priv_t *tp, char *line, tibet_span_t *pending_data_span, tibet_span_hints_t *active_hints) {

  int e;
  char *word1, *word2;
  uint32_t major, minor;
  
  line_tok_and_trim (line, &word2);
  word1 = line;
  
//printf("LN %d: word1=\"%s\", word2=\"%s\"\n", tp->ds_linenum + 1, word1, word2);
  
  // got version line yet?
  // TOHv3.2: now use separate tracking variable, ds_have_version;
  //          version is integer major/minor now
  if ( ! tp->ds_have_version ) {
    major = 0;
    minor = 0;
    e = version (tp->ds_linenum, word1, word2, &major, &minor);
    if (TAPE_E_OK != e) { return e; }
    if (major != TIBET_VERSION_MAJOR) {
      TIBET_ERR("TIBET: line %u: file major version (%u) incompatible with decoder (%u)",
                tp->ds_linenum+1, major, TIBET_VERSION_MAJOR);
      return TAPE_E_TIBET_VERSION_MAJOR;
    }
    if (minor > TIBET_VERSION_MINOR) {
      TIBET_ERR("TIBET: line %u: file minor version (%u) is newer than this decoder's (%u).",
                tp->ds_linenum+1, minor, TIBET_VERSION_MINOR);
      return TAPE_E_TIBET_VERSION_MINOR;
    }
    
    tp->ds_have_version = 1;
    tp->dd_version_major = major;
    tp->dd_version_minor = minor;
    
    return e;
    
  }
  
  if (TIBET_STATE_METADATA == tp->ds_state) {
    e = metadata_line (tp, word1, word2, pending_data_span, active_hints);
  } else {
    e = data_line (tp, word1, word2, pending_data_span);
    /* TOHv4-rc3: memory leak found by valgrind */
    if ( (TAPE_E_OK != e) && (NULL != pending_data_span->tones) ) {
        TIBET_FREE(pending_data_span->tones);
        pending_data_span->tones = NULL;
        pending_data_span->num_tones = 0;
        pending_data_span->num_tones_alloc = 0;
    }
  }
  
  return e;
}



//static int version (tibet_priv_t *tp, char *word1, char *word2) {
static int version (uint32_t linenum, char *word1, char *word2, uint32_t *major_out, uint32_t *minor_out) {

  int e;
  size_t vlen, i, num_dps, dpix;
  long major, minor;
  
  e = TAPE_E_OK;
  
  if (NULL == word2) {
    TIBET_ERR("TIBET: line %u: no space in version line: \"%s\"", linenum+1, word1);
    return TAPE_E_TIBET_VERSION_LINE_NOSPC;
  }
  if ( strcmp (word1, TIBET_K_TIBET) ) {
    TIBET_ERR("TIBET: line %u: bad version word: \"%s\"", linenum+1, word1);
    return TAPE_E_TIBET_VERSION_LINE;
  }
  // TOHv3.2: new, more flexible version logic
  vlen = strlen(word2);
  if ((vlen > TIBET_VERSION_MAX_LEN) || (vlen < 3)) {
    TIBET_ERR("TIBET: line %u: version has bad length: \"%s\"", linenum+1, word2);
    return TAPE_E_TIBET_VERSION_BAD_LEN;
  }
  for (i=0, num_dps=0, dpix=0; i<vlen; i++) {
    // ensure decimal point is always flanked by >=1 digit on each side
    if ((i>0) && (i<(vlen-1)) && (word2[i]=='.')) {
      dpix = i;
      num_dps++;
    } else if ((word2[i] < '0') || (word2[i] > '9')) {
      TIBET_ERR("TIBET: line %u: version non-numeric: \"%s\"", linenum+1, word2);
      return TAPE_E_TIBET_VERSION_NON_NUMERIC;
    }
  }
  if (num_dps != 1) {
    TIBET_ERR("TIBET: line %u: version has bad decimal point: \"%s\"", linenum+1, word2);
    return TAPE_E_TIBET_VERSION_NO_DP;
  }
#define TIBET_VERSION_PORTION_MAXLEN 5
  if (dpix > TIBET_VERSION_PORTION_MAXLEN) {
    TIBET_ERR("TIBET: line %u: major version is too long: \"%s\"", linenum+1, word2);
    return TAPE_E_TIBET_VERSION_BAD_LEN;
  }
  word2[dpix] = '\0'; // re-terminate
  major = strtol(word2, NULL, 10);
  word2[dpix] = '.'; // replace this
  if ((vlen - (1+dpix)) > TIBET_VERSION_PORTION_MAXLEN) {
    TIBET_ERR("TIBET: line %u: minor version is too long: \"%s\"", linenum+1, word2);
    return TAPE_E_TIBET_VERSION_BAD_LEN;
  }
  minor = strtol(word2 + dpix + 1, NULL, 10);
  
  // TOHv3.2: eliminated dd_version string and replace with
  //          dd_version_major and dd_version_minor integers.

  *major_out = (uint32_t) (0x7fffffff & major);
  *minor_out = (uint32_t) (0x7fffffff & minor);
  
  return e;
  
}


/* returns TRUE if there is a second word */
static uint8_t line_tok_and_trim (char *line, char **word2) {
  size_t n, len;
  len = strlen(line);
  *word2 = NULL;
  // start by eliminating trailing spaces
  while ((len>0) && (' '==line[len-1])) {
    len--;
  }
  line[len] = '\0';
  for (n=0; n < len; n++) {
    if (' ' == line[n]) {
      *word2 = line + n + 1;
      line[n] = '\0';
      break;
    }
  }
  return (*word2 != NULL);
}



static int metadata_line (tibet_priv_t *tp,
                          char *word1,
                          char *word2,
                          tibet_span_t *pending_data_span,
                          tibet_span_hints_t *hints_inout) {

  int e;
  
  e = TAPE_E_OK;

//printf("metadata_line: (%s, %s)\n", word1, word2);
  
  if ( ! strcmp (word1, TIBET_K_TIBET) ) {
    e = verify_dup_version_and_reset_baud_and_framing (tp, word2, hints_inout);
  } else if ( ! strcmp (word1, TIBET_K_SILENCE) ) {
    e = parse_silence (tp, word2, hints_inout);
  } else if ( ! strcmp (word1, TIBET_K_LEADER ) ) {
    e = parse_leader (tp, word2, hints_inout); // hints may be zeroed
  } else if ( ! strcmp (word1, TIBET_K_BAUD   ) ) {
    e = parse_baud (tp, word2, hints_inout); // hints may be zeroed
  } else if ( ! strcmp (word1, TIBET_K_FRAMING) ) {
    e = parse_framing (tp, word2, hints_inout);
  } else if ( ! strcmp (word1, TIBET_K_TIME   ) ) {
    e = parse_time (tp, word2, hints_inout);
  } else if ( ! strcmp (word1, TIBET_K_PHASE  ) ) {
    e = parse_phase (tp, word2, hints_inout);
  } else if ( ! strcmp (word1, TIBET_K_SPEED  ) ) {
    e = parse_speed (tp, word2, hints_inout);
  } else if ( ! strcmp (word1, TIBET_K_DATA   ) ) {
    if (word2 != NULL) {
      TIBET_ERR("TIBET: line %u: junk follows data keyword: \"%s\"", tp->ds_linenum+1, word2);
      e = TAPE_E_TIBET_DATA_JUNK_FOLLOWS_START;
    } else {
      e = begin_data (tp, 0, pending_data_span, hints_inout); // hints will be cloned onto data span, then wiped
    }
  } else if ( ! strcmp (word1, TIBET_K_SQUAWK ) ) {
    if (word2 != NULL) {
      TIBET_ERR("TIBET: line %u: junk follows squawk keyword: \"%s\"", tp->ds_linenum+1, word2);
      e = TAPE_E_TIBET_DATA_JUNK_FOLLOWS_START;
    } else {
      e = begin_data (tp, 1, pending_data_span, hints_inout); // hints will be cloned onto data span, then wiped
    }
  } else {
    TIBET_ERR("TIBET: line %u: unrecognised: \"%s\"", tp->ds_linenum+1, word1);
    e = TAPE_E_TIBET_UNK_WORD;
  }
  
  return e;

}


static int begin_data (tibet_priv_t *tp, char is_squawk, tibet_span_t *pending_span, tibet_span_hints_t *hints) {
  /* TIBET 0.5, TOHv3.2
     check removed -- we may need to have two adjacent data spans,
     if we need to e.g. switch framings during a block, so this restriction has been relaxed
  */
  /*
  if (t->parsed.num_spans > 1) {
    if (TIBET_SPAN_DATA == (current_span(t)-1)->type) {
      TIBET_ERR("TIBET: line %u: two adjacent data spans\n",
                state->linenum+1);
      return TAPE_E_TIBET_ADJACENT_SPANS_SAME_TYPE;
    }
  }
  */
  tp->ds_state = TIBET_STATE_DATA;
  pending_span->type = TIBET_SPAN_DATA;
  pending_span->is_squawk = is_squawk;
  pending_span->hints = *hints;
  memset(hints, 0, sizeof(tibet_span_hints_t));
  return TAPE_E_OK;
}


#define TIBET_SPEED_HINT_MIN 0.5
#define TIBET_SPEED_HINT_MAX 1.5

static int parse_speed (tibet_priv_t *tp, char *v, tibet_span_hints_t *hints) {
  int e;
  float f;
  //tibet_span_t *span;
  //span = pending_span; //c_current_span(tp);
  if (hints->have_speed) {
    TIBET_ERR("TIBET: line %u: %s specified twice for same span",
              tp->ds_linenum+1, TIBET_K_SPEED);
    return TAPE_E_TIBET_DUP_SPEED;
  }
  e = parse_float (tp->ds_linenum+1, v, &f);
  if (TAPE_E_OK != e) { return e; }
  if (f >= TIBET_SPEED_HINT_MAX) {
    TIBET_ERR("TIBET: line %u: %s is too large: \"%s\"",
              tp->ds_linenum+1, TIBET_K_SPEED, v);
    return TAPE_E_TIBET_SPEED_HINT_HIGH;
  }
  if (f <= TIBET_SPEED_HINT_MIN) {
    TIBET_ERR("TIBET: line %u: %s is too small: \"%s\"",
              tp->ds_linenum+1, TIBET_K_SPEED, v);
    return TAPE_E_TIBET_SPEED_HINT_LOW;
  }
  hints->speed = f;
  hints->have_speed = 1;
  return TAPE_E_OK;
}



static int parse_phase (tibet_priv_t *tp, char *v, tibet_span_hints_t *hints) {
  int e;
  uint32_t p;
  if (hints->have_phase) {
    TIBET_ERR("TIBET: line %u: %s specified twice for same span",
              tp->ds_linenum+1, TIBET_K_PHASE);
    return TAPE_E_TIBET_DUP_PHASE;
  }
  e = parse_int (tp->ds_linenum+1, v, &p);
  if (TAPE_E_OK != e) { return e; }
  if ((p != 0) && (p != 90) && (p != 180) && (p != 270)) {
    TIBET_ERR("TIBET: line %u: illegal %s: \"%s\"",
              tp->ds_linenum+1, TIBET_K_PHASE, v);
    return TAPE_E_TIBET_BAD_PHASE;
  }
  hints->phase = p;
  hints->have_phase = 1;
  return TAPE_E_OK;
}


#define TIBET_TIME_HINT_MAX 36000.0f

static int parse_time (tibet_priv_t *tp, char *v, tibet_span_hints_t *hints) {
  int e;
  float f;
  if (hints->have_time) {
    TIBET_ERR("TIBET: line %u: %s specified twice for same span",
              tp->ds_linenum+1, TIBET_K_TIME);
    return TAPE_E_TIBET_DUP_TIME;
  }
  e = parse_float (tp->ds_linenum+1, v, &f);
  if (TAPE_E_OK != e) { return e; }
  if (f > TIBET_TIME_HINT_MAX) {
    TIBET_ERR("TIBET: line %u: %s is too large: \"%s\"",
              tp->ds_linenum+1, TIBET_K_TIME, v);
    return TAPE_E_TIBET_TIME_HINT_TOOLARGE;
  }
  hints->time = f;
  hints->have_time = 1;
  return TAPE_E_OK;
}


static int parse_framing (tibet_priv_t *tp, char *v, tibet_span_hints_t *hints) {
  //tibet_span_t *span;
  //span = pending_span; //c_current_span(tp);
  if (hints->have_framing) {
    TIBET_ERR("TIBET: line %u: %s specified twice for same span",
               tp->ds_linenum+1, TIBET_K_FRAMING);
    return TAPE_E_TIBET_DUP_FRAMING;
  }
  if (    strcmp ("7E2", v)
       && strcmp ("7O2", v)
       && strcmp ("7E1", v)
       && strcmp ("7O1", v)
       && strcmp ("8N2", v)
       && strcmp ("8N1", v)
       && strcmp ("8E1", v)
       && strcmp ("8O1", v) ) {
    TIBET_ERR("TIBET: line %u: illegal %s: \"%s\"",
              tp->ds_linenum+1, TIBET_K_FRAMING, v);
    return TAPE_E_TIBET_BAD_FRAMING;
  }
  memcpy(hints->framing, v, 3);
  hints->framing[3] = '\0';
  hints->have_framing = 1;
  return TAPE_E_OK;
}


static int parse_baud (tibet_priv_t *tp, char *v, tibet_span_hints_t *hints) {
  int e;
  uint32_t b;
  if (hints->have_baud) {
    TIBET_ERR("TIBET: line %u: %s specified twice for same span\n",
              tp->ds_linenum+1, TIBET_K_BAUD);
    return TAPE_E_TIBET_DUP_BAUD;
  }
  e = parse_int (tp->ds_linenum+1, v, &b);
  if (TAPE_E_OK != e) { return e; }
  /* TOHv4: support intermediate rates */
  if ( 1
#ifdef TIBET_ALLOW_WEIRD_BAUD_RATES
       && (b != 75)  && (b != 150) && (b != 600)
#endif
       && (b != 300) && (b != 1200)) {
    TIBET_ERR("TIBET: line %u: illegal %s: \"%s\"",
              tp->ds_linenum+1, TIBET_K_BAUD, v);
    return TAPE_E_TIBET_BAD_BAUD;
  }
  hints->baud = b;
  hints->have_baud = 1;
  return TAPE_E_OK;
}


#define LEADER_MAX_CYCLES 100000000

static int parse_leader (tibet_priv_t *tp,
                         char *v,
                         tibet_span_hints_t *active_hints) {
  int e;
  uint32_t i;
  /* TOHv3: add check for missing field */
  if (NULL == v) {
      TIBET_ERR("TIBET: line %u: %s has missing value field",
                tp->ds_linenum+1, TIBET_K_LEADER);
      return TAPE_E_TIBET_EMPTY_LEADER;
  }
  e = enforce_hint_compat_for_leader_spans (active_hints, tp->ds_linenum+1);
  if (TAPE_E_OK != e) { return e; }
  e = parse_int (tp->ds_linenum+1, v, &i);
  if (TAPE_E_OK != e) { return e; }
  if (i > LEADER_MAX_CYCLES) {
    TIBET_ERR("TIBET: line %u: illegal %s length: \"%s\"",
              tp->ds_linenum+1, TIBET_K_LEADER, v);
    return TAPE_E_TIBET_LONG_LEADER;
  }
  /* don't write any timespan information yet */
  e = append_leader_span(tp, -1, i, active_hints);
  return e;
}



static int parse_int (int linenum, char *v, uint32_t *i) {
  size_t n, len;
  char *p;
  unsigned long lv;
  /* overhaul v3: sanity check to catch bug */
  if (NULL == v) {
    TIBET_ERR("TIBET: BUG: line %u: parse_int: NULL string!", linenum);
    return TAPE_E_BUG;
  }
  len = strlen(v);
  if (0 == len) {
    TIBET_ERR("TIBET: BUG: line %u: integer is void!", linenum);
    return TAPE_E_BUG;
  }
  if (len > 10) {
    TIBET_ERR("TIBET: line %u: integer value is too long: \"%s\"",
              linenum, v);
    return TAPE_E_TIBET_INT_TOO_LONG;
  }
  for (n=0; n < len; n++) {
    char c;
    c = v[n];
    if ((c<'0') || (c>'9')) {
      TIBET_ERR("TIBET: line %u: integer value has illegal char: \"%s\"",
                linenum, v);
      return TAPE_E_TIBET_INT_BAD_CHAR;
    }
  }
  errno = 0;
  lv = strtoul(v, &p, 10);
  if ((p != (v + len)) || (0 != errno) || (lv > ((unsigned long) 0xffffffff))) {
    TIBET_ERR("TIBET: line %u: bad integer: \"%s\"",
              linenum, v);
    return TAPE_E_TIBET_INT_PARSE;
  }
  *i = 0xffffffff & lv;
  return TAPE_E_OK;
}



#define TIBET_SILENCE_LEN_MAX  36000.0
#define TIBET_SILENCE_LEN_MIN  0.0004 // relaxed
#define TIBET_FREQ_HI          2403.8f

static int parse_silence (tibet_priv_t *tp, char *v, tibet_span_hints_t *active_hints) {
  int e;
  float f;
  //tibet_span_t *span;
  //span = pending_span; // c_current_span(tp);
  /*
  if (tp->dd_num_spans > 1) {
    if (TIBET_SPAN_SILENT == (span-1)->type) {
      TIBET_ERR("TIBET: line %u: two adjacent silent spans\n",
                tp->ds_linenum+1);
      return TAPE_E_TIBET_ADJACENT_SPANS_SAME_TYPE;
    }
  }
  */
  e = enforce_hint_compat_for_silent_spans (active_hints, tp->ds_linenum+1);
  if (TAPE_E_OK != e) { return e; }
  e = parse_float (tp->ds_linenum+1, v, &f);
  if (TAPE_E_OK != e) { return e; }
  if (f < TIBET_SILENCE_LEN_MIN) {
    /*
    TIBET_ERR("TIBET: line %u: %s is too short: \"%s\"\n",
              tp->ds_linenum+1, TIBET_K_SILENCE, v);
    return TAPE_E_TIBET_SHORT_SILENCE;
    */
    /* changed: very short periods of silence are now permitted,
       but the decoder must skip them. */
    return TAPE_E_OK;
  }
  if (f > TIBET_SILENCE_LEN_MAX) {
    TIBET_ERR("TIBET: line %u: %s has excessive length: \"%s\"",
              tp->ds_linenum+1, TIBET_K_SILENCE, v);
    return TAPE_E_TIBET_LONG_SILENCE;
  }
  /* do not write any timespan information yet ... */
  e = append_silent_span (tp, f, -1, active_hints);
  return e;
}


#define TIBET_DECIMAL_MAX_CHARS 50

static int parse_float (int linenum, char *v, float *f) {

  size_t n, len;
  char have_dp;
  char *p;
  
  len = strlen(v);
  have_dp=0;
  
  *f = 0.0f;
  
  if (len > TIBET_DECIMAL_MAX_CHARS) {
    TIBET_ERR("TIBET: line %u: decimal is too long (max. %d chars): \"%s\"",
              linenum, TIBET_DECIMAL_MAX_CHARS, v);
    return TAPE_E_TIBET_DECIMAL_TOO_LONG;
  }
  
  for (n=0; n < len; n++) {
    char c;
    c = v[n];
    if ('.' == c) {
      if (have_dp) {
        TIBET_ERR("TIBET: line %u: multiple decimal points in decimal: \"%s\"",
                  linenum, v);
        return TAPE_E_TIBET_MULTI_DECIMAL_POINT;
      }
      have_dp = 1;
      if (n==(len-1)) {
        TIBET_ERR("TIBET: line %u: decimal point at end of decimal: \"%s\"",
                  linenum, v);
        return TAPE_E_TIBET_POINT_ENDS_DECIMAL;
      }
    } else if ((c<'0') || (c>'9')) {
      TIBET_ERR("TIBET: line %u: illegal character in decimal: \"%s\"",
                linenum, v);
      return TAPE_E_TIBET_DECIMAL_BAD_CHAR;
    }
  } // next char
  
  p = NULL;
  errno = 0;
  *f = strtof(v, &p);
  
  if ((0 != errno) || (p != (v + len))) {
    TIBET_ERR ("TIBET: line %u: error parsing decimal: \"%s\"", linenum, v);
    return TAPE_E_TIBET_DECIMAL_PARSE;
  }
  
  //if (*f < TIBET_FLOAT_ZERO_MIN) {
  //  TIBET_ERR("TIBET: line %u: decimal is too small: \"%s\"\n",
  //            linenum, v);
  //  return TAPE_E_TIBET_SMALL_DECIMAL;
  //}
  
  return TAPE_E_OK;
  
}


static int
enforce_hint_compat_for_silent_spans (tibet_span_hints_t *active_hints,
                                      uint32_t linenum) {

  char *fields_msg;
  fields_msg = NULL;
  
  // (/time) is always permitted
  // silent spans: reject (baud, framing, /speed, /phase)

  if (active_hints->have_baud) {
    fields_msg = TIBET_K_BAUD;
  } else if (active_hints->have_framing) {
    fields_msg = TIBET_K_FRAMING;
  } else if (active_hints->have_speed) {
    fields_msg = TIBET_K_SPEED;
  } else if (active_hints->have_phase) {
    fields_msg = TIBET_K_PHASE;
  }
  
  if (fields_msg != NULL) {
    TIBET_ERR("TIBET: line %u: %s hint illegally supplied for silent span",
              linenum, fields_msg);
    return TAPE_E_TIBET_FIELD_INCOMPAT;
  }
  
  return TAPE_E_OK;
  
}


static int
enforce_hint_compat_for_leader_spans (tibet_span_hints_t *active_hints,
                                      uint32_t linenum) {

  char *fields_msg;
  fields_msg = NULL;

  // (/time) is always permitted
  // leader spans: reject (framing, /phase, /baud); permit (/speed)

  if (active_hints->have_baud) {
    fields_msg = TIBET_K_BAUD;
  } else if (active_hints->have_framing) {
    fields_msg = TIBET_K_FRAMING;
  } else if (active_hints->have_phase) {
    fields_msg = TIBET_K_PHASE;
  }

  if (fields_msg != NULL) {
    TIBET_ERR("TIBET: line %u: %s hint illegally supplied for leader span",
              linenum, fields_msg);
    return TAPE_E_TIBET_FIELD_INCOMPAT;
  }

  return TAPE_E_OK;

}



// TOHv3.2 rework for version changes
static int verify_dup_version_and_reset_baud_and_framing (tibet_priv_t *tp,
                                                          char *v,
                                                          tibet_span_hints_t *hints) {
  
  int e;
  uint32_t major, minor;
  
  major = 0;
  minor = 0;
  e = version (tp->ds_linenum, TIBET_K_TIBET, v, &major, &minor);
  if (e != TAPE_E_OK) { return e; }
  
  // for concatenation, enforce identical version numbers
  if ((tp->dd_version_major != major) || (tp->dd_version_minor != minor)) {
    TIBET_ERR("TIBET: line %u: %s mismatch: %u.%u vs. %u.%u", // TOHv3.2
              tp->ds_linenum+1,
              TIBET_K_TIBET,
              major,
              minor,
              tp->dd_version_major,
              tp->dd_version_minor);
    return TAPE_E_TIBET_CONCAT_VERSION_MISMATCH;
  }
  /* duplicate version line needs to reset to 8N1/1200,
   * since it represents the start of a concatenated file */
  hints->baud = 1200;
  memcpy(hints->framing, "8N1", 4);
  return TAPE_E_OK;
}



#define SPAN_TONES_ALLOC_DELTA 1024
//#define SPAN_MAX_TONES 2000000
#define SPAN_MAX_TONES 17000000 /* thinking of the 24-bit chunk &114 field here */

// just buffer all the tonechars; we'll just do some
// basic checking now, and then decode them on request later
static int data_line (tibet_priv_t *tp, char *word1, char *word2, tibet_span_t *pending_span) {

  int e;
  size_t n, m, len1, len2;
  char *line;
  e = TAPE_E_OK;

  if ( ! strcmp (word1, TIBET_K_END) ) {
//printf("tibet: data_line(): end found, going back to STATE_METADATA, wiping pending_span\n");
    tp->ds_state = TIBET_STATE_METADATA;
    e = append_data_span(tp, pending_span);
    memset(pending_span, 0, sizeof(tibet_span_t));
    return e;
  }
  
  // datastate

  //span = c_current_span(tp);
  
  // TOHv4-rc2:
  // reconstruct line from word1 and word2
  // TIBET 0.5: permit a) spaces in data lines and b) comments after data lines

  len1 = strlen(word1);
  len2 = (NULL == word2) ? 0 : strlen(word2);
  line = TIBET_MALLOC(len1+len2+1);
  if (NULL == line) {
    TIBET_ERR("TIBET: line %u: malloc failure",  tp->ds_linenum+1);
    return TAPE_E_MALLOC;
  }

  memcpy(line, word1, len1);

  for (n=0, m=0; (NULL != word2) && (n < len2); n++) {
    char c;
    c = word2[n];
    if (' ' == c) { continue; } // skip any further spaces
    // if ('#' == c) { break; }
    if ((c != '.') && (c != '-') && (c != 'P')) {
      TIBET_ERR ("TIBET: line %u: junk follows data; illegal tone character: \"%c\"",
                 tp->ds_linenum+1, c);
      e = TAPE_E_TIBET_DATA_JUNK_FOLLOWS_LINE;
      break;
    }
    line[len1+m] = word2[n]; // otherwise copy
    m++;
  }
  line[len1+m]='\0';

  for (n=0; (TAPE_E_OK == e) && (n < m+len1); n++) {
    char c;
    c = line[n];
    e = append_tonechar(tp, c, pending_span);
  }

  TIBET_FREE(line);
  line = NULL;
  
  /*
  if (TAPE_E_OK != e) {
    TIBET_FREE(span->tones);
    span->tones = NULL;
  }
  */
  
  return e;
  
}


static int append_tonechar (tibet_priv_t *tp, char c, tibet_span_t *span_pending) {
  //int e;
  tibet_span_t *span;
  char *p;
  uint32_t newlen;

  // our span is the pending span ...

  span = span_pending;

  if ((c != '.') && (c != '-') && (c != 'P')) {
    TIBET_ERR ("TIBET: line %u: illegal tone character: \"%c\"",
               tp->ds_linenum+1, c);
    return TAPE_E_TIBET_DATA_ILLEGAL_CHAR;
  }
  if (span->num_tones >= SPAN_MAX_TONES) {
    TIBET_ERR ("TIBET: line %u: too many tone characters in span", tp->ds_linenum + 1);
    return TAPE_E_TIBET_DATA_EXCESSIVE_TONES;
  }
  if ((span->num_tones > 0) && ('P' == c) && ('P' == span->tones[span->num_tones - 1])) {
    TIBET_ERR ("TIBET: line %u: illegal double pulse PP",
              tp->ds_linenum+1);
    return TAPE_E_TIBET_DATA_DOUBLE_PULSE;
  }
  if (span->num_tones >= span->num_tones_alloc) {
    newlen = span->num_tones + SPAN_TONES_ALLOC_DELTA;
    p = TIBET_REALLOC(span->tones, newlen);
    if (NULL == p) {
      TIBET_ERR ("TIBET: line %u: could not realloc tones", tp->ds_linenum + 1);
      return TAPE_E_MALLOC;
    }
    span->tones = p;
    span->num_tones_alloc = newlen;
  }
  span->tones[span->num_tones] = c;
  (span->num_tones)++; /* 2400ths */
  return TAPE_E_OK;
}


// 2400ths:
static int c_next_tonechar (tibet_priv_t *tp, uint8_t populate_elapsed, char *tc) {

  tibet_span_t *span;
  int e;

  e = c_get_span(tp, populate_elapsed, &span);
  if (TAPE_E_OK != e) { return e; }

  if (TIBET_SPAN_DATA == span->type) {
    if ('P' == span->tones[tp->c_tone_pos]) {
      // we just skip pulses
      (tp->c_tone_pos)++;
      next_tonechar_do_elapsed(populate_elapsed, span);
      return c_next_tonechar (tp, populate_elapsed, tc); // and recurse
    }
#ifdef BUILD_TAPE_SANITY
    if (   (span->tones[tp->c_tone_pos]!='.')
        && (span->tones[tp->c_tone_pos]!='-')) {
      TIBET_ERR("TIBET: BUG: illegal tone in data span->tones: &%x ('%c')\n",
                span->tones[tp->c_tone_pos],span->tones[tp->c_tone_pos]);
      return TAPE_E_BUG;
    }
#endif
    *tc = (span->tones[tp->c_tone_pos] == '.') ? '1' : '0';
    (tp->c_tone_pos)++;
  } else if (TIBET_SPAN_SILENT == span->type) {
    *tc = 'S';
    // TOHv3.2: playback silence fix
    // note double-precision accumulator
    // FIXME: probably should have just overloaded c_tone_pos
    // for this instead of needlessly introducing a pointless
    // ancillary floating point field, but never mind
    tp->c_silence_pos_s += (832.0 / 2000000.0);
  } else if (TIBET_SPAN_LEADER == span->type) {
// printf("\n\n");
    *tc = 'L';
    (tp->c_tone_pos)++;
  }

  next_tonechar_do_elapsed(populate_elapsed, span);

  // call this again, so that the span gets incremented if need be:
  e = c_get_span(tp, populate_elapsed, &span);

  return e;

}


static void next_tonechar_do_elapsed (bool const populate_elapsed, tibet_span_t * const span) {
  if (populate_elapsed) { /* TOHv4.3 */
    (span->timespan.sub_pos_4800ths) += 2;
    while (span->timespan.sub_pos_4800ths >= 4) {
      (span->timespan.sub_pos_4800ths) -= 4;
      (span->timespan.pos_1200ths)++;
    }
  }
}


bool tibet_peek_eof (tibet_t * const t) {
  tibet_priv_t *tp;
  int e;
  e = c_get_priv (t, &tp, 1);
  if (TAPE_E_OK != e) { return 1; }
  return (tp->c_cur_span >= tp->dd_new_num_spans);
}


static int c_get_span (tibet_priv_t *tp, uint8_t populate_elapsed, tibet_span_t **span) {

  tibet_span_t *_span;
  uint8_t advance; // TOHv3.2
  tape_interval_t *t0, *t1; /* TOHv4.3 */
  int32_t t0v;
  
  *span = NULL;
  advance = 0; // TOHv3.2
  
  if (tp->c_cur_span >= tp->dd_new_num_spans) {
    //TIBET_ERR("TIBETdec: attempt to read beyond final span\n");
    return TAPE_E_EOF;
  }
  _span = tp->dd_new_spans + tp->c_cur_span;
  
  // TOHv3.2: fix silence not working on playback
  if (TIBET_SPAN_SILENT == _span->type) {
    if (tp->c_silence_pos_s >= _span->opo_silence_duration_secs) {
      advance = 1;
    }
  } else {
    if (tp->c_tone_pos >= _span->num_tones) {
      // out of tones; attempt to advance span
      advance = 1;
    }
  }

  // TOHv3.2
  if (advance) {
    if (tp->c_cur_span >= tp->dd_new_num_spans) {
      //TIBET_ERR("TIBETdec: ran out of spans\n");
      return TAPE_E_EOF;
    }
    // TOHv4.3
    t0 = &(_span->timespan);
    if ((tp->c_cur_span+1) < tp->dd_new_num_spans) {
      t1 = &(tp->dd_new_spans[tp->c_cur_span+1].timespan);
      t0v = (t0->start_1200ths + t0->pos_1200ths); /* compute predicted elapsed */
  // printf("finished span %d, type %u, interval = %d + %d\n", tp->c_cur_span, tp->dd_new_spans[tp->c_cur_span].type, t0->start_1200ths, t0->pos_1200ths);
      if (! populate_elapsed) {
        if ((t1->start_1200ths > 0) && (t1->start_1200ths != t0v)) {
          TIBET_ERR("WARNING: TIBET: c_get_span(): expected %d 1200ths on new span but found %d existing (span ix %d)",
                    t0v, t1->start_1200ths, tp->c_cur_span+1);
        }
      }
      if (populate_elapsed) { /* TOHv4.3 */
  // printf("dd_new_spans[%d+1].start_1200ths = %d\n", tp->c_cur_span, t0v);
        t1->start_1200ths = t0v;
        t1->pos_1200ths = 0;
        t1->sub_pos_4800ths = 0;
      }
    }
    (tp->c_cur_span)++;
    tp->c_tone_pos = 0;
    tp->c_silence_pos_s = 0.0f;
  }

  // and again:
  _span = tp->dd_new_spans + tp->c_cur_span;

//printf("c_get_span: _span = %u, type %u\n", _span->id, _span->type);
  
  *span = _span;
  
  return TAPE_E_OK;
  
}


// value will be '0', '1', 'L', or 'S'
// -- a pair of atoms (a.k.a. tonechars): one bit at 1200 baud
static int c_read_1200 (tibet_priv_t *tp,
                        uint8_t populate_elapsed,
                        uint8_t *out_or_null,
                        uint32_t *cur_span_out) {

  tibet_span_t *span;
  int e;
  char tc1, tc2;
  
  *cur_span_out = 0;
  tc1 = 0;
  tc2 = 0;

  e = c_get_span(tp, populate_elapsed, &span);
  if (TAPE_E_OK != e) { return e; }

  e = c_next_tonechar (tp, populate_elapsed, &tc1);
  if (TAPE_E_OK != e) { return e; }
  e = c_next_tonechar (tp, populate_elapsed, &tc2);
  if (TAPE_E_OK != e) { return e; }

  do {
    // need a pair of tonechars
    *cur_span_out = tp->c_cur_span;
    if (tc1 == tc2) {
      // OK, tonechars match
      if (NULL != out_or_null) {
        *out_or_null = tc1;
      }
      return TAPE_E_OK;
    }
    // skip first tonechar, resync and try again
    tc1 = tc2;
    e = c_next_tonechar (tp, populate_elapsed, &tc2);
  } while (TAPE_E_OK == e);

  return e;
  
}




static int c_get_priv (tibet_t *t, tibet_priv_t **priv, uint8_t enforce_decoded) {
  tibet_priv_t *_priv;
  /*int e;*/
  _priv = NULL;
//printf("c_get_priv: t->priv = %p, enforce_decoded = %u\n", t->priv, enforce_decoded);
  if (NULL == t->priv) {
    // check this before malloc, so we don't malloc if error
    if ( enforce_decoded ) {
      TIBET_ERR("TIBET: attempt to use client functions before decode [priv]");
      return TAPE_E_TIBET_NO_DECODE;
    }
    c_alloc_priv_if_needed(t);

    /* note that if this is a brand new TIBET instance, a blank
       span will be created by default. The decoder needs this
       so that it has a span available to attach hint lines to, before a
       main data span actually begins. But it is a nuisance for
       creating a TIBET file from scratch. */
      /* REVERT ME */
    /*e = c_new_span(t->priv);
    if (TAPE_E_OK != e) {
      TIBET_FREE(t->priv);
      t->priv = NULL;
      return e;
    }*/
    
  }
//printf("lol\n");
  _priv = (tibet_priv_t *) t->priv;
  if (enforce_decoded && ! _priv->ds_decoded ) {
    TIBET_ERR("TIBET: attempt to use client functions before decode [ds_decoded]");
    return TAPE_E_TIBET_NO_DECODE;
  }
  *priv = _priv;
//printf("ret OK\n");
  return TAPE_E_OK;
}


/*
static int c_check_parity (uint8_t frame,
                           uint8_t parity_bit,
                           char parity_mode, // 'E' or 'O'
                           uint8_t *success_out) {
  uint8_t num_ones, i;
  num_ones = 0;
  // include bit 7 even if in 7-bit mode, it'll be zero in 7-bit mode so it doesn't matter
  for (i=0; i < 8; i++) {
    num_ones += (frame & 1);
    frame >>= 1;
  }
  num_ones += (parity_bit ? 1 : 0);
  *success_out = (parity_mode == 'E') ^ (1 == (num_ones & 1));
  return TAPE_E_OK;
}
*/

static tibet_span_t *c_current_span (tibet_priv_t *tp) {
  if (0 == tp->dd_new_num_spans) {
    return NULL;
  }
//printf("c_current_span: %p\n", tp->dd_new_spans + (tp->dd_new_num_spans - 1));
  return tp->dd_new_spans + (tp->dd_new_num_spans - 1);
}


// **********************************
// ***** BEGIN PUBLIC FUNCTIONS *****
// **********************************

/*
int tibet_set_pending_hints (tibet_t *t, tibet_span_hints_t *h) {
    c_get_priv(t, &tp, 0);
    tp->ds_hints_pending = *h;
}
*/

/*
int tibet_get_pending_data_span (tibet_t *t, tibet_span_t *span_out) {
    tibet_priv_t *tp;
    int e;
    *span_out = NULL;
    e = c_get_priv (t, &tp, 1);
    if (TAPE_E_OK != e) { return e; }
    *span_out = &(tp->ds_data_span_pending);
    return TAPE_E_OK;
}
*/

/*
int tibet_set_pending_data_span_hints (tibet_t *t, tibet_span_hints_t *h) {
    tibet_priv_t *tp;
    int e;
    e = c_get_priv (t, &tp, 1);
    if (TAPE_E_OK != e) { return e; }
    tp->ds_data_span_pending.hints = *h;
    return TAPE_E_OK;
}
*/

static uint8_t packet_len_for_framing_code (char fc[4]) {
  uint8_t q;
  q=1;                     /* start bit */
  q+=('7'==fc[0])?7:8;     /* 7-8 data bits */
  if ('N'!=fc[1]) { q++; } /* 0-1 parity bits */
  q+=('1'==fc[2])?1:2;     /* 1-2 stop bits */
  return q;
}

int tibet_build_output (tibet_t *t, char **opf, size_t *opf_len) {

  uint32_t sn;
  uint8_t pass;
  size_t pos, len;
  int e;
  char *buf;
  size_t z;
  //char *version;
  tibet_priv_t *tp;
  uint32_t major, minor;
  
  tp = NULL;
  
  *opf_len = 0;
  c_get_priv(t, &tp, 0);
  
  e = TAPE_E_OK;
  
  /* if version is undefined (file empty), use TIBET_VERSION */
  //version = (NULL == tp->dd_version) ? TIBET_VERSION : (tp->dd_version);
  //version = tp->ds_have_version ? tp->dd_version : TIBET_VERSION;
  
  if (tp->ds_have_version) {
    major = tp->dd_version_major;
    minor = tp->dd_version_minor;
  } else {
    major = TIBET_VERSION_MAJOR;
    minor = TIBET_VERSION_MINOR;
  }
  
  for (pass=0, buf=NULL, len=0; pass < 2; pass++) {
    uint8_t packet_len = 10; /* default: 8N1 nominal */
    pos = 0;
/* "tibet 0.4\n\n" */
#define TIBET_OP_FMT_HEADER "%s %u.%u\n\n"
    z = TAPE_SCPRINTF(TIBET_OP_FMT_HEADER,
                      TIBET_K_TIBET,
                      major,
                      minor);
    if ( 1 == pass ) {
      TAPE_SNPRINTF (buf+pos,
                     len+1-pos,
                     z,
                     TIBET_OP_FMT_HEADER,
                     TIBET_K_TIBET,
                     major,
                     minor);
    }
    pos += z;
    /*for (sn=0; (TAPE_E_OK == e) && (sn < (tp->dd_num_spans - 1)); sn++) {*/
    for (sn=0; (TAPE_E_OK == e) && (sn < tp->dd_new_num_spans); sn++) {
      tibet_span_t *span;
      float f;
      const char *s;
      uint32_t tn;
      span = tp->dd_new_spans + sn;
/* "leader 1234\n\n" */
#define TIBET_OP_FMT_LEADER "%s %d\n\n"
      if (TIBET_SPAN_LEADER == span->type) {
        z = TAPE_SCPRINTF(TIBET_OP_FMT_LEADER, TIBET_K_LEADER, span->num_tones);
        if ( 1 == pass ) {
          TAPE_SNPRINTF (buf+pos, len+1-pos, z, TIBET_OP_FMT_LEADER, TIBET_K_LEADER, span->num_tones);
        }
        pos += z;
/* "silence 1.2345\n\n" */
#define TIBET_OP_FMT_SILENCE "%s %f\n\n"
      } else if (TIBET_SPAN_SILENT == span->type) {
        f = span->opo_silence_duration_secs;
        z = TAPE_SCPRINTF(TIBET_OP_FMT_SILENCE, TIBET_K_SILENCE, f);
        if ( 1 == pass ) {
          TAPE_SNPRINTF (buf+pos, len+1-pos, z, TIBET_OP_FMT_SILENCE, TIBET_K_SILENCE, f);
        }
        pos += z;
/* "/baud 1200\n" */
#define TIBET_OP_FMT_BAUD "%s %d\n"
      } else if (TIBET_SPAN_DATA == span->type) {
        if (span->hints.have_baud) {
          z = TAPE_SCPRINTF(TIBET_OP_FMT_BAUD, TIBET_K_BAUD, span->hints.baud);
          if ( 1 == pass ) {
            TAPE_SNPRINTF (buf+pos,
                           len+1-pos,
                           z,
                           TIBET_OP_FMT_BAUD,
                           TIBET_K_BAUD,
                           span->hints.baud);
          }
          pos += z;
        }
/* "/framing 8N1\n" */
#define TIBET_OP_FMT_FRAMING "%s %s\n"
        if (span->hints.have_framing) {
          z = TAPE_SCPRINTF(TIBET_OP_FMT_FRAMING, TIBET_K_FRAMING, span->hints.framing);
          if ( 1 == pass ) {
            TAPE_SNPRINTF (buf+pos,
                           len+1-pos,
                           z,
                           TIBET_OP_FMT_FRAMING,
                           TIBET_K_FRAMING,
                           span->hints.framing);
          }
          pos += z;
          /* TOHv4.1, for superior line formatting */
          packet_len = packet_len_for_framing_code(span->hints.framing);
        }
/* "/speed 1.04\n" */
#define TIBET_OP_FMT_SPEED "%s %f\n"
        if (span->hints.have_speed) {
          z = TAPE_SCPRINTF(TIBET_OP_FMT_SPEED, TIBET_K_SPEED, span->hints.speed);
          if ( 1 == pass ) {
            TAPE_SNPRINTF (buf+pos,
                           len+1-pos,
                           z,
                           TIBET_OP_FMT_SPEED,
                           TIBET_K_SPEED,
                           span->hints.speed);
          }
          pos += z;
        }
/* "/time 25.3\n" */
#define TIBET_OP_FMT_TIME "%s %f\n"
        if (span->hints.have_time) {
          z = TAPE_SCPRINTF(TIBET_OP_FMT_TIME, TIBET_K_TIME, span->hints.time);
          if ( 1 == pass ) {
            TAPE_SNPRINTF (buf+pos,
                           len+1-pos,
                           z,
                           TIBET_OP_FMT_TIME,
                           TIBET_K_TIME,
                           span->hints.time);
          }
          pos += z;
        }
/* "/phase 180\n" */
#define TIBET_OP_FMT_PHASE "%s %u\n"
        if (span->hints.have_phase) {
          z = TAPE_SCPRINTF(TIBET_OP_FMT_PHASE, TIBET_K_PHASE, span->hints.phase);
          if ( 1 == pass ) {
            TAPE_SNPRINTF (buf+pos,
                           len+1-pos,
                           z,
                           TIBET_OP_FMT_PHASE,
                           TIBET_K_PHASE,
                           span->hints.phase);
          }
          pos += z;
        }
        s = span->is_squawk ? TIBET_K_SQUAWK : TIBET_K_DATA;
/* "data\n" or "squawk\n" */
#define TIBET_OP_FMT_DATA_PFX "%s\n"
        z = TAPE_SCPRINTF(TIBET_OP_FMT_DATA_PFX, s);
        if ( 1 == pass ) {
          TAPE_SNPRINTF (buf+pos, len+1-pos, z, TIBET_OP_FMT_DATA_PFX, s);
        }
        pos += z;
        for (tn = 0; tn < span->num_tones; tn++) {
          /* cosmetic hack: newline before first start bit */
          if (1 == pass) {
            buf[pos] = span->tones[tn];
          }
          pos++;
          /* TOHv4.1: better line breaks */
          if (((tn % (packet_len * 2)) == ((packet_len * 2) - 1)) && (tn < (span->num_tones - 1))) {
            if (1 == pass) { buf[pos] = '\n'; }
            pos++;
          }
        }
/* "end\n\n" */
#define TIBET_OP_FMT_DATA_SFX "\n%s\n\n"
        z = TAPE_SCPRINTF(TIBET_OP_FMT_DATA_SFX, TIBET_K_END);
        if ( 1 == pass ) {
          TAPE_SNPRINTF (buf+pos, len+1-pos, z, TIBET_OP_FMT_DATA_SFX, TIBET_K_END);
        }
        pos += z;
      } else {
        log_warn("TIBET: BUG: span to write (#%u) has illegal type %u", sn, span->type);
        e = TAPE_E_BUG;
        break;
      }
    } /* next span */
    if (TAPE_E_OK != e) { break; }
    len = pos;
    if (len >= TIBET_OUTPUT_MAX_LEN) {
      log_warn("TIBET: maximum output length exceeded");
      e = TAPE_E_TIBET_OP_LEN;
      break;
    }
    if ( 0 == pass ) {
      buf = TIBET_MALLOC(len + 1);
      if (NULL == buf) {
        log_warn("TIBET: build output, pass one: malloc failed");
        e = TAPE_E_BUG;
        break;
      }
    }
  } /* next pass */
  if ((TAPE_E_OK != e) && (buf != NULL)) {
    /* shouldn't happen, but just to be sure */
    TIBET_FREE(buf);
    buf = NULL;
  }
  *opf = buf;
  *opf_len = len;
  return e;
}


// fetch current span
tibet_span_t *tibet_current_final_span (tibet_t *t) {
  tibet_priv_t *tp;
  /*tp = (tibet_priv_t *) t->priv;*/
  tp = NULL;
  c_get_priv(t, &tp, 0);
  return c_current_span(tp);
}

#define SPANS_ALLOC_DELTA 100 /* TOHv4.3: was previously 5 */

// advance span
static int c_new_span (tibet_priv_t *tp) {

  tibet_span_t *spans, *span;
  uint32_t newsize;
  
  if (tp->dd_new_num_spans >= tp->ds_new_spans_alloc) {
    // realloc
    newsize = tp->ds_new_spans_alloc + SPANS_ALLOC_DELTA;
    if (newsize > TIBET_MAX_SPANS) {
      TIBET_ERR("TIBET: line %u: too many spans\n", tp->ds_linenum+1);
      return TAPE_E_TIBET_TOO_MANY_SPANS;
    }
    spans = TIBET_REALLOC (tp->dd_new_spans, sizeof(tibet_span_t) * newsize);
    if (NULL == spans) {
      TIBET_ERR("TIBET: line %u: could not realloc spans (newsize %u)\n",
                tp->ds_linenum+1, newsize);
      // don't free
      return TAPE_E_MALLOC;
    }
    memset(spans + tp->dd_new_num_spans,
           0,
           (sizeof(tibet_span_t) * (newsize - tp->dd_new_num_spans)));
    tp->ds_new_spans_alloc = newsize;
    tp->dd_new_spans = spans;
  }

  span = tp->dd_new_spans + tp->dd_new_num_spans;
  
  memset(span, 0, sizeof(tibet_span_t));
  
  span->id = tp->dd_new_num_spans;

//printf("c_new_span: id %u\n", span->id);
  
  (tp->dd_new_num_spans)++;
  
  return TAPE_E_OK;
  
}



int tibet_read_tonechar (tibet_t *t, uint8_t populate_elapsed, char *value_out) {

  tibet_span_t *span;
  int e;
  char tc;
  tibet_priv_t *tp;
  
  tc = '\0';
  
  /*e = c_get_priv (t, &tp, 1);*/ // decode required
  e = c_get_priv (t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }

  e = c_get_span (tp, populate_elapsed, &span);
  if (TAPE_E_OK != e) { return e; }

  e = c_next_tonechar (tp, populate_elapsed, &tc);
  if (TAPE_E_OK != e) { return e; }
  
  *value_out = tc;
  
  return TAPE_E_OK;
  
}


// value will be '0', '1', 'L', or 'S'
int tibet_read_bit (tibet_t *t,
                    uint8_t baud300,
                    uint8_t populate_elapsed, /* TOHv4.3 */
                    char *bit_out_or_null) {

  uint8_t tone0, tval0;
  uint32_t cur_span;
  uint8_t pairs_to_read;
  uint8_t n;
  int e;
  tibet_priv_t *tp;
  
  /*e = c_get_priv (t, &tp, 1);*/ // decode required
  e = c_get_priv (t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  
  cur_span = 0;
  pairs_to_read = baud300 ? 4 : 1;
  tval0 = '!';
  tone0 = '!';
  
  // at 1200 baud, this just reads one tonepair
  
  // at 300 baud it will read four tonepairs, and check them for consistency
  // if they are inconsistent it will reset the count to 0 and read another
  // four tonepairs, starting at the first mismatching tonepair, until it wins
  for (n=0; n < pairs_to_read; n++) {
    uint8_t tone, tval;
    e = c_read_1200 (tp, populate_elapsed, &tone, &cur_span);
    if (TAPE_E_OK != e) { return e; }
    if (tone == 'L') {
      tval = '1'; // for comparison purposes, 'L' becomes '1'
    } else {
      tval = tone;
    }
    if (0==n) {
      tval0 = tval;
      tone0 = tone;
    } else if (tval != tval0) { // 300 baud only
      tval0 = tval;
      tone0 = tone; // begin again
      n=0;
    }
  }
  
  // we specifically return the first value received,
  // in case we had to make up the end using leader
  
  if (bit_out_or_null != NULL) {
    *bit_out_or_null = tone0;
  }
  
  return TAPE_E_OK;
  
}

          


int tibet_rewind (tibet_t *t) {

  //tibet_span_t *dummy;
  int e;
  tibet_priv_t *tp;
  
  /*e = c_get_priv (t, &tp, 1);*/ // decode required
  e = c_get_priv (t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  
  tp->c_tone_pos = 0;
  tp->c_silence_pos_s = 0.0f;
  tp->c_cur_span = 0;
  
  return TAPE_E_OK;
  
}


//int tibet_get_version (tibet_t *t, char **version) {
// TOHv3.2: major/minor now
int tibet_get_version (tibet_t *t, uint32_t *major_out, uint32_t *minor_out) {
  tibet_priv_t *priv;
  int e;
  e = c_get_priv (t, &priv, 0);
  if (TAPE_E_OK != e) { return e; }
  *major_out = priv->dd_version_major;
  *minor_out = priv->dd_version_minor;
  return TAPE_E_OK;
}



int tibet_decode (char *buf, size_t len, tibet_t *t) {
  
  int e;
  size_t n;
  size_t line_start;
  tibet_priv_t *tp;
  tibet_span_hints_t active_hints;
  tibet_span_t pending_data_span;
  
  line_start = 0;

  /* TOHv4.3-a6
   * fix memleak: destroy any existing allocation before wiping the TIBET */
  tibet_finish(t);
  memset (t, 0, sizeof(tibet_t));
  memset (&pending_data_span, 0, sizeof(tibet_span_t));
  memset (&active_hints, 0, sizeof(tibet_span_hints_t));

  tp = NULL;
  
  e = c_get_priv(t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  
  for (n=0; n < len; n++) {
  
    unsigned char c;
    char final, nl;
    size_t linelen;
    
    c = (unsigned char) buf[n];
    
    if (((c < 0x20) || (c > 0x7e)) && (c != '\n')) {
      TIBET_ERR("TIBET: Illegal character 0x%x\n", 0xff&c);
      e = TAPE_E_TIBET_BADCHAR;
      break;
    }
    
    final = (n == (len-1));
    nl    = ('\n' == c);
    
    if (nl || final) {
      // end of line
      linelen = n - line_start;
      if (final && ! nl) {
        linelen++; // deal with final line stickiness
      }
      e = parse_line (tp, buf + line_start, linelen, &pending_data_span, &active_hints);
      line_start = n + 1; // skip newline character
    }
    
    if (TAPE_E_OK != e) { break; }
    
  }
  
  if (TAPE_E_OK == e) {

    // ensure we found a version line (unit test: "TIBET:Zero-byte file")
    //if (NULL == tp->dd_version) {
    if ((0==tp->dd_version_major) && (0==tp->dd_version_minor)) { // TOHv3.2: major/minor now
      TIBET_ERR("TIBET: version line not found; this is not a valid TIBET file.\n");
      e = TAPE_E_TIBET_ABSENT_VERSION;
    } else if (active_hints.have_time) { // ensure no "dangling" properties
      TIBET_ERR("TIBET: %s following final span\n", TIBET_K_TIME);
      e = TAPE_E_TIBET_DANGLING_TIME;
    } else if (active_hints.have_phase) {
      TIBET_ERR("TIBET: %s following final span\n", TIBET_K_PHASE);
      e = TAPE_E_TIBET_DANGLING_PHASE;
    } else if (active_hints.have_speed) {
      TIBET_ERR("TIBET: %s following final span\n", TIBET_K_SPEED);
      e = TAPE_E_TIBET_DANGLING_SPEED;
    } else if (active_hints.have_baud) {
      TIBET_ERR("TIBET: %s following final span\n", TIBET_K_BAUD);
      e = TAPE_E_TIBET_DANGLING_BAUD;
    } else if (active_hints.have_framing) {
      TIBET_ERR("TIBET: %s following final span\n", TIBET_K_FRAMING);
      e = TAPE_E_TIBET_DANGLING_FRAMING;
    }

  }
  
  if (TAPE_E_OK != e) {
    tibet_finish(t);
  } else {
    tp->ds_decoded = 1;
  }
  
  return e;
  
}



void tibet_finish (tibet_t *t) {
  uint32_t n;
  int e;
  tibet_priv_t *tp;
  if (NULL == t) { return; } /* fix: TOHv4.3-a6 */
  e = c_get_priv(t, &tp, 0);
  if (TAPE_E_OK != e) { return; }
  for (n=0; n < tp->dd_new_num_spans; n++) {
    if (NULL != tp->dd_new_spans[n].tones) {
      TIBET_FREE(tp->dd_new_spans[n].tones);
    }
  }
  if (NULL != tp->dd_new_spans) {
    TIBET_FREE(tp->dd_new_spans);
  }
  // TOHv3.2: removed
  //if (NULL != tp->dd_version) {
  //  TIBET_FREE(tp->dd_version);
  //}
  if (NULL != t->priv) {
    TIBET_FREE(t->priv);
  }
  memset(t, 0, sizeof(tibet_t));
}


static int c_alloc_priv_if_needed(tibet_t *t) {
  if (NULL == t->priv) {
    t->priv = malloc(sizeof(tibet_priv_t));
    if (NULL == t->priv) {
      TIBET_ERR("TIBET: cannot alloc NULL in->priv\n");
      return TAPE_E_MALLOC;
    }
    memset(t->priv, 0, sizeof(tibet_priv_t));
  }
  return TAPE_E_OK;
}



int tibet_clone (tibet_t *out, tibet_t *in) {

  tibet_priv_t *tp_in, *tp_out;
  int e;
  uint32_t n;
  
  out->priv = NULL;
  /* TOHv3: sanity */
  if (NULL == in) {
    TIBET_ERR("TIBET: BUG: attempt to clone NULL instance!\n");
    return TAPE_E_BUG;
  }
  if (NULL == in->priv) {
    TIBET_ERR("TIBET: BUG: attempt to clone instance w/NULL priv!\n");
    return TAPE_E_BUG;
  }
  e = c_get_priv(in, &tp_in, 0);
  if (TAPE_E_OK != e) { return e; }
  /* TOHv3: make sure input TIBET has a version set */
  e = c_set_version_if_missing(in);
  if (TAPE_E_OK != e) { return e; }
  if (NULL == tp_in) {
    TIBET_ERR("TIBET: attempt to clone a NULL instance\n");
    return TAPE_E_BUG;
  }
  e = c_get_priv(out, &tp_out, 0);
  if (TAPE_E_OK != e) { return e; }
  //tp_out = out->priv;
  memcpy(tp_out, tp_in, sizeof(tibet_priv_t));
  tp_out->dd_new_spans = NULL;

  /* TOHv3: make sure tp_in alloced size record is sane */
  if (tp_in->ds_new_spans_alloc < tp_in->dd_new_num_spans) {
    log_warn("TIBET: clone: BUG: ds_new_spans_alloc < dd_num_spans somehow\n");
    tibet_finish(out);
    return TAPE_E_BUG;
  }
  /* TOHv3: bugfix: alloc value was not accurate
   * on the destination TIBET. We were allocing just dd_num_spans,
   * but leaving tp_out->ds_new_spans_alloc having the same value as it was
   * on tp_in; clearly an inconsistency, and a problem if anything
   * else gets appended to tp_out later. We'll just alloc the full
   * tp_in->ds_new_spans_alloc here, then tp_out->ds_new_spans_alloc is also
   * accurate. */
  tp_out->dd_new_spans = malloc(tp_in->ds_new_spans_alloc * sizeof(tibet_span_t));
  if (NULL == tp_out->dd_new_spans) {
    TIBET_ERR("TIBET: could not malloc clone spans\n");
    tibet_finish(out);
    return TAPE_E_MALLOC;
  }
  /* TOHv3: insurance: make sure the whole allocated tp_out->dd_new_spans
   * is zeroed (else only tp_in->dd_num_spans worth will be meaningfully
   * populated, leaving an uninitialised disaster area at the end of
   * the buffer) */
  memset(tp_out->dd_new_spans,
         0,
         tp_in->ds_new_spans_alloc * sizeof(tibet_span_t));
  /* now copy just dd_num_spans across */
  // memcpy(tp_out->dd_new_spans,
  //        tp_in->dd_new_spans,
  //        tp_in->dd_new_num_spans * sizeof(tibet_span_t));
  // for (n=0; n < tp_in->dd_new_num_spans; n++) {
  //   tibet_span_t *span;
  //   tp_out->dd_new_spans[n] =
  //   span = tp_out->dd_new_spans + n;
  //   span->tones = NULL;
  // }
  for (n=0; n < tp_in->dd_new_num_spans; n++) {
    e = tibet_clone_span (tp_out->dd_new_spans + n, tp_in->dd_new_spans + n); // TOHv4.1
    if (TAPE_E_OK != e) {
      tibet_finish(out);
      return e;
    }
  }
  return TAPE_E_OK;
}


// TOHv4.1
int tibet_clone_span (tibet_span_t *out, tibet_span_t *in) {
  *out = *in;
  out->tones = NULL;
  if (in->tones != NULL) {
    out->tones = malloc(in->num_tones_alloc);
    if (NULL == out->tones) {
      TIBET_ERR("TIBET: could not malloc clone span tones\n");
      return TAPE_E_MALLOC;
    }
    memcpy(out->tones, in->tones, in->num_tones_alloc);
  }
  return TAPE_E_OK;
}


int tibet_append_tonechar (tibet_t *t, char c, tibet_span_t *pending_span) {
  tibet_priv_t *p;
  int e;
  /* make sure we have a version */
  /* TOHv3: add dedicated function for this */
  e = c_set_version_if_missing(t);
  if (TAPE_E_OK != e) { return e; }
  p = (tibet_priv_t *) t->priv;
  return append_tonechar(p, c, pending_span);
}

/* TOHv3 */
static int c_set_version_if_missing (tibet_t *t) {
  tibet_priv_t *p;
  //size_t vl;
  int e;
  e = c_get_priv(t, &p, 0);
  if (TAPE_E_OK != e) { return e; }
  /* make sure we have a version */
  if ((0==p->dd_version_major)&&(0==p->dd_version_minor)) { // TOHv3.2: major/minor
    p->dd_version_major = TIBET_VERSION_MAJOR;
    p->dd_version_minor = TIBET_VERSION_MINOR;
  }
  return TAPE_E_OK;
}


int tibet_append_leader_span (tibet_t *t, int32_t start_1200ths, uint32_t num_2400ths, tibet_span_hints_t *hints) {
  tibet_priv_t *tp;
  int e;
  e = c_get_priv(t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  return append_leader_span(tp, start_1200ths, num_2400ths, hints);
}


// hints will be copied, and zeroed on return
static int append_leader_span (tibet_priv_t *tp,
                               int32_t start_1200ths, /* or -1, before initial scan */
                               uint32_t num_2400ths,
                               tibet_span_hints_t *hints) {

  int e;
  tibet_span_t *span;

//printf("tibet_append_leader_span\n"); /*, ");*/

  if (num_2400ths < 2) {
      TIBET_ERR("TIBET: WARNING: append leader span: clamping num_2400ths: %u -> 2",
                num_2400ths);
      num_2400ths = 2;
  }

// printf("append_leader_span: start 1200ths %d, num 2400ths %u\n", start_1200ths, num_2400ths);

  if (start_1200ths < 0) { /* TOHv4.3: bugfix */
    start_1200ths = 0;
  }

  e = c_new_span(tp);
  if (TAPE_E_OK != e) { return e; }
  span = tp->dd_new_spans + (tp->dd_new_num_spans - 1);
  span->hints            = *hints;
  span->num_tones        = num_2400ths;
  span->type             = TIBET_SPAN_LEADER;
  memset(hints, 0, sizeof(tibet_span_hints_t));
  /* TOHv4.3 */
  if (start_1200ths > 0) {
    span->timespan.start_1200ths = start_1200ths;
    span->timespan.pos_1200ths = (num_2400ths / 2);
  }
  return TAPE_E_OK;
}


int tibet_append_silent_span (tibet_t *t, float len_s, int32_t start_1200ths, tibet_span_hints_t *hints) {
  int e;
  tibet_priv_t *tp;
  e = c_get_priv(t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  return append_silent_span (tp, len_s, start_1200ths, hints);
}


// hints will be copied, and zeroed on return
static int append_silent_span (tibet_priv_t *tp, float len_s, int32_t start_1200ths_or_minus_one, tibet_span_hints_t *hints) {
  int e;
  tibet_span_t *span;
  e = c_new_span(tp);
  if (TAPE_E_OK != e) { return e; }
  span = tp->dd_new_spans + (tp->dd_new_num_spans - 1);
// printf("append_silent_span (%u): len %f\n", (tp->dd_new_num_spans - 1), len_s);
  span->type = TIBET_SPAN_SILENT;
  span->opo_silence_duration_secs = len_s;
  span->hints = *hints;
  memset(hints, 0, sizeof(tibet_span_hints_t));
  /* TOHv4.3 */
  memset(&(span->timespan), 0, sizeof(tape_interval_t));
// printf("append_silent_span @ %d: start_1200ths = %d, len %f\n", tp->dd_new_num_spans-1, start_1200ths_or_minus_one, len_s);
  if (start_1200ths_or_minus_one >= 0) {
    span->timespan.start_1200ths = start_1200ths_or_minus_one;
    span->timespan.pos_1200ths = (int32_t) (0.5 + (len_s * (1.0/TAPE_1200TH_IN_S_FLT) /* 1201.9 */));
  }
  return TAPE_E_OK;
}


/* caution! heap memory on pending->tones will be stolen,
   and pending itself will never be the same again! */
int tibet_append_data_span (tibet_t *t, tibet_span_t *pending) {
  int e;
  tibet_priv_t *tp;
  e = c_get_priv(t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  return append_data_span(tp, pending);
}


static int append_data_span (tibet_priv_t *priv, tibet_span_t *pending) {
  int e;
  tibet_span_t *span;

  if (pending->type != TIBET_SPAN_DATA) {
    TIBET_ERR("TIBET: BUG: pending span has bad type (%u)", pending->type);
    return TAPE_E_BUG;
  }
//printf("tibet_append_data_span\n");

  e = c_new_span(priv);
  if (TAPE_E_OK != e) { return e; }
  /*
  e = c_get_span(tp, &span);
  if (TAPE_E_OK != e) { return e; }
  */
  span = priv->dd_new_spans + (priv->dd_new_num_spans - 1);

//printf("tibet_append_data_span: %d + %d = %d\n", span->timespan.start_1200ths, span->timespan.pos_1200ths, span->timespan.start_1200ths+span->timespan.pos_1200ths);

  memcpy(span, pending, sizeof(tibet_span_t));
  /* let's just make sure there are no little misunderstandings */
  memset(pending, 0, sizeof(tibet_span_t));
  return TAPE_E_OK;
}





int tibet_have_any_data (tibet_t *t, uint8_t * const have_data_out) {
  int e;
  uint32_t i;
  tibet_priv_t *tp;
  *have_data_out = 0;
  /* we don't want a call to c_get_priv() to cause a priv instance
   * to be created, if we don't need one, so this is a rare case of
   * checking t->priv first */
  if (NULL == t->priv) { return TAPE_E_OK; }
  e = c_get_priv(t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  for (i=0; i < tp->dd_new_num_spans; i++) {
    if (TIBET_SPAN_DATA == tp->dd_new_spans[i].type) {
        *have_data_out = 1;
        return TAPE_E_OK;
    }
  }
  return e;
}


int tibet_ffwd_to_end (tibet_t *t) {

    int e;
    tibet_priv_t *tp;
    
    e = c_get_priv(t, &tp, 0);
    if (TAPE_E_OK != e) { return e; }
    
    /* force read pointer past end of file, EOF */
    tp->c_cur_span = tp->dd_new_num_spans;
    return TAPE_E_OK;
    
}

int tibet_get_duration (tibet_t *t, int32_t *out_1200ths) {

    int e;
    tibet_priv_t *tp;
    tibet_span_t *span;

    *out_1200ths = -1;

    e = c_get_priv(t, &tp, 0);
    if (TAPE_E_OK != e) { return e; }

    if (0 == tp->dd_new_num_spans) {
        *out_1200ths = 0;
        return TAPE_E_OK;
    }

    span = tp->dd_new_spans + tp->dd_new_num_spans - 1;
/*printf("tibet_get_duration: %d spans: final is (%d + %d) type %u or %u\n", tp->dd_new_num_spans, span->timespan.start_1200ths, span->timespan.pos_1200ths, span->type, span->timespan.type);*/
    *out_1200ths = span->timespan.start_1200ths + span->timespan.pos_1200ths;

    return TAPE_E_OK;

}

/* TOHv4.3 */
int tibet_get_num_spans (tibet_t *t, uint32_t *out) {
    int e;
    tibet_priv_t *tp;
    *out = 0;
    e = c_get_priv(t, &tp, 0);
    if (TAPE_E_OK != e) { return e; }
    *out = tp->dd_new_num_spans;
    return e;
}

/* TOHv4.3 */
int tibet_get_time_interval_for_span (tibet_t * const t,
                                      uint32_t const span_ix,
                                      tape_interval_t * const interval_out) {
    int e;
    tibet_priv_t *tp;
    e = c_get_priv(t, &tp, 0);
    if (TAPE_E_OK != e) { return e; }
    if (span_ix >= tp->dd_new_num_spans) {
        TIBET_ERR("TIBET: BUG: tibet_get_time_interval_for_span: bad span ix (%u, have %u spans)",
                  span_ix, tp->dd_new_num_spans);
        return TAPE_E_BUG;
    }
    *interval_out = tp->dd_new_spans[span_ix].timespan;
    return e;
}

int tibet_get_type_for_span (tibet_t * const t, uint32_t const span_ix, char * const type_out) {
    int e;
    tibet_priv_t *tp;
    e = c_get_priv(t, &tp, 0);
    if (TAPE_E_OK != e) { return e; }
    if (span_ix >= tp->dd_new_num_spans) {
        TIBET_ERR("TIBET: BUG: tibet_get_type_for_span: bad span ix (%u, have %u spans)",
                  span_ix, tp->dd_new_num_spans);
        return TAPE_E_BUG;
    }
    *type_out = tp->dd_new_spans[span_ix].type;
    return e;
}

/* TOHv4.3 */
int tibet_change_current_span(tibet_t * const t, uint32_t const span_ix) {
    tibet_priv_t *tp;
    int e;
    e = c_get_priv(t, &tp, 0);
    if (TAPE_E_OK != e) { return e; }
    if (span_ix >= tp->dd_new_num_spans) {
        TIBET_ERR("TIBET: BUG: tibet_change_current_span: bad span ix (%u, total %u)",
                  span_ix, tp->dd_new_num_spans);
        return TAPE_E_BUG;
    }
    tp->c_cur_span = span_ix;
    reset_client_state(tp);
    return e;
}

/* TOHv4.3 */
static void reset_client_state(tibet_priv_t * const tp) {
    tp->c_tone_pos = 0;
    tp->c_silence_pos_s = 0.0;
    tp->c_prev_tonechar = '\0';
}


/* **************** */


/* the following functions are unused by B-Em, so they
   are uncompiled for this application ... */

#if 0


int tibet_peek_span_type (tibet_t *t, uint8_t *span_type_out) {
  int e;
  tibet_span_t *span;
  tibet_priv_t *tp;
  
  *span_type_out = TIBET_SPAN_INVALID;
  
  /*e = c_get_priv (t, &tp, 1);*/ // decode required
  e = c_get_priv (t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  e = c_get_span (tp, &span);
  if (TAPE_E_OK != e) { return e; }
  *span_type_out = span->type;
  return TAPE_E_OK;
}


int tibet_peek_span_hint_framing_data_bits (tibet_t *t, uint8_t *num_data_bits_out) {

  int e;
  tibet_span_t *span;
  tibet_priv_t *tp;
  
  *num_data_bits_out = 8;
  
  /*e = c_get_priv (t, &tp, 1);*/ // decode required
  e = c_get_priv (t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  e = c_get_span (tp, &span);
  if (TAPE_E_OK != e) { return e; }
  
  if ( span->have_hint_framing ) {
    *num_data_bits_out = (span->hint_framing[0] == '8') ? 8 : 7;
  }
  return TAPE_E_OK;
}


int tibet_peek_span_hint_framing_parity (tibet_t *t, char *parity_out) {

  int e;
  tibet_span_t *span;
  tibet_priv_t *tp;
  
  *parity_out = 'N';
  
  /*e = c_get_priv (t, &tp, 1);*/ // decode required
  e = c_get_priv (t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  e = c_get_span (tp, &span);
  if (TAPE_E_OK != e) { return e; }
  
  if ( span->have_hint_framing ) {
    *parity_out = span->hint_framing[1];
  }
  return TAPE_E_OK;
}


int tibet_peek_span_hint_framing_stop_bits (tibet_t *t, uint8_t *num_stop_bits_out) {

  int e;
  tibet_span_t *span;
  tibet_priv_t *tp;
  
  *num_stop_bits_out = 1;
  
  /*e = c_get_priv (t, &tp, 1);*/ // decode required
  e = c_get_priv (t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  e = c_get_span (tp, &span);
  if (TAPE_E_OK != e) { return e; }
  
  if ( span->have_hint_framing ) {
    *num_stop_bits_out = (span->hint_framing[2] == '1') ? 1 : 2;
  }
  return TAPE_E_OK;
  
}


int tibet_peek_span_hint_baud (tibet_t *t, uint32_t *baud_out) {

  int e;
  tibet_span_t *span;
  tibet_priv_t *tp;
  
  *baud_out = 1200;
  
  /*e = c_get_priv (t, &tp, 1);*/ // decode required
  e = c_get_priv (t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  e = c_get_span (tp, &span);
  if (TAPE_E_OK != e) { return e; }
  
  if ( span->have_hint_baud ) {
    *baud_out = span->hint_baud;
  }
  return TAPE_E_OK;
  
}



int tibet_peek_span_id (tibet_t *t, uint32_t *span_id_out) {

  int e;
  tibet_span_t *span;
  tibet_priv_t *tp;
  
  *span_id_out = 0xffffffff;
  
  /*e = c_get_priv (t, &tp, 1);*/ // decode required
  e = c_get_priv (t, &tp, 0);
  if (TAPE_E_OK != e) { return e; }
  e = c_get_span (tp, &span);
  if (TAPE_E_OK != e) { return e; }
  
  *span_id_out = span->id;
  return TAPE_E_OK;
}
#endif /* 0 */

/*
int tibet_read_frame (tibet_t *t,
                      uint8_t num_data_bits,
                      char parity, // 'N', 'O' or 'E'
                      uint8_t num_stop_bits,
                      uint8_t baud300,
                      uint8_t ignore_stop2_as_acia_does,
                      uint8_t *frame_out,
                      uint8_t *framing_err_out,
                      uint8_t *parity_err_out) {
                      
  int e;
  uint8_t n, parity_ok;
  uint8_t bit, frame;
  tibet_priv_t *tp;
  
  e = c_get_priv (t, &tp, 1); // decode required
  if (TAPE_E_OK != e) { return e; }
  
  *parity_err_out  = 0;
  *framing_err_out = 0;
  *frame_out       = 0;
  
  // await start bit
  bit = 0;
  do {
    e = tibet_read_bit (t, baud300, &bit);
    if (TAPE_E_OK != e) { return e; }
  } while (bit != '0');
  
  frame = 0;
  for (n=0; n < num_data_bits; n++) {
    e = tibet_read_bit (t, baud300, &bit);
    if (TAPE_E_OK != e) { return e; } // might be EOF
    frame = (frame >> 1) & 0x7f;
    frame |= ('0'==bit) ? 0 : 0x80;
  }
  if (num_data_bits == 7) {
    frame = (frame >> 1) & 0x7f;
  }
  
  *frame_out = frame;
  
  if (parity != 'N') {
    e = tibet_read_bit (t, baud300, &bit);
    if (TAPE_E_OK != e) { return e; } // might be EOF
    e = c_check_parity (frame, bit, parity, &parity_ok);
    if (TAPE_E_OK != e) { return e; }
    *parity_err_out = parity_ok ? 0 : 1;
  }
  
  // ACIA seems just to ignore the second stop bit.
  // if the second stop bit is incorrectly a 1, then the ACIA


  // just treats it as a new start bit.
  if (ignore_stop2_as_acia_does) {
    num_stop_bits = 1;
  }
  
  // note that MOS doesn't care about framing errors!
  for (n=0; n < num_stop_bits; n++) {
    e = tibet_read_bit (t, baud300, &bit);
    if (TAPE_E_OK != e) { return e; } // might be EOF
    if ((bit != '1') && (bit != 'L')) {
      *framing_err_out = 1;
      break; // ? quit if first stop is wrong?
    }
  }
  
  return TAPE_E_OK;

}
*/

