/* - overhaul by Diminished */

#include "b-em.h"
#include "acia.h"
#include "led.h"
#include "tape.h"
#include "serial.h"
#include "tapenoise.h"   /* for sound_tape flag */
#include "uef.h"
#include "csw.h"
#include "sysacia.h"
#include "tibet.h"
#include "gui-allegro.h" /* TOHv3: for greying-out menu items */
#include "main.h"        /* for shutdown codes */

#include <ctype.h>
#include <zlib.h>

/* TOHv3.2: moved globals onto this struct: */
tape_vars_t tape_vars;
int tapelcount,tapeledcount;
char tape_dummy_for_unix_scprintf;

static int check_pending_tibet_span_type (tape_state_t *t, uint8_t type);
static void tibet_load (const char *fn);
static void tibetz_load (const char *fn);
static int tape_write_end_data (tape_state_t *t);
static int tape_write_start_data (tape_state_t *t, uint8_t always_117, serial_framing_t *f);
static int tape_write_silence_2 (tape_state_t *t, double len_s, uint8_t silence112);
static int tape_write_1200th (tape_state_t *t,
                              serial_framing_t *f,
                              uint8_t tapenoise_active,
                              char value);
static int tape_write_leader (tape_state_t *t, uint32_t num_1200ths);
static void framing_to_string (char fs[4], serial_framing_t *f);
static int
tape_tone_1200th_from_back_end (uint8_t strip_silence_and_leader, /* speedup */
                                tape_state_t *ts,
                                uint8_t awaiting_start_bit,
                                uint8_t enable_phantom_block_protection, // TOHv3.3
                                char *tone_1200th_out);
static int
wp_bitclk_start_data_if_needed (tape_state_t *ts,
                                /* need to provide baud rate + framing,
                                for TIBET hints, UEF &114 header, etc: */
                                serial_framing_t *f,
                                uint8_t always_117,
                                uint8_t record_is_pressed);
static int wp_bitclk_accumulate_leader (tape_state_t *t,
                                        uint8_t tapenoise_active,
                                        int64_t ns_per_bit,
                                        uint8_t record_is_pressed,
                                        uint8_t *reset_shift_reg_out);
static int wp_end_data_section_if_ongoing (tape_state_t *t,
                                           uint8_t record_is_pressed,
                                           uint8_t *reset_shift_register_out);
static int
wp1_4k8_init_blank_tape_if_none_loaded (uint8_t *filetype_bits_inout,
                                        uint8_t record_activated);
static int wp_flush_accumulated_leader_maybe (tape_state_t *t, uint8_t record_is_pressed);
static int wp_bitclk_output_data (tape_state_t *ts,
                                  ACIA *acia,
                                  serial_framing_t *f,
                                  uint8_t tapenoise_active,
                                  int64_t ns_per_bit,
                                  uint8_t record_is_pressed,
                                  uint8_t always_117);

#define SERIAL_STATE_AWAITING_START 0
#define SERIAL_STATE_BITS           1
#define SERIAL_STATE_PARITY         2
#define SERIAL_STATE_AWAITING_STOP  3

#define TAPE_FILE_MAXLEN            (16 * 1024 * 1024)
#define TAPE_MAX_DECOMPRESSED_LEN   (32 * 1024 * 1024)

static int tape_read_1200th (tape_state_t *ser, char *bit_out);
/*static uint8_t tape_peek_eof (tape_state_t *ser);*/ /* not used in TOHv4 */
static int tape_rewind_2 (tape_state_t *ser);
static int tibet_load_file (const char *fn, uint8_t decompress, tibet_t *t);
static void load_successful (char *filename);
static void csw_load (const char *fn);
static void tibet_load_2 (const char *fn, uint8_t decompress);
static void uef_load (const char *fn);
static int tape_uef_flush_incomplete_frame (uef_state_t *uef_inout,
                                            uint8_t *serial_phase_inout,
                                            uint8_t *serial_frame_inout,
                                            uef_chunk_t *chunk_inout); /* TOHv3.2 */

static struct
{
        char *ext;
        void (*load)(const char *fn);
        void (*close)();
}
loaders[]=
{
/* TOHv2: strange mixed-metaphor here
   (terminated array & numeric limit) */
/* TOHv3: individual close-functions are gone;
   rely on universal tape_state_finish() now */
#define TAPE_NUM_LOADERS 4  /* TOHv2 */
        {"UEF",    uef_load,    NULL},
        {"CSW",    csw_load,    NULL},
        {"TIBET",  tibet_load,  NULL},  /* TOH */
        {"TIBETZ", tibetz_load, NULL},  /* TOH */
        {NULL, NULL, NULL} /* TOHv2 */
};

/* save-on-shutdown (-tapesave) configuration: */
tape_shutdown_save_t tape_shutdown_save_config; /* exported */

/* all of our state lives here: */
tape_state_t tape_state; /* exported */

