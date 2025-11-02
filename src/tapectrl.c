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

/* this file new in TOHv4.3 */

#include "tape2.h"

#ifdef BUILD_TAPE_TAPECTRL

#include "tapectrl.h"

/* reduce this value to increase responsiveness, at cost of CPU */
#define TAPECTRL_PAINT_SLEEP_MS             10

#define TAPECTRL_W                          720
#define TAPECTRL_H                          286

#define RECT_OUTLINE_W_PX_FLT               (1.0f)

#define SEEKER_MARGINS_PX                   45
#define SEEKER_H                            14

#define SEEKER_Y                            (236)
#define SEEKER_TRACK_WIDTH                  9
#define SEEKER_CLICKZONE_Y                  45
#define KNOB_H (18.0f)
#define KNOB_W (16.0f)

#define IN_SCRUBZONE_Y(my,margin_y,scale) (    (((float)(my)) >= (((float)margin_y) + (scale*(SEEKER_Y - (SEEKER_CLICKZONE_Y / 2))))) \
                                            && (((float)(my)) <= (((float)margin_y) + (scale*(SEEKER_Y + (SEEKER_CLICKZONE_Y / 2))))))

#define SEVENSEG_X                          350
#define SEVENSEG_Y                          30

                                            /* +RECT_OUTLINE_W_PX_FLT to line up outside edge with seeker: */
#define INLAY_X                             (SEEKER_MARGINS_PX + ((int)RECT_OUTLINE_W_PX_FLT) - (KNOB_W / 2))
#define INLAY_Y                             SEVENSEG_Y
#define INLAY_SQUARE_SIZE                   160

#define LAMP_PLAY_X                         (SEVENSEG_X - 60)
#define LAMP_PLAY_Y                         (SEVENSEG_Y + (SEVENSEG_BAR_LEN/2))
#define LAMP_RECORD_X                       (LAMP_PLAY_X - 35)
#define LAMP_RECORD_Y                       (SEVENSEG_Y + SEVENSEG_BAR_LEN)

#define SEVENSEG_BAR_TIP_GAP                2.0
#define SEVENSEG_BAR_WIDTH                  3.0
#define SEVENSEG_BAR_LEN                    25
#define SEVENSEG_BAR_TILT_X                 7
#define SEVENSEG_BAR_TOP_ROWS_TILT_FUDGE_X  1 /* x-offset for segs 1, 2, 4 */
#define SEVENSEG_SMALL_GAP                  12
#define SEVENSEG_SEPARATOR_WIDTH            (SEVENSEG_BAR_WIDTH + SEVENSEG_SMALL_GAP)
#define SEVENSEG_DIGIT_WIDTH                (SEVENSEG_BAR_LEN   + SEVENSEG_SMALL_GAP)

#define TAPECTRL_KEY_REPEAT_ONSET_S         0.5
#define TAPECTRL_KEY_REPEAT_PERIOD_S        0.05

#define TAPECTRL_KEY_PRESS_SEEK_1200THS     6010 /* 5s */
#define TAPECTRL_KEY_HOLD_TICK_SEEK_1200THS 20000

#define TAPECTRL_LAMP_TOP_ROW_Y             (SEVENSEG_Y + 4)
#define TAPECTRL_LAMP_ROW_H                 36
#define TAPECTRL_LAMP_BULB_RADIUS           6
#define TAPECTRL_LAMPS_X                    (TAPECTRL_W - 90) /*(SEVENSEG_X + 240)*/
#define TAPECTRL_LAMP_LABELS_X              (TAPECTRL_LAMPS_X + TAPECTRL_LAMP_BULB_RADIUS + 10)

//#define TAPECTRL_REC_IS_UP
#define TAPECTRL_REC_X           222
#ifdef TAPECTRL_REC_IS_UP
#define TAPECTRL_REC_Y           90
#else
#define TAPECTRL_REC_Y           150
#endif

#define TAPECTRL_REC_W           60
#define TAPECTRL_REC_H           40
#define TAPECTRL_REC_LBL_OFF_X   15
#define TAPECTRL_REC_LBL_OFF_Y   12

