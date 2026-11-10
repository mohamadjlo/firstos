#include "gui_overlays.h"
#include "gui.h"
#include "gui_internal.h"
#include "gui_calc.h"
#include "spotlight.h"
#include "explorer.h"
#include "framebuffer.h"
#include "font.h"
#include "ps2.h"
#include "hda.h"
#include "fs.h"
#include "terminal.h"
#include "io.h"

/* State owned by overlays subsystem */
int launcher_open = 0;
int launcher_hover = -1;
int explorer_quick_look_open = 0;
rect_t explorer_ql_close_btn;
rect_t explorer_ql_play_btn;
rect_t explorer_ql_open_btn;

/* External state provided by gui core */
extern int spotlight_open;
extern char spotlight_query[64];
extern int spotlight_qlen;
extern int explorer_selected;
extern int explorer_anim_ticks;
extern int cursor_blink_visible;
extern int terminal_cursor_visible;
extern int terminal_focused;
extern char cached_thumbnail_path[128];
extern u32 *cached_thumbnail_data;
extern int cached_thumbnail_w;
extern int cached_thumbnail_h;
extern char text_preview_buf[1024];

void update_explorer_thumbnail(const char *path, const char *ext);
void text_preview_update(const char *path);
int  load_ico_from_path(const char* path, u32* out_pixels, int* out_w, int* out_h);
void draw_icon_scaled(const u32 *pixels, int src_w, int src_h, int x, int y, int dst_w, int dst_h);
void terminal_draw(int frame_x, int frame_y, int frame_w, int frame_h);

/* Launcher cached browser icon */
static u32 launcher_browser_ico_pixels[32 * 32];
static int launcher_browser_ico_width = 0;
static int launcher_browser_ico_height = 0;
static int launcher_browser_ico_loaded = 0;

/* String search helper (also called by explorer.c) */
int sp_str_match(const char *hay, const char *ned) {
    for (int h = 0; hay[h]; h++) {
        int ok = 1;
        for (int n = 0; ned[n]; n++) {
            char a = hay[h+n]; char b = ned[n];
            if (a>='A'&&a<='Z') a+=32;
            if (b>='A'&&b<='Z') b+=32;
            if (a != b) { ok = 0; break; }
        }
        if (ok && ned[0]) return 1;
    }
    return 0;
}

/* ================================================================
   LAUNCHER OVERLAY (Launchpad)
   ================================================================ */
void draw_launcher_overlay(int mouse_x, int mouse_y) {
    if (!launcher_open) return;
    int sw = (int)fb.width;
    int sh = (int)fb.height;
    /* Full-screen semi-dark overlay */
    fb_rect(0, 0, sw, sh, 0x0A1828);
    /* Subtle edge darkening */
    fb_rect(0, 0, 80, sh, 0x060F1A);
    fb_rect(sw - 80, 0, 80, sh, 0x060F1A);

    /* Title */
    font_text(sw/2 - 44, 22, "Launchpad", 0xAAC8E8, 1);
    fb_rect(sw/2 - 44, 36, 88, 1, 0x3A5A7A);
    draw_label(sw - 110, 26, "[Esc] Close", 0x445566);

    /* App grid */
    #define LNCOLS 4
    static const int lapp_id[12]   = {APP_TERMINAL,APP_EXPLORER,APP_TEXT_EDIT,APP_FILES,APP_SETTINGS,APP_CALCULATOR,APP_MEDIA_PLAYER,APP_PHOTO_EDITOR,APP_IDE,APP_TASKMAN,APP_PREVIEW,APP_BROWSER};
    static const int lapp_ic[12]   = {ICON_TERMINAL,ICON_EXPLORER,ICON_FILE,ICON_FILES,ICON_SETTINGS,ICON_DISPLAY,ICON_DISPLAY,ICON_FILE,ICON_FILE,ICON_DISPLAY,ICON_FILE,ICON_NETWORK};
    static const char *lapp_nm[12] = {"Terminal","Explorer","Text Edit","Files","Settings","Calculator","Media Player","Photo Viewer","IDE","Task Manager","Preview","Web Browser"};

    int cell_w = 140, cell_h = 120;
    int total_w = LNCOLS * cell_w;
    int sx = (sw - total_w) / 2;
    int sy = 56;

    launcher_hover = -1;
    for (int i = 0; i < 12; i++) {
        int col = i % LNCOLS;
        int row = i / LNCOLS;
        int cx = sx + col * cell_w;
        int cy = sy + row * cell_h;
        rect_t cell = (rect_t){cx, cy, cell_w, cell_h, 0};
        int hov = point_in_rect(mouse_x, mouse_y, cell);
        if (hov) launcher_hover = i;
        if (hov) {
            fb_rect(cx + 16, cy + 8, cell_w - 32, cell_h - 28, 0x1E3A5A);
            outline(cx + 16, cy + 8, cell_w - 32, cell_h - 28, 0x3A6080);
        }
        if (lapp_id[i] == APP_BROWSER) {
            if (!launcher_browser_ico_loaded) {
                launcher_browser_ico_loaded = 1;
                load_ico_from_path("/icons/browser.ico", launcher_browser_ico_pixels, &launcher_browser_ico_width, &launcher_browser_ico_height);
            }
            if (launcher_browser_ico_width > 0) {
                draw_icon_scaled(launcher_browser_ico_pixels, launcher_browser_ico_width, launcher_browser_ico_height, cx + cell_w/2 - 16, cy + 16, 32, 32);
            } else {
                font_icon(cx + cell_w/2 - 8, cy + 20, lapp_ic[i], hov ? 0xFFFFFF : 0xFF7139);
            }
        } else {
            font_icon(cx + cell_w/2 - 8, cy + 20, lapp_ic[i], hov ? 0xFFFFFF : 0x7AAAC8);
        }
        font_text_fit_center(cx, cy + 70, lapp_nm[i], hov ? 0xFFFFFF : 0xAABBCC, cell_w);
    }
}

