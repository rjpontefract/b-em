#define _DEBUG
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
#include "tape-io.h"
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
unsigned hiresdisplay = BOOL_USE_CONFIG;
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

/* TOHv4.2 merge */
static int toh_cli_handle_tapesave (char *argv);
static int toh_cli_handle_tapetest (char *argv);
static int toh_cli_handle_expire   (char *argv);
#ifdef BUILD_TAPE_TAPECTRL
static int toh_cli_handle_tapeuiscale (const char * const argv);
#endif

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
    "-cfg file.cfg   - use the specified config file\n"
    "-log file.log   - use the specified log file\n"
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
    "-hires          - enable Hi-Res display mode\n"
    "-lores          - disable Hi-Res display mode\n"
    "-paste string   - paste string in as if typed (via OS)\n"
    "-pastek string  - paste string in as if typed (via KB)\n"
    "-printfile f    - printer output to file as text\n"
    "-printfilebin f - printer output to file as text\n"
    "-printcmd c     - printer output via command as text\n"
    "-printcmdbin c  - printer output via command as binary\n"
    "-vroot host-dir - set the VDFS root\n"
    "-vdir guest-dir - set the initial (boot) dir in VDFS\n"
    "-rs423file file - write RS423 output to file\n"
    "-tapetest <n>   - bits: 1=quit @ EOF | 2=quit @ err | 4=cat @ boot\n"               /* TOHv3, v4   */
    "                        | 8=cat @ end | 16=slow startup | 32=tapectrl\n"            /* TOHv4.1, v4.3 */
    "-record         - begin recording to tape at B-Em startup\n"                        /* TOHv3   */
    "-tapesave file  - save tape copy on exit (format set by extension)\n"               /* TOHv3   */
    "-tapeskip       - skip silence and leader when loading tapes (faster)\n"            /* TOHv3.2 */
    "-tape117        - when saving UEF, emit baud chunk &117 before every block\n"       /* TOHv3.2 */
    "-tape112        - when saving UEF, use chunk &112 rather than &116 for silence\n"   /* TOHv3.2 */
    "-tapeno0        - when appending to loaded UEF, do not emit origin chunk &0\n"      /* TOHv3.2 */
    "-tapewavcosine  - when saving WAV, use alternative phase (change tone at peak)\n"   /* TOHv4.2 */
#ifdef BUILD_TAPE_TAPECTRL
    "-tapeuiscale n  - set scale at which to display tape control window (1.0 - 4.0)\n"  /* TOHv4.3 */
    "-tapeuiresize   - allow free resize of tape control window (may cause crashes)\n"   /* TOHv4.3 */
#endif
    "-expire n       - quit, with predictable exit code, after n emulated seconds\n"     /* TOHv3.3 */
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

typedef enum {
    OPT_DISC1,
    OPT_DISC2,
    OPT_CFGFILE,
    OPT_LOGFILE,
    OPT_TAPE,
    OPT_EXEC,
    OPT_VDFS_ROOT,
    OPT_VDFS_DIR,
    OPT_PASTE_OS,
    OPT_PASTE_KBD,
    OPT_PRINT,
    OPT_GROUND,
    /* TOHv4.2 merge: */
    OPT_TAPETEST,
    OPT_TAPESAVE,
#ifdef BUILD_TAPE_TAPECTRL
    OPT_TAPEUISCALE,  /* TOHv4.3 */
#endif
    OPT_RS423FILE,
    OPT_EXPIRE
} opt_state;

#include "tapeseek.h"

