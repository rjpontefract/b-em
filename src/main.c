/*B-em v2.2 by Tom Walker
  Main loop + start/finish code*/

#include "b-em.h"
#include <allegro5/allegro_audio.h>
#include <allegro5/allegro_acodec.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_native_dialog.h>
#include <allegro5/allegro_primitives.h>

#include "6502.h"
#include "adc.h"
#include "model.h"
#include "cmos.h"
#include "config.h"
#include "csw.h"
#include "ddnoise.h"
#include "debugger.h"
#include "disc.h"
#include "fdi.h"
#include "hfe.h"
#include "gui-allegro.h"
#include "i8271.h"
#include "ide.h"
#include "joystick.h"
#include "keyboard.h"
#include "keydef-allegro.h"
#include "led.h"
#include "main.h"
#include "6809tube.h"
#include "mem.h"
#include "mmb.h"
#include "mouse.h"
#include "midi.h"
#include "music4000.h"
#include "music5000.h"
#include "mmccard.h"
#include "paula.h"
#include "pal.h"
#include "savestate.h"
#include "scsi.h"
#include "sdf.h"
#include "serial.h"
#include "sid_b-em.h"
#include "sn76489.h"
#include "sysacia.h"
#include "tape.h"
#include "tapecat-allegro.h"
#include "tapenoise.h"
#include "tube.h"
#include "via.h"
#include "sysvia.h"
#include "uef.h"
#include "uservia.h"
#include "vdfs.h"
#include "video.h"
#include "video_render.h"
#include "wd1770.h"

#include "tube.h"
#include "NS32016/32016.h"
#include "6502tube.h"
#include "65816.h"
#include "arm.h"
#include "x86_tube.h"
#include "z80.h"
#include "sprow.h"

#ifndef WIN32
#include <unistd.h>
#endif

#undef printf

bool quitting = false;
bool keydefining = false;
bool autopause = false;
bool autoskip = true;
bool skipover = false;
int autoboot=0;
int joybutton[4];
float joyaxes[4];
int emuspeed = 4;
bool tricky_sega_adapter = false;
/* TOHv3: although C exit code is an int, Unix shells don't safely allow
   you to use values > 125, so this is limited to a signed 8-bit value >:( */
int8_t shutdown_exit_code = SHUTDOWN_OK;

static ALLEGRO_TIMER *timer;
ALLEGRO_EVENT_QUEUE *queue;
static ALLEGRO_EVENT_SOURCE evsrc;

ALLEGRO_DISPLAY *tmp_display;

typedef enum {
    FSPEED_NONE,
    FSPEED_SELECTED,
    FSPEED_RUNNING
} fspeed_type_t;

static const int slice = 40000; // 8ms to match Music 5000.
static double time_limit;
static int fcount = 0;
static fspeed_type_t fullspeed = FSPEED_NONE;
static bool bempause  = false;

#define NUM_DEFAULT_SPEEDS 10

static const emu_speed_t default_speeds[NUM_DEFAULT_SPEEDS] = {
    {  "10%", 0.10, 1 },
    {  "25%", 0.25, 1 },
    {  "50%", 0.50, 1 },
    {  "75%", 0.75, 1 },
    { "100%",    1, 1 },
    { "150%", 1.50, 2 },
    { "200%", 2.00, 2 },
    { "300%", 3.00, 3 },
    { "400%", 4.00, 4 },
    { "500%", 5.00, 5 }
};

const emu_speed_t *emu_speeds = default_speeds;
int num_emu_speeds = NUM_DEFAULT_SPEEDS;
static int emu_speed_normal = 4;

void main_reset()
{
    m6502_reset();
    crtc_reset();
    video_reset();
    sysvia_reset();
    uservia_reset();
    serial_reset();
    wd1770_reset();
    i8271_reset();
    scsi_reset();
    vdfs_reset();
    sid_reset();
    music4000_reset();
    music5000_reset();
    paula_reset();
    sn_init();
    if (curtube != -1) tubes[curtube].cpu->reset();
    else               tube_exec = NULL;
    tube_reset();
}

