#ifndef __INC_TAPE_H
#define __INC_TAPE_H

#include "b-em.h"
#include "tibet.h"
#include "csw.h"
#include "uef.h"

/* TOHv3: switch to bitfield for filetypes */
#define TAPE_FILETYPE_BITS_NONE    0
#define TAPE_FILETYPE_BITS_UEF     1
#define TAPE_FILETYPE_BITS_TIBET   2
#define TAPE_FILETYPE_BITS_CSW     4
#define TAPE_FILETYPES_ALL         (1|2|4)

/* for -tapetest CLI switch */
#define TAPE_TEST_QUIT_ON_EOF     1
#define TAPE_TEST_QUIT_ON_ERR     2
#define TAPE_TEST_CAT_ON_STARTUP  4
#define TAPE_TEST_CAT_ON_SHUTDOWN 8  /* TOHv4   */
#define TAPE_TEST_SLOW_STARTUP    16 /* TOHv4.1 */
#define TAPE_TEST_BAD_MASK        0xffffffe0

#ifdef _WIN32
#define TAPE_SNPRINTF(BUF,BUFSIZE,MAXCHARS,FMTSTG,...)  _snprintf_s((BUF),(BUFSIZE),(MAXCHARS),(FMTSTG),__VA_ARGS__)
#define TAPE_SCPRINTF(FMTSTG,...)                       _scprintf((FMTSTG),__VA_ARGS__)
#define TAPE_SSCANF                                     sscanf_s
#define TAPE_STRNCPY(DEST,BUFSIZE,SRC,LEN)              _mbsncpy_s((DEST),(BUFSIZE),(SRC),(LEN))
#else
#define TAPE_SNPRINTF(BUF,BUFSIZE,MAXCHARS,FMTSTG...)   snprintf((BUF),(BUFSIZE),FMTSTG)
#define TAPE_SCPRINTF(FMTSTG...)                        snprintf(&tape_dummy_for_unix_scprintf, 0, FMTSTG)
#define TAPE_SSCANF                                     sscanf
#define TAPE_STRNCPY(DEST,BUFSIZE,SRC,LEN)              strncpy((DEST),(SRC),(LEN))
#endif /* end ndef _WIN32 */

/* tape is loaded if only *one* of TIBET, CSW or UEF is selected.
   All three means a tape-from-scratch; none at all means no tape at all. */
/* TOHv4: now a macro */
#define TAPE_IS_LOADED(filetype_bits) \
  ((0 != (filetype_bits)) && (TAPE_FILETYPES_ALL != (filetype_bits)))
  
#define TAPE_TONECODE_IS_LEGAL(c) (('0'==(c))||('1'==(c))||('S'==(c))||('L'==(c)))

typedef struct tape_shutdown_save_s {
    /* for -tapesave command-line option */
    char *filename; /* this memory belongs to argv[] */
    uint8_t filetype_bits;
    uint8_t do_compress;
} tape_shutdown_save_t;

/* TOHv3.2: introduced this struct
 * 
 * Gets rid of the messy tape global variables.
 * 
 * You should be able to swap to a different tape without
 * needing to touch any of the data on this struct. If you
 * are adding a variable that will need altering or clearing
 * when the loaded tape is changed or ejected, then it
 * belongs on tape_state_t rather than tape_vars_t. This is
 * for configuration stuff.
 */
typedef struct tape_vars_s {

    /* SETTINGS VARIABLES */
    
    /* TOHv4: mechanism for -expiry option: */
    uint8_t expired_quit;
    int64_t testing_expire_ticks; /* measured in "otherstuff" ticks */

    uint8_t overclock;    /* a.k.a. fasttape */
    
    uint8_t strip_silence_and_leader; /* TOHv3.2: another speed hack */

    uint8_t record_activated;    /* TOHv3 */
    
    ALLEGRO_PATH *save_filename; /* TOHv3 */
    ALLEGRO_PATH *load_filename;
    
    /* TOHv3: TAPE_TEST_xxx bitfields: */
    uint8_t testing_mode;
    
    /* TOHv3.2 */
    uint8_t save_always_117;
    uint8_t save_prefer_112;
    
    /* TOHv3.2, for knowing when to emit &117 */
    uint8_t cur_baud300;

    tape_shutdown_save_t quitsave_config; /* TOHv3 */
    
    uint8_t save_do_not_generate_origin_on_append; /* TOHv3.2 */
    
    uint8_t disable_phantom_block_protection;
    uint8_t disable_debug_memview;
    
} tape_vars_t;

#include "tibet.h"

