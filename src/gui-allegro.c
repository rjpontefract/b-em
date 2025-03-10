#include "b-em.h"
#include <allegro5/allegro_native_dialog.h>
#include "gui-allegro.h"

#include "6502.h"
#include "ide.h"
#include "config.h"
#include "debugger.h"
#include "ddnoise.h"
#include "disc.h"
#include "fullscreen.h"
#include "joystick.h"
#include "keyboard.h"
#include "keydef-allegro.h"
#include "main.h"
#include "mem.h"
#include "mmb.h"
#include "model.h"
#include "mouse.h"
#include "music5000.h"
#include "mmccard.h"
#include "paula.h"
#include "savestate.h"
#include "sid_b-em.h"
#include "scsi.h"
#include "sdf.h"
#include "sound.h"
#include "sn76489.h"
#include "sysacia.h"
#include "tape.h"
#include "tapecat-allegro.h"
#include "textsave.h"
#include "tube.h"
#include "uservia.h"
#include "video.h"
#include "video_render.h"
#include "vdfs.h"
#include "serial.h"
#include "tapenoise.h" /* TOHv4.1-rc9 */

#if defined(HAVE_JACK_JACK_H) || defined(HAVE_ALSA_ASOUNDLIB_H)
#define HAVE_LINUX_MIDI
#include "midi-linux.h"
#endif

#define ROM_LABEL_LEN 50

/* pclose() and popen() on Windows are _pclose() and _popen() */
#ifdef _WIN32
#  define pclose _pclose
#  define popen _popen
#endif

typedef struct {
    const char *label;
    int itemno;
} menu_map_t;

static enum pdest {
    PDEST_NONE,
    PDEST_FILE,
    PDEST_PIPE
} print_dest;

static ALLEGRO_MENU *disc_menu;
static ALLEGRO_MENU *rom_menu;

/* TOHv3.2, 3.3: */
static ALLEGRO_MENU *tape_opts_save_menu;
/* TOHv3.3: */
static ALLEGRO_MENU *tape_opts_load_menu;
/* TOHv3: */
static ALLEGRO_MENU *tape_save_menu;
static ALLEGRO_MENU *tape_main_menu;
/* can define BUILD_TAPE_DEV_MENU, to get the "Mischief..." menu items
   within the Tape menu: */
#ifdef BUILD_TAPE_DEV_MENU
static ALLEGRO_MENU *tape_dev_menu;
#endif
static ALLEGRO_MENU *create_tape_save_menu(void);
#ifdef BUILD_TAPE_DEV_MENU
static ALLEGRO_MENU *create_tape_dev_menu(void);
#endif
static void tape_save_menu_nothing(ALLEGRO_EVENT *ev);

static inline int menu_id_num(menu_id_t id, int num)
{
    return (num << 8) | id;
}

static inline menu_id_t menu_get_id(ALLEGRO_EVENT *event)
{
    return event->user.data1 & 0xff;
}

static inline int menu_get_num(ALLEGRO_EVENT *event)
{
    return event->user.data1 >> 8;
}

static void add_checkbox_item(ALLEGRO_MENU *parent, char const *title, uint16_t id, bool checked)
{
    int flags = ALLEGRO_MENU_ITEM_CHECKBOX;
    if (checked)
        flags |= ALLEGRO_MENU_ITEM_CHECKED;
    al_append_menu_item(parent, title, id, flags, NULL, NULL);
}

static void add_radio_item(ALLEGRO_MENU *parent, char const *title, uint16_t id, int this_value, int cur_value)
{
    add_checkbox_item(parent, title, menu_id_num(id, this_value), this_value == cur_value);
}

static void add_radio_set(ALLEGRO_MENU *parent, char const **labels, uint16_t id, int cur_value)
{
    const char *label;

    for (int i = 0; (label = *labels++); i++)
        add_checkbox_item(parent, label, menu_id_num(id, i), i == cur_value);
}

static int menu_cmp(const void *va, const void *vb)
{
    menu_map_t *a = (menu_map_t *)va;
    menu_map_t *b = (menu_map_t *)vb;
    return strcasecmp(a->label, b->label);
}

static void add_sorted_set(ALLEGRO_MENU *parent, menu_map_t *map, size_t items, uint16_t id, int cur_value)
{
    qsort(map, items, sizeof(menu_map_t), menu_cmp);
    for (int i = 0; i < items; i++) {
        int ino = map[i].itemno;
        add_checkbox_item(parent, map[i].label, menu_id_num(id, ino), ino == cur_value);
    }
}

static ALLEGRO_MENU *create_file_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    al_append_menu_item(menu, "Hard Reset", IDM_FILE_RESET, 0, NULL, NULL);
    al_append_menu_item(menu, "Load state...", IDM_FILE_LOAD_STATE, 0, NULL, NULL);
    al_append_menu_item(menu, "Save State...", IDM_FILE_SAVE_STATE, 0, NULL, NULL);
    al_append_menu_item(menu, "Save Screenshot...", IDM_FILE_SCREEN_SHOT, 0, NULL, NULL);
    al_append_menu_item(menu, "Save Screen as Text...", IDM_FILE_SCREEN_TEXT, 0, NULL, NULL);
    add_checkbox_item(menu, "Print to file", IDM_FILE_PRINT, print_dest == PDEST_FILE);
    add_checkbox_item(menu, "Print to command", IDM_FILE_PCMD, print_dest == PDEST_PIPE);
    add_checkbox_item(menu, "Serial to file", IDM_FILE_SERIAL, sysacia_fp);
    add_checkbox_item(menu, music5000_rec.prompt, IDM_FILE_M5000, music5000_rec.fp);
    add_checkbox_item(menu, paula_rec.prompt, IDM_FILE_PAULAREC, paula_rec.fp);
    add_checkbox_item(menu, sound_rec.prompt, IDM_FILE_SOUNDREC, sound_rec.fp);
    al_append_menu_item(menu, "Exit", IDM_FILE_EXIT, 0, NULL, NULL);
    return menu;
}

static ALLEGRO_MENU *create_edit_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    al_append_menu_item(menu, "Paste via OS", IDM_EDIT_PASTE_OS, 0, NULL, NULL);
    al_append_menu_item(menu, "Paste via keyboard", IDM_EDIT_PASTE_KB, 0, NULL, NULL);
    add_checkbox_item(menu, "Printer to clipboard", IDM_EDIT_COPY, prt_clip_str);
    return menu;
}

