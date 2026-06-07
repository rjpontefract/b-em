/*B-em v2.2 by Tom Walker
  Allegro video code*/
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_native_dialog.h>
#include "b-em.h"
#include "led.h"
#include "main.h"
#include "pal.h"
#include "serial.h"
#include "tape.h"
#include "video.h"
#include "video_render.h"

enum vid_disptype vid_dtype_user = VDT_UNSET, vid_dtype_intern = VDT_UNSET;
enum vid_coltype vid_colour_out;
ALLEGRO_COLOR mono_green_col, mono_amber_col, mono_white_col;
int vid_fskipmax = 1;
int vid_fullborders = 1;
int vid_ledlocation = LED_LOC_NONE;
int vid_ledvisibility = LED_VIS_ALWAYS;
int vid_lock_type;

static int fskipcount;

#ifdef __APPLE__
/* Set to true by the macOS live-resize observer (vid_macos.m) while the user
 * is dragging the resize handle. Prevents Thread 7 from issuing any OpenGL
 * draw calls while the macOS main thread is changing the Metal drawable size,
 * which would otherwise race and crash inside GLDTextureRec::getTextureResource. */
volatile bool vid_live_resizing = false;
void vid_macos_resize_init(void);
#endif

int vid_savescrshot = 0;
char vid_scrshotname[260];

int winsizex, winsizey, vid_win_multiplier;
int save_winsizex, save_winsizey;
int scr_x_start, scr_x_size, scr_y_start, scr_y_size;

bool vid_print_mode = false;

#ifdef WIN32
static const int y_fudge = 0;
#else
static const int y_fudge = 28;
#endif

static void video_calc_fullscreen(ALLEGRO_DISPLAY *display)
{
    ALLEGRO_COLOR black = al_map_rgb(0, 0, 0);
    double aspect = (double)winsizex / (double)winsizey;
    log_debug("vidalleg: video_enterfullscreen, new winsizex=%d, winsizey=%d, aspect=%g", winsizex, winsizey, aspect);
    if (aspect > (4.0 / 3.0)) {
        int value = 4 * winsizey / 3;
        scr_x_start = (winsizex - value) / 2;
        scr_y_start = 0;
        scr_x_size = value;
        scr_y_size = winsizey;
        al_set_target_backbuffer(display);
        // fill the gap between the left screen edge and the BBC image.
        al_draw_filled_rectangle(0, 0, scr_x_start, scr_y_size, black);
        // fill the gap between the BBC image and the right screen edge.
        al_draw_filled_rectangle(scr_x_start + value, 0, winsizex, winsizey, black);
    }
    else {
        int value = 3 * winsizex / 4;
        scr_x_start = 0;
        scr_y_start = (winsizey - value) / 2;
        scr_x_size = winsizex;
        scr_y_size = value;
        // fill the gap between the top of the screen and the BBC image.
        al_draw_filled_rectangle(0, 0, scr_x_size, scr_y_start, black);
        // fill the gap between the BBC image and the bottom of the screen.
        al_draw_filled_rectangle(0, scr_y_start + value, winsizex, winsizey, black);
    }
    log_debug("vidalleg: video_enterfullscreen, scr_x_start=%d, scr_y_start=%d, scr_x_size=%d, scr_y_size=%d", scr_x_start, scr_y_start, scr_x_size, scr_y_size);
}

static int fullscreen_pending = 0;

static void video_startfullscreen(ALLEGRO_DISPLAY *display)
{
    winsizex = al_get_display_width(display);
    winsizey = al_get_display_height(display);
    video_calc_fullscreen(display);
    if (winsizex == save_winsizex || winsizey == save_winsizey) {
        log_debug("vidalleg: video_enterfullscreen, no immediate change of size, setting fullscreen_pending");
        fullscreen_pending = 500;
    }
}

