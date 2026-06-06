/*
 *  B-Em
 *  This file (C) 2026 'Diminished'
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

/* this file new in TOHv4.3 */

#include "tape2.h"

#ifdef BUILD_TAPE_TAPECTRL

#include "tapectrl.h"

#define TAPECTRL_W                          720.0f
#define TAPECTRL_H                          500.0f

#define TAPECTRL_FIXED_MARGINS              32.0f

#define RECT_OUTLINE_W_PX_FLT               1.0f

#define SEEKER_H                            14.0f

#define VOLUME_CLICKZONE_X                  45.0f
#define VOLUME_CLICKZONE_Y                  15.0f
#define SEEKER_CLICKZONE_Y                  VOLUME_CLICKZONE_X
#define SEEKER_Y                            305.0f
#define SEEKER_TRACK_WIDTH                  9.0f
#define KNOB_H                              18.0f
#define KNOB_W                              16.0f
#define SEEKER_MARGINS_PX                   (TAPECTRL_FIXED_MARGINS + (KNOB_W / 2.0f))

#define IN_SEEKER_SCRUBZONE_Y(my,mgn_y,scale) (    (((float)(my)) >= (((float)mgn_y) + (scale*(SEEKER_Y - (SEEKER_CLICKZONE_Y / 2.0f))))) \
                                                && (((float)(my)) <= (((float)mgn_y) + (scale*(SEEKER_Y + (SEEKER_CLICKZONE_Y / 2.0f))))))
                                         

#define IN_VOLUME_SCRUBZONE_X(mx,mgn_x,scale) (    (((float)(mx)) >= (((float)mgn_x) + (scale*(TAPECTRL_VOLUME_X - (VOLUME_CLICKZONE_X / 2.0f))))) \
                                                && (((float)(mx)) <= (((float)mgn_x) + (scale*(TAPECTRL_VOLUME_X + (VOLUME_CLICKZONE_X / 2.0f))))))

#define TAPECTRL_NUM_BUTTONS          8
#define TAPECTRL_BUTTON_IX_LOAD       0
#define TAPECTRL_BUTTON_IX_SAVE       1
#define TAPECTRL_BUTTON_IX_CATALOGUE  2
#define TAPECTRL_BUTTON_IX_RECORD     3
#define TAPECTRL_BUTTON_IX_REWIND     4
#define TAPECTRL_BUTTON_IX_EJECT      5
#define TAPECTRL_BUTTON_IX_STRIP      6
#define TAPECTRL_BUTTON_IX_OVERCLOCK  7


#define TAPECTRL_ICON_ID_LOAD       TAPECTRL_BUTTON_IX_LOAD
#define TAPECTRL_ICON_ID_SAVE       TAPECTRL_BUTTON_IX_SAVE
#define TAPECTRL_ICON_ID_CATALOGUE  TAPECTRL_BUTTON_IX_CATALOGUE
#define TAPECTRL_ICON_ID_RECORD     TAPECTRL_BUTTON_IX_RECORD
#define TAPECTRL_ICON_ID_REWIND     TAPECTRL_BUTTON_IX_REWIND
#define TAPECTRL_ICON_ID_EJECT      TAPECTRL_BUTTON_IX_EJECT
#define TAPECTRL_ICON_ID_STRIP      TAPECTRL_BUTTON_IX_STRIP
#define TAPECTRL_ICON_ID_OVERCLOCK  TAPECTRL_BUTTON_IX_OVERCLOCK
/* non-button icons follow */
#define TAPECTRL_ICON_ID_PLAY       8
#define TAPECTRL_ICON_ID_TAPENOISE  9

#define TAPECTRL_VFD_PANEL_W                387.0f

                                            /* +RECT_OUTLINE_W_PX_FLT to line up outside edge with seeker: */
#define INLAY_X                             TAPECTRL_FIXED_MARGINS // (SEEKER_MARGINS_PX + ((int)RECT_OUTLINE_W_PX_FLT) - (KNOB_W / 2))
#define INLAY_Y                             TAPECTRL_FIXED_MARGINS
#define INLAY_SQUARE_SIZE                   160.0f

#define TAPECTRL_VFD_LAMP_SPACING 50.0f
#define LAMP_RECORD_X             255.0f //254 //235
#define LAMP_PLAY_X               (LAMP_RECORD_X + TAPECTRL_VFD_LAMP_SPACING)

#define SEVENSEG_BAR_TIP_GAP                2.0
#define SEVENSEG_BAR_WIDTH                  3.0
#define SEVENSEG_BAR_LEN                    25.0f
#define SEVENSEG_BAR_TILT_X                 7.0f
#define SEVENSEG_BAR_TOP_ROWS_TILT_FUDGE_X  1.0f /* x-offset for segs 1, 2, 4 */
#define SEVENSEG_SMALL_GAP                  12.0f
#define SEVENSEG_SEPARATOR_WIDTH            (SEVENSEG_BAR_WIDTH + SEVENSEG_SMALL_GAP)
#define SEVENSEG_DIGIT_WIDTH                (SEVENSEG_BAR_LEN   + SEVENSEG_SMALL_GAP)

#define TAPECTRL_KEY_REPEAT_ONSET_S         0.5f
#define TAPECTRL_KEY_REPEAT_PERIOD_S        0.05f

#define TAPECTRL_KEY_PRESS_SEEK_1200THS     6010 /* 5s */
#define TAPECTRL_KEY_HOLD_TICK_SEEK_1200THS 20000

#define TAPECTRL_LAMP_TOP_ROW_Y             (SEVENSEG_Y + 4.0f)
#define TAPECTRL_LAMP_ROW_H                 36.0f
#define TAPECTRL_LAMP_BULB_RADIUS           6.0f
#define TAPECTRL_LAMPS_X                    (TAPECTRL_W - 80.0f) /*(SEVENSEG_X + 240)*/
#define TAPECTRL_LAMP_LABELS_X              (TAPECTRL_LAMPS_X + TAPECTRL_LAMP_BULB_RADIUS + 10)

#define TAPECTRL_REC_X           360.0f
#define TAPECTRL_REC_Y           110.0f

#define TAPECTRL_BUTTON_W           60.0f
#define TAPECTRL_BUTTON_H           40.0f
#define TAPECTRL_REC_LBL_OFF_X   15.0f
#define TAPECTRL_REC_LBL_OFF_Y   12.0f

#include "tapectrl-labels.h"

#define COLOUR_BG                     8,8,30 //15,15,45 //24,24,24
#define COLOUR_LAMP_A                 255,255,0
#define COLOUR_LAMP_OUTLINE           96,96,0
#define COLOUR_LAMP_PLAY              0,210,0
#define COLOUR_LAMP_PLAY_OUTLINE      0,96,0
#define COLOUR_LAMP_REC               210,0,0
#define COLOUR_LAMP_REC_OUTLINE       96,0,0
#define COLOUR_LAMP_TAPENOISE         128,128,255
#define COLOUR_LAMP_TAPENOISE_OUTLINE 128,128,255
#define COLOUR_7SEG                   COLOUR_LAMP_A
#define COLOUR_VOLUME_TRACK           COLOUR_BUTTON_BACKGROUND //25,0,120
#define COLOUR_SEEKER_KNOB            0xff,0xef,0x90
#define COLOUR_ERROR                  255,0,0

#define COLOUR_RECT_OUTLINE 60,60,60 //200,200,200
#define COLOUR_INLAY_FILL   0,0,0

#define COLOUR_BLACK_BORDER 0,0,0

/* pink skin */
/*#define COLOUR_INTERVAL_SILENCE 0x1a,0x23,0x60*/  /* navy */
/*#define COLOUR_INTERVAL_LEADER  0xc0,0x50,0xc0*/  /* purple */
/*#define COLOUR_INTERVAL_DATA    0xff,0x90,0xff*/  /* pink */
/* LED skin */
/*#define COLOUR_INTERVAL_SILENCE 0,255,0*/
/*#define COLOUR_INTERVAL_LEADER  255,255,0*/
/*#define COLOUR_INTERVAL_DATA    255,0,0*/
/* fiery skin */
#define COLOUR_INTERVAL_SILENCE 64,0,0        /* scarlet */
#define COLOUR_INTERVAL_LEADER  0xb0,0x78,0x0 /* orange  */
#define COLOUR_INTERVAL_DATA    0xff,0xef,0   /* yellow  */

/* these must match WIDTH and HEIGHT in the mklabels.sh script and the value in mksource.c */
#define TAPECTRL_LABEL_WIDTH  72.0 /* was the -size parameter to imagemagick */
#define TAPECTRL_LABEL_HEIGHT 16.0 /* (can be safely reduced) */

#define COLOUR_BUTTON_FILL               190,190,205 //160,160,160
#define COLOUR_BUTTON_OUTLINE            150,150,150 //160,160,160
#define COLOUR_BUTTON_BACKGROUND         40,40,50
#define COLOUR_BUTTON_HIGHLIGHT          255,255,255
#define COLOUR_VFD_PANEL_BACKGROUND      0,0,0
#define COLOUR_MARKERS                   COLOUR_INTERVAL_DATA //255,128,255 /* TOHv4.4 */

#define TAPECTRL_ICON_REC_RADIUS               10


/* _BOX_SIZE is the square portion at the back. The "horn" width is half of this again. */
#define TAPECTRL_ICON_TAPENOISE_BOX_SIZE       12.0f
#define TAPECTRL_ICON_TAPENOISE_MARGIN_X       (-TAPECTRL_ICON_TAPENOISE_BOX_SIZE) //(-(TAPECTRL_ICON_TAPENOISE_BOX_SIZE * 3.0f) / 4.0f) //14


/*INLAY_SQUARE_RHS_SPACER*/ 
#define TAPECTRL_SPACER_1 22.0f

// left edge of load button and of 7seg panel
#define TAPECTRL_INLAY_SQUARE_RHS_X      (TAPECTRL_FIXED_MARGINS + INLAY_SQUARE_SIZE + TAPECTRL_SPACER_1)

#define TAPECTRL_VFD_PANEL_X             (TAPECTRL_INLAY_SQUARE_RHS_X)
#define TAPECTRL_VFD_PANEL_Y             (TAPECTRL_FIXED_MARGINS + TAPECTRL_BUTTON_H + TAPECTRL_SPACER_1)
#define TAPECTRL_VFD_PANEL_H             ((TAPECTRL_FIXED_MARGINS + INLAY_SQUARE_SIZE) - TAPECTRL_VFD_PANEL_Y)
#define TAPECTRL_VFD_PANEL_ICON_CENTRE_Y (TAPECTRL_VFD_PANEL_Y + (TAPECTRL_VFD_PANEL_H / 2))

#define TAPECTRL_VFD_PANEL_RHS_SPACER    30.0f

#define TAPECTRL_VOLUME_X                (TAPECTRL_W - (TAPECTRL_FIXED_MARGINS + 32.0f))
#define TAPECTRL_VOLUME_Y                (TAPECTRL_FIXED_MARGINS + TAPECTRL_SPACER_1)
#define TAPECTRL_VOLUME_LEN              ((TAPECTRL_SPACER_1 + TAPECTRL_BUTTON_H + TAPECTRL_VFD_PANEL_H) + 7.0f)

#define SEVENSEG_W  ((SEVENSEG_SEPARATOR_WIDTH*2) + (SEVENSEG_DIGIT_WIDTH*5))
#define SEVENSEG_X  ((TAPECTRL_VFD_PANEL_X + TAPECTRL_VFD_PANEL_W) - (TAPECTRL_VFD_PANEL_RHS_SPACER + SEVENSEG_W)) //220 //370
#define SEVENSEG_Y  (TAPECTRL_VFD_PANEL_Y + (TAPECTRL_VFD_PANEL_H / 2) - (SEVENSEG_BAR_LEN))

#define TAPECTRL_BOTTOM_RECTS_H            106.0f
#define TAPECTRL_TURBO_RECT_W              203.0f
#define TAPECTRL_300_BAUD_RECT_W           170.0f
#define TAPECTRL_BOTTOM_RECTS_SPACER       40.0f
#define COLOUR_DECAL                       192,192,192
#define TAPECTRL_DECAL_RECT_THICKNESS      1.0f
#define TAPECTRL_DECAL_RECT_ROUNDED_RADIUS 10.0f

#define TAPECTRL_BOTTOM_ROWS_UPPER_RHS_Y (SEEKER_Y + TAPECTRL_BOTTOM_RECTS_SPACER + 40.0f) // +30.0f
#define TAPECTRL_BOTTOM_ROWS_LOWER_RHS_Y (TAPECTRL_BOTTOM_ROWS_UPPER_RHS_Y        + 44.0f)
#define TAPECTRL_BOTTOM_ROWS_UPPER_LHS_Y (SEEKER_Y + TAPECTRL_BOTTOM_RECTS_SPACER + 30.0f)
#define TAPECTRL_BOTTOM_ROWS_LOWER_LHS_Y (TAPECTRL_BOTTOM_ROWS_UPPER_LHS_Y        + 48.0f)

#define TAPECTRL_LABEL_IX_TURBO    0
#define TAPECTRL_LABEL_IX_300_BAUD 1
#define TAPECTRL_LABEL_IX_SERIAL   2
#define TAPECTRL_LABEL_IX_TONE     3
#define TAPECTRL_LABEL_IX_DATA     4
#define TAPECTRL_LABEL_IX_DCD      5

static const char *tapectrl_labels[TAPECTRL_NUM_LABELS] = {
    TAPECTRL_LABEL_TURBO,
    TAPECTRL_LABEL_300_BAUD,
    TAPECTRL_LABEL_SERIAL,
    TAPECTRL_LABEL_TONE,
    TAPECTRL_LABEL_DATA,
    TAPECTRL_LABEL_DCD
};

#include "tape.h"

static float button_x_position (int const button_ix); /* TOHv4.4 */
static float button_y_position (int const button_ix); /* TOHv4.4 */

static void finish_labels (tape_ctrl_window_t * const tcw);

static void queue_to_gui_msg (tape_ctrl_window_t * const tcw,
                              const tape_ctrl_msg_to_gui_t * const msg) ;

static int guithread_rec_button_pressed (tape_ctrl_window_t * const tcw,
                                         tape_ctrl_msg_from_gui_t * const from_gui_out);
/* TOHv4.4 ... */
static int guithread_load_button_pressed (tape_ctrl_window_t * const tcw,
                                          tape_ctrl_msg_from_gui_t * const from_gui_out);
/* TOHv4.4 */
static int guithread_save_button_pressed (tape_ctrl_window_t * const tcw,
                                          tape_ctrl_msg_from_gui_t * const from_gui_out) ;
static int guithread_eject_button_pressed (tape_ctrl_window_t * const tcw,
                                          tape_ctrl_msg_from_gui_t * const from_gui_out);
static int guithread_rewind_button_pressed (tape_ctrl_window_t * const tcw,
                                          tape_ctrl_msg_from_gui_t * const from_gui_out);
//~ static int guithread_tapenoise_button_pressed (tape_ctrl_window_t * const tcw,
                                                //~ tape_ctrl_msg_from_gui_t * const from_gui_out);
static int guithread_catalogue_button_pressed (tape_ctrl_window_t * const tcw,
                                                tape_ctrl_msg_from_gui_t * const from_gui_out);
static int guithread_strip_button_pressed (      tape_ctrl_window_t * const tcw,
                                           tape_ctrl_msg_from_gui_t * const from_gui_out);
static int guithread_overclock_button_pressed (      tape_ctrl_window_t * const tcw,
                                               tape_ctrl_msg_from_gui_t * const from_gui_out);
static int guithread_rewind_button_pressed (tape_ctrl_window_t * const tcw,
                                            tape_ctrl_msg_from_gui_t * const from_gui_out);

static void *tape_ctrl_guithread_main (ALLEGRO_THREAD * const thread, void *arg);

static int guithread_update_time (int32_t const elapsed_1200ths,
                                  int32_t const duration_1200ths, /* TODO: not currently used */
                                  float const scale,
                                  float const margin_xin,
                                  float const margin_yin,
                                  int const reported_error);

static int draw_7seg (ALLEGRO_COLOR const c,
                      int const hrs,
                      int const mins,
                      int const secs,
                      float const scale,
                      float const margin_xin,
                      float const margin_yin,
                      bool const hours_and_separators);

static int tape_ctrl_mainthread_handle_messages_2 (tape_vars_t  * const tv,
                                                   tape_state_t * const ts,
                                                   ACIA * const acia);

static int guithread_main_paint (tape_ctrl_window_t * const tcw,
                                 int const black_bar_w,
                                 bool const letterbox,
                                 float const scale,
                                 bool const flash_on);

static int draw_digit (uint8_t const d,
                       float const x, /* x, y already include margin_x or margin_y */
                       float const y,
                       float scale,
                       ALLEGRO_COLOR const c,
                       bool const colon);
                       
static int seek_frac_from_mouse_xy (int const x,
                                    int const y,
                                    float const margin_x,
                                    float const margin_y,
                                    float const scale,
                                    float * const f_out);
                                    
// TOHv4.4
static int volume_frac_from_mouse_xy (  int   const x,
                                        int   const y,
                                      float   const margin_x,
                                      float   const margin_y,
                                      float   const scale,
                                      float * const f_out);
                                    
static int plot_label_to_bitmap (ALLEGRO_BITMAP * const ab, const char * const label);

static void finish_inlays (tape_ctrl_window_t * const tcw);

static int handle_mouse_button_down (int const mx,
                                     int const my,
                                     float const scale,
                                     float const margin_x,
                                     float const margin_y,
                                     bool const record_activated,
                                     uint32_t * const current_inlay_inout,
                                     bool * const seeker_held_out,
                                     bool * const volume_held_out,
                                     uint8_t buttons_pressed_out[TAPECTRL_NUM_BUTTONS]);

static int
guithread_init_intervals (tape_ctrl_window_t * const tcw,
                          /* This is the copy on the to_gui message.
                           * We will take it. */
                          tape_interval_list_t * const interval_list);

static int guithread_init_inlays (tape_ctrl_window_t * const tcw,
                                  uint32_t const num_scans,
                                  uef_inlay_scan_t * const scans);

static int guithread_paint_seeker_stripes (tape_ctrl_window_t * const tcw,
                                           double margin_x,
                                           double margin_y,
                                           float scale);

/* TOHv4.3-a3 */
static int32_t guithread_duration_from_intervals (const tape_interval_list_t * const iv_list);




#define SEEKER_KNOB_NUM_VERTICES 5
/*
        1 5
        2 4
         3
*/
static const float seeker_knob_vertices[2 * SEEKER_KNOB_NUM_VERTICES] = {
    -(KNOB_W/2.0f), -KNOB_H,
    -(KNOB_W/2.0f), -(KNOB_H*0.3f),
    0.0f, 0.0f,
    (KNOB_W/2.0f), -(KNOB_H*0.3f),
    (KNOB_W/2.0f), -KNOB_H
};

extern int shutdown_exit_code;
/* this is the top-level mainthread-polled function; it is responsible
   for executing the outcomes of events arriving from the GUI */
int tapectrl_handle_messages (tape_vars_t * const tv, tape_state_t * const ts, ACIA * const acia) { /* not const */

    int e;
    e = TAPE_E_OK;

    /* if the window has already gone away, we lock the
     * mutex and recover the final error code from tapectrl. */

    /* remember, tapectrl_opened is only changed by the main thread, so
     * there isn't a race here. If it's false then that means the main thread
     * hasn't tried to start the gui thread (or the window has closed again). */
    if (/*ts->disabled_due_to_error ||*/ ! tv->tapectrl_opened ) {
        e = tv->tapectrl.tapectrl_error;
    } else {
        /* otherwise call this like normal (locks mutex, checks display != NULL) */
        e = tape_ctrl_mainthread_handle_messages_2(tv, ts, acia);
    }
    if (TAPE_E_OK != ts->prior_exception) {
        return TAPE_E_OK;
    } /* TOHv4.3-a3 */
    /* exceptions for the tapectrl window are processed here */
    /* printf("tapectrl main thread: expect error from tcw to arrive here: e = %d\n", e); */
    tape_handle_exception (ts,
                           tv,
                           e,
                           tv->testing_mode & TAPE_TEST_QUIT_ON_EOF,
                           tv->testing_mode & TAPE_TEST_QUIT_ON_ERR,
                           true); /* alter menus */

    return TAPE_E_OK;

}