static ALLEGRO_MENU *create_disc_new_menu(int drive)
{
    ALLEGRO_MENU *menu = al_create_menu();

    al_append_menu_item(menu, "Acorn DFS, Single-sided, 40T", menu_id_num(IDM_DISC_NEW_DFS_10S_SIN_40T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Acorn DFS, Single-sided, 80T", menu_id_num(IDM_DISC_NEW_DFS_10S_SIN_80T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Acorn DFS, Double-sided, 40T", menu_id_num(IDM_DISC_NEW_DFS_10S_INT_40T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Acorn DFS, Double-sided, 80T", menu_id_num(IDM_DISC_NEW_DFS_10S_INT_80T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "ADFS, Single-sided, 40T (S)", menu_id_num(IDM_DISC_NEW_ADFS_S, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "ADFS, Single-sided, 80T (M)", menu_id_num(IDM_DISC_NEW_ADFS_M, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "ADFS, Double-sided, 80T (L)", menu_id_num(IDM_DISC_NEW_ADFS_L, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Solidisk DDFS, Single-sided, 40T", menu_id_num(IDM_DISC_NEW_DFS_16S_SIN_40T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Solidisk DDFS, Single-sided, 80T", menu_id_num(IDM_DISC_NEW_DFS_16S_SIN_80T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Solidisk DDFS, Double-sided, 80T", menu_id_num(IDM_DISC_NEW_DFS_16S_INT_80T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Watford DDFS, Single-sided, 40T", menu_id_num(IDM_DISC_NEW_DFS_18S_SIN_40T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Watford DDFS, Single-sided, 80T", menu_id_num(IDM_DISC_NEW_DFS_18S_SIN_80T, drive), 0, NULL, NULL);
    al_append_menu_item(menu, "Watford DDFS, Double-sided, 80T", menu_id_num(IDM_DISC_NEW_DFS_18S_INT_80T, drive), 0, NULL, NULL);
    return menu;
}

static ALLEGRO_MENU *create_disc_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();

    al_append_menu_item(menu, "Autoboot disc in 0/2...", IDM_DISC_AUTOBOOT, 0, NULL, NULL);
    al_append_menu_item(menu, "Load disc :0/2...", menu_id_num(IDM_DISC_LOAD, 0), 0, NULL, NULL);
    al_append_menu_item(menu, "Load disc :1/3...", menu_id_num(IDM_DISC_LOAD, 1), 0, NULL, NULL);
    al_append_menu_item(menu, "Load MMB file...", IDM_DISC_MMB_LOAD, 0, NULL, NULL);
    al_append_menu_item(menu, "Load SD Card...", IDM_DISC_MMC_LOAD, 0, NULL, NULL);
    al_append_menu_item(menu, "Eject disc :0/2", menu_id_num(IDM_DISC_EJECT, 0), 0, NULL, NULL);
    al_append_menu_item(menu, "Eject disc :1/3", menu_id_num(IDM_DISC_EJECT, 1), 0, NULL, NULL);
    al_append_menu_item(menu, "Eject MMB file", IDM_DISC_MMB_EJECT, 0, NULL, NULL);
    al_append_menu_item(menu, "Eject SD Card", IDM_DISC_MMC_EJECT, 0, NULL, NULL);
    al_append_menu_item(menu, "New disc :0/2...", 0, 0, NULL, create_disc_new_menu(0));
    al_append_menu_item(menu, "New disc :1/3...", 0, 0, NULL, create_disc_new_menu(1));
    add_checkbox_item(menu, "Write protect disc :0/2", menu_id_num(IDM_DISC_WPROT, 0), writeprot[0]);
    add_checkbox_item(menu, "Write protect disc :1/3", menu_id_num(IDM_DISC_WPROT, 1), writeprot[1]);
    add_checkbox_item(menu, "Default write protect", IDM_DISC_WPROT_D, defaultwriteprot);
    add_checkbox_item(menu, "IDE hard disc", IDM_DISC_HARD_IDE, ide_enable);
    add_checkbox_item(menu, "SCSI hard disc", IDM_DISC_HARD_SCSI, scsi_enabled);
    add_checkbox_item(menu, "VDFS Enabled", IDM_DISC_VDFS_ENABLE, vdfs_enabled);
    al_append_menu_item(menu, "Choose VDFS Root...", IDM_DISC_VDFS_ROOT, 0, NULL, NULL);
    disc_menu = menu;
    return menu;
}

void gui_allegro_set_eject_text(int drive, ALLEGRO_PATH *path)
{
    char temp[256];
    if (path)
        snprintf(temp, sizeof temp, "Eject drive %s: %s", drive ? "1/3" : "0/2", al_get_path_filename(path));
    else
        snprintf(temp, sizeof temp, "Eject drive %s", drive ? "1/3" : "0/2");
    al_set_menu_item_caption(disc_menu, menu_id_num(IDM_DISC_EJECT, drive), temp);
}




static ALLEGRO_MENU *create_tape_menu(void)
{
    ALLEGRO_MENU *speed = al_create_menu();
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *m_opts = al_create_menu(); /* TOHv3.2 */
    tape_opts_save_menu = al_create_menu();
    tape_opts_load_menu = al_create_menu();  /* TOHv3.3 */
    int fflags;
    al_append_menu_item(menu, "Load tape...", IDM_TAPE_LOAD, 0, NULL, NULL);
    al_append_menu_item(menu, "Save tape copy...", IDM_TAPE_SAVE, 0, NULL, create_tape_save_menu());
    al_append_menu_item(menu, "Eject tape", IDM_TAPE_EJECT, 0, NULL, NULL);
    al_append_menu_item(menu, "Rewind tape playback", IDM_TAPE_REWIND, 0, NULL, NULL);
    fflags = ALLEGRO_MENU_ITEM_CHECKBOX | (tape_is_record_activated(&tape_vars) ? ALLEGRO_MENU_ITEM_CHECKED : 0);
    al_append_menu_item(menu, "Record and append to tape", IDM_TAPE_RECORD, fflags, NULL, NULL);
    fflags = 0;
#ifdef BUILD_TAPE_MENU_GREYOUT_CAT
    if ( ! tape_peek_for_data(&tape_state) ) {
        fflags |= ALLEGRO_MENU_ITEM_DISABLED;
    }
#endif
    al_append_menu_item(menu, "Catalogue tape", IDM_TAPE_CAT, fflags, NULL, NULL);

    /* updated for TOHv3.2: */
    if (tape_vars.overclock) {
        /*nflags = ALLEGRO_MENU_ITEM_CHECKBOX;*/
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED;
    } else {
        /*nflags = ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED;*/
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX;
    }
    al_append_menu_item(speed, "Overclock ACIA + fast DCD (dubious)", IDM_TAPE_TURBO_OVERCLOCK, fflags, NULL, NULL);
    /*al_append_menu_item(speed, "Fast (unreliable)", IDM_TAPE_TURBO_SKIP, fflags, NULL, NULL);*/
    if (tape_vars.strip_silence_and_leader) {
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED;
    } else {
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX;
    }
    al_append_menu_item(speed, "Skip leader/gaps + fast DCD", IDM_TAPE_TURBO_SKIP, fflags, NULL, NULL);
    al_append_menu_item(menu, "Options", 0, 0, NULL, m_opts);
    al_append_menu_item(menu, "Turbo load", 0, 0, NULL, speed);
    if (tape_vars.save_always_117) {
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED;
    } else {
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX;
    }
    al_append_menu_item(tape_opts_save_menu, "UEF: Emit baud chunk (117) before each block",
                        IDM_TAPE_OPTS_SAVE_UEF_FORCE_117, fflags, NULL, NULL);
    if (tape_vars.save_prefer_112) {
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED;
    } else {
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX;
    }
    al_append_menu_item(tape_opts_save_menu, "UEF: Use chunk 112, not 116, for gaps",
                        IDM_TAPE_OPTS_SAVE_UEF_FORCE_112, fflags, NULL, NULL);
    if (tape_vars.save_do_not_generate_origin_on_append) {
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED;
    } else {
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX;
    }
    al_append_menu_item(tape_opts_save_menu, "UEF: Suppress origin chunk (0) on append",
                        IDM_TAPE_OPTS_SAVE_UEF_SUPPRESS_ORGN_ON_APPEND, fflags, NULL, NULL);
    if ( ! tape_vars.disable_phantom_block_protection ) {
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED;
    } else {
        fflags = ALLEGRO_MENU_ITEM_CHECKBOX;
    }
    /*al_append_menu_item(tape_opts_load_menu, "Do not filter out phantom blocks",*/
    al_append_menu_item(tape_opts_load_menu, "Filter out phantom blocks",
                        IDM_TAPE_OPTS_LOAD_NO_FILTER_PHANTOMS, fflags, NULL, NULL);
    al_append_menu_item(m_opts, "Loading", 0, 0, NULL, tape_opts_load_menu);
    al_append_menu_item(m_opts, "Saving", 0, 0, NULL, tape_opts_save_menu); /*m_uef_save);*/
#ifdef BUILD_TAPE_DEV_MENU
    al_append_menu_item(menu, "Mischief", IDM_TAPE_DEV, 0, NULL, create_tape_dev_menu());
#endif
    tape_main_menu = menu;
    return menu;
}

static void gen_rom_label(int slot, char *dest)
{
    const char *rr = rom_slots[slot].swram ? "RAM" : "ROM";
    const uint8_t *detail = mem_romdetail(slot);
    const char *name = rom_slots[slot].name;
    if (detail) {
        int ver = *detail++;
        if (name)
            snprintf(dest, ROM_LABEL_LEN, "%02d %s: %s %02X (%s)", slot, rr, detail, ver, name);
        else
            snprintf(dest, ROM_LABEL_LEN, "%02d %s: %s %02X", slot, rr, detail, ver);
    } else {
        if (name)
            snprintf(dest, ROM_LABEL_LEN, "%02d %s: %s", slot, rr, name);
        else
            snprintf(dest, ROM_LABEL_LEN, "%02d %s", slot, rr);
    }
}

static ALLEGRO_MENU *create_rom_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    for (int slot = ROM_NSLOT-1; slot >= 0; slot--) {
        char label[ROM_LABEL_LEN];
        gen_rom_label(slot, label);
        ALLEGRO_MENU *sub = al_create_menu();
        al_append_menu_item(sub, "Load...", menu_id_num(IDM_ROMS_LOAD, slot), 0, NULL, NULL);
        al_append_menu_item(sub, "Clear", menu_id_num(IDM_ROMS_CLEAR, slot), 0, NULL, NULL);
        add_checkbox_item(sub, "RAM", menu_id_num(IDM_ROMS_RAM, slot), rom_slots[slot].swram);
        al_append_menu_item(menu, label, slot+1, 0, NULL, sub);
    }
    rom_menu = menu;
    return menu;
}

static void update_rom_menu(void)
{
    ALLEGRO_MENU *menu = rom_menu;
    
    for (int slot = ROM_NSLOT-1; slot >= 0; slot--) {
        char label[ROM_LABEL_LEN];
        gen_rom_label(slot, label);
        al_set_menu_item_caption(menu, slot-ROM_NSLOT+1, label);
        ALLEGRO_MENU *sub = al_find_menu(menu, slot+1);
        if (sub) {
            int flags = rom_slots[slot].swram ? ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED : ALLEGRO_MENU_ITEM_CHECKBOX;
            al_set_menu_item_flags(sub, menu_id_num(IDM_ROMS_RAM, slot), flags);
        }
        else
            log_debug("gui-allegro: ROM sub-menu not found for slot %d", slot);
    }
}

static ALLEGRO_MENU *create_model_menu(void)
{
    menu_map_t *map = calloc(model_count * 2, sizeof(menu_map_t));
    if (map) {
        ALLEGRO_MENU *menu = al_create_menu();
        menu_map_t *groups = map + model_count;
        int ngroup = 0;
        for (int model_no = 0; model_no < model_count; ++model_no) {
            const char *group = models[model_no].group;
            if (group) {
                bool found = false;
                for (int group_no = 0; group_no < ngroup; ++group_no) {
                    if (!strcmp(group, groups[group_no].label)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    groups[ngroup].label = group;
                    groups[ngroup].itemno = ngroup;
                    ++ngroup;
                }
            }
        }
        qsort(groups, ngroup, sizeof(menu_map_t), menu_cmp);
        for (int group_no = 0; group_no < ngroup; ++group_no) {
            const char *group_label = groups[group_no].label;
            int item_no = 0;
            for (int model_no = 0; model_no < model_count; ++model_no) {
                const char *model_group = models[model_no].group;
                if (model_group && !strcmp(model_group, group_label)) {
                    map[item_no].label = models[model_no].name;
                    map[item_no].itemno = model_no;
                    ++item_no;
                }
            }
            ALLEGRO_MENU *sub = al_create_menu();
            add_sorted_set(sub, map, item_no, IDM_MODEL, curmodel);
            al_append_menu_item(menu, groups[group_no].label, 0, 0, NULL, sub);
        }
        int item_no = 0;
        for (int model_no = 0; model_no < model_count; ++model_no) {
            if (!models[model_no].group) {
                map[item_no].label = models[model_no].name;
                map[item_no].itemno = model_no;
                ++item_no;
            }
        }
        add_sorted_set(menu, map, item_no, IDM_MODEL, curmodel);
        free(map);
        return menu;
    }
    else {
        log_fatal("gui-allegro: out of memory");
        exit(1);
    }
}

static ALLEGRO_MENU *create_tube_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *sub = al_create_menu();
    menu_map_t *map = malloc(num_tubes * sizeof(menu_map_t));
    if (map) {
        for (int i = 0; i < num_tubes; ++i) {
            map[i].label = tubes[i].name;
            map[i].itemno = i;
        }
        add_sorted_set(menu, map, num_tubes, IDM_TUBE, curtube);
        for (int i = 0; i < NUM_TUBE_SPEEDS; i++)
            add_radio_item(sub, tube_speeds[i].name, IDM_TUBE_SPEED, i, tube_speed_num);
        al_append_menu_item(menu, "Tube speed", 0, 0, NULL, sub);
        return menu;
    }
    else {
        log_fatal("gui-allegro: out of memory");
        exit(1);
    }
}

static const char *mode7_font_files[] = { "saa5050", "brandy", "basicsdl", "original", NULL };
static const char *mode7_font_names[] = { "SAA5050", "Brandy BASIC", "BBC BASIC for SDL", "B-Em Original", NULL };
static int mode7_font_index;

static ALLEGRO_MENU *create_m7font_menu(void)
{
    int c = 0;
    const char *ptr;
    while ((ptr = mode7_font_files[c])) {
        if (!strcmp(ptr, mode7_fontfile)) {
            mode7_font_index = c;
            break;
        }
        ++c;
    }
    ALLEGRO_MENU *sub = al_create_menu();
    add_radio_set(sub, mode7_font_names, IDM_VIDEO_MODE7_FONT, mode7_font_index);
    return sub;
}

static const char *border_names[] = { "None", "Medium", "Full", NULL };
static const char *vmode_names[] = { "Scaled", "Interlace", "Scanlines", "Line doubling", NULL };
static const char *colout_names[] = { "RGB", "PAL", "Green Mono", "Amber Mono", "White Mono", NULL };
static const char *win_mult_names[] = { "Freeform", "1x", "2x", "3x", NULL };
static const char *led_location_names[] = { "None", "Overlapped", "Separate", NULL };
static const char *led_visibility_names[] = { "When changed", "When changed or transient", "Always", NULL };

static ALLEGRO_MENU *create_video_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *sub = al_create_menu();
    add_radio_set(sub, vmode_names, IDM_VIDEO_DISPTYPE, vid_dtype_user);
    al_append_menu_item(menu, "Display type...", 0, 0, NULL, sub);
    sub = al_create_menu();
    add_radio_set(sub, colout_names, IDM_VIDEO_COLTYPE, vid_colour_out);
    al_append_menu_item(menu, "Colour type...", 0, 0, NULL, sub);
    sub = al_create_menu();
    add_radio_set(sub, border_names, IDM_VIDEO_BORDERS, vid_fullborders);
    al_append_menu_item(menu, "Borders...", 0, 0, NULL, sub);
    sub = al_create_menu();
    add_radio_set(sub, win_mult_names, IDM_VIDEO_WIN_MULT, vid_win_multiplier);
    al_append_menu_item(menu, "Default window scaling...", 0, 0, NULL, sub);
    al_append_menu_item(menu, "Reset Window Size", IDM_VIDEO_WINSIZE, 0, NULL, NULL);
    add_checkbox_item(menu, "Fullscreen", IDM_VIDEO_FULLSCR, fullscreen);
    add_checkbox_item(menu, "NuLA", IDM_VIDEO_NULA, !nula_disable);
    sub = al_create_menu();
    al_append_menu_item(menu, "LED location...", 0, 0, NULL, sub);
    add_radio_set(sub, led_location_names, IDM_VIDEO_LED_LOCATION, vid_ledlocation);
    sub = al_create_menu();
    al_append_menu_item(menu, "LED visibility...", 0, 0, NULL, sub);
    add_radio_set(sub, led_visibility_names, IDM_VIDEO_LED_VISIBILITY, vid_ledvisibility);
    al_append_menu_item(menu, "Mode 7 Font...", 0, 0, NULL, create_m7font_menu());
    return menu;
}

static const char *sid_names[] =
{
    "6581",
    "8580",
    "8580 + digi boost",
    "6581R4",
    "6581R3 4885",
    "6581R3 0486S",
    "6581R3 3984",
    "6581R4AR 3789",
    "6581R3 4485",
    "6581R4 1986S",
    "8580R5 3691",
    "8580R5 3691 + digi boost",
    "8580R5 1489",
    "8580R5 1489 + digi boost",
    NULL
};

static ALLEGRO_MENU *create_sid_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *sub = al_create_menu();
    add_radio_set(sub, sid_names, IDM_SID_TYPE, cursid);
    al_append_menu_item(menu, "Model", 0, 0, NULL, sub);
    sub = al_create_menu();
    add_radio_item(sub, "Interpolating", IDM_SID_METHOD, 0, sidmethod);
    add_radio_item(sub, "Resampling",    IDM_SID_METHOD, 1, sidmethod);
    al_append_menu_item(menu, "Sample method", 0, 0, NULL, sub);
    return menu;
}