void main_init(int argc, char *argv[])
{
    if (!al_init()) {
        fputs("b-em: Failed to initialise Allegro!\n", stderr);
        exit(1);
    }

    opt_state state = OPT_GROUND;
    ALLEGRO_PATH *snap_fn = NULL;
    ALLEGRO_PATH *cfg_fn = NULL;
    const char *ext, *exec_fn = NULL, *log_file = NULL;
    const char *vroot = NULL, *vdir = NULL;
    int e; /* TOHv4.2 merge */
    char *rs423_fn;
    int32_t tape_duration_1200ths;
    bool tcw_opened; /* TOHv4.3 */

    /* TOHv3.3, TOHv4, TOHv4.2 merge */
    shutdown_exit_code = SHUTDOWN_OK;
    tape_vars.testing_mode  = 0;
    e = SHUTDOWN_OK;
    rs423_fn = NULL;
    tcw_opened = false;

    /* TOHv4.2 merge */
    tape_init(&tape_state, &tape_vars);

    /* TOHv4.3 */
#ifdef BUILD_TAPE_TAPECTRL
#ifdef BUILD_TAPE_TAPECTRL_ALLOW_RESIZE
    tape_vars.tapectrl_allow_resize = true;
#endif
#ifdef BUILD_TAPE_TAPECTRL_DOUBLE_SIZE
    tape_vars.tapectrl_ui_scale = 2.0f;
#endif
#endif

    while (--argc) {
        ALLEGRO_PATH *path;
        char *arg = *++argv;
        switch (state) {
            case OPT_GROUND:
                if (*arg == '-') {
                    if (!strcasecmp(++arg, "cfg"))
                        state = OPT_CFGFILE;
                    else if (!strcasecmp(arg, "log"))
                        state = OPT_LOGFILE;
                    else if (!strncasecmp(arg, "sp", 2)) {
                        sscanf(&arg[2], "%i", &emuspeed);
                        if(!(emuspeed < num_emu_speeds))
                            emuspeed = 4;
                    }
                    else if (!strcasecmp(arg, "fullscreen"))
                        fullscreen = 1;
                    /* begin TOHv4.2 merge ... */
                    else if (!strcasecmp(arg, "rs423file")) {
                        state = OPT_RS423FILE;
                    } else if (!strcasecmp(arg, "tapesave")) {
                        state = OPT_TAPESAVE;
                    } else if (!strcasecmp(arg, "tapetest")) {      /* TOHv3: must come before -tape */
                        state = OPT_TAPETEST;
                    } else if (!strcasecmp(arg, "expire")) {        /* TOHv3.3, TOHv4 */
                        state = OPT_EXPIRE;
                    } else if (!strcasecmp(arg, "tapeskip")) {      /* TOHv3.2: must come before -tape */
                        tape_vars.strip_silence_and_leader = true;
                    } else if (!strcasecmp(arg, "tape117")) {       /* TOHv3.2: must come before -tape */
                        tape_vars.save_always_117 = true;
                    } else if (!strcasecmp(arg, "tape112")) {       /* TOHv3.2: must come before -tape */
                        tape_vars.save_prefer_112 = true;
                    } else if (!strcasecmp(arg, "tapeno0")) {       /* TOHv3.2: must come before -tape */
                        tape_vars.save_do_not_generate_origin_on_append = true;
                    } else if (!strcasecmp(arg, "tapenopbp")) {     /* TOHv3.2: must come before -tape */
                        tape_vars.permit_phantoms = true;
                    } else if (!strcasecmp(arg, "tapewavcosine")) { /* TOHv4.2 */
                        tape_vars.wav_use_phase_shift = true;
#ifdef BUILD_TAPE_TAPECTRL  /* TOHv4.3 */
                    } else if (!strcasecmp(arg, "tapeuiscale")) {
                        state = OPT_TAPEUISCALE;
                    } else if (!strcasecmp(arg, "tapeuiresize")) {
                        tape_vars.tapectrl_allow_resize = true;
#endif
                    } else if (!strcasecmp(arg, "record")) {        /* TOHv3 */
                        // tape_state.display
                        tape_get_duration_1200ths(&tape_state, &tape_duration_1200ths);
#ifdef BUILD_TAPE_TAPECTRL
                        tapectrl_set_record(&(tape_vars.tapectrl), false, tape_duration_1200ths);
                        tcw_opened = true;
#endif
                        tape_set_record_activated(&tape_state,
                                                  &tape_vars,
                                                  &sysacia,
                                                  true,
                                                  tcw_opened);
                        //gui_set_record_mode(false);
                    /* end TOHv4.2 merge */
                    } else if (!strcasecmp(arg, "tape"))
                        state = OPT_TAPE;
                    else if (!strcasecmp(arg, "disc") || !strcasecmp(arg, "-disk"))
                        state = OPT_DISC1;
                    else if (!strcasecmp(arg, "disc1"))
                        state = OPT_DISC2;
                    else if (arg[0] == 'm' || arg[0] == 'M')
                        sscanf(&arg[1], "%i", &curmodel);
                    else if (arg[0] == 't' || arg[0] == 'T')
                        sscanf(&arg[1], "%i", &curtube);
                    else if (!strcasecmp(arg, "fasttape"))
                        tape_vars.overclock = true; /* TOHv4.2 merge */
                    else if (!strcasecmp(arg, "autoboot"))
                        autoboot = 150;
                    else if (arg[0] == 'f' || arg[0]=='F') {
                        if (sscanf(&arg[1], "%i", &vid_fskipmax) == 1) {
                            if (vid_fskipmax < 1) vid_fskipmax = 1;
                            if (vid_fskipmax > 9) vid_fskipmax = 9;
                            skipover = true;
                        }
                        else
                            fprintf(stderr, "invalid frame skip '%s'\n", &arg[2]);
                    }
                    else if (arg[0] == 's' || arg[0] == 'S')
                        vid_dtype_user = VDT_SCANLINES;
                    else if (!strcasecmp(arg, "debug"))
                        debug_core = 1;
                    else if (!strcasecmp(arg, "debugtube"))
                        debug_tube = 1;
                    else if (arg[0] == 'i' || arg[0] == 'I')
                        vid_dtype_user = VDT_INTERLACE;
                    else if (!strcasecmp(arg, "exec"))
                        state = OPT_EXEC;
                    else if (!strcasecmp(arg, "vroot"))
                        state = OPT_VDFS_ROOT;
                    else if (!strcasecmp(arg, "vdir"))
                        state = OPT_VDFS_DIR;
                    else if (!strcasecmp(arg, "paste"))
                        state = OPT_PASTE_OS;
                    else if (!strcasecmp(arg, "pastek"))
                        state = OPT_PASTE_KBD;
                    else if (!strcasecmp(arg, "printstdout"))
                        print_dest = PDEST_STDOUT;
                    else if (!strcasecmp(arg, "printfile")) {
                        print_dest = PDEST_FILE_TEXT;
                        state = OPT_PRINT;
                    }
                    else if (!strcasecmp(arg, "printfilebin")) {
                        print_dest = PDEST_FILE_BIN;
                        state = OPT_PRINT;
                    }
                    else if (!strcasecmp(arg, "printcmd")) {
                        print_dest = PDEST_PIPE_TEXT;
                        state = OPT_PRINT;
                    }
                    else if (!strcasecmp(arg, "printcmdbin")) {
                        print_dest = PDEST_PIPE_BIN;
                        state = OPT_PRINT;
                    }
                    else if (!strcasecmp(arg, "hires"))
                        hiresdisplay = true;
                    else if (!strcasecmp(arg, "lores"))
                        hiresdisplay = false;
                    else {
                        if (*arg != 'h' && *arg != '?')
                            fprintf(stderr, "b-em: unrecognised option '-%s'\n", arg);
                        fwrite(helptext, sizeof helptext-1, 1, stdout);
                        exit(1);
                    }
                }
                else {
                    path = al_create_path(arg);
                    ext = al_get_path_extension(path);
                    if (ext && !strcasecmp(ext, ".snp")) {
                        if (snap_fn)
                            al_destroy_path(snap_fn);
                        snap_fn = path;
                    }
                    else if ((ext != NULL) && (    ! strcasecmp(ext, ".uef")
                                                || ! strcasecmp(ext, ".csw")
                                                || ! strcasecmp(ext, ".tibet")  /* TOHv3: +TIBET */
                                                || ! strcasecmp(ext, ".tibetz") ) ) {
                        if (tape_vars.load_filename)
                            al_destroy_path(tape_vars.load_filename);
                        tape_vars.load_filename = al_create_path(arg);
                    }
                    else {
                        if (drives[0].discfn)
                            al_destroy_path(drives[0].discfn);
                        drives[0].discfn = path;
                        autoboot = 150;
                    }
                }
                continue;
            case OPT_DISC1:
            case OPT_DISC2:
                if (drives[state].discfn)
                    al_destroy_path(drives[state].discfn);
                drives[state].discfn = al_create_path(arg);
                break;
            case OPT_CFGFILE:
                if (cfg_fn)
                    al_destroy_path(cfg_fn);
                cfg_fn = al_create_path(arg);
                break;
            case OPT_LOGFILE:
                log_file = arg;
                break;
            /* begin TOHv4.2 merge ... */
            case OPT_EXPIRE:
                e = toh_cli_handle_expire(arg);
                break;
            case OPT_TAPESAVE:
                e = toh_cli_handle_tapesave(arg);
                break;
            case OPT_TAPETEST:
                e = toh_cli_handle_tapetest(arg);
                break;
#ifdef BUILD_TAPE_TAPECTRL
            case OPT_TAPEUISCALE: /* TOHv4.3 */
                e = toh_cli_handle_tapeuiscale(arg);
                break;
#endif
            case OPT_RS423FILE:
                rs423_fn = arg;
                break;
            case OPT_TAPE:
                path = al_create_path(arg);
                ext = al_get_path_extension(path);
                if ((ext != NULL) && (    ! strcasecmp(ext, ".uef")
                                       || ! strcasecmp(ext, ".csw")
                                       || ! strcasecmp(ext, ".tibet")
                                       || ! strcasecmp(ext, ".tibetz") ) ) {
                    if (tape_vars.load_filename)
                        al_destroy_path(tape_vars.load_filename);
                    tape_vars.load_filename = path;
                } else {
                    fprintf(stderr,
                            "\"-tape\" file extension \"%s\" not recognised; tape ignored\n",
                            ext);
                }
                break;
                /* end TOHv4.2 merge */
            case OPT_EXEC:
                exec_fn = arg;
                break;
            case OPT_VDFS_ROOT:
                vroot = arg;
                break;
            case OPT_VDFS_DIR:
                vdir = arg;
                break;
            case OPT_PASTE_OS:
                debug_paste(arg, os_paste_start);
                break;
            case OPT_PASTE_KBD:
                debug_paste(arg, key_paste_start);
                break;
            case OPT_PRINT:
                print_filename = arg;
                print_filename_alloc = false;
                break;
        }
        state = OPT_GROUND;
        if (SHUTDOWN_OK != e) { break; } /* TOHv4.2 merge */
    }
    if (state != OPT_GROUND) {
        fputs("b-em: missing argument\n", stderr);
        exit(1);
    }

    if (SHUTDOWN_OK != e) { exit(e); } /* TOHv4.2 merge */

    al_init_native_dialog_addon();
    al_set_new_window_title(VERSION_STR);
    al_init_primitives_addon();
    if (!al_install_keyboard()) {
        log_fatal("main: unable to install keyboard");
        exit(1);
    }
    key_init();
    /* TOHv4.2 merge: second argument forces certain config
     * settings (tape related ones right now) not to be read.
     * Specifically we don't want overclock getting switched
     * on by default, nor do we want a previous UEF etc. file
     * being loaded if none is provided on the command line;
     * these will mess up the tests */
    config_load(cfg_fn, (tape_vars.testing_mode != 0) || (tape_vars.disable_debug_memview));
    log_open(log_file);
    log_info("main: starting %s", VERSION_STR);

    main_load_speeds();
    model_loadcfg();

    /* TOHv3, TOHv3.2, TOHv4.2 merge */
    acia_init(&sysacia);

    ALLEGRO_DISPLAY *display = video_init();
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
        disc_load(0, drives[0].discfn);
    disc_load(1, drives[1].discfn);

    /* TOHv4.3 */
    /* need the mutex for tapectrl to exist for all time, so set that up now */
#ifdef BUILD_TAPE_TAPECTRL
    tape_vars.tapectrl.mutex = al_create_mutex();
    if (NULL == tape_vars.tapectrl.mutex) {
        log_warn("ERROR: Failed to create tapectrl mutex");
        exit(SHUTDOWN_STARTUP_FAILURE /* 1 */);
    }
#endif

    /* TOHv4.2 merge */
    if (tape_vars.load_filename != NULL) {
        e = tape_load(&tape_state, &tape_vars, tape_vars.load_filename); /* TOHv4.3: must load before tapectrl spawn */
    }
    if (TAPE_E_OK != e) {
        log_warn("ERROR: Failed to load tape (code %d)", e);
        /* not fatal, but shutdown_exit_code was set by tape_handle_exception(),
           so the tests can work their magic */
    }

#ifdef BUILD_TAPE_TAPECTRL /* TOHv4.3 */
    if (    ( TAPE_E_OK == e )
         && ( TAPE_E_OK == tape_state.prior_exception )
         && ( tape_vars.testing_mode & 32 )
         // && ! tape_state.disabled_due_to_error
         && ! tape_vars.tapectrl_opened ) {

        e = tapectrl_start_gui_thread (&tape_state,
                                       &tape_vars,
                                       tape_vars.tapectrl_allow_resize,
                                       tape_vars.tapectrl_ui_scale * ( hiresdisplay ? 2.0f : 1.0f));
        if (TAPE_E_OK != e) {
            log_warn("ERROR: Failed to start tapectrl window (code %d)", e);
            exit(SHUTDOWN_STARTUP_FAILURE /* 1 */);
        }

    }
#endif

    e = TAPE_E_OK;

    if (mmccard_fn)
        mmccard_load(mmccard_fn);
    if (defaultwriteprot)
        drives[0].writeprot = drives[1].writeprot = 1;
    if (drives[0].discfn)
        gui_set_disc_wprot(0, drives[0].writeprot);
    if (drives[1].discfn)
        gui_set_disc_wprot(1, drives[1].writeprot);
    main_setspeed(emuspeed);
    debug_start(exec_fn, ! tape_vars.disable_debug_memview);
    // lovebug
    if (fullscreen)
        video_enterfullscreen();
    // lovebug end

    /* TOHv4.1: mitigate Allegro crash problems under
     * certain test environments (macOS) by offering a means
     * of retarding startup */
    if (tape_vars.testing_mode & TAPE_TEST_SLOW_STARTUP) {
#ifdef WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    /* TOHv3: if tape testing mode _CAT_ON_STARTUP is enabled, do a
       full catalogue of the tape on startup: */
    if (tape_vars.testing_mode & TAPE_TEST_CAT_ON_STARTUP) {
        findfilenames_new(&tape_state,
                          0,  /* 0 = silent; no UI window: */
                          ! tape_vars.permit_phantoms);
    }
    if (rs423_fn != NULL) { /* TOHv4: for -rs423file */
        sysacia_rec_start(rs423_fn);
    }

}


#define TAPEUISCALE_MIN     0.5
#define TAPEUISCALE_MAX     1.5
#define TAPEUISCALE_EPSILON 0.0001

#ifdef BUILD_TAPE_TAPECTRL
static int toh_cli_handle_tapeuiscale (const char * const argv) {
    float scale;
    scale = (float) atof(argv);
    if (    (scale < (TAPEUISCALE_MIN - TAPEUISCALE_EPSILON))
         || (scale > (TAPEUISCALE_MAX + TAPEUISCALE_EPSILON))) {
        fprintf(stderr,
                "\"-tapeuiscale\" value was illegal (legal range %.1f - %.1f): %s\n",
                TAPEUISCALE_MIN, TAPEUISCALE_MAX, argv);
        return SHUTDOWN_STARTUP_FAILURE;
    }
    tape_vars.tapectrl_ui_scale = scale;
    return TAPE_E_OK;
}
#endif

static int toh_cli_handle_expire (char *argv) { /* TOHv4.2 merge */
#define OSP 15625 /* "otherstuff ticks" per second (see 6502.c) */
    tape_vars.testing_expire_ticks = OSP * ((int64_t)atoi(argv));
    if (    (tape_vars.testing_expire_ticks < (1    * ((int64_t)OSP)))
         || (tape_vars.testing_expire_ticks > (2000 * ((int64_t)OSP)))) {
        fprintf(stderr,
                "\"-expire\" value was illegal: %s\n",
                argv);
        return SHUTDOWN_STARTUP_FAILURE;
    }
    return SHUTDOWN_OK;
}

static int toh_cli_handle_tapetest (char *argv) { /* TOHv4.2 merge */
    tape_vars.testing_mode = 0x7f & atoi(argv);
    tape_vars.disable_debug_memview = 1; /* TOHv4: prevent slew of race conditions on mem view window */
    // if (tape_vars.testing_mode & 32) {
    //     tape_vars.open_tapectrl_at_start = true;
    // }
    /* TOHv4-rc4: now permit -tapetest 0
     * this suppresses mem view window but doesn't do anything else */
    if (TAPE_TEST_BAD_MASK & tape_vars.testing_mode) {
        fprintf(stderr,
                "\"-tapetest\" value was illegal: %u\n",
                tape_vars.testing_mode);
        return SHUTDOWN_STARTUP_FAILURE;
    }
    return SHUTDOWN_OK;
}


static int toh_cli_handle_tapesave (char *argv) { /* TOHv4.2 merge */

    uint8_t fail;
    size_t len;
    uint8_t tapesave_filetype_bits, tapesave_do_compress;

    fail = 0;
    tapesave_filetype_bits = 0;
    tapesave_do_compress = 0;

    /* examine final N characters */

    len = strlen(argv);
    if (len < 5) {
        fail = 1;
    } else if (0 == strcasecmp(argv + (len - 4), ".uef")) {    /* compress UEFs by default */
        tapesave_filetype_bits = TAPE_FILETYPE_BITS_UEF;
        tapesave_do_compress = 1;
    } else if (0 == strcasecmp(argv + (len - 4), ".csw")) {    /* compress CSWs by default */
        tapesave_filetype_bits = TAPE_FILETYPE_BITS_CSW;
        tapesave_do_compress = 1;
    } else if (0 == strcasecmp(argv + (len - 4), ".wav")) {    /* TOHv4.2: new WAV mode */
        tapesave_filetype_bits = TAPE_FILETYPE_BITS_WAV;
    }

    if ( ( ! fail ) && (TAPE_FILETYPE_BITS_NONE == tapesave_filetype_bits ) ) {
        if (len < 7) {
            fail = 1;
        } else if (0 == strcasecmp(argv + (len - 6), ".tibet")) {
            tapesave_filetype_bits = TAPE_FILETYPE_BITS_TIBET;
        }
    }

    if ( ( ! fail ) && (TAPE_FILETYPE_BITS_NONE == tapesave_filetype_bits ) ) {
        if (len < 8) {
            fail = 1;
        } else if (0 == strcasecmp(argv + (len - 7), ".unzuef")) { /* extension disables compression */
            tapesave_filetype_bits = TAPE_FILETYPE_BITS_UEF;
        } else if (0 == strcasecmp(argv + (len - 7), ".unzcsw")) { /* extension disables compression */
            tapesave_filetype_bits = TAPE_FILETYPE_BITS_CSW;
        } else if (0 == strcasecmp(argv + (len - 7), ".tibetz")) {
            tapesave_filetype_bits = TAPE_FILETYPE_BITS_TIBET;
            tapesave_do_compress = 1;
        }
    }

    if (fail) {
        fprintf(stderr, "\"-tapesave\" filename was too short: %s\n", argv);
        exit(1);
    } else if (TAPE_FILETYPE_BITS_NONE == tapesave_filetype_bits) {
        fprintf(stderr, "\"-tapesave\": unrecognised filetype, ignored: %s\n", argv);
    } else {
        tape_vars.quitsave_config.filename      = argv;
        tape_vars.quitsave_config.filetype_bits = tapesave_filetype_bits;
        tape_vars.quitsave_config.do_compress   = tapesave_do_compress;
    }

    return SHUTDOWN_OK;

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

#ifdef BUILD_TAPE_TAPECTRL
            /* TOHv4.3: handle messages from tape control window */
            if ((now - prev_time) > 0.1) {
                /* this calls tape_handle_exception() itself, so will probably always return _OK: */
                /*e =*/ tapectrl_handle_messages(&tape_vars, &tape_state, &sysacia);
            }
#endif

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
                set_quit();
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
                          ! tape_vars.permit_phantoms);
    }
    log_debug("main: end loop");
}

