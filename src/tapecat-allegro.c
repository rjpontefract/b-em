#include "b-em.h"
#include <allegro5/allegro_native_dialog.h>
#include "tapecat-allegro.h"
#include "csw.h"
#include "uef.h"
#include "tape.h"

static ALLEGRO_TEXTLOG *textlog;
ALLEGRO_EVENT_SOURCE uevsrc;

void cataddname(char *s)
{
    if (textlog)
        al_append_native_text_log(textlog, "%s\n", s);
}

static void *tapecat_thread(ALLEGRO_THREAD *thread, void *tdata)
{
    ALLEGRO_EVENT_QUEUE *queue = al_create_event_queue();
    if (queue) {
        ALLEGRO_EVENT event;
        ALLEGRO_TEXTLOG *ltxtlog = (ALLEGRO_TEXTLOG *)tdata;
        al_init_user_event_source(&uevsrc);
        al_register_event_source(queue, &uevsrc);
        al_register_event_source(queue, al_get_native_text_log_event_source(ltxtlog));
        do {
            al_wait_for_event(queue, &event);
        } while (event.type != ALLEGRO_EVENT_NATIVE_DIALOG_CLOSE);
        textlog = NULL;                    // set the global that cataddname will see to NULL
        al_close_native_text_log(ltxtlog); // before destroying the textlog with our local copy.
        al_destroy_event_queue(queue);
    }
    return NULL;
}

static void start_cat(uint8_t enable_phantom_block_protection) /* TOHv3.3: parameter */
{
    findfilenames_new(&tape_state,
                      1,               /* TOHv3: "1" here means show UI */
                      enable_phantom_block_protection);
}

void gui_tapecat_start(void)
{
    if (textlog) {
        start_cat( ! tape_vars.disable_phantom_block_protection );
    } else {
        ALLEGRO_TEXTLOG *ltxtlog = al_open_native_text_log("B-Em Tape Catalogue", ALLEGRO_TEXTLOG_MONOSPACE);
        if (ltxtlog) {
            ALLEGRO_THREAD *thread = al_create_thread(tapecat_thread, ltxtlog);
            if (thread) {
                al_start_thread(thread);
                textlog = ltxtlog; // open to writes from cataddname.
                start_cat( ! tape_vars.disable_phantom_block_protection );
                return;
            }
            al_close_native_text_log(ltxtlog);
        }
    }
}

void gui_tapecat_close(void)
{
    if (textlog) {
        ALLEGRO_EVENT event;
        event.type = ALLEGRO_EVENT_NATIVE_DIALOG_CLOSE;
        al_emit_user_event(&uevsrc, &event, NULL);
    }
}