static int seek_frac_from_mouse_xy (int const x,
                                    int const y,
                                    float const margin_x,
                                    float const margin_y,
                                    float const scale,
                                    float * const f_out) {
    float seeker_w;
    seeker_w = scale * (float) (TAPECTRL_W - (2*SEEKER_MARGINS_PX));
    if ((y<0) || IN_SEEKER_SCRUBZONE_Y(y,margin_y,scale)) {
        *f_out = (x - (margin_x + (scale * SEEKER_MARGINS_PX))) / seeker_w;
        if (*f_out > 1.0f) { *f_out = 1.0f; }
        if (*f_out < 0.0f) { *f_out = 0.0f; }
        return TAPE_E_OK;
    }
    return TAPE_E_TAPECTRL_OUTSIDE_ZONE;
}



/* TOHv4.4 */
/* pass x<0 in order to ignore x and only use the y-coord;
   also legitimises any value of y if x<0 */
static int volume_frac_from_mouse_xy (  int   const x,
                                        int   const y,
                                      float   const margin_x,
                                      float   const margin_y,
                                      float   const scale,
                                      float * const f_out) {
                                      
    float volume_len;
    float fy;
    volume_len = scale * TAPECTRL_VOLUME_LEN; // - (2.0f*SEEKER_MARGINS_PX));
    
    *f_out = NAN;
#define VOLUME 15.0f
    if ( (x<0) || (    IN_VOLUME_SCRUBZONE_X(x,margin_x,scale)
                    && (y > (margin_y + scale*(TAPECTRL_VOLUME_Y - VOLUME_CLICKZONE_Y))  )
                    && (y < (margin_y + (scale*(TAPECTRL_VOLUME_Y + TAPECTRL_VOLUME_LEN + VOLUME_CLICKZONE_Y))))
                  )) {
         
        fy = (((float) y) - (margin_y + scale*TAPECTRL_VOLUME_Y));
         
        *f_out = 1.0f - (fy / volume_len);
        
        if (*f_out > 1.0f) { *f_out = 1.0f; }
        if (*f_out < 0.0f) { *f_out = 0.0f; }
        
        return TAPE_E_OK;
        
    }
    
    return TAPE_E_TAPECTRL_OUTSIDE_ZONE;
    
}

#include "gui-allegro.h"
#include "tapeseek.h"
#include "taperead.h"
#include "tapenoise.h"       // for sound_tape flag
#include "tapecat-allegro.h" // for gui_tapecat_start()

/* this is called by the MAIN THREAD to process messages from the tape control window
 * called from main_timer() in main.c
 * This function is NOT called at all while tape_vars.tapectrl_opened is false.
 * This is a main thread-only variable. */
static int tape_ctrl_mainthread_handle_messages_2 (tape_vars_t  * const tv,
                                                   tape_state_t * const ts,
                                                   ACIA * const acia) { /* modified, not const */

    int32_t elapsed_1200ths_actual;
    int32_t duration_1200ths;
    int e;
    tape_ctrl_window_t *tcw;
    double t;
    bool key_held_seek;
    bool my_eof;
    int32_t div;
    int msg_i, msg_n;
    tape_ctrl_msg_from_gui_t from_gui_copy[TAPECTRL_MSG_QUEUE_SIZE];

    tcw = &(tv->tapectrl);

    e = TAPE_E_OK;
    key_held_seek = false;
    
    my_eof = tape_peek_eof(ts); /* TOHv4.4 */

    TAPECTRL_LOCK_MUTEX(tcw->mutex);

    /* confirm this */
    if (NULL == tv->tapectrl.display) {
        TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
        return TAPE_E_OK;
    }

    /* handle error from the tapectrl thread */
    if (tcw->tapectrl_error != TAPE_E_OK) {
        e = tcw->tapectrl_error;
        tcw->tapectrl_error = TAPE_E_OK;
        /* echo it back to the tapectrl thread */
        tapectrl_to_gui_msg_error  (tcw, false, false, e); /* TOHv4.3-a3 */
        TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
        return e;
    }

    /* decide whether we are interested in the RX bits or the TX bits */
    div = acia_get_divider_from_ctrl_reg_bits(3 & acia->control_reg);

    tapectrl_to_gui_msg_baud (tcw, false, false, (64==div) && ts->ula_motor); /* send msg to GUI */

    /* with the mutex locked, copy the messages */
    memcpy(from_gui_copy,
           (const void *) tcw->from_gui,
           sizeof(tape_ctrl_msg_from_gui_t) * tcw->from_gui_fill);
    msg_n = tcw->from_gui_fill;
    tcw->from_gui_fill = 0; /* empty the queue */

    TAPECTRL_UNLOCK_MUTEX(tcw->mutex);

    t = al_get_time();

    duration_1200ths = 0;
    /* if ( ! ts->disabled_due_to_error ) { */
    if ( TAPE_E_OK == ts->prior_exception ) { /* duration forced to 0 if tape is disabled */
        e = tape_get_duration_1200ths (ts, &duration_1200ths);
        if (TAPE_E_OK != e) { return e; }
    }
    
    /* now actually process the messages from the copy */
    for (msg_i=0; msg_i < msg_n; msg_i++) {
    
        tape_ctrl_msg_from_gui_t *from_gui;
        int32_t tallied;
        const char *extension;
        bool as_uef, as_csw, as_tibet;
        
        as_uef   = false;
        as_csw   = false;
        as_tibet = false;

        from_gui = from_gui_copy + msg_i;
        
        if ( ! from_gui->ready ) {
            log_warn("tapectrl: BUG: from_gui msg from queue does not have ready=true!");
            return TAPE_E_BUG;
        }

        /* FIXME: inhibited_by_gui is global when there should be three independent types
         *        for mouse, leftarrow, rightarrow -- otherwise can use combination of these
         *        controls to desync the state */
         
        if (TAPECTRL_FROM_GUI_THREAD_STARTED == from_gui->type) {
            /* GUI thread has started up -- need to send any
             * lazy init-time messages across from here,
             * e.g. current motor status */
            /* note that this still should happen even if the tape is disabled due to error */
            tallied = tv->record_activated ? duration_1200ths : ts->tallied_1200ths;

            /* send about twenty messages to the tapectrl thread,
             * under a single mutex lock */
            tapectrl_to_gui_msg_motor         (tcw,  true, false, ts->ula_motor);
            tapectrl_set_gui_rapid_value_time (tcw, false, false, tallied);
            tapectrl_to_gui_msg_eof           (tcw, false, false, my_eof); /* send msg to tapectrl GUI */
            tapectrl_to_gui_msg_error         (tcw, false, false, ts->prior_exception); /* TOHv4.3-a3 */
            tapectrl_to_gui_msg_record        (tcw, false, false, tv->record_activated);
            tapectrl_to_gui_msg_tapenoise     (tcw, false, false, sound_tape_volume_fraction); /* TOHv4.4 */
            tapectrl_to_gui_msg_overclock     (tcw, false, false, tv->overclock); /* TOHv4.4 */
            tapectrl_to_gui_msg_strip         (tcw, false, false, tv->strip_silence_and_leader); /* TOHv4.4 */
            e = tapectrl_to_gui_msg_stripes   (tcw, false, false, &(tv->interval_list));
            if (TAPE_E_OK == e) {
                e = tapectrl_to_gui_msg_inlays_2 (tcw,
                                                  false,
                                                  false, /* unlock */
                                                  ts->uef.globals.num_inlay_scans,
                                                  ts->uef.globals.inlay_scans);
            }
            TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
        } else if (TAPECTRL_FROM_GUI_LOAD_TAPE == from_gui->type) {
            /* using TCW's display rather than the main one
             * ensures that load dialogue appears
             * on top of TCW rather than behind it */
            gui_load_tape(tcw->display); //al_get_current_display());
        } else if (TAPECTRL_FROM_GUI_SAVE_TAPE == from_gui->type) {
            
            extension = ".uef"; /* the default; will be used if _BITS_NONE or _BITS_UEF */
            
            if (TAPE_FILETYPES_ALL != ts->filetype_bits) { // if _ALL, default to UEF
                if (ts->filetype_bits & TAPE_FILETYPE_BITS_CSW) {
                  extension = ".csw";
                  as_csw = true;
                } else if (ts->filetype_bits & TAPE_FILETYPE_BITS_TIBET) {
                  extension = ".tibetz";
                  as_tibet = true;
                }
            }
            return gui_save_tape (tcw->display,
                                  extension,
                                  // cannot specify file type if saving from console (at least not yet)
                                  // just use whatever type is currently loaded, or UEF by default
                                  as_uef,
                                  as_csw,
                                  as_tibet,
                                  false, // cannot save as WAV from the console, have to use menus
                                  true); // again, cannot choose this, so force compression on
        } else if (TAPECTRL_FROM_GUI_EJECT_TAPE == from_gui->type) {
            tape_ejected_by_user(ts, tv, acia);
        } else if (TAPECTRL_FROM_GUI_REWIND_TAPE == from_gui->type) {
            tape_rewind_2(ts, 
                          tcw,
                          tv->record_activated,
                          true);
        } else if (TAPECTRL_FROM_GUI_CATALOGUE == from_gui->type) {
            gui_tapecat_start();
        } else if  (    (TAPECTRL_FROM_GUI_SEEK_AND_SET_REC == from_gui->type)
                     /* record activation needs to work even if no tapetime exists yet */
                     && (TAPE_E_OK == ts->prior_exception) ) { /* TOHv4.3-a3: disabled gate */
            gui_set_record_mode (from_gui->data.seek.record_activated);
            e = tape_set_record_activated (ts,
                                           tv,
                                           acia,
                                           from_gui->data.seek.record_activated,
                                           tv->tapectrl_opened);
            if (TAPE_E_OK != e) { break; }
        } else if (TAPECTRL_FROM_GUI_TOGGLE_OVERCLOCK == from_gui->type) {
            tape_toggle_turbo_overclock();
        } else if (TAPECTRL_FROM_GUI_TOGGLE_STRIP     == from_gui->type) {
            tape_toggle_turbo_strip();
        } else if (TAPECTRL_FROM_GUI_TAPENOISE_VOLUME == from_gui->type) {
            tapenoise_set_volume(tcw, from_gui->data.volume.fraction, true);
        }

        if (duration_1200ths > 0) {
            if (    (TAPECTRL_FROM_GUI_SEEK == from_gui->type)
                 || (TAPECTRL_FROM_GUI_SEEK_AND_SET_REC == from_gui->type)) {
                /* keep a copy of current seek position on the mainthread side */
                tcw->seeking_last_position_1200ths = duration_1200ths * from_gui->data.seek.fraction;
                e = tape_seek_absolute (ts,
                                        tcw->seeking_last_position_1200ths,
                                        duration_1200ths,
                                        &elapsed_1200ths_actual,
                                        &my_eof,
                                        &(tv->desync_message_printed));

                if ( ! tcw->inhibited_by_gui ) { /* latch this */
                    tcw->inhibited_by_gui =    from_gui->data.seek.held
                                            || from_gui->data.seek.left_held
                                            || from_gui->data.seek.right_held;
                }

                if (TAPE_E_OK == e) {
                    tapectrl_set_gui_rapid_value_time(tcw, true, false, elapsed_1200ths_actual); /*, duration_1200ths); */
                    tapectrl_to_gui_msg_eof(tcw, false, false, my_eof); /* send msg to tapectrl GUI */
                }
                /* do this manually */
                TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
                if (TAPE_E_OK != e) { return e; }

                tv->previous_eof_value = my_eof;
                ts->tallied_1200ths = elapsed_1200ths_actual;

                if (from_gui->data.seek.left_held) {
                    tcw->seeking_left_held = true;
                    tcw->seeking_autorepeat_start_time = t;
                }
                if (from_gui->data.seek.right_held) {
                    tcw->seeking_right_held = true;
                    tcw->seeking_autorepeat_start_time = t;
                }
                
            } else if (    (TAPECTRL_FROM_GUI_SEEK_RELEASED  == from_gui->type)) {
                tcw->inhibited_by_gui = false;
            } else if (TAPECTRL_FROM_GUI_LEFT_RELEASED  == from_gui->type) {
                tcw->inhibited_by_gui = false;
                tcw->seeking_left_held = false;
            } else if (TAPECTRL_FROM_GUI_RIGHT_RELEASED == from_gui->type) {
                tcw->inhibited_by_gui = false;
                tcw->seeking_right_held = false;
            }
            if (TAPE_E_OK != e) { return e; }
        /* endif (duration_1200ths > 0) */
        }
        
    } /* next msg_i */
    
    if (duration_1200ths > 0) { /* again ! */

        if (tcw->seeking_left_held) {
            t = al_get_time();
            if (t > (tcw->seeking_autorepeat_start_time + TAPECTRL_KEY_REPEAT_ONSET_S)) {
                tcw->seeking_last_position_1200ths -= TAPECTRL_KEY_HOLD_TICK_SEEK_1200THS;
                key_held_seek = true;
            }
        } else if (tcw->seeking_right_held) {
            t = al_get_time();
            if (t > (tcw->seeking_autorepeat_start_time + TAPECTRL_KEY_REPEAT_ONSET_S)) {
                tcw->seeking_last_position_1200ths += TAPECTRL_KEY_HOLD_TICK_SEEK_1200THS;
                key_held_seek = true;
            }
        }
    }
    
    if (key_held_seek && (TAPE_E_OK == ts->prior_exception)) {

        if (tcw->seeking_last_position_1200ths >= duration_1200ths) {
            tcw->seeking_last_position_1200ths = duration_1200ths - TAPE_SEEK_CLAMP_BACK_OFF_1200THS;
        }
        if (tcw->seeking_last_position_1200ths < 0) {
            tcw->seeking_last_position_1200ths = 0;
        }

        e = tape_seek_absolute (ts,
                                tcw->seeking_last_position_1200ths,
                                duration_1200ths,
                                &elapsed_1200ths_actual,
                                &my_eof,
                                &(tv->desync_message_printed));
        if (TAPE_E_OK != e) { return e; }

        ts->tallied_1200ths = elapsed_1200ths_actual;

        /* complete the loop; echo an acknowledgement back to the tapectrl
         * thread, and get it to set the seeker position */
        tapectrl_set_gui_rapid_value_time(tcw, true, false, elapsed_1200ths_actual); /* call locks mutex */
        if (my_eof != tv->previous_eof_value) {
            /* maybe update EOF state too */
            tapectrl_to_gui_msg_eof(tcw, false, false, my_eof); /* send msg to tapectrl GUI */
        }
        tv->previous_eof_value = my_eof;
        /* do this manually */
        TAPECTRL_UNLOCK_MUTEX(tcw->mutex);

    } /* endif (key_held_seek) */
    
    return e;
    
}


// main thread
void tapectrl_finish (tape_ctrl_window_t * const tcw, bool * const tapectrl_opened_inout) {
    tapectrl_close (tcw, tapectrl_opened_inout);
    al_destroy_mutex(tcw->mutex);
    memset(tcw, 0, sizeof(tape_ctrl_window_t));
}

// main thread
void tapectrl_close (tape_ctrl_window_t * const tcw, bool * const tapectrl_opened_inout) {

    ALLEGRO_MUTEX *m;
    int ret, *ret_p;
    bool join;

    join = false;

    TAPECTRL_LOCK_MUTEX(tcw->mutex);
    join = (NULL != tcw->thread);
    tcw->shut_tapectrl_down = true; /* instruct tapectrl thread to shut down */
    TAPECTRL_UNLOCK_MUTEX(tcw->mutex);

    if (join) {
        ret_p = &ret;
        al_join_thread(tcw->thread, (void **) &ret_p);
    }

    TAPECTRL_LOCK_MUTEX(tcw->mutex);

    /* We don't destroy the mutex. Back up its pointer. */
    m = tcw->mutex;

    if (tcw->thread != NULL) {
        al_destroy_thread(tcw->thread);
    }
    tcw->thread = NULL;

    /* destroy bitmaps */
    finish_labels(tcw);
    finish_inlays(tcw);
    tape_interval_list_finish(&(tcw->interval_list)); /* the TCW copy of the intervals, not the one on tape_vars_t */

    memset(tcw, 0, sizeof(tape_ctrl_window_t));

    *tapectrl_opened_inout = false;

    tcw->mutex = m; /* Restore mutex. */

    TAPECTRL_UNLOCK_MUTEX(tcw->mutex);

}

static void finish_inlays (tape_ctrl_window_t * const tcw) {
    uint32_t i;
    if (NULL == tcw->inlays) { return; }
    for (i=0; i < tcw->num_inlays; i++) {
        if (NULL != tcw->inlays[i]) {
            al_destroy_bitmap(tcw->inlays[i]);
            tcw->inlays[i] = NULL;
        }
    }
    free(tcw->inlays);
    tcw->inlays = NULL;
    tcw->num_inlays = 0;
    tcw->current_inlay = 0;
}

static void finish_labels (tape_ctrl_window_t * const tcw) {
    int i;
    for (i=0; i < TAPECTRL_NUM_LABELS; i++) {
        if (NULL != tcw->labels[i]) {
            al_destroy_bitmap(tcw->labels[i]);
            tcw->labels[i] = NULL;
        }
    }
}

static void recompute_margins(tape_ctrl_window_t * const tcw, int const disp_w, int const disp_h) {
    float aspect_ratio, wanted_ratio;
    aspect_ratio =     ((float)disp_w) / (float) disp_h;
    wanted_ratio = ((float)TAPECTRL_W) / (float) TAPECTRL_H;
    /* compute margins */
    if (aspect_ratio > wanted_ratio) {
        /* window is wide */
        tcw->scale = disp_h / (float) TAPECTRL_H;
        tcw->margin_x = 0.5f * (disp_w - (TAPECTRL_W * tcw->scale));
        tcw->margin_y = 0.0f;
    } else {
        /* window is tall */
        tcw->scale = disp_w / (float) TAPECTRL_W;
        tcw->margin_x = 0.0f;
        tcw->margin_y = 0.5f * (disp_h - (TAPECTRL_H * tcw->scale));
    }
}


static int32_t guithread_duration_from_intervals (const tape_interval_list_t * const iv_list) {
    tape_interval_t *ivl;
    if ((NULL==iv_list) || (NULL==iv_list->list) || (iv_list->fill<1)) {
        return 0;
    }
    ivl = iv_list->list + iv_list->fill - 1;
    return ivl->start_1200ths + ivl->pos_1200ths;
}