static const char *wave_names[] = { "Square", "Saw", "Sine", "Triangle", "SID", NULL };
static const char *dd_type_names[] = { "5.25\"", "3.5\"", NULL };
static const char *dd_noise_vols[] = { "33%", "66%", "100%", NULL };
static const char *filt_freq[] = { "Original (3214Hz)", "16kHz", NULL };

static ALLEGRO_MENU *create_sound_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *sub;
    add_checkbox_item(menu, "Internal sound chip",   IDM_SOUND_INTERNAL,  sound_internal);
    add_checkbox_item(menu, "BeebSID",               IDM_SOUND_BEEBSID,   sound_beebsid);
    add_checkbox_item(menu, "Music 5000",            IDM_SOUND_MUSIC5000, sound_music5000);
    sub = al_create_menu();
    add_radio_set(sub, filt_freq, IDM_SOUND_MFILT, music5000_fno);
    al_append_menu_item(menu, "Music 5000 Filter", 0, 0, NULL, sub);
    add_checkbox_item(menu, "Paula",                 IDM_SOUND_PAULA,     sound_paula);
    add_checkbox_item(menu, "Printer port DAC",      IDM_SOUND_DAC,       sound_dac);
    add_checkbox_item(menu, "Disc drive noise",      IDM_SOUND_DDNOISE,   sound_ddnoise);
    add_checkbox_item(menu, "Tape signal",           IDM_SOUND_TAPE,      sound_tape);
    add_checkbox_item(menu, "Tape relay clicks",     IDM_SOUND_TAPE_RELAY, sound_tape_relay); /* TOHv2 */
    add_checkbox_item(menu, "Internal sound filter", IDM_SOUND_FILTER,    sound_filter);
    sub = al_create_menu();
    add_radio_set(sub, wave_names, IDM_WAVE, curwave);
    al_append_menu_item(menu, "Internal waveform", 0, 0, NULL, sub);
    al_append_menu_item(menu, "reSID configuration", 0, 0, NULL, create_sid_menu());
    sub = al_create_menu();
    add_radio_set(sub, dd_type_names, IDM_DISC_TYPE, ddnoise_type);
    al_append_menu_item(menu, "Disc drive type", 0, 0, NULL, sub);
    sub = al_create_menu();
    add_radio_set(sub, dd_noise_vols, IDM_DISC_VOL, ddnoise_vol);
    al_append_menu_item(menu, "Disc noise volume", 0, 0, NULL, sub);
    return menu;
}

#ifdef HAVE_LINUX_MIDI

static ALLEGRO_MENU *create_midi_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *sub = al_create_menu();
#ifdef HAVE_JACK_JACK_H
    add_checkbox_item(sub, "JACK MIDI", IDM_MIDI_M4000_JACK, midi_music4000.jack_enabled);
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    add_checkbox_item(sub, "ALSA Sequencer", IDM_MIDI_M4000_ASEQ, midi_music4000.alsa_seq_enabled);
    add_checkbox_item(sub, "ALSA Raw MIDI",  IDM_MIDI_M4000_ARAW, midi_music4000.alsa_raw_enabled);
#endif
    al_append_menu_item(menu, "M4000 Keyboard", 0, 0, NULL, sub);
    sub = al_create_menu();
#ifdef HAVE_JACK_JACK_H
    add_checkbox_item(sub, "JACK MIDI", IDM_MIDI_M2000_OUT1_JACK, midi_music2000_out1.jack_enabled);
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    add_checkbox_item(sub, "ALSA Sequencer", IDM_MIDI_M2000_OUT1_ASEQ, midi_music2000_out1.alsa_seq_enabled);
    add_checkbox_item(sub, "ALSA Raw MIDI",  IDM_MIDI_M2000_OUT1_ARAW, midi_music2000_out1.alsa_raw_enabled);
#endif
    al_append_menu_item(menu, "M2000 I/F O/P 1", 0, 0, NULL, sub);
    sub = al_create_menu();
#ifdef HAVE_JACK_JACK_H
    add_checkbox_item(sub, "JACK MIDI", IDM_MIDI_M2000_OUT2_JACK, midi_music2000_out2.jack_enabled);
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    add_checkbox_item(sub, "ALSA Sequencer", IDM_MIDI_M2000_OUT2_ASEQ, midi_music2000_out2.alsa_seq_enabled);
    add_checkbox_item(sub, "ALSA Raw MIDI",  IDM_MIDI_M2000_OUT2_ARAW, midi_music2000_out2.alsa_raw_enabled);
#endif
    al_append_menu_item(menu, "M2000 I/F O/P 2", 0, 0, NULL, sub);
    sub = al_create_menu();
#ifdef HAVE_JACK_JACK_H
    add_checkbox_item(sub, "JACK MIDI", IDM_MIDI_M2000_OUT3_JACK, midi_music2000_out3.jack_enabled);
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
    add_checkbox_item(sub, "ALSA Sequencer", IDM_MIDI_M2000_OUT3_ASEQ, midi_music2000_out3.alsa_seq_enabled);
    add_checkbox_item(sub, "ALSA Raw MIDI",  IDM_MIDI_M2000_OUT3_ARAW, midi_music2000_out3.alsa_raw_enabled);
#endif
    al_append_menu_item(menu, "M2000 I/F O/P 3", 0, 0, NULL, sub);
    return menu;
}

#endif

static ALLEGRO_MENU *create_keyboard_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    ALLEGRO_MENU *sub = al_create_menu();
    add_radio_set(sub, bem_key_modes, IDM_KEY_MODE, key_mode);
    al_append_menu_item(menu, "Keyboard Mode", 0, 0, NULL, sub);
    add_checkbox_item(menu, "Map CAPS/CTRL to A/S", IDM_KEY_AS, keyas);
    add_checkbox_item(menu, "PC/XT Keypad Mode", IDM_KEY_PAD, keypad);
    al_append_menu_item(menu, "Remap Keyboard", IDM_KEY_REDEFINE, 0, NULL, NULL);
    return menu;
}

