#include "spotlight.h"
#include "fs.h"
#include "hda.h"
#include "font.h"
#include "gui_internal.h"
#include "serial.h"

// Basic string functions
int sl_strlen(const char *s) { int i=0; while(s && s[i]) i++; return i; }
static void sl_strcpy(char *d, const char *s) { if (!d) return; if (!s) { *d = 0; return; } while(*s) *d++ = *s++; *d = 0; }
int sl_strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    while(*s1 && *s1 == *s2) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}
static void sl_strtolower(char *dst, const char *src) {
    if (!dst) return;
    if (!src) { *dst = 0; return; }
    while(*src) { *dst = (*src >= 'A' && *src <= 'Z') ? *src + 32 : *src; src++; dst++; }
    *dst = 0;
}
int sl_str_contains_ci(const char *haystack, const char *needle) {
    char h[256], n[256]; int hl, nl, i, j;
    if (!haystack || !needle) return 0;
    sl_strcpy(h, haystack); sl_strcpy(n, needle);
    sl_strtolower(h, h); sl_strtolower(n, n);
    hl = sl_strlen(h); nl = sl_strlen(n);
    if (nl == 0) return 1;
    for (i = 0; i <= hl - nl; i++) {
        int match = 1;
        for (j = 0; j < nl; j++) { if (h[i+j] != n[j]) { match = 0; break; } }
        if (match) return 1;
    }
    return 0;
}
static int sl_ends_with_ci(const char *str, const char *suffix) {
    int sl = sl_strlen(str);
    int sul = sl_strlen(suffix);
    if (sl < sul) return 0;
    for (int i = 0; i < sul; i++) {
        char c1 = str[sl - sul + i];
        char c2 = suffix[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return 0;
    }
    return 1;
}
void int_to_str(int val, char *buf) {
    int i = 0, j = 0, sign = 0;
    char temp[32];
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
    if (val < 0) { sign = 1; val = -val; }
    while (val > 0) { temp[i++] = (val % 10) + '0'; val /= 10; }
    if (sign) temp[i++] = '-';
    while (i > 0) buf[j++] = temp[--i];
    buf[j] = 0;
}

static int sl_classify_media(const char *name) {
    if (!name) return SPOTLIGHT_MEDIA_NONE;
    if (sl_ends_with_ci(name, ".jpg") || sl_ends_with_ci(name, ".jpeg") ||
        sl_ends_with_ci(name, ".png") || sl_ends_with_ci(name, ".bmp")  ||
        sl_ends_with_ci(name, ".tiff")|| sl_ends_with_ci(name, ".tif")  ||
        sl_ends_with_ci(name, ".gif") || sl_ends_with_ci(name, ".raw")) {
        return SPOTLIGHT_MEDIA_IMAGE;
    }
    if (sl_ends_with_ci(name, ".avi") || sl_ends_with_ci(name, ".mp4") ||
        sl_ends_with_ci(name, ".mov") || sl_ends_with_ci(name, ".mkv")) {
        return SPOTLIGHT_MEDIA_VIDEO;
    }
    if (sl_ends_with_ci(name, ".wav") || sl_ends_with_ci(name, ".mp3") ||
        sl_ends_with_ci(name, ".ogg") || sl_ends_with_ci(name, ".pcm")) {
        return SPOTLIGHT_MEDIA_AUDIO;
    }
    return SPOTLIGHT_MEDIA_NONE;
}

spotlight_result_t spotlight_results[SPOTLIGHT_MAX_RESULTS];
int spotlight_num_results = 0;
int spotlight_selected_index = 0;
int spotlight_view_mode = SPOTLIGHT_VIEW_SEARCH;
int spotlight_active_app_kind = 0;
int spotlight_media_type = SPOTLIGHT_MEDIA_NONE;
char spotlight_media_path[256] = {0};
char spotlight_media_title[64] = {0};
int spotlight_media_playing = 0;
int spotlight_media_anim_ticks = 0;

int spotlight_hotkey_ctrl = 1;
int spotlight_hotkey_alt = 0;
int spotlight_hotkey_shift = 0;
unsigned char spotlight_hotkey_char = ' '; // Space

/* ========================================================
   MATH PLUGIN (Recursive Descent Parser)
   ======================================================== */
static const char *math_expr;
static int math_pos;

static int math_parse_expr(void);
static void math_skip_ws(void) { while(math_expr[math_pos] == ' ') math_pos++; }

static int math_parse_factor(void) {
    math_skip_ws();
    int val = 0;
    if (math_expr[math_pos] == '(') {
        math_pos++;
        val = math_parse_expr();
        math_skip_ws();
        if (math_expr[math_pos] == ')') math_pos++;
    } else {
        while (math_expr[math_pos] >= '0' && math_expr[math_pos] <= '9') {
            val = val * 10 + (math_expr[math_pos] - '0');
            math_pos++;
        }
    }
    return val;
}

static int math_parse_term(void) {
    int val = math_parse_factor();
    while (1) {
        math_skip_ws();
        char op = math_expr[math_pos];
        if (op == '*' || op == '/') {
            math_pos++;
            int next = math_parse_factor();
            if (op == '*') val *= next;
            else if (next != 0) val /= next;
        } else break;
    }
    return val;
}

static int math_parse_expr(void) {
    int val = math_parse_term();
    while (1) {
        math_skip_ws();
        char op = math_expr[math_pos];
        if (op == '+' || op == '-') {
            math_pos++;
            int next = math_parse_term();
            if (op == '+') val += next;
            else val -= next;
        } else break;
    }
    return val;
}

static int math_query(const char *text, spotlight_result_t *results, int max_results) {
    if (max_results == 0) return 0;
    int has_digit = 0, has_op = 0, is_math = 1;
    for (int i=0; text[i]; i++) {
        if (text[i] >= '0' && text[i] <= '9') has_digit = 1;
        else if (text[i] == '+' || text[i] == '-' || text[i] == '*' || text[i] == '/' || text[i] == '(' || text[i] == ')') has_op = 1;
        else if (text[i] != ' ') is_math = 0;
    }
    if (is_math && has_digit && has_op) {
        math_expr = text;
        math_pos = 0;
        int res = math_parse_expr();
        char buf[32];
        int_to_str(res, buf);
        sl_strcpy(results[0].title, buf);
        sl_strcpy(results[0].subtitle, "Math Result");
        results[0].icon_id = ICON_DISPLAY;
        results[0].score = 100;
        results[0].plugin_id = 1;
        results[0].media_type = SPOTLIGHT_MEDIA_NONE;
        results[0].app_kind = 0;
        sl_strcpy(results[0].action_arg, buf);
        return 1;
    }
    return 0;
}
static void math_execute(spotlight_result_t *result) { (void)result; }
static void math_init(void) {}

/* ========================================================
   TERMINAL PLUGIN
   ======================================================== */
static int terminal_query(const char *text, spotlight_result_t *results, int max_results) {
    if (text[0] == '>' && max_results > 0) {
        sl_strcpy(results[0].title, text + 1);
        sl_strcpy(results[0].subtitle, "Execute Terminal Command - Tab to Dropdown");
        results[0].icon_id = ICON_TERMINAL;
        results[0].score = 90;
        results[0].plugin_id = 2;
        results[0].app_kind = APP_TERMINAL;
        results[0].media_type = SPOTLIGHT_MEDIA_NONE;
        sl_strcpy(results[0].action_arg, text + 1);
        return 1;
    }
    return 0;
}
static void terminal_execute(spotlight_result_t *result) {
    (void)result;
    extern void open_app(int kind);
    open_app(APP_TERMINAL);
}
static void terminal_init(void) {}

/* ========================================================
   APP PLUGIN
   ======================================================== */
static const struct {
    const char *name;
    int app_kind;
    int icon_id;
    const char *sub;
} known_apps[] = {
    {"Terminal",      APP_TERMINAL,     ICON_TERMINAL, "Interactive Shell Console - Tab to Dropdown"},
    {"File Explorer", APP_EXPLORER,     ICON_EXPLORER, "Browse Files & Directories"},
    {"Calculator",    APP_CALCULATOR,   ICON_DISPLAY,  "Math & Computing Tool - Tab to Dropdown"},
    {"Text Edit",     APP_TEXT_EDIT,    ICON_FILE,     "Document & Text Editor"},
    {"Media Player",  APP_MEDIA_PLAYER, ICON_DISPLAY,  "Music & Video Player"},
    {"Settings",      APP_SETTINGS,     ICON_SETTINGS, "System Preferences"},
    {"Photo Viewer",  APP_PHOTO_EDITOR, ICON_PHOTO,    "Image & Photo Viewer"},
    {"IDE",           APP_IDE,          ICON_FILE,     "Code & Script Editor"},
    {"Task Manager",  APP_TASKMAN,      ICON_DISPLAY,  "Monitor Processes"},
    {"Web Browser",     APP_BROWSER,    ICON_NETWORK,  "Native HTML5 Web Surfing"}
};

static int app_query(const char *text, spotlight_result_t *results, int max_results) {
    int count = 0;
    int num_apps = sizeof(known_apps) / sizeof(known_apps[0]);
    for (int i=0; i < num_apps && count < max_results; i++) {
        if (sl_str_contains_ci(known_apps[i].name, text) ||
            (sl_str_contains_ci("terminal", text) && known_apps[i].app_kind == APP_TERMINAL) ||
            (sl_str_contains_ci("calc", text) && known_apps[i].app_kind == APP_CALCULATOR) ||
            (sl_str_contains_ci("explorer", text) && known_apps[i].app_kind == APP_EXPLORER) ||
            (sl_str_contains_ci("editor", text) && known_apps[i].app_kind == APP_TEXT_EDIT) ||
            (sl_str_contains_ci("music", text) && known_apps[i].app_kind == APP_MEDIA_PLAYER) ||
            (sl_str_contains_ci("video", text) && known_apps[i].app_kind == APP_MEDIA_PLAYER) ||
            (sl_str_contains_ci("browser", text) && known_apps[i].app_kind == APP_BROWSER) ||
            (sl_str_contains_ci("web", text) && known_apps[i].app_kind == APP_BROWSER)) {
            sl_strcpy(results[count].title, known_apps[i].name);
            sl_strcpy(results[count].subtitle, known_apps[i].sub);
            results[count].icon_id = known_apps[i].icon_id;
            results[count].score = 75;
            if (sl_strcmp(known_apps[i].name, text) == 0) results[count].score = 98;
            results[count].plugin_id = 3;
            results[count].app_kind = known_apps[i].app_kind;
            results[count].media_type = SPOTLIGHT_MEDIA_NONE;
            sl_strcpy(results[count].action_arg, known_apps[i].name);
            count++;
        }
    }
    return count;
}
static void app_execute(spotlight_result_t *result) {
    if (result->app_kind == APP_BROWSER) {
        extern void web_browser_app_launch(const char *url);
        web_browser_app_launch(0);
        return;
    }
    extern void open_app(int kind);
    if (result->app_kind > 0) open_app(result->app_kind);
    else open_app(APP_TERMINAL);
}
static void app_init(void) {}

/* ========================================================
   FILESYSTEM PLUGIN (Deep Scan & Media Type Detection)
   ======================================================== */
static int fs_query(const char *text, spotlight_result_t *results, int max_results) {
    int count = 0;
    
    /* 1. DISKS INDEXING */
    int match_disk0 = sl_str_contains_ci("FirstOS HD (virtio)", text) ||
                      sl_str_contains_ci(text, "disk") || sl_str_contains_ci(text, "drive") ||
                      sl_str_contains_ci(text, "hd") || sl_str_contains_ci(text, "virtio") ||
                      sl_str_contains_ci(text, "storage") || sl_str_contains_ci(text, "volume") ||
                      sl_str_contains_ci(text, "firstos");
    if (match_disk0 && count < max_results) {
        sl_strcpy(results[count].title, "FirstOS HD (virtio)");
        sl_strcpy(results[count].subtitle, "Disk Volume (VirtIO Block) - Tab to Dropdown Explorer");
        results[count].icon_id = ICON_DISK;
        results[count].score = 85;
        if (sl_str_contains_ci(text, "hd") || sl_str_contains_ci(text, "disk")) results[count].score = 99;
        results[count].plugin_id = 4;
        results[count].app_kind = APP_EXPLORER;
        results[count].media_type = SPOTLIGHT_MEDIA_NONE;
        sl_strcpy(results[count].action_arg, "FirstOS");
        count++;
    }

    int match_disk1 = sl_str_contains_ci("System Volume", text) ||
                      sl_str_contains_ci(text, "system") || sl_str_contains_ci(text, "recovery") ||
                      sl_str_contains_ci(text, "disk") || sl_str_contains_ci(text, "volume");
    if (match_disk1 && count < max_results) {
        sl_strcpy(results[count].title, "System Volume");
        sl_strcpy(results[count].subtitle, "Disk Volume (Recovery & System) - Tab to Dropdown Explorer");
        results[count].icon_id = ICON_DISK;
        results[count].score = 82;
        results[count].plugin_id = 4;
        results[count].app_kind = APP_EXPLORER;
        results[count].media_type = SPOTLIGHT_MEDIA_NONE;
        sl_strcpy(results[count].action_arg, "FirstOS/System");
        count++;
    }

    /* 2. ROOT & VIRTUAL DIRECTORIES INDEXING */
    static const struct { const char *name; const char *path; } root_dirs[] = {
        {"Apps",       "FirstOS/Apps"},
        {"Audio",      "FirstOS/Audio"},
        {"Desktop",    "FirstOS/Desktop"},
        {"Movies",     "FirstOS/Movies"},
        {"System",     "FirstOS/System"},
        {"Trash",      "FirstOS/Trash"},
        {"Wallpapers", "FirstOS/Wallpapers"}
    };
    for (int r = 0; r < 7 && count < max_results; r++) {
        if (sl_str_contains_ci(root_dirs[r].name, text) || sl_str_contains_ci(root_dirs[r].path, text)) {
            sl_strcpy(results[count].title, root_dirs[r].name);
            sl_strcpy(results[count].subtitle, "Directory Folder - Tab to Dropdown Explorer");
            results[count].icon_id = ICON_FOLDER;
            results[count].score = 76;
            if (sl_strcmp(root_dirs[r].name, text) == 0) results[count].score = 96;
            results[count].plugin_id = 4;
            results[count].app_kind = APP_EXPLORER;
            results[count].media_type = SPOTLIGHT_MEDIA_NONE;
            sl_strcpy(results[count].action_arg, root_dirs[r].path);
            count++;
        }
    }

    /* 3. SYSTEM CORE FILES INDEXING */
    static const struct { const char *name; const char *path; } sys_files[] = {
        {"kernel.bin",  "FirstOS/System/kernel.bin"},
        {"firstos.iso", "FirstOS/System/firstos.iso"},
        {"fs.img",      "FirstOS/System/fs.img"},
        {"linker.ld",   "FirstOS/System/linker.ld"},
        {"Makefile",    "FirstOS/System/Makefile"}
    };
    for (int s = 0; s < 5 && count < max_results; s++) {
        if (sl_str_contains_ci(sys_files[s].name, text)) {
            sl_strcpy(results[count].title, sys_files[s].name);
            sl_strcpy(results[count].subtitle, "System Core File - FirstOS/System");
            results[count].icon_id = ICON_FILE;
            results[count].score = 68;
            results[count].plugin_id = 4;
            results[count].app_kind = 0;
            results[count].media_type = SPOTLIGHT_MEDIA_NONE;
            sl_strcpy(results[count].action_arg, sys_files[s].path);
            count++;
        }
    }

    /* 4. HARD DRIVE FILESYSTEM SCAN */
    int total_fs = fs_count();
    for (int i = 0; i < total_fs && count < max_results; i++) {
        const fs_entry_t *entry = fs_entry_at(i);
        if (!entry || !entry->used) continue;
        if (!sl_str_contains_ci(entry->name, text) && !sl_str_contains_ci(entry->path, text)) continue;
        
        if (entry->is_dir) {
            int exists = 0;
            for (int k = 0; k < count; k++) {
                if (sl_strcmp(results[k].title, entry->name) == 0) { exists = 1; break; }
            }
            if (!exists) {
                sl_strcpy(results[count].title, entry->name);
                sl_strcpy(results[count].subtitle, "Directory Folder - Tab to Dropdown Explorer");
                results[count].icon_id = ICON_FOLDER;
                results[count].score = 72;
                results[count].plugin_id = 4;
                results[count].app_kind = APP_EXPLORER;
                results[count].media_type = SPOTLIGHT_MEDIA_NONE;
                sl_strcpy(results[count].action_arg, entry->path);
                count++;
            }
        } else {
            int mtype = sl_classify_media(entry->name);
            sl_strcpy(results[count].title, entry->name);
            sl_strcpy(results[count].action_arg, entry->path);
            results[count].media_type = mtype;
            results[count].app_kind = 0;
            results[count].plugin_id = 4;
            
            if (mtype == SPOTLIGHT_MEDIA_IMAGE) {
                results[count].icon_id = ICON_PHOTO;
                sl_strcpy(results[count].subtitle, "Image File - Tab to Preview");
                results[count].score = 66;
            } else if (mtype == SPOTLIGHT_MEDIA_VIDEO) {
                results[count].icon_id = ICON_DISPLAY;
                sl_strcpy(results[count].subtitle, "Movie / Video - Tab to Preview");
                results[count].score = 66;
            } else if (mtype == SPOTLIGHT_MEDIA_AUDIO) {
                results[count].icon_id = ICON_DISPLAY;
                sl_strcpy(results[count].subtitle, "Audio Track - Tab to Preview");
                results[count].score = 66;
            } else if (sl_ends_with_ci(entry->name, ".txt") || sl_ends_with_ci(entry->name, ".c") || sl_ends_with_ci(entry->name, ".fl") || sl_ends_with_ci(entry->name, ".md")) {
                results[count].icon_id = ICON_FILE;
                sl_strcpy(results[count].subtitle, "Text Document");
                results[count].score = 58;
            } else {
                results[count].icon_id = ICON_FILE;
                sl_strcpy(results[count].subtitle, "File System");
                results[count].score = 52;
            }
            if (sl_strcmp(entry->name, text) == 0) results[count].score += 30;
            count++;
        }
    }
    return count;
}
static void fs_execute(spotlight_result_t *result) {
    extern void open_app(int kind);
    if (result->icon_id == ICON_FOLDER || result->icon_id == ICON_DISK || result->app_kind == APP_EXPLORER) {
        open_app(APP_EXPLORER);
        extern void explorer_open_path(const char *path);
        explorer_open_path(result->action_arg);
    } else if (result->media_type == SPOTLIGHT_MEDIA_IMAGE) {
        extern void preview_open(const char *path);
        open_app(APP_PREVIEW);
        preview_open(result->action_arg);
    } else if (result->media_type == SPOTLIGHT_MEDIA_VIDEO || result->media_type == SPOTLIGHT_MEDIA_AUDIO) {
        extern int media_player_open_file(const char *path);
        open_app(APP_MEDIA_PLAYER);
        media_player_open_file(result->action_arg);
    } else if (sl_ends_with_ci(result->action_arg, ".txt") || sl_ends_with_ci(result->action_arg, ".c") || sl_ends_with_ci(result->action_arg, ".fl") || sl_ends_with_ci(result->action_arg, ".md")) {
        extern void text_edit_open_file(const char *path);
        open_app(APP_TEXT_EDIT);
        text_edit_open_file(result->action_arg);
    } else {
        open_app(APP_EXPLORER);
    }
}
static void sl_fs_init(void) {}

#define MAX_PLUGINS 5
static spotlight_plugin_t plugins[MAX_PLUGINS];
static int num_plugins = 0;

void spotlight_register_plugin(spotlight_plugin_t *plugin) {
    if (num_plugins < MAX_PLUGINS) {
        plugin->init();
        plugins[num_plugins++] = *plugin;
    }
}

void spotlight_init(void) {
    num_plugins = 0;
    spotlight_plugin_t p1 = {"Math", math_init, math_query, math_execute};
    spotlight_plugin_t p2 = {"Terminal", terminal_init, terminal_query, terminal_execute};
    spotlight_plugin_t p3 = {"Apps", app_init, app_query, app_execute};
    spotlight_plugin_t p4 = {"Files", sl_fs_init, fs_query, fs_execute};
    spotlight_register_plugin(&p1);
    spotlight_register_plugin(&p2);
    spotlight_register_plugin(&p3);
    spotlight_register_plugin(&p4);
    spotlight_close_dropdown();
}

void spotlight_search(const char *query) {
    spotlight_num_results = 0;
    spotlight_selected_index = 0;
    if (sl_strlen(query) == 0) return;
    
    spotlight_result_t temp[50];
    int total = 0;
    
    for (int p=0; p<num_plugins; p++) {
        total += plugins[p].query(query, &temp[total], 50 - total);
    }
    
    // Sort logic (simple bubble sort by score descending)
    for (int i=0; i<total-1; i++) {
        for (int j=0; j<total-i-1; j++) {
            if (temp[j].score < temp[j+1].score) {
                spotlight_result_t t = temp[j];
                temp[j] = temp[j+1];
                temp[j+1] = t;
            }
        }
    }
    
    for (int i=0; i<total && i<SPOTLIGHT_MAX_RESULTS; i++) {
        spotlight_results[i] = temp[i];
    }
    spotlight_num_results = (total > SPOTLIGHT_MAX_RESULTS) ? SPOTLIGHT_MAX_RESULTS : total;
}

void spotlight_execute(int index) {
    if (index >= 0 && index < spotlight_num_results) {
        spotlight_result_t *res = &spotlight_results[index];
        spotlight_stop_media_playback();
        spotlight_view_mode = SPOTLIGHT_VIEW_SEARCH;
        
        if (res->plugin_id == 1) math_execute(res);
        else if (res->plugin_id == 2) terminal_execute(res);
        else if (res->plugin_id == 3) app_execute(res);
        else if (res->plugin_id == 4) fs_execute(res);
        
        extern int spotlight_open;
        spotlight_open = 0;
    }
}

/* ========================================================
   DROPDOWN & MEDIA PREVIEW MANAGEMENT
   ======================================================== */
void spotlight_open_dropdown_app(int app_kind) {
    spotlight_stop_media_playback();
    spotlight_view_mode = SPOTLIGHT_VIEW_APP_DROPDOWN;
    spotlight_active_app_kind = app_kind;
    spotlight_media_type = SPOTLIGHT_MEDIA_NONE;
    if (app_kind == APP_TERMINAL) {
        extern int terminal_focused;
        extern int terminal_cursor_visible;
        terminal_focused = 1;
        terminal_cursor_visible = 1;
    } else if (app_kind == APP_EXPLORER) {
        spotlight_open_dropdown_explorer("FirstOS");
    }
}

char spotlight_explorer_path[128] = "FirstOS";
int spotlight_explorer_selected = 0;
int spotlight_explorer_count = 0;
explorer_entry_t spotlight_explorer_entries[32];
static char sl_exp_names[32][32];
static char sl_exp_paths[32][256];

void spotlight_explorer_refresh(void) {
    spotlight_explorer_count = 0;
    
    if (sl_strcmp(spotlight_explorer_path, "FirstOS") == 0 || spotlight_explorer_path[0] == 0) {
        static const struct { const char *name; const char *path; int ico; } roots[] = {
            {"Apps",       "FirstOS/Apps",       ICON_APPS},
            {"Audio",      "FirstOS/Audio",      ICON_DISPLAY},
            {"Desktop",    "FirstOS/Desktop",    ICON_FOLDER},
            {"Movies",     "FirstOS/Movies",     ICON_DISPLAY},
            {"System",     "FirstOS/System",     ICON_FOLDER},
            {"Trash",      "FirstOS/Trash",      ICON_FOLDER},
            {"Wallpapers", "FirstOS/Wallpapers", ICON_PHOTO}
        };
        for (int i = 0; i < 7; i++) {
            sl_strcpy(sl_exp_names[i], roots[i].name);
            sl_strcpy(sl_exp_paths[i], roots[i].path);
            spotlight_explorer_entries[i].name = sl_exp_names[i];
            spotlight_explorer_entries[i].path = sl_exp_paths[i];
            spotlight_explorer_entries[i].is_dir = 1;
            spotlight_explorer_entries[i].cached_icon = roots[i].ico;
            spotlight_explorer_entries[i].folder_color = 0;
        }
        spotlight_explorer_count = 7;
    } else if (sl_strcmp(spotlight_explorer_path, "FirstOS/Apps") == 0 || sl_strcmp(spotlight_explorer_path, "Apps") == 0) {
        static const struct { const char *name; const char *path; int ico; } apps[] = {
            {"Terminal.app", "FirstOS/Apps/Terminal.app", ICON_TERMINAL},
            {"Settings.app", "FirstOS/Apps/Settings.app", ICON_SETTINGS},
            {"TextEdit.app", "FirstOS/Apps/TextEdit.app", ICON_FILE},
            {"Explorer.app", "FirstOS/Apps/Explorer.app", ICON_EXPLORER},
            {"TaskMan.app",  "FirstOS/Apps/TaskMan.app",  ICON_DISPLAY}
        };
        for (int i = 0; i < 5; i++) {
            sl_strcpy(sl_exp_names[i], apps[i].name);
            sl_strcpy(sl_exp_paths[i], apps[i].path);
            spotlight_explorer_entries[i].name = sl_exp_names[i];
            spotlight_explorer_entries[i].path = sl_exp_paths[i];
            spotlight_explorer_entries[i].is_dir = 0;
            spotlight_explorer_entries[i].cached_icon = apps[i].ico;
            spotlight_explorer_entries[i].folder_color = 0;
        }
        spotlight_explorer_count = 5;
    } else if (sl_strcmp(spotlight_explorer_path, "FirstOS/System") == 0 || sl_strcmp(spotlight_explorer_path, "System") == 0) {
        static const struct { const char *name; const char *path; } sys[] = {
            {"kernel.bin",  "FirstOS/System/kernel.bin"},
            {"firstos.iso", "FirstOS/System/firstos.iso"},
            {"fs.img",      "FirstOS/System/fs.img"},
            {"linker.ld",   "FirstOS/System/linker.ld"},
            {"Makefile",    "FirstOS/System/Makefile"}
        };
        for (int i = 0; i < 5; i++) {
            sl_strcpy(sl_exp_names[i], sys[i].name);
            sl_strcpy(sl_exp_paths[i], sys[i].path);
            spotlight_explorer_entries[i].name = sl_exp_names[i];
            spotlight_explorer_entries[i].path = sl_exp_paths[i];
            spotlight_explorer_entries[i].is_dir = 0;
            spotlight_explorer_entries[i].cached_icon = ICON_FILE;
            spotlight_explorer_entries[i].folder_color = 0;
        }
        spotlight_explorer_count = 5;
    } else {
        const char *fs_dir = spotlight_explorer_path;
        if (sl_strcmp(spotlight_explorer_path, "FirstOS/Desktop") == 0 || sl_strcmp(spotlight_explorer_path, "Desktop") == 0) fs_dir = "/Desktop";
        else if (sl_strcmp(spotlight_explorer_path, "FirstOS/Wallpapers") == 0 || sl_strcmp(spotlight_explorer_path, "Wallpapers") == 0) fs_dir = "/wallpapers";
        else if (sl_strcmp(spotlight_explorer_path, "FirstOS/Audio") == 0 || sl_strcmp(spotlight_explorer_path, "Audio") == 0) fs_dir = "/audio";
        else if (sl_strcmp(spotlight_explorer_path, "FirstOS/Movies") == 0 || sl_strcmp(spotlight_explorer_path, "Movies") == 0) fs_dir = "/movies";
        else if (sl_strcmp(spotlight_explorer_path, "FirstOS/Trash") == 0 || sl_strcmp(spotlight_explorer_path, "Trash") == 0) fs_dir = "/Trash";

        int total = fs_count_in_dir(fs_dir);
        int visible = 0;
        for (int i = 0; i < total && visible < 32; i++) {
            const fs_entry_t *e = fs_entry_in_dir(fs_dir, i);
            if (!e) continue;
            sl_strcpy(sl_exp_names[visible], e->name);
            sl_strcpy(sl_exp_paths[visible], e->path);
            spotlight_explorer_entries[visible].name = sl_exp_names[visible];
            spotlight_explorer_entries[visible].path = sl_exp_paths[visible];
            spotlight_explorer_entries[visible].is_dir = e->is_dir;
            
            if (e->is_dir) {
                spotlight_explorer_entries[visible].cached_icon = ICON_FOLDER;
            } else {
                int mt = sl_classify_media(e->name);
                if (mt == SPOTLIGHT_MEDIA_IMAGE) spotlight_explorer_entries[visible].cached_icon = ICON_PHOTO;
                else if (mt == SPOTLIGHT_MEDIA_VIDEO || mt == SPOTLIGHT_MEDIA_AUDIO) spotlight_explorer_entries[visible].cached_icon = ICON_DISPLAY;
                else spotlight_explorer_entries[visible].cached_icon = ICON_FILE;
            }
            spotlight_explorer_entries[visible].folder_color = 0;
            visible++;
        }
        spotlight_explorer_count = visible;
    }
    
    if (spotlight_explorer_selected >= spotlight_explorer_count) {
        spotlight_explorer_selected = (spotlight_explorer_count > 0) ? spotlight_explorer_count - 1 : 0;
    }
}

void spotlight_open_dropdown_explorer(const char *initial_path) {
    spotlight_stop_media_playback();
    spotlight_view_mode = SPOTLIGHT_VIEW_APP_DROPDOWN;
    spotlight_active_app_kind = APP_EXPLORER;
    spotlight_media_type = SPOTLIGHT_MEDIA_NONE;
    if (initial_path && initial_path[0]) {
        if (sl_str_contains_ci(initial_path, "FirstOS HD") || sl_strcmp(initial_path, "FirstOS") == 0 || sl_str_contains_ci(initial_path, "Explorer")) {
            sl_strcpy(spotlight_explorer_path, "FirstOS");
        } else if (sl_str_contains_ci(initial_path, "System Volume")) {
            sl_strcpy(spotlight_explorer_path, "FirstOS/System");
        } else {
            sl_strcpy(spotlight_explorer_path, initial_path);
        }
    } else {
        sl_strcpy(spotlight_explorer_path, "FirstOS");
    }
    spotlight_explorer_selected = 0;
    spotlight_explorer_refresh();
}

void spotlight_explorer_navigate(const char *path) {
    if (!path || !path[0]) sl_strcpy(spotlight_explorer_path, "FirstOS");
    else sl_strcpy(spotlight_explorer_path, path);
    spotlight_explorer_selected = 0;
    spotlight_explorer_refresh();
}

void spotlight_explorer_up(void) {
    int last_slash = -1;
    for (int i = 0; spotlight_explorer_path[i]; i++) {
        if (spotlight_explorer_path[i] == '/') last_slash = i;
    }
    if (last_slash <= 0 || sl_strcmp(spotlight_explorer_path, "FirstOS") == 0) {
        sl_strcpy(spotlight_explorer_path, "FirstOS");
    } else {
        spotlight_explorer_path[last_slash] = 0;
    }
    spotlight_explorer_selected = 0;
    spotlight_explorer_refresh();
}

void spotlight_explorer_move(int delta) {
    spotlight_explorer_selected += delta;
    if (spotlight_explorer_selected < 0) spotlight_explorer_selected = 0;
    if (spotlight_explorer_selected >= spotlight_explorer_count) {
        spotlight_explorer_selected = (spotlight_explorer_count > 0) ? spotlight_explorer_count - 1 : 0;
    }
}

void spotlight_explorer_enter(void) {
    if (spotlight_explorer_count == 0) return;
    if (spotlight_explorer_selected < 0 || spotlight_explorer_selected >= spotlight_explorer_count) return;
    explorer_entry_t *entry = &spotlight_explorer_entries[spotlight_explorer_selected];
    
    if (entry->is_dir) {
        spotlight_explorer_navigate(entry->path);
    } else {
        extern void open_app(int kind);
        spotlight_close_dropdown();
        extern int spotlight_open;
        spotlight_open = 0;
        
        int mt = sl_classify_media(entry->name);
        if (mt == SPOTLIGHT_MEDIA_IMAGE) {
            extern void preview_open(const char *path);
            open_app(APP_PREVIEW);
            preview_open(entry->path);
        } else if (mt == SPOTLIGHT_MEDIA_VIDEO || mt == SPOTLIGHT_MEDIA_AUDIO) {
            extern int media_player_open_file(const char *path);
            open_app(APP_MEDIA_PLAYER);
            media_player_open_file(entry->path);
        } else if (sl_ends_with_ci(entry->name, ".txt") || sl_ends_with_ci(entry->name, ".c") || sl_ends_with_ci(entry->name, ".fl") || sl_ends_with_ci(entry->name, ".md")) {
            extern void text_edit_open_file(const char *path);
            open_app(APP_TEXT_EDIT);
            text_edit_open_file(entry->path);
        } else if (sl_strcmp(entry->name, "Terminal.app") == 0) {
            open_app(APP_TERMINAL);
        } else if (sl_strcmp(entry->name, "Settings.app") == 0) {
            open_app(APP_SETTINGS);
        } else if (sl_strcmp(entry->name, "TextEdit.app") == 0) {
            open_app(APP_TEXT_EDIT);
        } else if (sl_strcmp(entry->name, "Explorer.app") == 0) {
            open_app(APP_EXPLORER);
        } else if (sl_strcmp(entry->name, "TaskMan.app") == 0) {
            open_app(APP_TASKMAN);
        } else {
            open_app(APP_EXPLORER);
        }
    }
}

void spotlight_open_media_preview(const char *path, int media_type, const char *title) {
    spotlight_stop_media_playback();
    spotlight_view_mode = SPOTLIGHT_VIEW_MEDIA_PREVIEW;
    spotlight_media_type = media_type;
    spotlight_active_app_kind = 0;
    sl_strcpy(spotlight_media_path, path ? path : "");
    sl_strcpy(spotlight_media_title, title ? title : (path ? path : "Media Preview"));
    spotlight_media_playing = 0;
    spotlight_media_anim_ticks = 0;
}

void spotlight_close_dropdown(void) {
    spotlight_stop_media_playback();
    spotlight_view_mode = SPOTLIGHT_VIEW_SEARCH;
    spotlight_active_app_kind = 0;
    spotlight_media_type = SPOTLIGHT_MEDIA_NONE;
    spotlight_media_path[0] = 0;
    spotlight_media_title[0] = 0;
}

int spotlight_is_in_dropdown(void) {
    return (spotlight_view_mode != SPOTLIGHT_VIEW_SEARCH);
}

void spotlight_toggle_media_playback(void) {
    if (spotlight_media_type == SPOTLIGHT_MEDIA_AUDIO && spotlight_media_path[0]) {
        if (spotlight_media_playing) {
            hda_pause_audio();
            spotlight_media_playing = 0;
        } else {
            if (hda_is_paused()) {
                hda_resume_audio();
            } else {
                hda_play_audio(spotlight_media_path);
            }
            spotlight_media_playing = 1;
        }
    } else if (spotlight_media_type == SPOTLIGHT_MEDIA_VIDEO) {
        spotlight_media_playing = !spotlight_media_playing;
    }
}

void spotlight_stop_media_playback(void) {
    if (spotlight_media_playing) {
        hda_stop_audio();
        spotlight_media_playing = 0;
    }
}

void spotlight_media_tick(void) {
    if (spotlight_view_mode == SPOTLIGHT_VIEW_MEDIA_PREVIEW) {
        spotlight_media_anim_ticks++;
        if (spotlight_media_type == SPOTLIGHT_MEDIA_AUDIO && spotlight_media_playing) {
            if (!hda_is_playing() && !hda_is_paused()) {
                spotlight_media_playing = 0;
            }
        }
    }
}