static void *tape_ctrl_guithread_main (ALLEGRO_THREAD * const thread, void *arg) {

    tape_vars_t *tv;  /* NO tape_state_t; communication must occur through tape_vars */
    tape_ctrl_window_t *tcw;
    ALLEGRO_EVENT_QUEUE *eq;
    ALLEGRO_EVENT ev;
    bool quit, just_started;
    int mx, my; /* mouse */
    int e;
    ALLEGRO_DISPLAY *tmpdisp;
    bool seeker_held, volume_held;
    int flagz;
    bool can_resize;
    int win_w, win_h;
    double now_s;

    memset(&ev, 0, sizeof(ALLEGRO_EVENT)); /* TOHv4.3-a1 */

    tv = (tape_vars_t *) arg;
    tcw = &(tv->tapectrl);

    eq = NULL;
    e = TAPE_E_OK;

    if (NULL == tcw->mutex) {
        log_warn("tapectrl: BUG: mutex is NULL");
        /* testing with valgrind revealed that
         * memory was being leaked in this case,
         * so go ahead and try to destroy these now */
        tape_interval_list_finish(&(tcw->interval_list));
        finish_inlays(tcw);
        finish_labels(tcw);
        return NULL;
    }

    can_resize  = tcw->can_resize;

    al_set_new_window_title("B-Em Tape Control");

    TAPECTRL_LOCK_MUTEX(tcw->mutex);

    /* window already open? */
    if ((NULL != tcw->display) || (tcw->shut_tapectrl_down)) {
        TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
        return NULL;
    }

    flagz = ALLEGRO_WINDOWED | (can_resize ? ALLEGRO_RESIZABLE : 0);
    al_set_new_display_flags (flagz);
    win_w = (int)(tcw->scale * TAPECTRL_W);
    win_h = (int)(tcw->scale * TAPECTRL_H);
    tmpdisp = al_create_display (win_w, win_h);
    if (NULL == tmpdisp) {
        log_warn("tapectrl: cannot create tape control display!");
        TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
        return NULL;
    }

    /* OK. Commit to it */
    tcw->display = tmpdisp;

    TAPECTRL_UNLOCK_MUTEX(tcw->mutex);

    eq = al_create_event_queue();
    al_register_event_source(eq, al_get_display_event_source((ALLEGRO_DISPLAY *) tcw->display));
    al_register_event_source(eq, al_get_keyboard_event_source());
    al_register_event_source(eq, al_get_mouse_event_source());

    recompute_margins(tcw, win_w, win_h);

    quit = 0;
    mx = 0;
    my = 0;
    seeker_held = false;
    volume_held = false;
    just_started = true;
    
    tv->flash_last_toggled_time_s = al_get_time();

    /* Let's rap about errors. Note that there are two error codes in play.
     * There is the normal error code 'e' here, which handles errors in
     * this loop related to the display of the tapectrl window. The
     * assumption is that tapectrl errors are fatal to the tapectrl window,
     * so the window should shut down if they occur.
     *
     * Meanwhile there is a second source of error, tcw->reported_error,
     * which is used for reporting errors from the main tape system. This
     * is the error type that is displayed on the 7-segment in red.
     *
     * The point here is that if an error occurs in the following loop,
     * you have an option -- report it on the 7-seg by setting
     * tcw->reported_error, or shut down the tapectrl by setting 'e' and
     * then optionally breaking out of the loop.
     *
     * tcw->reported_error is only accessed from the tapectrl thread,
     * so you can set it in here. */

    while ( ! quit && ! al_get_thread_should_stop (thread) && (TAPE_E_OK == e) ) {
    
        tape_ctrl_msg_to_gui_t       to_gui_copy[TAPECTRL_MSG_QUEUE_SIZE];
        tape_ctrl_msg_from_gui_t     from_gui;
        tape_ctrl_gui_rapid_values_t rapid_vals;
        float f;
        int msg_n,msg_i;
        int32_t dur; /* TOHv4.3-a3 */
        uint8_t buttons_pressed[TAPECTRL_NUM_BUTTONS];

        memset(&(from_gui.data.seek), 0, sizeof(from_gui.data.seek));

        from_gui.ready = false;

        if (just_started) {
            /* request initial state from main thread */
            from_gui.type  = TAPECTRL_FROM_GUI_THREAD_STARTED;
            from_gui.ready = true;
        }

        /* 1. PREPARE OUTGOING EVENTS (from_gui) */
        /* e.g. guithread_main_prepare_msgs_from_gui() */

        while ( ! quit && ! al_is_event_queue_empty (eq) && ! just_started ) {

            int kc;

            al_get_next_event(eq, &ev);
            
            if (ALLEGRO_EVENT_MOUSE_AXES == ev.type) { continue; } /* ignore */

            dur = guithread_duration_from_intervals(&(tcw->interval_list));

            if (ALLEGRO_EVENT_KEY_DOWN == ev.type) {
                kc = ev.keyboard.keycode;
                if ((ALLEGRO_KEY_LEFT == kc) && (ev.keyboard.display == tcw->display)) {
                    tcw->time_key_pressed = al_get_time();
                    tcw->elapsed_1200ths -= TAPECTRL_KEY_PRESS_SEEK_1200THS;
                    if (tcw->elapsed_1200ths < 0) { tcw->elapsed_1200ths = 0; }
                    from_gui.type = TAPECTRL_FROM_GUI_SEEK;
                    if (0==dur) {
                        from_gui.data.seek.fraction = 0.0f;
                    } else {
                        from_gui.data.seek.fraction = tcw->elapsed_1200ths / (double) dur;
                    }
                    from_gui.data.seek.left_held = true;
                    from_gui.ready = true;
                } else if ((ALLEGRO_KEY_RIGHT == kc) && (ev.keyboard.display == tcw->display)) {
                    tcw->time_key_pressed = al_get_time();
                    tcw->elapsed_1200ths += TAPECTRL_KEY_PRESS_SEEK_1200THS;
                    if (tcw->elapsed_1200ths >= dur) { tcw->elapsed_1200ths = dur; }
                    from_gui.type = TAPECTRL_FROM_GUI_SEEK;
                    if (0==dur) {
                        from_gui.data.seek.fraction = 0.0f;
                    } else {
                        from_gui.data.seek.fraction = tcw->elapsed_1200ths / (double) dur;
                    }
                    from_gui.data.seek.right_held = true;
                    from_gui.ready = true;
                }
            } else if (ALLEGRO_EVENT_KEY_UP == ev.type) {
                kc = ev.keyboard.keycode;
                if ((ALLEGRO_KEY_LEFT == kc) && (ev.keyboard.display == tcw->display)) {
                    from_gui.type = TAPECTRL_FROM_GUI_LEFT_RELEASED;
                    from_gui.data.seek.left_held = false;
                    from_gui.ready = true;
                } else if ((ALLEGRO_KEY_RIGHT == kc) && (ev.keyboard.display == tcw->display)) {
                    from_gui.type = TAPECTRL_FROM_GUI_RIGHT_RELEASED;
                    from_gui.data.seek.right_held = false;
                    from_gui.ready = true;
                }
            } else if (ALLEGRO_EVENT_DISPLAY_CLOSE == ev.type) {
                quit = true;
            } else if (ALLEGRO_EVENT_MOUSE_AXES == ev.type) {
                mx = ev.mouse.x;
                my = ev.mouse.y;
            } else if ((ALLEGRO_EVENT_MOUSE_BUTTON_DOWN == ev.type) && (ev.mouse.display == tcw->display)) {
                mx = ev.mouse.x;
                my = ev.mouse.y;
                memset(buttons_pressed, 0, TAPECTRL_NUM_BUTTONS);
                e = handle_mouse_button_down (mx,
                                              my,
                                              tcw->scale,
                                              tcw->margin_x,
                                              tcw->margin_y,
                                              tcw->record_activated,
                                              /* sets these variables: */
                                              &(tcw->current_inlay),
                                              &seeker_held,
                                              &volume_held,
                                              buttons_pressed);
                if (TAPE_E_OK != e) {
                    quit = true;
                    break;
                }

                if (buttons_pressed[TAPECTRL_BUTTON_IX_LOAD]) {
                    e = guithread_load_button_pressed(tcw, &from_gui);
                } else if (buttons_pressed[TAPECTRL_BUTTON_IX_SAVE]) {
                    e = guithread_save_button_pressed(tcw, &from_gui);
                } else if (buttons_pressed[TAPECTRL_BUTTON_IX_EJECT]) {
                    e = guithread_eject_button_pressed(tcw, &from_gui);
                } else if (buttons_pressed[TAPECTRL_BUTTON_IX_CATALOGUE]) {
                    e = guithread_catalogue_button_pressed(tcw, &from_gui);
                } else if (buttons_pressed[TAPECTRL_BUTTON_IX_RECORD]) {
                    e = guithread_rec_button_pressed(tcw, &from_gui);
                } else if (buttons_pressed[TAPECTRL_BUTTON_IX_REWIND]) {
                    e = guithread_rewind_button_pressed(tcw, &from_gui);
                } else if (buttons_pressed[TAPECTRL_BUTTON_IX_STRIP]) {
                    e = guithread_strip_button_pressed(tcw, &from_gui);
                } else if (buttons_pressed[TAPECTRL_BUTTON_IX_OVERCLOCK]) {
                    e = guithread_overclock_button_pressed(tcw, &from_gui);
                }
                
            } else if ((ALLEGRO_EVENT_MOUSE_BUTTON_UP == ev.type) && (ev.mouse.display == tcw->display)) {
                from_gui.type = TAPECTRL_FROM_GUI_SEEK_RELEASED;
                from_gui.ready = true;
                seeker_held = false;
                volume_held = false;
            } else if ((ALLEGRO_EVENT_MOUSE_ENTER_DISPLAY == ev.type) && (ev.mouse.display == tcw->display )) {

            } else if ((ALLEGRO_EVENT_MOUSE_LEAVE_DISPLAY == ev.type) && (ev.mouse.display == tcw->display )) {

            } else if (     (ALLEGRO_EVENT_DISPLAY_RESIZE == ev.type) && (ev.display.source == tcw->display)) {
                recompute_margins(tcw, ev.display.width, ev.display.height);
                al_acknowledge_resize((ALLEGRO_DISPLAY *) tcw->display);
            /* TOHv4.4: work around Allegro macOS resize crash: */
            } else if (ALLEGRO_EVENT_DISPLAY_HALT_DRAWING == ev.type) {
                 if (ev.display.source == tcw->display) {
                    tcw->halt_drawing = true;
                 }
                 al_acknowledge_drawing_halt(ev.display.source);
            } else if (ALLEGRO_EVENT_DISPLAY_RESUME_DRAWING == ev.type) {
                 if (ev.display.source == tcw->display) {
                    tcw->halt_drawing = false;
                 }
                 al_acknowledge_drawing_resume(ev.display.source);
            }
            
        }
        
        if (quit) { break; }
        
        from_gui.timestamp = ev.any.timestamp;
        
        just_started = false;

        /* 2. SEND AND RECEIVE EVENTS */
        
        if (NULL == tcw->mutex) {
            log_warn("tapectrl thread: BUG: tapectrl mutex is suddenly NULL");
            return NULL;
        }

        TAPECTRL_LOCK_MUTEX (tcw->mutex);

        /* check this again, while we have the mutex locked */
        if (tcw->shut_tapectrl_down) {
            TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
            quit = 1;
            break;
        }
        
        /* send message(s) to main thread */
        if (from_gui.ready) {
            tapectrl_queue_from_gui_msg (tcw, &from_gui);
        }

        /* with the mutex locked, copy the messages, and the rapid-values */
        memcpy(to_gui_copy,
               (const void *) tcw->to_gui,
               sizeof(tape_ctrl_msg_to_gui_t) * tcw->to_gui_fill);
        msg_n = tcw->to_gui_fill;
        tcw->to_gui_fill = 0; /* empty the queue */

        rapid_vals = tcw->gui_rapid_values;
        tcw->gui_rapid_values.time_ready = false;
        tcw->gui_rapid_values.tone_ready = false;
        
        TAPECTRL_UNLOCK_MUTEX (tcw->mutex);

        /* 3A. HANDLE INCOMING EVENTS (to_gui)
         *     Working from a copy, so can now proceed without locks. */

        for (msg_i = 0; (TAPE_E_OK==e) && (msg_i < msg_n); msg_i++) {
            tape_ctrl_msg_to_gui_t *msg;
            msg = to_gui_copy + msg_i;
            switch (msg->type) {
                case TAPECTRL_TO_GUI_EOF:
                    tcw->end_of_tape = msg->eof;
                    break;
                case TAPECTRL_TO_GUI_ERROR: /* TOHv4.3-a3 */
                    tcw->reported_error = msg->error;
                    break;
                case TAPECTRL_TO_GUI_MOTOR:
                    tcw->motor = msg->motor;
                    break;
                case TAPECTRL_TO_GUI_RECORD:
                    tcw->record_activated = msg->rec;
                    break;
                case TAPECTRL_TO_GUI_BAUD:
                    tcw->baud300 = msg->baud300;
                    break;
                case TAPECTRL_TO_GUI_DCD:
                    tcw->dcd = true; //msg->dcd;
                    tcw->dcd_on_start_time = al_get_time();
                    break;
                case TAPECTRL_TO_GUI_INLAYS:
                    /* make errors in inlay scan rendering fatal to tapectrl?
                     * Could arguably just set reported_error instead for a red light */
                    e = guithread_init_inlays (tcw, msg->inlays.fill, msg->inlays.scans);
                    msg->inlays.fill = 0;
                    msg->inlays.scans = NULL;
                    break;
                case TAPECTRL_TO_GUI_STRIPES:
                    /* guithread_init_intervals() steals the provided intervals list
                     * which was malloced in the main thread ... */
                    e = guithread_init_intervals(tcw, &(msg->stripes));
                    /* again, treat these errors as fatal to tapectrl */
                    break;
                case TAPECTRL_TO_GUI_TAPENOISE: /* TOHv4.4 */
                    tcw->tapenoise_volume = msg->volume;
                    break;
                case TAPECTRL_TO_GUI_STRIP:
                    tcw->strip_silence_and_leader = msg->strip;
                    break;
                case TAPECTRL_TO_GUI_OVERCLOCK:
                    tcw->overclock = msg->overclock;
                    break;
                default:
                    break;

            } /* end switch (msg type) */
        } /* next msg, from to_gui queue */

        if (TAPE_E_OK != e) { break; }

        /* 3B. RAPID VALUES */

        if (rapid_vals.time_ready) {
            tcw->elapsed_1200ths = rapid_vals.elapsed_1200ths;
        }
        if (rapid_vals.tone_ready) {
            tcw->have_signal = (rapid_vals.tone != 'S');
            tcw->have_data   = (tcw->have_signal && (rapid_vals.tone != 'L'));
            msg_n++;
        }

        /* Hack: Clear the DCD indicator after a brief pulse. */
        if (tcw->dcd && (al_get_time() > (tcw->dcd_on_start_time + 0.06f))) {
            tcw->dcd = false;
            msg_n++;
        }

        /* 4. HANDLE 'HELD' CONDITIONS */

        /* Mouse button was pressed or is currently held */

        dur = guithread_duration_from_intervals(&(tcw->interval_list));

        if (seeker_held) {

            mx = ev.mouse.x;
            my = ev.mouse.y;
            f = NAN;
            e = seek_frac_from_mouse_xy (mx, -1, tcw->margin_x, tcw->margin_y, tcw->scale, &f);
            if (TAPE_E_TAPECTRL_OUTSIDE_ZONE == e) { /* trap this */
                e = TAPE_E_OK;
            } else if (TAPE_E_OK != e) {
                /* break; */
            }
            if (TAPE_E_OK == e) {
                from_gui.type = TAPECTRL_FROM_GUI_SEEK;
                from_gui.data.seek.fraction = f;
                from_gui.ready = true;
                /* send another, new, separate event to the main thread */
                TAPECTRL_LOCK_MUTEX (tcw->mutex);
                tapectrl_queue_from_gui_msg(tcw, &from_gui);
                tcw->gui_rapid_values.suppress_next_rapid_time_value = true; /* TOHv4.3-a4: suppress bounce glitch */
                TAPECTRL_UNLOCK_MUTEX(tcw->mutex);

                /* this is the line of code that implements a snappy seek response,
                 * while we wait for the messages to go to the main thread and then
                 * the response to come back */
                tcw->elapsed_1200ths = (int32_t) (f * dur);

                if (tcw->shut_tapectrl_down) { /* (again, check this while we have the opportunity) */
                    quit = true;
                    break;
                }

            }

        } /* endif (seeker_held) */
        
        if (volume_held) {

            mx = ev.mouse.x;
            my = ev.mouse.y;
            f = NAN;
            /* volume is held so x=-1 */
            e = volume_frac_from_mouse_xy (-1, my, tcw->margin_x, tcw->margin_y, tcw->scale, &f);

            if (TAPE_E_OK == e) {
                /* send another, new, separate event to the main thread */
                from_gui.type = TAPECTRL_FROM_GUI_TAPENOISE_VOLUME;
                from_gui.data.volume.fraction = f;
                from_gui.ready = true;
                TAPECTRL_LOCK_MUTEX (tcw->mutex);
                tapectrl_queue_from_gui_msg(tcw, &from_gui);
                TAPECTRL_UNLOCK_MUTEX(tcw->mutex);

                if ( isnan (f) ) {
                    log_warn("tapectrl: BUG: volume fraction is wack (%f)\n", f);
                    e = TAPE_E_BUG;
                } else {
                    /* this is the line of code that implements a snappy seek response,
                     * while we wait for the messages to go to the main thread and then
                     * the response to bounce back, hard */
                    tcw->tapenoise_volume = f;
                }

                if (tcw->shut_tapectrl_down) { /* (again, check this while we have the opportunity) */
                    quit = true;
                    break;
                }
            }
            
            if (TAPE_E_TAPECTRL_OUTSIDE_ZONE == e) { /* trap this */
                e = TAPE_E_OK;
            } else if (TAPE_E_OK != e) {
                /* break; */
            }

        } /* endif (held) */
        
        now_s = al_get_time();
#define TAPECTRL_FLASH_INTERVAL_S 0.3
        if ((now_s - tv->flash_last_toggled_time_s) > TAPECTRL_FLASH_INTERVAL_S) {
            tv->flash_currently_illuminated = ! tv->flash_currently_illuminated;
            tv->flash_last_toggled_time_s = now_s;
        }

        /* 5. PAINT */
        if ( ! tcw->halt_drawing ) { 
            e = guithread_main_paint(tcw,
                                     (tcw->margin_y > 0) ? tcw->margin_y : tcw->margin_x,
                                     tcw->margin_y > 0,
                                     tcw->scale,
                                     tv->flash_currently_illuminated || ! tcw->end_of_tape || tcw->record_activated);
            if (TAPE_E_OK != e) { break; }
        }

        /* again, paint errors will be fatal to tapectrl */
        
#ifndef BUILD_TAPE_TAPECTRL_DELUXE_RESPONSE
        if (0 == msg_n) {
            /* no messages needed to be dealt with, so sleep for a bit */
#ifdef WIN32
            Sleep(20);
#else
            usleep(20000);
#endif
        }
#endif
        
    } /* end main loop */

    TAPECTRL_LOCK_MUTEX(tcw->mutex);

    tcw->tapectrl_error = e;

    finish_labels(tcw);
    finish_inlays(tcw);
    tape_interval_list_finish(&(tcw->interval_list)); /* the TCW copy of the intervals, not the one on tape_vars_t */

    al_destroy_display((ALLEGRO_DISPLAY *) tcw->display);
    tcw->display = NULL;

    /* This is supposed to be a mainthread-only variable.
     * However, Allegro seems to make this impossible. >:|
     * This variable should be cleared from the main thread,
     * but there seems to be no way of detecting from the
     * main thread that the tapectrl thread has shut down
     * (without repeatedly locking/unlocking a mutex)??
     */
    tv->tapectrl_opened = false;

    TAPECTRL_UNLOCK_MUTEX (tcw->mutex);
    
    return NULL;
    
}

/* MUTEX MUST BE LOCKED WHEN CALLED */
static void queue_to_gui_msg (tape_ctrl_window_t * const tcw,
                              const tape_ctrl_msg_to_gui_t * const msg) {
    if ( tcw->to_gui_fill >= TAPECTRL_MSG_QUEUE_SIZE ) {
        log_warn("tapectrl: to_gui_msgs queue is full; flushing");
        tcw->to_gui_fill = 0;
        return;
    }
    tcw->to_gui[tcw->to_gui_fill] = *msg;
    tcw->to_gui_fill++;
}


/* MUTEX MUST BE LOCKED WHEN CALLED */
void tapectrl_queue_from_gui_msg (tape_ctrl_window_t * const tcw,
                                  const tape_ctrl_msg_from_gui_t * const msg) {
    int i;
    if ( ! msg->ready ) {
        log_warn("tapectrl: BUG: queue from_gui msg: ready=0");
        return;
    }
    if ( tcw->from_gui_fill >= TAPECTRL_MSG_QUEUE_SIZE ) {
        log_warn("tapectrl: WARNING: from_gui overflow -- flushing queue");
        tcw->from_gui_fill = 0;
        return;
    }
    /* hack: if this is a _SEEK message, preferentially overwrite any
     * existing _SEEK message already on the queue */
    if (TAPECTRL_FROM_GUI_SEEK == msg->type) {
        for (i=0; i < tcw->from_gui_fill; i++) {
            if (TAPECTRL_FROM_GUI_SEEK == tcw->from_gui[i].type) {
                tcw->from_gui[i].data.seek.fraction = msg->data.seek.fraction; /* lol */
                return; /* lmao, even */
            }
        }
    }
    tcw->from_gui[tcw->from_gui_fill] = *msg;
    tcw->from_gui_fill++;
}

static int guithread_rec_button_pressed (tape_ctrl_window_t * const tcw,
                                         tape_ctrl_msg_from_gui_t * const from_gui_out) {
    /* 1. set main flag
       2. set gui-allegro menu
       3. set tapectrl-side flag
       4. set tapectrl-side GUI state
    */
    /* shouldn't need to check tcw->display as we shouldn't be here if there's no window */

    tcw->record_activated = tcw->record_activated ? false : true; /* toggle tapectrl-side flag */

    /* build SEEK+REC message to main thread */
    from_gui_out->type = TAPECTRL_FROM_GUI_SEEK_AND_SET_REC;
    from_gui_out->data.seek.record_activated = tcw->record_activated;
    from_gui_out->data.seek.fraction = (tcw->record_activated) ? 1.0f : 0.0f;
    from_gui_out->ready = true;
    
    return TAPE_E_OK;

}

static int guithread_save_button_pressed (tape_ctrl_window_t * const tcw,
                                          tape_ctrl_msg_from_gui_t * const from_gui_out) {
    /* build message to main thread */
    from_gui_out->type = TAPECTRL_FROM_GUI_SAVE_TAPE;
    from_gui_out->ready = true;
    return TAPE_E_OK;
}