static ALLEGRO_MENU *create_joystick_menu(int joystick)
{
    ALLEGRO_MENU *menu = al_create_menu();
    for (int i = 0; i < joystick_count; i++) if (joystick_names[i])
        add_checkbox_item(menu, joystick_names[i], menu_id_num(IDM_JOYSTICK + joystick, i), i == joystick_index[joystick]);
    return menu;
}

static ALLEGRO_MENU *create_joymap_menu(int joystick)
{
    ALLEGRO_MENU *menu = al_create_menu();
    for (int i = 0; i < joymap_count; i++)
        add_checkbox_item(menu, joymaps[i].name, menu_id_num(IDM_JOYMAP + joystick, i), i == joymap_index[joystick]);
    return menu;
}

static ALLEGRO_MENU *create_joysticks_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    add_checkbox_item(menu, "Tricky SEGA Adapter", IDM_TRIACK_SEGA_ADAPTER, autopause);
    al_append_menu_item(menu, "Joystick", 0, 0, NULL, create_joystick_menu(0));
    al_append_menu_item(menu, "Joystick Map", 0, 0, NULL, create_joymap_menu(0));
    if (joystick_count > 1) {
        al_append_menu_item(menu, "Joystick 2", 0, 0, NULL, create_joystick_menu(1));
        al_append_menu_item(menu, "Joystick 2 Map", 0, 0, NULL, create_joymap_menu(1));
    }
    return menu;
}


static const char *jim_sizes[] =
{
    "None (disabled)",
    "16M",
    "64M",
    "256M",
    "480M",
    "996M",
    NULL
};

static ALLEGRO_MENU *create_jim_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    add_radio_set(menu, jim_sizes, IDM_JIM_SIZE, mem_jim_size);
    return menu;
}

static ALLEGRO_MENU *create_settings_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    al_append_menu_item(menu, "Video...", 0, 0, NULL, create_video_menu());
    al_append_menu_item(menu, "Sound...", 0, 0, NULL, create_sound_menu());
#ifdef HAVE_LINUX_MIDI
    al_append_menu_item(menu, "MIDI", 0, 0, NULL, create_midi_menu());
#endif
    al_append_menu_item(menu, "Keyboard", 0, 0, NULL, create_keyboard_menu());
    al_append_menu_item(menu, "Jim Memory", 0, 0, NULL, create_jim_menu());
    add_checkbox_item(menu, "Auto-Pause", IDM_AUTO_PAUSE, autopause);
    add_checkbox_item(menu, "Mouse (AMX)", IDM_MOUSE_AMX, mouse_amx);
    if (joystick_count > 0)
        al_append_menu_item(menu, "Joysticks", 0, 0, NULL, create_joysticks_menu());
    add_checkbox_item(menu, "Joystick Mouse", IDM_MOUSE_STICK, mouse_stick);
    return menu;
}

static ALLEGRO_MENU *create_speed_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    add_radio_item(menu, "Paused", IDM_SPEED, EMU_SPEED_PAUSED, emuspeed);
    for (int i = 0; i < num_emu_speeds; i++)
        add_radio_item(menu, emu_speeds[i].name, IDM_SPEED, i, emuspeed);
    add_radio_item(menu, "Full-speed", IDM_SPEED, EMU_SPEED_FULL, emuspeed);
    add_checkbox_item(menu, "Auto Frameskip", IDM_AUTOSKIP, autoskip);
    return menu;
}

static ALLEGRO_MENU *create_debug_menu(void)
{
    ALLEGRO_MENU *menu = al_create_menu();
    add_checkbox_item(menu, "Debugger", IDM_DEBUGGER, debug_core);
    add_checkbox_item(menu, "Debug Tube", IDM_DEBUG_TUBE, debug_tube);
    al_append_menu_item(menu, "Break", IDM_DEBUG_BREAK, 0, NULL, NULL);
    return menu;
}

void gui_allegro_init(ALLEGRO_EVENT_QUEUE *queue, ALLEGRO_DISPLAY *display)
{
    ALLEGRO_MENU *menu = al_create_menu();
    al_append_menu_item(menu, "File", 0, 0, NULL, create_file_menu());
    al_append_menu_item(menu, "Edit", 0, 0, NULL, create_edit_menu());
    al_append_menu_item(menu, "Disc", 0, 0, NULL, create_disc_menu());
    al_append_menu_item(menu, "Tape", 0, 0, NULL, create_tape_menu());
    al_append_menu_item(menu, "ROM", 0, 0, NULL, create_rom_menu());
    al_append_menu_item(menu, "Model", 0, 0, NULL, create_model_menu());
    al_append_menu_item(menu, "Tube", 0, 0, NULL, create_tube_menu());
    al_append_menu_item(menu, "Settings", 0, 0, NULL, create_settings_menu());
    al_append_menu_item(menu, "Speed", 0, 0, NULL, create_speed_menu());
    al_append_menu_item(menu, "Debug", 0, 0, NULL, create_debug_menu());
    al_set_display_menu(display, menu);
    al_register_event_source(queue, al_get_default_menu_event_source());
}

/* TOHv3.2 */
void gui_set_record_mode (uint8_t activated) {
    int flags;
    flags = activated ? ALLEGRO_MENU_ITEM_CHECKED : 0;
    al_set_menu_item_flags(tape_main_menu, IDM_TAPE_RECORD, flags);
}

/* TOHv3 */
void gui_alter_tape_menus (uint8_t filetype_bits) {

    int flags;

    if (NULL == tape_save_menu) {
        log_warn("gui: alter tape submenu: tape_save_menu is NULL");
        return;
    }
    
    if (NULL == tape_main_menu) {
        log_warn("gui: alter tape submenu: tape_main_menu is NULL");
        return;
    }
    
    flags = (TAPE_FILETYPE_BITS_UEF & filetype_bits) ? 0 : ALLEGRO_MENU_ITEM_DISABLED;
    
    al_set_menu_item_flags(tape_save_menu, IDM_TAPE_SAVE_UEF, flags);
    al_set_menu_item_flags(tape_save_menu, IDM_TAPE_SAVE_UEF_UNCOMP, flags); /* TOHv3.2 */
    
    flags = (filetype_bits & TAPE_FILETYPE_BITS_TIBET) ? 0 : ALLEGRO_MENU_ITEM_DISABLED;
    al_set_menu_item_flags(tape_save_menu, IDM_TAPE_SAVE_TIBET, flags);
    al_set_menu_item_flags(tape_save_menu, IDM_TAPE_SAVE_TIBETZ, flags);
    
    flags = (filetype_bits & TAPE_FILETYPE_BITS_CSW) ? 0 : ALLEGRO_MENU_ITEM_DISABLED;
    al_set_menu_item_flags(tape_save_menu, IDM_TAPE_SAVE_CSW, flags);
    al_set_menu_item_flags(tape_save_menu, IDM_TAPE_SAVE_CSW_UNCOMP, flags); /* TOHv3.2 */
    
    gui_alter_tape_menus_2();

}

void gui_alter_tape_menus_2(void) {
    int flags;
    /* this one is for greying out Catalogue Tape in the main tape menu */
    flags = 0;
#ifdef BUILD_TAPE_MENU_GREYOUT_CAT
    if (!tape_peek_for_data(&tape_state)) {
        flags = ALLEGRO_MENU_ITEM_DISABLED;
    }
#endif
    al_set_menu_item_flags (tape_main_menu, IDM_TAPE_CAT, flags);
}

/* TOHv3 */
void gui_alter_tape_eject (char *path) {
    size_t z, len, pfxlen, sfxlen, sfx2len;
    char *s;
    const char *pfx, *sfx, *sfx2;
    if (NULL == path) {
        al_set_menu_item_caption (tape_main_menu,
                                  IDM_TAPE_EJECT,
                                  "Eject tape");
    } else {
        if (path[0]=='\0') { return; }
        for (z = strlen(path);
             (z > 0) && ((path[z-1] != '/') && (path[z-1] != '\\'));
             z--) ;
        /* decided to go with the colon rather than the brackets;
           brackets are slightly confusing given that they're also
           used for "Rewind (playback only)" */
        pfx = "Eject tape: ";
        sfx = "";
        sfx2 = "...";
        pfxlen = strlen(pfx);
        sfxlen = strlen(sfx);
        sfx2len = strlen(sfx2);
        len = strlen(path+z);
#define GUI_MENU_TAPEFILE_MAXLEN 48
        if (len > GUI_MENU_TAPEFILE_MAXLEN) {
            len = GUI_MENU_TAPEFILE_MAXLEN - 3;
            sfx = sfx2;
            sfxlen = sfx2len;
        }
        s = malloc(len + 1 + pfxlen + sfxlen);
        if (NULL == s) {
            log_warn("gui: alter tape eject menu entry: malloc failed for filename");
            return; 
        }
        memcpy (s,            pfx,    pfxlen);
        memcpy (s+pfxlen,     path+z, len);
        memcpy (s+pfxlen+len, sfx,    sfxlen+1);
        al_set_menu_item_caption (tape_main_menu, IDM_TAPE_EJECT, s);
        free(s);
    }
}

void gui_allegro_destroy(ALLEGRO_EVENT_QUEUE *queue, ALLEGRO_DISPLAY *display)
{
    al_unregister_event_source(queue, al_get_default_menu_event_source());
    al_set_display_menu(display, NULL);
}

static int radio_event_simple(ALLEGRO_EVENT *event, int current)
{
    int id = menu_get_id(event);
    int num = menu_get_num(event);
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    al_set_menu_item_flags(menu, menu_id_num(id, current), ALLEGRO_MENU_ITEM_CHECKBOX);
    return num;
}

static int radio_event_with_deselect(ALLEGRO_EVENT *event, int current)
{
    int id = menu_get_id(event);
    int num = menu_get_num(event);
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    if (num == current)
        num = -1;
    else
        al_set_menu_item_flags(menu, menu_id_num(id, current), ALLEGRO_MENU_ITEM_CHECKBOX);
    return num;
}

static void file_chooser_generic(ALLEGRO_EVENT *event, const char *initial_path, const char *title, const char *patterns, int flags, void (*callback)(const char *))
{
    ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(initial_path, title, patterns, flags);
    if (chooser) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0)
                callback(al_get_native_file_dialog_path(chooser, 0));
        }
        al_destroy_native_file_dialog(chooser);
    }
}

static void file_save_scrshot(const char *path)
{
    strncpy(vid_scrshotname, path, sizeof vid_scrshotname-1);
    vid_scrshotname[sizeof vid_scrshotname-1] = 0;
    vid_savescrshot = 2;
}

