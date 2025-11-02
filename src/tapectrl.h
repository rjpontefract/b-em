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

#ifndef __INC_TAPECTRL_H
#define __INC_TAPECTRL_H

/* this file new in TOHv4.3 */

#include <stdint.h>
#include <allegro5/allegro_primitives.h>

#include "tape2.h"

#define TAPECTRL_NUM_LAMPS_NEW 5
#define TAPECTRL_NUM_LABELS (TAPECTRL_NUM_LAMPS_NEW + 1)

#define TAPECTRL_FROM_GUI_NONE             0
#define TAPECTRL_FROM_GUI_SEEK             1
#define TAPECTRL_FROM_GUI_SEEK_AND_SET_REC 2
#define TAPECTRL_FROM_GUI_SEEK_RELEASED    3
#define TAPECTRL_FROM_GUI_LEFT_RELEASED    4
#define TAPECTRL_FROM_GUI_RIGHT_RELEASED   5
#define TAPECTRL_FROM_GUI_THREAD_STARTED   6

#define TAPECTRL_TO_GUI_NULL      0
/* #define TAPECTRL_TO_GUI_TIME      1 */
#define TAPECTRL_TO_GUI_EOF       2
#define TAPECTRL_TO_GUI_MOTOR     3
#define TAPECTRL_TO_GUI_RECORD    4
#define TAPECTRL_TO_GUI_BAUD      5
/* #define TAPECTRL_TO_GUI_SIGNAL    6 */
#define TAPECTRL_TO_GUI_DCD       7
#define TAPECTRL_TO_GUI_INLAYS    8
#define TAPECTRL_TO_GUI_STRIPES   9
#define TAPECTRL_TO_GUI_ERROR    10 /* error in main thread, inform the GUI of this (or cancel previous error) */

#define TAPE_SEEK_CLAMP_BACK_OFF_1200THS 120 /* 0.1 s */

#define TAPECTRL_MSG_QUEUE_SIZE   16

typedef struct tape_ctrl_msg_from_gui_s {

    /* messages from tapectrl GUI thread to main thread: */

    int type; /* TAPECTRL_FROM_GUI_... */

    union {
        struct { /* for TAPECTRL_FROM_GUI_SEEK_... */
            float fraction;
            bool held;
            bool left_held;
            bool right_held;
            bool record_activated; /* only for TAPECTRL_FROM_GUI_SEEK_AND_SET_REC variant */
        } seek;
        /* other event fields would go here */
    } data;
    
    bool ready;

    double timestamp; /* from allegro event */
    
} tape_ctrl_msg_from_gui_t;

typedef struct uef_inlay_scan_s uef_inlay_scan_t; /* fwdref */

#include "tape-interval.h"

typedef struct tape_ctrl_msg_to_gui_s {

    /* messages from main thread to tapectrl thread: */

    uint32_t type; /* TAPECTRL_TO_GUI_... */

    union {
        bool motor;     /* TAPECTRL_TO_GUI_MOTOR  */
        bool rec;       /* TAPECTRL_TO_GUI_RECORD */
        bool baud300;   /* TAPECTRL_TO_GUI_BAUD   */
        bool eof;       /* TAPECTRL_TO_GUI_EOF    */
        struct {
            int32_t fill;
            uef_inlay_scan_t *scans;
        } inlays;                      /* TAPECTRL_TO_GUI_INLAYS  */
        tape_interval_list_t stripes;  /* TAPECTRL_TO_GUI_STRIPES */
        int error;                     /* TAPECTRL_TO_GUI_ERROR   */
    };

} tape_ctrl_msg_to_gui_t;


typedef struct tape_ctrl_gui_rapid_values_s {
    volatile int32_t elapsed_1200ths;
    volatile bool time_ready;
    volatile bool suppress_next_rapid_time_value;
    volatile char tone;
    volatile bool tone_ready;
} tape_ctrl_gui_rapid_values_t;