/* ================================================================
   SPOTLIGHT OVERLAY
   ================================================================ */
void draw_spotlight_overlay(void) {
    if (!spotlight_open) return;
    int sw = (int)fb.width;
    int sh = (int)fb.height;

    if (spotlight_view_mode == SPOTLIGHT_VIEW_APP_DROPDOWN) {
        int bw = 580;
        int dh = 320;
        int bx = (sw - bw) / 2;
        int by = sh / 6;

        /* Search bar */
        fb_rect(bx - 3, by - 3, bw + 6, 46, 0x060F1A);
        fb_rect(bx, by, bw, 40, 0x162436);
        outline(bx, by, bw, 40, 0x4A7098);
        draw_label(bx + 12, by + 12, "O-", 0x4A7098);

        if (spotlight_active_app_kind == APP_TERMINAL) {
            font_text_fit(bx + 36, by + 12, "Terminal  (Active Dropdown Console)", 0x6EE492, bw - 50);
        } else if (spotlight_active_app_kind == APP_CALCULATOR) {
            font_text_fit(bx + 36, by + 12, "Calculator  (Active Dropdown)", 0x4DA3FF, bw - 50);
        } else if (spotlight_active_app_kind == APP_EXPLORER) {
            char title_bar[128];
            int p = 0;
            const char *prefix = "Dropdown Explorer: ";
            while (prefix[p]) { title_bar[p] = prefix[p]; p++; }
            int q = 0;
            while (spotlight_explorer_path[q] && p < 127) { title_bar[p++] = spotlight_explorer_path[q++]; }
            title_bar[p] = 0;
            font_text_fit(bx + 36, by + 12, title_bar, 0x4DA3FF, bw - 50);
        } else {
            font_text_fit(bx + 36, by + 12, "Application Dropdown View", 0xFFFFFF, bw - 50);
        }

        /* Dropdown body */
        fb_rect(bx - 3, by + 44, bw + 6, dh + 6, 0x060F1A);
        fb_rect(bx, by + 44, bw, dh, 0x0A121E);
        outline(bx, by + 44, bw, dh, 0x2A4060);

        /* Header bar */
        fb_rect(bx, by + 44, bw, 28, 0x142236);
        fb_rect(bx, by + 44 + 27, bw, 1, 0x243854);

        if (spotlight_active_app_kind == APP_TERMINAL) {
            font_icon(bx + 10, by + 50, ICON_TERMINAL, 0x6EE492);
            draw_label(bx + 34, by + 50, "TERMINAL CONSOLE", 0xFFFFFF);
        } else if (spotlight_active_app_kind == APP_EXPLORER) {
            font_icon(bx + 10, by + 50, ICON_EXPLORER, 0x4DA3FF);
            draw_label(bx + 34, by + 50, "SPOTLIGHT EXPLORER", 0xFFFFFF);

            /* [< Up] button */
            fb_rect(bx + 185, by + 48, 55, 20, 0x1E334D);
            outline(bx + 185, by + 48, 55, 20, 0x3E5D82);
            draw_label(bx + 193, by + 52, "< Up", 0xCCDDEE);

            /* Path pill */
            fb_rect(bx + 246, by + 48, 150, 20, 0x0E1A2A);
            outline(bx + 246, by + 48, 150, 20, 0x2A4060);
            font_text_fit(bx + 252, by + 52, spotlight_explorer_path, 0x7EAECE, 138);

            /* Item count */
            char cnt_str[32];
            int_to_str(spotlight_explorer_count, cnt_str);
            int cl = sl_strlen(cnt_str);
            cnt_str[cl++] = ' '; cnt_str[cl++] = 'i'; cnt_str[cl++] = 't'; cnt_str[cl++] = 'm'; cnt_str[cl++] = 's'; cnt_str[cl] = 0;
            draw_label(bx + 402, by + 52, cnt_str, 0x5A7A9A);
        } else {
            font_icon(bx + 10, by + 50, ICON_APPS, 0x4DA3FF);
            draw_label(bx + 34, by + 50, "APPLICATION DROPDOWN", 0xFFFFFF);
        }

        /* Close button on right */
        fb_rect(bx + bw - 115, by + 48, 105, 20, 0x1E334D);
        outline(bx + bw - 115, by + 48, 105, 20, 0x3E5D82);
        draw_label(bx + bw - 105, by + 52, "ESC: Close", 0xCCDDEE);

        /* Render content */
        if (spotlight_active_app_kind == APP_TERMINAL) {
            terminal_cursor_visible = cursor_blink_visible;
            terminal_focused = 1;
            terminal_draw(bx + 2, by + 44 + 28, bw - 4, dh - 28 - 2);
        } else if (spotlight_active_app_kind == APP_EXPLORER) {
            /* Left Sidebar */
            int sb_x = bx + 2;
            int sb_y = by + 44 + 28;
            int sb_w = 134;
            int sb_h = dh - 28 - 2;
            fb_rect(sb_x, sb_y, sb_w, sb_h, 0x0C1624);
            fb_rect(sb_x + sb_w - 1, sb_y, 1, sb_h, 0x1E324A);

            /* Sidebar Disks */
            draw_label(sb_x + 8, sb_y + 8, "DISKS & DRIVES", 0x4A6A88);
            
            int is_d0_sel = (sl_strcmp(spotlight_explorer_path, "FirstOS") == 0);
            if (is_d0_sel) fb_rect(sb_x + 4, sb_y + 24, sb_w - 8, 20, 0x1E3A5A);
            font_icon(sb_x + 8, sb_y + 26, ICON_DISK, is_d0_sel ? 0x6EE492 : 0x4D7A95);
            font_text_fit(sb_x + 28, sb_y + 28, "FirstOS HD", is_d0_sel ? 0xFFFFFF : 0x8AACCF, sb_w - 32);

            int is_d1_sel = (sl_strcmp(spotlight_explorer_path, "FirstOS/System") == 0);
            if (is_d1_sel) fb_rect(sb_x + 4, sb_y + 46, sb_w - 8, 20, 0x1E3A5A);
            font_icon(sb_x + 8, sb_y + 48, ICON_DISK, is_d1_sel ? 0x6EE492 : 0x4D7A95);
            font_text_fit(sb_x + 28, sb_y + 50, "System Vol", is_d1_sel ? 0xFFFFFF : 0x8AACCF, sb_w - 32);

            /* Sidebar Places */
            draw_label(sb_x + 8, sb_y + 74, "PLACES", 0x4A6A88);
            static const struct { const char *label; const char *path; int ico; } places[] = {
                {"Apps",       "FirstOS/Apps",       ICON_APPS},
                {"Wallpapers", "FirstOS/Wallpapers", ICON_PHOTO},
                {"Movies",     "FirstOS/Movies",     ICON_DISPLAY},
                {"Audio",      "FirstOS/Audio",      ICON_DISPLAY},
                {"Desktop",    "FirstOS/Desktop",    ICON_FOLDER},
                {"Trash",      "FirstOS/Trash",      ICON_FOLDER}
            };
            for (int p = 0; p < 6; p++) {
                int py = sb_y + 90 + p * 22;
                int p_sel = (sl_strcmp(spotlight_explorer_path, places[p].path) == 0);
                if (p_sel) fb_rect(sb_x + 4, py, sb_w - 8, 20, 0x1E3A5A);
                font_icon(sb_x + 8, py + 2, places[p].ico, p_sel ? 0x4DA3FF : 0x5A7A9A);
                font_text_fit(sb_x + 28, py + 4, places[p].label, p_sel ? 0xFFFFFF : 0x8AACCF, sb_w - 32);
            }

            /* Right File Viewport */
            int fv_x = sb_x + sb_w;
            int fv_y = sb_y;
            int fv_w = bw - sb_w - 4;
            int fv_h = sb_h - 22;
            fb_rect(fv_x, fv_y, fv_w, fv_h, 0x09121E);

            int scroll = 0;
            if (spotlight_explorer_selected >= 8) scroll = spotlight_explorer_selected - 7;
            int max_visible = 9;

            for (int i = scroll; i < spotlight_explorer_count && (i - scroll) < max_visible; i++) {
                int row_y = fv_y + 4 + (i - scroll) * 26;
                int is_sel = (i == spotlight_explorer_selected);
                if (is_sel) {
                    fb_rect(fv_x + 4, row_y, fv_w - 8, 24, 0x1E3A5A);
                    outline(fv_x + 4, row_y, fv_w - 8, 24, 0x3E7AB0);
                }
                fb_rect(fv_x + 4, row_y + 25, fv_w - 8, 1, 0x101B2A);

                explorer_entry_t *e = &spotlight_explorer_entries[i];
                int ico = e->cached_icon ? e->cached_icon : (e->is_dir ? ICON_FOLDER : ICON_FILE);
                u32 ico_col = is_sel ? 0xAADDFF : (e->is_dir ? 0x6EBAE8 : 0x5A7A9A);
                font_icon(fv_x + 10, row_y + 4, ico, ico_col);
                font_text_fit(fv_x + 36, row_y + 5, e->name, is_sel ? 0xFFFFFF : 0xCCDDEE, fv_w - 180);

                if (is_sel) {
                    if (e->is_dir) {
                        draw_label(fv_x + fv_w - 120, row_y + 5, "[Enter: Open]", 0x6EE492);
                    } else {
                        draw_label(fv_x + fv_w - 120, row_y + 5, "[Enter: Launch]", 0x4DA3FF);
                    }
                } else {
                    draw_label(fv_x + fv_w - 90, row_y + 5, e->is_dir ? "Folder" : "File", 0x3E5E7E);
                }
            }
            if (spotlight_explorer_count == 0) {
                draw_label(fv_x + 20, fv_y + 30, "Folder is empty", 0x4A6A88);
            }

            /* Bottom Hint Strip */
            int hint_y = sb_y + fv_h;
            fb_rect(fv_x, hint_y, fv_w, 22, 0x060E18);
            fb_rect(fv_x, hint_y, fv_w, 1, 0x1E324A);
            draw_label(fv_x + 8, hint_y + 5, "Up/Down: Select   Enter: Open/Descend   Left/Bksp: Up Dir   Esc: Close", 0x3A5A7A);
        } else if (spotlight_active_app_kind == APP_CALCULATOR) {
            app_window_t *calc_app = app_slot(APP_CALCULATOR);
            if (calc_app) {
                rect_t old_frame = calc_app->frame;
                calc_app->frame = (rect_t){bx + 10, by + 44 + 32, bw - 20, dh - 40, 0};
                draw_calculator_contents(calc_app);
                calc_app->frame = old_frame;
            }
        }
        return;
    }

    if (spotlight_view_mode == SPOTLIGHT_VIEW_MEDIA_PREVIEW) {
        int bw = 560;
        int dh = 310;
        int bx = (sw - bw) / 2;
        int by = sh / 6;

        /* Search bar */
        fb_rect(bx - 3, by - 3, bw + 6, 46, 0x060F1A);
        fb_rect(bx, by, bw, 40, 0x162436);
        outline(bx, by, bw, 40, 0x4A7098);
        draw_label(bx + 12, by + 12, "O-", 0x4A7098);
        font_text_fit(bx + 36, by + 12, spotlight_media_title, 0xFFFFFF, bw - 50);

        /* Dropdown body */
        fb_rect(bx - 3, by + 44, bw + 6, dh + 6, 0x060F1A);
        fb_rect(bx, by + 44, bw, dh, 0x0D1726);
        outline(bx, by + 44, bw, dh, 0x2E4A6E);

        /* Header bar */
        fb_rect(bx, by + 44, bw, 28, 0x16263D);
        fb_rect(bx, by + 44 + 27, bw, 1, 0x2A4060);

        /* Close button on right */
        fb_rect(bx + bw - 120, by + 48, 110, 20, 0x203650);
        outline(bx + bw - 120, by + 48, 110, 20, 0x406085);
        draw_label(bx + bw - 110, by + 52, "ESC: Close", 0xCCDDEE);

        if (spotlight_media_type == SPOTLIGHT_MEDIA_AUDIO) {
            font_icon(bx + 10, by + 50, ICON_DISPLAY, 0x5ED38E);
            draw_label(bx + 34, by + 50, "AUDIO PREVIEW & PLAYER", 0xFFFFFF);

            /* Track Info */
            draw_label(bx + 24, by + 84, spotlight_media_title, 0xFFFFFF);
            draw_label(bx + 24, by + 104, "Format: WAV PCM 44.1kHz 16-bit Stereo", 0x7E9EB8);
            font_text_fit(bx + 24, by + 122, spotlight_media_path, 0x4E6E88, bw - 48);

            /* Equalizer / Spectrum Visualizer */
            int spec_x = bx + 24;
            int spec_y = by + 144;
            int spec_w = bw - 48;
            int spec_h = 68;
            fb_rect(spec_x, spec_y, spec_w, spec_h, 0x070E18);
            outline(spec_x, spec_y, spec_w, spec_h, 0x1E334D);

            int num_bars = 28;
            int bar_w = (spec_w - (num_bars + 1) * 3) / num_bars;
            for (int b = 0; b < num_bars; b++) {
                int bh = 6;
                if (spotlight_media_playing) {
                    int wave1 = (spotlight_media_anim_ticks * 5 + b * 17) % 100;
                    int wave2 = ((spotlight_media_anim_ticks * 3 + b * 31)) % 70;
                    bh = 12 + (wave1 * 36 / 100) + (wave2 * 18 / 70);
                    if (bh > spec_h - 8) bh = spec_h - 8;
                } else {
                    bh = 6 + (b % 4) * 5;
                }
                int bx_pos = spec_x + 4 + b * (bar_w + 3);
                int by_pos = spec_y + spec_h - bh - 4;
                u32 bar_col = spotlight_media_playing ? (b < 8 ? 0x4CD37B : (b < 18 ? 0x4DA3FF : 0xFFB347)) : 0x2A3E54;
                fb_rect(bx_pos, by_pos, bar_w, bh, bar_col);
            }

            /* Transport controls */
            fb_rect(bx + 24, by + 224, 96, 26, spotlight_media_playing ? 0x2B4B75 : 0x1B683B);
            outline(bx + 24, by + 224, 96, 26, spotlight_media_playing ? 0x4D79B3 : 0x369B5C);
            draw_label(bx + 34, by + 230, spotlight_media_playing ? "|| Pause" : "> Play", 0xFFFFFF);

            /* Time scrubber */
            fb_rect(bx + 130, by + 233, bw - 260, 8, 0x182638);
            int play_pct = spotlight_media_playing ? ((spotlight_media_anim_ticks * 2) % 100) : 25;
            fb_rect(bx + 130, by + 233, (bw - 260) * play_pct / 100, 8, 0x4DA3FF);

            /* Open in Media Player button */
            fb_rect(bx + bw - 120, by + 224, 96, 26, 0x18304A);
            outline(bx + bw - 120, by + 224, 96, 26, 0x325A84);
            draw_label(bx + bw - 110, by + 230, "Open Player", 0xCCDDEE);

            /* Bottom hint */
            draw_label(bx + 24, by + 268, "Space: Play / Pause    Enter: Open in Media Player    Esc: Close", 0x3E5E7E);
        }
        else if (spotlight_media_type == SPOTLIGHT_MEDIA_VIDEO) {
            font_icon(bx + 10, by + 50, ICON_DISPLAY, 0x4DA3FF);
            draw_label(bx + 34, by + 50, "MOVIE / VIDEO PREVIEW", 0xFFFFFF);

            /* Left Cinema Screen */
            int sc_x = bx + 20, sc_y = by + 80, sc_w = 230, sc_h = 160;
            fb_rect(sc_x, sc_y, sc_w, sc_h, 0x050910);
            outline(sc_x, sc_y, sc_w, sc_h, 0x2A3E56);

            /* Filmstrip ticks on top & bottom */
            for (int fx = sc_x + 4; fx < sc_x + sc_w - 8; fx += 14) {
                fb_rect(fx, sc_y + 3, 8, 4, 0x223344);
                fb_rect(fx, sc_y + sc_h - 7, 8, 4, 0x223344);
            }

            /* Video Center Play Symbol */
            fb_rect(sc_x + sc_w / 2 - 20, sc_y + sc_h / 2 - 20, 40, 40, spotlight_media_playing ? 0x2B4B75 : 0x1B3048);
            outline(sc_x + sc_w / 2 - 20, sc_y + sc_h / 2 - 20, 40, 40, spotlight_media_playing ? 0x5D99E3 : 0x4070A0);
            draw_label(sc_x + sc_w / 2 - 4, sc_y + sc_h / 2 - 6, spotlight_media_playing ? "||" : ">", 0xFFFFFF);

            /* Video Scrubber bar */
            fb_rect(sc_x, sc_y + sc_h + 8, sc_w, 6, 0x162436);
            int vid_pct = spotlight_media_playing ? ((spotlight_media_anim_ticks * 3) % 100) : 35;
            fb_rect(sc_x, sc_y + sc_h + 8, sc_w * vid_pct / 100, 6, 0x4DA3FF);

            /* Right Metadata Panel */
            draw_label(bx + 265, by + 84, spotlight_media_title, 0xFFFFFF);
            draw_label(bx + 265, by + 108, "Container: AVI (RIFF Video Stream)", 0x8AACCF);
            draw_label(bx + 265, by + 128, "Resolution: 320 x 240 px", 0x6E8CA8);
            draw_label(bx + 265, by + 146, "Framerate: 24 fps (72 frames)", 0x6E8CA8);
            draw_label(bx + 265, by + 164, "Audio: 44.1kHz Stereo", 0x6E8CA8);
            font_text_fit(bx + 265, by + 184, spotlight_media_path, 0x4E6E88, 270);

            /* Open in Media Player Button */
            fb_rect(bx + 265, by + 214, 160, 26, 0x18304A);
            outline(bx + 265, by + 214, 160, 26, 0x325A84);
            draw_label(bx + 275, by + 220, "Open in Media Player", 0xCCDDEE);

            /* Bottom hint */
            draw_label(bx + 20, by + 268, "Space: Play / Pause    Enter: Open in Media Player    Esc: Close", 0x3E5E7E);
        }
        else if (spotlight_media_type == SPOTLIGHT_MEDIA_IMAGE) {
            font_icon(bx + 10, by + 50, ICON_PHOTO, 0xFFB347);
            draw_label(bx + 34, by + 50, "IMAGE PREVIEW", 0xFFFFFF);

            /* Load thumbnail if needed */
            if (spotlight_media_path[0]) {
                int path_changed = 0;
                int k = 0;
                while (spotlight_media_path[k] && cached_thumbnail_path[k]) {
                    if (spotlight_media_path[k] != cached_thumbnail_path[k]) { path_changed = 1; break; }
                    k++;
                }
                if (spotlight_media_path[k] != cached_thumbnail_path[k]) path_changed = 1;
                if (path_changed || !cached_thumbnail_data) {
                    k = 0;
                    while (spotlight_media_path[k] && k < 127) { cached_thumbnail_path[k] = spotlight_media_path[k]; k++; }
                    cached_thumbnail_path[k] = 0;
                    int ext = 0;
                    for (int j = 0; spotlight_media_path[j]; j++) { if (spotlight_media_path[j] == '.') ext = j + 1; }
                    if (ext > 0) update_explorer_thumbnail(spotlight_media_path, &spotlight_media_path[ext]);
                }
            }

            /* Left Image Viewport */
            int iv_x = bx + 20, iv_y = by + 80, iv_w = 230, iv_h = 160;
            fb_rect(iv_x, iv_y, iv_w, iv_h, 0x080E18);
            outline(iv_x, iv_y, iv_w, iv_h, 0x2A3E56);

            /* Draw real cached thumbnail or placeholder */
            if (cached_thumbnail_data && cached_thumbnail_w > 0 && cached_thumbnail_h > 0) {
                int dx = iv_x + (iv_w - cached_thumbnail_w) / 2;
                int dy = iv_y + (iv_h - cached_thumbnail_h) / 2;
                for (int ty = 0; ty < cached_thumbnail_h; ty++) {
                    u32 *src_row = cached_thumbnail_data + (ty * cached_thumbnail_w);
                    for (int tx = 0; tx < cached_thumbnail_w; tx++) {
                        fb_putpixel(dx + tx, dy + ty, src_row[tx]);
                    }
                }
            } else {
                font_icon(iv_x + iv_w / 2 - 12, iv_y + iv_h / 2 - 20, ICON_PHOTO, 0x6090C0);
                draw_label(iv_x + iv_w / 2 - 34, iv_y + iv_h / 2 + 10, "Image Canvas", 0x5A7A9A);
            }

            /* Right Metadata Panel */
            draw_label(bx + 265, by + 84, spotlight_media_title, 0xFFFFFF);
            draw_label(bx + 265, by + 108, "Format: Image (RGB TrueColor)", 0x8AACCF);
            draw_label(bx + 265, by + 128, "Dimensions: 1024 x 768 px", 0x6E8CA8);
            draw_label(bx + 265, by + 146, "Color Depth: 32-bit", 0x6E8CA8);
            font_text_fit(bx + 265, by + 172, spotlight_media_path, 0x4E6E88, 270);

            /* Open in Preview Button */
            fb_rect(bx + 265, by + 214, 150, 26, 0x18304A);
            outline(bx + 265, by + 214, 150, 26, 0x325A84);
            draw_label(bx + 275, by + 220, "Open in Preview", 0xCCDDEE);

            /* Bottom hint */
            draw_label(bx + 20, by + 268, "Enter: Open in Preview App    Esc: Close Preview", 0x3E5E7E);
        }
        return;
    }

    /* Standard Results List Mode (SPOTLIGHT_VIEW_SEARCH) */
    /* Panel */
    int bw = 480;
    int bx = (sw - bw) / 2;
    int by = sh / 5;

    /* Search bar */
    fb_rect(bx - 3, by - 3, bw + 6, 46, 0x060F1A);
    fb_rect(bx, by, bw, 40, 0x162436);
    outline(bx, by, bw, 40, 0x4A7098);

    /* Magnifier symbol */
    draw_label(bx + 12, by + 12, "O-", 0x4A7098);

    if (spotlight_qlen > 0) {
        font_text_fit(bx + 36, by + 12, spotlight_query, 0xFFFFFF, bw - 50);
    } else {
        draw_label(bx + 36, by + 12, "Search apps and files...", 0x2A4060);
    }
    /* Cursor bar */
    fb_rect(bx + 36 + spotlight_qlen * 7, by + 27, 6, 2, 0x5A90C0);

    /* Results with ghosting prevention */
    int rh = 36;
    int max_rh = 10 * rh + 32;
    fb_rect(bx - 3, by + 44, bw + 6, max_rh, 0x060F1A);

    int results_to_show = (spotlight_num_results > 10) ? 10 : spotlight_num_results;
    int actual_h = (results_to_show > 0) ? (results_to_show * rh) : rh;

    fb_rect(bx, by + 44, bw, actual_h, 0x101E30);
    outline(bx, by + 44, bw, actual_h, 0x2A3A50);

    if (results_to_show == 0 && spotlight_qlen > 0) {
        draw_label(bx + 20, by + 56, "No results found", 0x3A5070);
    }

    for (int i = 0; i < results_to_show; i++) {
        int ry = by + 44 + i * rh;
        int sel = (spotlight_selected_index == i);
        if (sel) {
            fb_rect(bx + 1, ry + 1, bw - 2, rh - 2, 0x1E3A5A);
        }
        fb_rect(bx, ry + rh - 1, bw, 1, 0x1E3050);
        int ico = spotlight_results[i].icon_id ? spotlight_results[i].icon_id : ICON_FILE;
        font_icon(bx + 12, ry + 10, ico, sel ? 0xAADDFF : 0x4A6A88);
        font_text_fit(bx + 38, ry + 10, spotlight_results[i].title, sel ? 0xFFFFFF : 0x8AACCF, 150);
        if (sel) {
            int hint_w = 96;
            font_text_fit(bx + 195, ry + 10, spotlight_results[i].subtitle, 0x4A6A88, bw - 195 - hint_w - 6);
            if (spotlight_results[i].icon_id == ICON_FOLDER || spotlight_results[i].icon_id == ICON_DISK ||
                spotlight_results[i].app_kind == APP_EXPLORER) {
                draw_label(bx + bw - 102, ry + 10, "[Tab Explorer]", 0x4DA3FF);
            } else if (spotlight_results[i].plugin_id == 3 || spotlight_results[i].app_kind > 0) {
                draw_label(bx + bw - 84, ry + 10, "[Tab View]", 0x2A6A9A);
            } else if (spotlight_results[i].media_type != SPOTLIGHT_MEDIA_NONE) {
                draw_label(bx + bw - 96, ry + 10, "[Tab Preview]", 0x3A7AAA);
            } else {
                draw_label(bx + bw - 64, ry + 10, "[Enter]", 0x2A4A6A);
            }
        }
    }

    /* Isolated footer hint bar */
    int footer_y = by + 44 + actual_h + 4;
    fb_rect(bx, footer_y, bw, 20, 0x0C1624);
    outline(bx, footer_y, bw, 20, 0x1A2838);
    draw_label(bx + 12, footer_y + 6, "Up/Down: navigate | Tab: Preview | Enter: open | Esc: close", 0x4A6A88);
}