static int guithread_load_button_pressed (tape_ctrl_window_t * const tcw,
                                          tape_ctrl_msg_from_gui_t * const from_gui_out) {
    /* build message to main thread */
    from_gui_out->type = TAPECTRL_FROM_GUI_LOAD_TAPE;
    from_gui_out->ready = true;
    return TAPE_E_OK;
}

static int guithread_eject_button_pressed (tape_ctrl_window_t * const tcw,
                                           tape_ctrl_msg_from_gui_t * const from_gui_out) {
    /* build message to main thread */
    from_gui_out->type = TAPECTRL_FROM_GUI_EJECT_TAPE;
    from_gui_out->ready = true;
    return TAPE_E_OK;
}

static int guithread_catalogue_button_pressed (tape_ctrl_window_t * const tcw,
                                                tape_ctrl_msg_from_gui_t * const from_gui_out) {
    /* build message to main thread */
    from_gui_out->type = TAPECTRL_FROM_GUI_CATALOGUE;
    from_gui_out->ready = true;
    return TAPE_E_OK;
}

static int guithread_rewind_button_pressed (tape_ctrl_window_t * const tcw,
                                            tape_ctrl_msg_from_gui_t * const from_gui_out) {
    /* build message to main thread */
    from_gui_out->type = TAPECTRL_FROM_GUI_REWIND_TAPE;
    from_gui_out->ready = true;
    return TAPE_E_OK;
}

static int guithread_strip_button_pressed (      tape_ctrl_window_t * const tcw,
                                           tape_ctrl_msg_from_gui_t * const from_gui_out) {
    /* build message to main thread */
    from_gui_out->type = TAPECTRL_FROM_GUI_TOGGLE_STRIP;
    from_gui_out->ready = true;
    return TAPE_E_OK;
}

static int guithread_overclock_button_pressed (      tape_ctrl_window_t * const tcw,
                                               tape_ctrl_msg_from_gui_t * const from_gui_out) {
    /* build message to main thread */
    from_gui_out->type = TAPECTRL_FROM_GUI_TOGGLE_OVERCLOCK;
    from_gui_out->ready = true;
    return TAPE_E_OK;
}

static int handle_mouse_button_down (int const mx,
                                     int const my,
                                     float const scale,
                                     float const margin_x,
                                     float const margin_y,
                                     bool const record_activated,
                                     uint32_t * const current_inlay_inout,
                                     bool * const seeker_held_out,
                                     bool * const volume_held_out,
                                     uint8_t buttons_pressed_out[TAPECTRL_NUM_BUTTONS]) {
                              
    float f;
    int e;
    int b;
    
    memset(buttons_pressed_out, 0, TAPECTRL_NUM_BUTTONS);
    
    f = NAN;
    /* only seek if record is not activated */
    e = TAPE_E_OK;
    
    /* 1. SEEKER */
    
    if ( ! record_activated ) {
        e = seek_frac_from_mouse_xy (mx, my, margin_x, margin_y, scale, &f);
        if (TAPE_E_OK == e) { /* seek_frac_from_mouse_xy returns error if outside scrubzone */
            *seeker_held_out = true;
        }
    }
    
    /* 2. VOLUME SLIDER */
    
    e = volume_frac_from_mouse_xy (mx, my, margin_x, margin_y, scale, &f);
    if (TAPE_E_OK == e) { /* volume_frac_from_mouse_xy returns error if outside scrubzone */
        *volume_held_out = true;
    }
    
    /* 3. INLAY SCAN */
    
    if (    (mx > (margin_x + (scale * (float) INLAY_X)))
         && (mx < (margin_x + (scale * (float) (INLAY_X + INLAY_SQUARE_SIZE))))
         && (my > (margin_y + (scale * (float) INLAY_Y)))
         && (my < (margin_y + (scale * (float) (INLAY_Y + INLAY_SQUARE_SIZE))))) {
        /* if clicked, advance inlay scan (value validation is done later) */
        (*current_inlay_inout)++;
    }
    
    /* 4. BUTTON ARRAY */
    
    for (b=0; b < TAPECTRL_NUM_BUTTONS; b++) {
        float x, y;
        x = button_x_position(b);
        y = button_y_position(b);
        if (    (mx > (margin_x + (scale * (float) x)))
             && (mx < (margin_x + (scale * (float) (x + TAPECTRL_BUTTON_W))))
             && (my > (margin_y + (scale * (float) y)))
             && (my < (margin_y + (scale * (float) (y + TAPECTRL_BUTTON_H))))) {
            buttons_pressed_out[b] = 1;
            break;
        }
    }
    
    return TAPE_E_OK;
}



static ALLEGRO_COLOR interval_type_to_colour (uint8_t type) {
    if (TAPE_INTERVAL_TYPE_SILENCE == type) {
        return al_map_rgb(COLOUR_INTERVAL_SILENCE);
    } else if (TAPE_INTERVAL_TYPE_LEADER == type) {
        return al_map_rgb(COLOUR_INTERVAL_LEADER);
    }
    return al_map_rgb(COLOUR_INTERVAL_DATA);
}

/* NOTE: rectangle outlines are not counted in this; the coordinate is the left edge
 * of the shaded part */
static float button_x_position (int const button_ix) {
    if (TAPECTRL_BUTTON_IX_LOAD        == button_ix) {
        return TAPECTRL_INLAY_SQUARE_RHS_X + RECT_OUTLINE_W_PX_FLT;
    } else if (TAPECTRL_BUTTON_IX_SAVE == button_ix) {
        return TAPECTRL_INLAY_SQUARE_RHS_X + TAPECTRL_BUTTON_W + RECT_OUTLINE_W_PX_FLT;
    } else if (TAPECTRL_BUTTON_IX_CATALOGUE == button_ix) {
        return TAPECTRL_VFD_PANEL_X + TAPECTRL_VFD_PANEL_W - (TAPECTRL_BUTTON_W + RECT_OUTLINE_W_PX_FLT);
    } else if (TAPECTRL_BUTTON_IX_RECORD == button_ix) {
        return (TAPECTRL_W / 2.0f) - (TAPECTRL_BUTTON_W * 1.5f);
    } else if (TAPECTRL_BUTTON_IX_REWIND == button_ix) {
        return (TAPECTRL_W / 2.0f) - (TAPECTRL_BUTTON_W * 0.5f);
    } else if (TAPECTRL_BUTTON_IX_EJECT == button_ix) {
        return (TAPECTRL_W / 2.0f) + (TAPECTRL_BUTTON_W * 0.5f);
    } else if (TAPECTRL_BUTTON_IX_STRIP == button_ix) {
#define TAPECTRL_TURBO_BUTTONS_GAP 10.0f
#define TAPECTRL_BUTTON_STRIP_X     (TAPECTRL_FIXED_MARGINS + (TAPECTRL_TURBO_RECT_W / 2.0f) - (TAPECTRL_BUTTON_W + (TAPECTRL_TURBO_BUTTONS_GAP / 2.0f)))
#define TAPECTRL_BUTTON_OVERCLOCK_X (TAPECTRL_FIXED_MARGINS + (TAPECTRL_TURBO_RECT_W / 2.0f) + (TAPECTRL_TURBO_BUTTONS_GAP / 2.0f))
        return TAPECTRL_BUTTON_STRIP_X;
    } else if (TAPECTRL_BUTTON_IX_OVERCLOCK == button_ix) {
        return TAPECTRL_BUTTON_OVERCLOCK_X;
    }
    return 0;
}

/* NOTE: rectangle outlines are not counted in this; the coordinate is the top edge
 * of the shaded part */
static float button_y_position (int const button_ix) {
    /* deal with the three buttons along the top */
    if (    (TAPECTRL_BUTTON_IX_LOAD      == button_ix)
         || (TAPECTRL_BUTTON_IX_SAVE      == button_ix)
         || (TAPECTRL_BUTTON_IX_CATALOGUE == button_ix) ) {
        return TAPECTRL_FIXED_MARGINS;
    } else if (    (TAPECTRL_BUTTON_IX_RECORD == button_ix)
                || (TAPECTRL_BUTTON_IX_REWIND == button_ix)
                || (TAPECTRL_BUTTON_IX_EJECT  == button_ix)) {
        return TAPECTRL_VFD_PANEL_Y + TAPECTRL_VFD_PANEL_H + TAPECTRL_SPACER_1;
    } else if (    (TAPECTRL_BUTTON_IX_STRIP     == button_ix)
                || (TAPECTRL_BUTTON_IX_OVERCLOCK == button_ix)) {
        return TAPECTRL_BOTTOM_ROWS_LOWER_LHS_Y - (TAPECTRL_BUTTON_H / 2.0f);
    }
    return 0;
}

#define LAMP_PLAY_TRIANGLE_NUM_VERTICES 3
static const float lamp_play_triangle_vertices[2 * LAMP_PLAY_TRIANGLE_NUM_VERTICES] = {
    -9.0f,         -10.0f,
    -9.0f,         -10.0f + 20.0f,
    -9.0f + 20.0f, -10.0f + (20.0f / 2.0f)
};

/* should be 3x2 ish? */
#define TAPECTRL_CASSETTE_ICONS_SHELL_W        42.0f
#define TAPECTRL_CASSETTE_ICONS_SHELL_H        24.0f 
#define TAPECTRL_CASSETTE_ICONS_CORNER_RADIUS   0.0f 
#define TAPECTRL_CASSETTE_ICONS_FUDGE_NUDGE     1.0f 

#define TAPECTRL_ICON_LOAD_MARGIN_Y             7.0f
#define TAPECTRL_ICON_LOAD_MARGIN_X             9.0f
#define TAPECTRL_ICON_LOAD_SPINDLE_RADIUS       3.0f
#define TAPECTRL_ICON_LOAD_SPINDLE_OFFSET      11.0f
#define TAPECTRL_ICON_LOAD_SPINDLE_Y_OFFSET     0.0f /* positive values move tape spindles and window upwards */

#define TAPECTRL_CASSETTE_ICONS_Y_SHIFT         3.0f

static void draw_cassette_icon (float x_prescaled,
                                float y_prescaled,
                                float scale,
                                float stroke_width,
                                ALLEGRO_COLOR fgc_fill,
                                ALLEGRO_COLOR bgc) {
                                  
    float offcentre_y;
    offcentre_y = TAPECTRL_CASSETTE_ICONS_Y_SHIFT;
    /* black external border */
    al_draw_rounded_rectangle (x_prescaled - scale * (TAPECTRL_CASSETTE_ICONS_SHELL_W/2.0f),
                               y_prescaled + (scale*offcentre_y) - (scale * (TAPECTRL_CASSETTE_ICONS_SHELL_H/2.0f)),
                               x_prescaled + scale * (TAPECTRL_CASSETTE_ICONS_SHELL_W/2.0f),
                               y_prescaled + scale * (offcentre_y + TAPECTRL_CASSETTE_ICONS_FUDGE_NUDGE + TAPECTRL_CASSETTE_ICONS_SHELL_H/2.0f),
                               scale * TAPECTRL_CASSETTE_ICONS_CORNER_RADIUS,
                               scale * TAPECTRL_CASSETTE_ICONS_CORNER_RADIUS,
                               al_map_rgb(COLOUR_BLACK_BORDER),
                               scale * 2.0f);
    /* cassette shell */
    al_draw_filled_rounded_rectangle (x_prescaled - scale * (TAPECTRL_CASSETTE_ICONS_SHELL_W/2.0f),
                                      y_prescaled + scale*offcentre_y - scale * (TAPECTRL_CASSETTE_ICONS_SHELL_H/2.0f),
                                      x_prescaled + scale * (TAPECTRL_CASSETTE_ICONS_SHELL_W/2.0f),
                                      y_prescaled + scale * (offcentre_y + TAPECTRL_CASSETTE_ICONS_FUDGE_NUDGE + TAPECTRL_CASSETTE_ICONS_SHELL_H/2.0f),
                                      scale * TAPECTRL_CASSETTE_ICONS_CORNER_RADIUS,
                                      scale * TAPECTRL_CASSETTE_ICONS_CORNER_RADIUS,
                                      fgc_fill);
    /* spindles */
    al_draw_filled_circle(x_prescaled + scale * (TAPECTRL_ICON_LOAD_SPINDLE_OFFSET),
                          y_prescaled + scale * (offcentre_y - TAPECTRL_ICON_LOAD_SPINDLE_Y_OFFSET),
                          scale * TAPECTRL_ICON_LOAD_SPINDLE_RADIUS,
                          bgc);
    al_draw_filled_circle(x_prescaled + scale * (-1.0f * TAPECTRL_ICON_LOAD_SPINDLE_OFFSET),
                          y_prescaled + scale * (offcentre_y - TAPECTRL_ICON_LOAD_SPINDLE_Y_OFFSET),
                          scale * TAPECTRL_ICON_LOAD_SPINDLE_RADIUS,
                          bgc);
    /* window */
    al_draw_filled_rectangle(x_prescaled + scale * (-5.0f),
                             y_prescaled + scale * (offcentre_y - (2.0f + TAPECTRL_ICON_LOAD_SPINDLE_Y_OFFSET)),
                             x_prescaled + scale * (5.0f),
                             y_prescaled + scale * (offcentre_y + (2.0f - TAPECTRL_ICON_LOAD_SPINDLE_Y_OFFSET)),
                             bgc);
    float my_y = -14.0f;
#define TAPECTRL_CASSETTE_ICONS_EMBOSSED_H 7.0f
    al_draw_line(x_prescaled + scale * (-17.0f),
                 y_prescaled + scale * (offcentre_y + 1.0f - my_y),
                 x_prescaled + scale * (-15.0f),
                 y_prescaled + scale * (offcentre_y - (my_y + TAPECTRL_CASSETTE_ICONS_EMBOSSED_H)),
                 bgc,
                 stroke_width * scale);
    al_draw_line(x_prescaled + scale * (15.0f),
                 y_prescaled + scale * (offcentre_y - (my_y + TAPECTRL_CASSETTE_ICONS_EMBOSSED_H)),
                 x_prescaled + scale * (-15.0f),
                 y_prescaled + scale * (offcentre_y - (my_y + TAPECTRL_CASSETTE_ICONS_EMBOSSED_H)),
                 bgc,
                 stroke_width * scale);
    al_draw_line(x_prescaled + scale * (17.0f),
                 y_prescaled + scale * (offcentre_y + 1.0f - my_y),
                 x_prescaled + scale * (15.0f),
                 y_prescaled + scale * (offcentre_y - (my_y + TAPECTRL_CASSETTE_ICONS_EMBOSSED_H)),
                 bgc,
                 stroke_width * scale);
}