typedef struct tape_ctrl_window_s {

    /* tapectrl GUI thread ONLY ... */
    ALLEGRO_THREAD  *thread;
    int32_t elapsed_1200ths;     /* displayed time */
    bool have_signal;            /* lamps */
    bool have_data;
    bool dcd;
    bool record_activated;
    double time_key_pressed;
    double time_last_key_repeat;
    bool motor;
    bool end_of_tape;
    bool baud300;
    ALLEGRO_BITMAP *labels[TAPECTRL_NUM_LABELS];
    float margin_x;
    float margin_y;
    float scale;
    bool can_resize;
    float dcd_on_start_time;     /* apply some persistence to the DCD lamp */
    int reported_error;          /* current main thread error; now also handles EOF conditions */
    ALLEGRO_BITMAP **inlays;
    uint32_t num_inlays;         /* copied from UEF globals */
    uint32_t current_inlay;
    tape_interval_list_t interval_list;

    /* EITHER thread may access these (via mutex) */
    ALLEGRO_MUTEX   *mutex;
    volatile ALLEGRO_DISPLAY *display; /* doubles as the "window is open" flag */
    volatile bool shut_tapectrl_down;
    volatile int from_gui_fill;
    volatile tape_ctrl_msg_from_gui_t from_gui[TAPECTRL_MSG_QUEUE_SIZE];
    volatile int to_gui_fill;
    volatile tape_ctrl_msg_to_gui_t   to_gui[TAPECTRL_MSG_QUEUE_SIZE];
    volatile int tapectrl_error;   /* GUI-side error (thread shuts down?) */
    /* TOHv4.3-a3: Time and signal messages have had to be replaced
     * by just locking the mutex and overwriting a value, as they were
     * saturating the traditional message queue mechanism. So there are
     * effectively two messaging mechanisms now, an infrequent one
     * and this frequent one: */
    volatile tape_ctrl_gui_rapid_values_t gui_rapid_values;

    /* main thread ONLY */
    bool seeking_left_held;
    bool seeking_right_held;
    double seeking_autorepeat_start_time;
    int32_t seeking_last_position_1200ths;
    /* If seeking using the GUI, then playback is inhibited until the
       mouse button or left/rightarrow is released again. */
    bool inhibited_by_gui;

} tape_ctrl_window_t;

#include <stdio.h>
#include "acia.h"
#include "uef.h"

/* fwdref */
typedef struct tape_state_s tape_state_t;

#ifdef BUILD_TAPE_TAPECTRL /* if not compiled in, keep the structures, but not the functions */

int tapectrl_handle_messages (tape_vars_t * const tv,
                              tape_state_t * const ts,
                              ACIA * const acia);

int tapectrl_start_gui_thread (tape_state_t * const ts,
                               tape_vars_t * const tv,
                               bool const can_resize,
                               float const initial_scale);
                                      
void tapectrl_finish (tape_ctrl_window_t * const tcw, bool * const tapectrl_opened_inout);

void tapectrl_close (tape_ctrl_window_t * const tcw, bool * const tapectrl_opened_inout);

void tapectrl_set_record (tape_ctrl_window_t * const tcw,
                          bool const activated,
                          int32_t const duration_1200ths);

/* functions for sending messages from main thread to GUI thread */
void tapectrl_to_gui_msg_record (tape_ctrl_window_t * const tcw,
                                bool const need_lock,
                                bool const need_unlock,
                                bool const rec);

void tapectrl_to_gui_msg_motor (tape_ctrl_window_t * const tcw,
                                bool const need_lock,
                                bool const need_unlock,
                                bool const motor);

void tapectrl_to_gui_msg_dcd (tape_ctrl_window_t * const tcw,
                              bool const need_lock,
                              bool const need_unlock);
                                // bool const dcd);

int tapectrl_to_gui_msg_stripes (tape_ctrl_window_t * const tcw,
                                 bool const need_lock,
                                 bool const need_unlock,
                                 const tape_interval_list_t * const intervals); /* from tape_vars.interval_list */