void video_enterfullscreen()
{
    ALLEGRO_DISPLAY *display = al_get_current_display();
    save_winsizex = al_get_display_width(display);
    save_winsizey = al_get_display_height(display);
    log_debug("vidalleg: video_enterfullscreen, save_winsizex=%d, save_winsizey=%d", save_winsizex, save_winsizey);
    if (al_set_display_flag(display, ALLEGRO_FULLSCREEN_WINDOW, true)) {
#ifdef WIN32
        al_set_display_flag(display, ALLEGRO_FULLSCREEN_WINDOW, false);
        al_set_display_flag(display, ALLEGRO_FULLSCREEN_WINDOW, true);
#endif
        video_startfullscreen(display);
    }
    else if (al_set_display_flag(display, ALLEGRO_FRAMELESS, true)) {
        if (al_set_display_flag(display, ALLEGRO_MAXIMIZED, true)) {
            video_startfullscreen(display);
            fullscreen = 2;
        }
        else {
            log_error("vidalleg: could not set graphics mode to full-screen");
            fullscreen = 0;
        }
    }
    else {
        log_error("vidalleg: could not set graphics mode to full-screen");
        fullscreen = 0;
    }
}

static int video_led_height(void);

void video_set_window_size(bool fudge)
{
    int x_wanted, y_wanted;
    scr_x_start = 0;
    scr_y_start = 0;

    switch(vid_fullborders) {
        case 0:
            x_wanted = BORDER_NONE_X_END_GRA - BORDER_NONE_X_START_GRA;
            y_wanted = (BORDER_NONE_Y_END_GRA - BORDER_NONE_Y_START_GRA) * 2;
            break;
        default:
            x_wanted = BORDER_MED_X_END_GRA - BORDER_MED_X_START_GRA;
            y_wanted = (BORDER_MED_Y_END_GRA - BORDER_MED_Y_START_GRA) * 2;
            break;
        case 2:
            x_wanted = BORDER_FULL_X_END_GRA - BORDER_FULL_X_START_GRA;
            y_wanted = (BORDER_FULL_Y_END_GRA - BORDER_FULL_Y_START_GRA) * 2;
    }
    if (vid_win_multiplier > 0) {
        float dscale = vid_display_scale();
        winsizex = scr_x_size = (int)(x_wanted * vid_win_multiplier * dscale + 0.5f);
        winsizey = scr_y_size = (int)(y_wanted * vid_win_multiplier * dscale + 0.5f);
        if (fudge)
            winsizey += y_fudge;
        winsizey += video_led_height();
    }
    else {
        if (fudge)
            y_wanted += y_fudge;
        if ((scr_x_size = winsizex) < x_wanted)
            scr_x_size = winsizex = x_wanted;
        y_wanted += video_led_height();
        if ((scr_y_size = winsizey) < y_wanted)
            scr_y_size = winsizey = y_wanted;
    }
    log_debug("vidalleg: video_set_window_size, scr_x_size=%d, scr_y_size=%d, winsizex=%d, winsizey=%d", scr_x_size, scr_y_size, winsizex, winsizey);
}

void video_set_borders(int borders)
{
    vid_fullborders = borders;
    video_set_window_size(false);
    al_resize_display(al_get_current_display(), winsizex, winsizey);
}

void video_set_multipier(int multipler)
{
    vid_win_multiplier = multipler;
    video_set_window_size(false);
    al_resize_display(al_get_current_display(), winsizex, winsizey);
}

void video_set_led_location(int location)
{
    vid_ledlocation = location;
    video_set_window_size(false);
    al_resize_display(al_get_current_display(), winsizex, winsizey);
}

void video_set_led_visibility(int visibility)
{
    if (visibility == LED_VIS_TRANSIENT && vid_ledvisibility == LED_VIS_ALWAYS)
        last_led_update_at = framesrun;

    vid_ledvisibility = visibility;
}

#ifdef ALLEGRO_GTK_TOPLEVEL
/* GDK is loaded into the process via allegro_dialog's GTK support.  Use
 * dlsym to resolve the symbols at runtime so we don't need to add an
 * explicit link-time dependency on libgdk-3.
 *
 * Two separate GDK mechanisms cover the common desktop environments:
 *   gdk_screen_get_resolution() — returns the Xft DPI, which KDE (and other
 *     DEs using XSettings) sets to 96*scale for fractional scaling (e.g. 120
 *     for 125%).  Returns -1 if unset (treat as 96).
 *   gdk_monitor_get_scale_factor() — returns an integer pixel ratio (1, 2…)
 *     used by GNOME for its 2× HiDPI mode.  KDE leaves this at 1.
 * The total scale is the product of both. */