/* ================================================================
   EXPLORER QUICK LOOK MODAL
   ================================================================ */
void draw_explorer_quick_look(void) {
    if (!explorer_quick_look_open) return;
    int entry_count = explorer_entry_count();
    if (explorer_selected < 0 || explorer_selected >= entry_count) {
        explorer_quick_look_open = 0;
        return;
    }
    const explorer_entry_t *entry = explorer_entry_at(explorer_selected);
    if (!entry || entry->is_dir) {
        explorer_quick_look_open = 0;
        return;
    }

    int sw = (int)fb.width;
    int sh = (int)fb.height;
    int modal_w = 540;
    int modal_h = 380;
    if (modal_w > sw - 40) modal_w = sw - 40;
    if (modal_h > sh - 60) modal_h = sh - 60;
    int modal_x = (sw - modal_w) / 2;
    int modal_y = (sh - modal_h) / 2;

    // Drop shadow
    fb_rect(modal_x + 6, modal_y + 6, modal_w, modal_h, 0x0A1018);
    // Outer frame
    fb_rect(modal_x, modal_y, modal_w, modal_h, 0x1E242B);
    outline(modal_x, modal_y, modal_w, modal_h, 0x4B627D);
    outline(modal_x + 1, modal_y + 1, modal_w - 2, modal_h - 2, 0x2A3747);

    // Titlebar
    int tb_h = 36;
    fb_rect(modal_x + 2, modal_y + 2, modal_w - 4, tb_h, 0x28313D);
    fb_rect(modal_x + 2, modal_y + tb_h + 1, modal_w - 4, 1, 0x3E4E63);

    // Titlebar icon & name
    int ext = 0, j = 0;
    while (entry->name[j]) { if (entry->name[j] == '.') ext = j + 1; j++; }
    int icon = ICON_FILE;
    if (explorer_is_image_file(entry->name)) icon = ICON_DISPLAY;
    else if (explorer_is_video_file(entry->name)) icon = ICON_DISPLAY;
    else if (explorer_is_audio_file(entry->name)) icon = ICON_FILE;
    font_icon(modal_x + 14, modal_y + 10, icon, 0x7FA2C9);
    font_text_fit(modal_x + 36, modal_y + 12, entry->name, 0xFFFFFF, modal_w - 140);
    draw_label(modal_x + modal_w - 95, modal_y + 12, "Quick Look", 0x8FA4BC);

    // Close button
    explorer_ql_close_btn = (rect_t){modal_x + modal_w - 28, modal_y + 8, 20, 20, 0};
    fb_rect(explorer_ql_close_btn.x, explorer_ql_close_btn.y, explorer_ql_close_btn.w, explorer_ql_close_btn.h, 0x3A4452);
    outline(explorer_ql_close_btn.x, explorer_ql_close_btn.y, explorer_ql_close_btn.w, explorer_ql_close_btn.h, 0x5C6D82);
    draw_label(explorer_ql_close_btn.x + 6, explorer_ql_close_btn.y + 4, "X", 0xE0E6ED);

    // Body area
    int body_x = modal_x + 12;
    int body_y = modal_y + tb_h + 10;
    int body_w = modal_w - 24;
    int body_h = modal_h - tb_h - 52;

    if (explorer_is_image_file(entry->name)) {
        int path_changed = 0, k = 0;
        while (entry->path[k] && cached_thumbnail_path[k] && entry->path[k] == cached_thumbnail_path[k]) k++;
        if (entry->path[k] != cached_thumbnail_path[k]) path_changed = 1;
        if (path_changed) {
            k = 0;
            while (entry->path[k] && k < 127) { cached_thumbnail_path[k] = entry->path[k]; k++; }
            cached_thumbnail_path[k] = 0;
            if (ext > 0) update_explorer_thumbnail(entry->path, &entry->name[ext]);
        }

        fb_rect(body_x, body_y, body_w, body_h, 0x14181E);
        outline(body_x, body_y, body_w, body_h, 0x2D3744);

        if (cached_thumbnail_data && cached_thumbnail_w > 0 && cached_thumbnail_h > 0) {
            int dx = body_x + (body_w - cached_thumbnail_w) / 2;
            int dy = body_y + (body_h - cached_thumbnail_h) / 2 - 10;
            for (int ty = 0; ty < cached_thumbnail_h; ty++) {
                u32 *src_row = cached_thumbnail_data + (ty * cached_thumbnail_w);
                for (int tx = 0; tx < cached_thumbnail_w; tx++) {
                    u32 c = src_row[tx];
                    fb_putpixel(dx + tx, dy + ty, c);
                }
            }
            draw_label(body_x + 14, body_y + body_h - 22, "Format: Image File", 0x7FA2C9);
        } else {
            font_icon(body_x + body_w / 2 - 10, body_y + body_h / 2 - 30, ICON_DISPLAY, 0x5C80A6);
            draw_label(body_x + body_w / 2 - 40, body_y + body_h / 2 + 10, "Image File", 0x8FA4BC);
        }
    } else if (explorer_is_video_file(entry->name)) {
        fb_rect(body_x, body_y, body_w, body_h, 0x101418);
        outline(body_x, body_y, body_w, body_h, 0x2A3542);

        for (int fx = body_x + 10; fx < body_x + body_w - 10; fx += 16) {
            fb_rect(fx, body_y + 4, 8, 4, 0x3E4E63);
            fb_rect(fx, body_y + body_h - 8, 8, 4, 0x3E4E63);
        }

        int cx = body_x + body_w / 2;
        int cy = body_y + body_h / 2 - 15;
        fb_rect(cx - 24, cy - 24, 48, 48, 0x1E2B3C);
        outline(cx - 24, cy - 24, 48, 48, 0x46658A);
        draw_label(cx - 4, cy - 6, ">", 0x55DDFF);

        draw_label(cx - 55, cy + 34, "FirstOS Media Player", 0xA0B5CC);
        draw_label(cx - 75, cy + 50, "AVI/MP4 Video (320x240 @ 24fps)", 0x6E859E);
    } else if (explorer_is_audio_file(entry->name)) {
        fb_rect(body_x, body_y, body_w, body_h, 0x121A22);
        outline(body_x, body_y, body_w, body_h, 0x293D52);

        draw_label(body_x + 20, body_y + 16, "AUDIO STREAM SPECTRUM", 0x6082A6);
        draw_label(body_x + 20, body_y + 32, "Codec: PCM WAV / Stereo 44.1kHz 16-bit", 0x8FA4BC);

        int eq_x = body_x + 24;
        int eq_y = body_y + 160;
        int num_bars = 28;
        int bar_w = (body_w - 48 - (num_bars * 3)) / num_bars;
        if (bar_w < 3) bar_w = 3;
        int is_play = hda_is_playing();

        for (int b = 0; b < num_bars; b++) {
            int bh;
            if (is_play) {
                bh = 10 + ((explorer_anim_ticks * 4 + b * 11) % 65);
                if (b % 3 == 0) bh += ((explorer_anim_ticks * 2 + b * 7) % 20);
            } else {
                bh = 12 + ((b * 17) % 35);
            }
            int bx = eq_x + b * (bar_w + 3);
            u32 bcol = is_play ? (bh > 50 ? 0xEE6655 : (bh > 30 ? 0xFFAA33 : 0x44DD88)) : 0x3D5B7A;
            fb_rect(bx, eq_y - bh, bar_w, bh, bcol);
        }

        explorer_ql_play_btn = (rect_t){body_x + 20, body_y + 185, 110, 26, 0};
        draw_button(explorer_ql_play_btn, is_play ? 0xB54A3B : 0x2E7D32, is_play ? 0xDD6454 : 0x4CAF50, is_play ? "Pause" : "Play Audio", 0xFFFFFF);
        if (is_play) {
            draw_label(body_x + 140, body_y + 191, "Playing live via Intel HDA Audio...", 0x55DD88);
        } else if (hda_is_paused()) {
            draw_label(body_x + 140, body_y + 191, "Paused", 0xDDBB55);
        } else {
            draw_label(body_x + 140, body_y + 191, "Ready to stream", 0x7FA2C9);
        }
    } else {
        text_preview_update(entry->path);
        fb_rect(body_x, body_y, body_w, body_h, 0x161B20);
        outline(body_x, body_y, body_w, body_h, 0x303B48);

        int gutter_w = 28;
        fb_rect(body_x, body_y, gutter_w, body_h, 0x121518);
        outline(body_x, body_y, gutter_w, body_h, 0x242D38);

        int line_num = 1;
        int text_i = 0;
        int ty = body_y + 8;
        while (text_preview_buf[text_i] && ty < body_y + body_h - 14 && line_num <= 16) {
            char num_str[8];
            num_str[0] = line_num < 10 ? ('0' + line_num) : ('0' + line_num / 10);
            num_str[1] = line_num < 10 ? ':' : ('0' + (line_num % 10));
            num_str[2] = line_num < 10 ? 0 : ':';
            num_str[3] = 0;
            draw_label(body_x + 4, ty, num_str, 0x56687A);

            char line_buf[64];
            int li = 0;
            while (text_preview_buf[text_i] && text_preview_buf[text_i] != '\n' && li < 60) {
                if (text_preview_buf[text_i] != '\r') {
                    line_buf[li++] = text_preview_buf[text_i];
                }
                text_i++;
            }
            if (text_preview_buf[text_i] == '\n') text_i++;
            line_buf[li] = 0;

            font_text_fit(body_x + gutter_w + 8, ty, line_buf, 0xD4DEE8, body_w - gutter_w - 14);
            ty += 14;
            line_num++;
        }
    }

    int footer_y = modal_y + modal_h - 36;
    draw_label(modal_x + 16, footer_y + 8, "Space: Play/Pause  |  Arrows: Navigate  |  Esc: Close", 0x738A9C);

    explorer_ql_open_btn = (rect_t){modal_x + modal_w - 150, footer_y + 2, 138, 24, 0};
    draw_button(explorer_ql_open_btn, 0x2A5885, 0x3F77AC, "Open in App [Enter]", 0xFFFFFF);
}