static void file_print_close_file(void)
{
    if (fclose(prt_fp))
        log_error("error closing print file: %s",  strerror(errno));
    prt_fp = NULL;
    print_dest = PDEST_NONE;
}

static void file_print_close_pipe(void)
{
    int ecode = pclose(prt_fp);
    if (ecode == -1)
        log_error("error waiting for print command: %s", strerror(errno));
    else if (ecode > 0)
        log_error("print command failed, exit status=%d", ecode);
    prt_fp = NULL;
    print_dest = PDEST_NONE;
}

static void file_print_open(ALLEGRO_EVENT *event)
{
    ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(savestate_name, "Print to file", "*.prn", ALLEGRO_FILECHOOSER_SAVE);
    if (chooser) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        while (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) <= 0)
                break;
            if ((prt_fp = fopen(al_get_native_file_dialog_path(chooser, 0), "wb")))
                break;
        }
        al_destroy_native_file_dialog(chooser);
    }
    if (prt_fp)
        print_dest = PDEST_FILE;
    else
        al_set_menu_item_flags((ALLEGRO_MENU *)(event->user.data3), IDM_FILE_PRINT, 0);
}

static void file_print_file(ALLEGRO_EVENT *event)
{
    switch(print_dest) {
        case PDEST_PIPE:
            file_print_close_pipe();
            al_set_menu_item_flags((ALLEGRO_MENU *)(event->user.data3), IDM_FILE_PCMD, 0);
            /* FALL THROUGH */
        case PDEST_NONE:
            file_print_open(event);
            break;
        case PDEST_FILE:
            file_print_close_file();
    }
}

static void file_print_open_pipe(ALLEGRO_EVENT *event)
{
    const char *pcmd = get_config_string(NULL, "printcmd", "lp");
    if ((prt_fp = popen(pcmd, "w")))
        print_dest = PDEST_PIPE;
    else {
        log_error("unable to start print command '%s': %s", pcmd, strerror(errno));
        al_set_menu_item_flags((ALLEGRO_MENU *)(event->user.data3), IDM_FILE_PCMD, 0);
    }
}

static void file_print_pipe(ALLEGRO_EVENT *event)
{
    switch(print_dest) {
        case PDEST_FILE:
            file_print_close_file();
            al_set_menu_item_flags((ALLEGRO_MENU *)(event->user.data3), IDM_FILE_PRINT, 0);
            /* FALL THROUGH */
        case PDEST_NONE:
            file_print_open_pipe(event);
            break;
        case PDEST_PIPE:
            file_print_close_pipe();
    }
}

static void serial_rec(ALLEGRO_EVENT *event)
{

    if (sysacia_fp)
        sysacia_rec_stop();
    else {
        ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(savestate_name, "Record serial to file", "*.txt", ALLEGRO_FILECHOOSER_SAVE);
        if (chooser) {
            ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
            while (al_show_native_file_dialog(display, chooser)) {
                if (al_get_native_file_dialog_count(chooser) <= 0)
                    break;
                if (sysacia_rec_start(al_get_native_file_dialog_path(chooser, 0)))
                    break;
            }
            al_destroy_native_file_dialog(chooser);
        }
    }
}

static void toggle_record(ALLEGRO_EVENT *event, sound_rec_t *rec)
{
    if (rec->fp)
        sound_stop_rec(rec);
    else {
        ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(savestate_name, rec->prompt, "*.wav", ALLEGRO_FILECHOOSER_SAVE);
        if (chooser) {
            ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
            while (al_show_native_file_dialog(display, chooser)) {
                if (al_get_native_file_dialog_count(chooser) <= 0)
                    break;
                if (sound_start_rec(rec, al_get_native_file_dialog_path(chooser, 0)))
                    break;
            }
            al_destroy_native_file_dialog(chooser);
        }
    }
}

static void edit_paste_start(ALLEGRO_EVENT *event, void (*paste_start)(char *str))
{
    ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
    char *text = al_get_clipboard_text(display);
#ifndef WIN32
    if (!text) {
        sleep(1);  // try again - Allegro bug.
        text = al_get_clipboard_text(display);
    }
#endif
    if (text)
        paste_start(text);
}

static void edit_print_clip(ALLEGRO_EVENT *event)
{
    if (prt_clip_str) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        al_set_clipboard_text(display, al_cstr(prt_clip_str));
        al_ustr_free(prt_clip_str);
        prt_clip_str = NULL;
    }
    else
        prt_clip_str = al_ustr_dup(al_ustr_empty_string());
}

void gui_set_disc_wprot(int drive, bool enabled)
{
    al_set_menu_item_flags(disc_menu, menu_id_num(IDM_DISC_WPROT, drive), enabled ? ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED : ALLEGRO_MENU_ITEM_CHECKBOX);
}

static void disc_choose_new(ALLEGRO_EVENT *event, const char *ext)
{
    int drive = menu_get_num(event);
    ALLEGRO_PATH *apath = discfns[drive];
    ALLEGRO_FILECHOOSER *chooser;
    const char *fpath;
    char name[20], title[70];
    snprintf(name, sizeof(name), "new%s", strchr(ext, '.'));
    if (apath) {
        apath = al_clone_path(apath);
        al_set_path_filename(apath, name);
        fpath = al_path_cstr(apath, ALLEGRO_NATIVE_PATH_SEP);
    }
    else
        fpath = name;
    snprintf(title, sizeof title, "Choose an image file name to create for drive %d/%d", drive, drive+2);
    if ((chooser = al_create_native_file_dialog(fpath, title, ext, ALLEGRO_FILECHOOSER_SAVE))) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0) {
                ALLEGRO_PATH *path = al_create_path(al_get_native_file_dialog_path(chooser, 0));
                disc_close(drive);
                if (discfns[drive])
                    al_destroy_path(discfns[drive]);
                discfns[drive] = path;
                switch(menu_get_id(event)) {
                    case IDM_DISC_NEW_ADFS_S:
                        sdf_new_disc(drive, path, &sdf_geometries.adfs_s);
                        break;
                    case IDM_DISC_NEW_ADFS_M:
                        sdf_new_disc(drive, path, &sdf_geometries.adfs_m);
                        break;
                    case IDM_DISC_NEW_ADFS_L:
                        sdf_new_disc(drive, path, &sdf_geometries.adfs_l);
                        break;
                    case IDM_DISC_NEW_DFS_10S_SIN_40T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_10s_sin_40t);
                        break;
                    case IDM_DISC_NEW_DFS_10S_INT_40T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_10s_int_40t);
                        break;
                    case IDM_DISC_NEW_DFS_10S_SIN_80T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_10s_sin_80t);
                        break;
                    case IDM_DISC_NEW_DFS_10S_INT_80T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_10s_int_80t);
                        break;
                    case IDM_DISC_NEW_DFS_16S_SIN_40T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_16s_sin_40t);
                        break;
                    case IDM_DISC_NEW_DFS_16S_SIN_80T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_16s_sin_80t);
                        break;
                    case IDM_DISC_NEW_DFS_16S_INT_80T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_16s_int_80t);
                        break;
                    case IDM_DISC_NEW_DFS_18S_SIN_40T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_18s_sin_40t);
                        break;
                    case IDM_DISC_NEW_DFS_18S_SIN_80T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_18s_sin_80t);
                        break;
                    case IDM_DISC_NEW_DFS_18S_INT_80T:
                        sdf_new_disc(drive, path, &sdf_geometries.dfs_18s_int_80t);
                        break;
                    default:
                        break;
                }
                gui_set_disc_wprot(drive, writeprot[drive]);
            }
        }
        al_destroy_native_file_dialog(chooser);
    }
    if (fpath != name)
        al_destroy_path(apath);
}

static void disc_choose(ALLEGRO_EVENT *event, const char *opname, const char *exts, int flags)
{
    int drive = menu_get_num(event);
    ALLEGRO_PATH *apath;
    const char *fpath;
    if (!(apath = discfns[drive]) || !(fpath = al_path_cstr(apath, ALLEGRO_NATIVE_PATH_SEP)))
        fpath = ".";
    char title[70];
    snprintf(title, sizeof title, "Choose a disc to %s drive %d/%d", opname, drive, drive+2);
    ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(fpath, title, exts, flags);
    if (chooser) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0) {
                ALLEGRO_PATH *path = al_create_path(al_get_native_file_dialog_path(chooser, 0));
                disc_close(drive);
                if (discfns[drive])
                    al_destroy_path(discfns[drive]);
                discfns[drive] = path;
                switch(menu_get_id(event)) {
                    case IDM_DISC_AUTOBOOT:
                        main_reset();
                        autoboot = 150;
                        /* FALLTHROUGH */
                    case IDM_DISC_LOAD:
                        if (!disc_load(drive, path)) {
                            if (defaultwriteprot)
                                writeprot[drive] = 1;
                        }
                        else {
                            al_destroy_path(path);
                            discfns[drive] = NULL;
                        }
                        break;
                    default:
                        break;
                }
                gui_set_disc_wprot(drive, writeprot[drive]);
            }
        }
        al_destroy_native_file_dialog(chooser);
    }
}

static void disc_eject(ALLEGRO_EVENT *event)
{
    int drive = menu_get_num(event);
    disc_close(drive);
    if (discfns[drive]) {
        al_destroy_path(discfns[drive]);
        discfns[drive] = NULL;
    }
    al_set_menu_item_caption(disc_menu, menu_id_num(IDM_DISC_EJECT, drive), drive ? "Eject disc :1/3" : "Eject disc :0/2");
}

static void disc_wprot(ALLEGRO_EVENT *event)
{
    int drive = menu_get_num(event);
    writeprot[drive] = !writeprot[drive];
}

static void disc_mmb_load(const char *path)
{
    char *fn = strdup(path);
    mmb_eject();
    mmb_load(fn);
}

static void disc_mmc_load(const char *path)
{
    char *fn = strdup(path);
    mmccard_eject();
    mmccard_load(fn);
}

static void disc_toggle_ide(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    if (ide_enable) {
        ide_close();
        ide_enable = false;
    }
    else {
        if (scsi_enabled) {
            scsi_close();
            scsi_enabled = false;
            al_set_menu_item_flags(menu, IDM_DISC_HARD_SCSI, ALLEGRO_MENU_ITEM_CHECKBOX);
        }
        ide_enable = true;
        ide_init();
    }
}