static const char helptext[] =
    VERSION_STR " command line options:\n\n"
    "-mx             - start as model x (see readme.txt for models)\n"
    "-tx             - start with tube x (see readme.txt for tubes)\n"
    "-disc disc.ssd  - load disc.ssd into drives :0/:2\n"
    "-disc1 disc.ssd - load disc.ssd into drives :1/:3\n"
    "-autoboot       - boot disc in drive :0\n"
    "-tape tape.uef  - load tape.uef (or .csw/.tibet/.tibetz)\n"
    "-fasttape       - enable tape overclocking\n"
    "-Fx             - set maximum video frames skipped\n"
    "-s              - scanlines display mode\n"
    "-i              - interlace display mode\n"
// lovebug
    "-fullscreen     - fullscreen display mode\n"
// lovebug end
    "-spx            - Emulation speed x from 0 to 9 (default 4)\n"
    "-debug          - start debugger\n"
    "-debugtube      - start debugging tube processor\n"
    "-exec file      - debugger to execute file\n"
    "-paste string   - paste string in as if typed (via OS)\n"
    "-pastek string  - paste string in as if typed (via KB)\n"
    "-vroot host-dir - set the VDFS root\n"
    "-vdir guest-dir - set the initial (boot) dir in VDFS\n"
    "-rs423file file - write RS423 output to file\n"
    "-tapetest <n>   - bits: 1=quit @ EOF | 2=quit @ err | 4=cat @ boot |\n"             /* TOHv3, v4   */
    "                        8=cat @ end  | 16=slow startup\n"                           /* TOHv4.1 */
    "-record         - begin recording to tape at B-Em startup\n"                        /* TOHv3   */
    "-tapesave file  - save tape copy on exit (format set by extension)\n"               /* TOHv3   */
    "-tapeskip       - skip silence and leader when loading tapes (faster)\n"            /* TOHv3.2 */
    "-tape117        - when saving UEF, emit baud chunk &117 before every block\n"       /* TOHv3.2 */
    "-tape112        - when saving UEF, use chunk &112 rather than &116 for silence\n"   /* TOHv3.2 */
    "-tapeno0        - when appending to loaded UEF, do not emit origin chunk &0\n"      /* TOHv3.2 */
    "-expire <n>     - quit, with predictable exit code, after <n> emulated seconds\n"   /* TOHv3.3 */
    "-tapenopbp      - do not filter out phantom blocks on tape load\n"
    "\n";

static double main_calc_timer(int speed)
{
    double multiplier = emu_speeds[speed].multiplier;
    double secs = ((double)slice / 2000000.0) / multiplier;
    time_limit = secs * 2.0;
    log_debug("main: main_calc_timer for speed#%d, multiplier %g timer %gs", speed, multiplier, secs);
    return secs;
}

static int main_speed_cmp(const void *va, const void *vb)
{
    double res = ((const emu_speed_t *)va)->multiplier - ((const emu_speed_t *)vb)->multiplier;
    return (int)round(res);
}

static void main_load_speeds(void)
{
    ALLEGRO_CONFIG_ENTRY *iter;
    int num_speed = 0;
    for (char const *name = al_get_first_config_entry(bem_cfg, "speeds", &iter); name; name = al_get_next_config_entry(&iter))
        ++num_speed;
    if (num_speed > 0) {
        log_info("main: %d speeds found in config file", num_speed);
        emu_speed_t *speeds = malloc(num_speed * sizeof(emu_speed_t));
        if (speeds) {
            bool worked = true;
            emu_speed_t *ptr = speeds;
            for (char const *name = al_get_first_config_entry(bem_cfg, "speeds", &iter); name; name = al_get_next_config_entry(&iter)) {
                const char *str = al_get_config_value(bem_cfg, "speeds", name);
                char *end;
                double multiplier = strtod(str, &end);
                if (multiplier <= 0 || *end != ',') {
                    log_error("main: speed '%s': invalid multiplier '%s'", name, str);
                    worked = false;
                    break;
                }
                str = end + 1;
                int fskipmax = strtol(str, &end, 0);
                if (fskipmax < 0 || *end) {
                    log_error("main: speed '%s': invalid fskipmax '%s'", name, str);
                    worked = false;
                    break;
                }
                ptr->name = name;
                ptr->multiplier = multiplier;
                ptr->fskipmax = fskipmax;
                ++ptr;
            }
            if (worked) {
                qsort(speeds, num_speed, sizeof(emu_speed_t), main_speed_cmp);
                emu_speeds = speeds;
                num_emu_speeds = num_speed;
                if (emu_speed_normal >= num_speed)
                    emu_speed_normal = num_speed - 1;
            }
            else {
                log_error("main: reverting to default speeds");
                free(speeds);
            }
        }
    }
}