/* TOHv4.4 */
static void draw_icon (bool filled,
                       int const icon_ix,
                       float const x_prescaled, 
                       float const y_prescaled,
                       float const scale,
                       ALLEGRO_COLOR const fgc_fill,
                       ALLEGRO_COLOR const fgc_outline,
                       ALLEGRO_COLOR const bgc,
                       ALLEGRO_COLOR const fgc_highlight,
                       bool draw_lamp_outlines) {
    int v,i;
    float xx, yy, sz;
    float stroke_width;
    float play_triangle[LAMP_PLAY_TRIANGLE_NUM_VERTICES * 2];
    float eject_triangle[] = {
        x_prescaled,           y_prescaled - scale * 12.0f,
        x_prescaled + scale * -10.0f, y_prescaled + scale * 2.0f,
        x_prescaled + scale *  10.0f, y_prescaled + scale * 2.0f
    };
    float f, g, q;
#define TAPECTRL_ICON_REWIND_SPLIT             15
    float rewind_triangle_left[] = {
        x_prescaled + scale * -5.0f, y_prescaled + scale * 12.0f,
        x_prescaled + scale * -5.0f, y_prescaled - scale * 12.0f,
        x_prescaled + scale * -16.0f, y_prescaled
    };
    float rewind_triangle_right[] = {
        rewind_triangle_left[0] + (scale * TAPECTRL_ICON_REWIND_SPLIT), rewind_triangle_left[1],
        rewind_triangle_left[2] + (scale * TAPECTRL_ICON_REWIND_SPLIT), rewind_triangle_left[3],
        rewind_triangle_left[4] + (scale * TAPECTRL_ICON_REWIND_SPLIT), rewind_triangle_left[5]
    };
    
#define TAPECTRL_TAPENOISE_POLYGON_NUM_VERTICES 7
    float tapenoise_polygon[] = {
        /*
         *
         *          -+ <1>
         *         / |
         * <3> <2>/  |
         *   +---+   |
         *   |   O   |
         *   +---+   |
         * <4> <5>\  |
         *         \ |
         *          -+ <0>
         *             <6>
         */
#define MARGINX TAPECTRL_ICON_TAPENOISE_MARGIN_X
#define BOXSIZE TAPECTRL_ICON_TAPENOISE_BOX_SIZE
        x_prescaled + scale * (MARGINX + ((BOXSIZE*3.0f)/2.0f)), /* <0>,x */
        y_prescaled + scale * BOXSIZE,                           /* <0>,y */
        x_prescaled + scale * (MARGINX + ((BOXSIZE*3.0f)/2.0f)), /* <1>,x */
        y_prescaled + scale * -BOXSIZE,                          /* <1>,y */
        x_prescaled + scale * (MARGINX + (BOXSIZE)),             /* <2>,x */
        y_prescaled + scale * -BOXSIZE/2.0f,                     /* <2>,y */
        x_prescaled + scale * MARGINX,                           /* <3>,x */
        y_prescaled + scale * -BOXSIZE/2.0f,                     /* <3>,y */
        x_prescaled + scale * (MARGINX),                         /* <4>,x */
        y_prescaled + scale * BOXSIZE/2.0f,                      /* <4>,y */
        x_prescaled + scale * (MARGINX + BOXSIZE),               /* <5>,x */
        y_prescaled + scale * BOXSIZE/2.0f,                      /* <5>,y */
        x_prescaled + scale * (MARGINX + ((BOXSIZE*3.0f)/2.0f)), /* <6>,x */
        y_prescaled + scale * BOXSIZE,    // <6>,y
    };
#define TAPECTRL_TAPENOISE_CUT_POLYGON_NUM_VERTICES 6
    float tapenoise_erase_polygon[] = {
        /*
         *
         * <2> +--------+ <1>
         *     |       / (x=2)
         *     |      /
         *     |     + <0> <5>
         *     |      \ (x=1.5)
         *     |       \
         * <3> +--------+ <4>
         */
        /* reminder that MARGINX is negative */
        x_prescaled + (scale * (MARGINX + ((BOXSIZE*3.0f)/2.0f))), // 0
        y_prescaled,
        x_prescaled + (scale * (MARGINX + (BOXSIZE*3.0f))),         //1
        y_prescaled + (scale * (BOXSIZE*-2.0f)),
        x_prescaled + (scale * (MARGINX - BOXSIZE*0.6f)),           //2
        y_prescaled + (scale * (BOXSIZE*-2.0f)),
        x_prescaled + (scale * (MARGINX - BOXSIZE*0.6f)),           //3
        y_prescaled + (scale * (BOXSIZE*2.0f)),
        x_prescaled + (scale * (MARGINX + BOXSIZE*3.0f)),           //4
        y_prescaled + (scale * (BOXSIZE*2.0f)),
        x_prescaled + (scale * (MARGINX + ((BOXSIZE*3.0f)/2.0f))),  //5
        y_prescaled
    };
    /*            <2>
     *             x            x
     *      <4> <3>|\           |\
     *         x---x \      x---x \
     *         |<--G->x<1>  |      x
     *         x---x /      x---x /
     *      <5> <6>|/           |/
     *             x            x
     *          <0 & 7>
     *            
     * 
     *  <0>   <11>   <8>   <7>    <4>   <3>
     *   x-----x      x--0--x      x-----x   -------
     *   |     |      |     |      |     |        ^
     *   |<-W->|<-G-->|     |      |     |        |
     *   |     |      |     |      |     |        |
     *   |     |      |     |      |     |        H
     *   |     |      |     |      |     |        |
     *   |     x------x     x------x     |   ---  |
     *   |    <10>   <9>   <6>    <5>    |    D   v
     *   x-------------------------------x   -------
     *  <1>                             <2>
     */


    /* lower portion */
#define TAPECTRL_SKIPS_MAIN_POLY_W  11.0f 
#define TAPECTRL_SKIPS_MAIN_POLY_G   9.0f
#define TAPECTRL_SKIPS_MAIN_POLY_H  17.0f
#define TAPECTRL_SKIPS_MAIN_POLY_D   1.0f
    /* f is left edge, prescaled */
    f = x_prescaled + scale * -((1.5f * TAPECTRL_SKIPS_MAIN_POLY_W) + TAPECTRL_SKIPS_MAIN_POLY_G);
    /* g is right edge, prescaled */
    g = f + scale * (TAPECTRL_SKIPS_MAIN_POLY_W * 3.0f + TAPECTRL_SKIPS_MAIN_POLY_G * 2.0f);
    /* q is upper base edge, prescaled */
    q = y_prescaled + scale*(TAPECTRL_SKIPS_MAIN_POLY_H-TAPECTRL_SKIPS_MAIN_POLY_D);
#define TAPECTRL_SKIPS_MAIN_POLYGON_NUM_VERTICES 13
    float skips_main_polygon[] = {
        f,                                                   /* <0> */
        y_prescaled,
        f,                                                   /* <1> */
        y_prescaled + scale*TAPECTRL_SKIPS_MAIN_POLY_H,
        g,                                                   /* <2> */
        y_prescaled + scale*TAPECTRL_SKIPS_MAIN_POLY_H,
        g,                                                   /* <3> */
        y_prescaled,
        g - scale*TAPECTRL_SKIPS_MAIN_POLY_W,                /* <4> */
        y_prescaled,
        g - scale*TAPECTRL_SKIPS_MAIN_POLY_W,                               /* <5> */
        q,
        g - scale*(TAPECTRL_SKIPS_MAIN_POLY_W+TAPECTRL_SKIPS_MAIN_POLY_G),  /* <6> */
        q,
        x_prescaled + scale*TAPECTRL_SKIPS_MAIN_POLY_W*0.5f,                /* <7> */
        y_prescaled,
        x_prescaled - scale*TAPECTRL_SKIPS_MAIN_POLY_W*0.5f,                /* <8> */
        y_prescaled,
        x_prescaled - scale*TAPECTRL_SKIPS_MAIN_POLY_W*0.5f,                /* <9> */
        q,
        x_prescaled - scale*(TAPECTRL_SKIPS_MAIN_POLY_W*0.5f + TAPECTRL_SKIPS_MAIN_POLY_G), /* <10> */
        q,
        x_prescaled - scale*(TAPECTRL_SKIPS_MAIN_POLY_W*0.5f + TAPECTRL_SKIPS_MAIN_POLY_G), /* <11> */
        y_prescaled,
        f,                                                                                  /* <12> (dup. <0>) */
        y_prescaled
    };
#define TAPECTRL_SKIPS_GAP          4.0f
#define TAPECTRL_SKIPS_ARROWHEAD_W  4.0f
#define TAPECTRL_SKIPS_ARROWHEAD_H  12.0f
#define TAPECTRL_SKIPS_ARROWSHAFT_H 4.0f
#define TAPECTRL_SKIPS_LEFT_ARROW_POLYGON_NUM_VERTICES 8
f = x_prescaled - scale*(TAPECTRL_SKIPS_MAIN_POLY_W*0.5f + TAPECTRL_SKIPS_ARROWHEAD_W);
float skips_left_arrow_polygon[] = {
  f, /* <0> */
  y_prescaled - scale*TAPECTRL_SKIPS_GAP,
  x_prescaled - scale*TAPECTRL_SKIPS_MAIN_POLY_W*0.5f, /* <1> */
  y_prescaled - scale*(TAPECTRL_SKIPS_GAP + 0.5f*TAPECTRL_SKIPS_ARROWHEAD_H),
  f, /* <2> */
  y_prescaled - scale*(TAPECTRL_SKIPS_GAP + TAPECTRL_SKIPS_ARROWHEAD_H),
  f, /* <3> */
  y_prescaled - scale*(TAPECTRL_SKIPS_GAP + 0.5f*(TAPECTRL_SKIPS_ARROWHEAD_H+TAPECTRL_SKIPS_ARROWSHAFT_H)),
  x_prescaled - scale*(TAPECTRL_SKIPS_MAIN_POLY_W*0.5f + TAPECTRL_SKIPS_MAIN_POLY_G), /* <4> */
  y_prescaled - scale*(TAPECTRL_SKIPS_GAP + 0.5f*(TAPECTRL_SKIPS_ARROWHEAD_H+TAPECTRL_SKIPS_ARROWSHAFT_H)),
  x_prescaled - scale*(TAPECTRL_SKIPS_MAIN_POLY_W*0.5f + TAPECTRL_SKIPS_MAIN_POLY_G), /* <5> */
  y_prescaled + scale*(0.5f*TAPECTRL_SKIPS_ARROWSHAFT_H -(TAPECTRL_SKIPS_GAP + 0.5f*TAPECTRL_SKIPS_ARROWHEAD_H)),
  f, /* <6> */
  y_prescaled + scale*(0.5f*TAPECTRL_SKIPS_ARROWSHAFT_H -(TAPECTRL_SKIPS_GAP + 0.5f*TAPECTRL_SKIPS_ARROWHEAD_H)),
  f, /* <7> */
  y_prescaled - scale*TAPECTRL_SKIPS_GAP,
};
#define TAPECTRL_ROCKET_POLYGON_NUM_VERTICES 29
    float rocket_polygon[] = {
        x_prescaled + scale * (14.6290909090909),
        y_prescaled + scale * (-14.6018181818182),
        x_prescaled + scale * (12.4036363636364),
        y_prescaled + scale * (-13.8790909090909),
        x_prescaled + scale * (10.3227272727273),
        y_prescaled + scale * (-13.0390909090909),
        x_prescaled + scale * (8.25818181818182),
        y_prescaled + scale * (-12.0545454545455),
        x_prescaled + scale * (6.41454545454546),
        y_prescaled + scale * (-10.9881818181818),
        x_prescaled + scale * (4.29545454545455),
        y_prescaled + scale * (-9.67090909090909),
        x_prescaled + scale * (2.29636363636364),
        y_prescaled + scale * (-8.15454545454545),
        x_prescaled + scale * (0.384545454545455),
        y_prescaled + scale * (-6.43636363636364),
        x_prescaled + scale * (-1.52181818181818),
        y_prescaled + scale * (-4.47272727272727),
        x_prescaled + scale * (-3.76909090909091),
        y_prescaled + scale * (-1.90636363636364),
        x_prescaled + scale * (-5.93181818181818),
        y_prescaled + scale * (-2.31818181818182),
        x_prescaled + scale * (-14.7681818181818),
        y_prescaled + scale * (4.71818181818182),
        x_prescaled + scale * (-7.87636363636364),
        y_prescaled + scale * (4.85454545454545),
        x_prescaled + scale * (-7.51363636363636),
        y_prescaled + scale * (5.20909090909091),
        x_prescaled + scale * (-8.82272727272727),
        y_prescaled + scale * (6.53181818181818),
        x_prescaled + scale * (-6.79090909090909),
        y_prescaled + scale * (8.58272727272727),
        x_prescaled + scale * (-5.47363636363636),
        y_prescaled + scale * (7.25727272727273),
        x_prescaled + scale * (-5.11090909090909),
        y_prescaled + scale * (7.63636363636364),
        x_prescaled + scale * (-5.06181818181818),
        y_prescaled + scale * (14.5827272727273),
        x_prescaled + scale * (2.09727272727273),
        y_prescaled + scale * (5.75454545454545),
        x_prescaled + scale * (1.68545454545454),
        y_prescaled + scale * (3.64363636363636),
        x_prescaled + scale * (4.71272727272727),
        y_prescaled + scale * (1.04727272727273),
        x_prescaled + scale * (6.64090909090909),
        y_prescaled + scale * (-0.815454545454545),
        x_prescaled + scale * (8.32909090909091),
        y_prescaled + scale * (-2.70818181818182),
        x_prescaled + scale * (9.69272727272727),
        y_prescaled + scale * (-4.50545454545455),
        x_prescaled + scale * (11.1081818181818),
        y_prescaled + scale * (-6.69),
        x_prescaled + scale * (12.2154545454545),
        y_prescaled + scale * (-8.65636363636363),
        x_prescaled + scale * (13.1127272727273),
        y_prescaled + scale * (-10.5436363636364),
        x_prescaled + scale * (13.9227272727273),
        y_prescaled + scale * (-12.4963636363636)
    };
        
    stroke_width = 1.0f;
    if ((stroke_width * scale) < 1.0f) {
        /* clamp stroke width to >=1.0f, regardless of scaling factors */
        stroke_width = 1.0f / scale;
    }
    
    
/*         <6>
 *          x
 *         / \
 *        /   \
 *       /     \
 *      /       \
 *  <0>x---x x---x<5>
 *      <1>| |<4>
 *         | |
 *         | |
 *         x-x
 *      <2>   <3>
 */
    xx = x_prescaled;
    yy = y_prescaled - scale*8.0f;
    sz = 3.0f;
#define ARROW_POLY_NUM_POINTS 7
    const float load_uparrow_polygon[ARROW_POLY_NUM_POINTS*2]   = { xx-scale*sz*3.0f, yy+scale*sz*0.0f,    /* far left */
                                                                    xx-scale*sz*1.0f, yy+scale*sz*0.0f,
                                                                    xx-scale*sz*1.0f, yy+scale*sz*2.0f,    /* bottom */
                                                                    xx+scale*sz*1.0f, yy+scale*sz*2.0f,
                                                                    xx+scale*sz*1.0f, yy+scale*sz*0.0f,
                                                                    xx+scale*sz*3.0f, yy+scale*sz*0.0f,    /* far right */
                                                                    xx+scale*sz*0.0f, yy-scale*sz*3.0f  }; /* tip */
    sz *= -1.0f; /* flip both axes */
    yy = y_prescaled - scale*11.0f;
    const float save_downarrow_polygon[ARROW_POLY_NUM_POINTS*2] = { xx-scale*sz*3.0f, yy+scale*sz*0.0f,    // far left
                                                                    xx-scale*sz*1.0f, yy+scale*sz*0.0f,
                                                                    xx-scale*sz*1.0f, yy+scale*sz*2.0f,    // bottom
                                                                    xx+scale*sz*1.0f, yy+scale*sz*2.0f,
                                                                    xx+scale*sz*1.0f, yy+scale*sz*0.0f,
                                                                    xx+scale*sz*3.0f, yy+scale*sz*0.0f,    // far right
                                                                    xx+scale*sz*0.0f, yy-scale*sz*3.0f  }; // tip
    
    if (TAPECTRL_ICON_ID_LOAD == icon_ix) {
        /* load */
        draw_cassette_icon (x_prescaled, y_prescaled, scale, stroke_width, fgc_fill, bgc);
        al_draw_polygon(load_uparrow_polygon, ARROW_POLY_NUM_POINTS, ALLEGRO_LINE_JOIN_ROUND, al_map_rgb(0,0,0), scale*2.0f, 0.0f);
        al_draw_filled_polygon(load_uparrow_polygon, ARROW_POLY_NUM_POINTS, fgc_highlight);
    } else if (TAPECTRL_ICON_ID_SAVE == icon_ix) {
        /* TOHv4.4
           save */
        draw_cassette_icon (x_prescaled, y_prescaled, scale, stroke_width, fgc_fill, bgc);
        al_draw_polygon(save_downarrow_polygon, ARROW_POLY_NUM_POINTS, ALLEGRO_LINE_JOIN_ROUND, al_map_rgb(0,0,0), scale*2.0f, 0.0f);
        al_draw_filled_polygon(save_downarrow_polygon, ARROW_POLY_NUM_POINTS, fgc_highlight);
    } else if (TAPECTRL_ICON_ID_EJECT == icon_ix) {
        /* eject */
        /* external black border */
        al_draw_polygon(eject_triangle, 3, ALLEGRO_LINE_JOIN_ROUND, al_map_rgb(COLOUR_BLACK_BORDER), scale*2.0f, 0.0f);
        /* filled triangle */
        al_draw_filled_polygon(eject_triangle, 3, fgc_fill);
        /* external black border */
        al_draw_filled_rectangle(eject_triangle[2] - scale*1.0f,
                                 eject_triangle[3] + scale*5.0f - scale*1.0f,
                                 eject_triangle[4] + scale*1.0f,
                                 eject_triangle[3] + scale*10.0f + scale*1.0f,
                                 al_map_rgb(COLOUR_BLACK_BORDER));
        al_draw_filled_rectangle(eject_triangle[2],
                                 eject_triangle[3] + scale*5.0f,
                                 eject_triangle[4],
                                 eject_triangle[3] + scale*10.0f,
                                 fgc_fill);
    } else if (TAPECTRL_ICON_ID_TAPENOISE == icon_ix) {
        /* tapenoise
           draw the circles first, then clean them up */
        al_draw_circle(x_prescaled,
                       y_prescaled,
                       10.0f*scale,
                       fgc_fill,
                       stroke_width * scale);
        al_draw_circle(x_prescaled,
                       y_prescaled,
                       14.0f*scale,
                       fgc_fill,
                       stroke_width * scale);
        al_draw_circle(x_prescaled,
                       y_prescaled,
                       18.0f*scale,
                       fgc_fill,
                       stroke_width * scale);
                       
        al_draw_filled_polygon(tapenoise_erase_polygon, TAPECTRL_TAPENOISE_CUT_POLYGON_NUM_VERTICES, bgc);
        
        /* then draw the rest */
        if (filled) {
            al_draw_filled_polygon(tapenoise_polygon, TAPECTRL_TAPENOISE_POLYGON_NUM_VERTICES, fgc_fill);
        } else {
            al_draw_polygon(tapenoise_polygon, TAPECTRL_TAPENOISE_POLYGON_NUM_VERTICES, ALLEGRO_LINE_JOIN_ROUND, fgc_outline, stroke_width * scale, 0.0f);
        }

    } else if (TAPECTRL_ICON_ID_CATALOGUE == icon_ix) {
#define TAPECTRL_ICON_CATALOGUE_BOX_W 20.0f
#define TAPECTRL_ICON_CATALOGUE_BOX_H 25.0f
        /* external black border */
        al_draw_rectangle(x_prescaled + (-TAPECTRL_ICON_CATALOGUE_BOX_W / 2.0f) * scale,
                          y_prescaled + (-TAPECTRL_ICON_CATALOGUE_BOX_H / 2.0f) * scale,
                          x_prescaled + (TAPECTRL_ICON_CATALOGUE_BOX_W / 2.0f) * scale,
                          y_prescaled + (TAPECTRL_ICON_CATALOGUE_BOX_H / 2.0f) * scale,
                          al_map_rgb(COLOUR_BLACK_BORDER),
                          (stroke_width + 2.0f) * scale);
        /* main border */
        al_draw_rectangle(x_prescaled + (-TAPECTRL_ICON_CATALOGUE_BOX_W / 2.0f) * scale,
                          y_prescaled + (-TAPECTRL_ICON_CATALOGUE_BOX_H / 2.0f) * scale,
                          x_prescaled + (TAPECTRL_ICON_CATALOGUE_BOX_W / 2.0f) * scale,
                          y_prescaled + (TAPECTRL_ICON_CATALOGUE_BOX_H / 2.0f) * scale,
                          fgc_fill,
                          stroke_width * scale);
#define TAPECTRL_ICON_CATALOGUE_NUM_LINES 5
#define TAPECTRL_ICON_CATALOGUE_LINES_MARGIN 4.0f
        for (i=1; i <= TAPECTRL_ICON_CATALOGUE_NUM_LINES; i++) {
            float line_h;
            float g;
            line_h = TAPECTRL_ICON_CATALOGUE_BOX_H / (1.0f + TAPECTRL_ICON_CATALOGUE_NUM_LINES);
            g = (-TAPECTRL_ICON_CATALOGUE_BOX_H / 2.0f) + (i*line_h);
            al_draw_rectangle(x_prescaled + (((-TAPECTRL_ICON_CATALOGUE_BOX_W) / 2.0f) + TAPECTRL_ICON_CATALOGUE_LINES_MARGIN) * scale,
                              y_prescaled + (g * scale),
                              x_prescaled + (((TAPECTRL_ICON_CATALOGUE_BOX_W) / 2.0f) - TAPECTRL_ICON_CATALOGUE_LINES_MARGIN) * scale,
                              y_prescaled + ((g/*+1*/) * scale),
                              fgc_fill,
                              stroke_width * scale);
        }

    } else if (TAPECTRL_ICON_ID_RECORD == icon_ix) {
        /* record
           external black border */
        al_draw_filled_circle(x_prescaled,
                              y_prescaled,
                              scale * (TAPECTRL_ICON_REC_RADIUS + 1.0f),
                              al_map_rgb(COLOUR_BLACK_BORDER));
        if ( filled ) {
            al_draw_filled_circle(x_prescaled,
                                  y_prescaled,
                                  scale * TAPECTRL_ICON_REC_RADIUS,
                                  fgc_fill);
        } else if ( draw_lamp_outlines ) {
            /* black fill */
            al_draw_circle (x_prescaled,
                            y_prescaled,
                            scale * TAPECTRL_ICON_REC_RADIUS,
                            fgc_outline,
                            stroke_width * scale);
        }
    } else if (TAPECTRL_ICON_ID_REWIND == icon_ix) {
        /* black border */
        al_draw_polygon(rewind_triangle_left,  3, ALLEGRO_LINE_JOIN_ROUND, al_map_rgb(COLOUR_BLACK_BORDER), scale*2.0f, 0.0f);
        /* fill */
        al_draw_filled_polygon(rewind_triangle_left,  3, fgc_fill);
        /* black border */
        al_draw_polygon(rewind_triangle_right, 3, ALLEGRO_LINE_JOIN_ROUND, al_map_rgb(COLOUR_BLACK_BORDER), scale*2.0f, 0.0f);
        /* fill */
        al_draw_filled_polygon(rewind_triangle_right, 3, fgc_fill);
    } else if (TAPECTRL_ICON_ID_PLAY == icon_ix) {
        for (v=0; v < (LAMP_PLAY_TRIANGLE_NUM_VERTICES * 2); v+=2) {
            play_triangle[v]   = x_prescaled + (scale * (float) lamp_play_triangle_vertices[v]  );
            play_triangle[v+1] = y_prescaled + (scale * (float) lamp_play_triangle_vertices[v+1]);
        }
        if ( filled ) {
            al_draw_filled_polygon(play_triangle, 3, fgc_fill);
        } else if ( draw_lamp_outlines ) {
            al_draw_polygon(play_triangle, 3, ALLEGRO_LINE_JOIN_ROUND, fgc_outline, stroke_width * scale, 0);
        }
    } else if (TAPECTRL_ICON_ID_OVERCLOCK == icon_ix) {
        /* black external border */
        al_draw_polygon(rocket_polygon, 
                        TAPECTRL_ROCKET_POLYGON_NUM_VERTICES,
                        ALLEGRO_LINE_JOIN_ROUND,
                        al_map_rgb(COLOUR_BLACK_BORDER),
                        (2.0f + stroke_width) * scale,
                        0.0f);
        if ( filled ) {
            al_draw_filled_polygon(rocket_polygon, TAPECTRL_ROCKET_POLYGON_NUM_VERTICES, fgc_fill);        
        } else {
            al_draw_polygon(rocket_polygon, 
                            TAPECTRL_ROCKET_POLYGON_NUM_VERTICES,
                            ALLEGRO_LINE_JOIN_ROUND,
                            fgc_outline,
                            stroke_width * scale,
                            0.0f);
        }
    } else if (TAPECTRL_ICON_ID_STRIP == icon_ix) {
        /* main polygon first:
           black external border */
        al_draw_polygon(skips_main_polygon, 
                        TAPECTRL_SKIPS_MAIN_POLYGON_NUM_VERTICES,
                        ALLEGRO_LINE_JOIN_ROUND,
                        al_map_rgb(COLOUR_BLACK_BORDER),
                        (2.0f + stroke_width) * scale,
                        0.0f);
        al_draw_filled_polygon(skips_main_polygon, TAPECTRL_SKIPS_MAIN_POLYGON_NUM_VERTICES, fgc_fill);

        /* black external border */
        al_draw_polygon(skips_left_arrow_polygon, 
                        TAPECTRL_SKIPS_LEFT_ARROW_POLYGON_NUM_VERTICES,
                        ALLEGRO_LINE_JOIN_ROUND,
                        al_map_rgb(COLOUR_BLACK_BORDER),
                        (2.0f + stroke_width) * scale,
                        0.0f);
        al_draw_filled_polygon(skips_left_arrow_polygon, TAPECTRL_SKIPS_LEFT_ARROW_POLYGON_NUM_VERTICES, fgc_highlight);
        /* translate it for the right arrow ... */
        for (v=0;v<TAPECTRL_SKIPS_LEFT_ARROW_POLYGON_NUM_VERTICES*2;v+=2) {
            skips_left_arrow_polygon[v] += scale*(TAPECTRL_SKIPS_MAIN_POLY_W + TAPECTRL_SKIPS_MAIN_POLY_G);
        }
        /* black border */
        al_draw_polygon(skips_left_arrow_polygon, 
                        TAPECTRL_SKIPS_LEFT_ARROW_POLYGON_NUM_VERTICES,
                        ALLEGRO_LINE_JOIN_ROUND,
                        al_map_rgb(COLOUR_BLACK_BORDER),
                        (2.0f + stroke_width) * scale,
                        0.0f);
        al_draw_filled_polygon(skips_left_arrow_polygon, TAPECTRL_SKIPS_LEFT_ARROW_POLYGON_NUM_VERTICES, fgc_highlight);
    } else {
        log_warn("unknown icon_ix %d\n", icon_ix);
    }
}