static void disc_toggle_scsi(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    if (scsi_enabled) {
        scsi_close();
        scsi_enabled = false;
    }
    else {
        if (ide_enable) {
            ide_close();
            ide_enable = false;
            al_set_menu_item_flags(menu, IDM_DISC_HARD_IDE, ALLEGRO_MENU_ITEM_CHECKBOX);
        }
        scsi_enabled = true;
        scsi_init();
    }
}

static void disc_vdfs_root(const char *path)
{
    vdfs_set_root(path);
    config_save();
}

static void tape_load_ui(ALLEGRO_EVENT *event)
{
    const char *fpath;
    if (!tape_vars.load_filename || !(fpath = al_path_cstr(tape_vars.load_filename, ALLEGRO_NATIVE_PATH_SEP)))
        fpath = ".";
    ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(fpath, "Choose a tape to load", "*.uef;*.csw;*.tibet;*.tibetz", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST);
    if (chooser) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0) {
                /* TOHv3 */
                tape_state_finish(&tape_state, 1); /* alter menus */
                ALLEGRO_PATH *path = al_create_path(al_get_native_file_dialog_path(chooser, 0));
                tape_load(path);
                tape_vars.load_filename = path;
            }
        }
    }
}

/* tape overhaul v3 */
static void tape_save (ALLEGRO_EVENT *event, const char *ext) {
    ALLEGRO_PATH *apath = tape_vars.save_filename; /* tape file chooser path */
    ALLEGRO_FILECHOOSER *chooser;
    const char *fpath;
    char name[20], title[70];

    snprintf(name, sizeof(name), "new%s", strchr(ext, '.')); /* e.g. name = "new.ssd" */
    if (NULL != apath) {
        apath = al_clone_path(apath);
        al_set_path_filename(apath, name);
        fpath = al_path_cstr(apath, ALLEGRO_NATIVE_PATH_SEP);
    } else {
        fpath = name;
    }
    snprintf(title, sizeof(title), "Choose a tape file name to create");
    if ((chooser = al_create_native_file_dialog(fpath, title, ext, ALLEGRO_FILECHOOSER_SAVE))) {
        ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
        if (al_show_native_file_dialog(display, chooser)) {
            if (al_get_native_file_dialog_count(chooser) > 0) {
                ALLEGRO_PATH *path = al_create_path(al_get_native_file_dialog_path(chooser, 0));
                /*disc_close(drive);*/
                /* ^^ we don't do this because tape doesn't work this way.
                      we only have one tape loaded, and we can save
                      repeated snapshots of it. We can load a completely
                      new tape, but that's using the "load tape" dialogue,
                      not the "save tape" one. */

                /* update tape_save_fn */
                if (NULL != tape_vars.save_filename) {
                    al_destroy_path(tape_vars.save_filename);
                }
                tape_vars.save_filename = path;

                switch(menu_get_id(event)) {
                    case IDM_TAPE_SAVE_UEF:
                    case IDM_TAPE_SAVE_UEF_UNCOMP: /* TOHv3.2 */
                        tape_generate_and_save_output_file (TAPE_FILETYPE_BITS_UEF,
                                                            tape_vars.save_prefer_112, /* TOHv3.2 */
                                                            (char *) al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP),
                                                            menu_get_id(event)==IDM_TAPE_SAVE_UEF, /* compress? */
                                                            &sysacia); /* might need resetting */
                        break;
                    case IDM_TAPE_SAVE_TIBET:
                        tape_generate_and_save_output_file (TAPE_FILETYPE_BITS_TIBET,
                                                            tape_vars.save_prefer_112, /* TOHv3.2 */
                                                            (char *) al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP),
                                                            0, /* no Z => no compression */
                                                            &sysacia);
                        break;
                    case IDM_TAPE_SAVE_TIBETZ:
                        tape_generate_and_save_output_file (TAPE_FILETYPE_BITS_TIBET,
                                                            tape_vars.save_prefer_112, /* TOHv3.2 */
                                                            (char *) al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP),
                                                            1, /* with Z => compress */
                                                            &sysacia);
                        break;
                    case IDM_TAPE_SAVE_CSW:
                    case IDM_TAPE_SAVE_CSW_UNCOMP: /* TOHv3.2 */
                        tape_generate_and_save_output_file (TAPE_FILETYPE_BITS_CSW,
                                                            tape_vars.save_prefer_112, /* TOHv3.2 */
                                                            (char *) al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP),
                                                            menu_get_id(event)==IDM_TAPE_SAVE_CSW, /* compress? */
                                                            &sysacia);
                        break;
                    default:
                        break;
                }
            }
        }
        al_destroy_native_file_dialog(chooser);
    }
    if (fpath != name)
        al_destroy_path(apath);
}

/* TOHv3 */
static ALLEGRO_MENU *create_tape_save_menu(void)
 {
    ALLEGRO_MENU *menu = al_create_menu();
    al_append_menu_item (menu, "UEF...",                IDM_TAPE_SAVE_UEF,        0, NULL, NULL);
    al_append_menu_item (menu, "UEF (uncompressed)...", IDM_TAPE_SAVE_UEF_UNCOMP, 0, NULL, NULL); /* TOHv3.2 */
    al_append_menu_item (menu, "TIBETZ...",             IDM_TAPE_SAVE_TIBETZ,     0, NULL, NULL);
    al_append_menu_item (menu, "TIBET...",              IDM_TAPE_SAVE_TIBET,      0, NULL, NULL);
    al_append_menu_item (menu, "CSW...",                IDM_TAPE_SAVE_CSW,        0, NULL, NULL);
    al_append_menu_item (menu, "CSW (uncompressed)...", IDM_TAPE_SAVE_CSW_UNCOMP, 0, NULL, NULL); /* TOHv3.2 */
    tape_save_menu = menu;
    return menu;
}
#ifdef BUILD_TAPE_DEV_MENU
static ALLEGRO_MENU *create_tape_dev_menu(void) {

    ALLEGRO_MENU *menu = al_create_menu();

#define TAPE_DEV_COMEDY_EMOTES ""
// text version
/*#define TAPE_DEV_COMEDY_EMOTES " >:-)"*/
// alt. unicode emoji version
/*#define TAPE_DEV_COMEDY_EMOTES " \xF0\x9F\x98\x88"*/

    al_append_menu_item(menu, "Corrupt next ACIA read" TAPE_DEV_COMEDY_EMOTES, IDM_TAPE_CORRUPT_READ, 0, NULL, NULL);
    al_append_menu_item(menu, "Mis-frame next ACIA read" TAPE_DEV_COMEDY_EMOTES, IDM_TAPE_MISFRAME_READ, 0, NULL, NULL);
    al_append_menu_item(menu, "Generate parity error on next ACIA read" TAPE_DEV_COMEDY_EMOTES, IDM_TAPE_GEN_PARITY_ERROR, 0, NULL, NULL);

    tape_dev_menu = menu;

    return menu;
}
#endif


static void tape_save_menu_nothing(ALLEGRO_EVENT *ev) {

}

static void tape_eject(void)
{
    tape_set_record_activated(&tape_state, &tape_vars, &sysacia, 0);
    gui_set_record_mode(0);
    tape_state_finish(&tape_state, 1); /* 1 = update menus */
}

/* TOHv3.2: repurposed */
static void tape_toggle_turbo_overclock(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    /* toggle */
    if (tape_vars.overclock) {
        tape_vars.overclock = false;
        al_set_menu_item_flags(menu, IDM_TAPE_TURBO_OVERCLOCK, ALLEGRO_MENU_ITEM_CHECKBOX);
    } else {
        tape_vars.overclock = true;
        al_set_menu_item_flags(menu, IDM_TAPE_TURBO_OVERCLOCK, ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED);
    }
    
    serial_recompute_dividers_and_thresholds (tape_vars.overclock,
                                              tape_state.ula_ctrl_reg,
                                              &(tape_state.ula_rx_thresh_ns),
                                              &(tape_state.ula_tx_thresh_ns),
                                              &(tape_state.ula_rx_divider),
                                              &(tape_state.ula_tx_divider));
    
}

static void tape_toggle_no_filter_phantoms (ALLEGRO_EVENT *event) {
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    /* toggle */
    if (tape_vars.disable_phantom_block_protection) {
        tape_vars.disable_phantom_block_protection = 0;
        al_set_menu_item_flags(menu,
                               IDM_TAPE_OPTS_LOAD_NO_FILTER_PHANTOMS,
                               ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED);
    } else {
        tape_vars.disable_phantom_block_protection = 1;
        al_set_menu_item_flags(menu,
                               IDM_TAPE_OPTS_LOAD_NO_FILTER_PHANTOMS,
                               ALLEGRO_MENU_ITEM_CHECKBOX);
    }
}

static void tape_toggle_force_112 (ALLEGRO_EVENT *event) {
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    /* toggle */
    if (tape_vars.save_prefer_112) {
        tape_vars.save_prefer_112 = 0;
        al_set_menu_item_flags(menu, IDM_TAPE_OPTS_SAVE_UEF_FORCE_112, ALLEGRO_MENU_ITEM_CHECKBOX);
    } else {
        tape_vars.save_prefer_112 = 1;
        al_set_menu_item_flags(menu,
                               IDM_TAPE_OPTS_SAVE_UEF_FORCE_112,
                               ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED);
    }
}

static void tape_toggle_force_117 (ALLEGRO_EVENT *event) {
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    /* toggle */
    if (tape_vars.save_always_117) {
        tape_vars.save_always_117 = 0;
        al_set_menu_item_flags(menu, IDM_TAPE_OPTS_SAVE_UEF_FORCE_117, ALLEGRO_MENU_ITEM_CHECKBOX);
    } else {
        tape_vars.save_always_117 = 1;
        al_set_menu_item_flags(menu,
                               IDM_TAPE_OPTS_SAVE_UEF_FORCE_117,
                               ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED);
    }
}

static void tape_toggle_append_origin (ALLEGRO_EVENT *event) {
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    /* toggle */
    if (tape_vars.save_do_not_generate_origin_on_append) {
        tape_vars.save_do_not_generate_origin_on_append = 0;
        al_set_menu_item_flags(menu,
                               IDM_TAPE_OPTS_SAVE_UEF_SUPPRESS_ORGN_ON_APPEND,
                               ALLEGRO_MENU_ITEM_CHECKBOX);
    } else {
        tape_vars.save_do_not_generate_origin_on_append = 1;
        al_set_menu_item_flags(menu,
                               IDM_TAPE_OPTS_SAVE_UEF_SUPPRESS_ORGN_ON_APPEND,
                               ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED);
    }
}