void main_init(int argc, char *argv[])
{

    /* TOHv3 */
    int tapetestnext = 0, tapesavenext = 0;
    uint8_t tapesave_dup, tapesave_do_compress, tapesave_filetype_bits;
    int e; /* TOHv3.3 */

    int tapenext = 0, discnext = 0, execnext = 0, vdfsnext = 0, pastenext = 0;
    int rs423filenext = 0; /* TOHv4 */
    int expirenext = 0; /* TOHv3.3 */
    ALLEGRO_DISPLAY *display;
    ALLEGRO_PATH *path;
    const char *ext, *exec_fn = NULL;
    const char *vroot = NULL, *vdir = NULL;
    const char *rs423_fn = NULL; /* TOHv4 */
    
    e = TAPE_E_OK; /* TOHv3.3 */

    /* TOHv3: */
    shutdown_exit_code = SHUTDOWN_OK;
    tape_vars.testing_mode  = 0;

    if (!al_init()) {
        fputs("Failed to initialise Allegro!\n", stderr);
        exit(1);
    }

    al_init_native_dialog_addon();
    al_set_new_window_title(VERSION_STR);
    al_init_primitives_addon();
    if (!al_install_keyboard()) {
        log_fatal("main: unable to install keyboard");
        exit(1);
    }
    key_init();
    config_load();
    log_open();
    log_info("main: starting %s", VERSION_STR);

    main_load_speeds();
    model_loadcfg();

    /* TOHv3 */
    acia_init(&sysacia); /* TOHv3.2 */
    tape_init(&tape_state, &tape_vars);
    tapesave_filetype_bits = TAPE_FILETYPE_BITS_NONE;
    tapesave_dup = 0;
    tapesave_do_compress = 0;

    for (int c = 1; c < argc; c++) {
        size_t len; /* TOHv3 */
        uint8_t fail;
        if (!strcasecmp(argv[c], "--help") || !strcmp(argv[c], "-?") || !strcasecmp(argv[c], "-h")) {
            fwrite(helptext, sizeof helptext-1, 1, stdout);
            exit(1);
        }
        else if (!strncasecmp(argv[c], "-sp", 3)) {
            sscanf(&argv[c][3], "%i", &emuspeed);
            if(!(emuspeed < num_emu_speeds))
                emuspeed = 4;
        }
	// lovebug
        else if (!strcasecmp(argv[c], "-fullscreen"))
            fullscreen = 1;
	// lovebug end
        else if (!strcasecmp(argv[c], "-rs423file"))
            rs423filenext = 1;
        else if (!strcasecmp(argv[c], "-tapesave"))
            tapesavenext = 1;
        else if (!strcasecmp(argv[c], "-tapetest")) /* TOHv3: must come before -tape */
            tapetestnext = 1;
        else if (!strcasecmp(argv[c], "-tapeskip")) /* TOHv3.2: must come before -tape */
            tape_vars.strip_silence_and_leader = 1;
        else if (!strcasecmp(argv[c], "-tape117"))  /* TOHv3.2: must come before -tape */
            tape_vars.save_always_117 = 1;
        else if (!strcasecmp(argv[c], "-tape112"))  /* TOHv3.2: must come before -tape */
            tape_vars.save_prefer_112 = 1;
        else if (!strcasecmp(argv[c], "-tapeno0"))  /* TOHv3.2: must come before -tape */
            tape_vars.save_do_not_generate_origin_on_append = 1;
        else if (!strcasecmp(argv[c], "-tapenopbp")) /* TOHv3.2: must come before -tape */
            tape_vars.disable_phantom_block_protection = 1;
        else if (!strcasecmp(argv[c], "-tape"))
            tapenext = 1;
        else if (!strcasecmp(argv[c], "-record")) /* TOHv3 */
            e = tape_set_record_activated(&tape_state, &tape_vars, NULL, 1);
        else if (!strcasecmp(argv[c], "-expire")) /* TOHv3.3, TOHv4 */
            expirenext = 1;
        else if (!strcasecmp(argv[c], "-disc") || !strcasecmp(argv[c], "-disk"))
            discnext = 1;
        else if (!strcasecmp(argv[c], "-disc1"))
            discnext = 2;
        else if (argv[c][0] == '-' && (argv[c][1] == 'm' || argv[c][1] == 'M'))
            sscanf(&argv[c][2], "%i", &curmodel);
        else if (argv[c][0] == '-' && (argv[c][1] == 't' || argv[c][1] == 'T'))
            sscanf(&argv[c][2], "%i", &curtube);
        else if (!strcasecmp(argv[c], "-fasttape"))
            tape_vars.overclock = true;
        else if (!strcasecmp(argv[c], "-autoboot"))
            autoboot = 150;
        else if (argv[c][0] == '-' && (argv[c][1] == 'f' || argv[c][1]=='F')) {
            if (sscanf(&argv[c][2], "%i", &vid_fskipmax) == 1) {
                if (vid_fskipmax < 1) vid_fskipmax = 1;
                if (vid_fskipmax > 9) vid_fskipmax = 9;
                skipover = true;
            }
            else
                fprintf(stderr, "invalid frame skip '%s'\n", &argv[c][2]);
        }
        else if (argv[c][0] == '-' && (argv[c][1] == 's' || argv[c][1] == 'S'))
            vid_dtype_user = VDT_SCANLINES;
        else if (!strcasecmp(argv[c], "-debug"))
            debug_core = 1;
        else if (!strcasecmp(argv[c], "-debugtube"))
            debug_tube = 1;
        else if (argv[c][0] == '-' && (argv[c][1] == 'i' || argv[c][1] == 'I'))
            vid_dtype_user = VDT_INTERLACE;
        else if (!strcasecmp(argv[c], "-exec"))
            execnext = 1;
        else if (!strcasecmp(argv[c], "-vroot"))
            vdfsnext = 1;
        else if (!strcasecmp(argv[c], "-vdir"))
            vdfsnext = 2;
        else if (!strcasecmp(argv[c], "-paste"))
            pastenext = 1;
        else if (expirenext) { /* TOHv3.3 */
            expirenext = 0;
#define OSP 15625 /* "otherstuff ticks" per second (see 6502.c) */
            tape_vars.testing_expire_ticks = OSP * ((int64_t)atoi(argv[c]));
            if (    (tape_vars.testing_expire_ticks < (1    * ((int64_t)OSP)))
                 || (tape_vars.testing_expire_ticks > (2000 * ((int64_t)OSP)))) {
                fprintf(stderr,
                        "\"-expire\" value was illegal: %s\n",
                        argv[c]);
                exit(1);
            }
        } else if (tapetestnext) { /* TOHv3 */
            tape_vars.testing_mode = 0x7f & atoi(argv[c]);
            tape_vars.disable_debug_memview = 1; /* TOHv4: prevent slew of race conditions on mem view window */
            tapetestnext = 0;
            /* TOHv4-rc4: now permit -tapetest 0
             * this suppresses mem view window but doesn't do anything else */
            if (/*(0 == tape_vars.testing_mode) ||*/ (TAPE_TEST_BAD_MASK & tape_vars.testing_mode)) {
                fprintf(stderr,
                        "\"-tapetest\" value was illegal: %u\n",
                        tape_vars.testing_mode);
                exit(1);
            }
        } else if (tapesavenext) { /* TOHv3 */

            if (tapesave_dup) {
                fprintf(stderr, "\"-tapesave\" option specified multiple times!\n");
                exit(1);
            }

            tapesave_dup = 1;
            fail = 0;

            /* examine final N characters */

            len = strlen(argv[c]);
            if (len < 5) {
                fail = 1;
            } else if (0 == strcasecmp(argv[c] + (len - 4), ".uef")) {    /* compress UEFs by default */
                tapesave_filetype_bits = TAPE_FILETYPE_BITS_UEF;
                tapesave_do_compress = 1;
            } else if (0 == strcasecmp(argv[c] + (len - 4), ".csw")) {    /* compress CSWs by default */
                tapesave_filetype_bits = TAPE_FILETYPE_BITS_CSW;
                tapesave_do_compress = 1;
            }

            if ( ( ! fail ) && (TAPE_FILETYPE_BITS_NONE == tapesave_filetype_bits ) ) {
                if (len < 7) {
                    fail = 1;
                } else if (0 == strcasecmp(argv[c] + (len - 6), ".tibet")) {
                    tapesave_filetype_bits = TAPE_FILETYPE_BITS_TIBET;
                }
            }

            if ( ( ! fail ) && (TAPE_FILETYPE_BITS_NONE == tapesave_filetype_bits ) ) {
                if (len < 8) {
                    fail = 1;
                } else if (0 == strcasecmp(argv[c] + (len - 7), ".unzuef")) { /* extension disables compression */
                    tapesave_filetype_bits = TAPE_FILETYPE_BITS_UEF;
                } else if (0 == strcasecmp(argv[c] + (len - 7), ".unzcsw")) { /* extension disables compression */
                    tapesave_filetype_bits = TAPE_FILETYPE_BITS_CSW;
                } else if (0 == strcasecmp(argv[c] + (len - 7), ".tibetz")) {
                    tapesave_filetype_bits = TAPE_FILETYPE_BITS_TIBET;
                    tapesave_do_compress = 1;
                }
            }

            if (fail) {
                fprintf(stderr, "\"-tapesave\" filename was too short: %s\n", argv[c]);
                exit(1);
            } else if (TAPE_FILETYPE_BITS_NONE == tapesave_filetype_bits) {
                fprintf(stderr, "\"-tapesave\": unrecognised filetype: %s\n", argv[c]);
            } else {
                tape_vars.quitsave_config.filename      = argv[c];
                tape_vars.quitsave_config.filetype_bits = tapesave_filetype_bits;
                tape_vars.quitsave_config.do_compress   = tapesave_do_compress;
            }

            tapesavenext = 0;

        }
        else if (!strcasecmp(argv[c], "-pastek"))
            pastenext = 2;
        else if (tapenext) {
            if (tape_vars.load_filename)
                al_destroy_path(tape_vars.load_filename);
            tape_vars.load_filename = al_create_path(argv[c]);
        }
        else if (discnext) {
            if (discfns[discnext-1])
                al_destroy_path(discfns[discnext-1]);
            discfns[discnext-1] = al_create_path(argv[c]);
            discnext = 0;
        }
        else if (execnext) {
            exec_fn = argv[c];
            execnext = 0;
        }
        else if (vdfsnext) {
            if (vdfsnext == 2)
                vdir = argv[c];
            else
                vroot = argv[c];
            vdfsnext = 0;
        }
        else if (pastenext)
            debug_paste(argv[c], pastenext == 2 ? key_paste_start : os_paste_start);
        else if (rs423filenext) {
            rs423_fn = argv[c];
            rs423filenext = 0;
        } else {
            path = al_create_path(argv[c]);
            ext = al_get_path_extension(path);
            if (ext && !strcasecmp(ext, ".snp"))
                savestate_load(argv[c]);
            else if (ext &&
                      (    ! strcasecmp(ext, ".uef") /* TOHv3: +TIBET */
                        || ! strcasecmp(ext, ".csw")
                        || ! strcasecmp(ext, ".tibet")
                        || ! strcasecmp(ext, ".tibetz") ) ) {
                if (tape_vars.load_filename)
                    al_destroy_path(tape_vars.load_filename);
                tape_vars.load_filename = path;
                tapenext = 0;
            }
            else {
                if (discfns[0])
                    al_destroy_path(discfns[0]);
                discfns[0] = path;
                discnext = 0;
                autoboot = 150;
            }
        }

        if (tapenext) tapenext--;
        
        if (TAPE_E_OK != e) { break; }

    }
    
    if (TAPE_E_OK != e) {
        log_warn("main: command-line error (code %d)", e); /* TOHv4-rc3 */
        shutdown_exit_code = SHUTDOWN_STARTUP_FAILURE;     /* TOHv4-rc3 */
      /* FIXME (TOHv3.3): We would prefer to be able to return a failure
         code from main_init(). There are loads of ways it might fail. */
        return;
    }
    display = video_init();
    mode7_makechars();
    al_init_image_addon();
    led_init();

    mem_init();

    if (!(queue = al_create_event_queue())) {
        log_fatal("main: unable to create event queue");
        exit(1);
    }
    al_register_event_source(queue, al_get_display_event_source(display));

    if (!al_install_audio()) {
        log_fatal("main: unable to initialise audio");
        exit(1);
    }
    if (!al_reserve_samples(3)) {
        log_fatal("main: unable to reserve audio samples");
        exit(1);
    }
    if (!al_init_acodec_addon()) {
        log_fatal("main: unable to initialise audio codecs");
        exit(1);
    }

    sound_init();
    sid_init();
    sid_settype(sidmethod, cursid);
    music5000_init(emu_speed_normal);
    paula_init();
    ddnoise_init();
    tapenoise_init(queue);

    adc_init();
    pal_init();
    disc_init();
    fdi_init();
    hfe_init();

    scsi_init();
    ide_init();
    vdfs_init(vroot, vdir);

    model_init();

    midi_init();
    main_reset();

    joystick_init(queue);

    tmp_display = display;

    gui_allegro_init(queue, display);

    if (!(timer = al_create_timer(main_calc_timer(emu_speed_normal)))) {
        log_fatal("main: unable to create timer");
        exit(1);
    }

    al_register_event_source(queue, al_get_timer_event_source(timer));
    al_init_user_event_source(&evsrc);
    al_register_event_source(queue, &evsrc);

    al_register_event_source(queue, al_get_keyboard_event_source());

    oldmodel = curmodel;

    al_install_mouse();
    al_register_event_source(queue, al_get_mouse_event_source());

    if (mmb_fn)
        mmb_load(mmb_fn);
    else
        disc_load(0, discfns[0]);
    disc_load(1, discfns[1]);
    if (tape_vars.load_filename != NULL) {
        tape_load(tape_vars.load_filename);
    }
    if (mmccard_fn)
        mmccard_load(mmccard_fn);
    if (defaultwriteprot)
        writeprot[0] = writeprot[1] = 1;
    if (discfns[0])
        gui_set_disc_wprot(0, writeprot[0]);
    if (discfns[1])
        gui_set_disc_wprot(1, writeprot[1]);
    main_setspeed(emuspeed);
    debug_start(exec_fn, ! tape_vars.disable_debug_memview);
    // lovebug
    if (fullscreen)
        video_enterfullscreen();
    // lovebug end

    /* TOHv4.1: mitigate Allegro crash problems under
     * certain test environments (macOS) by offering a means
     * of delaying startup */
    if (tape_vars.testing_mode & TAPE_TEST_SLOW_STARTUP) {
#ifdef WIN32
        Sleep(2000);
#else
        sleep(2);
#endif
    }

    /* TOHv3: if tape testing mode is enabled, do a
       full catalogue of the tape on startup: */
    if (tape_vars.testing_mode & TAPE_TEST_CAT_ON_STARTUP) {
        findfilenames_new(&tape_state,
                          0,  /* 0 = silent; no UI window: */
                          ! tape_vars.disable_phantom_block_protection);
    }
    if (rs423_fn != NULL) { /* TOHv4: for -rs423file */
        sysacia_rec_start(rs423_fn);
    }


}