#include "tapectrl-labels.h"

#define COLOUR_BG                24,24,24
#define COLOUR_LAMP_A            255,255,0
#define COLOUR_LAMP_OUTLINE      96,96,0
#define COLOUR_LAMP_PLAY         0,210,0
#define COLOUR_LAMP_PLAY_OUTLINE 0,96,0
#define COLOUR_LAMP_REC          210,0,0
#define COLOUR_LAMP_REC_OUTLINE  96,0,0
#define COLOUR_7SEG              COLOUR_LAMP_A
#define COLOUR_SEEKER_TRACK      25,0,120
#define COLOUR_SEEKER_KNOB       0xff,0xef,0x90
#define COLOUR_ERROR             255,0,0

#define COLOUR_RECT_OUTLINE 200,200,200

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
#define TAPECTRL_LABEL_WIDTH  40 /* was the -size parameter to imagemagick */
#define TAPECTRL_LABEL_HEIGHT 14

/*
#define TAPECTRL_IX_TONE   0
#define TAPECTRL_IX_DATA   1
#define TAPECTRL_IX_EOF    2
#define TAPECTRL_IX_300    3
#define TAPECTRL_IX_DCD    4
*/
#define TAPECTRL_IX_REC    5
static const char *tapectrl_labels[TAPECTRL_NUM_LABELS] = {
    TAPECTRL_LABEL_TONE,
    TAPECTRL_LABEL_DATA,
    TAPECTRL_LABEL_DCD,
    TAPECTRL_LABEL_300,
    TAPECTRL_LABEL_EOF,
    TAPECTRL_LABEL_REC
};

#include "tape.h"

static void finish_labels (tape_ctrl_window_t * const tcw);

static void queue_to_gui_msg (tape_ctrl_window_t * const tcw,
                              const tape_ctrl_msg_to_gui_t * const msg) ;

static int guithread_rec_button_pressed (tape_ctrl_window_t * const tcw,
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
                                 float const scale);

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
                                    
static int plot_label_to_bitmap (ALLEGRO_BITMAP * const ab, const char * const label);

static void finish_inlays (tape_ctrl_window_t * const tcw);

static int handle_mouse_button_down (int const mx,
                                     int const my,
                                     float const scale,
                                     float const margin_x,
                                     float const margin_y,
                                     bool const record_activated,
                                     /* outputs: */
                                     uint32_t * const current_inlay_inout,
                                     bool * const held_out,
                                     bool * const rec_button_pressed_out);

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


#define LAMP_PLAY_TRIANGLE_NUM_VERTICES 3
static const float lamp_play_triangle_vertices[2 * LAMP_PLAY_TRIANGLE_NUM_VERTICES] = {
    LAMP_PLAY_X,                    LAMP_PLAY_Y,
    LAMP_PLAY_X,                    LAMP_PLAY_Y + SEVENSEG_BAR_LEN,
    LAMP_PLAY_X + SEVENSEG_BAR_LEN, LAMP_PLAY_Y + (SEVENSEG_BAR_LEN / 2)
};

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
    if ((y<0) || IN_SCRUBZONE_Y(y,margin_y,scale)) {
        *f_out = (x - (margin_x + (scale * SEEKER_MARGINS_PX))) / seeker_w;
        if (*f_out > 1.0f) { *f_out = 1.0f; }
        if (*f_out < 0.0f) { *f_out = 0.0f; }
        return TAPE_E_OK;
    }
    return TAPE_E_TAPECTRL_OUTSIDE_ZONE;
}