typedef struct tape_state_s {

    /* STATE VARIABLES */

    /* TOHv3: filetype is now a bitfield */
    uint8_t filetype_bits;

    /* format-specific state: */
    tibet_t tibet;
    uef_state_t uef;
    csw_state_t csw;
    
    /* TIBET data spans are built here,
       before being sent to the TIBET interface
       to be included in the spans list: */
    tibet_span_t tibet_data_pending;

    // vars for local byte decoding:
    uint32_t num_1200ths_since_silence;
    uint32_t start_bit_wait_count_1200ths;
    uint8_t bits_count;
    
    uint8_t tapenoise_no_emsgs;        /* prevent "ring buffer full" spam */
    uint8_t tape_finished_no_emsgs;    /* overhaul v2: prevent "tape finished" spam */
    
    /* WRITING (w_...) : */
    uint8_t w_must_end_data; /* data pending, e.g. on w_uef_tmpchunk */

    /* used to accumulate bit periods, until we have some 1200ths to send;
       bit periods will be converted into tones (1200ths) */
    int64_t w_accumulate_bit_periods_ns;
    
    /* meanwhile this is used to accumulate all the silence and leader
       in the current span (note that w_accumulate_bit_periods_ns
       is included in these values) */
    int64_t w_accumulate_silence_ns;
    int64_t w_accumulate_leader_ns;
    
    uef_chunk_t w_uef_tmpchunk;
    uint8_t w_uef_origin_written;                   /* TOHv3.2 */
    
    /* we scan backwards for &117s and update this value according
     * to the first chunk &117 we find, whenever
     *   i)  a new UEF is loaded; or
     *   ii) Record Mode is activated.
     * This allows the save code to know whether it needs to
     * insert &117 to change the prevailing baud or not.
     */
    int32_t w_uef_prevailing_baud;            /* TOHv4 */
    
    /* for decoding the ACIA's serial stream, to convert back into bytes,
       for writing UEF */
    uint8_t w_uef_serial_frame;
    uint8_t w_uef_serial_phase;

    uint16_t w_uef_enc100_shift_value;
    uint8_t w_uef_enc100_shift_amount; /* max 16 */
    
    /* needed so we know whether we're appending UEF (and therefore
       whether we need to grey out "don't emit origin" in the UEF
       save options submenu) */
    uint8_t w_uef_was_loaded; /* TOHv3.2 */
    
    uint8_t disabled_due_to_error;
    
    uint8_t ula_motor;              /* TOHv3.3: moved here from tape_vars_t above */
    int32_t ula_rx_ns;              /* TOHv4 */
    int32_t ula_tx_ns;              /* TOHv4 */
    uint8_t ula_ctrl_reg;           /* TOHv4 (moved from serial.c) */
    
    /* whether or not this value makes it to the ACIA
       depends on whether we're in tape or RS423 mode: */
    uint8_t ula_dcd_tape;           /* TOHv3.3 */
    
    int32_t ula_dcd_2mhz_counter;   /* TOHv4 */
    int32_t ula_dcd_blipticks;      /* TOHv4 */
    int32_t ula_rs423_taperoll_2mhz_counter; /* TOHv4 */

    int32_t tape_noise_counter;     /* TOHv4 */
    
#define TAPE_TONES300_BUF_LEN 4
    
    /* for decoding 300 baud from its component 1200ths */
    char tones300[TAPE_TONES300_BUF_LEN];
    int8_t tones300_fill;
    
    /* In the interests of efficiency, the DCD part of the
       tape interface is clocked at a different rate to the
       RX part. This means that we cannot necessarily feed
       a "fresh" bit from the tape into the DCD state machine.
       So, we have to persist the most recently obtained bit
       value, and supply that to the DCD logic. */
    char ula_prevailing_rx_bit_value;
             
    /* TOHv4: pre-computed when control register is written;
       see serial_recompute_dividers_and_thresholds() */
    int32_t ula_rx_thresh_ns;
    int32_t ula_tx_thresh_ns;
    int32_t ula_rx_divider;
    int32_t ula_tx_divider;
    
    /* TOHv4: hack for serial-to-file mode.
       The correct architecture here would be a (linked) list
       of pointers to functions that should be called
       one after the other to sink a byte of TX serial data.
       For now we just have this flag */
    uint8_t ula_have_serial_sink;

} tape_state_t;


/* TOHv3.2 */
extern tape_vars_t  tape_vars;
extern tape_state_t tape_state;

/* TODO: ? should these be moved onto tape_vars too? */
extern int tapelcount, tapeledcount;