void main_restart()
{
    main_pause("restarting");
    cmos_save(&models[oldmodel]);

    model_init();
    main_reset();
    main_resume();
}

int resetting = 0;
int framesrun = 0;
static double spd = 0;
static double prev_spd = 0;

void main_cleardrawit()
{
    fcount = 0;
}

static void main_newspeed(int speed)
{
    spd = emu_speeds[speed].multiplier;
    if (!skipover) {
        vid_fskipmax = autoskip ? 1 : emu_speeds[speed].fskipmax;
        log_debug("main: main_setspeed: vid_fskipmax=%d", vid_fskipmax);
    }
    music5000_init(speed);
}

void main_start_fullspeed(void)
{
    if (fullspeed != FSPEED_RUNNING) {
        ALLEGRO_EVENT event;

        log_debug("main: starting full-speed");
        al_stop_timer(timer);
        fullspeed = FSPEED_RUNNING;
        main_newspeed(num_emu_speeds-1);
        prev_spd = 0.0;
        event.type = ALLEGRO_EVENT_TIMER;
        al_emit_user_event(&evsrc, &event, NULL);
    }
}

void main_stop_fullspeed(bool hostshift)
{
    if (emuspeed != EMU_SPEED_FULL) {
        if (!hostshift) {
            log_debug("main: stopping fullspeed (PgUp)");
            if (fullspeed == FSPEED_RUNNING && emuspeed != EMU_SPEED_PAUSED) {
                main_newspeed(emuspeed);
                al_start_timer(timer);
            }
            fullspeed = FSPEED_NONE;
        }
        else
            fullspeed = FSPEED_SELECTED;
    }
}