#include <dlfcn.h>

static float vid_linux_display_scale(void)
{
    typedef void *GdkScreen;
    typedef void *GdkDisplay;
    typedef void *GdkMonitor;
    typedef GdkScreen  *(*fn_screen_t)(void);
    typedef double      (*fn_res_t)(GdkScreen *);
    typedef GdkDisplay *(*fn_display_t)(void);
    typedef GdkMonitor *(*fn_primary_t)(GdkDisplay *);
    typedef GdkMonitor *(*fn_monitor_t)(GdkDisplay *, int);
    typedef int         (*fn_scale_t)(GdkMonitor *);

    fn_screen_t  get_screen  = (fn_screen_t) dlsym(RTLD_DEFAULT, "gdk_screen_get_default");
    fn_res_t     get_res     = (fn_res_t)    dlsym(RTLD_DEFAULT, "gdk_screen_get_resolution");
    fn_display_t get_display = (fn_display_t)dlsym(RTLD_DEFAULT, "gdk_display_get_default");
    fn_primary_t get_primary = (fn_primary_t)dlsym(RTLD_DEFAULT, "gdk_display_get_primary_monitor");
    fn_monitor_t get_monitor = (fn_monitor_t)dlsym(RTLD_DEFAULT, "gdk_display_get_monitor");
    fn_scale_t   get_scale   = (fn_scale_t)  dlsym(RTLD_DEFAULT, "gdk_monitor_get_scale_factor");

    /* Fractional DPI scale from XSettings/Xft (covers KDE fractional scaling). */
    float dpi_scale = 1.0f;
    if (get_screen && get_res) {
        GdkScreen *screen = get_screen();
        if (screen) {
            double dpi = get_res(screen);
            if (dpi > 0.0)
                dpi_scale = (float)(dpi / 96.0);
        }
    }

    /* Integer monitor pixel ratio (covers GNOME 2× HiDPI). */
    int mon_scale = 1;
    if (get_display && get_scale) {
        GdkDisplay *display = get_display();
        if (display) {
            GdkMonitor *mon = NULL;
            if (get_primary) mon = get_primary(display);
            if (!mon && get_monitor) mon = get_monitor(display, 0);
            if (mon) mon_scale = get_scale(mon);
        }
    }

    return dpi_scale * (float)mon_scale;
}
#endif

#ifdef WIN32
#include <allegro5/allegro_windows.h>

static float vid_windows_display_scale(void)
{
    ALLEGRO_DISPLAY *display;
    HWND hwnd = NULL;
    HMODULE user32;
    UINT dpi = 0;

    /* During initial sizing video_set_window_size() is called from video_init() 
     * before al_create_display() so there is no display yet.
     * Fall through to the GetDeviceCaps() path below with a NULL HWND,
     * which queries the primary monitor via the screen DC. 
     */
    if ((display = al_get_current_display()))
        hwnd = al_get_win_window_handle(display);

    if (hwnd && (user32 = GetModuleHandleA("user32.dll"))) {
        typedef UINT (WINAPI *fn_dpi_t)(HWND);
        fn_dpi_t get_dpi_for_window = (fn_dpi_t)GetProcAddress(user32, "GetDpiForWindow");
        if (get_dpi_for_window)
            dpi = get_dpi_for_window(hwnd);
    }
    if (!dpi) {
        HDC hdc = GetDC(hwnd);
        if (hdc) {
            dpi = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(hwnd, hdc);
        }
    }
    return dpi > 0 ? (float)dpi / 96.0f : 1.0f;
}
#endif

float vid_display_scale(void)
{
#ifdef __APPLE__
    return vid_macos_display_scale();
#elif defined(ALLEGRO_GTK_TOPLEVEL)
    return vid_linux_display_scale();
#elif defined(WIN32)
    return vid_windows_display_scale();
#else
    return 1.0f;
#endif
}

static int video_led_height(void)
{
    if (vid_ledlocation != LED_LOC_SEPARATE)
        return 0;
    return (int)(LED_BOX_HEIGHT * vid_display_scale() + 0.5f);
}