#include "gui-allegro.h"
#include "tapeseek.h"
#include "taperead.h"

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
    my_eof = false;

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

        from_gui = from_gui_copy + msg_i;
        
        if ( ! from_gui->ready ) {
            log_warn("tapectrl: BUG: from_gui msg from queue does not have ready=true!");
            return TAPE_E_BUG;
        }

        /* FIXME: inhibited_by_gui is global when there should be three independent types
         *        for mouse, leftarrow, rightarrow -- otherwise can use combination of these
         *        controls to desync the state */

        /* record activation needs to work even if no tapetime exists yet */
        if  (    (TAPECTRL_FROM_GUI_SEEK_AND_SET_REC == from_gui->type)
              // && ! ts->disabled_due_to_error) { /* TOHv4.3-a3: disabled gate */
              && (TAPE_E_OK == ts->prior_exception) ) { /* TOHv4.3-a3: disabled gate */
            gui_set_record_mode (from_gui->data.seek.record_activated);
            e = tape_set_record_activated (ts,
                                           tv,
                                           acia,
                                           from_gui->data.seek.record_activated,
                                           tv->tapectrl_opened);
            if (TAPE_E_OK != e) { break; }
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
/*printf("tapectrl_to_gui_msg_eof(%u): %s:%s(%d)\n", my_eof, __FILE__, __func__, __LINE__);*/
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
        } /* endif (duration_1200ths > 0) */
        
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
/*printf("tapectrl_to_gui_msg_eof(%u): %s:%s(%d)\n", my_eof, __FILE__, __func__, __LINE__);*/
            tapectrl_to_gui_msg_error         (tcw, false, false, ts->prior_exception); /* TOHv4.3-a3 */
            tapectrl_to_gui_msg_record        (tcw, false, false, tv->record_activated);
            e = tapectrl_to_gui_msg_stripes   (tcw, false, false, &(tv->interval_list));
            if (TAPE_E_OK == e) {
                e = tapectrl_to_gui_msg_inlays_2 (tcw,
                                                  false,
                                                  false, /* unlock */
                                                  ts->uef.globals.num_inlay_scans,
                                                  ts->uef.globals.inlay_scans);
            }
            TAPECTRL_UNLOCK_MUTEX(tcw->mutex);
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
    
    // if (key_held_seek && ! ts->disabled_due_to_error) {
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
/*printf("tapectrl_to_gui_msg_eof(%u): %s:%s(%d)\n", my_eof, __FILE__, __func__, __LINE__);*/
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
    ALLEGRO_MUTEX *mutex_p;
    ALLEGRO_THREAD *thread_p;
    ALLEGRO_DISPLAY *tmpdisp;
    bool held;
    int flagz;
    bool can_resize;
    int win_w, win_h;

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
    held = false;
    just_started = true;

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

        memset(&(from_gui.data.seek), 0, sizeof(from_gui.data.seek));

        from_gui.ready = false;

        if (just_started) {
            /* request initial state from main thread */
            from_gui.type  = TAPECTRL_FROM_GUI_THREAD_STARTED;
            from_gui.ready = true;
        }

        /* 1. PREPARE OUTGOING EVENTS (from_gui) */
        // e.g. guithread_main_prepare_msgs_from_gui()

        if ( ! quit && ! al_is_event_queue_empty (eq) && ! just_started ) {

            int kc;
            bool rec_pressed;

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
                    // leftarrow_held = false;
                    from_gui.type = TAPECTRL_FROM_GUI_LEFT_RELEASED;
                    from_gui.data.seek.left_held = false;
                    from_gui.ready = true;
                } else if ((ALLEGRO_KEY_RIGHT == kc) && (ev.keyboard.display == tcw->display)) {
                    // rightarrow_held = false;
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
                rec_pressed = false;
                e = handle_mouse_button_down (mx,
                                              my,
                                              tcw->scale,
                                              tcw->margin_x,
                                              tcw->margin_y,
                                              tcw->record_activated,
                                              /* sets these variables: */
                                              &(tcw->current_inlay),
                                              &held,
                                              &rec_pressed);
                if (TAPE_E_OK != e) {
                    quit = true;
                    break;
                }
                if (rec_pressed) {
                    e = guithread_rec_button_pressed(tcw, &from_gui);
                }
            } else if ((ALLEGRO_EVENT_MOUSE_BUTTON_UP == ev.type) && (ev.mouse.display == tcw->display)) {
                from_gui.type = TAPECTRL_FROM_GUI_SEEK_RELEASED;
                from_gui.ready = true;
                held = false;
            } else if ((ALLEGRO_EVENT_MOUSE_ENTER_DISPLAY == ev.type) && (ev.mouse.display == tcw->display)) {

            } else if ((ALLEGRO_EVENT_MOUSE_LEAVE_DISPLAY == ev.type) && (ev.mouse.display == tcw->display)) {

            } else if ((ALLEGRO_EVENT_DISPLAY_RESIZE == ev.type) && (ev.display.source == tcw->display)) {
                recompute_margins(tcw, ev.display.width, ev.display.height);
                al_acknowledge_resize((ALLEGRO_DISPLAY *) tcw->display);
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
        // tcw->gui_rapid_values.eof_ready  = false;
        tcw->gui_rapid_values.tone_ready = false;
        
        TAPECTRL_UNLOCK_MUTEX (tcw->mutex);

        /* 3. HANDLE INCOMING EVENTS (to_gui)
         *    Working from a copy, so can now proceed without locks. */

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
                default:
                    break;

            } /* end switch (msg type) */
        } /* next msg, from to_gui queue */

        if (TAPE_E_OK != e) { break; }

        // 3b. RAPID VALUES

        if (rapid_vals.time_ready) {
            tcw->elapsed_1200ths = rapid_vals.elapsed_1200ths;
        }
        if (rapid_vals.tone_ready) {
            tcw->have_signal = (rapid_vals.tone != 'S');
            tcw->have_data   = (tcw->have_signal && rapid_vals.tone != 'L');
            msg_n++;
        }

        /* Hack: Clear the DCD indicator after a brief pulse. */
        if (tcw->dcd && (al_get_time() > (tcw->dcd_on_start_time + 0.06f))) {
            tcw->dcd = false;
            msg_n++;
        }

        /* 4. HANDLE 'HELD' CONDITION */

        /* Mouse button was pressed or is currently held */

        dur = guithread_duration_from_intervals(&(tcw->interval_list));

        if (held) {

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

        } /* endif (held) */

        // 5. PAINT

        e = guithread_main_paint(tcw,
                                 (tcw->margin_y > 0) ? tcw->margin_y : tcw->margin_x,
                                 tcw->margin_y > 0,
                                 tcw->scale);
        if (TAPE_E_OK != e) { break; }

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

    /* back up the mutex and the thread */
    mutex_p = tcw->mutex;
    thread_p = tcw->thread;

    tcw->mutex = mutex_p;  /* don't destroy the mutex */
    tcw->thread = thread_p; /* or the thread */

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