/* TOHv3.2: repurposed */
static void tape_toggle_turbo_skip(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);

    if ( tape_vars.strip_silence_and_leader ) {
        tape_vars.strip_silence_and_leader = 0;
        al_set_menu_item_flags(menu, IDM_TAPE_TURBO_SKIP, ALLEGRO_MENU_ITEM_CHECKBOX);
    } else {
        tape_vars.strip_silence_and_leader = 1;
        al_set_menu_item_flags(menu, IDM_TAPE_TURBO_SKIP, ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED);
    }
}

static void rom_load(ALLEGRO_EVENT *event)
{
    int slot = menu_get_num(event);
    rom_slot_t *slotp = rom_slots + slot;
    if (!slotp->locked) {
        char tempname[PATH_MAX];
        if (slotp->name)
            strncpy(tempname, slotp->name, sizeof tempname-1);
        else
            tempname[0] = 0;
        ALLEGRO_FILECHOOSER *chooser = al_create_native_file_dialog(tempname, "Choose a ROM to load", "*.rom", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST);
        if (chooser) {
            ALLEGRO_DISPLAY *display = (ALLEGRO_DISPLAY *)(event->user.data2);
            if (al_show_native_file_dialog(display, chooser)) {
                if (al_get_native_file_dialog_count(chooser) > 0) {
                    char label[ROM_LABEL_LEN];
                    ALLEGRO_PATH *path = al_create_path(al_get_native_file_dialog_path(chooser, 0));
                    mem_clearrom(slot);
                    mem_loadrom(slot, al_get_path_filename(path), al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP), 0);
                    al_destroy_path(path);
                    gen_rom_label(slot, label);
                    al_set_menu_item_caption(rom_menu, slot-ROM_NSLOT+1, label);
                }
            }
        }
    }
}

static void rom_clear(ALLEGRO_EVENT *event)
{
    int slot = menu_get_num(event);
    char label[ROM_LABEL_LEN];

    mem_clearrom(slot);
    gen_rom_label(slot, label);
    al_set_menu_item_caption(rom_menu, slot-ROM_NSLOT+1, label);
}

static void rom_ram_toggle(ALLEGRO_EVENT *event)
{
    int slot = menu_get_num(event);
    char label[ROM_LABEL_LEN];

    rom_slots[slot].swram = !rom_slots[slot].swram;
    gen_rom_label(slot, label);
    al_set_menu_item_caption(rom_menu, slot-ROM_NSLOT+1, label);
}

static void change_model(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);
    if (curmodel == menu_get_num(event)) {
        al_set_menu_item_flags(menu, menu_id_num(IDM_MODEL, curmodel), ALLEGRO_MENU_ITEM_CHECKBOX|ALLEGRO_MENU_ITEM_CHECKED);
        return;
    }
    al_set_menu_item_flags(menu, menu_id_num(IDM_MODEL, curmodel), ALLEGRO_MENU_ITEM_CHECKBOX);
    config_save();
    oldmodel = curmodel;
    curmodel = menu_get_num(event);
    main_restart();
    oldmodel = curmodel;
    update_rom_menu();
}

static void change_tube(ALLEGRO_EVENT *event)
{
    ALLEGRO_MENU *menu = (ALLEGRO_MENU *)(event->user.data3);
    int newtube = menu_get_num(event);
    log_debug("gui: change_tube newtube=%d", newtube);
    if (newtube == selecttube)
        selecttube = -1;
    else {
        al_set_menu_item_flags(menu, menu_id_num(IDM_TUBE, selecttube), ALLEGRO_MENU_ITEM_CHECKBOX);
        selecttube = newtube;
    }
    main_restart();
    update_rom_menu();
}

static void change_tube_speed(ALLEGRO_EVENT *event)
{
    tube_speed_num = radio_event_simple(event, tube_speed_num);
    tube_updatespeed();
}

static void set_sid_type(ALLEGRO_EVENT *event)
{
    cursid = radio_event_simple(event, cursid);
    sid_settype(sidmethod, cursid);
}

static void set_sid_method(ALLEGRO_EVENT *event)
{
    sidmethod = radio_event_simple(event, sidmethod);
    sid_settype(sidmethod, cursid);
}

static void change_ddnoise_dtype(ALLEGRO_EVENT *event)
{
    ddnoise_type = radio_event_simple(event, ddnoise_type);
    ddnoise_close();
    ddnoise_init();
}

static void change_mode7_font(ALLEGRO_EVENT *event)
{
    int newix = radio_event_simple(event, mode7_font_index);
    if (mode7_loadchars(mode7_font_files[newix]))
        mode7_font_index = newix;
}

static void toggle_music5000(void)
{
    if (sound_music5000) {
        sound_music5000 = false;
        music5000_close();
    }
    else {
        sound_music5000 = true;
        music5000_init(emuspeed);
    }
}    

static const char all_dext[] = "*.ssd;*.dsd;*.img;*.adf;*.ads;*.adm;*.adl;*.sdd;*.ddd;*.fdi;*.imd;*.hfe;"
                               "*.SSD;*.DSD;*.IMG;*.ADF;*.ADS;*.ADM;*.ADL;*.SDD;*.DDD;*.FDI;*.IMD;*.HFE";