void video_update_window_size(ALLEGRO_EVENT *event)
{
    if (!fullscreen) {
        scr_x_start = 0;
        scr_x_size = winsizex = event->display.width;
        scr_y_start = 0;
        winsizey = event->display.height;
        scr_y_size = winsizey - video_led_height();
        log_debug("vidalleg: video_update_window_size, scr_x_size=%d, scr_y_size=%d", scr_x_size, scr_y_size);
    }
    al_acknowledge_resize(event->display.source);
}

void video_leavefullscreen(void)
{
    ALLEGRO_DISPLAY *display;

    display = al_get_current_display();
    //try and restore size to pre fullscreen size
    al_resize_display(display, save_winsizex, save_winsizey);

    if (fullscreen == 2) {
        log_debug("vidalleg: video_leavefullscreen, cancelling ALLEGRO_FRAMELESS");
        al_set_display_flag(display, ALLEGRO_MAXIMIZED, false);
        al_set_display_flag(display, ALLEGRO_FRAMELESS, false);
    }
    else if (fullscreen == 1) {
        log_debug("vidalleg: video_leavefullscreen, cancelling ALLEGRO_FULLSCREEN_WINDOW");
        al_set_display_flag(display, ALLEGRO_FULLSCREEN_WINDOW, false);
    }
    else
        log_debug("vidalleg: video_leavefullscreen, called with fullscreen=%d", fullscreen);

    scr_x_start = 0;
    scr_x_size = winsizex = al_get_display_width(display);
    scr_y_start = 0;
    winsizey = al_get_display_height(display);
    scr_y_size = winsizey - video_led_height();
}

static void upscale_only(ALLEGRO_BITMAP *src, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh)
{
    al_set_target_backbuffer(al_get_current_display());
    if (dw > sw+10 || dh > sh+10)
        al_draw_scaled_bitmap(src, sx, sy, sw, sh, dx, dy, dw, dh, 0);
    else {
        al_draw_bitmap_region(src, sx, sy, sw, sh, dx, dy, 0);
        if (dw > sw)
            al_draw_filled_rectangle(dx + sw, 0, dx + dw, dh, border_col);
        if (dh > sh)
            al_draw_filled_rectangle(0, dy + sh, dw, dy + dh, border_col);
    }
}

static void line_double(void)
{
    char *yptr1 = (char *)region->data + region->pitch * firsty * 2;
    char *yptr2 = yptr1 + region->pitch;
    size_t linesize = abs(region->pitch);

    for (int y = firsty; y < lasty; y++) {
        memcpy(yptr2, yptr1, linesize);
        yptr1 = yptr2 + region->pitch;
        yptr2 = yptr1 + region->pitch;
    }
}

static void mono_convert(int x1, int y1, int x2, int y2, ALLEGRO_COLOR mono_col)
{
    float mono_r, mono_g, mono_b;
    al_unmap_rgb_f(mono_col, &mono_r, &mono_g, &mono_b);
    log_debug("mono_convert: mono_r=%g, mono_g=%g, mono_b=%g", mono_r, mono_g, mono_b);
    ALLEGRO_LOCKED_REGION *dest_region = al_lock_bitmap(b32, ALLEGRO_PIXEL_FORMAT_ARGB_8888, vid_lock_type);
    for (int y = y1; y < y2; ++y) {
        char *src_row = (char *)region->data + region->pitch * y;
        char *dest_row = (char *)dest_region->data + dest_region->pitch * y;
        for (int x = x1; x < x2; ++x) {
            uint32_t *src_addr = (uint32_t *)(src_row + x * region->pixel_size);
            uint32_t pixel = *src_addr;
            double pix_r = (double)((pixel >> 16) & 0xff);
            double pix_g = (double)((pixel >> 8) & 0xff);
            double pix_b = (double)(pixel & 0xff);
            double pix_l = 780.37344691265060115 * (pix_r/(470+2200) + pix_g/(470+1000) + pix_b/(470+3900));
            uint32_t new_r = (uint32_t)(mono_r * pix_l);
            uint32_t new_g = (uint32_t)(mono_g * pix_l);
            uint32_t new_b = (uint32_t)(mono_b * pix_l);
            uint32_t *dest_addr = (uint32_t *)(dest_row + + x * dest_region->pixel_size);
            *dest_addr = (pixel & 0xff000000) | (new_r << 16) | (new_g << 8) | new_b;
        }
    }
    al_unlock_bitmap(b32);
}