#define OUTLINE_THICKNESS (1.0f)

static void paint_lamp(float const margin_x,
                       float const margin_y,
                       float const scale,
                       float const x_unscaled,
                       float const y_unscaled,
                       bool const draw_lamp_outlines,
                       bool const lit) {
    if (lit) {
        al_draw_filled_circle (margin_x + scale * x_unscaled,
                               margin_y + scale * y_unscaled,
                               scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                               al_map_rgb(COLOUR_LAMP_A));
    } else if (draw_lamp_outlines) {
#define COLOUR_LAMP_FILL_WHEN_OFF 0,0,0
        al_draw_filled_circle (margin_x + scale * x_unscaled,
                               margin_y + scale * y_unscaled,
                               scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                               al_map_rgb(COLOUR_LAMP_FILL_WHEN_OFF));
        al_draw_circle (margin_x + scale * x_unscaled,
                        margin_y + scale * y_unscaled,
                        scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                        al_map_rgb(COLOUR_LAMP_OUTLINE),
                        OUTLINE_THICKNESS * scale);
    }
}

static int guithread_main_paint (tape_ctrl_window_t * const tcw,
                                 int const black_bar_w,
                                 bool const letterbox,
                                 float const scale,
                                 bool const flash_on) {

    int v;
    float bm_w, bm_h, lb_w, lb_h;
    ALLEGRO_BITMAP *bm;
    double margin_x, margin_y, seeker_w, scrub_x;
    int i;
    float knob_vx[SEEKER_KNOB_NUM_VERTICES * 2];
    float square_f;
    float inlay_w, inlay_h, max_dim;
    bool large;
    bool draw_lamp_outlines;
    bool ok;
    int e;
    int32_t dur;             /* TOHv4.3-a3  */
    float button_icon_scale; /* TOHv4.4     */
    float f;                 /* TOHv4.4     */

    margin_x = (float) (letterbox ?           0 : black_bar_w);
    margin_y = (float) (letterbox ? black_bar_w :           0);
    
    lb_w = (float) TAPECTRL_LABEL_WIDTH;
    lb_h = (float) TAPECTRL_LABEL_HEIGHT;

    ok = (TAPE_E_OK == tcw->reported_error);

    al_clear_to_color (al_map_rgba (COLOUR_BG, 0));
        
    draw_lamp_outlines = false;
#ifdef BUILD_TAPE_TAPECTRL_LAMP_OUTLINES
    draw_lamp_outlines = true;
#endif
    
    /* 1. SEEKER */

    dur = guithread_duration_from_intervals(&(tcw->interval_list));

    if ( ok && ! tcw->record_activated && (dur>0)) { /* do not offer seeking in record mode */

        scrub_x = 0.0f;
        seeker_w = scale * (((float) TAPECTRL_W) - (2.0f * ((float) SEEKER_MARGINS_PX)));

        /* FIXME? old code to deal with elapsed_1200ths=-1 ... is this still needed or not? */
        if ( tcw->elapsed_1200ths < 0 ) {
            tcw->elapsed_1200ths = dur;
        }
        
        if (tcw->elapsed_1200ths > dur) {
            log_warn("tapectrl: seek: WARNING: elapsed_1200ths (%d) > duration (%d), clamping",
                     tcw->elapsed_1200ths, dur);
            tcw->elapsed_1200ths = dur;
        }
        scrub_x = (((float)tcw->elapsed_1200ths) * seeker_w) / (float) dur;
        
        e = guithread_paint_seeker_stripes(tcw, margin_x, margin_y, scale);
        if (TAPE_E_OK != e) { return e; }

        scrub_x += (scale * (float) SEEKER_MARGINS_PX);

        for (v=0; v < (SEEKER_KNOB_NUM_VERTICES * 2); v+=2) {
            knob_vx[v]   = margin_x + scrub_x + (scale *   (float) seeker_knob_vertices[v]  );
            knob_vx[v+1] = margin_y +           (scale * (((float) seeker_knob_vertices[v+1]) + (float) (SEEKER_Y - (SEEKER_H / 2))));
        }
        
        al_draw_polygon (knob_vx,
                         SEEKER_KNOB_NUM_VERTICES,
                         ALLEGRO_LINE_JOIN_ROUND,
                         al_map_rgb(COLOUR_BLACK_BORDER),
                         scale*2.0f,
                         0.0f);

        al_draw_filled_polygon (knob_vx,
                                SEEKER_KNOB_NUM_VERTICES,
                                al_map_rgb(COLOUR_SEEKER_KNOB));
    }

    /* 2. VFD PANEL */
    
    /* Always draw this, even if there is a prevailing error */
    /* TOHv4.4 */
    al_draw_filled_rectangle (margin_x + (scale * (TAPECTRL_VFD_PANEL_X)),
                              margin_y + (scale * (TAPECTRL_VFD_PANEL_Y)),
                              margin_x + (scale * (TAPECTRL_VFD_PANEL_X + TAPECTRL_VFD_PANEL_W)),
                              margin_y + (scale * (TAPECTRL_VFD_PANEL_Y + TAPECTRL_VFD_PANEL_H)),
                              al_map_rgb(COLOUR_VFD_PANEL_BACKGROUND));
    al_draw_rectangle (margin_x + (scale * (TAPECTRL_VFD_PANEL_X)),
                       margin_y + (scale * (TAPECTRL_VFD_PANEL_Y)),
                       margin_x + (scale * (TAPECTRL_VFD_PANEL_X + TAPECTRL_VFD_PANEL_W)),
                       margin_y + (scale * (TAPECTRL_VFD_PANEL_Y + TAPECTRL_VFD_PANEL_H)),
                       al_map_rgb(COLOUR_RECT_OUTLINE),
                       RECT_OUTLINE_W_PX_FLT * scale);
    
    /* 3. TIME */
    
    guithread_update_time (tcw->elapsed_1200ths,
                            dur,
                            scale,
                            margin_x,
                            margin_y,
                            tcw->reported_error);
    
    /* 4. PLAY TRIANGLE */
    
    bool fill_play_triangle;
    
    fill_play_triangle = tcw->motor;
    
    if ( ! flash_on ) { fill_play_triangle = false; }

    if (ok) {
        draw_icon(fill_play_triangle, /* TOHv4.4 */
                  TAPECTRL_ICON_ID_PLAY,
                  margin_x + (scale * (LAMP_PLAY_X)),
                  margin_y + (scale * (TAPECTRL_VFD_PANEL_ICON_CENTRE_Y)),
                  scale,
                  al_map_rgb(COLOUR_LAMP_PLAY), /* fill */
                  al_map_rgb(COLOUR_LAMP_PLAY_OUTLINE), /* outline */
                  al_map_rgb(COLOUR_BG), /* bgc */
                  al_map_rgb(COLOUR_BUTTON_HIGHLIGHT), /* hi */
                  draw_lamp_outlines);
    } /* endif (ok) */
    
    /* 5. RECORD CIRCLE */
    
    if (ok) {
        draw_icon(tcw->record_activated,
                  TAPECTRL_ICON_ID_RECORD,
                  margin_x + (scale * (float) (LAMP_RECORD_X)), 
                  margin_y + (scale * (float) (TAPECTRL_VFD_PANEL_ICON_CENTRE_Y)),
                  scale,
                  al_map_rgb(COLOUR_LAMP_REC),
                  al_map_rgb(COLOUR_LAMP_REC_OUTLINE),
                  al_map_rgb(COLOUR_BG),
                  al_map_rgb(COLOUR_BUTTON_HIGHLIGHT), /* hi */
                  draw_lamp_outlines);
    }
    
    /* 6. INLAY SCAN BORDER */

    square_f = (float) INLAY_SQUARE_SIZE;

    if (ok) {
        al_draw_filled_rectangle (margin_x + (scale * ((-RECT_OUTLINE_W_PX_FLT) + (float)(INLAY_X))),
                                  margin_y + (scale * ((-RECT_OUTLINE_W_PX_FLT) + (float)(INLAY_Y))),
                                  margin_x + (scale * ((square_f) + (float)(INLAY_X))),
                                  margin_y + (scale * ((square_f) + (float)(INLAY_Y))),
                                  al_map_rgb(COLOUR_INLAY_FILL));
        al_draw_rectangle (margin_x + (scale * ((-RECT_OUTLINE_W_PX_FLT) + (float)(INLAY_X))),
                           margin_y + (scale * ((-RECT_OUTLINE_W_PX_FLT) + (float)(INLAY_Y))),
                           margin_x + (scale * ((square_f) + (float)(INLAY_X))),
                           margin_y + (scale * ((square_f) + (float)(INLAY_Y))),
                           al_map_rgb(COLOUR_RECT_OUTLINE),
                           scale * RECT_OUTLINE_W_PX_FLT);
    }

    /* 7. INLAY SCAN IMAGE */

    if (ok && (tcw->num_inlays > 0)) {

        if (tcw->current_inlay >= tcw->num_inlays) {
            tcw->current_inlay = 0;
        }
        
        bm = tcw->inlays[tcw->current_inlay];
        
        bm_w = (float) al_get_bitmap_width  (bm);
        bm_h = (float) al_get_bitmap_height (bm);
        
        max_dim = (bm_w > bm_h) ? bm_w : bm_h;
        
        large = (max_dim > square_f);
        
        if (large) {
            if (bm_w > bm_h) {
                inlay_w = scale * square_f;
                inlay_h = scale * ((bm_h * square_f) / bm_w);
            } else {
                inlay_w = scale * ((bm_w * square_f) / bm_h);
                inlay_h = scale * square_f;
            }
        } else {
            /* both dimensions are smaller than the frame
               image appears 1:1, centred in both X and Y */
            inlay_w = scale * bm_w;
            inlay_h = scale * bm_h;
        }
        
        al_draw_scaled_bitmap(bm,
                              0,
                              0,
                              bm_w,
                              bm_h,
                              margin_x + (scale * ( ((float)INLAY_X) + ((square_f-bm_w)/2.0f))),
                              margin_y + (scale * ( ((float)INLAY_Y) + ((square_f-bm_h)/2.0f))),
                              inlay_w,
                              inlay_h,
                              0);
    } /* endif (ok) */
    
    /* 8. BUTTONS */
    button_icon_scale = 1.0f;
    for (i=0; i < TAPECTRL_NUM_BUTTONS; i++) {
        
        int brx0, bry0, brx1, bry1;
        
        /* don't draw anything except load and eject if reported error exists */
        if ( (TAPECTRL_BUTTON_IX_EJECT != i) && (TAPECTRL_BUTTON_IX_LOAD != i) && ! ok ) {
            continue;
        }
        
        brx0 = margin_x + (scale * button_x_position(i) - RECT_OUTLINE_W_PX_FLT);
        bry0 = margin_y + (scale * button_y_position(i) - RECT_OUTLINE_W_PX_FLT);
        brx1 = margin_x + (scale * (button_x_position(i) + TAPECTRL_BUTTON_W + RECT_OUTLINE_W_PX_FLT));
        bry1 = margin_y + (scale * (button_y_position(i) + TAPECTRL_BUTTON_H + RECT_OUTLINE_W_PX_FLT));
    
        al_draw_filled_rectangle (brx0, bry0, brx1, bry1, al_map_rgb(COLOUR_BUTTON_BACKGROUND));
        al_draw_rectangle        (brx0, bry0, brx1, bry1, al_map_rgb(COLOUR_BUTTON_OUTLINE), RECT_OUTLINE_W_PX_FLT * scale);

        /* TOHv4.4: buttons farmed out hard to generic icon function */
        draw_icon (true,
                   i,
                   margin_x + (scale * (button_x_position(i) + (0.5f * TAPECTRL_BUTTON_W))),
                   margin_y + (scale * (button_y_position(i) + (0.5f * TAPECTRL_BUTTON_H))),
                   scale * button_icon_scale,
                   al_map_rgb(COLOUR_BUTTON_FILL),       // fill
                   al_map_rgb(COLOUR_BUTTON_OUTLINE),    // outline
                   al_map_rgb(COLOUR_BUTTON_BACKGROUND), // background
                  al_map_rgb(COLOUR_BUTTON_HIGHLIGHT), // hi
                   false);
    }
    
    /* 9. BOTTOM RECTANGLES */

    if (ok) {
      
        /* turbo rectangle: */
        
        f = TAPECTRL_FIXED_MARGINS + TAPECTRL_TURBO_RECT_W;
        float y_top;
        y_top = (SEEKER_Y + TAPECTRL_BOTTOM_RECTS_SPACER);
        al_draw_rounded_rectangle (margin_x + scale * TAPECTRL_FIXED_MARGINS,
                                   margin_y + scale * y_top,
                                   margin_x + scale * f, //(TAPECTRL_FIXED_MARGINS + TAPECTRL_TURBO_RECT_W),
                                   margin_y + scale * (TAPECTRL_H - TAPECTRL_FIXED_MARGINS), //(SEEKER_Y + TAPECTRL_BOTTOM_RECTS_SPACER + TAPECTRL_BOTTOM_RECTS_H),
                                   TAPECTRL_DECAL_RECT_ROUNDED_RADIUS*scale, TAPECTRL_DECAL_RECT_ROUNDED_RADIUS*scale,
                                   al_map_rgb(COLOUR_DECAL),
                                   TAPECTRL_DECAL_RECT_THICKNESS * scale);
        /* this cuts the hole for the text in the top bar of the rectangle */
        al_draw_filled_rectangle (margin_x + scale * (TAPECTRL_FIXED_MARGINS + TAPECTRL_SPACER_1),
                                  margin_y + scale * (y_top - (1.0f + (TAPECTRL_LABEL_HEIGHT / 2.0f))),
                                  margin_x + scale * (TAPECTRL_FIXED_MARGINS + TAPECTRL_SPACER_1 + 59.0f),
                                  margin_y + scale * (y_top + (1.0f + (TAPECTRL_LABEL_HEIGHT / 2.0f))),
                                  al_map_rgb(COLOUR_BG));
        /* draw the label */
        al_draw_scaled_bitmap (tcw->labels[TAPECTRL_LABEL_IX_TURBO],
                               0.0f, 0.0f,
                               TAPECTRL_LABEL_WIDTH, TAPECTRL_LABEL_HEIGHT, // source -- not scaled!
                               margin_x + scale * (TAPECTRL_FIXED_MARGINS + TAPECTRL_SPACER_1 + 10.0f),
                               margin_y + scale * (y_top - (0.0f + (TAPECTRL_LABEL_HEIGHT / 2.0f))),
                               lb_w * scale, lb_h * scale,
                               0);
        /* draw the two turbo lamps */
        paint_lamp (margin_x,
                    margin_y,
                    scale,
                    TAPECTRL_BUTTON_STRIP_X + (TAPECTRL_BUTTON_W/2.0f),
                    TAPECTRL_BOTTOM_ROWS_UPPER_LHS_Y,
                    draw_lamp_outlines,
                    tcw->strip_silence_and_leader);
        paint_lamp (margin_x,
                    margin_y,
                    scale,
                    (TAPECTRL_BUTTON_OVERCLOCK_X + (TAPECTRL_BUTTON_W/2.0f)),
                    TAPECTRL_BOTTOM_ROWS_UPPER_LHS_Y,
                    draw_lamp_outlines,
                    tcw->overclock);
        
        /* serial rectangle: */
        f += TAPECTRL_SPACER_1;
        /* this will stretch to fill the remaining gap widthwise */
        float serial_rect_w;
        serial_rect_w = (TAPECTRL_W - TAPECTRL_FIXED_MARGINS) - f; // needed later
        al_draw_rounded_rectangle (margin_x + scale * f,
                                   margin_y + scale * y_top,
                                   margin_x + scale * (TAPECTRL_W - TAPECTRL_FIXED_MARGINS), //(TAPECTRL_FIXED_MARGINS + TAPECTRL_TURBO_RECT_W),
                                   margin_y + scale * (TAPECTRL_H - TAPECTRL_FIXED_MARGINS), //(SEEKER_Y + TAPECTRL_BOTTOM_RECTS_SPACER + TAPECTRL_BOTTOM_RECTS_H),
                                   TAPECTRL_DECAL_RECT_ROUNDED_RADIUS*scale, TAPECTRL_DECAL_RECT_ROUNDED_RADIUS*scale,
                                   al_map_rgb(COLOUR_DECAL),
                                   TAPECTRL_DECAL_RECT_THICKNESS * scale);
        /* this cuts the hole for the text in the top bar of the rectangle */
        al_draw_filled_rectangle (margin_x + scale * (f + TAPECTRL_SPACER_1),
                                  margin_y + scale * (y_top - (1.0f + (TAPECTRL_LABEL_HEIGHT / 2.0f))),
                                  margin_x + scale * (f + TAPECTRL_SPACER_1 + 60.0f),
                                  margin_y + scale * (y_top + (1.0f + (TAPECTRL_LABEL_HEIGHT / 2.0f))),
                                  al_map_rgb(COLOUR_BG));
        /* draw the label */
        al_draw_scaled_bitmap (tcw->labels[TAPECTRL_LABEL_IX_SERIAL],
                               0.0f, 0.0f,
                               TAPECTRL_LABEL_WIDTH, TAPECTRL_LABEL_HEIGHT, // source -- not scaled!
                               margin_x + scale * (f + TAPECTRL_SPACER_1 + 10.0f),
                               margin_y + scale * (y_top - (0.0f + (TAPECTRL_LABEL_HEIGHT / 2.0f))),
                               lb_w * scale, lb_h * scale,
                               0);
        /* draw the lamp for 300 baud */
        float serial_led_gap = serial_rect_w / 5.0f;
        f += serial_led_gap;
        paint_lamp (margin_x,
                    margin_y,
                    scale,
                    f,
                    TAPECTRL_BOTTOM_ROWS_UPPER_RHS_Y,
                    draw_lamp_outlines,
                    tcw->baud300);
        /* draw the label for 300 baud */
        al_draw_scaled_bitmap (tcw->labels[TAPECTRL_LABEL_IX_300_BAUD],
                               0.0f, 0.0f,
                               TAPECTRL_LABEL_WIDTH, TAPECTRL_LABEL_HEIGHT, // source -- not scaled!
                               margin_x + scale * (f - 33.0f), // centred manually ...
                               margin_y + scale * (TAPECTRL_BOTTOM_ROWS_LOWER_RHS_Y - (TAPECTRL_LABEL_HEIGHT/2.0f)), //margin_y + scale * (y_top - (0.0f + (TAPECTRL_LABEL_HEIGHT / 2.0f))),
                               lb_w * scale, lb_h * scale,
                               0);
        /* draw the lamp for tone on */
        f += serial_led_gap;
        paint_lamp (margin_x,
                    margin_y,
                    scale,
                    f,
                    TAPECTRL_BOTTOM_ROWS_UPPER_RHS_Y,
                    draw_lamp_outlines,
                    tcw->have_signal);
        /* draw the label for tone on */
        al_draw_scaled_bitmap (tcw->labels[TAPECTRL_LABEL_IX_TONE],
                               0.0f, 0.0f,
                               TAPECTRL_LABEL_WIDTH, TAPECTRL_LABEL_HEIGHT, // source -- not scaled!
                               margin_x + scale * (f - 16.0f), // centred manually ...
                               margin_y + scale * (TAPECTRL_BOTTOM_ROWS_LOWER_RHS_Y - (TAPECTRL_LABEL_HEIGHT/2.0f)), //margin_y + scale * (y_top - (0.0f + (TAPECTRL_LABEL_HEIGHT / 2.0f))),
                               lb_w * scale, lb_h * scale,
                               0);
        
        /* draw the lamp for data */
        f += serial_led_gap; //f += TAPECTRL_SERIAL_LAMP_GAP_X;
        paint_lamp (margin_x,
                    margin_y,
                    scale,
                    f,
                    TAPECTRL_BOTTOM_ROWS_UPPER_RHS_Y,
                    draw_lamp_outlines,
                    tcw->have_data);
        /* draw the label for data */
        al_draw_scaled_bitmap (tcw->labels[TAPECTRL_LABEL_IX_DATA],
                               0.0f, 0.0f,
                               TAPECTRL_LABEL_WIDTH, TAPECTRL_LABEL_HEIGHT, // source -- not scaled!
                               margin_x + scale * (f - 17.0f), // centred manually ...
                               margin_y + scale * (TAPECTRL_BOTTOM_ROWS_LOWER_RHS_Y - (TAPECTRL_LABEL_HEIGHT/2.0f)), //margin_y + scale * (y_top - (0.0f + (TAPECTRL_LABEL_HEIGHT / 2.0f))),
                               lb_w * scale, lb_h * scale,
                               0);
        
        
        /* draw the lamp for DCD */
        f += serial_led_gap;
        paint_lamp (margin_x,
                    margin_y,
                    scale,
                    f,
                    TAPECTRL_BOTTOM_ROWS_UPPER_RHS_Y,
                    draw_lamp_outlines,
                    tcw->dcd);
        /* draw the label for DCD */
        al_draw_scaled_bitmap (tcw->labels[TAPECTRL_LABEL_IX_DCD],
                               0.0f, 0.0f,
                               TAPECTRL_LABEL_WIDTH, TAPECTRL_LABEL_HEIGHT, // source -- not scaled!
                               margin_x + scale * (f - 17.0f), // centred manually ...
                               margin_y + scale * (TAPECTRL_BOTTOM_ROWS_LOWER_RHS_Y - (TAPECTRL_LABEL_HEIGHT/2.0f)), //margin_y + scale * (y_top - (0.0f + (TAPECTRL_LABEL_HEIGHT / 2.0f))),
                               lb_w * scale, lb_h * scale,
                               0);
        
        
    }

    /* 10. VOLUME */

    if (ok) {
    
        al_draw_line (margin_x + (scale * TAPECTRL_VOLUME_X),
                      margin_y + (scale * TAPECTRL_VOLUME_Y),
                      margin_x + (scale * TAPECTRL_VOLUME_X),
                      margin_y + (scale * (TAPECTRL_VOLUME_Y + TAPECTRL_VOLUME_LEN)),
                      al_map_rgb(COLOUR_VOLUME_TRACK),
                      scale * (float) SEEKER_TRACK_WIDTH);
                      
        f = 1.0f - tcw->tapenoise_volume;
        
#ifdef BUILD_TAPE_SANITY
        if ((f<0.0f) || (f>1.0f) || isnan(f)) {
            log_warn("tapectrl: bug: volume slider fraction is defective: %f\n", f);
            return TAPE_E_BUG;
        }
#endif

        al_draw_filled_circle(margin_x + (scale * TAPECTRL_VOLUME_X),
                              margin_y + (scale * (TAPECTRL_VOLUME_Y + (f * TAPECTRL_VOLUME_LEN))),
                              scale * (1.0f+(KNOB_W / 2.0f)),
                              al_map_rgb(COLOUR_BLACK_BORDER));

        al_draw_filled_circle(margin_x + (scale * TAPECTRL_VOLUME_X),
                              margin_y + (scale * (TAPECTRL_VOLUME_Y + (f * TAPECTRL_VOLUME_LEN))),
                              scale * (KNOB_W / 2.0f),
                              al_map_rgb(COLOUR_SEEKER_KNOB));
                              
        /* draw tapenoise icon underneath */
        
        f = margin_y + scale * (TAPECTRL_VFD_PANEL_Y + TAPECTRL_VFD_PANEL_H + TAPECTRL_SPACER_1 + TAPECTRL_BUTTON_H);
                   
        /* draw decal border */
        al_draw_rounded_rectangle (margin_x + scale * (TAPECTRL_VFD_PANEL_X + TAPECTRL_VFD_PANEL_W + TAPECTRL_SPACER_1),
                                   margin_y + scale * (TAPECTRL_FIXED_MARGINS),
                                   margin_x + scale * (TAPECTRL_W - TAPECTRL_FIXED_MARGINS),
                                   f,
                                   scale * TAPECTRL_DECAL_RECT_ROUNDED_RADIUS,
                                   scale * TAPECTRL_DECAL_RECT_ROUNDED_RADIUS,
                                   al_map_rgb(COLOUR_DECAL),
                                   scale * TAPECTRL_DECAL_RECT_THICKNESS);
                 
        /* cut hole in decal border ... */
        al_draw_filled_rectangle (margin_x + (scale * (TAPECTRL_VOLUME_X - 22.0f)),
                                  f - 8.0f,
                                  margin_x + (scale * (TAPECTRL_VOLUME_X + 22.0f)),
                                  f + 8.0f,
                                  al_map_rgb(COLOUR_BG));
                                   
        draw_icon (true,
                   TAPECTRL_ICON_ID_TAPENOISE,
                   margin_x + (scale * (TAPECTRL_VOLUME_X - 4.0f)),
                   /* place it alongside the middle button row */
                   f,
                   scale,
                   al_map_rgb(COLOUR_BUTTON_FILL),      /* fill */
                   al_map_rgb(COLOUR_BG),               /* outline */
                   al_map_rgb(COLOUR_BG),               /* background */
                   al_map_rgb(COLOUR_BUTTON_HIGHLIGHT), /* hi */
                   false);
                   

    }

    al_flip_display();

    return TAPE_E_OK;

}