static int handle_mouse_button_down (int const mx,
                                     int const my,
                                     float const scale,
                                     float const margin_x,
                                     float const margin_y,
                                     bool const record_activated,
                                     uint32_t * const current_inlay_inout,
                                     bool * const held_out,
                                     bool * const rec_button_pressed_out) {
                              
    float f;
    int e;
    
    f = NAN;
    /* only seek if record is not activated */
    e = TAPE_E_OK;
    
    /* 1. SEEKER */
    
    if ( ! record_activated ) {
        e = seek_frac_from_mouse_xy (mx, my, margin_x, margin_y, scale, &f);
        if (TAPE_E_OK == e) { /* seek_frac_from_mouse_xy returns error if outside scrubzone */
            *held_out = true;
        }
    }
    
    /* 2. INLAY SCAN */
    
    if (    (mx > (margin_x + (scale * (float) INLAY_X)))
         && (mx < (margin_x + (scale * (float) (INLAY_X + INLAY_SQUARE_SIZE))))
         && (my > (margin_y + (scale * (float) INLAY_Y)))
         && (my < (margin_y + (scale * (float) (INLAY_Y + INLAY_SQUARE_SIZE))))) {
        /* if clicked, advance inlay scan (value validation is done later) */
        (*current_inlay_inout)++;
    }
    
    /* 3. RECORD BUTTON */
    
    if (    (mx > (margin_x + (scale * (float) TAPECTRL_REC_X)))
         && (mx < (margin_x + (scale * (float) (TAPECTRL_REC_X + TAPECTRL_REC_W))))
         && (my > (margin_y + (scale * (float) TAPECTRL_REC_Y)))
         && (my < (margin_y + (scale * (float) (TAPECTRL_REC_Y + TAPECTRL_REC_H))))) {
        *rec_button_pressed_out = true;
    }
    
    return TAPE_E_OK;
}