static inline void save_screenshot(void)
{
    if (!--vid_savescrshot) {
        int xsize = lastx - firstx;
        int ysize = lasty - firsty;
        ALLEGRO_BITMAP *scrshotb  = al_create_bitmap(xsize, ysize << 1);
        ALLEGRO_COLOR mono_col;

        switch(vid_colour_out) {
            case VDC_RGB:
                al_set_target_bitmap(scrshotb);
                switch(vid_dtype_intern) {
                    case VDT_SCALE:
                        al_unlock_bitmap(b);
                        al_draw_scaled_bitmap(b, firstx, firsty, xsize, ysize, 0, 0, xsize, ysize << 1, 0);
                        break;
                    case VDT_INTERLACE:
                        al_unlock_bitmap(b);
                        al_draw_bitmap_region(b, firstx, firsty << 1, xsize, ysize << 1, 0, 0, 0);
                        break;
                    case VDT_SCANLINES:
                        al_unlock_bitmap(b);
                        for (int c = 0, y = firsty; y < lasty; y++, c += 2)
                            al_draw_bitmap_region(b, firstx, y, xsize, 1, 0, c, 0);
                        break;
                    case VDT_LINEDOUBLE:
                        line_double();
                        al_unlock_bitmap(b);
                        al_draw_scaled_bitmap(b, firstx, firsty << 1, xsize, ysize << 1, 0, 0, xsize, ysize << 1, 0);
                        break;
                    case VDT_UNSET:
                        break;
                }
                region = al_lock_bitmap(b, ALLEGRO_PIXEL_FORMAT_ARGB_8888, vid_lock_type);
                break;
            case VDC_PAL:
                switch(vid_dtype_intern) {
                    case VDT_SCALE:
                        pal_convert(firstx, firsty, lastx, lasty, 1);
                        al_set_target_bitmap(scrshotb);
                        al_draw_scaled_bitmap(b32, firstx, firsty, xsize, ysize, 0, 0, xsize, ysize << 1, 0);
                        break;
                    case VDT_INTERLACE:
                        pal_convert(firstx, firsty << 1, lastx, lasty << 1, 1);
                        al_set_target_bitmap(scrshotb);
                        al_draw_bitmap_region(b32, firstx, firsty << 1, xsize, ysize << 1, 0, 0, 0);
                        break;
                    case VDT_SCANLINES:
                        pal_convert(firstx, firsty, lastx, lasty, 1);
                        al_set_target_bitmap(scrshotb);
                        for (int c = 0, y = firsty; y < lasty; y++, c += 2)
                            al_draw_bitmap_region(b32, firstx, y, xsize, 1, 0, c, 0);
                        break;
                    case VDT_LINEDOUBLE:
                        line_double();
                        pal_convert(firstx, firsty << 1, lastx, lasty << 1, 1);
                        al_set_target_bitmap(scrshotb);
                        al_draw_bitmap_region(b32, firstx, firsty << 1, xsize, ysize << 1, 0, 0, 0);
                        break;
                    case VDT_UNSET:
                        break;
                }
                break;
            case VDC_GREEN:
                mono_col = mono_green_col;
                goto mono_screenshot;
            case VDC_AMBER:
                mono_col = mono_amber_col;
                goto mono_screenshot;
            case VDC_WHITE:
                mono_col = mono_white_col;
            mono_screenshot:
                switch(vid_dtype_intern) {
                    case VDT_SCALE:
                        mono_convert(firstx, firsty, lastx, lasty, mono_col);
                        al_set_target_bitmap(scrshotb);
                        al_draw_scaled_bitmap(b32, firstx, firsty, xsize, ysize, 0, 0, xsize, ysize << 1, 0);
                        break;
                    case VDT_INTERLACE:
                        mono_convert(firstx, firsty << 1, lastx, lasty << 1, mono_col);
                        al_set_target_bitmap(scrshotb);
                        al_draw_bitmap_region(b32, firstx, firsty << 1, xsize, ysize << 1, 0, 0, 0);
                        break;
                    case VDT_SCANLINES:
                        mono_convert(firstx, firsty, lastx, lasty, mono_col);
                        al_set_target_bitmap(scrshotb);
                        for (int c = 0, y = firsty; y < lasty; y++, c += 2)
                            al_draw_bitmap_region(b32, firstx, y, xsize, 1, 0, c, 0);
                        break;
                    case VDT_LINEDOUBLE:
                        line_double();
                        mono_convert(firstx, firsty << 1, lastx, lasty << 1, mono_col);
                        al_set_target_bitmap(scrshotb);
                        al_draw_bitmap_region(b32, firstx, firsty << 1, xsize, ysize << 1, 0, 0, 0);
                        break;
                    case VDT_UNSET:
                        break;
                }
                break;
        }
        al_save_bitmap(vid_scrshotname, scrshotb);
        al_destroy_bitmap(scrshotb);
    }
}