void main_key_break(void)
{
    m6502_reset();
    video_reset();
    i8271_reset();
    wd1770_reset();
    sid_reset();
    music5000_reset();
    cmos_reset();
    paula_reset();

    if (curtube != -1)
        tubes[curtube].cpu->reset();
    tube_reset();
}

void main_key_fullspeed(void)
{
    if (fullspeed != FSPEED_RUNNING)
        main_start_fullspeed();
}

void main_key_pause(void)
{
    if (bempause) {
        if (emuspeed != EMU_SPEED_PAUSED) {
            bempause = false;
            if (emuspeed != EMU_SPEED_FULL)
                al_start_timer(timer);
        }
    } else {
        al_stop_timer(timer);
        bempause = true;
    }
}

static double prev_time = 0;
static int execs = 0;
static int slow_count = 0;

static void main_timer(ALLEGRO_EVENT *event)
{
    double now = al_get_time();
    double delay = now - event->any.timestamp;

    if (delay < time_limit && music5000_ok()) {
        if (autoboot)
            autoboot--;
        if (x65c02)
            m65c02_exec(slice);
        else
            m6502_exec(slice);
        execs++;

        if (ddnoise_ticks > 0 && --ddnoise_ticks == 0)
            ddnoise_headdown();

        if (tapeledcount) {
            /* TOHv3.3: state, not vars */
            if ( --tapeledcount == 0 && ! tape_state.ula_motor ) {
                log_debug("main: delayed cassette motor LED off");
                led_update(LED_CASSETTE_MOTOR, 0, 0);
            }
        }

        if (savestate_wantload)
            savestate_doload();
        if (savestate_wantsave)
            savestate_dosave();

        if (now - prev_time > 0.1) {
            double speed = execs * slice / (now - prev_time);

            if (spd < 0.0001)
                spd = speed / 2000000;
            else
                spd = spd * 0.75 + 0.25 * speed / 2000000;

            char buf[120];
            snprintf(buf, sizeof(buf), "%s %.3fMHz %.1f%%", VERSION_STR, speed / 1000000, spd * 100.0);
            al_set_window_title(tmp_display, buf);

            if (autoskip && !skipover) {
                if (fullspeed != FSPEED_NONE) {
                    if (spd > prev_spd && ++slow_count >= 6) {
                        slow_count = 0;
                        ++vid_fskipmax;
                        log_debug("main: full-speed, speed increased from %g to %g, increasing vid_fskipmax to %d", prev_spd, spd, vid_fskipmax);
                        prev_spd = spd;
                    }
                }
                else if (spd < (emu_speeds[emuspeed].multiplier * 0.95)) {
                    if (++slow_count >= 6) {
                        slow_count = 0;
                        ++vid_fskipmax;
                        log_debug("main: going slow, target=%g, spd=%g, new vid_fskipmax=%d", emu_speeds[emuspeed].multiplier, spd, vid_fskipmax);
                    }
                }
                else
                    slow_count = 0;
            }
            execs = 0;
            prev_time = now;
        }
    }
    if (fullspeed == FSPEED_RUNNING) {
        ALLEGRO_EVENT event;
        event.type = ALLEGRO_EVENT_TIMER;
        al_emit_user_event(&evsrc, &event, NULL);
    }
}