/* used to determine whether "catalogue tape" should be greyed out */
#ifdef BUILD_TAPE_MENU_GREYOUT_CAT
uint8_t tape_peek_for_data (tape_state_t *ts) {

    uint8_t r;
    uint8_t have_tibet_data;
    int e;
    
    r = 0;
    have_tibet_data = 0;
    
    if (ts->disabled_due_to_error) {
        return 0;
    }
    
    if (    (ts->filetype_bits & TAPE_FILETYPE_BITS_CSW)
         && (ts->csw.pulses_fill > 0)) {
        r |= TAPE_FILETYPE_BITS_CSW; /* = 4 */
    }
    if (    (ts->filetype_bits & TAPE_FILETYPE_BITS_UEF)
         && (ts->uef.num_chunks > 0)) {
        r |= TAPE_FILETYPE_BITS_UEF; /* = 1 */
    }
    if (ts->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {
        e = tibet_have_any_data (&(ts->tibet), &have_tibet_data);
        if ((TAPE_E_OK == e) && have_tibet_data) {
            r |= TAPE_FILETYPE_BITS_TIBET; /* = 2 */
        }
    }

    return r;
    
}
#endif /* BUILD_TAPE_MENU_GREYOUT_CAT */

void tape_load(ALLEGRO_PATH *fn)
{
        int c = 0;
        const char *p, *cpath;

        if (!fn) return;
        p = al_get_path_extension(fn);
        if (!p || !*p) return;
        if (*p == '.')
            p++;
        cpath = al_path_cstr(fn, ALLEGRO_NATIVE_PATH_SEP);
        log_info("tape: Loading %s %s", cpath, p);
        while (loaders[c].ext)
        {
                if (!strcasecmp(p, loaders[c].ext))
                {
                        loaders[c].load(cpath);
                        return;
                }
                c++;
        }
}

/* define this to get messages on poll timing */
/*
#ifdef BUILD_TAPE_CHECK_POLL_TIMING
#include <sys/time.h>
static uint64_t poll_timing_us_prev = 0;
static uint32_t poll_timing_num_calls = 0;
#endif
*/

static int
wp1_4k8_init_blank_tape_if_none_loaded (uint8_t *filetype_bits_inout,
                                        uint8_t record_activated) {
    /* tape loaded? */
    if (TAPE_FILETYPE_BITS_NONE != *filetype_bits_inout) {
        return TAPE_E_OK; /* yes */
    }
    /* no tape loaded: initialise blank tape; all three/four
       file types simultaneously, since we don't know
       which one the user wants yet. */
    *filetype_bits_inout = TAPE_FILETYPES_ALL;
    log_info("tape: initialised blank tape");
    gui_alter_tape_menus(*filetype_bits_inout);
    gui_set_record_mode (record_activated);
    return TAPE_E_OK;
}

/* tape.c */
static int wp_bitclk_handle_silence (tape_state_t *t,
                                     uint8_t silent,
                                     uint8_t tapenoise_active,
                                     uint8_t record_is_pressed,
                                     int64_t ns_per_bit,
                                     uint8_t silence112,
                                     uint8_t *reset_shift_register_out) {
                               
    int e;
    int64_t num_1200ths, i;
    
    e = TAPE_E_OK;
                               
    /* handle silence logic:
       1. if silent, wipe the ACIA's tx data, send tape noise
       2. if not silent, but previously silent, end section, flush the silence to tape
    */
    
    if ( silent ) {
        e = wp_end_data_section_if_ongoing(t, record_is_pressed, reset_shift_register_out);
        if (record_is_pressed) { /* only accumulate silence if record is pressed */
            t->w_accumulate_silence_ns += ns_per_bit;
        }
        t->w_accumulate_bit_periods_ns += ns_per_bit;
        num_1200ths = (t->w_accumulate_bit_periods_ns) / TAPE_1200TH_IN_NS_INT;
        /* consume, if > 0; leave any remainder on variable for next time */
        t->w_accumulate_bit_periods_ns -= (num_1200ths * TAPE_1200TH_IN_NS_INT);
        for (i=0; tapenoise_active && (i < num_1200ths); i++) { /* TOHv4: tones_per_bit loop */
            tapenoise_send_1200 ('S', &(t->tapenoise_no_emsgs));
        }
    } else if (t->w_accumulate_silence_ns > 0) {
        /* a prior silent section has ended, so we need to write it out */
        if (record_is_pressed) {
            e = tape_write_silence_2 (t,
                                      ((double) t->w_accumulate_silence_ns) / 1000000000.0,
                                      silence112);
        }
        t->w_accumulate_silence_ns = 0;
    }
    
    return e;
    
}


/* TOHv3.2: rework; flush partial frames out to UEF properly */
/* tape.c */
static int wp_end_data_section_if_ongoing (tape_state_t *t,
                                           uint8_t record_is_pressed,
                                           uint8_t *reset_shift_register_out) {
                                            
    int e;
    
    e = TAPE_E_OK;
    *reset_shift_register_out = 0;
                                            
    if (t->w_must_end_data) {

        /* Make sure there isn't a half-finished frame kicking about
           on the UEF reservoir; this would end up being prepended to the start of
           the next block, with disastrous consequences. This can happen
           if the ACIA pipelining is wrong and the master reset that fires
           at the end of the block lets half a byte escape or something.
           Particular culprits: 300 baud, fast-tape writing. */
           
        if (record_is_pressed && (t->w_uef_serial_phase != 0)) {

            log_warn("tape: partial frame (phase=%u, expected 0)",
                     t->w_uef_serial_phase);

            e = tape_uef_flush_incomplete_frame (&(t->uef),
                                                 &(t->w_uef_serial_phase),
                                                 &(t->w_uef_serial_frame),
                                                 &(t->w_uef_tmpchunk));
            t->w_uef_serial_phase = 0;

        }

        *reset_shift_register_out = 1;

        if (record_is_pressed) {
            e = tape_write_end_data(t);
        }
        t->w_must_end_data = 0;

    }

    return e;

}



/* tape.c */
static int wp_bitclk_accumulate_leader (tape_state_t *t,
                                        uint8_t tapenoise_active,
                                        int64_t ns_per_bit,
                                        uint8_t record_is_pressed,
                                        uint8_t *reset_shift_reg_out) {

    int64_t num_1200ths, i;
    /* if there is nothing to output in the shift register, then we have leader tone */
    /* end and write out any current data section */
    wp_end_data_section_if_ongoing (t, record_is_pressed, reset_shift_reg_out);
    if (record_is_pressed) { /* only accumulate leader if record is pressed */
        t->w_accumulate_leader_ns += ns_per_bit;
    }
    t->w_accumulate_bit_periods_ns += ns_per_bit;
    num_1200ths = t->w_accumulate_bit_periods_ns / TAPE_1200TH_IN_NS_INT;
    /* consume it; leave remainder */
    t->w_accumulate_bit_periods_ns -= (num_1200ths * TAPE_1200TH_IN_NS_INT);
    for (i=0; tapenoise_active && (i < num_1200ths); i++) {
        tapenoise_send_1200 ('L', &(t->tapenoise_no_emsgs));
    }
    
    return TAPE_E_OK;
    
}


/* tape.c */
static int wp_flush_accumulated_leader_maybe (tape_state_t *t, uint8_t record_is_pressed) {
    int e;
    /* handle the end of a leader section */
    if ( t->w_accumulate_leader_ns > 0 ) {
        if (record_is_pressed) {
/*printf("flushing %lf s of accumulated leader\n", t->w_accumulated_leader_s);*/
            e = tape_write_leader (t, t->w_accumulate_leader_ns / TAPE_1200TH_IN_NS_INT);
            if (TAPE_E_OK != e) { return e; }
        }
        t->w_accumulate_leader_ns = 0;
    }
    return TAPE_E_OK;
}

/* tape.c */
static int
wp_bitclk_start_data_if_needed (tape_state_t *ts,
                                /* need to provide baud rate + framing,
                                for TIBET hints, UEF &114 header, etc: */
                                serial_framing_t *f,
                                uint8_t always_117,
                                uint8_t record_is_pressed) {
                                
    int e;
    
    e = TAPE_E_OK;
                                
    /* Beware: record_is_pressed may be activated at any time,
       including in the middle of a block. Additionally, there
       is a further pathological case where record_is_pressed
       gets toggled multiple times during the course of a
       single block. We have to know when we're supposed to call
       tape_write_start_data() under such circumstances. */
    if ( ! ts->w_must_end_data ) {
        ts->w_must_end_data = 1;
        if (record_is_pressed) {
            e = tape_write_start_data (ts, always_117, f);
        }
    }
    
    return e;
    
}




#ifdef BUILD_TAPE_READ_POS_TO_EOF_ON_WRITE
static int read_ffwd_to_end (tape_state_t *t) {
    int e;
    e = TAPE_E_OK;
    if (TAPE_FILETYPE_BITS_UEF & t->filetype_bits) {
        e = uef_ffwd_to_end (&(t->uef));
    }
    if ((TAPE_E_OK == e) && (TAPE_FILETYPE_BITS_CSW & t->filetype_bits)) {
        e = csw_ffwd_to_end (&(t->csw));
    }
    if ((TAPE_E_OK == e) && (TAPE_FILETYPE_BITS_TIBET & t->filetype_bits)) {
        e = tibet_ffwd_to_end (&(t->tibet));
    }
    return e;
}
#endif


/* calls gated by ( ( ! silent ) && ( ! have_leader ) ) */
static int wp_bitclk_output_data (tape_state_t *ts,
                                  ACIA *acia,
                                  serial_framing_t *f,
                                  uint8_t tapenoise_active,
                                  int64_t ns_per_bit,
                                  uint8_t record_is_pressed,
                                  uint8_t always_117) {

    int e;
    int64_t i, num_1200ths;
    
    e = TAPE_E_OK;

    /*
       This is another hackish consequence of the fact that emulated
       tape hardware is only really half-duplex. Scenario:
       
       - Record & append to tape is DISABLED (read mode)
       - Tape noise generation is ENABLED
       - a SAVE operation begins
       
       In this situation, rather than the SAVE operation producing
       corresponding tape noise as might be expected, the tape noise
       generator will instead PLAY BACK the loaded tape while
       the SAVE occurs (remember, no actual writing to tape takes
       place because record&append is OFF). In order to get tape
       noise from the SAVE operation, record&append needs to be
       enabled.
       
       In order to combat this counter-intuitive behaviour,
       we can define BUILD_TAPE_READ_POS_TO_EOF_ON_WRITE in order
       to ensure that the read pointers for any loaded tape formats
       are advanced to EOF when a data write occurs ("tape finished"),
       ensuring that no audio is played back. This is done regardless
       of whether record&append is enabled or not. */

    /* TOHv3.3: added extra call to read_ffwd_to_end() to stop receive overflows */
#ifdef BUILD_TAPE_READ_POS_TO_EOF_ON_WRITE
    e = read_ffwd_to_end(ts);
    if (TAPE_E_OK != e) { return e; }
#endif

    /* setup: handle TIBET hints, UEF baud chunk &117,
       UEF chunk &114 header information: */
    e = wp_bitclk_start_data_if_needed (ts, f, always_117, record_is_pressed);
    if (TAPE_E_OK != e) { return e; }
    
    ts->w_accumulate_bit_periods_ns += ns_per_bit;
    num_1200ths = ts->w_accumulate_bit_periods_ns / TAPE_1200TH_IN_NS_INT;
    /* consume any whole 1200ths */
    ts->w_accumulate_bit_periods_ns -= (num_1200ths * TAPE_1200TH_IN_NS_INT);
    
    /* FIXME: I don't think this 0-case should ever happen?
       wp_bitclk_accumulate_leader() should have
       been called instead of this one? */
    if (0 == acia->tx_shift_reg_loaded) {

        log_warn("tape: BUG: wp_bitclk_output_data called with shift reg not loaded");
        return TAPE_E_BUG;
        
    } else if (    (acia->tx_shift_reg_shift > 0)
                && (acia->tx_shift_reg_shift < (f->num_data_bits + 1))) { /* nom. 9 */
    
        /* Start bit not sent yet? Send it now. Start bit is deferred,
           in order to simulate the correct TX pipelining behaviour.
           (This won't jeopardise tapenoise; double bit will just be
           sequenced in the tapenoise ringbuffer.) */
        if (1 == acia->tx_shift_reg_shift) {
            for (i=0;
                 record_is_pressed && (TAPE_E_OK == e) && (i < num_1200ths);
                 i++) {
                e = tape_write_1200th (ts, f, tapenoise_active, '0');
            }
        }
    
        /* data bits */
        for (i=0;
             record_is_pressed && (TAPE_E_OK == e) && (i < num_1200ths);
             i++) {
            e = tape_write_1200th (ts,
                                   f,
                                   tapenoise_active,
                                   (acia->tx_shift_reg_value & 1) ? '1' : '0');
        }
        
    } else if (    (acia->tx_shift_reg_shift > 0)
                && (acia->tx_shift_reg_shift >= (f->num_data_bits + 1))) {

        /* stop bit(s) */
        for (i=0;
             record_is_pressed && (TAPE_E_OK == e) && (i < num_1200ths);
             i++) {
            e = tape_write_1200th (ts, f, tapenoise_active, '1');
        }

    }
    /* else {
        log_warn("tape: BUG: tx_shift_reg_shift insane value %u", acia->tx_shift_reg_shift);
        return TAPE_E_BUG;
    }*/
    
    return e;
    
}

#include "acia.h"


int tape_flush_pending_piece (tape_state_t *ts, ACIA *acia_or_null, uint8_t silence112) {

    uint8_t reset_shift_reg;
    int e;
    
    e = TAPE_E_OK;
    
/*printf("tape_flush_pending_piece: w_accumulate_silence_ns = %lld\n", ts->w_accumulate_silence_ns);*/
    
    if (ts->w_accumulate_leader_ns > 0) {
        e = tape_write_leader (ts, ts->w_accumulate_leader_ns / TAPE_1200TH_IN_NS_INT);
        ts->w_accumulate_leader_ns = 0;
    } else if (ts->w_accumulate_silence_ns > TAPE_1200TH_IN_NS_INT) {
        e = tape_write_silence_2 (ts,
                                  ((double) ts->w_accumulate_silence_ns) / 1000000000.0,
                                  silence112);
        ts->w_accumulate_silence_ns = 0;
    } else {
        reset_shift_reg = 0;
        wp_end_data_section_if_ongoing (ts, 1, &reset_shift_reg);
        if (acia_or_null != NULL) {
            acia_hack_tx_reset_shift_register(acia_or_null);
        } else {
         /*   log_warn("tape: WARNING: ignoring reset_shift_reg=1 from wp_end_data_section_if_ongoing");*/
        }
    }
    
    /* TOHv3.2: fixed memory leak: never zero this;
       it wipes the allocation on w_uef_tmpchunk */
    /*memset(&(t->w_uef_tmpchunk), 0, sizeof(uef_chunk_t));*/

    /* do this instead */
    ts->w_uef_tmpchunk.num_data_bytes_written = 0;
    ts->w_uef_tmpchunk.type                   = 0xffff;
    ts->w_uef_tmpchunk.len                    = 0;
    
    return e;
    
}



static int tape_write_prelude (tape_state_t *ts,
                               uint8_t record_is_pressed,
                               uint8_t no_origin_chunk_on_append) {
    
    int e;
    
    e = TAPE_E_OK;
    
#ifdef BUILD_TAPE_READ_POS_TO_EOF_ON_WRITE
    if (record_is_pressed) {
        e = read_ffwd_to_end(ts);
        if (TAPE_E_OK != e) { return e; }
    }
#endif

    /* make sure that the first thing that gets written
       to a UEF stream is an origin chunk. Note that we are
       writing this directly to the UEF on the tape_state_t,
       whereas data will be written to the staging chunk,
       t->w_uef_tmpchunk (for now). */
       
    if (      ( ts->filetype_bits & TAPE_FILETYPE_BITS_UEF )
         &&     record_is_pressed
         && !   ts->w_uef_origin_written ) {
         
        if ( ! no_origin_chunk_on_append ) {
            e = uef_store_chunk(&(ts->uef),
                                (uint8_t *) VERSION_STR,
                                0, /* origin, chunk &0000 */
                                strlen(VERSION_STR)+1); /* include the \0 */
            if (TAPE_E_OK != e) { return e; }
        }
        
        ts->w_uef_origin_written = 1; /* don't try again, mark it written even if disabled */
        
    }
    
    e = wp1_4k8_init_blank_tape_if_none_loaded (&(ts->filetype_bits), record_is_pressed);
    if (TAPE_E_OK != e) { return e; }

    return e;

}


/* TOHv4: now, we want to be called at the bit rate.
   For each tick, we either have leader, silence, or data.
   Leader and silence packets will have to be expanded out into
   the appropriate number of 1200ths. */
int tape_write_bitclk (tape_state_t *ts,
                       ACIA *acia,
                       char bit,
                       int64_t ns_per_bit, /* 1024 = 1200 baud; 4096 = 300; 8192 = 150 etc. */
                       uint8_t silent,
                       uint8_t tapenoise_write_enabled,
                       uint8_t record_is_pressed,
                       uint8_t always_117,
                       uint8_t silence112,
                       uint8_t no_origin_chunk_on_append) {

    uint8_t have_leader;
    int e;
    serial_framing_t f;
    uint8_t reset_shift_reg;
    uint8_t had_data = 0;

    if (0 == ns_per_bit) {
        log_warn("tape: BUG: ns_per_bit is zero!");
        return TAPE_E_BUG;
    }

    had_data = tape_peek_for_data(ts);
    
    /* do some housekeeping: */
    e = tape_write_prelude (ts, record_is_pressed, no_origin_chunk_on_append);
    if (TAPE_E_OK != e) { return e; }
    
    /*
       handle silence:
       - if silent:
         - flush data section, if this silence interrupts one;
         - generate silent tapenoise (potentially multiple 1200ths)
       - or, if not silent:
         - write silent section if one has just ended
    */
    reset_shift_reg = 0;
    e = wp_bitclk_handle_silence (ts,
                                  silent,
                                  tapenoise_write_enabled,
                                  record_is_pressed,
                                  ns_per_bit,
                                  silence112,
                                  &reset_shift_reg);
    if (TAPE_E_OK != e)  { return e; }
    
    /* Hack?? reset tx shiftreg if a partial frame needs flushing to the UEF.
       Shouldn't be necessary?? */
    if (reset_shift_reg) { acia_hack_tx_reset_shift_register(acia); }
    
    /* handle leader:
       - write data section if leader interrupts one;
       - or, write leader section if one has just ended;
       - if leader and tapenoise active, then send a leader tapenoise 1200th */
    have_leader = ! acia->tx_shift_reg_loaded;
    
    if ( have_leader && ! silent ) {
        /* also sends potentially multiple 1200ths of tapenoise */
        e = wp_bitclk_accumulate_leader (ts,
                                         record_is_pressed && tapenoise_write_enabled,
                                         ns_per_bit,
                                         record_is_pressed,
                                         &reset_shift_reg);
        if (TAPE_E_OK != e)  { return e; }
    }
    
    /* Hack?? reset tx shiftreg if a partial frame needs flushing to the UEF.
       Shouldn't be necessary?? */
    if (reset_shift_reg) { acia_hack_tx_reset_shift_register(acia); }
    
    if ( silent || ! have_leader ) {
        /* end of leader section? */
        e = wp_flush_accumulated_leader_maybe (ts, record_is_pressed);
        if (TAPE_E_OK != e) { return e; }
    }
 
    /* Silence and leader 1200ths & tapenoise are done;
       data still remains: */
    if ( ( ! silent ) && ( ! have_leader ) ) {
    
/*
#ifdef BUILD_TAPE_CHECK_POLL_TIMING
*/
/* timing check thing */
/*
struct timeval tv;
uint64_t us, elapsed;

gettimeofday(&tv, NULL);
us = (tv.tv_sec * 1000000) + tv.tv_usec;
#define POLL_TIMING_NUM_CALLS 1000
if (POLL_TIMING_NUM_CALLS == poll_timing_num_calls) {
    elapsed = us - poll_timing_us_prev;*/
    /* overhaul v2: changed, moving average now */
    /*printf("tape poll timing: %llu microseconds\n", elapsed / POLL_TIMING_NUM_CALLS);
    poll_timing_num_calls = 0;
    poll_timing_us_prev = us;
} else {
    poll_timing_num_calls++;
}
#endif */

        acia_get_framing (acia->control_reg, &f);
        f.nominal_baud = (int32_t) (0x7fffffff & ((TAPE_1200TH_IN_NS_INT * 1200) / ns_per_bit));

        e = wp_bitclk_output_data (ts,
                                   acia,
                                   &f,
                                   record_is_pressed && tapenoise_write_enabled,
                                   ns_per_bit,
                                   record_is_pressed,
                                   always_117);
        if (TAPE_E_OK != e) { return e; }
        
    }
    
    /* need to maybe un-grey Catalogue Tape in the Tape GUI menu? */
    if (tape_peek_for_data(ts) != had_data) { /* suddenly we have data, as it's just been written */
        gui_alter_tape_menus_2();
    }
    
    return e;
    
}

/* TOHv3.2 */
static int tape_uef_flush_incomplete_frame (uef_state_t *uef_inout,
                                            uint8_t *serial_phase_inout,
                                            uint8_t *serial_frame_inout,
                                            uef_chunk_t *chunk_inout) {

    uint8_t i;
    int e;

    /*
    printf ("tape_uef_flush_incomplete_frame (phase=%u, frame=0x%02x)\n",
            *serial_phase_inout, *serial_frame_inout);
    */

    if ((*serial_phase_inout >= 2) && (*serial_phase_inout <= 8)) {
        for (i=0; i < (9 - *serial_phase_inout) ; i++) {
            *serial_frame_inout >>= 1;
        }
    }
    e = uef_append_byte_to_chunk (chunk_inout, *serial_frame_inout);
    if (TAPE_E_OK == e) {
        e = uef_store_chunk(uef_inout,
                            chunk_inout->data,
                            chunk_inout->type,
                            chunk_inout->len);
    }

    /* we don't clean up the uef tmpchunk allocation any more;
        just set the length to zero, and leave alloc and the data
        buffer as they are, for future use */
    chunk_inout->len = 0;
    chunk_inout->num_data_bytes_written = 0;
    *serial_phase_inout = 0;
    *serial_frame_inout = 0;

    return e;

}


/* should be called by the reset callback */
int tape_handle_acia_master_reset (tape_state_t *t) {

    int e;
    
    e = TAPE_E_OK;
    
    if (NULL == t) {
      log_warn("BUG: tape_handle_acia_master_reset(NULL)\n");
      return TAPE_E_BUG;
    }

/*log_warn("tape_handle_acia_master_reset: phase is %u\n", t->w_uef_serial_phase);*/
    
    /* if reset occurs during a frame */
    if ((t->filetype_bits & TAPE_FILETYPE_BITS_UEF) && (t->w_uef_serial_phase != 0)) {
        /* if UEF, finish the frame */
        /* phase=1 : start bit sent
         * phase=2 : bit 0 sent
         * ...
         * phase=9 : bit 7 sent
         */
        /* TOHv3.2: actually push partial frames out to the UEF properly */
        e = tape_uef_flush_incomplete_frame (&(t->uef),
                                             &(t->w_uef_serial_phase),
                                             &(t->w_uef_serial_frame),
                                             &(t->w_uef_tmpchunk));
    }
    
    return e;
       
}


static int tape_write_1200th (tape_state_t *ts,
                              serial_framing_t *f,
                              uint8_t tapenoise_active,
                              char value) {

    int e;
    uint8_t v;
    
    e = TAPE_E_OK;

    /* 1. TAPE NOISE */
    if (tapenoise_active) {
        tapenoise_send_1200(value, &(ts->tapenoise_no_emsgs));
    }
    
    v = (value=='0') ? 0 : 1;
    
    do { /* try { */
    
        double pulse;
        uint8_t i;
        uint8_t have_uef_bit, bit_value;
        uint8_t total_num_bits;
        uint16_t mask;
        uint8_t len;
        const char *payload117;
    
        /* 2. TIBET */
        if (ts->filetype_bits & TAPE_FILETYPE_BITS_TIBET) { /* TIBET enabled? */
            for (i=0; (TAPE_E_OK == e) && (i < 2); i++) { /* write two tonechars */
                e = tibet_append_tonechar (&(ts->tibet), v?'.':'-', &(ts->tibet_data_pending));
            }
            if (TAPE_E_OK != e) { break; }
        }
        
        /* 3. UEF */
        if ( ts->filetype_bits & TAPE_FILETYPE_BITS_UEF ) {

            have_uef_bit = 0;
            
            /* Have to decode incoming stream of 1200ths to make
               chunks &100 and &104. Need to respect wacky baud rates
               in order to do that. */
               
            mask = 0;
            if (1200 == f->nominal_baud) {
                mask = 1;
                len = 1;
            } else if (600 == f->nominal_baud) {
                mask = 3;
                len = 2;
            } else if (300 == f->nominal_baud) {
                mask = 0xf;
                len = 4;
            } else if (150 == f->nominal_baud) {
                mask = 0xff;
                len = 8;
            } else if (75 == f->nominal_baud) {
                mask = 0xffff;
                len = 16;
            } else {
                log_warn("tape: BUG: illegal tx baud rate %d", f->nominal_baud);
                e = TAPE_E_BUG;
                break;
            }
            
            (ts->w_uef_enc100_shift_value) <<= 1;
            (ts->w_uef_enc100_shift_value)  |= v;
            (ts->w_uef_enc100_shift_value)  &= mask;
            (ts->w_uef_enc100_shift_amount)++;
            
            /* check that the frame we have so far is uniform */
            for (i=0; i < ts->w_uef_enc100_shift_amount; i++) {
                if (    (((ts->w_uef_enc100_shift_value) >> i) & 1)
                     != (ts->w_uef_enc100_shift_value & 1)          ) {
                    log_warn("tape: BUG? UEF chunk &100/&104 bit-assembly shift register desync!");
                    /* resynchronise; keep the new wayward bit and discard anything older */
                    (ts->w_uef_enc100_shift_amount)   = 1;
                    (ts->w_uef_enc100_shift_value)  >>= i;
                    (ts->w_uef_enc100_shift_value)   &= 1;
                    break;
                }
            }
            
            if (ts->w_uef_enc100_shift_amount == len) {
                /* bit ready */
                have_uef_bit = 1;
                bit_value    = ts->w_uef_enc100_shift_value & 1;
                (ts->w_uef_enc100_shift_amount) = 0;
            }

            if (have_uef_bit) {
            
                total_num_bits = 1 + f->num_data_bits
                                   + ((f->parity == 'N') ? 0 : 1)
                                   + f->num_stop_bits;
                                   
                if (0 == ts->w_uef_serial_phase) {
                    /* expect start bit */
                    if (bit_value == 0) {
                        ts->w_uef_serial_frame = 0;
                        (ts->w_uef_serial_phase)++;
                    }
                    /* don't advance phase if start bit not found */
                } else if (ts->w_uef_serial_phase <= f->num_data_bits) {
                    (ts->w_uef_serial_frame) >>= 1;
                    ts->w_uef_serial_frame |= (bit_value ? 0x80 : 0);
                    (ts->w_uef_serial_phase)++;
                } else if (ts->w_uef_serial_phase == (total_num_bits - 1)) {
                    /* (second) stop bit; frame is ready */
                    ts->w_uef_serial_phase = 0; /* expect next start bit */
                    
                    if (ts->w_uef_prevailing_baud != f->nominal_baud) {

                        ts->w_uef_prevailing_baud = f->nominal_baud; /* acknowledge the change */
                        /*printf("new baud %u\n", f->baud300 ? 300 : 1200);*/
                        
                        if (0 == ts->w_uef_tmpchunk.num_data_bytes_written) {
                            /* OK. Nothing written yet in this chunk, so we can insert a chunk 117
                               with no ill effects. */
                            /*printf("uef: new baud %u; OK to insert &117, no data written yet\n", f->baud300 ? 300 : 1200);*/
                            payload117 = NULL;
                            e = uef_get_117_payload_for_nominal_baud (f->nominal_baud, &payload117);
                            if (TAPE_E_OK != e) { break; }
                            e = uef_store_chunk(&(ts->uef),
                                                (uint8_t *) payload117,
                                                0x117,
                                                2);
                            if (TAPE_E_OK != e) { break; }
                        } else {
                            /* TODO: a baud change occurred during a data chunk.
                               Not yet supported for UEF.
                               This needs the current data chunk to be flushed, a baud chunk &117 to be inserted,
                               and then another data chunk begun that contains the remaining data from
                               this point onwards.
                               ( Workaround: Use TIBET, insert "end","/baud 300","data" manually, then tibetuef.php. ) */
                            log_warn("uef: BUG: baud change (%u) mid-chunk is not implemented!", f->nominal_baud);
                        }
                    }
                    
                    /* write byte into temporary chunk */
                    e = uef_append_byte_to_chunk (&(ts->w_uef_tmpchunk), ts->w_uef_serial_frame);
                    if (TAPE_E_OK != e) { break; }
                    (ts->w_uef_tmpchunk.num_data_bytes_written)++;
                } else { /* parity or first of two stop bits */
                    (ts->w_uef_serial_phase)++;
                }
                
            } /* endif (have_uef_bit) */
            
        } /* endif UEF */

        /* 4. CSW */
        if ( ts->filetype_bits & TAPE_FILETYPE_BITS_CSW ) {
        
            e = csw_init_blank_if_necessary(&(ts->csw));
            if (TAPE_E_OK != e) { return e; }
            
            pulse = ts->csw.len_1200th_smps_perfect / 2.0;
            if (v) { pulse /= 2.0; }
            
            for (i=0; (TAPE_E_OK == e) && (i < (v?4:2)); i++) {
                e = csw_append_pulse_fractional_length (&(ts->csw), pulse);
            }
            
            if (TAPE_E_OK != e) { break; }
            
        }
        
    } while (0);
    
    if (TAPE_E_OK != e) {
        ts->w_uef_tmpchunk.len = 0;
    }
    
    return e;
    
}


static int check_pending_tibet_span_type (tape_state_t *t, uint8_t type) {
    if (t->tibet_data_pending.type != type) {
        log_warn("tape: write: warning: TIBET: pending span has type %u, should be %u",
                 t->tibet_data_pending.type, type);
        return TAPE_E_BUG;
    }
    return TAPE_E_OK;
}



static int tape_write_end_data (tape_state_t *t) {
    
    int e;
    
    e = TAPE_E_OK;
    
    if (t->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {
        /* ensure we have a data span on the go */
        e = check_pending_tibet_span_type(t, TIBET_SPAN_DATA); /* an assert */
        /* TOHv4-rc2: Can get a non-data pending span type here if you
            * hit Record and Append in the middle of a block write
            * with an initially-blank tape. In this case such a data span is
            * empty anyway so we just skip it. */
        if (TAPE_E_OK == e) {
            e = tibet_append_data_span(&(t->tibet), &(t->tibet_data_pending));
        } else {
            /* TOHv4-rc2 */
            log_warn("tape: write: warning: TIBET: discarding partial data span");
        }
        if (TAPE_E_OK != e) { return e; }
        memset(&(t->tibet_data_pending), 0, sizeof(tibet_span_t));
    }

    
    if ((TAPE_E_OK == e) && (t->filetype_bits & TAPE_FILETYPE_BITS_UEF)) {
        /* UEF: append tmpchunk to actual UEF struct */
        e = uef_store_chunk(&(t->uef),
                            t->w_uef_tmpchunk.data,
                            t->w_uef_tmpchunk.type,
                            t->w_uef_tmpchunk.len);
    }
    
    /* we don't clean up the uef tmpchunk allocation any more;
       just set the length to zero, and leave alloc and the data
       buffer as they are, for future use */
    t->w_uef_tmpchunk.len = 0;
    t->w_uef_tmpchunk.num_data_bytes_written = 0;
    
    if (TAPE_E_OK != e) { return e; }
    
    return e;
    
}


static int tape_write_start_data (tape_state_t *t, uint8_t always_117, serial_framing_t *f) {

    /* We don't insert hints for phase or speed any more.
     * Those are qualities of a real-world tape, not
     * one produced by an emulator. */
                                  
    int e;
    uint8_t i;
    uint8_t uef_chunk_104_framing[3];
    tibet_span_hints_t *h;
    const char *payload117;
    
    e = TAPE_E_OK;
    
    if (t->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {
    
        e = check_pending_tibet_span_type(t, TIBET_SPAN_INVALID); /* an assert */
        if (e != TAPE_E_OK) { return e; }
        
        memset(&(t->tibet_data_pending), 0, sizeof(tibet_span_t));
        t->tibet_data_pending.type = TIBET_SPAN_DATA;
        
        h = &(t->tibet_data_pending.hints);
    
        h->have_baud    = 1;
        h->baud         = f->nominal_baud;
        h->have_framing = 1;
        framing_to_string(h->framing, f);
        if (e != TAPE_E_OK) { return e; }
    }
    
    /* UEF */
    if (t->filetype_bits & TAPE_FILETYPE_BITS_UEF) {

        if (always_117) {
        
            payload117 = NULL;
        
            /* Well, UEF 0.10 states that the only values which are valid in
               baud chunk &117 are 300 and 1200. Furthermore, it's only
               a 16-bit field so theoretical baud rates > 38400 won't fit.
               However, we can save perfectly valid tapes at e.g. 75 baud
               (even though they can't be loaded back in). */
            
            e = uef_get_117_payload_for_nominal_baud (f->nominal_baud, &payload117);
            if (TAPE_E_OK != e) { return e; }
        
            /* If baud is valid for it, append baud chunk &117 */
            if (NULL != payload117) {
                e = uef_store_chunk(&(t->uef),
                                    (uint8_t *) payload117,
                                    0x117,
                                    2);
                if (e != TAPE_E_OK) { return e; }
            }
            
            /*
             * We set the last used baud rate here, so the rate change detection
             * at byte level won't be triggered.
             */
            
            t->w_uef_prevailing_baud = f->nominal_baud;
            
        }
    
        /* clean up any prior tmpchunk */
        t->w_uef_tmpchunk.len = 0;
        
        /* work out which chunk type to use, based on framing */
        if (    (  8 == f->num_data_bits)
             && (  1 == f->num_stop_bits)
             && ('N' == f->parity) ) {
            /* 8N1 => use chunk &100 */
            t->w_uef_tmpchunk.type = 0x100;
        } else {
            /* some other framing => use chunk &104 */
            t->w_uef_tmpchunk.type = 0x104;
        }
        
        /* if chunk &104, then build the chunk sub-header w/framing information */
        if ( 0x104 == t->w_uef_tmpchunk.type ) {
        
            uef_chunk_104_framing[0] = f->num_data_bits;
            uef_chunk_104_framing[1] = f->parity;
            uef_chunk_104_framing[2] = f->num_stop_bits;
            
            for (i=0; (TAPE_E_OK == e) && (i < 3); i++) {
                e = uef_append_byte_to_chunk(&(t->w_uef_tmpchunk),
                                             uef_chunk_104_framing[i]);
            }
            
        }
        
        /* on error, clean up tmpchunk */
        if (e != TAPE_E_OK) {
            t->w_uef_tmpchunk.len = 0;
            return e;
        }
        
    }
    
    return e;
    
}



/* TODO: no support yet for (leader+&AA+leader), chunk &111 */
static int tape_write_leader (tape_state_t *t, uint32_t num_1200ths) {

    int e;
    uint8_t num_2400ths[2];
    tibet_span_hints_t hints;
    
    e = TAPE_E_OK;
    memset(&hints, 0, sizeof(tibet_span_hints_t));
    
    if (t->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {
        /* sanity: assert that no data is pending */
        e = check_pending_tibet_span_type (t, TIBET_SPAN_INVALID);
        if (e != TAPE_E_OK) { return e; }
    }
    
    /* this compromise may be necessary, because num_1200ths is computed
       as (num_4800ths / 4): */
    if (0 == num_1200ths) {
        num_1200ths = 1;
    }
    
    if (t->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {
        e = tibet_append_leader_span (&(t->tibet), 2 * num_1200ths, &hints);
        if (TAPE_E_OK != e) { return e; }
    }
    
    /* chunk &110 */
    if (t->filetype_bits & TAPE_FILETYPE_BITS_UEF) {
        tape_write_u16(num_2400ths, 2 * num_1200ths);
        e = uef_store_chunk(&(t->uef), num_2400ths, 0x110, 2);
        if (TAPE_E_OK != e) { return e; }
    }
    
    if (t->filetype_bits & TAPE_FILETYPE_BITS_CSW) {
        e = csw_append_leader(&(t->csw), num_1200ths);
    }
    
    return e;
    
}


/* TOHv3.2 (rc1 killer!): respect -tape112 switch (or GUI equivalent) */
static int tape_write_silence_2 (tape_state_t *t, double len_s, uint8_t silence112) {

    int e;
    tibet_span_hints_t hints;
    int32_t num_112_cycs, rem;
    uint8_t buf[2];

    e = TAPE_E_OK;
    memset(&hints, 0, sizeof(tibet_span_hints_t));

    if (len_s < TAPE_1200TH_IN_S_FLT) { /*TAPE_832_US) {*/
        log_warn("tape: WARNING: zero-length silence on write, adjusting to one cycle :/");
        /* hack, to prevent silence w/duration of zero 1200ths */
        len_s = TAPE_1200TH_IN_S_FLT; /*TAPE_832_US;*/
    }
    
    /* FIXME: this is a little bit nasty, because the TIBET structs
       only allow expressing silence in 2400ths, whereas the
       text file itself allows silence with floating-point resolution */
    if (t->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {
        e = tibet_append_silent_span (&(t->tibet), len_s, &hints);
        if (TAPE_E_OK != e) { return e; }
    }
    
    /* silence in UEFs ... */
    if (t->filetype_bits & TAPE_FILETYPE_BITS_UEF) {
        if (silence112) {
            /* one or more chunk &112s */
            num_112_cycs = (int32_t) (0.5f + (len_s * TAPE_1200_HZ * 2.0)); // correct frequency value
            if (0 == num_112_cycs) {
              log_warn ("tape: WARNING: silence as &112: gap is tiny (%f s); round up to 1/2400", len_s);
              num_112_cycs = 1;
            }
            /* multiple chunks needed */
            for ( rem = num_112_cycs; rem > 0; rem -= 65535 ) {
              tape_write_u16(buf, (rem > 65535) ? 65535 : rem);
              e = uef_store_chunk(&(t->uef), buf, 0x112, 2);
              if (TAPE_E_OK != e) { return e; }
            }
        } else {
            /* chunk &116 */
            /* FIXME: endianness? */
            union { float f; uint8_t b[4]; } u;
            u.f = len_s;
            e = uef_store_chunk(&(t->uef), u.b, 0x116, 4);
            if (TAPE_E_OK != e) { return e; }
        }
    }
    
    if (t->filetype_bits & TAPE_FILETYPE_BITS_CSW) {
        e = csw_append_silence(&(t->csw), len_s);
    }
    
    return e;
    
}



int tape_generate_and_save_output_file (uint8_t filetype_bits,
                                        uint8_t silence112,
                                        char *path,
                                        uint8_t compress,
                                        ACIA *acia_or_null) {

    char *tape_op_buf, *bufz;
    FILE *f;
    int e;
    size_t z, tape_op_buf_len, bufz_len;
    uint8_t tibet, uef, csw;
    
    e = TAPE_E_OK;
    tape_op_buf = NULL;
    f = NULL;
    tape_op_buf_len = 0;
    bufz = NULL;
    bufz_len = 0;

    /* make sure any pending pieces are appended before saving */
    e = tape_flush_pending_piece (&tape_state, acia_or_null, silence112);
    if (TAPE_E_OK != e) { return e; }
    
    tibet = (TAPE_FILETYPE_BITS_TIBET == filetype_bits);
    uef   = (TAPE_FILETYPE_BITS_UEF   == filetype_bits);
    csw   = (TAPE_FILETYPE_BITS_CSW   == filetype_bits);
    
    do {

        if (tibet) {
            e = tibet_build_output (&(tape_state.tibet),
                                    &tape_op_buf,
                                    &tape_op_buf_len);
        } else if (uef) {
            e = uef_build_output (&(tape_state.uef),
                                  &tape_op_buf,
                                  &tape_op_buf_len);
        } else if (csw) {
            e = csw_build_output (&(tape_state.csw),
                                  compress,
                                  &tape_op_buf,
                                  &tape_op_buf_len);
        } else {
            log_warn("tape: write: BUG: state failure");
            e = TAPE_E_BUG;
        }
        
        if (TAPE_E_OK != e) { break; }
        
        if (NULL == tape_op_buf) {
            log_warn("tape: write: BUG: generated tape output is NULL!");
            e = TAPE_E_BUG;
        } else if (0 == tape_op_buf_len) {
            log_warn("tape: write: BUG: generated tape output has zero length!");
            e = TAPE_E_BUG;
        }
        
        if (TAPE_E_OK != e) { break; }
        
        if (compress && (tibet || uef)) {
            /* TIBET and UEF are just gzipped */
            e = tape_zlib_compress (tape_op_buf,
                                    tape_op_buf_len,
                                    1, /* use gzip encoding, boy howdy */
                                    &bufz,
                                    &bufz_len);
            if (TAPE_E_OK != e) { break; }
            /* transparently replace original buffer */
            free(tape_op_buf);
            tape_op_buf     = bufz;
            tape_op_buf_len = bufz_len;
        }
        
        f = fopen(path, "wb");
        
        if (NULL == f) {
            log_warn("tape: could not open file for saving: %s", path);
            e = TAPE_E_SAVE_FOPEN;
            break;
        }
        
        if ( (NULL != tape_op_buf) && (TAPE_E_OK == e) ) {
            z = fwrite(tape_op_buf, tape_op_buf_len, 1, f);
            if (z != 1) {
                log_warn("tape: fwrite failed saving to file: %s", path);
                e = TAPE_E_SAVE_FWRITE;
            } else {
                log_info("tape: saved: %s", path);
            }
        }
        
    } while (0);
    
    if (tape_op_buf != NULL) {
        free(tape_op_buf);
        tape_op_buf = NULL;
    }
    
    if (NULL != f) {
        fclose(f);
    }
    
    if (TAPE_E_OK != e) {
        log_warn("tape: failed to save output file (code %u)\n", e);
    }
    
    return e;
    
}


static int
tape_tone_1200th_from_back_end (uint8_t strip_silence_and_leader, /* speedup */
                                tape_state_t *ts,
                                uint8_t awaiting_start_bit,
                                uint8_t enable_phantom_block_protection, // TOHv3.3
                                char *tone_1200th_out) { /* must know about leader */

    int e;
    uint8_t inhibit_start_bit_for_phantom_block_protection; /* TOHv3.2 */
    
    *tone_1200th_out = '?';
    inhibit_start_bit_for_phantom_block_protection = 0;
    
/*    if (tape_peek_eof (ts)) {
        *tone_1200th_out = 'S';
        return TAPE_E_EOF;
    } */
    
    /* read 1/1200 seconds of tone from the selected tape back-end */
    
    do { /* TOHv3.2: only loops back if (strip_silence_and_leader) */
    
        e = tape_read_1200th (ts, tone_1200th_out);
        if (TAPE_E_EOF == e) {
            *tone_1200th_out = 'S';
            return TAPE_E_EOF;
        }
        if (e != TAPE_E_OK) { return e; }

        /* handle some conditions concerning leader: */
        if ( awaiting_start_bit ) {
            /* perform leader detection if not already done (for CSW, specifically),
             * to assist the tape noise generator: */
            if (    (ts->start_bit_wait_count_1200ths > 60) /* 15 bits of 300-baud ones */
                 && ('1' == *tone_1200th_out)) {
                *tone_1200th_out = 'L';
            }
            /* If enabled, prevent "squawks" being mis-detected as
             * the start of genuine MOS blocks.
             *
             * Real hardware tends to reject these phantom blocks,
             * because smashing the analogue circuitry with a signal
             * immediately following a long silence doesn't do wonders
             * for its ability to detect a start bit correctly. It *is*
             * possible to see these fake blocks on a Model B using a
             * high quality digital tone source, and judicious volume
             * settings.
             * 
             * This check will make sure a certain duration of
             * continuous tone has prevailed before a start bit may be
             * recognised by the tape system.
             * 
             * We don't do this if using the strip_silence_and_leader
             * speed hack.
             *
             */
#define SQUAWKPROT_1200THS_SINCE_SILENT 100
            if ( ! strip_silence_and_leader ) {
                if ('S' == *tone_1200th_out) {
                    ts->num_1200ths_since_silence = 0;
                    /* avoid possible overflow when incrementing this: */
                } else if (ts->num_1200ths_since_silence < SQUAWKPROT_1200THS_SINCE_SILENT) {
                    (ts->num_1200ths_since_silence)++;
                    /* enforce >=~100 of 1200ths of leader tone after any silence */
                    inhibit_start_bit_for_phantom_block_protection = 1;
                }
            }
            /* otherwise, start bit is always eligible */
            (ts->start_bit_wait_count_1200ths)++;
        } else {
            ts->start_bit_wait_count_1200ths = 0;
        }
        
        if (('0' == *tone_1200th_out) || ('S' == *tone_1200th_out)) {
          ts->start_bit_wait_count_1200ths = 0; /* nix leader identification on '0' or 'S' */
        }
        
    } while (    (('L' == *tone_1200th_out) || ('S' == *tone_1200th_out))
              && strip_silence_and_leader
              && (TAPE_E_OK == e)); /* TOHv3.2; loop to strip silence and leader */
    
    if (e != TAPE_E_OK) { return e; }
    
    /* TOHv3.3: new run-time enable_phantom_block_protection variable */
    if (inhibit_start_bit_for_phantom_block_protection && enable_phantom_block_protection) {
        *tone_1200th_out = 'L';
    }
    
    return TAPE_E_OK;

}


static void framing_to_string (char fs[4], serial_framing_t *f) {
    fs[0] = (f->num_data_bits == 7) ? '7' : '8';
    fs[1] = f->parity;
    fs[2] = (f->num_stop_bits == 1) ? '1' : '2';
    fs[3] = '\0';
}



/* for tape catalogue */
   
/* WARNING: on error, caller is expected to clean up "out" */
int tape_state_clone_and_rewind (tape_state_t *out, tape_state_t *in) {

    int e;
    
    e = TAPE_E_OK;
    
    memcpy(out, in, sizeof(tape_state_t));
    
    /* wipe all of the structures on the target, and any pointers
       they might have duplicated */
    memset(&(out->w_uef_tmpchunk), 0, sizeof(uef_chunk_t));
    memset(&(out->tibet),          0, sizeof(tibet_t)    );
    memset(&(out->uef),            0, sizeof(uef_state_t));
    memset(&(out->csw),            0, sizeof(csw_state_t));
    
    /* TOHv3: modified for simultaneous file types */
    
    if ((TAPE_FILETYPE_BITS_TIBET & in->filetype_bits) && (in->tibet.priv != NULL)) {
        e = tibet_clone (&(out->tibet), &(in->tibet));
        if (TAPE_E_OK == e) {
            e = tibet_rewind(&(out->tibet));
        }
    }
    if ((TAPE_E_OK == e) && (TAPE_FILETYPE_BITS_CSW & in->filetype_bits)) {
        e = csw_clone (&(out->csw), &(in->csw));
        if (TAPE_E_OK == e) {
            csw_rewind (&(out->csw));
        }
    }
    if ((TAPE_E_OK == e) && (TAPE_FILETYPE_BITS_UEF & in->filetype_bits)) {
        e = uef_clone (&(out->uef), &(in->uef));
        if (TAPE_E_OK == e) {
            uef_rewind (&(out->uef));
        }
    }
    
    if (TAPE_E_OK != e) {
        log_warn("tape: clone-and-rewind: error, code %u\n", e);
        /* caller must handle error, e.g. calling tape_state_finish() */
    }
    
    return e;
    
}



int findfilenames_new (tape_state_t *ts,
                       uint8_t show_ui_window,
                       uint8_t enable_phantom_block_protection) {

    int e;
    int32_t n;
    char fn[11];
    char s[256];
    uint32_t load, exec;
    uint32_t file_len;
    tape_state_t tclone;
    ACIA acia_tmp;
    
    load = 0;
    exec = 0;
    file_len = 0;
    
    /* get a clone of the live tape state,
       so we don't interfere with any ongoing cassette operation: */
    e = tape_state_clone_and_rewind (&tclone, ts);
    
    /* and make a suitable ACIA */
    memset(&acia_tmp, 0, sizeof(ACIA));
    acia_init(&acia_tmp);
    acia_tmp.control_reg = 0x14; // 8N1
    
    while ( /*TAPE_IS_LOADED(tclone.filetype_bits) &&*/
            (TAPE_E_OK == e) ) {
    
        int32_t limit;
        uint16_t blknum;
        uint16_t blklen;
        uint8_t final;
        uint8_t empty;
    
        memset(fn, 0, 11);
        
        limit = 29; /* initial byte limit, will be updated once block len is known */
        
        blknum = 0;
        blklen = 0;
        final = 0;
        empty = 0;
        load = 0;
        exec = 0;
        memset (s, 0, 256);
        
        /* process one block: */
        for (n=0; (n < limit) && (TAPE_E_OK == e); n++) {
        
            uint8_t k;
            uint8_t cat_1200th;
            uint8_t value;
            
            /* assume 8N1/1200.
             * 
             * MOS always uses 8N1; there is no concept of
             * a "filename" without 8N1. However, the baud rate
             * could be 300 rather than 1200. A job for another day
             * will be to try both baud rates, and
             * pick the one that produces the more meaningful outcome,
             * although this gets tougher for mixed-baud tapes.
             * 
             * For UEF chunks &100, &102 and &104 we could bypass
             * tape_read_poll_...() and rifle through the UEF
             * file itself; this would be baud-agnostic. It wouldn't
             * help with chunk &114, nor with CSW or TIBET, though. */

            e = tape_tone_1200th_from_back_end (0, /* don't strip silence and leader */
                                                &tclone,
                                                acia_rx_awaiting_start(&acia_tmp), /* awaiting start bit? */
                                                enable_phantom_block_protection, // enable
                                                (char *) &cat_1200th);
            if (TAPE_E_OK != e) { break; }
            
            /* just assume 1200 baud for now, then
               (i.e. 1/1200th = 1 ACIA RX bit): */
            e = acia_receive_bit_code (&acia_tmp, cat_1200th);
            
            if (TAPE_E_OK != e) { break; }
            
            if ( ! acia_rx_frame_ready(&acia_tmp) ) {
                n--;
                continue; /* frame not ready, go around again */
            }
            
            /* frame ready */
            
            value = acia_read(&acia_tmp, 0); // status reg
            value = acia_read(&acia_tmp, 1); // data reg
            
            if (0 == n) { /* 0 */
                /* need sync byte */
                if ('*' != value) {
                    n--; /* try again */
                }
            } else if (n<12) { /* 1-11 */
                if ((11==n) && (value!=0)) {
                    /* no NULL terminator found; abort, resync */
                    n=-1;
                } else {
                    fn[n-1] = (char) value;
                    /* handle terminator */
                    if (0 == value) {
                        memset(fn + n, 0, 11 - n);
                        n = 11;
                    /* sanitise non-printable characters */
                    } else if ( ! isprint(value) ) {
                        fn[n-1] = '?';
                    }
                }
            } else if (n<16) { /* 12-15 */
                load = (load >> 8) & 0xffffff;
                load |= (((uint32_t) value) << 24);
            } else if (n<20) { /* 16-19 */
                exec = (exec >> 8) & 0xffffff;
                exec |= (((uint32_t) value) << 24);
            } else if (n<22) { /* 20-21 */
                blknum = (blknum >> 8) & 0xff;
                blknum |= (((uint16_t) value) << 8);
            } else if (n<24) { /* 22-23 */
                blklen = (blklen >> 8) & 0xff;
                blklen |= (((uint16_t) value) << 8);
            } else if (n<25) { /* 24 */
                if (blklen < 0x100) {
                    final = 1;
                }
                if (0x80 & value) {
                    /* 'F' flag */
                    final = 1;
                }
                if (0x40 & value) {
                    /* 'E' flag */
                    empty = 1;
                }
            } else if (n<29) { /* 25-28 */
                /* As a sanity check, make sure that the next file address bytes
                   are all zero. If they're not, we'll skip this, because it's
                   likely not actually a file. Some protection schemes will
                   obviously break this.
                   (Ideally we'd check the header CRC instead) */
                if (value != 0) {
                    file_len = 0;
                    break; /* abort */
                }
                if (28 == n) {
                    /* add block length to file total */
                    file_len += (uint32_t) blklen;
                    if (final) {
                        for (k=0; k < 10; k++) {
                            /* for compatibility with what CSW does */
                            if (fn[k] == '\0') { fn[k] = ' '; }
                        }
                        sprintf(s, "%s Size %04X Load %08X Run %08X", fn, file_len, load, exec);
                        /* TOHv3: For tape testing, don't bother to spawn the UI window.
                           It can lead to a race condition when shutdown happens
                           immediately on end-of-tape. Besides, the user will never
                           see it. */
                        if (show_ui_window) {
                            cataddname(s);
                        } else {
                            /* TOHv3: for -tapetest: print to stdout;
                               wanted to use stderr, but difficulties
                               capturing output from it under Windows? */
                            fprintf(stdout, "tapetest:%s %04x %08x %08x\n", fn, file_len, load, exec);
                        }
                        file_len = 0;
                        memset(fn, 0, 11);
                    }
                    /* skip remainder of block: HCRC2 + data + DCRC2 (but not if file empty) */
                    limit += 2;
                    if ( ! empty ) {
                        limit += (blklen + 2);
                    }
                }
            } else {
                /* actual block data here */
                /* ... skip byte ... */
            }
            
        } /* next byte in block */
        
    } /* next block */
    
    /* artificially force b-em shutdown, if TAPE_TEST_QUIT_ON_EOF;
       this is for automated testing via -tapetest on CL */
    /* TOHv3.2: errors now take precedence over EOF condition */
    tape_handle_exception (&tclone, NULL, e, tape_vars.testing_mode & TAPE_TEST_QUIT_ON_EOF, 0);
    
    /* free clone */
    tape_state_finish(&tclone, 0); /* 0 = DO NOT alter menus since this is a clone */
    
    return e;
    
}




void tape_handle_exception (tape_state_t *ts,
                            tape_vars_t *tv, /* TOHv3.2: may be NULL */
                            int error_code,
                            uint8_t eof_fatal,
                            uint8_t err_fatal) {
    int e;
    /* TOHv3.2: errors take precedence over EOF */
    if ((error_code != TAPE_E_EOF) && (error_code != TAPE_E_OK)) {
        if (TAPE_E_EXPIRY == error_code) {
            quitting = 1;
            set_shutdown_exit_code(SHUTDOWN_EXPIRED);
            return;
        }
        log_warn("tape: code %d; disabling tape! (Eject tape to clear.)", error_code);
        tape_state_finish(ts, 1); /*tv, 1, 1);*/  /* alter menus  */
        e = tape_set_record_activated(ts, tv, NULL, 0);
        if (TAPE_E_OK == e) {
            gui_set_record_mode(0);
            tape_state_init(ts, tv, 0, 1);    /* alter menus */
        }
        tape_state.disabled_due_to_error = 1;
        if (err_fatal) {
            quitting = 1;
            set_shutdown_exit_code(SHUTDOWN_TAPE_ERROR);
        }
    } else if (TAPE_E_EOF == error_code) {
/*        log_warn("tape: end of tape");*/
        if (eof_fatal) {
            quitting = 1;
            set_shutdown_exit_code(SHUTDOWN_TAPE_EOF);
        }
    }
}

/* not currently used in TOHv4 */
/*
static uint8_t tape_peek_eof (tape_state_t *t) {

    uint8_t r;*/

    /* changed for simultaneous filetypes
    
       Things start to get potentially thorny here, because we
       could have parallel representations that disagree on precisely
       when the EOF is. We will operate on the principle that
       if we have simultaneous filetypes (because we're writing
       to output and user hasn't picked what kind of file we want
       to save yet), just use UEF for reading back. */
       
    /*
    r = 1;
       
    if (TAPE_FILETYPE_BITS_UEF & t->filetype_bits) {
        r = uef_peek_eof(&(t->uef));
    } else if (TAPE_FILETYPE_BITS_CSW & t->filetype_bits) {
        r = csw_peek_eof(&(t->csw));
    } else if (TAPE_FILETYPE_BITS_TIBET & t->filetype_bits) {
        r = 0xff & tibet_peek_eof(&(t->tibet));
    }
    
    return r;
    
} */


#include "6502.h"

static int tape_read_1200th (tape_state_t *ser, char *value_out) {
    
    int e;
    uef_meta_t meta[UEF_MAX_METADATA];
    uint32_t meta_len;
    uint32_t m;
    
    e = TAPE_E_OK;
    
    /* TOHv3: Again the complication:
       Saved data where the user hasn't picked an output format yet exists
       as three parallel copies of itself. In this situation we use UEF
       as the copy to read from, mainly because we might have written
       UEF metadata into it which we would like to read back; we don't
       have as rich metadata in the other file types. (TIBET allows timestamps
       etc. but UEF's potential metadata is richer and more useful.) */
    
    /* so, we check for UEF first. */
    if (TAPE_FILETYPE_BITS_UEF & ser->filetype_bits) {
        meta_len = 0;
        e = uef_read_1200th (&(ser->uef),
                             value_out,
                             meta,
                             &meta_len);
        /* TODO: Any UEF metadata chunks that punctuate actual data
         * chunks are accumulated on meta by the above call. Currently
         * this includes chunks &115 (phase change), &120 (position marker),
         * &130 (tape set info), and &131 (start of tape side). At
         * present, nothing is done with these chunks and they are
         * simply destroyed again here. For now we'll just log them */
        if ((TAPE_E_OK == e) || (TAPE_E_EOF == e)) { /* throws EOF */
            for (m=0; m < meta_len; m++) {
                if (meta[m].type != 0x117) { /* only baud rate is currently used */
                    log_info("uef: unused metadata, chunk &%x\n", meta[m].type);
                }
            }
            uef_metadata_list_finish (meta, meta_len);
        }
    } else if (TAPE_FILETYPE_BITS_CSW & ser->filetype_bits) {
        e = csw_read_1200th (&(ser->csw), value_out);
    } else if (TAPE_FILETYPE_BITS_TIBET & ser->filetype_bits) {
        /* The TIBET reference decoder has a built-in facility
         * for decoding 300 baud; however, it is not used here,
         * so zero is always passed to it. We always request
         * 1/1200th tones from the TIBET back-end (and all other
         * back-ends). 300 baud decoding is now done by b-em itself,
         * regardless of the back-end in use. */
        e = tibet_read_bit (&(ser->tibet),
                            0, /* always 1/1200th tones */
                            value_out);
    } else if (TAPE_FILETYPE_BITS_NONE == ser->filetype_bits) {
        e = TAPE_E_EOF;
    } else {
        log_warn("tape: BUG: unknown internal filetype: &%x", ser->filetype_bits);
        return TAPE_E_BUG;
    }
    
    if (TAPE_E_EOF == e) {
#ifdef BUILD_TAPE_LOOP /* TOHv2 */
        log_warn("tape: tape finished; rewinding (TAPE_LOOP)");
        e = tape_rewind_2(ser);
#else
        if ( ! tape_state.tape_finished_no_emsgs ) {
            log_warn("tape: tape finished");
        }
        tape_state.tape_finished_no_emsgs = 1; /* suppress further messages */
#endif
    } else {
        tape_state.tape_finished_no_emsgs = 0;
    }

    return e;

}


void tape_rewind(void) {
    tape_rewind_2 (&tape_state);
}


static int tape_rewind_2 (tape_state_t *t) {
    int e;
    e = TAPE_E_OK;
    if (TAPE_FILETYPE_BITS_CSW   & t->filetype_bits) {
        csw_rewind(&(t->csw));
    }
    if (TAPE_FILETYPE_BITS_TIBET & t->filetype_bits) {
        e = tibet_rewind(&(t->tibet));
    }
    if (TAPE_FILETYPE_BITS_UEF   & t->filetype_bits) {
        uef_rewind(&(t->uef));
    }
    t->tape_finished_no_emsgs = 0; /* TOHv2: un-inhibit "tape finished" msg */
    return e;
}


/* tv may be NULL: */
void tape_state_init (tape_state_t *t,
                      tape_vars_t *tv,
                      uint8_t filetype_bits,
                      uint8_t alter_menus) {
                      
    /* TOHv3.2: we don't touch Record Mode any more. If -record is
       specified on the command line along with -tape (for auto-appending),
       we don't want tape_state_init() or tape_state_finish() to cancel it. */
    
    tape_state_finish(t, alter_menus); /* alter menus (grey-out Eject Tape, etc.) */
    t->filetype_bits = filetype_bits;
    if (alter_menus) {
        gui_alter_tape_menus(filetype_bits);
        gui_set_record_mode(tape_is_record_activated(tv));
    }
    t->w_uef_prevailing_baud = 1200; /* TOHv4 */
    
    /* TOHv4 */
    /* This is a hack to make sure that the initial cached
       values for dividers and thresholds that are stored on tape_state
       are set to sane values, before MOS makes the first write to
       the serial ULA's control register. */
    serial_recompute_dividers_and_thresholds (tape_vars.overclock,
                                              0x64, //tape_state.ula_ctrl_reg,
                                              &(tape_state.ula_rx_thresh_ns),
                                              &(tape_state.ula_tx_thresh_ns),
                                              &(tape_state.ula_rx_divider),
                                              &(tape_state.ula_tx_divider));

}



void tape_state_finish (tape_state_t *t, uint8_t alter_menus) {
    // uint8_t old_ula_ctrl_reg_value;
    /* TOHv3: updated for simultaneous output formats */
    if (TAPE_FILETYPE_BITS_UEF & t->filetype_bits) {
        uef_finish(&(t->uef));
    }
    if (TAPE_FILETYPE_BITS_CSW & t->filetype_bits) {
        csw_finish(&(t->csw));
    }
    if (TAPE_FILETYPE_BITS_TIBET & t->filetype_bits) {
        tibet_finish(&(t->tibet));
    }
    if (t->w_uef_tmpchunk.data != NULL) {
        free(t->w_uef_tmpchunk.data);
    }

    /* TOHv4-rc2: no longer just zero out the whole state;
     *            turns out that is hoodlum behaviour. Be more selective */

    /*memset(t, 0, sizeof(tape_state_t));*/
    if (t->tibet_data_pending.tones != NULL) {
        free(t->tibet_data_pending.tones);
    }
    memset(&(t->tibet_data_pending), 0, sizeof(tibet_span_t));
    t->w_must_end_data = 0;

    if (alter_menus) {
        /* set displayed Eject path to blank: */
        gui_alter_tape_eject(NULL);
        /* deactivate Record Mode and update menus accordingly;
         * simultaneously show all file types for saving since all
         * are available on a blank tape: */
        gui_alter_tape_menus(TAPE_FILETYPES_ALL);
        /*gui_alter_tape_menus_2();*/ /* grey out Catalogue Tape */
    }
    /*tape_set_record_activated(t, &tape_vars, &sysacia, 0);*/
    t->filetype_bits = TAPE_FILETYPES_ALL;
    t->w_accumulate_silence_ns     = 0;
    t->w_accumulate_leader_ns      = 0;
    memset(&(t->w_uef_tmpchunk), 0, sizeof(uef_chunk_t));
    t->w_uef_origin_written        = 0;
    t->w_uef_prevailing_baud       = 1200;
    t->w_uef_serial_frame          = 0;
    t->w_uef_serial_phase          = 0;
    t->w_uef_enc100_shift_value    = 0;
    t->w_uef_enc100_shift_amount   = 0;
    t->w_uef_was_loaded            = 0;
    t->disabled_due_to_error       = 0;
    t->tones300_fill               = 0;
    t->ula_prevailing_rx_bit_value = 'S';

    /* TODO: pass in sysacia */
    serial_push_dcd_cts_lines_to_acia(&sysacia, t);
    acia_update_irq_and_status_read(&sysacia);

}

uint8_t tape_is_record_activated(tape_vars_t *tv) {
    return tv->record_activated;
}

int tape_set_record_activated (tape_state_t *t, tape_vars_t *tv, ACIA *acia_or_null, uint8_t value) {
    int32_t baud;
    if (NULL == tv) {
        log_warn("BUG: tape_set_record_activated passed NULL tape_vars_t\n");
        return TAPE_E_BUG;
    } else if (NULL == t) {
        log_warn("BUG: tape_set_record_activated passed NULL tape_state_t\n");
        return TAPE_E_BUG;
    }
    if ( (0 == value) && (0 != tv->record_activated) ) {
        /* recording finished; flush any pending data */
        tape_flush_pending_piece(t, acia_or_null, tv->save_prefer_112);
    } else {
        /* work out current baud status when record is activated,
         * so we know whether we need a chunk &117 for the first data
         * chunk to be appended; set value on tape_state_t */
        baud = 0;
        uef_scan_backwards_for_chunk_117 (&(t->uef), &baud);
        if (0 == baud) {
            baud = 1200;
        }
        tape_state.w_uef_prevailing_baud = baud;
    }
    tv->record_activated = value;
    return TAPE_E_OK;
}


#define UNZIP_CHUNK 256
#define DECOMP_DELTA (100 * 1024)

int tape_decompress (uint8_t **out, uint32_t *len_out, uint8_t *in, uint32_t len_in) {

    /* if this returns E_OK, then 'in' will have been invalidated;
     * otherwise, 'in' is still valid, and must be freed by the caller. */

    uint8_t buf[UNZIP_CHUNK];
    size_t alloc;
    z_stream strm;
    uint32_t pos;
    
    *out = NULL;
    strm.zalloc   = Z_NULL;
    strm.zfree    = Z_NULL;
    strm.opaque   = Z_NULL;
    strm.next_in  = (unsigned char *) in;
    strm.avail_in = len_in;
    
    /* OR 32 allows us to decompress both zlib (for CSW)
       and gzip (for UEF and TIBETZ): */
    if ( inflateInit2 (&strm, 15 | 32) < 0 ) {
        log_warn("tape: could not decompress data; zlib init failed.");
        return TAPE_E_ZLIB_INIT;
    }
    
    alloc = 0;
    pos = 0;
    
    do {

        int zerr;
        uint32_t piece_len;
        uint32_t newsize;
        uint8_t *p;
        
        strm.avail_out = UNZIP_CHUNK;
        strm.next_out  = buf;
        
        zerr = inflate (&strm, Z_NO_FLUSH);
        
        switch (zerr) {
            case Z_OK:
            case Z_STREAM_END:
            case Z_BUF_ERROR:
                break;
            default:
                inflateEnd (&strm);
                log_warn ("tape: could not decompress data; zlib code %d", zerr);
                if (NULL != *out) { free(*out); }
                *out = NULL;
                return TAPE_E_ZLIB_DECOMPRESS;
        }
        
        piece_len = (UNZIP_CHUNK - strm.avail_out);
        
        p = NULL;
        
        if ((piece_len + pos) >= alloc) {
            newsize = piece_len + pos + DECOMP_DELTA;
            /* prevent shenanigans */
            if (newsize >= TAPE_MAX_DECOMPRESSED_LEN) {
                log_warn ("tape: decompressed size is too large\n");
                inflateEnd (&strm);
                if (*out != NULL) { free(*out); }
                *out = NULL;
                return TAPE_E_DECOMPRESSED_TOO_LARGE;
            }
            p = realloc (*out, newsize);
            if (NULL == p)  {
                log_warn ("tape: could not decompress data; realloc failed\n");
                inflateEnd (&strm);
                if (*out != NULL) { free(*out); }
                *out = NULL;
                return TAPE_E_MALLOC;
            }
            *out = p;
            alloc = newsize;
        }
        
        memcpy (*out + pos, buf, piece_len);
        pos += piece_len;
        
    } while (strm.avail_out == 0);
    
    inflateEnd (&strm);
    
    *len_out = pos;
    
    return TAPE_E_OK;
    
}


int tape_load_file (const char *fn, uint8_t decompress, uint8_t **buf_out, uint32_t *len_out) {
    
    FILE *f;
    uint32_t pos, alloc;
    uint8_t *buf;
    int e;
    uint8_t *buf2;
    uint32_t buf2_len;
    
    e = TAPE_E_OK;
    
    buf = NULL;
    buf2 = NULL;
    buf2_len = 0;

    if (NULL == (f = fopen (fn, "rb"))) {
        log_warn("tape: Unable to open file '%s': %s", fn, strerror(errno));
        return TAPE_E_FOPEN;
    }
    
    pos = 0;
    alloc = 0;
    
#define TAPE_FILE_ALLOC_DELTA (1024 * 1024)
    
    while ( ! feof(f) && ! ferror(f) ) {
        uint32_t chunk;
        uint32_t newsize;
        long num_read;
        uint8_t *p;
        chunk = 1024;
        /* ask for 1024 bytes */
        if ((pos + chunk) >= TAPE_FILE_MAXLEN) {
            log_warn("tape: File is too large: '%s' (max. %d)", fn, TAPE_FILE_MAXLEN);
            e = TAPE_E_FILE_TOO_LARGE;
            break;
        }
        if ((pos + chunk) >= alloc) {
            newsize = pos + chunk + TAPE_FILE_ALLOC_DELTA;
            p = realloc(buf, newsize);
            if (NULL == p) {
                log_warn("tape: Failed to grow file buffer: '%s'", fn);
                e = TAPE_E_MALLOC;
                break;
            }
            alloc = newsize;
            buf = p;
        }
        num_read = fread (buf+pos, 1, chunk, f);
        if (ferror(f) || (num_read < 0)) {
            log_warn("tape: Stream error reading file '%s': %s", fn, strerror(errno));
            e = TAPE_E_FREAD;
            break;
        }
        pos += (uint32_t) (0x7fffffff & num_read);
    }
    
    fclose(f);
    
    if (TAPE_E_OK != e) {
        free(buf);
        return e;
    }
    
    /* TOHv3.2: avoid attempting decompression, if uncompressed UEF is detected */
    e = uef_detect_magic (buf, pos, fn, 0); /* 0 = quiet, no errors */
    if (TAPE_E_OK == e) { decompress = 0; }
    e = TAPE_E_OK; /* trap errors */
    
    if (decompress) {
        e = tape_decompress (&buf2, &buf2_len, buf, pos & 0x7fffffff);
        free(buf);
        if (TAPE_E_OK != e) { return e; }
        log_info("tape_decompress: %u -> %u\n", (uint32_t) (pos & 0x7fffffff), buf2_len);
        pos = buf2_len;
        buf = buf2;
    }
    
    *buf_out = buf;
    *len_out = pos;
    
    return TAPE_E_OK;
    
}


static void tibet_load (const char *fn) {
    tibet_load_2(fn, 0);
}

static void tibetz_load (const char *fn) {
    tibet_load_2(fn, 1);
}

static void uef_load (const char *fn) {
    int e;
    int32_t baud;
    tape_state_init (&tape_state, &tape_vars, TAPE_FILETYPE_BITS_UEF, 1);
    e = uef_load_file (fn, &(tape_state.uef));
    if (TAPE_E_OK != e) {
        log_warn("tape: could not load UEF file (code %d): '%s'", e, fn);
        tape_handle_exception(&tape_state,
                              &tape_vars,
                              e,
                              tape_vars.testing_mode & TAPE_TEST_QUIT_ON_EOF,
                              tape_vars.testing_mode & TAPE_TEST_QUIT_ON_ERR);
    } else {
        load_successful((char *) fn);
        tape_state.w_uef_was_loaded = 1;
        tape_state.w_uef_origin_written = 0;
        baud = 0;
        uef_scan_backwards_for_chunk_117 (&(tape_state.uef), &baud);
        if (0 == baud) { baud = 1200; }
        tape_state.w_uef_prevailing_baud = baud;
    }
}


static void tibet_load_2 (const char *fn, uint8_t decompress) {
    int e;
    tape_state_init (&tape_state, &tape_vars, TAPE_FILETYPE_BITS_TIBET, 1); /* alter menus */
    e = tibet_load_file (fn, decompress, &(tape_state.tibet));
    if (TAPE_E_OK != e) {
        log_warn("tape: could not load TIBET file (code %d): '%s'", e, fn);
        tape_handle_exception (&tape_state,
                               &tape_vars,
                               e,
                               tape_vars.testing_mode & TAPE_TEST_QUIT_ON_EOF,
                               tape_vars.testing_mode & TAPE_TEST_QUIT_ON_ERR);
    } else {
        load_successful((char *)fn);
    }
}

static void csw_load (const char *fn) {
    int e;
    tape_state_init (&tape_state, &tape_vars, TAPE_FILETYPE_BITS_CSW, 1); /* alter menus */
    e = csw_load_file (fn, &(tape_state.csw));
    if (TAPE_E_OK != e) {
        log_warn("tape: could not load CSW file (code %d): '%s'", e, fn);
        tape_handle_exception(&tape_state,
                              &tape_vars,
                              e,
                              tape_vars.testing_mode & TAPE_TEST_QUIT_ON_EOF,
                              tape_vars.testing_mode & TAPE_TEST_QUIT_ON_ERR);
    } else {
        load_successful((char *)fn);
    }
}

static void load_successful (char *path) {

    /* TOHv3: tapellatch removed */
                    
    tapelcount  = 0;
    
    gui_alter_tape_eject (path);
    gui_alter_tape_menus(tape_state.filetype_bits);
    /*gui_set_record_mode(tape_vars.record_activated);*/
    
}


static int tibet_load_file (const char *fn, uint8_t decompress, tibet_t *t) {

    int e;
    char *buf;
    uint32_t len;
    
    len = 0;
    buf = NULL;

    e = tape_load_file (fn, decompress, (uint8_t **) &buf, &len);
    if (TAPE_E_OK != e) { return e; }

    e = tibet_decode (buf, len, t);
    if (TAPE_E_OK != e) {
        log_warn("tape: error (code %d) decoding TIBET file '%s'", e, fn);
        free(buf);
        return e;
    }
    
    free(buf);
    buf = NULL;

    return TAPE_E_OK;
    
}


/* TOHv3 */
void tape_write_u32 (uint8_t b[4], uint32_t v) {
    b[0] = v & 0xff;
    b[1] = (v >> 8) & 0xff;
    b[2] = (v >> 16) & 0xff;
    b[3] = (v >> 24) & 0xff;
}

/* TOHv3 */
void tape_write_u16 (uint8_t b[2], uint16_t v) {
    b[0] = v & 0xff;
    b[1] = (v >> 8) & 0xff;
}

uint32_t tape_read_u32 (uint8_t *in) {
    uint32_t u;
    u =   (  (uint32_t)in[0])
        | ((((uint32_t)in[1]) << 8) & 0xff00)
        | ((((uint32_t)in[2]) << 16) & 0xff0000)
        | ((((uint32_t)in[3]) << 24) & 0xff000000);
    return u;
}

uint32_t tape_read_u24 (uint8_t *in) {
    uint32_t u;
    u =   (  (uint32_t)in[0])
        | ((((uint32_t)in[1]) << 8) & 0xff00)
        | ((((uint32_t)in[2]) << 16) & 0xff0000);
    return u;
}

uint16_t tape_read_u16 (uint8_t *in) {
    uint16_t u;
    u =   (  (uint16_t)in[0])
        | ((((uint16_t)in[1]) << 8) & 0xff00);
    return u;
}



int tape_zlib_compress (char *source_c,
                        size_t srclen,
                        uint8_t use_gzip_encoding,
                        char **dest, /* TOHv3.2: protect against NULL *dest */
                        size_t *destlen) {

    int ret, flush;
    z_stream strm;
    size_t alloced=0;
    uint8_t *source;
    
    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    
    /* char -> uint8 */
    source = (uint8_t *) source_c;

    /* TOHv3.2: added refusal to compress empty buffer */
    if ((0 == srclen) || (srclen > 0x7fffffff)) {
        log_warn("tape: write: compress: BUG: srclen is insane (%zu). Aborting.", srclen);
        return TAPE_E_BUG;
    }
    
    if (use_gzip_encoding) {
        ret = deflateInit2 (&strm,
                            Z_BEST_COMPRESSION, /* Z_DEFAULT_COMPRESSION */
                            Z_DEFLATED,
                            15 + 16, /* windowbits 15, +16 for gzip encoding */
                            8,
                            Z_DEFAULT_STRATEGY);
    } else {
        ret = deflateInit  (&strm, Z_BEST_COMPRESSION);
    }
    if (ret != Z_OK) {
        return TAPE_E_SAVE_ZLIB_INIT;
    }

    /* compress until end of file */
    strm.avail_in = 0x7fffffff & srclen;
    flush = Z_FINISH;
    
    strm.next_in = source;

    /* run deflate() on input until output buffer not full, finish
       compression if all of source has been read in */
    do {
    
        char *dest2;

        if (alloced <= *destlen) { /* realloc if no space left */
            dest2 = realloc(*dest, alloced = (*destlen ? (*destlen * 2) : srclen));
            if (NULL == dest2) {
                log_warn("tape: write: compress: Failed to grow zlib buf (newsize %zu).", *destlen);
                deflateEnd(&strm);
                /* TOHv3.2: avoid free(NULL) */
                if (*dest != NULL) {
                    free(*dest);
                }
                *dest = NULL;
                return TAPE_E_MALLOC;
            }
            *dest = dest2;
        }
        strm.avail_out = 0x7fffffff & (alloced - *destlen); /* bytes available in output buffer */
        strm.next_out = (uint8_t *) (*dest + *destlen);     /* current offset in output buffer */
        ret = deflate(&strm, flush);         /* no bad return value */
        *destlen += (alloced - *destlen) - strm.avail_out;
      
    } while (strm.avail_out == 0);
    
    /* clean up */
    deflateEnd(&strm);
    
    if ( ( strm.avail_in != 0 ) || ( ret != Z_STREAM_END ) ) {     /* all input will be used */
        if (strm.avail_in != 0) {
            log_warn("tape: write: compress: zlib compression failed: strm.avail_in != 0.");
        } else {
            log_warn("tape: write: compress: zlib compression failed (code %u).", ret);
        }
        free(*dest);
        *dest = NULL;
        return TAPE_E_SAVE_ZLIB_COMPRESS;
    }
    
    return TAPE_E_OK;
  
}




/* TOHv3 */
void tape_init (tape_state_t *ts, tape_vars_t *tv) {
    /*uint8_t old_serial_ula_ctrl_reg_value;
    old_serial_ula_ctrl_reg_value = ts->ula_ctrl_reg;*/ /* TOHv4-rc2: BUGFIX LOL */
    memset(ts, 0, sizeof(tape_state_t));
    /*ts->ula_ctrl_reg = old_serial_ula_ctrl_reg_value;*/
    tape_set_record_activated(ts, tv, NULL, 0); /* TOHv3.2, v4 */
    tv->record_activated         = 0;
    tapelcount                   = 0;
    tapeledcount                 = 0;
    tv->overclock                = false;
    tv->save_filename            = NULL;
    tv->load_filename            = NULL;
    tv->strip_silence_and_leader = 0;

}

int tape_save_on_shutdown (uef_state_t *uef,
                           uint8_t record_is_pressed,
                           uint8_t *our_filetype_bits_inout,
                           uint8_t silence112,
                           tape_shutdown_save_t *c) {
    int e;

    /* TOHv4-rc2 */
    e = wp1_4k8_init_blank_tape_if_none_loaded (our_filetype_bits_inout, record_is_pressed);
    if (TAPE_E_OK != e) { return e; }

    /* TOHv4-rc2: add an origin chunk if there isn't one */
    if ((c->filetype_bits & TAPE_FILETYPE_BITS_UEF) && (0 == uef->num_chunks) && record_is_pressed) {
        e = uef_store_chunk(uef,
                            (uint8_t *) VERSION_STR,
                            0, /* origin, chunk &0000 */
                            strlen(VERSION_STR)+1); /* include the \0 */
        if (TAPE_E_OK != e) { return e; }
    }

    if (0 != (*our_filetype_bits_inout & c->filetype_bits)) {
        e = tape_generate_and_save_output_file (c->filetype_bits,
                                                silence112,
                                                c->filename,
                                                c->do_compress,
                                                NULL);
        if (TAPE_E_OK == e) {
            log_info("tape: -tapesave: saved file: %s", c->filename);
        } else {
            log_warn("tape: -tapesave: error saving file: %s", c->filename);
        }
    } else if (0 != c->filetype_bits) {
        log_warn ("tape: -tapesave error: data does not exist in desired format (have &%x, want &%x)",
                  *our_filetype_bits_inout, c->filetype_bits);
    }
    return TAPE_E_OK;
}


/* TOHv3.2: added explicit tape_start_motor(), tape_stop_motor(), tape_is_motor_running()
 * made motor variable private to tape.c/.h
 * TOHv3.3: moved some stuff here from serial.c */
void tape_start_motor (tape_state_t *ts, uint8_t also_force_dcd_high) {
    if (ts->ula_motor) { return; }
    ts->ula_motor = 1;
    tapenoise_motorchange(1);
    led_update(LED_CASSETTE_MOTOR, 1, 0);
    /* hack for strip-silence-and-leader mode:
     * there is never enough leader to initialise DCD,
     * so no bits are detected; this hack will do that artificially. */
    /*if (tv->strip_silence_and_leader) {*/
    if (also_force_dcd_high) {
        acia_dcdhigh(&sysacia);
    }
}

void tape_stop_motor(tape_state_t *ts) {
    if ( ! ts->ula_motor ) { return; }
    ts->ula_motor = 0;
    tapeledcount = 2; /* FIXME: ???? (formerly from serial.c) */
    tapenoise_motorchange(0);
}

int tape_rs423_eat_1200th (tape_state_t *ts,
                           tape_vars_t *tv,
                           ACIA *acia,
                           uint8_t emit_tapenoise,
                           uint8_t *throw_eof_out) {

    int e;
    char tone;

    /* consume tape 1200th */
    tone = 0;
    e = tape_tone_1200th_from_back_end (0, ts, 0, 0, &tone);
    if (TAPE_E_EOF == e) {
        *throw_eof_out = 1;
        e = TAPE_E_OK;
    }
    if (TAPE_E_OK != e)  { return e; }

    /* we also need to send s/1200 of silence to the TX back end */
    e = tape_write_bitclk (ts,
                           acia,
                           'S',
                           TAPE_1200TH_IN_NS_INT,
                           1, /* silent */
                           emit_tapenoise && tv->record_activated,
                           tv->record_activated,
                           tv->save_always_117,
                           tv->save_prefer_112,
                           tv->save_do_not_generate_origin_on_append);
    if (TAPE_E_OK != e) { return e; }
    
    if (0 != tone) { ts->ula_prevailing_rx_bit_value = tone; }
    
    return e;

}


int tape_fire_acia_rxc (tape_state_t *ts,
                        tape_vars_t *tv,
                        ACIA *acia,
                        int32_t acia_rx_divider,
                        uint8_t emit_tapenoise,
                        uint8_t *throw_eof_out) {

    int64_t ns_per_bit;
    uint8_t bit_ready;
    char bit_in;
    int e, m;
    
    e = TAPE_E_OK;

    /* TOHv4-rc6: now gated by ula_motor; Atic-Atac loader + BREAK tapenoise bug */
    if ( ! ts->ula_motor ) { return TAPE_E_OK; }

    /* both dividers have fired, so the ACIA now NEEDS A BIT from the appropriate tape back end;
       however, the back ends emit 1200ths of TONE, not bits, so we need to convert
       for 300 baud, and possibly also for 19200 baud if we want to try to support
       whatever happens if you try to feed 1200/2400 tone to ~19.2 KHz decoding;
       (I really need to test this on hardware) */
    
    /* 1200 baud is 832016ns; if double-divided ACIA clock tick is faster than this, then
       behaviour is "undefined" (specifically, for 19.2 KHz); nevertheless, the actual
       hardware must do *something*; ideally, an emulator would duplicate the hardware's
       exact sequence of e.g. framing errors and partial bytes; also I am wondering if
       it is possible to craft a custom stream that decodes successfully at 19.2 KHz */
    
    ns_per_bit = (13 * 1000 * 64 * acia_rx_divider) / 16;
    
    bit_in = '1';
    
    bit_ready = 0;
    
    if (832000 == ns_per_bit) {
    
        /* 1201.92 baud */
        e = tape_tone_1200th_from_back_end (tv->strip_silence_and_leader,
                                            ts,
                                            acia_rx_awaiting_start(acia), /* from shift reg state */
                                            ! tv->disable_phantom_block_protection,
                                            &bit_in);
        if (TAPE_E_EOF == e) {
            *throw_eof_out = 1;
            e = TAPE_E_OK;
        }
        if ( TAPE_E_OK != e ) { return e; }
        /* TOHv4-rc6: now gated by ula_motor; tackle Atic-Atac loader tapenoise after BREAK bug */
        if ( ( ! tv->record_activated ) && emit_tapenoise && ts->ula_motor) {
            tapenoise_send_1200 (bit_in, &(ts->tapenoise_no_emsgs));
        }
        bit_ready = 1;
        ts->tones300_fill = 0;
        ts->ula_prevailing_rx_bit_value = bit_in;
        
    } else if (3328000 == ns_per_bit) {
    
        /* 300.48 baud, so we must consume four 1200ths;
           note that we always do this regardless of whether
           it makes a valid bit or not ... */
        
        for (m=0; m < TAPE_TONES300_BUF_LEN; m++) {
        
            char tone, a, b;
        
            e = tape_tone_1200th_from_back_end (tv->strip_silence_and_leader,
                                                ts,
                                                acia_rx_awaiting_start(acia), // from shift reg state
                                                ! tv->disable_phantom_block_protection,
                                                &tone);
            if (TAPE_E_EOF == e) {
                *throw_eof_out = 1;
                e = TAPE_E_OK;
            }
            if (TAPE_E_OK != e) { return e; }
            
            ts->ula_prevailing_rx_bit_value = tone;
            
            ts->tones300[ts->tones300_fill] = tone;
            
            b = ('L' == tone)            ? '1' : tone;
            a = ('L' == ts->tones300[0]) ? '1' : ts->tones300[0];
            
            /* does the new 1200th match tones[0] ? */
            if (a != b) {
                log_info("tape: warning: fuzzy 300-baud bit: [ %c %c %c %c ]; resynchronising",
                         ts->tones300[0],
                         (ts->tones300[1]!=0)?ts->tones300[1]:' ',
                         (ts->tones300[2]!=0)?ts->tones300[2]:' ',
                         (ts->tones300[3]!=0)?ts->tones300[3]:' ');
                /* shift down and resync */
                ts->tones300[0] = tone;
                ts->tones300[1] = 0;
                ts->tones300[2] = 0;
                ts->tones300[3] = 0;
                ts->tones300_fill = 1;
            } else if ( (TAPE_TONES300_BUF_LEN - 1) == ts->tones300_fill ) {
                /* bit ready */
                bit_ready = 1;
                bit_in = ts->tones300[0];
                ts->tones300_fill = 0;
                if ( ( ! tv->record_activated ) && emit_tapenoise ) {
                    tapenoise_send_1200 (bit_in, &(ts->tapenoise_no_emsgs));
                    tapenoise_send_1200 (bit_in, &(ts->tapenoise_no_emsgs));
                    tapenoise_send_1200 (bit_in, &(ts->tapenoise_no_emsgs));
                    tapenoise_send_1200 (bit_in, &(ts->tapenoise_no_emsgs));
                }
            } else {
                (ts->tones300_fill)++;
            }
            
        }

    } else if (52000 == ns_per_bit) {
        /* 19230.77 baud */
        /* log_warn("tape: warning: RX @ 19231 baud (I dispute that this is unusable; assume high tone for now)"); */
      ts->tones300_fill = 0;
    } else {
        log_warn("tape: BUG: RX: bad ns_per_bit (%"PRId64")", ns_per_bit);
        e = TAPE_E_BUG;
    }
    
    if (TAPE_E_OK != e) { return e; }
    
    if (bit_ready) {
        e = acia_receive_bit_code (acia, bit_in);
    }
    
    return e;
    
}