void tapectrl_set_record (tape_ctrl_window_t * const tcw,
                          bool const activated,
                          int32_t const duration_1200ths) {
    /* mutex will be NULL if 'set record activated' is arriving from the command-line,
     * so test for that */
    if (tcw->mutex != NULL) {
        tapectrl_to_gui_msg_record        (tcw, true, false, activated);
        tapectrl_set_gui_rapid_value_time (tcw, false, true, duration_1200ths);
    }
}


/* TOHv4.3-a3: signal, EOF and time messages have been replaced with
 * just setting some variables on tcw. */
int tapectrl_set_gui_rapid_value_signal (tape_ctrl_window_t * const tcw,
                                         bool const need_lock,
                                         bool const need_unlock,
                                         char const tonecode) {
    if ('\0' == tonecode) {
        log_warn("tapectrl: BUG: set signal rapid value: tonecode is nil\n");
        return TAPE_E_BUG;
    }
    if (need_lock)   { TAPECTRL_LOCK_MUTEX(tcw->mutex);   }
    if (tcw->display != NULL) {
        tcw->gui_rapid_values.tone       = tonecode;
        tcw->gui_rapid_values.tone_ready = true;
    }
    if (need_unlock) { TAPECTRL_UNLOCK_MUTEX(tcw->mutex); }
    return TAPE_E_OK;
}

/* TOHv4.4 */
void tapectrl_to_gui_msg_tapenoise (tape_ctrl_window_t * const tcw,
                                    bool const need_lock,
                                    bool const need_unlock,
                                    float const volume) {
    tape_ctrl_msg_to_gui_t msg;
    msg.type = TAPECTRL_TO_GUI_TAPENOISE;
    msg.volume = volume;
    if (need_lock)            { TAPECTRL_LOCK_MUTEX(tcw->mutex);   }
    if (tcw->display != NULL) { queue_to_gui_msg(tcw, &msg);       }
    if (need_unlock)          { TAPECTRL_UNLOCK_MUTEX(tcw->mutex); }
}

/* TOHv4.4 */
void tapectrl_to_gui_msg_overclock (tape_ctrl_window_t * const tcw,
                                    bool const need_lock,
                                    bool const need_unlock,
                                    bool const overclock_activated) {
    tape_ctrl_msg_to_gui_t msg;
    msg.type = TAPECTRL_TO_GUI_OVERCLOCK;
    msg.overclock = overclock_activated;
    if (need_lock)            { TAPECTRL_LOCK_MUTEX(tcw->mutex);   }
    if (tcw->display != NULL) { queue_to_gui_msg(tcw, &msg);       }
    if (need_unlock)          { TAPECTRL_UNLOCK_MUTEX(tcw->mutex); }
}

/* TOHv4.4 */
void tapectrl_to_gui_msg_strip (tape_ctrl_window_t * const tcw,
                                bool const need_lock,
                                bool const need_unlock,
                                bool const strip_activated) {
    tape_ctrl_msg_to_gui_t msg;
    msg.type = TAPECTRL_TO_GUI_STRIP;
    msg.strip = strip_activated;
    if (need_lock)            { TAPECTRL_LOCK_MUTEX(tcw->mutex);   }
    if (tcw->display != NULL) { queue_to_gui_msg(tcw, &msg);       }
    if (need_unlock)          { TAPECTRL_UNLOCK_MUTEX(tcw->mutex); }
}

void tapectrl_to_gui_msg_eof (tape_ctrl_window_t * const tcw,
                              bool const need_lock,
                              bool const need_unlock,
                              bool const end) {
    tape_ctrl_msg_to_gui_t msg;
    msg.type = TAPECTRL_TO_GUI_EOF;
    msg.eof = end;
    if (need_lock)            { TAPECTRL_LOCK_MUTEX(tcw->mutex);   }
    if (tcw->display != NULL) { queue_to_gui_msg(tcw, &msg);       }
    if (need_unlock)          { TAPECTRL_UNLOCK_MUTEX(tcw->mutex); }
}

void tapectrl_set_gui_rapid_value_time (tape_ctrl_window_t * const tcw,
                                        bool const need_lock,
                                        bool const need_unlock,
                                        int32_t const elapsed_1200ths) {
    if (need_lock)   { TAPECTRL_LOCK_MUTEX(tcw->mutex);   }
    if (NULL != tcw->display) {
        /* When user performs a seek, the seeker marker is updated immediately
         * by the tapectrl thread. It also sends a from_gui _SEEK message.
         * However, before the main thread processes the seek message, it may
         * update the time on the tapectrl by sending it a to_gui message.
         * This will contain the old time rather than the new one requested
         * by the user, leading to a harmless but annoying visual glitch.
         *
         * Combat this by allowing the "do seek click" routine on tapectrl
         * to suppress the next time update being sent by the main thread:
         */
        if (tcw->gui_rapid_values.suppress_next_rapid_time_value) {       /* TOHv4.3-a4  */
            tcw->gui_rapid_values.suppress_next_rapid_time_value = false; /* cancel this */
        } else { /* otherwise, actually send the update */
            tcw->gui_rapid_values.elapsed_1200ths = elapsed_1200ths;
            tcw->gui_rapid_values.time_ready      = true;
        }
    }
    if (need_unlock) { TAPECTRL_UNLOCK_MUTEX(tcw->mutex); }
}

void tapectrl_to_gui_msg_dcd (tape_ctrl_window_t * const tcw,
                              bool const need_lock,
                              bool const need_unlock) {
    tape_ctrl_msg_to_gui_t msg;
    msg.type = TAPECTRL_TO_GUI_DCD;
    if (need_lock) {
        TAPECTRL_LOCK_MUTEX(tcw->mutex);
    }
    if (NULL != tcw->display) { /* check it again */
        queue_to_gui_msg(tcw, &msg);
    }
    if (need_unlock) {
        TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
    }
}


/* send record state to tapectrl thread */
void tapectrl_to_gui_msg_record (tape_ctrl_window_t * const tcw,
                                 bool const need_lock,
                                 bool const need_unlock,
                                 bool const rec) {
    tape_ctrl_msg_to_gui_t msg;
    msg.type = TAPECTRL_TO_GUI_RECORD;
    msg.rec  = rec;
    if (need_lock  )          { TAPECTRL_LOCK_MUTEX(tcw->mutex);   }
    if (tcw->display != NULL) { queue_to_gui_msg(tcw, &msg);       }
    if (need_unlock)          { TAPECTRL_UNLOCK_MUTEX(tcw->mutex); }
}

/* send motor state to tapectrl thread */
void tapectrl_to_gui_msg_motor (tape_ctrl_window_t * const tcw,
                                bool const need_lock,
                                bool const need_unlock,
                                bool const motor) {
    tape_ctrl_msg_to_gui_t msg;
    msg.type = TAPECTRL_TO_GUI_MOTOR;
    msg.motor = motor;
    if (need_lock) { TAPECTRL_LOCK_MUTEX(tcw->mutex); }
    if (tcw->display != NULL) {
        queue_to_gui_msg(tcw, &msg);
    }
    if (need_unlock) { TAPECTRL_UNLOCK_MUTEX(tcw->mutex); }
}

/* TOHv4.3-a3 */
int tapectrl_to_gui_msg_error (tape_ctrl_window_t * const tcw,
                                bool const need_lock,
                                bool const need_unlock,
                                int const error) {
    tape_ctrl_msg_to_gui_t msg;
    if (TAPE_E_EOF == error) {
        log_warn("tapectrl: BUG: Attempt to send to_gui error message of type _EOF");
        return TAPE_E_BUG;
    }
    msg.type = TAPECTRL_TO_GUI_ERROR;
    msg.error = error;
    if (need_lock)            { TAPECTRL_LOCK_MUTEX(tcw->mutex);   }
    if (tcw->display != NULL) { queue_to_gui_msg(tcw, &msg);       }
    if (need_unlock)          { TAPECTRL_UNLOCK_MUTEX(tcw->mutex); }
    return TAPE_E_OK;
}

int tapectrl_to_gui_msg_inlays_2 (tape_ctrl_window_t * const tcw,
                                  bool const need_lock,
                                  bool const need_unlock,
                                  int32_t const num_scans,
                                  uef_inlay_scan_t * const scans) {
    tape_ctrl_msg_to_gui_t msg;
    msg.type = TAPECTRL_TO_GUI_INLAYS;
    msg.inlays.fill = num_scans;
    msg.inlays.scans = scans;     /* permanently steal the allocation */
    if (need_lock)            { TAPECTRL_LOCK_MUTEX(tcw->mutex);   }
    if (tcw->display != NULL) { queue_to_gui_msg(tcw, &msg);       }
    if (need_unlock)          { TAPECTRL_UNLOCK_MUTEX(tcw->mutex); }
    return TAPE_E_OK;
}

int tapectrl_to_gui_msg_stripes (tape_ctrl_window_t * const tcw,
                                 bool const need_lock,
                                 bool const need_unlock,
                                 const tape_interval_list_t * const intervals) { /* this will be from tape_vars */

    tape_ctrl_msg_to_gui_t msg;
    int e;

    msg.type = TAPECTRL_TO_GUI_STRIPES;

    if (NULL == intervals) {
        memset(&(msg.stripes), 0, sizeof(tape_interval_list_t));
    } else {
        /* Allocate a fresh copy of the master intervals list from tape_vars.
         * A pointer to this copy will be passed in the to_gui message, and
         * the allocation will be managed henceforth by the tapectrl thread. */
        e = tape_interval_list_clone (&(msg.stripes), intervals);
        if (TAPE_E_OK != e) { return e; }
    }

    if (need_lock)            { TAPECTRL_LOCK_MUTEX(tcw->mutex);   }
    if (tcw->display != NULL) { queue_to_gui_msg(tcw, &msg);       }
    if (need_unlock)          { TAPECTRL_UNLOCK_MUTEX(tcw->mutex); }

    return TAPE_E_OK;

}

void tapectrl_to_gui_msg_baud (tape_ctrl_window_t * const tcw,
                                bool const need_lock,
                                bool const need_unlock,
                                bool const baud300) {
    tape_ctrl_msg_to_gui_t msg;
    msg.type = TAPECTRL_TO_GUI_BAUD;
    msg.baud300 = baud300;
    if (need_lock)            { TAPECTRL_LOCK_MUTEX(tcw->mutex);   }
    if (tcw->display != NULL) { queue_to_gui_msg(tcw, &msg);       }
    if (need_unlock)          { TAPECTRL_UNLOCK_MUTEX(tcw->mutex); }
}

static int guithread_update_time (int32_t const elapsed_1200ths,
                                  int32_t const duration_1200ths, /* TODO: not currently used */
                                  float const scale,
                                  float const margin_xin,
                                  float const margin_yin,
                                  int const reported_error) {
    ALLEGRO_COLOR ca;
    int ok;
    int hrs,mins,secs;
    ok = (reported_error == TAPE_E_OK);
    ca     = ok ? al_map_rgb(COLOUR_7SEG) : al_map_rgb(COLOUR_ERROR);

    hrs = mins = secs = 0;
    if (ok) {
        to_hours_minutes_seconds(elapsed_1200ths, &hrs, &mins, &secs);
    } else {
        secs = reported_error % 100;
        mins = (reported_error / 100) % 60;
    }
    draw_7seg(ca, hrs, mins, secs, scale, margin_xin, margin_yin, reported_error==TAPE_E_OK);  /* draw new */
    return TAPE_E_OK;
}

/* don't bother protecting this w/paint_mutex; only runs on tapectrl init */
static int init_labels (tape_ctrl_window_t * const tcw) {
    int e,i;
    e = TAPE_E_OK;
    finish_labels(tcw);
    /* needs to be MEMORY_BITMAP on macOS, or bad things occur */
    al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP);
    for (i=0; i < TAPECTRL_NUM_LABELS; i++) {
        ALLEGRO_BITMAP *ab;
        ab = al_create_bitmap(TAPECTRL_LABEL_WIDTH, TAPECTRL_LABEL_HEIGHT);
        if (NULL == ab) {
            log_warn("tapectrl init: al_create_bitmap for label %d failed", i);
            e = TAPE_E_ALLEGRO_CREATE_BITMAP;
            break;
        }
        e = plot_label_to_bitmap (ab, tapectrl_labels[i]); /* convert 2bpp -> native */
        if (TAPE_E_OK != e) { break; }
        tcw->labels[i] = ab;
    }
    if (TAPE_E_OK != e) {
        /* clean up */
        finish_labels(tcw);
    }
    return e;
}



static int
guithread_init_intervals (tape_ctrl_window_t * const tcw,
                          /* This is the copy on the to_gui message.
                           * We will take it. */
                          tape_interval_list_t * const interval_list_or_null) {

    int e;

    e = TAPE_E_OK;

    tape_interval_list_finish(&(tcw->interval_list));

    if (NULL == interval_list_or_null) {
        memset(&(tcw->interval_list), 0, sizeof(tape_interval_list_t));
    } else {
        tcw->interval_list = *interval_list_or_null; /* take the copy from the to_gui msg */
    }

    return e;
}



static int guithread_init_inlays (tape_ctrl_window_t * const tcw,
                                  uint32_t const num_scans,
                                  uef_inlay_scan_t * const scans) {

    int e;
    uint32_t u;
    
    /* found by valgrind testing: malloc(0) */
    if ( 0 == num_scans ) {
        finish_inlays(tcw);
        return TAPE_E_OK;
    }

    e = TAPE_E_OK;

    do {
    
        finish_inlays(tcw);

        /* rely on bound on num global chunks to make this sane */
        tcw->inlays = malloc(num_scans * sizeof(ALLEGRO_BITMAP *));
        if (NULL == tcw->inlays) {
            log_warn("tapectrl init: out of memory allocating tapectrl inlays");
            e = TAPE_E_MALLOC;
            break;
        }

        al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP); /* hopefully avoid macOS chaos */

        for (u=0; u < num_scans; u++) {

            uef_inlay_scan_t *scan;
            uint32_t x, y, k;
            ALLEGRO_BITMAP *bitmap;
            ALLEGRO_LOCKED_REGION *region;

            scan = scans + u;

            bitmap = al_create_bitmap(scan->w, scan->h);

            if (NULL == bitmap) {
                log_warn("tapectrl init: al_create_bitmap for inlay %u failed", u);
                e = TAPE_E_ALLEGRO_CREATE_BITMAP;
                break;
            }

            region = al_lock_bitmap(bitmap, ALLEGRO_PIXEL_FORMAT_RGBA_8888, ALLEGRO_LOCK_WRITEONLY);
            if (NULL == region) {
                log_warn("tapectrl: al_lock_bitmap failed");
                e = TAPE_E_ALLEGRO_LOCK_BITMAP;
                break;
            }

            al_set_target_bitmap(bitmap);
            al_reset_clipping_rectangle();

            if (scan->bpp != 8) {
                log_warn("tapectrl: WARNING: skipping inlay scan #%u having unsupported bpp (%u)\n",
                        u, scan->bpp);
                al_draw_filled_rectangle (0.0f,0.0f,INLAY_SQUARE_SIZE,INLAY_SQUARE_SIZE,al_map_rgb(255,255,0));
                al_draw_line (0.0f,0.0f,INLAY_SQUARE_SIZE,INLAY_SQUARE_SIZE,al_map_rgb(255,0,255),INLAY_SQUARE_SIZE/10.0f);
                al_draw_line (INLAY_SQUARE_SIZE,0.0f,0.0f,INLAY_SQUARE_SIZE,al_map_rgb(255,0,255),INLAY_SQUARE_SIZE/10.0f);
            } else {
                for (y=0, k=0; y < scan->h; y++) {
                    for (x=0; x < scan->w; x++, k+=(scan->bpp/8)) {
                        uint8_t b,g,r;
                        uint32_t offset;
                        ALLEGRO_COLOR col;
                        b=g=r=0;
                        offset = (0xff&((uint32_t)scan->body[k])) * 3;
                        if (scan->palette != NULL) {
                            b = scan->palette[0 + offset];
                            g = scan->palette[1 + offset];
                            r = scan->palette[2 + offset];
                        } else if (scan->grey) {
                            b = g = r = scan->body[k];
                        }
                        col = al_map_rgb(r,g,b);
                        al_put_pixel(x,y,col);
                    }
                }
            }
            al_unlock_bitmap(bitmap);
            al_set_target_backbuffer(al_get_current_display());
            tcw->inlays[u] = bitmap;
        }
        if (TAPE_E_OK != e) {
            finish_inlays(tcw);
            break;
        }
        tcw->num_inlays = num_scans;
        
    } while (0);

    return e;
}