static double last_switch_in = 0.0;

void main_run()
{
    ALLEGRO_EVENT event;

    log_debug("main: about to start timer");
    if (NULL == timer) { /* TOHv3.3: sanity check */
        log_warn("BUG: cannot start a NULL timer!\n");
        quitting = 1;
    } else {
        al_start_timer(timer);
    }

    log_debug("main: entering main loop");
    while (!quitting) {
        al_wait_for_event(queue, &event);
        switch(event.type) {
            case ALLEGRO_EVENT_KEY_DOWN:
                if (!keydefining)
                    key_down_event(&event);
                break;
            case ALLEGRO_EVENT_KEY_CHAR:
                if (!keydefining)
                    key_char_event(&event);
                break;
            case ALLEGRO_EVENT_KEY_UP:
                if (!keydefining)
                    key_up_event(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_AXES:
                mouse_axes(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_BUTTON_DOWN:
                log_debug("main: mouse button down");
                mouse_btn_down(&event);
                break;
            case ALLEGRO_EVENT_MOUSE_BUTTON_UP:
                log_debug("main: mouse button up");
                mouse_btn_up(&event);
                break;
            case ALLEGRO_EVENT_JOYSTICK_AXIS:
                joystick_axis(&event);
                break;
            case ALLEGRO_EVENT_JOYSTICK_BUTTON_DOWN:
                joystick_button_down(&event);
                break;
            case ALLEGRO_EVENT_JOYSTICK_BUTTON_UP:
                joystick_button_up(&event);
                break;
            case ALLEGRO_EVENT_JOYSTICK_CONFIGURATION:
                joystick_rescan_sticks();
                break;
            case ALLEGRO_EVENT_DISPLAY_CLOSE:
                log_debug("main: event display close - quitting");
                quitting = true;
                break;
            case ALLEGRO_EVENT_TIMER:
                main_timer(&event);
                break;
            case ALLEGRO_EVENT_MENU_CLICK:
                main_pause("menu active");
                gui_allegro_event(&event);
                main_resume();
                break;
            case ALLEGRO_EVENT_DISPLAY_RESIZE:
                video_update_window_size(&event);
                break;
            case ALLEGRO_EVENT_DISPLAY_SWITCH_OUT:
                /* bodge for when OUT events immediately follow an IN event */
                if ((event.any.timestamp - last_switch_in) > 0.01) {
                    key_lost_focus();
                    if (autopause && !debug_core && !debug_tube)
                        main_pause("auto-paused");
                }
                break;
            case ALLEGRO_EVENT_DISPLAY_SWITCH_IN:
                last_switch_in = event.any.timestamp;
                if (autopause)
                    main_resume();
        }
    }

    if (tape_vars.testing_mode & TAPE_TEST_CAT_ON_SHUTDOWN) { /* TOHv4 */
        findfilenames_new(&tape_state,
                          0,  /* 0 = silent; no UI window: */
                          ! tape_vars.disable_phantom_block_protection);
    }

    log_debug("main: end loop");
}

void main_close()
{
    gui_tapecat_close();
    gui_keydefine_close();
    
    sysacia_rec_stop(); /* TOHv4 */

    debug_kill();

    config_save();
    cmos_save(&models[curmodel]);

    midi_close();
    mem_close();
    /* TOHv3 */

    tape_save_on_shutdown(&(tape_state.uef),
                          tape_vars.record_activated,
                          &(tape_state.filetype_bits),
                          tape_vars.save_prefer_112, /* TOHv3.2 */
                          &(tape_vars.quitsave_config));
    tape_state_finish(&tape_state, 0); /* 0 = don't alter menus */
    tube_6502_close();
    arm_close();
    x86_close();
    z80_close();
    w65816_close();
    n32016_close();
    mc6809nc_close();
    sprow_close();
    disc_close(0);
    disc_close(1);
    scsi_close();
    ide_close();
    vdfs_close();
    music5000_close();
    ddnoise_close();
    tapenoise_close();

    video_close();
    log_close();
}

void main_setspeed(int speed)
{
    log_debug("main: setspeed %d", speed);
    if (speed == EMU_SPEED_FULL)
        main_start_fullspeed();
    else {
        al_stop_timer(timer);
        fullspeed = FSPEED_NONE;
        if (speed != EMU_SPEED_PAUSED) {
            if (speed >= num_emu_speeds) {
                log_warn("main: speed #%d out of range, defaulting to 100%%", speed);
                speed = 4;
            }
            al_set_timer_speed(timer, main_calc_timer(speed));
            main_newspeed(speed);
            al_start_timer(timer);
        }
        emuspeed = speed;
    }
}

void main_pause(const char *why)
{
    char buf[120];
    snprintf(buf, sizeof(buf), "%s (%s)", VERSION_STR, why);
    al_set_window_title(tmp_display, buf);
    al_stop_timer(timer);
}

void main_resume(void)
{
    if (emuspeed != EMU_SPEED_PAUSED && emuspeed != EMU_SPEED_FULL)
        al_start_timer(timer);
}

void main_setquit(void)
{
    quitting = true;
}

/* TOHv3 */
void set_shutdown_exit_code (uint8_t c) {
    /* prevent a new error from scribbling an older one */
    /* TOHv4-rc1: except SHUTDOWN_EXPIRED which always overrides anything prior */
    if (    (SHUTDOWN_OK == shutdown_exit_code)
         || (SHUTDOWN_EXPIRED == c)) {
        shutdown_exit_code = 0x7f & c;
    }
}

int main(int argc, char **argv)
{
    main_init(argc, argv);
    main_run();
    main_close();
    return shutdown_exit_code;
}