static inline void calc_limits(bool non_ttx, uint8_t vtotal)
{
    switch(vid_fullborders) {
        case 0:
            if (non_ttx) {
                firstx = BORDER_NONE_X_START_GRA;
                lastx  = BORDER_NONE_X_END_GRA;
            }
            else {
                firstx = BORDER_NONE_X_START_TTX;
                lastx  = BORDER_NONE_X_END_TTX;
            }
            if (vtotal > 30) {
                firsty = BORDER_NONE_Y_START_GRA;
                lasty  = BORDER_NONE_Y_END_GRA;
            }
            else {
                firsty = BORDER_NONE_Y_START_TXT;
                lasty  = BORDER_NONE_Y_END_TXT;
            }
            break;
        case 1:
            if (non_ttx) {
                firstx = BORDER_MED_X_START_GRA;
                lastx  = BORDER_MED_X_END_GRA;
            }
            else {
                firstx = BORDER_MED_X_START_TTX;
                lastx  = BORDER_MED_X_END_TTX;
            }
            if (vtotal > 30) {
                firsty = BORDER_MED_Y_START_GRA;
                lasty  = BORDER_MED_Y_END_GRA;
            }
            else {
                firsty = BORDER_MED_Y_START_TXT;
                lasty  = BORDER_MED_Y_END_TXT;
            }
            break;
        case 2:
            if (non_ttx) {
                firstx = BORDER_FULL_X_START_GRA;
                lastx  = BORDER_FULL_X_END_GRA;
            }
            else {
                firstx = BORDER_FULL_X_START_TTX;
                lastx  = BORDER_FULL_X_END_TTX;
            }
            if (vtotal > 30) {
                firsty = BORDER_FULL_Y_START_GRA;
                lasty  = BORDER_FULL_Y_END_GRA;
            }
            else {
                firsty = BORDER_FULL_Y_START_TXT;
                lasty  = BORDER_FULL_Y_END_TXT;
            }
    }
}