/* main thread spawns tapectrl thread */
int tapectrl_start_gui_thread (tape_state_t * const ts,
                               tape_vars_t  * const tv,
                               bool const can_resize,
                               float const scale) {

    int e;
    int32_t d;
    tape_ctrl_window_t *tcw;
    e = TAPE_E_OK;
    d = 0;
    
    tcw = &(tv->tapectrl);

    if (tv->tapectrl_opened) {
        log_warn("tapectrl: BUG: start thread: tapectrl_opened already set!");
        return TAPE_E_BUG;
    }

    if (NULL == tcw->mutex) {
        log_warn("tapectrl: BUG: start thread: mutex is NULL");
        return TAPE_E_BUG;
    }

    if ( TAPE_E_OK == ts->prior_exception ) { /* TOHv4.3-a2: gate this */
        e = tape_get_duration_1200ths (ts, &d);
        if (TAPE_E_OK != e) { return e; }
    }

    e = init_labels(tcw);
    if (TAPE_E_OK != e) { return e; }

    /* render inlay scans to native surfaces */
    tcw->num_inlays = 0;

    /* we can call this from the main thread, since the GUI thread isn't running yet */
    if (    (ts->filetype_bits & TAPE_FILETYPE_BITS_UEF)
         && (ts->uef.globals.num_inlay_scans > 0)) { /* bugfix: only if we have scans */

        e = guithread_init_inlays(tcw,
                                  ts->uef.globals.num_inlay_scans,
                                  ts->uef.globals.inlay_scans);
        if (TAPE_E_OK != e) { return e; }

    }

    /* we can call this from the main thread, since the GUI thread isn't running yet */
    e = guithread_init_intervals(tcw, NULL); //&(tv->interval_list));
    if (TAPE_E_OK != e) {
        finish_inlays(tcw);
        return e;
    }

    /* before starting the thread, get the tcw state right */
    tcw->record_activated = tv->record_activated;
    tcw->tapenoise_volume = sound_tape_volume_fraction;
    tcw->strip_silence_and_leader = tv->strip_silence_and_leader;
    tcw->overclock = tv->overclock;

    if (tv->record_activated) {
        tcw->elapsed_1200ths = d;
        ts->tallied_1200ths = d;
    }

    /* copy these flags onto tcw for access by the GUI thread. */
    tcw->can_resize = can_resize;
    tcw->scale = scale;
    
    /* make it so that mainthread will now have to lock the mutex
     * any time it wants to access protected variables: */
    tv->tapectrl_opened = true;

    TAPECTRL_LOCK_MUTEX(tcw->mutex);

    /* Start the thread. */
    if ( tcw->shut_tapectrl_down ) {
        e = TAPE_E_TAPECTRL_THREAD_SHUTTING_DOWN;
    } else if ( NULL == tcw->display ) {
        tcw->thread = al_create_thread (tape_ctrl_guithread_main, tv);
        if (NULL == tcw->thread) {
            e = TAPE_E_TAPECTRL_CREATE_THREAD;
            tv->tapectrl_opened = false;
        } else {
            al_start_thread(tcw->thread);
        }
    } else {
        e = TAPE_E_TAPECTRL_THREAD_EXISTS;
    }

    if ((TAPE_E_OK != e) && (TAPE_E_TAPECTRL_THREAD_EXISTS != e)) {
        finish_inlays(tcw);
        tape_interval_list_finish(&(tv->interval_list));
    }

    TAPECTRL_UNLOCK_MUTEX(tcw->mutex);

    return e;

}

static int plot_label_to_bitmap (ALLEGRO_BITMAP * const ab, const char * const label) {

    int p,y;
    ALLEGRO_LOCKED_REGION *region;

    if (NULL == ab) {
        log_warn("tapectrl: BUG: plot_label_to_bitmap called w/NULL bitmap");
        return TAPE_E_BUG;
    }

    al_set_target_bitmap(ab);

    region = al_lock_bitmap(ab, ALLEGRO_PIXEL_FORMAT_RGBA_8888, ALLEGRO_LOCK_WRITEONLY);
    if (NULL == region) {
        log_warn("tapectrl: al_lock_bitmap failed");
        return TAPE_E_ALLEGRO_LOCK_BITMAP;
    }

    for (y=0, p=0; y<TAPECTRL_LABEL_HEIGHT; y++) {
        int x;
        /* each byte is 4 pixels */
        for (x=0; x<TAPECTRL_LABEL_WIDTH; x+=4, p++) {
            int z;
            uint8_t u;
            u = label[p];
            for (z=0; z<4; z++, u<<=2) {
                uint8_t b;
                b = u&0xc0;
                al_put_pixel(z+x, y, al_map_rgba(b,b,b,0));
            }
        }
    }

    al_unlock_bitmap(ab);

    return TAPE_E_OK;

}



static int draw_digit (uint8_t const d,
                       float const x, /* x, y already include margin_x or margin_y */
                       float const y,
                       float scale,
                       ALLEGRO_COLOR const c,
                       bool const colon) {

    uint8_t segs_bits;
    float tilt, x1, x2, y1, y2, bw;
    
    /* Segments are thus:

     1-
   2|  |4
     8-
  16|  |32
     64         */
     
    segs_bits = 0;
     
    if (0==d) {
        segs_bits = 1 + 2 + 4 + 16 + 32 + 64;
    } else if (1==d) {
        segs_bits = 4 + 32;
    } else if (2==d) {
        segs_bits = 1 + 4 + 8 + 16 + 64;
    } else if (3==d) {
        segs_bits = 1 + 4 + 8 + 32 + 64;
    } else if (4==d) {
        segs_bits = 2 + 4 + 8 + 32;
    } else if (5==d) {
        segs_bits = 1 + 2 + 8 + 32 + 64;
    } else if (6==d) {
        segs_bits = 1 + 2 + 8 + 16 + 32 + 64;
    } else if (7==d) {
        segs_bits = 1 + 4 + 32;
    } else if (8==d) {
        segs_bits = 1 + 2 + 4 + 8 + 16 + 32 + 64;
    } else if (9==d) {
        segs_bits = 1 + 2 + 4 + 8 + 32 + 64;
    }
    
    bw = scale * SEVENSEG_BAR_WIDTH;
    
    if (segs_bits & 1) {
        tilt = scale * ((2.0f * SEVENSEG_BAR_TILT_X) + SEVENSEG_BAR_TOP_ROWS_TILT_FUDGE_X);
        x1 = x + tilt + (scale * SEVENSEG_BAR_TIP_GAP);
        y1 = y;
        x2 = x + tilt + (scale * (SEVENSEG_BAR_LEN - SEVENSEG_BAR_TIP_GAP));
        y2 = y;
        al_draw_line (x1, y1, x2, y2, c, bw);
    }
    
    if (segs_bits & 2) {
        tilt = scale * (SEVENSEG_BAR_TILT_X + SEVENSEG_BAR_TOP_ROWS_TILT_FUDGE_X);
        x1 = x + tilt + (scale * SEVENSEG_BAR_TILT_X);
        y1 = y + (scale * SEVENSEG_BAR_TIP_GAP);
        x2 = x + tilt;
        y2 = y + (scale * (SEVENSEG_BAR_LEN - SEVENSEG_BAR_TIP_GAP));
        al_draw_line (x1, y1, x2, y2, c, bw);
    }
    
    if (segs_bits & 4) {
        tilt = scale * (SEVENSEG_BAR_TILT_X + SEVENSEG_BAR_TOP_ROWS_TILT_FUDGE_X);
        x1 = x + tilt + (scale * (SEVENSEG_BAR_TILT_X + SEVENSEG_BAR_LEN));
        y1 = y + (scale * SEVENSEG_BAR_TIP_GAP);
        x2 = x + tilt + (scale * SEVENSEG_BAR_LEN);
        y2 = y + (scale * (SEVENSEG_BAR_LEN - SEVENSEG_BAR_TIP_GAP));
        al_draw_line (x1, y1, x2, y2, c, bw);
    }
    
    if (segs_bits & 8) {
        tilt = scale * SEVENSEG_BAR_TILT_X;
        x1 = x + tilt + (scale * SEVENSEG_BAR_TIP_GAP);
        y1 = y + (scale * SEVENSEG_BAR_LEN);
        x2 = x + tilt + (scale * (SEVENSEG_BAR_LEN - SEVENSEG_BAR_TIP_GAP));
        y2 = y + (scale * SEVENSEG_BAR_LEN);
        al_draw_line (x1, y1, x2, y2, c, bw);
    }
    
    if (segs_bits & 16) {
        tilt = 0;
        x1 = x + tilt;
        y2 = y + (scale * (SEVENSEG_BAR_TIP_GAP + SEVENSEG_BAR_LEN));
        x2 = x + tilt + (scale * SEVENSEG_BAR_TILT_X);
        y1 = y + (scale * ((SEVENSEG_BAR_LEN * 2) - SEVENSEG_BAR_TIP_GAP));
        al_draw_line (x1, y1, x2, y2, c, bw);
    }
    
    if (segs_bits & 32) {
        tilt = 0;
        x1 = x + tilt + (scale * (SEVENSEG_BAR_LEN + SEVENSEG_BAR_TILT_X));
        y1 = y + (scale * (SEVENSEG_BAR_TIP_GAP + SEVENSEG_BAR_LEN));
        x2 = x + tilt + (scale * SEVENSEG_BAR_LEN);
        y2 = y + (scale * ((SEVENSEG_BAR_LEN*2.0f) - SEVENSEG_BAR_TIP_GAP));
        al_draw_line (x1, y1, x2, y2, c, bw);
    }
    
    if (segs_bits & 64) {
        tilt = 0;
        x1 = x + tilt + (scale * SEVENSEG_BAR_TIP_GAP);
        y1 = y + (scale*2.0f*SEVENSEG_BAR_LEN);
        x2 = x + tilt + (scale * (SEVENSEG_BAR_LEN - SEVENSEG_BAR_TIP_GAP));
        y2 = y + (scale*2.0f*SEVENSEG_BAR_LEN);
        al_draw_line (x1, y1, x2, y2, c, bw);
    }
    
    if (colon) {
        tilt = (scale * SEVENSEG_BAR_TILT_X) / 2.0f;
        x1 = x + tilt + (scale * SEVENSEG_DIGIT_WIDTH);
        y1 = y + ((scale * SEVENSEG_BAR_LEN * 3.0f) / 2.0f);
        x2 = x1 + bw;
        y2 = y1;
        al_draw_line (x1, y1, x2, y2, c, bw);
        
        tilt = scale * ((SEVENSEG_BAR_TILT_X * 3.0f) / 2.0f); /* now three-halves */
        x1 = tilt + x + (scale * SEVENSEG_DIGIT_WIDTH);
        y1 = y + ((scale * SEVENSEG_BAR_LEN) / 2.0f);
        x2 = x1 + bw;
        y2 = y1;
        al_draw_line (x1, y1, x2, y2, c, bw);
    }
    
    return TAPE_E_OK;
                 
}





static int draw_7seg (ALLEGRO_COLOR const c,
                      int const h,
                      int const m,
                      int const s,
                      float const scale,
                      float const margin_xin,
                      float const margin_yin,
                      bool const hrs_and_colons) {

    int i;
    float x;
    
    for (i=0, x=0.0f; i<5; i++, x += (scale * SEVENSEG_DIGIT_WIDTH)) { /* 0:12:34 */
        int d;
        if ((0==i)&&!hrs_and_colons) { continue; }
        if ((1==i) || (3==i)) {
            x += (scale * SEVENSEG_SEPARATOR_WIDTH); /* leave space for colon */
        }
        d = 0;
        if (0==i) {
            d = h;
        } else if (1==i) {
            d = m/10;
        } else if (2==i) {
            d = m%10;
        } else if (3==i) {
            d = s/10;
        } else if (4==i) {
            d = s%10;
        }
        draw_digit (d,
                    margin_xin + (x + (scale * SEVENSEG_X)),
                    margin_yin +      (scale * SEVENSEG_Y),
                    scale,
                    c,
                    hrs_and_colons&&((0==i)||(2==i)));
    }

    return TAPE_E_OK;
    
}


/* main thread */
int tapectrl_eject (tape_ctrl_window_t * const tcw) {
    int e;
    TAPECTRL_LOCK_MUTEX(tcw->mutex);
    e = tapectrl_set_gui_rapid_value_signal (tcw, false, false, 'S');
    if (TAPE_E_OK != e) {
        TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
        return e;
    }
    tapectrl_to_gui_msg_eof(tcw, false, false, true); /* send msg to tapectrl GUI */
    e = tapectrl_to_gui_msg_error (tcw, false, false, TAPE_E_OK);
    if (TAPE_E_OK == e) {
        e = tapectrl_to_gui_msg_inlays_2 (tcw, false, false, 0, NULL);
    }
    if (TAPE_E_OK == e) {
        e = tapectrl_to_gui_msg_stripes (tcw, false, false, NULL);
    }
    finish_inlays(tcw);
    tape_interval_list_finish(&(tcw->interval_list)); /* the TCW copy of the intervals, not the one on tape_vars_t */
    TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
    return e;
}


void _tapectrl_lock_mutex (ALLEGRO_MUTEX * const m) {
    al_lock_mutex(m);
}

void _tapectrl_unlock_mutex (ALLEGRO_MUTEX * const m) {
    al_unlock_mutex(m);
}

/* caution: this will lock+unlock the mutex */
int send_tone_to_tapectrl_maybe (tape_ctrl_window_t * const tcw,
                                  int32_t const elapsed,
                                  int32_t const duration,
                                  char const tone,
                                  uint32_t * const since_last_tone_sent_to_gui_inout) {
    int e;
    e = TAPE_E_OK;
    /* limit rate of TIME and SIGNAL messages */
    if (*since_last_tone_sent_to_gui_inout > 100) {
        *since_last_tone_sent_to_gui_inout = 0;
        tapectrl_set_gui_rapid_value_time(tcw, true, false, elapsed);
        if (TAPE_E_OK == e) {
            e = tapectrl_set_gui_rapid_value_signal(tcw, false, true, tone);
        }
    } else {
        (*since_last_tone_sent_to_gui_inout)++;
    }
    return e;
}

static int guithread_paint_seeker_stripes (tape_ctrl_window_t * const tcw,
                                           double margin_x,
                                           double margin_y,
                                           float scale) {

    tape_interval_list_t *ivl;
    int32_t ivn;
    double next_x_off;
    int32_t dur;
    double dur_d;

    ivl = &(tcw->interval_list);

    dur = guithread_duration_from_intervals(&(tcw->interval_list));
    dur_d = (double) dur;

    for (ivn=0, next_x_off=0.0f; ivn < ivl->fill; ivn++) {

        double x_px, w_px, frac, extra_w;
        tape_interval_t *prev, *cur, *next;
        int32_t m;

        cur = ivl->list + ivn;

#ifdef BUILD_TAPE_SANITY
        if (cur->start_1200ths > dur) {
            log_warn("tapectrl: paint: BUG: interval %d start (%d) > duration (%d); skip painting",
                        ivn, cur->start_1200ths, dur); //tcw->duration_1200ths);
            return TAPE_E_BUG;
        }
#endif

        extra_w = 0.0;

        /* if both adjacent stripes are wider, widen the current stripe by 1 pixel.
         * This emphasises the thinner intervals, so that they should be discernible
         * in the seeker bar even if very short (at scale 1.0, at least). */
#ifdef BUILD_TAPE_TAPECTRL_WIDEN_THIN_STRIPES
        if ( (ivn>0) && (ivn < (ivl->fill - 1))) {
            prev = cur - 1;
            next = cur + 1;
            if ((prev->pos_1200ths > cur->pos_1200ths) && (next->pos_1200ths > cur->pos_1200ths)) {
                extra_w = 0.5 * scale;
            }
        }
#endif

        if (0==dur) {
            x_px = 0.0;
            w_px = 0.0;
        } else {
            frac = ((double)cur->start_1200ths) / dur_d;
            x_px = margin_x + next_x_off + (scale * (SEEKER_MARGINS_PX + (frac * (((double)TAPECTRL_W) - (2.0*(double)SEEKER_MARGINS_PX)))));

            frac = ((double)cur->pos_1200ths) / dur_d;
            w_px = extra_w + (scale * frac * (float) (TAPECTRL_W - (2.0*SEEKER_MARGINS_PX))) - next_x_off;
        }

        al_draw_line (x_px,
                      margin_y + (scale * (float) SEEKER_Y),
                      x_px + w_px + extra_w + (1.0*scale), /* +1 ensures no hairline cracks and will be probably painted over anyway */
                      margin_y + (scale * (float) SEEKER_Y),
                      interval_type_to_colour(ivl->list[ivn].type),
                      scale * (float) SEEKER_TRACK_WIDTH);

        
        /* TOHv4.4: &120 markers */
        for (m=0; m < cur->num_markers_fill; m++) {
            float arrow[ARROW_POLY_NUM_POINTS*2];
            float sz_x,sz_y,xx,yy;
            frac = (((double)cur->start_1200ths) + ((double) cur->markers[m].pos_1200ths)) / dur_d;
            x_px = margin_x + next_x_off + (scale * (SEEKER_MARGINS_PX + (frac * (((double)TAPECTRL_W) - (2.0*(double)SEEKER_MARGINS_PX)))));
            xx = x_px;
            yy = (margin_y + (scale * (float) (SEEKER_Y - 50))) + scale*70.0f;
            sz_x = 1.7f;
            sz_y = 3.0f;
            arrow[0]  = xx-scale*sz_x*3.0f;  arrow[1] = yy+scale*sz_y*0.0f; // far left
            arrow[2]  = xx-scale*sz_x*1.0f;  arrow[3] = yy+scale*sz_y*0.0f;
            arrow[4]  = xx-scale*sz_x*1.0f;  arrow[5] = yy+scale*sz_y*2.0f; // bottom 
            arrow[6]  = xx+scale*sz_x*1.0f;  arrow[7] = yy+scale*sz_y*2.0f;
            arrow[8]  = xx+scale*sz_x*1.0f;  arrow[9] = yy+scale*sz_y*0.0f;
            arrow[10] = xx+scale*sz_x*3.0f; arrow[11] = yy+scale*sz_y*0.0f; // far right
            arrow[12] = xx+scale*sz_x*0.0f; arrow[13] = yy-scale*sz_y*3.0f; // tip
/* int m;for(m=0;m<14;m+=2){printf("(%.1f, %.1f); ",arrow[m],arrow[m+1]);} printf("\n"); */
            al_draw_filled_polygon (arrow, ARROW_POLY_NUM_POINTS, al_map_rgb(COLOUR_MARKERS)); //255,0,255));
        }

        if (next_x_off > 0.001) {
            next_x_off = 0.0;
        }

        if (extra_w > 0.001) {
            next_x_off = 1.0 * scale;
        }

    }

    return TAPE_E_OK;

}

#endif /* BUILD_TAPE_TAPECTRL */