extern char tape_dummy_for_unix_scprintf;

uint32_t tape_read_u32 (uint8_t *in);
uint16_t tape_read_u16 (uint8_t *in);
uint32_t tape_read_u24 (uint8_t *in);
void tape_write_u32 (uint8_t b[4], uint32_t v); /* TOHv3 */
void tape_write_u16 (uint8_t b[2], uint16_t v); /* TOHv3 */

#include "acia.h"

void tape_rewind (void);
void tape_load (ALLEGRO_PATH *fn);
void tape_close (void);
void tape_receive (ACIA *acia, uint8_t data);
int findfilenames_new (tape_state_t *ts,
                       uint8_t show_ui_window,
                       uint8_t enable_phantom_block_protection); /* TOHv3: P.B.P. */
void tape_state_init (tape_state_t *t,
                      tape_vars_t *tv,
                      uint8_t filetype_bits,
                      uint8_t alter_menus);
void tape_state_finish (tape_state_t *t, uint8_t alter_menus);
int tape_state_clone_and_rewind (tape_state_t *out, tape_state_t *in);
int tape_load_file (const char *fn,
                    uint8_t decompress,
                    uint8_t **buf_out,
                    uint32_t *len_out);
int tape_decompress (uint8_t **out, uint32_t *len_out, uint8_t *in, uint32_t len_in);
int tape_generate_and_save_output_file (uint8_t filetype_bits,
                                        uint8_t silence112,
                                        char *path,
                                        uint8_t compress,
                                        ACIA *acia_or_null);
int tape_flush_pending_piece (tape_state_t *ts, ACIA *acia_or_null, uint8_t silence112);
int tape_zlib_compress (char *source_c,
                        size_t srclen,
                        uint8_t use_gzip_encoding,
                        char **dest,
                        size_t *destlen);
int tape_handle_acia_master_reset (tape_state_t *t);
void tape_init (tape_state_t *t, tape_vars_t *tv);
uint8_t tape_peek_for_data (tape_state_t *ts);
int tape_save_on_shutdown (uef_state_t *uef,
                           uint8_t record_is_pressed,
                           uint8_t *our_filetype_bits_inout,
                           uint8_t silence112,
                           tape_shutdown_save_t *c);
void tape_stop_motor (tape_state_t *ts);
void tape_start_motor (tape_state_t *ts, uint8_t also_force_dcd_high);
int tape_set_record_activated (tape_state_t *t, tape_vars_t *tv, ACIA *acia_or_null, uint8_t value);
uint8_t tape_is_record_activated (tape_vars_t *tv);

#define TAPE_1200TH_IN_1MHZ_INT (64 * 13) /* the famous 832us */
#define TAPE_1200TH_IN_S_FLT    (((double)TAPE_1200TH_IN_1MHZ_INT) / 1000000.0)
#define TAPE_1200TH_IN_NS_INT   (1000 * TAPE_1200TH_IN_1MHZ_INT)
#define TAPE_1200TH_IN_2MHZ_INT (2 * TAPE_1200TH_IN_1MHZ_INT)

#define TAPE_1200_HZ     (1.0 / TAPE_1200TH_IN_S_FLT) /* 1201.9 Hz */
#define TAPE_FLOAT_ZERO  0.00001
                                    
/* exported in TOHv4 */
void tape_handle_exception (tape_state_t *ts,
                            tape_vars_t *tv, /* TOHv3.2: may be NULL */
                            int error_code,
                            uint8_t eof_fatal,
                            uint8_t err_fatal,
                            uint8_t alter_menus);

/* TOHv4 */
int tape_write_bitclk (tape_state_t *ts,
                       ACIA *acia,
                       char bit,
                       int64_t ns_per_bit, /* 1024 = 1200 baud; 4096 = 300; 8192 = 150 etc. */
                       uint8_t silent,
                       uint8_t tapenoise_write_enabled,
                       uint8_t record_is_pressed,
                       uint8_t always_117,
                       uint8_t silence112,
                       uint8_t no_origin_chunk_on_append);

/* TOHv4 */
int tape_rs423_eat_1200th (tape_state_t *ts,
                           tape_vars_t *tv,
                           ACIA *acia,
                           uint8_t emit_tapenoise,
                           uint8_t *throw_eof_out);

/* TOHv4 */
int tape_fire_acia_rxc (tape_state_t *ts,
                        tape_vars_t *tv,
                        ACIA *acia,
                        int32_t acia_rx_divider,
                        uint8_t emit_tapenoise,
                        uint8_t *throw_eof_out);

#endif