void main_close()
{

    /* TOHv4.3 */
    tape_vars_t *tv;
    tape_state_t *ts;
    tv  = &tape_vars;
    ts  = &tape_state;

    /* TOHv4.3-a4: with tapectrl, allegro becomes crashy here on Windows
     * under conditions of rapid startup/shutdown. Investigating
     * reveals that the display bitmap contains an invalid pointer
     * at the point of the al_destroy_display() call in video_close().
     * Disposing of the display bitmap ASAP early seems to mitigate this
     * crash. */
    al_set_target_bitmap(NULL);

    gui_tapecat_close();
    gui_keydefine_close();
    
    sysacia_rec_stop(); /* TOHv4 */

    debug_kill();

    config_save();
    cmos_save(&models[curmodel]);

    midi_close();
    mem_close();
    
    /* TOHv3 */

    tube_6502_close();
    arm_close();
    x86_close();
    z80_close();
    w65816_close();
    n32016_close();
    mc6809nc_close();
    sprow_close();
    disc_free(0);
    disc_free(1);
    scsi_close();
    ide_close();
    vdfs_close();
    music5000_close();
    ddnoise_close();
    tapenoise_close();
    al_destroy_timer(timer);
    al_destroy_event_queue(queue);
    led_close();
#ifdef BUILD_TAPE_TAPECTRL
    tapectrl_finish(&(tv->tapectrl), &(tv->tapectrl_opened));
#endif
    tape_save_on_shutdown(ts,
                          tv->record_activated,
                          &(ts->filetype_bits),    /* save using same type as we loaded (or any type if not loaded) */
                          tv->save_prefer_112,     /* TOHv3.2 */
                          tv->wav_use_phase_shift, /* TOHv4.2 */
                          &(tv->quitsave_config));
    tape_state_finish(ts, 0); /* 0 = don't alter menus */
    video_close();
    model_close();
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

void set_quit(void)
{
    quitting = true; /* for outer loops */
    cycles = 0;      /* stop the host (I/O) processor */
    tubecycles = 0;  /* stop the tube processor */
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