static ALLEGRO_COLOR interval_type_to_colour (uint8_t type) {
    if (TAPE_INTERVAL_TYPE_SILENCE == type) {
        return al_map_rgb(COLOUR_INTERVAL_SILENCE); /* navy */
    } else if (TAPE_INTERVAL_TYPE_LEADER == type) {
        return al_map_rgb(COLOUR_INTERVAL_LEADER); /* purple */
    }
    return al_map_rgb(COLOUR_INTERVAL_DATA); /* pink */
}

/* In testing on macOS, this redraw is incredibly crashy when run
 * from the tapectrl thread, and a resize happens.
 *
 * On Linux, it's better, but even here it may throw a video error
 * and hang the emulator. The 'nouveau' driver may be flaky. The
 * Official Nvidia Driver seems a bit better.
 * 
 * Windows looks OK I think although it may depend on hardware.
 *
 * Resizing behaviour is therefore now a boolean value at the
 * time of GUI thread creation. */

static int guithread_main_paint (tape_ctrl_window_t * const tcw,
                                 int const black_bar_w,
                                 bool const letterbox,
                                 float const scale) {

    int lbix, v;
    float bm_w, bm_h, lb_w, lb_h;
    ALLEGRO_BITMAP *bm;
    double margin_x, margin_y, seeker_w, scrub_x;
    float vertices[LAMP_PLAY_TRIANGLE_NUM_VERTICES * 2];
    float knob_vx[SEEKER_KNOB_NUM_VERTICES * 2];
    float lamp_x, lamp_y, square_f;
    float inlay_w, inlay_h, max_dim;
    bool large;
    bool draw_lamp_outlines;
    float row;
    ALLEGRO_COLOR ca, cb;
    bool ok;
    int e;
    int32_t dur; /* TOHv4.3-a3 */

    margin_x = (float) (letterbox ?           0 : black_bar_w);
    margin_y = (float) (letterbox ? black_bar_w :           0);
    
#ifdef WIN32
    Sleep(TAPECTRL_PAINT_SLEEP_MS);
#else
    usleep(1000 * TAPECTRL_PAINT_SLEEP_MS);
#endif

    ok = (TAPE_E_OK == tcw->reported_error);

    al_clear_to_color (al_map_rgba (COLOUR_BG, 0));
    
    /* 1. SEEKER */

    dur = guithread_duration_from_intervals(&(tcw->interval_list));

    if ( ok && ! tcw->record_activated ) { /* do not offer seeking in record mode */

        scrub_x = 0.0f;
        seeker_w = scale * (((float) TAPECTRL_W) - (2.0f * ((float) SEEKER_MARGINS_PX)));

        /* FIXME? old code to deal with elapsed_1200ths=-1 ... is this still needed or not? */
        if ( tcw->elapsed_1200ths < 0 ) {
            tcw->elapsed_1200ths = dur;
        }
        if (dur > 0) {
            if (tcw->elapsed_1200ths > dur) {
                log_warn("tapectrl: seek: WARNING: elapsed_1200ths (%d) > duration (%d), clamping",
                         tcw->elapsed_1200ths, dur);
                tcw->elapsed_1200ths = dur;
            }
            scrub_x = (tcw->elapsed_1200ths * seeker_w) / (float) dur;
        }

        e = guithread_paint_seeker_stripes(tcw, margin_x, margin_y, scale);
        if (TAPE_E_OK != e) { return e; }

        scrub_x += (scale * (float) SEEKER_MARGINS_PX);

        for (v=0; v < (SEEKER_KNOB_NUM_VERTICES * 2); v+=2) {
            knob_vx[v]   = margin_x + scrub_x + (scale *   (float) seeker_knob_vertices[v]  );
            knob_vx[v+1] = margin_y +           (scale * (((float) seeker_knob_vertices[v+1]) + (float) (SEEKER_Y - (SEEKER_H / 2))));
        }

        al_draw_filled_polygon (knob_vx,
                                SEEKER_KNOB_NUM_VERTICES,
                                al_map_rgb(COLOUR_SEEKER_KNOB));
    }

    
    /* 2. TIME */
    
    guithread_update_time (tcw->elapsed_1200ths,
                            dur, //tcw->duration_1200ths,
                            scale,
                            margin_x,
                            margin_y,
                            tcw->reported_error);

    lamp_x = margin_x + (scale * (float) TAPECTRL_LAMPS_X);
    lamp_y = margin_y + (scale * (float) TAPECTRL_LAMP_TOP_ROW_Y);

    
    /* 3. LAMPARRAY */

    if (ok) {
    
        draw_lamp_outlines = false;
#ifdef BUILD_TAPE_TAPECTRL_LAMP_OUTLINES
        draw_lamp_outlines = true;
#define OUTLINE_THICKNESS (1.0f)
#endif

        ca = al_map_rgb(COLOUR_LAMP_A);
        cb = al_map_rgb(COLOUR_LAMP_OUTLINE);

        row = 0.0f;
        if (tcw->have_signal) {
            al_draw_filled_circle (lamp_x,
                                lamp_y + (scale * row * (float) TAPECTRL_LAMP_ROW_H),
                                scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                                ca);
        } else if (draw_lamp_outlines) {
            al_draw_circle (lamp_x,
                            lamp_y + (scale * row * (float) TAPECTRL_LAMP_ROW_H),
                            scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                            cb,
                            OUTLINE_THICKNESS * scale);
        }

        row += 1.0f;
        if (tcw->have_data) {
            al_draw_filled_circle (lamp_x,
                                lamp_y + (scale * row * (float) TAPECTRL_LAMP_ROW_H),
                                scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                                ca);
        } else if (draw_lamp_outlines) {
            al_draw_circle (lamp_x,
                            lamp_y + (scale * row * (float) TAPECTRL_LAMP_ROW_H),
                            scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                            cb,
                            OUTLINE_THICKNESS * scale);
        }

        row += 1.0f;
        if (tcw->dcd) {
            al_draw_filled_circle (lamp_x,
                                lamp_y + (scale * row * (float) TAPECTRL_LAMP_ROW_H),
                                scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                                ca);
        } else if (draw_lamp_outlines) {
            al_draw_circle (lamp_x,
                                lamp_y + (scale * row * (float) TAPECTRL_LAMP_ROW_H),
                                scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                                cb,
                                OUTLINE_THICKNESS * scale);
        }

        row += 1.0f;
        if (tcw->baud300) {
            al_draw_filled_circle (lamp_x,
                                lamp_y + (scale * row * (float) TAPECTRL_LAMP_ROW_H),
                                scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                                ca);
        } else if (draw_lamp_outlines) {
            al_draw_circle (lamp_x,
                            lamp_y + (scale * row * (float) TAPECTRL_LAMP_ROW_H),
                            scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                            cb,
                            OUTLINE_THICKNESS * scale);
        }

        row += 1.0f;
        if (tcw->end_of_tape && ! tcw->record_activated) { /* record inhibits EOF */
            al_draw_filled_circle (lamp_x,
                                lamp_y + (scale * row * (float) TAPECTRL_LAMP_ROW_H),
                                scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                                ca);
        } else if (draw_lamp_outlines) {
            al_draw_circle (lamp_x,
                            lamp_y + (scale * row * (float) TAPECTRL_LAMP_ROW_H),
                            scale * (float) TAPECTRL_LAMP_BULB_RADIUS,
                            cb,
                            OUTLINE_THICKNESS * scale);
        }
    } /* endif (ok) */
    
    /* 4. PLAY TRIANGLE */

    if (ok) {

        for (v=0; v < (LAMP_PLAY_TRIANGLE_NUM_VERTICES * 2); v+=2) {
            vertices[v]   = margin_x + (scale * (float) lamp_play_triangle_vertices[v]  );
            vertices[v+1] = margin_y + (scale * (float) lamp_play_triangle_vertices[v+1]);
        }

        if (tcw->motor) {
            al_draw_filled_polygon (vertices,
                                    LAMP_PLAY_TRIANGLE_NUM_VERTICES, /* 3, natch */
                                    al_map_rgb(COLOUR_LAMP_PLAY));
        } else if (draw_lamp_outlines) {
            al_draw_polygon(vertices,
                            LAMP_PLAY_TRIANGLE_NUM_VERTICES,
                            ALLEGRO_LINE_JOIN_ROUND,
                            al_map_rgb(COLOUR_LAMP_PLAY_OUTLINE),
                            OUTLINE_THICKNESS * scale,
                            1.0f);
        }
    } /* endif (ok) */
    
    /* 5. RECORD CIRCLE */
    
    if (ok) {
        if (tcw->record_activated) {
            al_draw_filled_circle (margin_x + (scale * (float) LAMP_RECORD_X),
                                   margin_y + (scale * (float) LAMP_RECORD_Y),
                                   (scale * (float) SEVENSEG_BAR_LEN) / 2.0f,
                                   al_map_rgb(COLOUR_LAMP_REC));
        } else if (draw_lamp_outlines) {
            al_draw_circle (margin_x + (scale * (float) LAMP_RECORD_X),
                            margin_y + (scale * (float) LAMP_RECORD_Y),
                            (scale * (float) SEVENSEG_BAR_LEN) / 2.0f,
                            al_map_rgb(COLOUR_LAMP_REC_OUTLINE),
                            OUTLINE_THICKNESS * scale);
        }
    }
    
    /* 6. LABELS */
    
    if (ok) {

        lb_w = (float) TAPECTRL_LABEL_WIDTH;
        lb_h = (float) TAPECTRL_LABEL_HEIGHT;

        for (lbix=0; lbix < (TAPECTRL_NUM_LABELS - 1); lbix++) {
            float row_offset;
            float fudge;
            /* fudge: move labels in capitals down one pixel */
            fudge = ((2==lbix)||(3==lbix)) ? 2.0f : 3.0f;
            row_offset =   TAPECTRL_LAMP_TOP_ROW_Y
                         + (lbix * TAPECTRL_LAMP_ROW_H)
                         - (fudge + TAPECTRL_LAMP_BULB_RADIUS);  /* adjust the 3 */
            al_draw_scaled_bitmap (tcw->labels[lbix],
                                   0.0f, 0.0f,
                                   lb_w, lb_h,
                                   margin_x + (scale * (float) TAPECTRL_LAMP_LABELS_X),
                                   margin_y + (scale * row_offset),
                                   lb_w * scale, lb_h * scale,
                                   0);
        }

    }
    
    /* 7. INLAY SCAN BORDER */

    square_f = (float) INLAY_SQUARE_SIZE;

    if (ok) {
        al_draw_rectangle (margin_x + (scale * ((-RECT_OUTLINE_W_PX_FLT) + (float)(INLAY_X))),
                           margin_y + (scale * ((-RECT_OUTLINE_W_PX_FLT) + (float)(INLAY_Y))),
                           margin_x + (scale * ((square_f) + (float)(INLAY_X))),
                           margin_y + (scale * ((square_f) + (float)(INLAY_Y))),
                           al_map_rgb(COLOUR_RECT_OUTLINE),
                           scale * RECT_OUTLINE_W_PX_FLT);
    }

    /* 8. INLAY SCAN IMAGE */

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

    /* 9. RECORD BUTTON */
    if (ok) {
        al_draw_rectangle (margin_x + (scale * (float) TAPECTRL_REC_X),
                        margin_y + (scale * (float) TAPECTRL_REC_Y),
                        margin_x + (scale * (float) (TAPECTRL_REC_X + TAPECTRL_REC_W)),
                        margin_y + (scale * (float) (TAPECTRL_REC_Y + TAPECTRL_REC_H)),
                        al_map_rgb(COLOUR_RECT_OUTLINE),
                        RECT_OUTLINE_W_PX_FLT * scale);

        al_draw_scaled_bitmap (tcw->labels[TAPECTRL_IX_REC],
                            0.0f, 0.0f,
                            lb_w, lb_h,
                            margin_x + (scale * (float) (TAPECTRL_REC_X + TAPECTRL_REC_LBL_OFF_X)),
                            margin_y + (scale * (float) (TAPECTRL_REC_Y + TAPECTRL_REC_LBL_OFF_Y)),
                            lb_w * scale, lb_h * scale,
                            0);
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
        tapectrl_to_gui_msg_record       (tcw, true, false, activated);
        tapectrl_set_gui_rapid_value_time(tcw, false, true, duration_1200ths);
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
/*printf("tapectrl_to_gui_msg_stripes(): %d intervals.\n", intervals->fill);*/
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
    al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP); //ALLEGRO_VIDEO_BITMAP); //);
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

    /* if (!ts->disabled_due_to_error) { */ /* TOHv4.3-a2: gate this */
    if ( TAPE_E_OK == ts->prior_exception ) { /* TOHv4.3-a2: gate this */
        e = tape_get_duration_1200ths(ts, &d);
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

    // tcw->duration_1200ths = d;
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
/*printf("tapectrl_to_gui_msg_eof(%u): %s:%s(%d)\n", false, __FILE__, __func__, __LINE__);*/
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

    ivl = &(tcw->interval_list);

    dur = guithread_duration_from_intervals(&(tcw->interval_list));

    for (ivn=0, next_x_off=0.0f; ivn < ivl->fill; ivn++) {

        float x_px, w_px, frac, extra_w;
        tape_interval_t *prev, *cur, *next;

        cur = ivl->list + ivn;

#ifdef BUILD_TAPE_SANITY
        if (cur->start_1200ths > (dur)) {
            log_warn("tapectrl: paint: BUG: interval %d start (%d) > duration (%d); skip painting",
                        ivn, cur->start_1200ths, dur); //tcw->duration_1200ths);
            return TAPE_E_BUG;
        }
#endif

        extra_w = 0.0f;

        /* if both adjacent stripes are wider, widen the current stripe by 1 pixel.
         * This emphasises the thinner intervals, so that they should be discernible
         * in the seeker bar even if very short (at scale 1.0, at least). */
#ifdef BUILD_TAPE_TAPECTRL_WIDEN_THIN_STRIPES
        if ( (ivn>0) && (ivn < (ivl->fill - 1))) {
            prev = cur - 1;
            next = cur + 1;
            if ((prev->pos_1200ths > cur->pos_1200ths) && (next->pos_1200ths > cur->pos_1200ths)) {
                extra_w = 0.5f * scale;
            }
        }
#endif

        if (0==dur) {
            x_px=0;
            w_px=0;
        } else {
            frac = cur->start_1200ths / (float) dur;
            x_px = margin_x + next_x_off + (scale * (SEEKER_MARGINS_PX + (frac * (float) (TAPECTRL_W - (2*SEEKER_MARGINS_PX)))));

            frac = cur->pos_1200ths / (float) dur;
            w_px = extra_w + (scale * frac * (float) (TAPECTRL_W - (2*SEEKER_MARGINS_PX))) - next_x_off;
        }

        al_draw_line(x_px,
                    margin_y + (scale * (float) SEEKER_Y),
                    x_px + w_px + extra_w + 1, /* +1 ensures no hairline cracks and will be probably painted over anyway */
                    margin_y + (scale * (float) SEEKER_Y),
                    interval_type_to_colour(ivl->list[ivn].type),
                    scale * (float) SEEKER_TRACK_WIDTH);

        if (next_x_off > 0.001f) {
            next_x_off = 0.0f;
        }

        if (extra_w > 0.001f) {
            next_x_off = 1.0f * scale;
        }

    }

    return TAPE_E_OK;

}

#endif /* BUILD_TAPE_TAPECTRL */