static inline void blit_screen(void)
{
    int xsize = lastx - firstx;
    int ysize = lasty - firsty;
    ALLEGRO_COLOR mono_col;

    switch(vid_colour_out) {
        case VDC_RGB:
            switch(vid_dtype_intern) {
                case VDT_SCALE:
                    al_unlock_bitmap(b);
                    al_set_target_backbuffer(al_get_current_display());
                    al_draw_scaled_bitmap(b, firstx, firsty, xsize, ysize, scr_x_start, scr_y_start, scr_x_size, scr_y_size, 0);
                    break;
                case VDT_INTERLACE:
                    al_unlock_bitmap(b);
                    upscale_only(b, firstx, firsty << 1, lastx - firstx, (lasty - firsty) << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                    break;
                case VDT_SCANLINES:
                    al_unlock_bitmap(b);
                    al_set_target_bitmap(b16);
                    al_clear_to_color(border_col);
                    for (int c = firsty; c < lasty; c++)
                        al_draw_bitmap_region(b, firstx, c, xsize, 1, 0, c << 1, 0);
                    upscale_only(b16, 0, firsty << 1, lastx - firstx, (lasty - firsty) << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                    break;
                case VDT_LINEDOUBLE:
                    line_double();
                    al_unlock_bitmap(b);
                    upscale_only(b, firstx, firsty << 1, xsize, ysize  << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                case VDT_UNSET:
                    break;
            }
            region = al_lock_bitmap(b, ALLEGRO_PIXEL_FORMAT_ARGB_8888, vid_lock_type);
            break;
        case VDC_PAL:
            switch(vid_dtype_intern) {
                case VDT_SCALE:
                    pal_convert(firstx, firsty, lastx, lasty, 1);
                    al_set_target_backbuffer(al_get_current_display());
                    al_draw_scaled_bitmap(b32, firstx, firsty, xsize, ysize, scr_x_start, scr_y_start, scr_x_size, scr_y_size, 0);
                    break;
                case VDT_INTERLACE:
                    pal_convert(firstx, firsty << 1, lastx, lasty << 1, 1);
                    upscale_only(b32, firstx, firsty << 1, xsize, ysize << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                    break;
                case VDT_SCANLINES:
                    pal_convert(firstx, firsty, lastx, lasty, 1);
                    al_set_target_bitmap(b16);
                    al_clear_to_color(al_map_rgb(0, 0,0));
                    for (int c = firsty; c < lasty; c++)
                        al_draw_bitmap_region(b32, firstx, c, lastx - firstx, 1, 0, c << 1, 0);
                    upscale_only(b16, 0, firsty << 1, xsize, ysize << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                    break;
                case VDT_LINEDOUBLE:
                    line_double();
                    pal_convert(firstx, firsty << 1, lastx, lasty << 1, 1);
                    upscale_only(b32, firstx, firsty << 1, xsize, ysize << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                    break;
                case VDT_UNSET:
                    break;
            }
            break;
        case VDC_GREEN:
            mono_col = mono_green_col;
            goto mono_common;
        case VDC_AMBER:
            mono_col = mono_amber_col;
            goto mono_common;
        case VDC_WHITE:
            mono_col = mono_white_col;
        mono_common:
            switch(vid_dtype_intern) {
                case VDT_SCALE:
                    mono_convert(firstx, firsty, lastx, lasty, mono_col);
                    al_set_target_backbuffer(al_get_current_display());
                    al_draw_scaled_bitmap(b32, firstx, firsty, xsize, ysize, scr_x_start, scr_y_start, scr_x_size, scr_y_size, 0);
                    break;
                case VDT_INTERLACE:
                    mono_convert(firstx, firsty << 1, lastx, lasty << 1, mono_col);
                    upscale_only(b32, firstx, firsty << 1, xsize, ysize << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                    break;
                case VDT_SCANLINES:
                    mono_convert(firstx, firsty, lastx, lasty, mono_col);
                    al_set_target_bitmap(b16);
                    al_clear_to_color(al_map_rgb(0, 0,0));
                    for (int c = firsty; c < lasty; c++)
                        al_draw_bitmap_region(b32, firstx, c, lastx - firstx, 1, 0, c << 1, 0);
                    upscale_only(b16, 0, firsty << 1, xsize, ysize << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                    break;
                case VDT_LINEDOUBLE:
                    line_double();
                    mono_convert(firstx, firsty << 1, lastx, lasty << 1, mono_col);
                    upscale_only(b32, firstx, firsty << 1, xsize, ysize << 1, scr_x_start, scr_y_start, scr_x_size, scr_y_size);
                    break;
                case VDT_UNSET:
                    break;

            }
    }
}

static inline void fill_pillarbox(void)
{
    // fill the gap between the left screen edge and the BBC image.
    al_draw_filled_rectangle(0, 0, scr_x_start, scr_y_size, border_col);
    // fill the gap between the BBC image and the right screen edge.
    al_draw_filled_rectangle(scr_x_start + scr_x_size, 0, winsizex, winsizey, border_col);
}

static inline void fill_letterbox(void)
{
    // fill the gap between the top of the screen and the BBC image.
    al_draw_filled_rectangle(0, 0, scr_x_size, scr_y_start, border_col);
    // fill the gap between the BBC image and the bottom of the screen.
    al_draw_filled_rectangle(0, scr_y_start + scr_y_size, winsizex, winsizey, border_col);
}

static void render_leds(void)
{
    if (vid_ledlocation > LED_LOC_NONE) {
        if (led_ticks > 0 && --led_ticks == 0) {
            led_timer_fired();
            al_set_target_backbuffer(al_get_current_display());
        }
        float w = al_get_bitmap_width(led_bitmap);
        float h = al_get_bitmap_height(led_bitmap);
        float scale = vid_display_scale();
        float dw = w * scale;
        float dh = h * scale;
        float dx = ((float)winsizex - dw) / 2.0f;
        float dy = (float)winsizey - dh;
        if (vid_ledvisibility == LED_VIS_ALWAYS || (vid_ledvisibility == LED_VIS_TRANSIENT && led_any_transient_led_on())) {
            log_debug("led: drawing non-faded bitmap");
            al_draw_scaled_bitmap(led_bitmap, 0, 0, w, h, dx, dy, dw, dh, 0);
        }
        else {
            ALLEGRO_COLOR led_tint;
            const int led_visible_for_frames = 50;
            const int led_fade_frames = 25;

            int led_visible_frames_left = led_visible_for_frames - (framesrun - last_led_update_at);
            if (led_visible_frames_left > 0) {
                log_debug("led: visible frames left=%d", led_visible_frames_left);
                if (led_visible_frames_left <= led_fade_frames) {
                    int i = (255 * led_visible_frames_left) / led_fade_frames;
                    log_debug("led: tint, i=%d", i);
                    led_tint = al_map_rgba(i, i, i, vid_ledlocation == LED_LOC_SEPARATE ? 255 : i);
                }
                else
                    led_tint = al_map_rgb(255, 255, 255);
            }
            else
                led_tint = al_map_rgba(0, 0, 0, vid_ledlocation == LED_LOC_SEPARATE ? 255 : 0);
            al_draw_tinted_scaled_bitmap(led_bitmap, led_tint, 0, 0, w, h, dx, dy, dw, dh, 0);
        }
    }
}

void video_doblit(bool non_ttx, uint8_t vtotal)
{
    if (vid_savescrshot)
        save_screenshot();

    ++framesrun;
    if (++fskipcount >= ((motor && fasttape) ? 5 : vid_fskipmax)) {
        if (fullscreen_pending) {
            ALLEGRO_DISPLAY *display = al_get_current_display();
            int newsizex = al_get_display_width(display);
            int newsizey = al_get_display_height(display);
            log_debug("vidalleg: fullscreen_pending=%d, newsizex=%d, newsizey=%d", fullscreen_pending, newsizex, newsizey);
            --fullscreen_pending;
            if (newsizex > winsizex || newsizey > winsizey) {
                winsizex = newsizex;
                winsizey = newsizey;
                video_calc_fullscreen(display);
                fullscreen_pending = 0;
            }
        }
#ifdef __APPLE__
        if (vid_live_resizing) {
            al_flip_display();
        } else {
#endif
        /* If the OS delivered a window that differs from our calculated
         * winsizey (Windows DPI rounding, menu-bar insertion, etc.), sync
         * now before rendering so the LED strip is always at the bottom. */
        if (!fullscreen) {
            ALLEGRO_DISPLAY *cur = al_get_current_display();
            int actual_w = al_get_display_width(cur);
            int actual_h = al_get_display_height(cur);
            if (actual_w != winsizex || actual_h != winsizey) {
                log_debug("vidalleg: video_doblit sync: winsizex %d→%d winsizey %d→%d",
                          winsizex, actual_w, winsizey, actual_h);
                scr_x_start = 0;
                scr_x_size = winsizex = actual_w;
                scr_y_start = 0;
                winsizey = actual_h;
                scr_y_size = winsizey - video_led_height();
                if (scr_y_size < 0) scr_y_size = 0;
            }
        }
        lasty++;
        calc_limits(non_ttx, vtotal);
        fskipcount = 0;
        blit_screen();
        if (scr_x_start > 0)
            fill_pillarbox();
        else if (scr_y_start > 0)
            fill_letterbox();

        render_leds();
        al_set_target_bitmap(b);
        al_flip_display();
#ifdef __APPLE__
        }
#endif
    }
    firstx = firsty = 65535;
    lastx  = lasty  = 0;
}