void gui_allegro_event(ALLEGRO_EVENT *event)
{
    switch(menu_get_id(event)) {
        case IDM_ZERO:
            break;
        case IDM_FILE_RESET:
            nula_reset();
            main_restart();
            update_rom_menu();
            break;
        case IDM_FILE_LOAD_STATE:
            file_chooser_generic(event, savestate_name, "Load state from file", "*.snp", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST, savestate_load);
            break;
        case IDM_FILE_SAVE_STATE:
            file_chooser_generic(event, savestate_name, "Save state to file", "*.snp", ALLEGRO_FILECHOOSER_SAVE, savestate_save);
            break;
        case IDM_FILE_SCREEN_SHOT:
            file_chooser_generic(event, vid_scrshotname, "Save screenshot to file", "*.bmp;*.pcx;*.tga;*.png;*.jpg", ALLEGRO_FILECHOOSER_SAVE, file_save_scrshot);
            break;
        case IDM_FILE_SCREEN_TEXT:
            file_chooser_generic(event, savestate_name, "Save screen as text to file", "*.txt", ALLEGRO_FILECHOOSER_SAVE, textsave);
            break;
        case IDM_FILE_PRINT:
            file_print_file(event);
            break;
        case IDM_FILE_PCMD:
            file_print_pipe(event);
            break;
        case IDM_FILE_SERIAL:
            serial_rec(event);
            break;
        case IDM_FILE_M5000:
            toggle_record(event, &music5000_rec);
            break;
        case IDM_FILE_PAULAREC:
            toggle_record(event, &paula_rec);
            break;
        case IDM_FILE_SOUNDREC:
            toggle_record(event, &sound_rec);
            break;
        case IDM_FILE_EXIT:
            quitting = true;
            break;
        case IDM_EDIT_PASTE_OS:
            edit_paste_start(event, os_paste_start);
            break;
        case IDM_EDIT_PASTE_KB:
            edit_paste_start(event, key_paste_start);
            break;
        case IDM_EDIT_COPY:
            edit_print_clip(event);
            break;
        case IDM_DISC_AUTOBOOT:
            disc_choose(event, "autoboot in", all_dext, ALLEGRO_FILECHOOSER_FILE_MUST_EXIST);
            break;
        case IDM_DISC_LOAD:
            disc_choose(event, "load into", all_dext, ALLEGRO_FILECHOOSER_FILE_MUST_EXIST);
            break;
        case IDM_DISC_MMB_LOAD:
            file_chooser_generic(event, mmb_fn ? mmb_fn : ".", "Choose an MMB file", "*.mmb", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST, disc_mmb_load);
            break;
        case IDM_DISC_EJECT:
            disc_eject(event);
            break;
        case IDM_DISC_MMB_EJECT:
            mmb_eject();
            break;
        case IDM_DISC_MMC_LOAD:
            file_chooser_generic(event, mmccard_fn ? mmccard_fn : ".", "Choose an MMC card image", "*", ALLEGRO_FILECHOOSER_FILE_MUST_EXIST, disc_mmc_load);
            break;
        case IDM_DISC_MMC_EJECT:
            mmccard_eject();
            break;
        case IDM_DISC_NEW_ADFS_S:
            disc_choose_new(event, "*.ads");
            break;
        case IDM_DISC_NEW_ADFS_M:
            disc_choose_new(event, "*.adm");
            break;
        case IDM_DISC_NEW_ADFS_L:
            disc_choose_new(event, "*.adl");
            break;
        case IDM_DISC_NEW_DFS_10S_SIN_40T:
        case IDM_DISC_NEW_DFS_10S_SIN_80T:
            disc_choose_new(event, "*.ssd");
            break;
        case IDM_DISC_NEW_DFS_10S_INT_40T:
        case IDM_DISC_NEW_DFS_10S_INT_80T:
            disc_choose_new(event, "*.dsd");
            break;
        case IDM_DISC_NEW_DFS_16S_SIN_40T:
        case IDM_DISC_NEW_DFS_16S_SIN_80T:
        case IDM_DISC_NEW_DFS_18S_SIN_40T:
        case IDM_DISC_NEW_DFS_18S_SIN_80T:
            disc_choose_new(event, "*.sdd");
            break;
        case IDM_DISC_NEW_DFS_16S_INT_80T:
        case IDM_DISC_NEW_DFS_18S_INT_80T:
            disc_choose_new(event, "*.ddd");
            break;
        case IDM_DISC_WPROT:
            disc_wprot(event);
            break;
        case IDM_DISC_WPROT_D:
            defaultwriteprot = !defaultwriteprot;
            break;
        case IDM_DISC_HARD_IDE:
            disc_toggle_ide(event);
            break;
        case IDM_DISC_HARD_SCSI:
            disc_toggle_scsi(event);
            break;
        case IDM_DISC_VDFS_ENABLE:
            vdfs_enabled = !vdfs_enabled;
            break;
        case IDM_DISC_VDFS_ROOT:
            file_chooser_generic(event, vdfs_get_root(), "Choose a folder to be the VDFS root", "*", ALLEGRO_FILECHOOSER_FOLDER, disc_vdfs_root);
            break;
        case IDM_TAPE_LOAD:
            tape_load_ui(event);
            break;
        /* TOHv3 */
        case IDM_TAPE_SAVE:
            tape_save_menu_nothing(event);
            break;
        case IDM_TAPE_SAVE_UEF:
        case IDM_TAPE_SAVE_UEF_UNCOMP: /* TOHv3.2 */
            tape_save(event, "*.uef");
            break;
        case IDM_TAPE_SAVE_TIBET:
            tape_save(event, "*.tibet");
            break;
        case IDM_TAPE_SAVE_TIBETZ:
            tape_save(event, "*.tibetz");
            break;
        case IDM_TAPE_SAVE_CSW:
        case IDM_TAPE_SAVE_CSW_UNCOMP: /* TOHv3.2 */
            tape_save(event, "*.csw");
            break;
        case IDM_TAPE_RECORD:
            /* toggle */
            tape_set_record_activated (&tape_state,
                                       &tape_vars,
                                       &sysacia,
                                       ! tape_is_record_activated (&tape_vars));
            break;
#ifdef BUILD_TAPE_DEV_MENU
        case IDM_TAPE_DEV:
            tape_save_menu_nothing(event);
            break;
        case IDM_TAPE_CORRUPT_READ:
            sysacia.corrupt_next_read = 1;
            break;
        case IDM_TAPE_MISFRAME_READ:
            /* can use Atic Atac's third-stage loader (after the "red rain") to test this one ... */
            sysacia.misframe_next_read = 1;
            break;
        case IDM_TAPE_GEN_PARITY_ERROR:
            /* can use Atic Atac's third-stage loader (after the "red rain") to test this one? */
            sysacia.gen_parity_error_next_read = 1;
            break;
#endif
        case IDM_TAPE_REWIND:
            tape_rewind(); /* now lives on tape.c */
            break;
        case IDM_TAPE_EJECT:
            tape_eject();
            break;
        case IDM_TAPE_TURBO_OVERCLOCK:
            tape_toggle_turbo_overclock(event);
            break;
        case IDM_TAPE_TURBO_SKIP:
            tape_toggle_turbo_skip(event);
            break;
        case IDM_TAPE_CAT:
            gui_tapecat_start();
            break;
        case IDM_TAPE_OPTS_SAVE_UEF_FORCE_117:
            tape_toggle_force_117(event);
            break;
        case IDM_TAPE_OPTS_SAVE_UEF_FORCE_112:
            tape_toggle_force_112(event);
            break;
        case IDM_TAPE_OPTS_SAVE_UEF_SUPPRESS_ORGN_ON_APPEND:
            tape_toggle_append_origin(event);
            break;
        case IDM_TAPE_OPTS_LOAD_NO_FILTER_PHANTOMS:
            tape_toggle_no_filter_phantoms(event);
            break;
        case IDM_ROMS_LOAD:
            rom_load(event);
            break;
        case IDM_ROMS_CLEAR:
            rom_clear(event);
            break;
        case IDM_ROMS_RAM:
            rom_ram_toggle(event);
            break;
        case IDM_MODEL:
            change_model(event);
            break;
        case IDM_TUBE:
            change_tube(event);
            break;
        case IDM_TUBE_SPEED:
            change_tube_speed(event);
            break;
        case IDM_VIDEO_DISPTYPE:
            video_set_disptype(radio_event_simple(event, vid_dtype_user));
            break;
        case IDM_VIDEO_COLTYPE:
            vid_colour_out = radio_event_simple(event, vid_colour_out);
            break;
        case IDM_VIDEO_BORDERS:
            video_set_borders(radio_event_simple(event, vid_fullborders));
            break;
        case IDM_VIDEO_WIN_MULT:
            video_set_multipier(radio_event_simple(event, vid_win_multiplier));
            break;
        case IDM_VIDEO_WINSIZE:
            video_set_borders(vid_fullborders);
            break;
        case IDM_VIDEO_FULLSCR:
            toggle_fullscreen();
            break;
        case IDM_VIDEO_NULA:
            nula_disable = !nula_disable;
            break;
        case IDM_VIDEO_LED_LOCATION:
            video_set_led_location(radio_event_simple(event, vid_ledlocation));
            break;
        case IDM_VIDEO_LED_VISIBILITY:
            video_set_led_visibility(radio_event_simple(event, vid_ledvisibility));
            break;
        case IDM_VIDEO_MODE7_FONT:
            change_mode7_font(event);
            break;
        case IDM_SOUND_INTERNAL:
            sound_internal = !sound_internal;
            break;
        case IDM_SOUND_BEEBSID:
            sound_beebsid = !sound_beebsid;
            break;
        case IDM_SOUND_MUSIC5000:
            toggle_music5000();
            break;
        case IDM_SOUND_MFILT:
            music5000_fno = radio_event_with_deselect(event, music5000_fno);
            break;
        case IDM_SOUND_PAULA:
            sound_paula = !sound_paula;
            break;
        case IDM_SOUND_DAC:
            sound_dac = !sound_dac;
            break;
        case IDM_SOUND_DDNOISE:
            sound_ddnoise = !sound_ddnoise;
            break;
        case IDM_SOUND_TAPE:
            sound_tape = !sound_tape;
            tapenoise_activated_hook();
            break;
        /* TOHv2: */
        case IDM_SOUND_TAPE_RELAY:
            sound_tape_relay = !sound_tape_relay;
            break;
        case IDM_SOUND_FILTER:
            sound_filter = !sound_filter;
            break;
        case IDM_WAVE:
            curwave = radio_event_simple(event, curwave);
            break;
        case IDM_SID_TYPE:
            set_sid_type(event);
            break;
        case IDM_SID_METHOD:
            set_sid_method(event);
            break;
        case IDM_DISC_TYPE:
            change_ddnoise_dtype(event);
            break;
        case IDM_DISC_VOL:
            ddnoise_vol = radio_event_simple(event, ddnoise_vol);
            break;
#ifdef HAVE_JACK_JACK_H
        case IDM_MIDI_M4000_JACK:
            midi_music4000.jack_enabled = !midi_music4000.jack_enabled;
            break;
        case IDM_MIDI_M2000_OUT1_JACK:
            midi_music2000_out1.jack_enabled = !midi_music2000_out1.jack_enabled;
            break;
        case IDM_MIDI_M2000_OUT2_JACK:
            midi_music2000_out2.jack_enabled = !midi_music2000_out2.jack_enabled;
            break;
        case IDM_MIDI_M2000_OUT3_JACK:
            midi_music2000_out3.jack_enabled = !midi_music2000_out3.jack_enabled;
            break;
#endif
#ifdef HAVE_ALSA_ASOUNDLIB_H
        case IDM_MIDI_M4000_ASEQ:
            midi_music4000.alsa_seq_enabled = !midi_music4000.alsa_seq_enabled;
            break;
        case IDM_MIDI_M4000_ARAW:
            midi_music4000.alsa_raw_enabled = !midi_music4000.alsa_raw_enabled;
            break;
        case IDM_MIDI_M2000_OUT1_ASEQ:
            midi_music2000_out1.alsa_seq_enabled = !midi_music2000_out1.alsa_seq_enabled;
            break;
        case IDM_MIDI_M2000_OUT1_ARAW:
            midi_music2000_out1.alsa_raw_enabled = !midi_music2000_out1.alsa_raw_enabled;
            break;
        case IDM_MIDI_M2000_OUT2_ASEQ:
            midi_music2000_out2.alsa_seq_enabled = !midi_music2000_out2.alsa_seq_enabled;
            break;
        case IDM_MIDI_M2000_OUT2_ARAW:
            midi_music2000_out2.alsa_raw_enabled = !midi_music2000_out2.alsa_raw_enabled;
            break;
        case IDM_MIDI_M2000_OUT3_ASEQ:
            midi_music2000_out3.alsa_seq_enabled = !midi_music2000_out3.alsa_seq_enabled;
            break;
        case IDM_MIDI_M2000_OUT3_ARAW:
            midi_music2000_out3.alsa_raw_enabled = !midi_music2000_out3.alsa_raw_enabled;
            break;
#endif
        case IDM_SPEED:
            main_setspeed(radio_event_simple(event, emuspeed));
            break;
        case IDM_AUTOSKIP:
            autoskip = !autoskip;
            break;
        case IDM_DEBUGGER:
            debug_toggle_core( ! tape_vars.disable_debug_memview ); /* TOHv4: spawn_memview */
            break;
        case IDM_DEBUG_TUBE:
            debug_toggle_tube();
            break;
        case IDM_DEBUG_BREAK:
            debug_step = 1;
            break;
        case IDM_KEY_REDEFINE:
            gui_keydefine_open();
            break;
        case IDM_KEY_AS:
            keyas = !keyas;
            break;
        case IDM_KEY_MODE:
            key_mode = radio_event_simple(event, key_mode);
            key_reset();
            break;
        case IDM_KEY_PAD:
            keypad = !keypad;
            break;
        case IDM_JIM_SIZE:
            mem_jim_setsize(radio_event_simple(event, mem_jim_size));
            break;
        case IDM_AUTO_PAUSE:
            autopause = !autopause;
            break;
        case IDM_MOUSE_AMX:
            mouse_amx = !mouse_amx;
            break;
        case IDM_TRIACK_SEGA_ADAPTER:
            tricky_sega_adapter = !tricky_sega_adapter;
            remap_joystick(0);
            remap_joystick(1);
            break;
        case IDM_JOYSTICK:
            change_joystick(0, radio_event_with_deselect(event, joystick_index[0]));
            break;
        case IDM_JOYSTICK2:
            change_joystick(1, radio_event_with_deselect(event, joystick_index[1]));
        case IDM_MOUSE_STICK:
            mouse_stick = !mouse_stick;
            break;
        case IDM_JOYMAP:
            joymap_index[0] = radio_event_simple(event, joymap_index[0]);
            remap_joystick(0);
        case IDM_JOYMAP2:
            joymap_index[1] = radio_event_simple(event, joymap_index[1]);
            remap_joystick(1);
            break;
    }
}