int tapectrl_to_gui_msg_inlays_2 (tape_ctrl_window_t * const tcw,
                                  bool const need_lock,
                                  bool const need_unlock,
                                  int32_t const num_scans,
                                  uef_inlay_scan_t * const scans); /* this will be stolen */

void tapectrl_to_gui_msg_baud (tape_ctrl_window_t * const tcw,
                                bool const need_lock,
                                bool const need_unlock,
                                bool const baud300) ;

int tapectrl_to_gui_msg_error (tape_ctrl_window_t * const tcw,
                                bool const need_lock,
                                bool const need_unlock,
                                int const error);

void tapectrl_to_gui_msg_eof (tape_ctrl_window_t * const tcw,
                              bool const need_lock,
                              bool const need_unlock,
                              bool const end);


/* TOHv4.3-a3: signal and time messages no longer use the queue.
 * They now use a separate "one-shot" mechanism, as they were
 * completely saturating the message queues previously. */
int tapectrl_set_gui_rapid_value_signal (tape_ctrl_window_t * const tcw,
                                         bool const need_lock,
                                         bool const need_unlock,
                                         char const tonecode);

void tapectrl_set_gui_rapid_value_time (tape_ctrl_window_t * const tcw,
                                        bool const need_lock,
                                        bool const need_unlock,
                                        int32_t const elapsed_1200ths);

int tapectrl_eject (tape_ctrl_window_t * const tcw);

int send_eof_to_tapectrl_if_changed (tape_ctrl_window_t * const tcw,
                                    int32_t const elapsed,
                                    int32_t const duration,
                                    char const tone,
                                    uint32_t * const since_last_tone_sent_to_gui_inout);

int send_tone_to_tapectrl_maybe (tape_ctrl_window_t * const tcw,
                                 int32_t const elapsed,
                                 int32_t const duration,
                                 char const tone,
                                 uint32_t * const since_last_tone_sent_to_gui_inout);

/* exported in TOHv4.3-a3 so that load_successful() can send _THREAD_STARTED to itself */
void tapectrl_queue_from_gui_msg (tape_ctrl_window_t * const tcw,
                                  const tape_ctrl_msg_from_gui_t * const msg);

#ifdef BUILD_TAPE_TAPECTRL_PRINT_MUTEX_LOCKS
#define TAPECTRL_LOCK_MUTEX(m)   _tapectrl_lock_mutex(m);   printf("  lock: %s:%s(%d)\n", __FILE__, __func__, __LINE__)
#define TAPECTRL_UNLOCK_MUTEX(m) _tapectrl_unlock_mutex(m); printf("unlock: %s:%s(%d)\n", __FILE__, __func__, __LINE__)
#elif defined BUILD_TAPE_TAPECTRL_LOG_MUTEX_LOCKS
#define TAPECTRL_LOCK_MUTEX(m)   _tapectrl_lock_mutex(m);   log_warn("  lock: %s:%s(%d)", __FILE__, __func__, __LINE__)
#define TAPECTRL_UNLOCK_MUTEX(m) _tapectrl_unlock_mutex(m); log_warn("unlock: %s:%s(%d)", __FILE__, __func__, __LINE__)
#else
#define TAPECTRL_LOCK_MUTEX(m)   _tapectrl_lock_mutex(m)
#define TAPECTRL_UNLOCK_MUTEX(m) _tapectrl_unlock_mutex(m)
#endif

/*
#define TAPECTRL_LOCK_MUTEX(m)
#define TAPECTRL_UNLOCK_MUTEX(m)
*/

void _tapectrl_lock_mutex   (ALLEGRO_MUTEX * const m);
void _tapectrl_unlock_mutex (ALLEGRO_MUTEX * const m);


#endif /* BUILD_TAPE_TAPECTRL */

#endif /* __INC_TAPECTRL_H */
