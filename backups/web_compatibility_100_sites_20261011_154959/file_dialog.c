#include "file_dialog.h"
#include "fs.h"
#include "framebuffer.h"
#include "font.h"
#include "gui_internal.h"
#include "ide.h"
#include "sound.h"

extern app_window_t *app_slot(int kind);
extern app_window_t *active_app_window(void);
extern void focus_app_window(int index);
extern void text_edit_open_file(const char *path);
extern void ide_open_in_new_tab(const char *path);
extern void preview_open(const char *path);
extern int  media_player_open_file(const char *path);
extern int  media_player_import_file(const char *path);
extern int  point_in_rect(int px, int py, rect_t r);
extern void outline(int x, int y, int w, int h, u32 color);
extern void font_text_fit(int x, int y, const char *text, u32 color, int max_w);
extern void font_text_fit_center(int x, int y, const char *text, u32 color, int max_w);
extern int  ps2_mouse_x(void);
extern int  ps2_mouse_y(void);
extern int  needs_redraw;
extern int  editor_saved;
extern int  editor_length;
extern char editor_text[];
extern char editor_filename[];

static void fd_str_copy(char *dst, const char *src, int max) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (!src) { dst[0] = 0; return; }
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int fd_str_eq(const char *a, const char *b) {
    int i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int fd_str_len(const char *s) {
    int l = 0;
    if (!s) return 0;
    while (s[l]) l++;
    return l;
}

static int fd_str_ends_ci(const char *str, const char *suffix) {
    int sl = fd_str_len(str);
    int el = fd_str_len(suffix);
    if (sl < el) return 0;
    for (int i = 0; i < el; i++) {
        char a = str[sl - el + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

static int fd_str_contains_ci(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) return 1;
    if (!haystack) return 0;
    int hl = fd_str_len(haystack);
    int nl = fd_str_len(needle);
    for (int i = 0; i <= hl - nl; i++) {
        int match = 1;
        for (int j = 0; j < nl; j++) {
            char a = haystack[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

static void fd_format_size(u32 bytes, char *out, int max) {
    if (bytes < 1024) {
        char buf[16]; int idx = 0;
        u32 temp = bytes;
        if (temp == 0) { buf[idx++] = '0'; }
        else {
            char rev[12]; int r = 0;
            while (temp > 0) { rev[r++] = '0' + (temp % 10); temp /= 10; }
            while (r > 0) buf[idx++] = rev[--r];
        }
        buf[idx++] = ' '; buf[idx++] = 'B'; buf[idx] = 0;
        fd_str_copy(out, buf, max);
    } else if (bytes < 1024 * 1024) {
        u32 kb = bytes / 1024;
        char buf[16]; int idx = 0;
        u32 temp = kb;
        if (temp == 0) { buf[idx++] = '1'; }
        else {
            char rev[12]; int r = 0;
            while (temp > 0) { rev[r++] = '0' + (temp % 10); temp /= 10; }
            while (r > 0) buf[idx++] = rev[--r];
        }
        buf[idx++] = ' '; buf[idx++] = 'K'; buf[idx++] = 'B'; buf[idx] = 0;
        fd_str_copy(out, buf, max);
    } else {
        u32 mb = bytes / (1024 * 1024);
        char buf[16]; int idx = 0;
        char rev[12]; int r = 0;
        if (mb == 0) mb = 1;
        while (mb > 0) { rev[r++] = '0' + (mb % 10); mb /= 10; }
        while (r > 0) buf[idx++] = rev[--r];
        buf[idx++] = ' '; buf[idx++] = 'M'; buf[idx++] = 'B'; buf[idx] = 0;
        fd_str_copy(out, buf, max);
    }
}

typedef struct {
    char name[128];
    char path[256];
    int is_dir;
    u32 size;
} fd_cached_entry_t;

#define FD_MAX_ENTRIES 128
static fd_cached_entry_t fd_entries[FD_MAX_ENTRIES];
static int fd_entry_count = 0;

static int fd_open = 0;
static int fd_mode = FILE_DIALOG_OPEN;
static int fd_caller = APP_TEXT_EDIT;
static char fd_current_dir[256] = "/Desktop";
static char fd_prev_dir[256] = "/";
static char fd_filename[128] = {0};
static int  fd_filename_len = 0;
static int  fd_filename_cursor = 0;
static int  fd_filename_focused = 1;

static char fd_search[64] = {0};
static int  fd_search_len = 0;
static int  fd_search_focused = 0;

static int fd_selected_index = -1;
static int fd_scroll_offset = 0;

typedef struct {
    const char *label;
    const char *path;
    int icon;
} fd_place_t;

static const fd_place_t fd_places[] = {
    {"Desktop",    "/Desktop",    ICON_DISPLAY},
    {"Wallpapers", "/wallpapers", ICON_PHOTO},
    {"Audio",      "/audio",      ICON_FILES},
    {"Apps",       "/Apps",       ICON_APPS},
    {"System",     "/System",     ICON_SETTINGS},
    {"FirstOS HD", "/",           ICON_DISK},
};
#define FD_PLACES_COUNT 6

static int fd_file_matches_filter(const char *name, int is_dir) {
    if (is_dir) return 1;

    if (fd_search[0] && !fd_str_contains_ci(name, fd_search)) return 0;

    if (fd_mode == FILE_DIALOG_SAVE) return 1;

    if (fd_caller == APP_TEXT_EDIT) {
        if (fd_str_ends_ci(name, ".txt") || fd_str_ends_ci(name, ".doc") ||
            fd_str_ends_ci(name, ".md")  || fd_str_ends_ci(name, ".c")   ||
            fd_str_ends_ci(name, ".h")   || fd_str_ends_ci(name, ".fl")  ||
            fd_str_ends_ci(name, ".cfg") || fd_str_ends_ci(name, ".ini") ||
            fd_str_ends_ci(name, ".log") || fd_str_ends_ci(name, ".json")||
            fd_str_ends_ci(name, ".sh")  || !fd_str_contains_ci(name, ".")) return 1;
        return 0;
    } else if (fd_caller == APP_IDE) {
        if (fd_str_ends_ci(name, ".fl")  || fd_str_ends_ci(name, ".c")   ||
            fd_str_ends_ci(name, ".h")   || fd_str_ends_ci(name, ".txt") ||
            fd_str_ends_ci(name, ".md")  || fd_str_ends_ci(name, ".app") ||
            fd_str_ends_ci(name, ".cfg") || fd_str_ends_ci(name, ".py")) return 1;
        return 0;
    } else if (fd_caller == APP_MEDIA_PLAYER) {
        if (fd_str_ends_ci(name, ".wav") || fd_str_ends_ci(name, ".mp3") ||
            fd_str_ends_ci(name, ".mp4") || fd_str_ends_ci(name, ".avi") ||
            fd_str_ends_ci(name, ".mov") || fd_str_ends_ci(name, ".mid")) return 1;
        return 0;
    } else if (fd_caller == APP_PREVIEW) {
        if (fd_str_ends_ci(name, ".png") || fd_str_ends_ci(name, ".jpg") ||
            fd_str_ends_ci(name, ".jpeg")|| fd_str_ends_ci(name, ".bmp") ||
            fd_str_ends_ci(name, ".gif") || fd_str_ends_ci(name, ".ico") ||
            fd_str_ends_ci(name, ".ppm")) return 1;
        return 0;
    } else if (fd_caller == APP_SETTINGS) {
        if (fd_str_ends_ci(name, ".png") || fd_str_ends_ci(name, ".jpg") ||
            fd_str_ends_ci(name, ".jpeg")) return 1;
        return 0;
    }

    return 1;
}

static const char *fd_get_type_description(const char *name, int is_dir) {
    if (is_dir) return "Folder";
    if (fd_str_ends_ci(name, ".txt")) return "Text Document";
    if (fd_str_ends_ci(name, ".doc")) return "Word Document";
    if (fd_str_ends_ci(name, ".md"))  return "Markdown File";
    if (fd_str_ends_ci(name, ".c"))   return "C Source Code";
    if (fd_str_ends_ci(name, ".h"))   return "C Header";
    if (fd_str_ends_ci(name, ".fl"))  return "FirstLang Script";
    if (fd_str_ends_ci(name, ".wav")) return "WAV Audio";
    if (fd_str_ends_ci(name, ".mp3")) return "MP3 Audio";
    if (fd_str_ends_ci(name, ".mp4")) return "MP4 Video";
    if (fd_str_ends_ci(name, ".avi")) return "AVI Video";
    if (fd_str_ends_ci(name, ".png")) return "PNG Image";
    if (fd_str_ends_ci(name, ".jpg") || fd_str_ends_ci(name, ".jpeg")) return "JPEG Image";
    if (fd_str_ends_ci(name, ".bmp")) return "Bitmap Image";
    if (fd_str_ends_ci(name, ".cfg") || fd_str_ends_ci(name, ".ini")) return "Config File";
    if (fd_str_ends_ci(name, ".app")) return "Application";
    return "Document";
}

static int fd_get_icon_for_file(const char *name, int is_dir) {
    if (is_dir) return ICON_FOLDER;
    if (fd_str_ends_ci(name, ".png") || fd_str_ends_ci(name, ".jpg") ||
        fd_str_ends_ci(name, ".bmp") || fd_str_ends_ci(name, ".jpeg")) return ICON_PHOTO;
    if (fd_str_ends_ci(name, ".wav") || fd_str_ends_ci(name, ".mp3") ||
        fd_str_ends_ci(name, ".mp4") || fd_str_ends_ci(name, ".avi")) return ICON_FILES;
    if (fd_str_ends_ci(name, ".app")) return ICON_APPS;
    return ICON_FILE;
}

static void file_dialog_refresh(void) {
    fd_entry_count = 0;
    fd_selected_index = -1;
    fd_scroll_offset = 0;

    // Pass 1: Directories
    for (int i = 0; i < 256 && fd_entry_count < FD_MAX_ENTRIES; i++) {
        const fs_entry_t *e = fs_entry_in_dir(fd_current_dir, i);
        if (!e) break;
        if (e->is_dir) {
            if (fd_search[0] && !fd_str_contains_ci(e->name, fd_search)) continue;
            fd_str_copy(fd_entries[fd_entry_count].name, e->name, 128);
            fd_str_copy(fd_entries[fd_entry_count].path, e->path, 256);
            fd_entries[fd_entry_count].is_dir = 1;
            fd_entries[fd_entry_count].size = 0;
            fd_entry_count++;
        }
    }

    // Pass 2: Files
    for (int i = 0; i < 256 && fd_entry_count < FD_MAX_ENTRIES; i++) {
        const fs_entry_t *e = fs_entry_in_dir(fd_current_dir, i);
        if (!e) break;
        if (!e->is_dir) {
            if (!fd_file_matches_filter(e->name, 0)) continue;
            fd_str_copy(fd_entries[fd_entry_count].name, e->name, 128);
            fd_str_copy(fd_entries[fd_entry_count].path, e->path, 256);
            fd_entries[fd_entry_count].is_dir = 0;
            fd_entries[fd_entry_count].size = e->size;
            fd_entry_count++;
        }
    }
}

static void file_dialog_commit(void) {
    if (fd_filename[0] == 0 && fd_selected_index >= 0 && !fd_entries[fd_selected_index].is_dir) {
        fd_str_copy(fd_filename, fd_entries[fd_selected_index].name, 128);
        fd_filename_len = fd_str_len(fd_filename);
    }

    // If selected is a directory and clicked action button, navigate into that directory!
    if (fd_selected_index >= 0 && fd_entries[fd_selected_index].is_dir) {
        const char *dirname = fd_entries[fd_selected_index].name;
        char new_path[256];
        int p = 0;
        if (fd_str_eq(fd_current_dir, "/")) {
            new_path[p++] = '/';
            int j = 0; while (dirname[j] && p < 254) new_path[p++] = dirname[j++];
            new_path[p] = 0;
        } else {
            int j = 0; while (fd_current_dir[j] && p < 254) new_path[p++] = fd_current_dir[j++];
            if (p > 0 && new_path[p-1] != '/' && p < 254) new_path[p++] = '/';
            j = 0; while (dirname[j] && p < 254) new_path[p++] = dirname[j++];
            new_path[p] = 0;
        }
        fd_str_copy(fd_prev_dir, fd_current_dir, 256);
        fd_str_copy(fd_current_dir, new_path, 256);
        fd_selected_index = -1;
        fd_scroll_offset = 0;
        file_dialog_refresh();
        return;
    }

    if (fd_filename[0] == 0) return;

    char full_path[256];
    int p = 0;
    if (fd_str_eq(fd_current_dir, "/")) {
        full_path[p++] = '/';
        int j = 0; while (fd_filename[j] && p < 254) full_path[p++] = fd_filename[j++];
        full_path[p] = 0;
    } else {
        int j = 0; while (fd_current_dir[j] && p < 254) full_path[p++] = fd_current_dir[j++];
        if (p > 0 && full_path[p-1] != '/' && p < 254) full_path[p++] = '/';
        int j2 = 0; while (fd_filename[j2] && p < 254) full_path[p++] = fd_filename[j2++];
        full_path[p] = 0;
    }

    if (fd_mode == FILE_DIALOG_OPEN) {
        if (fd_caller == APP_TEXT_EDIT) {
            text_edit_open_file(full_path);
            app_window_t *app = app_slot(APP_TEXT_EDIT);
            if (app) { app->open = 1; app->minimized = 0; }
            focus_app_window(APP_TEXT_EDIT - 1);
        } else if (fd_caller == APP_IDE) {
            ide_open_in_new_tab(full_path);
            app_window_t *app = app_slot(APP_IDE);
            if (app) { app->open = 1; app->minimized = 0; }
            focus_app_window(APP_IDE - 1);
        } else if (fd_caller == APP_PREVIEW) {
            preview_open(full_path);
            app_window_t *app = app_slot(APP_PREVIEW);
            if (app) { app->open = 1; app->minimized = 0; }
            focus_app_window(APP_PREVIEW - 1);
        } else if (fd_caller == APP_MEDIA_PLAYER) {
            media_player_open_file(full_path);
            app_window_t *app = app_slot(APP_MEDIA_PLAYER);
            if (app) { app->open = 1; app->minimized = 0; }
            focus_app_window(APP_MEDIA_PLAYER - 1);
        } else if (fd_caller == APP_SETTINGS) {
            extern void settings_handle_avatar_selected(const char *path);
            settings_handle_avatar_selected(full_path);
            app_window_t *app = app_slot(APP_SETTINGS);
            if (app) { app->open = 1; app->minimized = 0; }
            focus_app_window(APP_SETTINGS - 1);
        }
    } else if (fd_mode == FILE_DIALOG_IMPORT) {
        if (fd_caller == APP_MEDIA_PLAYER) {
            media_player_import_file(full_path);
            app_window_t *app = app_slot(APP_MEDIA_PLAYER);
            if (app) { app->open = 1; app->minimized = 0; }
            focus_app_window(APP_MEDIA_PLAYER - 1);
        }
    } else if (fd_mode == FILE_DIALOG_SAVE) {
        int has_dot = 0;
        for (int k = 0; full_path[k]; k++) {
            if (full_path[k] == '.') has_dot = 1;
            if (full_path[k] == '/') has_dot = 0;
        }
        if (!has_dot) {
            if (fd_caller == APP_TEXT_EDIT) {
                int fl = 0; while (full_path[fl]) fl++;
                if (fl + 4 < 255) {
                    full_path[fl++] = '.'; full_path[fl++] = 't'; full_path[fl++] = 'x'; full_path[fl++] = 't'; full_path[fl] = 0;
                }
            } else if (fd_caller == APP_IDE) {
                int fl = 0; while (full_path[fl]) fl++;
                if (fl + 3 < 255) {
                    full_path[fl++] = '.'; full_path[fl++] = 'f'; full_path[fl++] = 'l'; full_path[fl] = 0;
                }
            }
        }

        fs_create_at(full_path, 0);

        if (fd_caller == APP_TEXT_EDIT) {
            fs_write_at(full_path, editor_text, editor_length);
            fs_sync();
            editor_saved = 1;
            int fi = 0; while (full_path[fi] && fi < 255) { editor_filename[fi] = full_path[fi]; fi++; }
            editor_filename[fi] = 0;
            app_window_t *app = app_slot(APP_TEXT_EDIT);
            if (app) { app->open = 1; app->minimized = 0; }
            focus_app_window(APP_TEXT_EDIT - 1);
        } else if (fd_caller == APP_IDE) {
            fs_write_at(full_path, ide_text, ide_length);
            fs_sync();
            ide_saved = 1;
            int fi = 0; while (full_path[fi] && fi < 31) { ide_tabs[ide_active_tab].filename[fi] = full_path[fi]; fi++; }
            ide_tabs[ide_active_tab].filename[fi] = 0;
            app_window_t *app = app_slot(APP_IDE);
            if (app) { app->open = 1; app->minimized = 0; }
            focus_app_window(APP_IDE - 1);
        }
    }

    file_dialog_close();
    needs_redraw = 1;
}

void file_dialog_init(void) {
    fd_open = 0;
    fd_selected_index = -1;
    fd_scroll_offset = 0;
    fd_str_copy(fd_current_dir, "/Desktop", 256);
    fd_str_copy(fd_prev_dir, "/", 256);
}

void file_dialog_show(int caller_kind, int mode, const char *default_name, const char *initial_path) {
    fd_open = 1;
    fd_caller = caller_kind;
    fd_mode = mode;
    fd_selected_index = -1;
    fd_search[0] = 0;
    fd_search_len = 0;
    fd_search_focused = 0;
    fd_filename_focused = (mode == FILE_DIALOG_SAVE) ? 1 : 0;

    if (mode == FILE_DIALOG_SAVE) {
        sound_play_event(SOUND_EVENT_DIALOG_POPUP);
    }

    if (default_name && default_name[0]) {
        fd_str_copy(fd_filename, default_name, 128);
        fd_filename_len = fd_str_len(fd_filename);
        fd_filename_cursor = fd_filename_len;
    } else {
        fd_filename[0] = 0;
        fd_filename_len = 0;
        fd_filename_cursor = 0;
    }

    if (initial_path && initial_path[0]) {
        fd_str_copy(fd_current_dir, initial_path, 256);
    } else {
        if (mode == FILE_DIALOG_IMPORT || caller_kind == APP_MEDIA_PLAYER) {
            fd_str_copy(fd_current_dir, "/audio", 256);
        } else if (caller_kind == APP_PREVIEW || caller_kind == APP_SETTINGS) {
            fd_str_copy(fd_current_dir, "/wallpapers", 256);
        } else {
            fd_str_copy(fd_current_dir, "/Desktop", 256);
        }
    }
    fd_str_copy(fd_prev_dir, "/", 256);

    file_dialog_refresh();
}

void file_dialog_close(void) {
    fd_open = 0;
    fd_selected_index = -1;
    fd_filename[0] = 0;
    fd_filename_len = 0;
    fd_search[0] = 0;
    fd_search_len = 0;
    fd_filename_focused = 0;
    fd_search_focused = 0;

    if (fd_caller > 0) {
        focus_app_window(fd_caller - 1);
    }
}

int file_dialog_is_open(void) {
    return fd_open;
}

void file_dialog_draw(void) {
    if (!fd_open) return;

    int sw = (int)fb.width;
    int sh = (int)fb.height;
    int dw = 640;
    int dh = 450;
    int dx = (sw - dw) / 2;
    int dy = (sh - dh) / 2;
    int mx = ps2_mouse_x();
    int my = ps2_mouse_y();

    // 1. Semi-transparent backdrop over entire screen
    fb_rect_alpha(0, 0, sw, sh, 0x07111E, 155);

    // 2. Window drop shadows (modern transparent black multi-layer feathered shadow)
    fb_draw_window_shadow(dx, dy, dw, dh, 10, 1);

    // 3. Dialog Window Card
    fb_round_rect(dx, dy, dw, dh, 10, 0xFFFFFF);
    fb_round_rect_border(dx, dy, dw, dh, 10, 0x1E2E42);

    // 4. Sleek Titlebar (height 32, rounded at top)
    fb_round_rect(dx, dy, dw, 32, 10, 0x1A2838);
    fb_rect(dx, dy + 10, dw, 22, 0x1A2838);
    fb_rect(dx, dy + 31, dw, 1, 0x2E435B);

    // Traffic light dots
    int btn_close_hov = (mx >= dx + 10 && mx <= dx + 24 && my >= dy + 9 && my <= dy + 23);
    fb_round_rect(dx + 12, dy + 10, 12, 12, 6, btn_close_hov ? 0xEF4444 : 0xDC2626);
    fb_round_rect_border(dx + 12, dy + 10, 12, 12, 6, 0x991B1B);
    if (btn_close_hov) font_text(dx + 15, dy + 11, "x", 0xFFFFFF, 1);

    fb_round_rect(dx + 30, dy + 10, 12, 12, 6, 0xF59E0B);
    fb_round_rect_border(dx + 30, dy + 10, 12, 12, 6, 0xB45309);

    fb_round_rect(dx + 48, dy + 10, 12, 12, 6, 0x10B981);
    fb_round_rect_border(dx + 48, dy + 10, 12, 12, 6, 0x047857);

    // Title text
    const char *title_text = "Open Document";
    if (fd_mode == FILE_DIALOG_SAVE) title_text = "Save Document As";
    else if (fd_mode == FILE_DIALOG_IMPORT) title_text = "Import Media File";
    font_text(dx + 72, dy + 10, title_text, 0xFFFFFF, 1);

    // Right mode badge
    rect_t badge = {dx + dw - 92, dy + 7, 80, 18, 0};
    u32 badge_bg = (fd_mode == FILE_DIALOG_SAVE) ? 0x059669 : ((fd_mode == FILE_DIALOG_IMPORT) ? 0x7C3AED : 0x2563EB);
    fb_rect(badge.x, badge.y, badge.w, badge.h, badge_bg);
    outline(badge.x, badge.y, badge.w, badge.h, 0xFFFFFF);
    const char *badge_str = (fd_mode == FILE_DIALOG_SAVE) ? "SAVE MODE" : ((fd_mode == FILE_DIALOG_IMPORT) ? "IMPORT" : "OPEN MODE");
    font_text_fit_center(badge.x, badge.y + 4, badge_str, 0xFFFFFF, badge.w);

    // 5. Navigation Bar (dy + 32, height 36)
    int nav_y = dy + 32;
    fb_rect(dx, nav_y, dw, 36, 0xF1F5F9);
    fb_rect(dx, nav_y + 35, dw, 1, 0xCBD5E1);

    // Nav Back [<]
    int hov_back = (mx >= dx + 10 && mx <= dx + 38 && my >= nav_y + 6 && my <= nav_y + 30);
    fb_round_rect(dx + 10, nav_y + 6, 28, 24, 4, hov_back ? 0xE2E8F0 : 0xFFFFFF);
    fb_round_rect_border(dx + 10, nav_y + 6, 28, 24, 4, 0x94A3B8);
    font_text_fit_center(dx + 10, nav_y + 11, "<", 0x334155, 28);

    // Nav Up [^]
    int hov_up = (mx >= dx + 44 && mx <= dx + 72 && my >= nav_y + 6 && my <= nav_y + 30);
    fb_round_rect(dx + 44, nav_y + 6, 28, 24, 4, hov_up ? 0xE2E8F0 : 0xFFFFFF);
    fb_round_rect_border(dx + 44, nav_y + 6, 28, 24, 4, 0x94A3B8);
    font_text_fit_center(dx + 44, nav_y + 11, "^", 0x334155, 28);

    // Location Breadcrumb Pill
    rect_t path_pill = {dx + 80, nav_y + 6, 360, 24, 0};
    fb_round_rect(path_pill.x, path_pill.y, path_pill.w, path_pill.h, 6, 0xFFFFFF);
    fb_round_rect_border(path_pill.x, path_pill.y, path_pill.w, path_pill.h, 6, 0xCBD5E1);
    font_icon(path_pill.x + 8, path_pill.y + 4, ICON_FOLDER, 0x2563EB);
    char breadcrumb[128];
    breadcrumb[0] = 0;
    if (fd_str_eq(fd_current_dir, "/")) {
        fd_str_copy(breadcrumb, "FirstOS (Root /)", 128);
    } else {
        fd_str_copy(breadcrumb, "FirstOS > ", 128);
        int bl = fd_str_len(breadcrumb);
        const char *cd = (fd_current_dir[0] == '/') ? fd_current_dir + 1 : fd_current_dir;
        int ci = 0;
        while (cd[ci] && bl < 126) {
            if (cd[ci] == '/') { breadcrumb[bl++] = ' '; breadcrumb[bl++] = '>'; breadcrumb[bl++] = ' '; }
            else breadcrumb[bl++] = cd[ci];
            ci++;
        }
        breadcrumb[bl] = 0;
    }
    font_text_fit(path_pill.x + 28, path_pill.y + 6, breadcrumb, 0x1E293B, path_pill.w - 36);

    // Search Input Box (width 170)
    rect_t search_box = {dx + dw - 180, nav_y + 6, 170, 24, 0};
    fb_round_rect(search_box.x, search_box.y, search_box.w, search_box.h, 6, 0xFFFFFF);
    fb_round_rect_border(search_box.x, search_box.y, search_box.w, search_box.h, 6, fd_search_focused ? 0x2563EB : 0xCBD5E1);
    font_text(search_box.x + 6, search_box.y + 6, "Q:", 0x64748B, 1);
    if (fd_search[0]) {
        font_text_fit(search_box.x + 24, search_box.y + 6, fd_search, 0x0F172A, search_box.w - 30);
    } else {
        font_text(search_box.x + 24, search_box.y + 6, "Search...", 0x94A3B8, 1);
    }

    // 6. Left Sidebar (Places & Volumes) (width 148, y = dy + 68, height 312)
    int body_y = dy + 68;
    int body_h = 312;
    fb_rect(dx, body_y, 148, body_h, 0xF8FAFC);
    fb_rect(dx + 148, body_y, 1, body_h, 0xCBD5E1);

    // Places header
    font_text(dx + 14, body_y + 10, "PLACES", 0x64748B, 1);

    for (int p = 0; p < 5; p++) {
        int item_y = body_y + 28 + p * 24;
        rect_t place_r = {dx + 8, item_y, 132, 22, 0};
        int is_sel = fd_str_eq(fd_current_dir, fd_places[p].path);
        int is_hov = point_in_rect(mx, my, place_r);

        if (is_sel) {
            fb_rect(place_r.x, place_r.y, place_r.w, place_r.h, 0xDBEAFE);
            outline(place_r.x, place_r.y, place_r.w, place_r.h, 0x93C5FD);
        } else if (is_hov) {
            fb_rect(place_r.x, place_r.y, place_r.w, place_r.h, 0xEEF2F6);
        }
        font_icon(place_r.x + 6, place_r.y + 3, fd_places[p].icon, is_sel ? 0x1D4ED8 : 0x475569);
        font_text(place_r.x + 26, place_r.y + 5, fd_places[p].label, is_sel ? 0x1D4ED8 : 0x1E293B, 1);
    }

    // Volumes header
    font_text(dx + 14, body_y + 160, "VOLUMES", 0x64748B, 1);
    {
        int item_y = body_y + 178;
        rect_t place_r = {dx + 8, item_y, 132, 22, 0};
        int is_sel = fd_str_eq(fd_current_dir, "/");
        int is_hov = point_in_rect(mx, my, place_r);
        if (is_sel) {
            fb_rect(place_r.x, place_r.y, place_r.w, place_r.h, 0xDBEAFE);
            outline(place_r.x, place_r.y, place_r.w, place_r.h, 0x93C5FD);
        } else if (is_hov) {
            fb_rect(place_r.x, place_r.y, place_r.w, place_r.h, 0xEEF2F6);
        }
        font_icon(place_r.x + 6, place_r.y + 3, ICON_DISK, is_sel ? 0x1D4ED8 : 0x475569);
        font_text(place_r.x + 26, place_r.y + 5, "FirstOS HD", is_sel ? 0x1D4ED8 : 0x1E293B, 1);
    }

    // 7. Right File Viewport (x = dx + 149, w = 491, h = 312)
    int vp_x = dx + 149;
    int vp_w = dw - 149;
    fb_rect(vp_x, body_y, vp_w, body_h, 0xFFFFFF);

    // Column Headers (height 22)
    fb_rect(vp_x, body_y, vp_w, 22, 0xF1F5F9);
    fb_rect(vp_x, body_y + 21, vp_w, 1, 0xE2E8F0);
    font_text(vp_x + 36, body_y + 6, "Name", 0x475569, 1);
    font_text(vp_x + 280, body_y + 6, "Size", 0x475569, 1);
    font_text(vp_x + 370, body_y + 6, "Type", 0x475569, 1);

    // Rows
    int rows_y = body_y + 22;
    int max_visible_rows = 11;
    if (fd_entry_count == 0) {
        font_text_fit_center(vp_x, rows_y + 60, "This folder is empty.", 0x94A3B8, vp_w);
    } else {
        for (int r = 0; r < max_visible_rows; r++) {
            int idx = fd_scroll_offset + r;
            if (idx >= fd_entry_count) break;

            int row_y = rows_y + r * 26;
            rect_t row_r = {vp_x + 4, row_y + 1, vp_w - 18, 24, 0};
            int is_sel = (fd_selected_index == idx);
            int is_hov = point_in_rect(mx, my, row_r);

            if (is_sel) {
                fb_rect(row_r.x, row_r.y, row_r.w, row_r.h, 0xDBEAFE);
                outline(row_r.x, row_r.y, row_r.w, row_r.h, 0x93C5FD);
            } else if (is_hov) {
                fb_rect(row_r.x, row_r.y, row_r.w, row_r.h, 0xF8FAFC);
            }

            int icon = fd_get_icon_for_file(fd_entries[idx].name, fd_entries[idx].is_dir);
            u32 icon_col = fd_entries[idx].is_dir ? 0x2563EB : (is_sel ? 0x1D4ED8 : 0x64748B);
            font_icon(vp_x + 12, row_y + 4, icon, icon_col);

            font_text_fit(vp_x + 36, row_y + 6, fd_entries[idx].name, is_sel ? 0x1D4ED8 : 0x0F172A, 235);

            char size_str[16];
            if (fd_entries[idx].is_dir) {
                fd_str_copy(size_str, "--", 16);
            } else {
                fd_format_size(fd_entries[idx].size, size_str, 16);
            }
            font_text(vp_x + 280, row_y + 6, size_str, 0x64748B, 1);

            const char *type_desc = fd_get_type_description(fd_entries[idx].name, fd_entries[idx].is_dir);
            font_text_fit(vp_x + 370, row_y + 6, type_desc, 0x64748B, 100);
        }

        // Scrollbar if needed
        if (fd_entry_count > max_visible_rows) {
            int sb_x = dx + dw - 10;
            int sb_y = rows_y;
            int sb_h = 286;
            fb_rect(sb_x, sb_y, 6, sb_h, 0xF1F5F9);
            int thumb_h = (max_visible_rows * sb_h) / fd_entry_count;
            if (thumb_h < 20) thumb_h = 20;
            int max_scroll = fd_entry_count - max_visible_rows;
            int thumb_y = sb_y + (fd_scroll_offset * (sb_h - thumb_h)) / (max_scroll > 0 ? max_scroll : 1);
            fb_rect(sb_x, thumb_y, 6, thumb_h, 0x94A3B8);
        }
    }

    // 8. Bottom Action Bar (y = dy + 380, height 70)
    int bot_y = dy + 380;
    fb_rect(dx, bot_y, dw, 70, 0xF8FAFC);
    fb_rect(dx, bot_y, dw, 1, 0xCBD5E1);

    // Row 1: File name input & Format badge
    font_text(dx + 16, bot_y + 12, "File name:", 0x334155, 1);

    rect_t input_box = {dx + 90, bot_y + 7, 340, 24, 0};
    fb_round_rect(input_box.x, input_box.y, input_box.w, input_box.h, 6, 0xFFFFFF);
    fb_round_rect_border(input_box.x, input_box.y, input_box.w, input_box.h, 6, fd_filename_focused ? 0x2563EB : 0x94A3B8);
    if (fd_filename[0]) {
        font_text_fit(input_box.x + 8, input_box.y + 6, fd_filename, 0x0F172A, input_box.w - 16);
    } else {
        font_text(input_box.x + 8, input_box.y + 6, (fd_mode == FILE_DIALOG_SAVE) ? "Enter file name..." : "Choose a file...", 0x94A3B8, 1);
    }
    if (fd_filename_focused) {
        int cur_x = input_box.x + 8 + fd_filename_cursor * 7;
        if (cur_x < input_box.x + input_box.w - 8) {
            fb_rect(cur_x, input_box.y + 4, 2, 16, 0x2563EB);
        }
    }

    // Format dropdown pill
    rect_t fmt_pill = {dx + 440, bot_y + 7, 186, 24, 0};
    fb_round_rect(fmt_pill.x, fmt_pill.y, fmt_pill.w, fmt_pill.h, 6, 0xEEF2F6);
    fb_round_rect_border(fmt_pill.x, fmt_pill.y, fmt_pill.w, fmt_pill.h, 6, 0xCBD5E1);
    const char *fmt_str = "All Files (*.*)";
    if (fd_caller == APP_TEXT_EDIT) fmt_str = "Text Documents (*.txt)";
    else if (fd_caller == APP_IDE) fmt_str = "FirstLang Scripts (*.fl)";
    else if (fd_caller == APP_MEDIA_PLAYER) fmt_str = "Audio / Video (*.wav, *.mp4)";
    else if (fd_caller == APP_PREVIEW) fmt_str = "Images (*.png, *.jpg)";
    else if (fd_caller == APP_SETTINGS) fmt_str = "Images (*.jpg, *.png)";
    font_text_fit(fmt_pill.x + 8, fmt_pill.y + 6, fmt_str, 0x475569, fmt_pill.w - 20);

    // Row 2: Cancel & Action Buttons
    rect_t btn_cancel = {dx + dw - 190, bot_y + 38, 85, 26, 0};
    int hov_cancel = point_in_rect(mx, my, btn_cancel);
    fb_round_rect(btn_cancel.x, btn_cancel.y, btn_cancel.w, btn_cancel.h, 6, hov_cancel ? 0xE2E8F0 : 0xFFFFFF);
    fb_round_rect_border(btn_cancel.x, btn_cancel.y, btn_cancel.w, btn_cancel.h, 6, 0x94A3B8);
    font_text_fit_center(btn_cancel.x, btn_cancel.y + 7, "Cancel", 0x1E293B, btn_cancel.w);

    rect_t btn_action = {dx + dw - 95, bot_y + 38, 85, 26, 0};
    int hov_action = point_in_rect(mx, my, btn_action);
    const char *action_lbl = (fd_mode == FILE_DIALOG_SAVE) ? "Save" : ((fd_mode == FILE_DIALOG_IMPORT) ? "Import" : "Open");
    fb_round_rect(btn_action.x, btn_action.y, btn_action.w, btn_action.h, 6, hov_action ? 0x1D4ED8 : 0x2563EB);
    fb_round_rect_border(btn_action.x, btn_action.y, btn_action.w, btn_action.h, 6, 0x1E40AF);
    font_text_fit_center(btn_action.x, btn_action.y + 7, action_lbl, 0xFFFFFF, btn_action.w);
}

int file_dialog_handle_click(int mx, int my) {
    if (!fd_open) return 0;

    int sw = (int)fb.width;
    int sh = (int)fb.height;
    int dw = 640;
    int dh = 450;
    int dx = (sw - dw) / 2;
    int dy = (sh - dh) / 2;

    // Check if clicked outside the dialog card (on the dimmed backdrop)
    if (mx < dx || mx >= dx + dw || my < dy || my >= dy + dh) {
        return 1; // Block click from reaching background apps!
    }

    // Close button [x]
    if (mx >= dx + 10 && mx <= dx + 24 && my >= dy + 9 && my <= dy + 23) {
        file_dialog_close();
        return 1;
    }

    // Navigation Bar
    int nav_y = dy + 32;
    // Back [<]
    if (mx >= dx + 10 && mx <= dx + 38 && my >= nav_y + 6 && my <= nav_y + 30) {
        char temp[256];
        fd_str_copy(temp, fd_current_dir, 256);
        fd_str_copy(fd_current_dir, fd_prev_dir, 256);
        fd_str_copy(fd_prev_dir, temp, 256);
        file_dialog_refresh();
        return 1;
    }
    // Up [^]
    if (mx >= dx + 44 && mx <= dx + 72 && my >= nav_y + 6 && my <= nav_y + 30) {
        if (!fd_str_eq(fd_current_dir, "/")) {
            fd_str_copy(fd_prev_dir, fd_current_dir, 256);
            int last_slash = -1;
            for (int i = 0; fd_current_dir[i]; i++) {
                if (fd_current_dir[i] == '/') last_slash = i;
            }
            if (last_slash <= 0) {
                fd_str_copy(fd_current_dir, "/", 256);
            } else {
                fd_current_dir[last_slash] = 0;
            }
            file_dialog_refresh();
        }
        return 1;
    }
    // Search Box
    rect_t search_box = {dx + dw - 180, nav_y + 6, 170, 24, 0};
    if (point_in_rect(mx, my, search_box)) {
        fd_search_focused = 1;
        fd_filename_focused = 0;
        return 1;
    }

    // Sidebar Items
    int body_y = dy + 68;
    for (int p = 0; p < 5; p++) {
        rect_t place_r = {dx + 8, body_y + 28 + p * 24, 132, 22, 0};
        if (point_in_rect(mx, my, place_r)) {
            fd_str_copy(fd_prev_dir, fd_current_dir, 256);
            fd_str_copy(fd_current_dir, fd_places[p].path, 256);
            fd_selected_index = -1;
            fd_filename_focused = (fd_mode == FILE_DIALOG_SAVE);
            fd_search_focused = 0;
            file_dialog_refresh();
            return 1;
        }
    }
    // Volume: FirstOS HD
    rect_t vol_r = {dx + 8, body_y + 178, 132, 22, 0};
    if (point_in_rect(mx, my, vol_r)) {
        fd_str_copy(fd_prev_dir, fd_current_dir, 256);
        fd_str_copy(fd_current_dir, "/", 256);
        fd_selected_index = -1;
        fd_filename_focused = (fd_mode == FILE_DIALOG_SAVE);
        fd_search_focused = 0;
        file_dialog_refresh();
        return 1;
    }

    // Viewport Rows
    int vp_x = dx + 149;
    int vp_w = dw - 149;
    int rows_y = body_y + 22;
    int max_visible_rows = 11;
    if (mx >= vp_x && mx < vp_x + vp_w && my >= rows_y && my < rows_y + max_visible_rows * 26) {
        int clicked_row = (my - rows_y) / 26;
        int idx = fd_scroll_offset + clicked_row;
        if (idx >= 0 && idx < fd_entry_count) {
            fd_search_focused = 0;
            if (fd_selected_index == idx) {
                if (fd_entries[idx].is_dir) {
                    fd_str_copy(fd_prev_dir, fd_current_dir, 256);
                    char new_path[256];
                    int p = 0;
                    if (fd_str_eq(fd_current_dir, "/")) {
                        new_path[p++] = '/';
                        int j = 0; while (fd_entries[idx].name[j] && p < 254) new_path[p++] = fd_entries[idx].name[j++];
                        new_path[p] = 0;
                    } else {
                        int j = 0; while (fd_current_dir[j] && p < 254) new_path[p++] = fd_current_dir[j++];
                        if (p > 0 && new_path[p-1] != '/' && p < 254) new_path[p++] = '/';
                        j = 0; while (fd_entries[idx].name[j] && p < 254) new_path[p++] = fd_entries[idx].name[j++];
                        new_path[p] = 0;
                    }
                    fd_str_copy(fd_current_dir, new_path, 256);
                    fd_selected_index = -1;
                    file_dialog_refresh();
                    return 1;
                } else {
                    fd_str_copy(fd_filename, fd_entries[idx].name, 128);
                    fd_filename_len = fd_str_len(fd_filename);
                    fd_filename_cursor = fd_filename_len;
                    file_dialog_commit();
                    return 1;
                }
            } else {
                fd_selected_index = idx;
                if (!fd_entries[idx].is_dir) {
                    fd_str_copy(fd_filename, fd_entries[idx].name, 128);
                    fd_filename_len = fd_str_len(fd_filename);
                    fd_filename_cursor = fd_filename_len;
                }
                return 1;
            }
        }
    }

    // Bottom Action Bar
    int bot_y = dy + 380;
    rect_t input_box = {dx + 90, bot_y + 7, 340, 24, 0};
    if (point_in_rect(mx, my, input_box)) {
        fd_filename_focused = 1;
        fd_search_focused = 0;
        return 1;
    }

    rect_t btn_cancel = {dx + dw - 190, bot_y + 38, 85, 26, 0};
    if (point_in_rect(mx, my, btn_cancel)) {
        file_dialog_close();
        return 1;
    }

    rect_t btn_action = {dx + dw - 95, bot_y + 38, 85, 26, 0};
    if (point_in_rect(mx, my, btn_action)) {
        file_dialog_commit();
        return 1;
    }

    return 1;
}

int file_dialog_handle_scroll(int delta) {
    if (!fd_open) return 0;
    int max_scroll = fd_entry_count - 11;
    if (max_scroll <= 0) return 1;
    fd_scroll_offset += delta;
    if (fd_scroll_offset < 0) fd_scroll_offset = 0;
    if (fd_scroll_offset > max_scroll) fd_scroll_offset = max_scroll;
    return 1;
}

int file_dialog_handle_drag(int mx, int my) {
    (void)mx; (void)my;
    if (!fd_open) return 0;
    return 1; // Consume dragging so background apps cannot be dragged
}

int file_dialog_handle_key(unsigned char key, char ascii) {
    if (!fd_open) return 0;

    // Esc closes/cancels dialog
    if (key == 0x01) {
        file_dialog_close();
        return 1;
    }

    // Enter confirms / commits
    if (key == 0x1C) {
        if (fd_search_focused) {
            fd_search_focused = 0;
            fd_filename_focused = 1;
            return 1;
        }
        file_dialog_commit();
        return 1;
    }

    // Backspace
    if (key == 0x0E) {
        if (fd_search_focused) {
            if (fd_search_len > 0) {
                fd_search[--fd_search_len] = 0;
                file_dialog_refresh();
            }
        } else {
            if (fd_filename_len > 0) {
                fd_filename[--fd_filename_len] = 0;
                fd_filename_cursor = fd_filename_len;
            }
        }
        return 1;
    }

    // Arrow keys
    if (key == 0x48) { // Up
        if (fd_selected_index > 0) {
            fd_selected_index--;
            if (fd_selected_index < fd_scroll_offset) {
                fd_scroll_offset = fd_selected_index;
            }
            if (!fd_entries[fd_selected_index].is_dir) {
                fd_str_copy(fd_filename, fd_entries[fd_selected_index].name, 128);
                fd_filename_len = fd_str_len(fd_filename);
                fd_filename_cursor = fd_filename_len;
            }
        }
        return 1;
    }
    if (key == 0x50) { // Down
        if (fd_selected_index + 1 < fd_entry_count) {
            fd_selected_index++;
            if (fd_selected_index >= fd_scroll_offset + 11) {
                fd_scroll_offset = fd_selected_index - 10;
            }
            if (!fd_entries[fd_selected_index].is_dir) {
                fd_str_copy(fd_filename, fd_entries[fd_selected_index].name, 128);
                fd_filename_len = fd_str_len(fd_filename);
                fd_filename_cursor = fd_filename_len;
            }
        }
        return 1;
    }

    // Normal typing
    if (ascii >= 32 && ascii <= 126) {
        if (fd_search_focused) {
            if (fd_search_len < 60) {
                fd_search[fd_search_len++] = ascii;
                fd_search[fd_search_len] = 0;
                file_dialog_refresh();
            }
        } else {
            if (fd_filename_len < 120) {
                fd_filename[fd_filename_len++] = ascii;
                fd_filename[fd_filename_len] = 0;
                fd_filename_cursor = fd_filename_len;
            }
        }
        return 1;
    }

    return 1;
}
