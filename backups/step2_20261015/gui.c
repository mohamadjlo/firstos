int ide_saving_ui = 0;
#include "gui.h"
#include "gui_internal.h"
#include "gui_widgets.h"
#include "app_framework.h"
#include "ui_builder.h"
extern int ide_designer_mode;
#include "heap.h"
#include "hda.h"
#include "settings.h"
#include "theme.h"
#include "text_edit.h"
#include "framebuffer.h"
#include "pci.h"
#include "ps2.h"
#include "font.h"
#include "cpu.h"
#include "explorer.h"
#include "rtc.h"
#include "fs.h"
#include "terminal.h"
#include "firstlang.h"
#include "spotlight.h"
#include "dock.h"
#include "ide.h"
#include "taskman.h"
#include "photo_viewer.h"
#include "media_player.h"
#include "preview.h"
#include "widgets.h"
#include "keybindings.h"
#include "net.h"
#include "file_dialog.h"
#include "user.h"
#include "io.h"
#include "thumbnail.h"
#include "sound.h"
#include "web_browser_app.h"
#include "http_client.h"

static void gui_fl_cpy(char *dst, const char *src, int max) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (src) {
        while (i < max - 1 && src[i]) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static int gui_fl_ends(const char *str, const char *suffix) {
    int len = 0, slen = 0;
    while(str[len]) len++;
    while(suffix[slen]) slen++;
    if(len < slen) return 0;
    for(int i=0; i<slen; i++) {
        if(str[len - slen + i] != suffix[i]) return 0;
    }
    return 1;
}
static void gui_fl_cat(char *dst, const char *src, int max) {
    int i=0; while(dst[i] && i<max-1) i++;
    int j=0; while(src[j] && i<max-1) { dst[i]=src[j]; i++; j++; }
    dst[i]=0;
}
static void gui_fl_itos(int num, char *str, int max) {
    if(num==0) { str[0]='0'; str[1]=0; return; }
    char tmp[32]; int i=0, j=0;
    while(num>0 && i<31) { tmp[i++]=(num%10)+'0'; num/=10; }
    while(i>0 && j<max-1) str[j++]=tmp[--i];
    str[j]=0;
}

static int gui_fl_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static int gui_str_contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    int hl = 0; while (haystack[hl]) hl++;
    int nl = 0; while (needle[nl]) nl++;
    if (nl == 0) return 1;
    for (int i = 0; i <= hl - nl; i++) {
        int m = 1;
        for (int j = 0; j < nl; j++) {
            char c1 = haystack[i + j];
            char c2 = needle[j];
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) { m = 0; break; }
        }
        if (m) return 1;
    }
    return 0;
}

static void t2d(int v, char *out) {
    out[0] = '0' + ((v / 10) % 10);
    out[1] = '0' + (v % 10);
    out[2] = 0;
}

static void draw_wallpaper(void);


extern void ide_insert_char(char c);
extern void ide_backspace(void);
extern void ide_mark_saved(void);
extern void ide_copy_selection(void);
extern void ide_cut_selection(void);
extern void ide_paste_clipboard(void);
extern void ide_select_all(void);
extern void ide_undo(void);
extern int ide_position_from_point(int mx, int my);

#include "serial.h"
#include "phase5_architecture.h"

static void desktop_copy_name(char *dst, int max, const char *src);
static void sync_desktop_icons_from_fs(void);
static const char* explorer_to_fs_path(const char *path);

static int explorer_path_exists(const char *path);
static int explorer_build_unique_paste_path(const char *dir, const char *name, char *out, int out_size);
static void explorer_copy_selected_file(int cut);
static void explorer_paste_file(void);
static void begin_explorer_rename_selected(void);
static void handle_explorer_open_selected(void);
static int explorer_item_at(app_window_t *app, int mx, int my);
static int is_text_or_doc_file(const char *name);
static int explorer_entry_matches_search(const explorer_entry_t *entry);
static int explorer_filtered_index_from_visible(int visible_index);
static int explorer_filtered_count(void);
static int explorer_entry_match_start(const explorer_entry_t *entry);
static void explorer_focus_search_result(int step);
static void explorer_store_search_history(void);
static int explorer_visible_items_capacity(app_window_t *app);
static int explorer_grid_columns(app_window_t *app);
static void explorer_ensure_selection_visible(app_window_t *app);
static int explorer_is_selected(int index);
static void explorer_clear_multi_selection(void);
static void explorer_select_range(int start_index, int end_index);
static int load_start_menu_ico(void);
static int load_brand_ico(void);
static void explorer_open_file_default(const char *path, const char *name);
static void draw_explorer_quick_look(void);
static void explorer_copy_selected_file(int cut);
static void explorer_paste_file(void);
static void desktop_paste_file(void);
static void draw_drag_preview(int mx, int my);

static void draw_window_scrollbar(app_window_t *app, int total_items, int visible_items, int scroll_index);
void focus_app_window(int index);
static void begin_window_transition(int index, int mode);
void apply_snap_to_window(app_window_t* app, int snap_side);
static u32 mix_color(u32 a, u32 b) {
    u32 ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    u32 br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    return (((ar + br) >> 1) << 16) | (((ag + bg) >> 1) << 8) | ((ab + bb) >> 1);
}


static rect_t desktop_bar;
static rect_t dock_rect;
static int gui_start_button_visible = 0;
static rect_t start_button = {0, 0, 0, 0, 0};
static rect_t start_menu_rect;
static rect_t start_apps_item;
static rect_t start_browser_item;
static rect_t start_explorer_item;
static rect_t start_settings_item;
static rect_t start_textedit_item;
static rect_t start_shutdown_item;
static rect_t start_media_item;
static rect_t start_calc_item;
static rect_t start_ide_item;
static rect_t start_taskman_item;
static rect_t start_taskman_item;
static rect_t start_demo_item;
static rect_t start_restart_item;
static rect_t top_brand_button;
static rect_t top_brand_menu;
static rect_t top_updates_item;
static rect_t top_system_info_item;
static rect_t top_logout_item;
static rect_t top_sleep_item;
static rect_t top_reboot_item;
static rect_t top_shutdown_item;
static rect_t top_app_menu_button;
static rect_t top_file_menu_button;
static rect_t top_edit_menu_button;
static rect_t top_view_menu_button;
static rect_t top_app_menu_rect;
static rect_t top_file_menu_rect;
static rect_t top_edit_menu_rect;
static rect_t top_view_menu_rect;
static rect_t top_app_action_1;
static rect_t top_app_action_2;
static rect_t top_app_action_3;
static rect_t top_app_action_4;
static rect_t top_app_action_5;
static rect_t top_app_action_6;
static int volume_menu_open = 0;
int system_volume = 50;
static rect_t top_volume_menu;
static rect_t top_volume_slider;
static rect_t top_volume_button;
static rect_t top_wifi_button;
static rect_t top_wifi_menu;
int wifi_menu_open = 0;
int current_wifi_network = 0; // 0 = connected to first network
const char *wifi_networks[] = {"Home Wi-Fi 5G", "Office Network", "FirstOS Public", "Guest Network", "Starbucks Free"};

int wifi_pwd_modal_open = 0;
int wifi_pwd_ap_index = -1;
char wifi_pwd_input[64] = {0};
int wifi_pwd_cursor = 0;
int wifi_pwd_show_pass = 0;
int wifi_pwd_error = 0;

void wifi_password_modal_open_for_ap(int ap_index) {
    if (ap_index < 0 || ap_index >= net_get_ap_count()) return;
    const wifi_ap_t *ap = net_get_ap(ap_index);
    wifi_pwd_modal_open = 1;
    wifi_pwd_ap_index = ap_index;
    if (ap && ap->expected_pass[0]) {
        gui_fl_cpy(wifi_pwd_input, ap->expected_pass, sizeof(wifi_pwd_input));
        int l = 0;
        while (wifi_pwd_input[l]) l++;
        wifi_pwd_cursor = l;
    } else {
        wifi_pwd_input[0] = 0;
        wifi_pwd_cursor = 0;
    }
    wifi_pwd_show_pass = 0;
    wifi_pwd_error = 0;
    wifi_menu_open = 0;
    needs_redraw = 1;
}
static rect_t top_search_button;
static rect_t top_fps_button;
static rect_t top_caps_button;
static rect_t fps_dropdown_rect;
static int fps_dropdown_open = 0;
static rect_t top_clock_button;
static rect_t top_clock_menu;
static rect_t top_weather_button;
static rect_t weather_popover_rect;
static rect_t top_notif_button;
static rect_t notif_center_rect;

weather_info_t current_weather = {
    "New York",
    WEATHER_SUNNY,
    22,
    48,
    14,
    "Sunny"
};

int desktop_icon_size = ICON_SIZE_MEDIUM;
int widgets_window_open = 0;
rect_t widgets_window_rect = {360, 180, 280, 220, 0};
static int widgets_window_dragging = 0;
static int widgets_window_drag_ox = 0;
static int widgets_window_drag_oy = 0;

static rect_t desktop_ctx_icon_size_s;
static rect_t desktop_ctx_icon_size_m;
static rect_t desktop_ctx_icon_size_l;
static rect_t desktop_ctx_add_widgets;

void weather_set_city(const char *name);
static int weather_popover_open = 0;
static char weather_input_city[32] = {0};
static int weather_input_len = 0;
static int weather_input_focused = 0;

#define MAX_NOTIFICATIONS 8
typedef struct {
    int id;
    int active;
    char app[20];
    char title[32];
    char message[64];
    char time_ago[16];
    int icon;
    u32 accent;
} gui_notification_t;

static gui_notification_t gui_notifications[MAX_NOTIFICATIONS];
static int notification_count = 0;
static int notification_center_open = 0;
static int notification_dnd = 0;
static int notif_initialized = 0;

static int clock_view_month = 0;
static int clock_view_year = 0;
static rect_t main_window;
static rect_t main_title_bar;
static rect_t main_button_close;
static rect_t main_button_max;
static rect_t main_button_min;
static rect_t dock_terminal;
static rect_t dock_files;
static rect_t dock_display;
static rect_t dock_explorer;
static rect_t dock_settings;

static u32 desktop_bg = 0x102033;
static u32 desktop_panel = 0x18304C;
static u32 dock_bg = 0x18283D;
static u32 window_bg = 0xD7DEE8;
static u32 title_bg = 0x315A8A;
static u32 shadow = 0x09111A;
static u32 window_border = 0x294A6B;
static u32 control_bg = 0xB9C8D7;
static u32 control_bg_hi = 0xD4DFEA;
static u32 control_text = 0x20344E;
static u32 accent_color = 0x6E99C9;
u32 system_font_color = 0xFFFFFF;

// legacy dock removed

/* ---- Launcher overlay ---- */
int launcher_open = 0;
static int launcher_hover = -1;

int spotlight_open = 0;
char spotlight_query[64];
int spotlight_qlen = 0;
// spotlight structures moved to spotlight.h
static int dragging;
static int drag_offset_x;
static int drag_offset_y;
static int prev_left;
static int prev_right_button;
static int start_menu_open;
int active_app_index = -1;
static int z_order[32] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31
};
static const char *top_menu_app_label = "Desktop";
static const char *top_menu_app_item_1 = "About Desktop";
static const char *top_menu_app_item_2 = "Hide Desktop";
static const char *top_menu_app_item_3 = "Show Desktop";
static const char *top_menu_file_item_1 = "New";
static const char *top_menu_file_item_2 = "Open";
static const char *top_menu_file_item_3 = "Close";
static const char *top_menu_file_item_4 = "";
static const char *top_menu_file_item_5 = "";
static const char *top_menu_file_item_6 = "";
static const char *top_menu_edit_item_1 = "Copy";
static const char *top_menu_edit_item_2 = "Paste";
static const char *top_menu_edit_item_3 = "Delete";
static const char *top_menu_edit_item_4 = "Design";
static const char *top_menu_edit_items_list[16];
static int top_menu_app_items = 3;
static int top_menu_file_items = 3;
static int top_menu_edit_items = 3;
int desktop_context_open;
int desktop_sort_mode = DESKTOP_SORT_NONE;
static rect_t desktop_context_menu;
static rect_t desktop_ctx_refresh;
static rect_t desktop_ctx_change_wall;
static rect_t desktop_ctx_settings;
static rect_t desktop_ctx_textedit;
static rect_t desktop_ctx_new_folder;
static rect_t desktop_ctx_new_file;
static rect_t desktop_ctx_sort_name;
static rect_t desktop_ctx_sort_date;
static rect_t desktop_ctx_sort_kind;
static rect_t desktop_ctx_copy;
static rect_t desktop_ctx_paste;
static rect_t desktop_ctx_rename;
static rect_t desktop_ctx_delete;
static int desktop_context_target;
int desktop_context_is_explorer;
static char media_player_current_file[64] = "";
int media_player_is_playing = 0;
static char preview_image_path[128] = "";
static u32 *preview_image_data = 0;
static int preview_image_w = 0;
static int preview_image_h = 0;
static int desktop_context_scroll;
static int explorer_context_open;
static rect_t explorer_context_menu;
static int explorer_context_scroll;
static int taskbar_context_open;
static int taskbar_context_index = -1;
static int taskbar_context_scroll;
static rect_t taskbar_context_menu;
static rect_t taskbar_ctx_open_restore;
static rect_t taskbar_ctx_minimize;
static rect_t taskbar_ctx_maximize;
static rect_t taskbar_ctx_close;
static rect_t explorer_ctx_copy;
static rect_t explorer_ctx_paste;
static rect_t explorer_ctx_rename;
static rect_t explorer_ctx_delete;
static rect_t explorer_ctx_terminal;
static rect_t explorer_ctx_color;
static rect_t explorer_paste_dialog;
static rect_t explorer_paste_replace_button;
static rect_t explorer_paste_copy_button;
static rect_t explorer_paste_cancel_button;
static int explorer_context_index = -1;
static int brand_menu_open;

// Phase 1: Consolidated GUI state
static int window_resizing = 0;
static int window_resizing_edge = 0; // 0=none, 1=right, 2=bottom, 3=right-bottom
#define SNAP_NONE 0
#define SNAP_LEFT 1
#define SNAP_RIGHT 2
#define SNAP_FULLSCREEN 3

static int window_snapping = SNAP_NONE;
int snap_enabled = 1;
static void update_explorer_thumbnail(const char *path, const char *ext);
int window_animations = 1;
static int window_transition_ticks[32];
static int window_transition_mode[32];
int task_switcher_enabled = 1;

typedef struct {
    rect_t icon_rect;
    char name[128];
    char path[256];
    int used;
    int is_dir;
    int rename_mode;
    int cursor;
    int last_click_ticks;
    int last_click;
    u32 date_order;
    int has_custom_pos;
    int custom_x;
    int custom_y;
} desktop_item_t;

static desktop_item_t desktop_items[32];
static int desktop_item_count;
static int desktop_selected = -1;
static int desktop_context_index = -1;
static char explorer_rename_name[128];
static int explorer_rename_mode;
static int explorer_rename_cursor;
static int explorer_rename_index = -1;
static int app_menu_open;
static int file_menu_open;
static int edit_menu_open;
static int view_menu_open;
static int clock_menu_open;
int needs_redraw = 1;
int gui_window_dirty_only = 0;
static int fps_counter = 0;
static int fps_value = 0;
static int fps_last_second = -1;
static gui_event_t gui_last_event;
static int last_mouse_x = -1;
static int last_mouse_y = -1;
static int last_mouse_buttons;

void gui_toggle_wifi_menu(void) {
    wifi_menu_open = !wifi_menu_open;
    if (wifi_menu_open) {
        extern void net_rescan_wifi(void);
        net_rescan_wifi();
    }
    weather_popover_open = 0;
    fps_dropdown_open = 0;
    notification_center_open = 0;
    volume_menu_open = 0;
    clock_menu_open = 0;
    brand_menu_open = 0;
    start_menu_open = 0;
    app_menu_open = 0;
    file_menu_open = 0;
    edit_menu_open = 0;
    view_menu_open = 0;
    desktop_context_open = 0;
    needs_redraw = 1;
}

static void gui_capture_event(gui_event_t *event) {
    int buttons;
    if (!event) return;
    buttons = (ps2_mouse_left() ? 1 : 0) | (ps2_mouse_right() ? 2 : 0) | (ps2_mouse_middle() ? 4 : 0);
    event->type = GUI_EVENT_NONE;
    event->x = ps2_mouse_x();
    event->y = ps2_mouse_y();
    event->key = ps2_last_key();
    event->button = buttons;
    if (event->key) event->type = (event->key & 0x80) ? GUI_EVENT_KEY_UP : GUI_EVENT_KEY_DOWN;
    else if (buttons != last_mouse_buttons) event->type = (buttons & 1) ? GUI_EVENT_MOUSE_DOWN : GUI_EVENT_MOUSE_UP;
    else if (event->x != last_mouse_x || event->y != last_mouse_y) event->type = GUI_EVENT_MOUSE_MOVE;
}

static int click_tone_ticks;

static void gui_click_sound(void) {
    sound_play_event(SOUND_EVENT_CLICK);
}

static void gui_click_sound_tick(void) {
}


static int fullscreen_mode;
static int main_window_open = 0;
static int main_window_minimized;
static int saved_x;
static int saved_y;
static int saved_w;
static int saved_h;
static int window_drag_kind;
static int power_off_requested;
static int clock_hours;
static int clock_minutes;
static int clock_seconds;
static int rtc_cached_second = -1;
int wallpaper_id = 0;
int wallpaper_source = 1;
char wallpaper_path[128] = "/wallpapers/default_wallpaper.raw";
static int explorer_file_clipboard_set;
static int explorer_file_clipboard_cut;

static int explorer_paste_conflict_open;
#define MAX_CLIPBOARD 16
static char explorer_file_clipboard_path[MAX_CLIPBOARD][256];
static char explorer_file_clipboard_name[MAX_CLIPBOARD][128];
static int explorer_file_clipboard_is_dir[MAX_CLIPBOARD];
static int explorer_file_clipboard_count = 0;
static char explorer_paste_target_dir[256] = "";
static char explorer_paste_target_path[256] = "";
static int wallpaper_jpg_ready = 0;
static int wallpaper_cache_dirty = 1;
static int wallpaper_cached_id = -1;
static int wallpaper_cached_source = -1;
static char wallpaper_cached_path[128] = "";
#define MAX_SCREEN_WIDTH 1920
#define MAX_SCREEN_HEIGHT 1200
/* Wallpaper pixel cache — painted once, blitted every frame */
static u32 wallpaper_cache[MAX_SCREEN_WIDTH * MAX_SCREEN_HEIGHT];
static unsigned char wallpaper_file_data[1024 * 1024];
static u32 start_menu_ico_pixels[32 * 32];
static int start_menu_ico_width = 0;
static int start_menu_ico_height = 0;
static int start_menu_ico_loaded = 0;

static u32 terminal_ico_pixels[32 * 32];
static int terminal_ico_width = 0, terminal_ico_height = 0, terminal_ico_loaded = 0;

static u32 explorer_ico_pixels[32 * 32];
static int explorer_ico_width = 0, explorer_ico_height = 0, explorer_ico_loaded = 0;

static u32 textedit_ico_pixels[32 * 32];
static int textedit_ico_width = 0, textedit_ico_height = 0, textedit_ico_loaded = 0;

static u32 audiowav_ico_pixels[32 * 32];
static int audiowav_ico_width = 0, audiowav_ico_height = 0, audiowav_ico_loaded = 0;

static u32 browser_ico_pixels[32 * 32];
static int browser_ico_width = 0, browser_ico_height = 0, browser_ico_loaded = 0;

static u32 brand_ico_pixels[32 * 32];
static int brand_ico_width = 0;
static int brand_ico_height = 0;
static int brand_ico_loaded = 0;
u32 titlebar_custom_color = 0x315A8A;
int titlebar_color_custom = 0;
u8 titlebar_alpha = 255;
u32 topbar_color = 0x182432;
int topbar_color_custom = 0;
u8 topbar_alpha = 240;
u32 wallpaper_solid_color = 0x102033;
int mouse_context_cursors_enabled = 1;
int mouse_cursor_state = CURSOR_STATE_IDLE;
int mouse_cursor_style = 0;
int mouse_cursor_size = 1;
u32 mouse_cursor_color = 0xFFFFFF;
int resolution_id = 0;
static int explorer_selected = -1;
static int explorer_selection_anchor = -1;
static unsigned char explorer_multi_selected[256];
static int explorer_last_click_index = -1;
static int explorer_last_click_ticks;
static rect_t explorer_new_file_button;
static rect_t explorer_preview_play_btn;
static int explorer_preview_pane_open = 1;
static int explorer_quick_look_open = 0;
static rect_t explorer_preview_toggle_btn;
static rect_t explorer_btn_open_app;
static rect_t explorer_ql_close_btn;
static rect_t explorer_ql_play_btn;
static rect_t explorer_ql_open_btn;
static char text_preview_buf[1024];
static char text_preview_path[128] = {0};
static int explorer_anim_ticks = 0;
static int explorer_scroll = 0;
int settings_scroll = 0;
static int settings_scroll_dragging = 0;
static int settings_scroll_drag_offset = 0;




extern unsigned int get_system_ticks(void);
static unsigned int desktop_drag_start_time = 0;
static unsigned int explorer_drag_start_time = 0;
static unsigned int unified_last_click_time = 0;
static int unified_last_click_index = -1;
static int unified_last_click_source = -1; /* 0 = desktop, 1 = explorer */
static int unified_last_click_x = -1;
static int unified_last_click_y = -1;

int desktop_drag_mx = 0;
int desktop_drag_my = 0;
int desktop_dragging = 0;
int desktop_drag_index = -1;
int desktop_drag_moved = 0;
int desktop_window_drag_start_x = 0;
int desktop_window_drag_start_y = 0;
int desktop_marquee_active = 0;
int desktop_marquee_x = 0;
int desktop_marquee_y = 0;
int desktop_marquee_mx = 0;
int desktop_marquee_my = 0;
int explorer_marquee_active = 0;
int explorer_marquee_x = 0;
int explorer_marquee_y = 0;
int explorer_marquee_mx = 0;
int explorer_marquee_my = 0;
static int explorer_dragging = 0;

static int explorer_drag_index = -1;
static int explorer_window_drag_start_x = 0;
static int explorer_window_drag_start_y = 0;
static int explorer_drag_moved = 0;
static int explorer_drop_target_index = -1;
static int explorer_drag_preview_x = 0;
static int explorer_drag_preview_y = 0;
static int window_dragging;
static int window_resizing;
static int window_drag_index = -1;
static int window_resize_index = -1;
static int window_resize_start_x;
static int window_resize_start_y;
static int window_resize_start_w;
static int window_resize_start_h;
static int post_boot_fs_loaded;
static int post_boot_fs_attempted;
static int post_boot_fs_deadline_ticks;
static const char *explorer_path = "FirstOS";
extern void kernel_post_boot_fs_load(void);

typedef enum {
    GUI_MODE_LOGIN,
    GUI_MODE_DESKTOP
} gui_mode_t;

typedef struct {
    int selected_user_index;
    char password[32];
    int password_len;
    int focused_field; /* 0 = password box, 1 = terminal pin box */
    char error[64];
    int show_hint;
    int show_recovery;
    int show_terminal_modal;
    char term_pin[16];
    int term_pin_len;
    char term_error[64];
} login_state_t;

static gui_mode_t gui_mode = GUI_MODE_LOGIN;
static login_state_t login_state = {
    0,
    "",
    0,
    0,
    "",
    0,
    0,
    0,
    "",
    0,
    ""
};

static rect_t login_card_rect;
static rect_t login_user_pills[MAX_USERS];
static rect_t login_pass_rect;
static rect_t login_btn_submit;
static rect_t login_btn_forgot;
static rect_t login_btn_hint;
static rect_t login_btn_shutdown;
static rect_t login_btn_reboot;
static rect_t login_btn_terminal;
static rect_t login_term_modal_rect;
static rect_t login_term_pin_rect;
static rect_t login_term_btn_unlock;
static rect_t login_term_btn_cancel;
static rect_t login_recovery_modal_rect;
static rect_t login_recovery_btn_close;

static int text_width(const char *text);
static char scancode_to_ascii(unsigned char key);
static int keyboard_ctrl;
static int keyboard_alt;
static int keyboard_shift;
static int keyboard_shift_l;
static int keyboard_shift_r;
static int keyboard_caps_lock;

int gui_keyboard_caps_lock(void) {
    return keyboard_caps_lock;
}

void gui_keyboard_set_caps_lock(int enabled) {
    keyboard_caps_lock = enabled ? 1 : 0;
    needs_redraw = 1;
}

int gui_is_ctrl_pressed(void) {
    return keyboard_ctrl;
}

int gui_is_alt_pressed(void) {
    return keyboard_alt;
}

int gui_is_shift_pressed(void) {
    return keyboard_shift;
}
static char gui_input_text[64] = "READY> TYPE HERE";
static int gui_input_cursor = 16;
static int cursor_blink_visible = 1;
static int cursor_blink_ticks;
static int text_input_focus = 1;
static unsigned char last_key_processed;
static unsigned char last_key_down;
static int key_repeat_ticks;
static int key_repeat_delay = 10;
static int key_repeat_rate = 2;
char editor_filename[32] = "notes.txt";

char editor_text[32768] = "";
char editor_style[32768];
int editor_current_style = 0;
int editor_length = 0;
int editor_saved = 1;
char ide_clipboard[512];
char ide_save_name[32] = "main.fl";
rect_t ide_text_area;
int ide_save_dialog_open = 0;

static int text_edit_unsaved_dialog_open = 0;
static int text_edit_closing_app_index = -1;
static rect_t text_edit_unsaved_btn_save;
static rect_t text_edit_unsaved_btn_dont_save;
static rect_t text_edit_unsaved_btn_cancel;

int editor_save_dialog_open;
int explorer_picker_mode;
char explorer_search_text[32] = "";
int explorer_search_cursor;
int explorer_search_focus;
int explorer_search_result_index = -1;
static rect_t explorer_search_clear_button;
static rect_t explorer_search_history_rect;
char explorer_search_history[5][32];
int explorer_search_history_count;
int explorer_search_history_open;
int explorer_search_history_active = -1;
char editor_save_name[32] = "notes.txt";
int editor_save_name_cursor = 9;
int editor_save_name_focus;
int editor_cursor = 0;
int editor_select_anchor = -1;
int editor_select_active = -1;
char editor_clipboard[512] = "";
int editor_mouse_selecting;
int ide_mouse_selecting;
int editor_scroll_y;
int editor_scroll_x;
rect_t editor_text_area;
app_window_t app_windows[32];

void terminal_close_active_window(void) {
    if (active_app_index >= 0 && active_app_index < 32 && app_windows[active_app_index].kind == APP_TERMINAL) {
        app_windows[active_app_index].open = 0;
        needs_redraw = 1;
        return;
    }
    app_window_t *app = app_slot(APP_TERMINAL);
    if (app) { app->open = 0; needs_redraw = 1; }
}

static void fill(rect_t r) { fb_rect(r.x, r.y, r.w, r.h, r.color); }

static int menu_hover(rect_t r) {
    return point_in_rect(ps2_mouse_x(), ps2_mouse_y(), r);
}

static int text_width(const char *text) {
    int len = 0;
    while (text && text[len]) len++;
    return len * 6;
}

static void login_set_error(const char *msg) {
    int i = 0;
    while (msg && msg[i] && i < (int)sizeof(login_state.error) - 1) {
        login_state.error[i] = msg[i];
        i++;
    }
    login_state.error[i] = 0;
}

static void login_clear_error(void) {
    login_state.error[0] = 0;
}

static void login_append_char(char *buf, int *len, int max_len, char c) {
    if (*len >= max_len - 1) return;
    if (c < 32 || c > 126) return;
    buf[*len] = c;
    (*len)++;
    buf[*len] = 0;
}

static void login_backspace(char *buf, int *len) {
    if (*len <= 0) return;
    (*len)--;
    buf[*len] = 0;
}

static char scancode_to_ascii(unsigned char key) {
    int cap = keyboard_caps_lock || keyboard_shift;
    switch (key) {
        case 0x02: return keyboard_shift ? '!' : '1';
        case 0x03: return keyboard_shift ? '@' : '2';
        case 0x04: return keyboard_shift ? '#' : '3';
        case 0x05: return keyboard_shift ? '$' : '4';
        case 0x06: return keyboard_shift ? '%' : '5';
        case 0x07: return keyboard_shift ? '^' : '6';
        case 0x08: return keyboard_shift ? '&' : '7';
        case 0x09: return keyboard_shift ? '*' : '8';
        case 0x0A: return keyboard_shift ? '(' : '9';
        case 0x0B: return keyboard_shift ? ')' : '0';
        case 0x0C: return keyboard_shift ? '_' : '-';
        case 0x0D: return keyboard_shift ? '+' : '=';
        case 0x10: return cap ? 'Q' : 'q';
        case 0x11: return cap ? 'W' : 'w';
        case 0x12: return cap ? 'E' : 'e';
        case 0x13: return cap ? 'R' : 'r';
        case 0x14: return cap ? 'T' : 't';
        case 0x15: return cap ? 'Y' : 'y';
        case 0x16: return cap ? 'U' : 'u';
        case 0x17: return cap ? 'I' : 'i';
        case 0x18: return cap ? 'O' : 'o';
        case 0x19: return cap ? 'P' : 'p';
        case 0x1A: return keyboard_shift ? '{' : '[';
        case 0x1B: return keyboard_shift ? '}' : ']';
        case 0x1E: return cap ? 'A' : 'a';
        case 0x1F: return cap ? 'S' : 's';
        case 0x20: return cap ? 'D' : 'd';
        case 0x21: return cap ? 'F' : 'f';
        case 0x22: return cap ? 'G' : 'g';
        case 0x23: return cap ? 'H' : 'h';
        case 0x24: return cap ? 'J' : 'j';
        case 0x25: return cap ? 'K' : 'k';
        case 0x26: return cap ? 'L' : 'l';
        case 0x27: return keyboard_shift ? ':' : ';';
        case 0x28: return keyboard_shift ? '"' : '\'';
        case 0x29: return keyboard_shift ? '~' : '`';
        case 0x2B: return keyboard_shift ? '|' : '\\';
        case 0x2C: return cap ? 'Z' : 'z';
        case 0x2D: return cap ? 'X' : 'x';
        case 0x2E: return cap ? 'C' : 'c';
        case 0x2F: return cap ? 'V' : 'v';
        case 0x30: return cap ? 'B' : 'b';
        case 0x31: return cap ? 'N' : 'n';
        case 0x32: return cap ? 'M' : 'm';
        case 0x33: return keyboard_shift ? '<' : ',';
        case 0x34: return keyboard_shift ? '>' : '.';
        case 0x35: return keyboard_shift ? '?' : '/';
        case 0x39: return ' ';
        default: return 0;
    }
}

static void system_reboot(void) {
    fs_sync();
    serial_write("[PWR] Reboot\n");
    unsigned char good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
    outb(0x0CF9, 0x02);
    outb(0x0CF9, 0x06);
    while(1) __asm__ volatile("cli; hlt");
}

static void system_shutdown(void) {
    fs_sync();
    serial_write("[PWR] Shutdown\n");
    sound_play_event(SOUND_EVENT_SHUTDOWN);
    sound_wait_finished(1500);
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    outw(0x8900, 0x5307);
    outw(0x8900, 0x5301);
    outw(0x8900, 0x2000);
    while(1) __asm__ volatile("cli; hlt");
}

static void login_submit(void) {
    if (user_check_password(login_state.selected_user_index, login_state.password)) {
        login_clear_error();
        const user_profile_t *u = user_get_at(login_state.selected_user_index);
        user_set_active_index(login_state.selected_user_index);
        terminal_set_active_user(u ? u->username : "root", u ? u->is_root : 1);
        gui_mode = GUI_MODE_DESKTOP;
        post_boot_fs_attempted = 1;
        post_boot_fs_deadline_ticks = (rtc_cached_second + 2) % 60;
        serial_write("[gui] user logged in, switched to desktop mode\n");
    } else {
        login_set_error("Invalid password. Please try again.");
    }
    needs_redraw = 1;
}

void gui_unlock_to_desktop(void) {
    if (gui_mode == GUI_MODE_LOGIN) {
        login_clear_error();
        user_set_active_index(0);
        terminal_set_active_user("admin", 1);
        gui_mode = GUI_MODE_DESKTOP;
        post_boot_fs_attempted = 1;
        post_boot_fs_deadline_ticks = (rtc_cached_second + 2) % 60;
        serial_write("[gui] automated unlock to desktop mode\n");
        needs_redraw = 1;
    }
}

static void login_term_submit(void) {
    if (gui_fl_eq(login_state.term_pin, "1234") || user_check_password(0, login_state.term_pin)) {
        login_state.show_terminal_modal = 0;
        login_state.term_pin[0] = 0;
        login_state.term_pin_len = 0;
        login_state.term_error[0] = 0;
        user_set_active_index(0);
        terminal_set_active_user("root", 1);
        gui_mode = GUI_MODE_DESKTOP;
        open_app(APP_TERMINAL);
        focus_app_window(APP_TERMINAL - 1);
    } else {
        gui_fl_cpy(login_state.term_error, "Incorrect PIN. Default admin PIN is 1234.", sizeof(login_state.term_error));
    }
    needs_redraw = 1;
}

static void login_handle_key(unsigned char key) {
    char c;
    if (key == 0x1D || key == 0x38 || key == 0x2A || key == 0x36 || key == 0xE0) return;

    if (key == 0x01) { /* Esc */
        if (login_state.show_recovery) login_state.show_recovery = 0;
        if (login_state.show_terminal_modal) login_state.show_terminal_modal = 0;
        login_state.show_hint = 0;
        login_state.focused_field = 0;
        needs_redraw = 1;
        return;
    }

    if (key == 0x0F) { /* Tab */
        int u_count = user_get_count();
        if (!login_state.show_terminal_modal && u_count > 0) {
            login_state.selected_user_index = (login_state.selected_user_index + 1) % u_count;
            login_state.password[0] = 0;
            login_state.password_len = 0;
            login_clear_error();
        }
        needs_redraw = 1;
        return;
    }

    if (key == 0x0E) { /* Backspace */
        if (login_state.show_terminal_modal) {
            login_backspace(login_state.term_pin, &login_state.term_pin_len);
            login_state.term_error[0] = 0;
        } else {
            login_backspace(login_state.password, &login_state.password_len);
            login_clear_error();
        }
        needs_redraw = 1;
        return;
    }

    if (key == 0x1C) { /* Enter */
        if (login_state.show_terminal_modal) {
            login_term_submit();
        } else {
            login_submit();
        }
        return;
    }

    c = scancode_to_ascii(key);
    if (!c) return;

    if (login_state.show_terminal_modal) {
        login_append_char(login_state.term_pin, &login_state.term_pin_len, (int)sizeof(login_state.term_pin), c);
        login_state.term_error[0] = 0;
    } else {
        login_append_char(login_state.password, &login_state.password_len, (int)sizeof(login_state.password), c);
        login_clear_error();
    }
    needs_redraw = 1;
}

static void login_handle_mouse(int mx, int my, int pressed_now, int left, int right) {
    (void)right;
    if (!pressed_now && !left) return;
    sound_play_event(SOUND_EVENT_CLICK);

    if (login_state.show_recovery) {
        if (point_in_rect(mx, my, login_recovery_btn_close) || !point_in_rect(mx, my, login_recovery_modal_rect)) {
            login_state.show_recovery = 0;
            needs_redraw = 1;
        }
        return;
    }

    if (login_state.show_terminal_modal) {
        if (point_in_rect(mx, my, login_term_btn_unlock)) {
            login_term_submit();
        } else if (point_in_rect(mx, my, login_term_btn_cancel) || !point_in_rect(mx, my, login_term_modal_rect)) {
            login_state.show_terminal_modal = 0;
            login_state.term_pin[0] = 0;
            login_state.term_pin_len = 0;
            login_state.term_error[0] = 0;
            login_state.focused_field = 0;
            needs_redraw = 1;
        } else if (point_in_rect(mx, my, login_term_pin_rect)) {
            login_state.focused_field = 1;
            needs_redraw = 1;
        }
        return;
    }

    int u_count = user_get_count();
    for (int i = 0; i < u_count && i < MAX_USERS; i++) {
        if (point_in_rect(mx, my, login_user_pills[i])) {
            login_state.selected_user_index = i;
            login_state.password[0] = 0;
            login_state.password_len = 0;
            login_clear_error();
            login_state.show_hint = 0;
            login_state.focused_field = 0;
            needs_redraw = 1;
            return;
        }
    }

    if (point_in_rect(mx, my, login_pass_rect)) {
        login_state.focused_field = 0;
        needs_redraw = 1;
        return;
    }

    if (point_in_rect(mx, my, login_btn_submit)) {
        login_submit();
        return;
    }

    if (point_in_rect(mx, my, login_btn_forgot)) {
        login_state.show_recovery = 1;
        needs_redraw = 1;
        return;
    }

    if (point_in_rect(mx, my, login_btn_hint)) {
        login_state.show_hint = !login_state.show_hint;
        needs_redraw = 1;
        return;
    }

    if (point_in_rect(mx, my, login_btn_terminal)) {
        login_state.show_terminal_modal = 1;
        login_state.focused_field = 1;
        login_state.term_pin[0] = 0;
        login_state.term_pin_len = 0;
        login_state.term_error[0] = 0;
        needs_redraw = 1;
        return;
    }

    if (point_in_rect(mx, my, login_btn_reboot)) {
        system_reboot();
        return;
    }

    if (point_in_rect(mx, my, login_btn_shutdown)) {
        system_shutdown();
        return;
    }
}

/* -------------------------------------------------------------
 * AVATAR RENDERING & CACHING
 * ------------------------------------------------------------- */
static u32 avatar_cache[68 * 68];
static char avatar_cache_path[128] = {0};
static int avatar_cache_valid = 0;

void gui_invalidate_avatar_cache(void) {
    avatar_cache_valid = 0;
    avatar_cache_path[0] = 0;
}

void gui_draw_avatar(int x, int y, int diameter, const user_profile_t *user) {
    if (!user) return;
    int r = diameter / 2;
    int r_sq = r * r;
    int border_r_sq = (r > 2) ? ((r - 2) * (r - 2)) : 0;

    if (user->avatar_path[0]) {
        int match = avatar_cache_valid;
        if (match) {
            int i = 0;
            while (user->avatar_path[i] && avatar_cache_path[i]) {
                if (user->avatar_path[i] != avatar_cache_path[i]) { match = 0; break; }
                i++;
            }
            if (user->avatar_path[i] != avatar_cache_path[i]) match = 0;
        }

        if (!match) {
            extern u8 *image_load(const char*, int*, int*);
            extern void image_free(u8*);
            int iw = 0, ih = 0;
            u8 *raw = image_load(user->avatar_path, &iw, &ih);
            if (raw && iw > 0 && ih > 0) {
                for (int dy = 0; dy < 68; dy++) {
                    int sy = (dy * ih) / 68;
                    for (int dx = 0; dx < 68; dx++) {
                        int sx = (dx * iw) / 68;
                        int idx = (sy * iw + sx) * 4;
                        u8 cr = raw[idx];
                        u8 cg = raw[idx + 1];
                        u8 cb = raw[idx + 2];
                        avatar_cache[dy * 68 + dx] = ((u32)cr << 16) | ((u32)cg << 8) | (u32)cb;
                    }
                }
                image_free(raw);
                int pi = 0;
                while (user->avatar_path[pi] && pi < 127) {
                    avatar_cache_path[pi] = user->avatar_path[pi];
                    pi++;
                }
                avatar_cache_path[pi] = 0;
                avatar_cache_valid = 1;
            } else {
                if (raw) image_free(raw);
                avatar_cache_valid = 0;
            }
        }
    } else {
        avatar_cache_valid = 0;
        avatar_cache_path[0] = 0;
    }

    if (avatar_cache_valid) {
        for (int dy = 0; dy < diameter; dy++) {
            int c_dy = (dy * 68) / diameter;
            int rel_y = dy - r;
            for (int dx = 0; dx < diameter; dx++) {
                int rel_x = dx - r;
                int dist_sq = rel_x * rel_x + rel_y * rel_y;
                if (dist_sq <= r_sq) {
                    if (dist_sq >= border_r_sq) {
                        fb_putpixel(x + dx, y + dy, 0xFFFFFF);
                    } else {
                        int c_dx = (dx * 68) / diameter;
                        fb_putpixel(x + dx, y + dy, avatar_cache[c_dy * 68 + c_dx]);
                    }
                }
            }
        }
    } else {
        fb_round_rect(x, y, diameter, diameter, r, user->avatar_color ? user->avatar_color : 0x2563EB);
        fb_round_rect_border(x, y, diameter, diameter, r, 0xFFFFFF);
        char init_str[2] = {user->username[0], 0};
        if (init_str[0] >= 'a' && init_str[0] <= 'z') init_str[0] -= ('a' - 'A');
        font_text(x + (diameter - 16) / 2, y + (diameter - 22) / 2, init_str, 0xFFFFFF, 3);
    }
}

void gui_logout(void) {
    serial_write("[gui] gui_logout called!\n");
    sound_play_event(SOUND_EVENT_LOGOUT);
    start_menu_open = 0;
    brand_menu_open = 0;
    main_window_open = 0;
    main_window_minimized = 1;
    for (int i = 0; i < 7; i++) app_windows[i].minimized = 1;
    login_state.password[0] = 0;
    login_state.password_len = 0;
    login_state.error[0] = 0;
    login_state.show_hint = 0;
    login_state.show_recovery = 0;
    login_state.show_terminal_modal = 0;
    login_state.focused_field = 0;
    gui_mode = GUI_MODE_LOGIN;
    needs_redraw = 1;
}

static void login_draw(void) {
    int sw = (int)fb.width;
    int sh = (int)fb.height;
    int mx = ps2_mouse_x();
    int my = ps2_mouse_y();

    /* 1. Background */
    if (settings_login_bg_mode == 0) {
        if (fb.backbuffer && sw <= MAX_SCREEN_WIDTH && sh <= MAX_SCREEN_HEIGHT) {
            fast_memcpy32(fb.backbuffer, wallpaper_cache, sw * sh);
        }
        fb_rect_alpha(0, 0, sw, sh, 0x07111E, 130);
    } else if (settings_login_bg_mode == 1) {
        fb_rect(0, 0, sw, sh, 0x091E2A);
        fb_round_rect_alpha(sw / 4, -40, sw / 2, 260, 100, 0x0E4D5E, 70);
        fb_round_rect_alpha(sw / 3, sh - 180, sw / 2, 220, 90, 0x133959, 70);
    } else if (settings_login_bg_mode == 2) {
        fb_rect(0, 0, sw, sh, 0x0C1826);
    } else {
        fb_rect(0, 0, sw, sh, 0x111827);
    }

    /* Top bar */
    fb_rect_alpha(0, 0, sw, 30, 0x000000, 70);
    draw_label(16, 9, "FirstOS", 0x94A3B8);
    rtc_time_t t; rtc_read(&t);
    char time_str[16]; time_str[0] = 0;
    char n_buf[4];
    t2d(t.hour, n_buf); gui_fl_cat(time_str, n_buf, sizeof(time_str)); gui_fl_cat(time_str, ":", sizeof(time_str));
    t2d(t.minute, n_buf); gui_fl_cat(time_str, n_buf, sizeof(time_str));
    font_text_fit_center((sw - 100) / 2, 9, time_str, 0xE2E8F0, 100);

    /* 2. Centered Login Card */
    int card_w = 420;
    int card_h = 420;
    int card_x = (sw - card_w) / 2;
    int card_y = (sh - card_h) / 2 - 10;
    login_card_rect = (rect_t){card_x, card_y, card_w, card_h, 0};

    fb_round_rect_alpha(card_x - 6, card_y - 3, card_w + 12, card_h + 12, 22, 0x000000, 60);
    fb_round_rect_alpha(card_x - 2, card_y - 1, card_w + 4, card_h + 6, 20, 0x000000, 90);
    fb_round_rect_alpha(card_x, card_y, card_w, card_h, 18, 0x111D2B, 225);
    fb_round_rect_border(card_x, card_y, card_w, card_h, 18, 0x334E6B);
    fb_rect_alpha(card_x + 18, card_y + 1, card_w - 36, 1, 0x60A5FA, 80);

    /* 3. User Switcher Pills */
    int u_count = user_get_count();
    int cur_y = card_y + 16;
    if (u_count > 1) {
        int pill_w = 88;
        int pill_gap = 8;
        int total_pills_w = u_count * pill_w + (u_count - 1) * pill_gap;
        int start_pill_x = card_x + (card_w - total_pills_w) / 2;
        for (int i = 0; i < u_count && i < MAX_USERS; i++) {
            const user_profile_t *up = user_get_at(i);
            int px = start_pill_x + i * (pill_w + pill_gap);
            login_user_pills[i] = (rect_t){px, cur_y, pill_w, 24, 0};
            int is_sel = (i == login_state.selected_user_index);
            int is_hov = point_in_rect(mx, my, login_user_pills[i]);
            u32 p_bg = is_sel ? 0x2563EB : (is_hov ? 0x22354A : 0x162638);
            u32 p_bd = is_sel ? 0x60A5FA : 0x2D435E;
            fb_round_rect_alpha(px, cur_y, pill_w, 24, 12, p_bg, 230);
            fb_round_rect_border(px, cur_y, pill_w, 24, 12, p_bd);
            font_text_fit_center(px, cur_y + 7, up->username, is_sel ? 0xFFFFFF : (is_hov ? 0xE2E8F0 : 0x94A3B8), pill_w);
        }
        cur_y += 34;
    } else {
        cur_y += 10;
    }

    /* 4. User Avatar (diameter 68px) */
    const user_profile_t *active_u = user_get_at(login_state.selected_user_index);
    if (!active_u) active_u = user_get_at(0);
    int avatar_d = 68;
    int avatar_x = card_x + (card_w - avatar_d) / 2;
    int avatar_y = cur_y;
    gui_draw_avatar(avatar_x, avatar_y, avatar_d, active_u);
    cur_y += avatar_d + 10;

    /* 5. Username & Role Badge */
    font_text_fit_center(card_x + 20, cur_y, active_u->display_name, 0xFFFFFF, card_w - 40);
    cur_y += 20;

    const char *role_text = active_u->is_root ? "Super User" : (login_state.selected_user_index == 2 ? "Guest" : "Standard User");
    u32 role_col = active_u->is_root ? 0x60A5FA : (login_state.selected_user_index == 2 ? 0xF59E0B : 0x10B981);
    int rw = 96;
    int rx = card_x + (card_w - rw) / 2;
    fb_round_rect_alpha(rx, cur_y, rw, 18, 9, 0x162638, 200);
    fb_round_rect_border(rx, cur_y, rw, 18, 9, role_col);
    font_text_fit_center(rx, cur_y + 5, role_text, role_col, rw);
    cur_y += 28;

    /* 6. Password Textbox */
    int tb_w = 280;
    int tb_h = 36;
    int tb_x = card_x + (card_w - tb_w) / 2;
    login_pass_rect = (rect_t){tb_x, cur_y, tb_w, tb_h, 0};
    int pass_focused = (login_state.focused_field == 0);
    u32 tb_bg = 0x0D1722;
    u32 tb_border = pass_focused ? 0x3B82F6 : 0x334E6B;
    fb_round_rect_alpha(tb_x, cur_y, tb_w, tb_h, 8, tb_bg, 240);
    fb_round_rect_border(tb_x, cur_y, tb_w, tb_h, 8, tb_border);

    if (login_state.password_len == 0) {
        font_text(tb_x + 14, cur_y + 13, "Enter password...", 0x64748B, 1);
    } else {
        char bullets[34]; int bi;
        for (bi = 0; bi < login_state.password_len && bi < 32; bi++) bullets[bi] = '*';
        bullets[bi] = 0;
        font_text(tb_x + 14, cur_y + 13, bullets, 0xFFFFFF, 1);
        if (pass_focused) {
            fb_rect(tb_x + 14 + bi * 6 + 2, cur_y + 10, 2, 16, 0x60A5FA);
        }
    }
    if (keyboard_caps_lock) {
        fb_round_rect(tb_x + tb_w - 50, cur_y + 8, 42, 20, 5, 0x2563EB);
        font_text_fit_center(tb_x + tb_w - 50, cur_y + 13, "CAPS", 0xFFFFFF, 42);
    }
    cur_y += tb_h + 12;

    /* 7. Primary [ Log In ] Button */
    login_btn_submit = (rect_t){tb_x, cur_y, tb_w, 36, 0};
    int submit_hov = point_in_rect(mx, my, login_btn_submit);
    u32 s_bg = submit_hov ? 0x3B82F6 : 0x2563EB;
    fb_round_rect(tb_x, cur_y, tb_w, 36, 8, s_bg);
    fb_round_rect_border(tb_x, cur_y, tb_w, 36, 8, 0x60A5FA);
    font_text_fit_center(tb_x, cur_y + 13, "Log In", 0xFFFFFF, tb_w);
    cur_y += 36 + 12;

    /* 8. Secondary Actions: [Forgot Password?] and [(i) Hint] */
    int btn_half_w = (tb_w - 10) / 2;
    login_btn_forgot = (rect_t){tb_x, cur_y, btn_half_w, 22, 0};
    login_btn_hint = (rect_t){tb_x + btn_half_w + 10, cur_y, btn_half_w, 22, 0};

    int forgot_hov = point_in_rect(mx, my, login_btn_forgot);
    int hint_hov = point_in_rect(mx, my, login_btn_hint);

    font_text_fit_center(login_btn_forgot.x, cur_y + 6, "Forgot password?", forgot_hov ? 0xFFFFFF : 0x94A3B8, btn_half_w);
    font_text_fit_center(login_btn_hint.x, cur_y + 6, login_state.show_hint ? "(i) Hide Hint" : "(i) Show Hint", hint_hov ? 0x60A5FA : 0x38BDF8, btn_half_w);
    cur_y += 24;

    /* 9. Feedback / Error / Hint area */
    if (login_state.error[0]) {
        fb_round_rect_alpha(tb_x, cur_y, tb_w, 24, 6, 0x451A1A, 220);
        fb_round_rect_border(tb_x, cur_y, tb_w, 24, 6, 0xEF4444);
        font_text_fit_center(tb_x, cur_y + 8, login_state.error, 0xFCA5A5, tb_w);
    } else if (login_state.show_hint) {
        fb_round_rect_alpha(tb_x, cur_y, tb_w, 24, 6, 0x0E2E42, 220);
        fb_round_rect_border(tb_x, cur_y, tb_w, 24, 6, 0x38BDF8);
        font_text_fit_center(tb_x, cur_y + 8, active_u->hint, 0xBAE6FD, tb_w);
    }

    /* 10. Right-Bottom Corner Controls */
    int rb_y = sh - 46;
    login_btn_shutdown = (rect_t){sw - 110, rb_y, 96, 32, 0};
    login_btn_reboot   = (rect_t){sw - 110 - 10 - 96, rb_y, 96, 32, 0};
    login_btn_terminal = (rect_t){sw - 110 - 10 - 96 - 10 - 106, rb_y, 106, 32, 0};

    int shut_hov = point_in_rect(mx, my, login_btn_shutdown);
    int reb_hov  = point_in_rect(mx, my, login_btn_reboot);
    int term_hov = point_in_rect(mx, my, login_btn_terminal);

    fb_round_rect_alpha(login_btn_terminal.x, rb_y, login_btn_terminal.w, 32, 16, term_hov ? 0x1E354A : 0x112131, 220);
    fb_round_rect_border(login_btn_terminal.x, rb_y, login_btn_terminal.w, 32, 16, term_hov ? 0x60A5FA : 0x2D435E);
    font_icon(login_btn_terminal.x + 10, rb_y + 8, ICON_TERMINAL, term_hov ? 0xFFFFFF : 0x94A3B8);
    draw_label(login_btn_terminal.x + 32, rb_y + 11, "Terminal", term_hov ? 0xFFFFFF : 0xE2E8F0);

    fb_round_rect_alpha(login_btn_reboot.x, rb_y, login_btn_reboot.w, 32, 16, reb_hov ? 0x1E354A : 0x112131, 220);
    fb_round_rect_border(login_btn_reboot.x, rb_y, login_btn_reboot.w, 32, 16, reb_hov ? 0xF59E0B : 0x2D435E);
    font_icon(login_btn_reboot.x + 10, rb_y + 8, ICON_RESTART, reb_hov ? 0xFFFFFF : 0x94A3B8);
    draw_label(login_btn_reboot.x + 32, rb_y + 11, "Reboot", reb_hov ? 0xFFFFFF : 0xE2E8F0);

    fb_round_rect_alpha(login_btn_shutdown.x, rb_y, login_btn_shutdown.w, 32, 16, shut_hov ? 0x4A1E24 : 0x112131, 220);
    fb_round_rect_border(login_btn_shutdown.x, rb_y, login_btn_shutdown.w, 32, 16, shut_hov ? 0xEF4444 : 0x2D435E);
    font_icon(login_btn_shutdown.x + 10, rb_y + 8, ICON_POWER, shut_hov ? 0xFFFFFF : 0x94A3B8);
    draw_label(login_btn_shutdown.x + 32, rb_y + 11, "Shutdown", shut_hov ? 0xFFFFFF : 0xE2E8F0);

    /* 11. Overlays (Recovery Modal & Terminal Unlock Modal) */
    if (login_state.show_recovery) {
        fb_rect_alpha(0, 0, sw, sh, 0x000000, 160);
        int mw = 380, mh = 200;
        int m_x = (sw - mw) / 2, m_y = (sh - mh) / 2;
        login_recovery_modal_rect = (rect_t){m_x, m_y, mw, mh, 0};
        fb_round_rect_alpha(m_x, m_y, mw, mh, 16, 0x152232, 250);
        fb_round_rect_border(m_x, m_y, mw, mh, 16, 0x476A8F);
        draw_label(m_x + 24, m_y + 20, "Password Recovery", 0xFFFFFF);
        font_text_fit(m_x + 24, m_y + 54, "Administrator PIN can be viewed by clicking", 0x94A3B8, mw - 48);
        font_text_fit(m_x + 24, m_y + 70, "the '(i) Hint' button on the login screen.", 0x94A3B8, mw - 48);
        font_text_fit(m_x + 24, m_y + 96, "You can also launch the Super User Terminal", 0x94A3B8, mw - 48);
        font_text_fit(m_x + 24, m_y + 112, "at the bottom-right to reset user accounts.", 0x94A3B8, mw - 48);

        login_recovery_btn_close = (rect_t){m_x + mw - 110, m_y + mh - 44, 86, 28, 0};
        int c_hov = point_in_rect(mx, my, login_recovery_btn_close);
        fb_round_rect(login_recovery_btn_close.x, login_recovery_btn_close.y, 86, 28, 6, c_hov ? 0x3B82F6 : 0x2563EB);
        font_text_fit_center(login_recovery_btn_close.x, login_recovery_btn_close.y + 9, "Close", 0xFFFFFF, 86);
    } else if (login_state.show_terminal_modal) {
        fb_rect_alpha(0, 0, sw, sh, 0x000000, 160);
        int mw = 400, mh = 220;
        int m_x = (sw - mw) / 2, m_y = (sh - mh) / 2;
        login_term_modal_rect = (rect_t){m_x, m_y, mw, mh, 0};
        fb_round_rect_alpha(m_x, m_y, mw, mh, 16, 0x152232, 250);
        fb_round_rect_border(m_x, m_y, mw, mh, 16, 0x476A8F);
        draw_label(m_x + 24, m_y + 20, "Root Terminal Access", 0xFFFFFF);
        font_text_fit(m_x + 24, m_y + 48, "Enter Administrator PIN to unlock root console:", 0x94A3B8, mw - 48);

        login_term_pin_rect = (rect_t){m_x + 24, m_y + 76, mw - 48, 32, 0};
        fb_round_rect_alpha(login_term_pin_rect.x, login_term_pin_rect.y, login_term_pin_rect.w, 32, 6, 0x0D1722, 240);
        fb_round_rect_border(login_term_pin_rect.x, login_term_pin_rect.y, login_term_pin_rect.w, 32, 6, 0x3B82F6);
        if (login_state.term_pin_len == 0) {
            font_text(login_term_pin_rect.x + 12, login_term_pin_rect.y + 11, "Enter PIN (default: 1234)...", 0x64748B, 1);
        } else {
            char bullets[18]; int bi;
            for (bi = 0; bi < login_state.term_pin_len && bi < 16; bi++) bullets[bi] = '*';
            bullets[bi] = 0;
            font_text(login_term_pin_rect.x + 12, login_term_pin_rect.y + 11, bullets, 0xFFFFFF, 1);
        }

        if (login_state.term_error[0]) {
            font_text_fit(m_x + 24, m_y + 120, login_state.term_error, 0xEF4444, mw - 48);
        }

        login_term_btn_unlock = (rect_t){m_x + mw - 190, m_y + mh - 44, 90, 28, 0};
        login_term_btn_cancel = (rect_t){m_x + mw - 90,  m_y + mh - 44, 66, 28, 0};

        int u_hov = point_in_rect(mx, my, login_term_btn_unlock);
        int can_hov = point_in_rect(mx, my, login_term_btn_cancel);

        fb_round_rect(login_term_btn_unlock.x, login_term_btn_unlock.y, 90, 28, 6, u_hov ? 0x3B82F6 : 0x2563EB);
        font_text_fit_center(login_term_btn_unlock.x, login_term_btn_unlock.y + 9, "Unlock", 0xFFFFFF, 90);

        fb_round_rect(login_term_btn_cancel.x, login_term_btn_cancel.y, 66, 28, 6, can_hov ? 0x334155 : 0x1E293B);
        font_text_fit_center(login_term_btn_cancel.x, login_term_btn_cancel.y + 9, "Cancel", 0x94A3B8, 66);
    }
}

static void wrapped_text_padded(int x, int y, const char *text, u32 color,
                                int max_width, int max_lines,
                                int pad_x, int pad_y) {
    int chars_per_line;
    int usable_width;
    int i = 0;
    int line;

    if (!text || max_width <= 0 || max_lines <= 0) return;
    if (pad_x < 0) pad_x = 0;
    if (pad_y < 0) pad_y = 0;

    usable_width = max_width - pad_x * 2;
    if (usable_width <= 0) return;

    chars_per_line = usable_width / 6;
    if (chars_per_line <= 0) return;

    i = 0;
    line = 0;

    while (text[i] && line < max_lines) {
        int start;
        int end;
        int count;
        int last_space;
        char buf[128];
        int j;

        while (text[i] == ' ') i++;
        if (!text[i]) break;

        if (text[i] == '\n') {
            line++;
            i++;
            continue;
        }

        start = i;
        end = i;
        count = 0;
        last_space = -1;

        while (text[end] && text[end] != '\n' && count < chars_per_line) {
            if (text[end] == ' ') last_space = end;
            end++;
            count++;
        }

        if (text[end] && text[end] != '\n' && count >= chars_per_line && last_space >= start) {
            end = last_space;
            count = end - start;
        }

        if (count <= 0) {
            if (text[end] == '\n') {
                i = end + 1;
                line++;
                continue;
            }
            break;
        }

        if (count > 127) count = 127;

        for (j = 0; j < count; j++) {
            buf[j] = text[start + j];
        }
        buf[count] = 0;

        draw_label(x + pad_x, y + pad_y + line * 12, buf, color);
        line++;

        i = end;

        while (text[i] == ' ') i++;
        if (text[i] == '\n') i++;
    }
}

static void wrapped_text(int x, int y, const char *text, u32 color, int max_width, int max_lines) {
    wrapped_text_padded(x, y, text, color, max_width, max_lines, 0, 0);
}

static void draw_label_box(rect_t box, const char *text, u32 text_color, int pad_x, int pad_y, int max_lines) {
    int inner_w;
    if (pad_x < 0) pad_x = 0;
    if (pad_y < 0) pad_y = 0;
    inner_w = box.w - pad_x * 2;
    if (inner_w <= 0 || box.h <= pad_y * 2) return;
    wrapped_text_padded(box.x, box.y, text, text_color, box.w, max_lines, pad_x, pad_y);
}


void font_text_fit(int x, int y, const char *text, u32 color, int max_width);
void font_text_fit_center(int x, int y, const char *text, u32 color, int max_width);
static const char *app_name_from_kind(int kind);
app_window_t* app_slot(int kind);
void open_app(int kind);
void focus_app_window(int index);
app_window_t* active_app_window(void);
static int explorer_nav_back(void);
static void explorer_keyboard_open_selected(void);
static void explorer_keyboard_move_selection(int delta);
static void handle_mouse_wheel(void);

























static int keyboard_extended;
static int shortcuts_enabled = 1;
static int alt_tab_armed;
static int alt_tab_preview_open;
static int alt_tab_preview_index = -1;
static int alt_tab_preview_anim = 0;
static rect_t alt_tab_preview_card;
static int taskbar_hover_preview_index = -1;
static rect_t taskbar_hover_preview_card;
static rect_t taskbar_hover_preview_close;

static int app_icon_from_kind(int kind) {
    if (kind == APP_TERMINAL) return ICON_DISPLAY;
    if (kind == APP_FILES) return ICON_FILES;
    if (kind == APP_DISPLAY) return ICON_DISPLAY;
    if (kind == APP_EXPLORER) return ICON_FOLDER;
    if (kind == APP_SETTINGS) return ICON_SETTINGS;
    return ICON_FILE;
}

static int handle_system_shortcut(unsigned char key) {
    int i = 0;
    if (!shortcuts_enabled) return 0;
    if (key == 0x01) {
        start_menu_open = 0;
        brand_menu_open = 0;
        app_menu_open = 0;
        file_menu_open = 0;
        edit_menu_open = 0;
        clock_menu_open = 0;
        volume_menu_open = 0;
        weather_popover_open = 0;
        fps_dropdown_open = 0;
        notification_center_open = 0;
        wifi_menu_open = 0;
        desktop_context_open = 0;
        if (spotlight_open) {
            if (spotlight_is_in_dropdown()) {
                spotlight_close_dropdown();
            } else {
                spotlight_open = 0;
            }
        }
        launcher_open = 0;
        needs_redraw = 1;
        return 1;
    }
    if (wifi_menu_open && !keyboard_alt && !keyboard_ctrl) {
        char asc = scancode_to_ascii(key);
        if (asc == 'd' || asc == 'D') {
            net_disconnect();
            current_wifi_network = -1;
            wifi_menu_open = 0;
            needs_redraw = 1;
            return 1;
        }
        if (asc >= '1' && asc <= '5') {
            int ap_idx = asc - '1';
            const wifi_ap_t *ap = net_get_ap(ap_idx);
            if (ap) {
                if (ap->encrypted) {
                    if (ap->expected_pass[0] != 0) {
                        int l = 0;
                        while (ap->expected_pass[l]) l++;
                        if (l >= 8 && net_connect(ap_idx, ap->expected_pass)) {
                            current_wifi_network = ap_idx;
                        } else {
                            wifi_password_modal_open_for_ap(ap_idx);
                        }
                    } else {
                        wifi_password_modal_open_for_ap(ap_idx);
                    }
                } else {
                    net_connect(ap_idx, "");
                    current_wifi_network = ap_idx;
                    wifi_menu_open = 0;
                }
                needs_redraw = 1;
                return 1;
            }
        }
    }
    if (keybindings_handle(key, scancode_to_ascii(key), keyboard_ctrl, keyboard_alt, keyboard_shift)) {
        return 1;
    }
    if (keyboard_alt && key == 0x0F && alt_tab_armed) {
        int next = -1;
        int start = alt_tab_preview_open && alt_tab_preview_index >= 0 ? alt_tab_preview_index : active_app_index;
        if (start < 0) start = z_order[31];
        for (i = 1; i <= 31; i++) {
            int p;
            int idx = -1;
            for (p = 0; p < 32; p++) if (z_order[p] == start) { idx = z_order[(p - i + 32) % 32]; break; }
            if (idx >= 0 && app_windows[idx].open && !app_windows[idx].minimized) { next = idx; break; }
        }
        if (next >= 0) {
            alt_tab_preview_open = 1;
            alt_tab_preview_index = next;
            alt_tab_preview_anim = 0;
            needs_redraw = 1;
        }
        return 1;
    }
    if (keyboard_alt && key == 0x2E) {
        app_window_t *app = active_app_window();
        if (app) { begin_window_transition((int)(app - app_windows), 1); app->minimized = 1; needs_redraw = 1; }
        return 1;
    }
    if (keyboard_alt && key == 0x13) {
        open_app(APP_SETTINGS);
        needs_redraw = 1;
        return 1;
    }
    if (keyboard_alt && key == 0x14) { // Alt+T (Option+T)
        app_window_t *app = active_app_window();
        if (app && app->kind == APP_IDE) {
            extern ide_tab_t ide_tabs[4];
            extern void ide_switch_tab(int);
            int ti;
            for (ti = 0; ti < 4; ti++) {
                if (!ide_tabs[ti].text[0] && !ide_tabs[ti].filename[0]) {
                    ide_switch_tab(ti); break;
                }
            }
            needs_redraw = 1;
        }
        return 1;
    }
    if (keyboard_alt && key == 0x11) { // Alt+W (Option+W)
        app_window_t *app = active_app_window();
        if (app && app->kind == APP_IDE) {
            extern int ide_active_tab;
            extern void ide_close_tab(int);
            ide_close_tab(ide_active_tab);
            needs_redraw = 1;
        }
        return 1;
    }
    /* Configurable Hotkey to toggle Spotlight */
    int hotkey_match = 1;
    if (spotlight_hotkey_ctrl && !keyboard_ctrl) hotkey_match = 0;
    if (spotlight_hotkey_alt && !keyboard_alt) hotkey_match = 0;
    if (spotlight_hotkey_shift && !keyboard_shift) hotkey_match = 0;
    if (scancode_to_ascii(key) != spotlight_hotkey_char && key != 0x39 /* fallback Space */) hotkey_match = 0;

    if (hotkey_match && key > 0) {
        if (spotlight_open && spotlight_is_in_dropdown()) {
            spotlight_close_dropdown();
        } else {
            spotlight_open = !spotlight_open;
            if (spotlight_open) {
                spotlight_close_dropdown();
                spotlight_qlen = 0; spotlight_query[0] = 0;
                spotlight_search("");
            } else {
                spotlight_close_dropdown();
            }
        }
        needs_redraw = 1;
        return 1;
    }
    if (keyboard_ctrl && key == 0x18) {
        app_window_t *app = active_app_window();
        if (app && app->kind == APP_TEXT_EDIT) { file_dialog_show(APP_TEXT_EDIT, FILE_DIALOG_OPEN, "", "/Desktop"); needs_redraw = 1; return 1; }
        else if (app && app->kind == APP_IDE) { file_dialog_show(APP_IDE, FILE_DIALOG_OPEN, "", "/Desktop"); needs_redraw = 1; return 1; }
        else if (app && app->kind == APP_MEDIA_PLAYER) { file_dialog_show(APP_MEDIA_PLAYER, FILE_DIALOG_IMPORT, "", "/Audio"); needs_redraw = 1; return 1; }
    }
    if (keyboard_ctrl && key == 0x1F) {
        app_window_t *app = active_app_window();
        if (app && app->kind == APP_TEXT_EDIT) { file_dialog_show(APP_TEXT_EDIT, FILE_DIALOG_SAVE, "untitled.txt", "/Desktop"); needs_redraw = 1; return 1; }
        else if (app && app->kind == APP_IDE) { file_dialog_show(APP_IDE, FILE_DIALOG_SAVE, "script.fl", "/Desktop"); needs_redraw = 1; return 1; }
        return 1;
    }
    if (keyboard_ctrl && key == 0x11) {
        app_window_t *app = active_app_window();
        if (app) { app->open = 0; if (app->kind == APP_PREVIEW) preview_close(); needs_redraw = 1; return 1; }
    }
    if (keyboard_ctrl && key == 0x31) {
        app_window_t *app = active_app_window();
        if (app) { open_new_window(app->kind); needs_redraw = 1; }
        return 1;
    }
    if (keyboard_ctrl && key == 0x2E) {
        app_window_t *app = active_app_window();
        if (app && app->kind == APP_TEXT_EDIT) { text_edit_copy_selection(); needs_redraw = 1; return 1; }
        else if (app && app->kind == APP_IDE) { extern void ide_copy_selection(void); ide_copy_selection(); needs_redraw = 1; return 1; }
        else if (app && app->kind == APP_TERMINAL) {
            extern int terminal_has_selection(void);
            extern void terminal_copy(void);
            if (terminal_has_selection()) {
                terminal_copy();
                needs_redraw = 1;
                return 1;
            }
        }
        else if (app && app->kind == APP_EXPLORER) {
            explorer_copy_selected_file(0);
            needs_redraw = 1;
            return 1;
        } else if (!app || desktop_selected >= 0) {
            if (desktop_selected >= 0 && desktop_selected < desktop_item_count && desktop_items[desktop_selected].used) {
                explorer_file_clipboard_count = 1;
                desktop_copy_name(explorer_file_clipboard_path[0], sizeof(explorer_file_clipboard_path[0]), desktop_items[desktop_selected].path);
                desktop_copy_name(explorer_file_clipboard_name[0], sizeof(explorer_file_clipboard_name[0]), desktop_items[desktop_selected].name);
                explorer_file_clipboard_is_dir[0] = desktop_items[desktop_selected].is_dir ? 1 : 0;
                explorer_file_clipboard_set = 1;
                explorer_file_clipboard_cut = 0;
                needs_redraw = 1;
                return 1;
            }
        }
    }
    if (keyboard_ctrl && key == 0x2D) {
        app_window_t *app = active_app_window();
        if (app && app->kind == APP_TEXT_EDIT) { text_edit_cut_selection(); needs_redraw = 1; return 1; }
        else if (app && app->kind == APP_IDE) { extern void ide_cut_selection(void); ide_cut_selection(); needs_redraw = 1; return 1; }
        else if (app && app->kind == APP_EXPLORER) {
            explorer_copy_selected_file(1);
            needs_redraw = 1;
            return 1;
        } else if (!app || desktop_selected >= 0) {
            if (desktop_selected >= 0 && desktop_selected < desktop_item_count && desktop_items[desktop_selected].used) {
                explorer_file_clipboard_count = 1;
                desktop_copy_name(explorer_file_clipboard_path[0], sizeof(explorer_file_clipboard_path[0]), desktop_items[desktop_selected].path);
                desktop_copy_name(explorer_file_clipboard_name[0], sizeof(explorer_file_clipboard_name[0]), desktop_items[desktop_selected].name);
                explorer_file_clipboard_is_dir[0] = desktop_items[desktop_selected].is_dir ? 1 : 0;
                explorer_file_clipboard_set = 1;
                explorer_file_clipboard_cut = 1;
                needs_redraw = 1;
                return 1;
            }
        }
    }
    if (keyboard_ctrl && key == 0x2F) {
        app_window_t *app = active_app_window();
        if (app && app->kind == APP_TEXT_EDIT) { text_edit_paste_clipboard(); needs_redraw = 1; return 1; }
        else if (app && app->kind == APP_IDE) { extern void ide_paste_clipboard(void); ide_paste_clipboard(); needs_redraw = 1; return 1; }
        else if (app && app->kind == APP_TERMINAL) { extern void terminal_paste(void); terminal_paste(); needs_redraw = 1; return 1; }
        else if (app && app->kind == APP_EXPLORER) {
            explorer_paste_file();
            needs_redraw = 1;
            return 1;
        } else if (!app || desktop_selected >= 0) {
            desktop_paste_file();
            needs_redraw = 1;
            return 1;
        }
    }
    if (keyboard_ctrl && key == 0x2C) {
        app_window_t *app = active_app_window();
        if (app && app->kind == APP_TEXT_EDIT) { text_edit_select_all(); needs_redraw = 1; return 1; }
        else if (app && app->kind == APP_IDE) { extern void ide_select_all(void); ide_select_all(); needs_redraw = 1; return 1; }
    }
    if (key == 0xE0 || keyboard_extended) {
        app_window_t *app = active_app_window();
        if (app && app->kind == APP_EXPLORER) {
            if ((key & 0x80) == 0) {
                if (key == 0x48) { explorer_keyboard_move_selection(-1); keyboard_extended = 0; return 1; }
                if (key == 0x50) { explorer_keyboard_move_selection(1); keyboard_extended = 0; return 1; }
                if (key == 0x4B) { explorer_nav_back(); keyboard_extended = 0; return 1; }
                if (key == 0x4D) { explorer_keyboard_open_selected(); keyboard_extended = 0; return 1; }
            }
        }
    }
    return 0;
}

static void draw_alt_tab_preview(void) {
    int box_w = 300;
    int box_h = 148;
    int anim = alt_tab_preview_anim;
    int grow = (6 - anim) > 0 ? (6 - anim) * 6 : 0;
    int x = ((int)fb.width - box_w) / 2;
    int y = ((int)fb.height - box_h) / 2 - 28 + (6 - anim > 0 ? 6 - anim : 0) * 3;
    int preview_x;
    int preview_y;
    int preview_w;
    int preview_h;
    const char *name;
    app_window_t *app;
    if (!alt_tab_preview_open || alt_tab_preview_index < 0 || alt_tab_preview_index >= 32) return;
    if (anim < 1) anim = 1;
    app = &app_windows[alt_tab_preview_index];
    name = app_name_from_kind(app->kind);
    alt_tab_preview_card = (rect_t){x, y, box_w, box_h, 0};
    fb_rect(x + 16, y + 16, box_w - grow, box_h - grow / 2, shadow);
    fb_rect(x, y, box_w, box_h, 0xDCE4EE);
    fb_rect(x + 2, y + 2, box_w - 4, box_h - 4, 0xEEF3F8);
    outline(x, y, box_w, box_h, 0x5D738E);
    fb_rect(x + 18, y + 16, box_w - 36, 24, 0x315A8A);
    font_text_fit_center(x + 18, y + 24, "ALT + TAB", 0xFFFFFF, box_w - 36);
    preview_x = x + 24;
    preview_y = y + 52;
    preview_w = 116;
    preview_h = 68;
    fb_rect(preview_x, preview_y, preview_w, preview_h, 0xFAFCFE);
    outline(preview_x, preview_y, preview_w, preview_h, 0x8EA4BA);
    fb_rect(preview_x + 4, preview_y + 4, preview_w - 8, 14, app->title.color ? app->title.color : 0x5D738E);
    fb_rect(preview_x + 8, preview_y + 24, preview_w - 16, preview_h - 32, app->kind == APP_SETTINGS ? 0xE7EBF4 : app->kind == APP_EXPLORER ? 0xECE6D1 : app->kind == APP_TEXT_EDIT ? 0xFFFDF8 : 0xF3F6FA);
    if (app->kind == APP_TEXT_EDIT) {
        fb_rect(preview_x + 14, preview_y + 30, preview_w - 28, 8, 0xEADCCB);
        fb_rect(preview_x + 14, preview_y + 42, preview_w - 34, 6, 0xD9C0A4);
        fb_rect(preview_x + 14, preview_y + 52, preview_w - 22, 6, 0xD9C0A4);
    } else if (app->kind == APP_SETTINGS) {
        fb_rect(preview_x + 14, preview_y + 28, preview_w - 28, 10, 0xCFDBE8);
        fb_rect(preview_x + 14, preview_y + 44, preview_w - 40, 8, 0xB6C8DA);
        fb_rect(preview_x + 14, preview_y + 56, preview_w - 34, 6, 0x9DB4CB);
    } else if (app->kind == APP_EXPLORER) {
        fb_rect(preview_x + 12, preview_y + 28, 28, 22, 0xD9C58F);
        fb_rect(preview_x + 44, preview_y + 28, 28, 22, 0xB7C6D7);
        fb_rect(preview_x + 76, preview_y + 28, 26, 22, 0xD8E2EA);
        fb_rect(preview_x + 12, preview_y + 54, preview_w - 24, 6, 0xB08F58);
    } else {
        fb_rect(preview_x + 14, preview_y + 30, preview_w - 28, 10, 0xD7E2EE);
        fb_rect(preview_x + 14, preview_y + 46, preview_w - 34, 8, 0xB9CBDE);
        fb_rect(preview_x + 14, preview_y + 58, preview_w - 22, 4, 0x8EABC8);
    }
    fb_rect(x + 154, y + 56, box_w - 178, 22, 0xF9FBFD);
    font_text_fit(x + 166, y + 63, name, 0x20344E, box_w - 198);
    wrapped_text_padded(x + 166, y + 84, "Live mini preview", 0x607284, box_w - 198, 2, 2, 0);
    fb_rect(x + 154, y + 98, box_w - 178, 10, 0xC7D6E5);
    fb_rect(x + 158, y + 101, (box_w - 186) * anim / 6, 4, 0x6C92C0);
    wrapped_text_padded(x + 24, y + 128, "Release ALT or click to switch", 0x506479, box_w - 48, 2, 2, 0);
}

static void draw_taskbar_hover_preview(void) {
    int box_w = 220;
    int box_h = 120;
    int x;
    int y;
    int preview_x;
    int preview_y;
    int preview_w;
    int preview_h;
    const char *name;
    app_window_t *app;
    if (taskbar_hover_preview_index < 0 || taskbar_hover_preview_index >= 32) return;
    app = &app_windows[taskbar_hover_preview_index];
    if (!app->open) return;
    int base_chip_x = (start_button.w > 0) ? (start_button.x + start_button.w + 12) : 10;
    x = base_chip_x + taskbar_hover_preview_index * 36 - 92;
    if (x < 12) x = 12;
    if (x + box_w > (int)fb.width - 12) x = (int)fb.width - box_w - 12;
    y = dock_rect.y - box_h - 10;
    taskbar_hover_preview_card = (rect_t){x, y, box_w, box_h, 0};
    taskbar_hover_preview_close = (rect_t){x + box_w - 24, y + 12, 14, 14, 0};
    fb_rect(x + 8, y + 8, box_w, box_h, 0x6A7A8A);
    fb_rect(x, y, box_w, box_h, 0xE6EDF5);
    fb_rect(x + 2, y + 2, box_w - 4, box_h - 4, 0xF7FAFD);
    outline(x, y, box_w, box_h, 0x5D738E);
    fb_rect(x + 14, y + 12, box_w - 28, 18, app->title.color ? app->title.color : 0x5D738E);
    fb_rect(taskbar_hover_preview_close.x, taskbar_hover_preview_close.y, taskbar_hover_preview_close.w, taskbar_hover_preview_close.h, 0xC86E6E);
    outline(taskbar_hover_preview_close.x, taskbar_hover_preview_close.y, taskbar_hover_preview_close.w, taskbar_hover_preview_close.h, 0x7D3838);
    draw_label(taskbar_hover_preview_close.x + 4, taskbar_hover_preview_close.y + 4, "X", 0xFFFFFF);
    font_text_fit(x + 22, y + 17, app_name_from_kind(app->kind), 0xFFFFFF, box_w - 52);
    preview_x = x + 16;
    preview_y = y + 40;
    preview_w = box_w - 32;
    preview_h = 52;
    fb_rect(preview_x, preview_y, preview_w, preview_h, 0xFAFCFE);
    outline(preview_x, preview_y, preview_w, preview_h, 0x8EA4BA);
    fb_rect(preview_x + 4, preview_y + 4, preview_w - 8, 10, app->title.color ? app->title.color : 0x6C92C0);
    fb_rect(preview_x + 8, preview_y + 20, preview_w - 16, preview_h - 28, app->kind == APP_SETTINGS ? 0xE7EBF4 : app->kind == APP_EXPLORER ? 0xECE6D1 : app->kind == APP_TEXT_EDIT ? 0xFFFDF8 : 0xF3F6FA);
    if (app->kind == APP_TEXT_EDIT) {
        fb_rect(preview_x + 16, preview_y + 26, preview_w - 32, 6, 0xEADCCB);
        fb_rect(preview_x + 16, preview_y + 36, preview_w - 42, 4, 0xD9C0A4);
    } else if (app->kind == APP_SETTINGS) {
        fb_rect(preview_x + 16, preview_y + 24, preview_w - 32, 8, 0xCFDBE8);
        fb_rect(preview_x + 16, preview_y + 38, preview_w - 54, 5, 0x9DB4CB);
    } else if (app->kind == APP_EXPLORER) {
        fb_rect(preview_x + 14, preview_y + 24, 34, 18, 0xD9C58F);
        fb_rect(preview_x + 54, preview_y + 24, 34, 18, 0xB7C6D7);
        fb_rect(preview_x + 94, preview_y + 24, preview_w - 108, 18, 0xD8E2EA);
    } else {
        fb_rect(preview_x + 16, preview_y + 24, preview_w - 32, 8, 0xD7E2EE);
        fb_rect(preview_x + 16, preview_y + 38, preview_w - 46, 5, 0xB9CBDE);
    }
    name = app->kind == APP_EXPLORER ? explorer_path : app_name_from_kind(app->kind);
    font_text_fit(x + 18, y + 100, name ? name : app_name_from_kind(app->kind), 0x4A5C70, box_w - 36);
}

static void trigger_explorer_save(void) {
    int len = 0;
    while (editor_save_name[len]) len++;
    if (len > 0 && fs_ready()) {
        if (explorer_picker_mode == 1 && editor_save_dialog_open && !ide_save_dialog_open) {
            if (!is_text_or_doc_file(editor_save_name)) {
                int sn = 0; while (editor_save_name[sn]) sn++;
                if (sn + 4 < (int)sizeof(editor_save_name)) {
                    editor_save_name[sn++] = '.';
                    editor_save_name[sn++] = 't';
                    editor_save_name[sn++] = 'x';
                    editor_save_name[sn++] = 't';
                    editor_save_name[sn] = 0;
                }
            }
        }
        char full_path[64];
        const char *current = explorer_current_path();
        const char *base = current;
        int a = 0;
        int b = 0;
        int created;
        if (!base || !base[0] || explorer_is_virtual_path(base)) base = "/Desktop";
        else if (base[0] != '/') base = "/Desktop";
        while (base[a] && a + 1 < (int)sizeof(full_path)) { full_path[a] = base[a]; a++; }
        if (a > 0 && full_path[a - 1] != '/' && a + 1 < (int)sizeof(full_path)) full_path[a++] = '/';
        while (editor_save_name[b] && a + 1 < (int)sizeof(full_path)) { full_path[a] = editor_save_name[b]; a++; b++; }
        full_path[a] = 0;
        if (explorer_picker_mode == 3) {
            media_player_import_file(full_path);
            editor_save_dialog_open = 0;
            explorer_picker_mode = 0;
            editor_save_name_focus = 0;
            app_window_t *exp = app_slot(APP_EXPLORER);
            if (exp) exp->open = 0;
            app_window_t *media_app = app_slot(APP_MEDIA_PLAYER);
            if (media_app) {
                media_app->open = 1;
                media_app->minimized = 0;
            }
            focus_app_window(APP_MEDIA_PLAYER - 1);
            needs_redraw = 1;
            return;
        }

        if (explorer_picker_mode == 2) {
            if (ide_save_dialog_open) {
                extern void ide_open_in_new_tab(const char*);
                extern int ide_active_tab;
                extern ide_tab_t ide_tabs[4];
                int free_tab = -1;
                for (int ti=0; ti<4; ti++) if (!ide_tabs[ti].text[0] && !ide_tabs[ti].filename[0]) { free_tab = ti; break; }
                if (free_tab >= 0) ide_active_tab = free_tab;
                ide_open_in_new_tab(full_path);
            } else {
                extern void text_edit_open_file(const char*);
                text_edit_open_file(full_path);
            }
            int was_ide = ide_save_dialog_open;
            editor_save_dialog_open = 0;
            ide_save_dialog_open = 0;
            explorer_picker_mode = 0;
            editor_save_name_focus = 0;
            app_window_t *exp = app_slot(APP_EXPLORER);
            if (exp) exp->open = 0;
            app_window_t *editor_app = app_slot(was_ide ? APP_IDE : APP_TEXT_EDIT);
            if (editor_app) editor_app->minimized = 0;
            focus_app_window((was_ide ? APP_IDE : APP_TEXT_EDIT) - 1);
            needs_redraw = 1;
            return;
        }

        created = fs_create_at(full_path, 0);
        
        // removed shadow ide_saving_ui
        char ui_buf[4096];
        if (ide_saving_ui) {
            extern void ui_builder_generate_code(char*, int);
            ui_builder_generate_code(ui_buf, 4096);
        }
        
        // Ensure ide_text is saved if it's the active editor
        app_window_t *active = active_app_window();
        char *text_to_save = editor_text;
        int len_to_save = editor_length;
        if (ide_save_dialog_open) {
            text_to_save = ide_text;
            len_to_save = ide_length;
        }
        
        if (ide_saving_ui == 1) {
            text_to_save = ui_buf;
            len_to_save = 0; while(ui_buf[len_to_save]) len_to_save++;
        }
        serial_write("Saving file: ");
        serial_write(full_path);
        serial_write("\n");
        serial_write_hex(len_to_save);
        serial_write("\n");
        fs_write_at(full_path, text_to_save, len_to_save);
        if (1) {
            if (ide_save_dialog_open && !ide_saving_ui) {
                extern int ide_active_tab;
                
                ide_saved = 1;
                int i=0; while(full_path[i]) { ide_tabs[ide_active_tab].filename[i] = full_path[i]; i++; }
                ide_tabs[ide_active_tab].filename[i] = 0;
            }
            if (ide_saving_ui == 2) ide_saving_ui = 0;
            if (ide_saving_ui == 1) {
                // Generate auto-detect import code
                char imp[128]; imp[0] = 0;
                gui_fl_cat(imp, "function ui:\n    import \"", 128);
                gui_fl_cat(imp, full_path, 128);
                gui_fl_cat(imp, "\"\nend\n", 128);
                int im = 0; while(imp[im]) { ide_text[im] = imp[im]; im++; }
                ide_text[im] = 0;
                ide_length = im;
                ide_cursor = im;
                ide_saving_ui = 0;
                ide_designer_mode = 0;
            }
            
            int was_ide = ide_save_dialog_open;
            editor_save_dialog_open = 0;
            ide_save_dialog_open = 0;
            explorer_picker_mode = 0;
            editor_save_name_focus = 0;
            app_window_t *exp = app_slot(APP_EXPLORER);
            if (exp) exp->open = 0;
            {
                app_window_t *editor_app = app_slot(was_ide ? APP_IDE : APP_TEXT_EDIT);
                if (editor_app) editor_app->minimized = 0;
                focus_app_window((was_ide ? APP_IDE : APP_TEXT_EDIT) - 1);
            }
            needs_redraw = 1;
        }
    }
}

static void update_keyboard_input(unsigned char key) {
     
    char c;
    app_window_t *editor = app_slot(APP_TEXT_EDIT);
    if (!key) {
        last_key_processed = 0;
        last_key_down = 0;
        key_repeat_ticks = 0;
        return;
    }
    if (key == last_key_down) {
        key_repeat_ticks++;
        if (key_repeat_ticks < key_repeat_delay) return;
        if (((key_repeat_ticks - key_repeat_delay) % key_repeat_rate) != 0) return;
    } else {
        last_key_down = key;
        key_repeat_ticks = 0;
    }
    last_key_processed = key;
    if (key == 0xE0) { keyboard_extended = 1; return; }
    if (key == 0x1D) { keyboard_ctrl = 1; return; }
    if (key == 0x38) { keyboard_alt = 1; alt_tab_armed = 1; return; }
    if (key == 0x2A) { keyboard_shift_l = 1; keyboard_shift = 1; return; }
    if (key == 0x36) { keyboard_shift_r = 1; keyboard_shift = 1; return; }
    if (key == 0x3A) {
        if (key_repeat_ticks == 0) {
            keyboard_caps_lock = !keyboard_caps_lock;
            sound_play_event(SOUND_EVENT_CLICK);
            needs_redraw = 1;
        }
        return;
    }
    if (key & 0x80) {
        if (key == 0x9D) keyboard_ctrl = 0;
        else if (key == 0xB8) {
            keyboard_alt = 0;
            alt_tab_armed = 0;
            if (alt_tab_preview_open && alt_tab_preview_index >= 0 && alt_tab_preview_index < 32) {
                focus_app_window(alt_tab_preview_index);
            }
            alt_tab_preview_open = 0;
            alt_tab_preview_index = -1;
            alt_tab_preview_anim = 0;
            needs_redraw = 1;
        }
        else if (key == 0xAA) { keyboard_shift_l = 0; keyboard_shift = keyboard_shift_r; }
        else if (key == 0xB6) { keyboard_shift_r = 0; keyboard_shift = keyboard_shift_l; }
        keyboard_extended = 0;
        return;
    }
    if (gui_mode == GUI_MODE_LOGIN) {
        login_handle_key(key);
        keyboard_extended = 0;
        return;
    }
    if (file_dialog_is_open()) {
        char c = scancode_to_ascii(key);
        file_dialog_handle_key(key, c);
        needs_redraw = 1;
        keyboard_extended = 0;
        return;
    }
    if (wifi_pwd_modal_open) {
        if (key == 0x01) { /* Esc */
            wifi_pwd_modal_open = 0;
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x0F /* Tab */ || (keyboard_alt && (key == 0x19 || scancode_to_ascii(key) == 'p' || scancode_to_ascii(key) == 'P'))) {
            wifi_pwd_show_pass = !wifi_pwd_show_pass;
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x1C) { /* Enter */
            int plen = 0;
            while (wifi_pwd_input[plen]) plen++;
            if (plen < 8) {
                wifi_pwd_error = 2;
            } else if (net_connect(wifi_pwd_ap_index, wifi_pwd_input)) {
                wifi_pwd_modal_open = 0;
                current_wifi_network = wifi_pwd_ap_index;
                wifi_pwd_error = 0;
            } else {
                wifi_pwd_error = 1;
            }
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x0E) { /* Backspace */
            if (wifi_pwd_cursor > 0) {
                wifi_pwd_input[--wifi_pwd_cursor] = 0;
                wifi_pwd_error = 0;
                needs_redraw = 1;
            }
            keyboard_extended = 0;
            return;
        }
        char c = scancode_to_ascii(key);
        if (c && wifi_pwd_cursor < 63) {
            wifi_pwd_input[wifi_pwd_cursor++] = c;
            wifi_pwd_input[wifi_pwd_cursor] = 0;
            wifi_pwd_error = 0;
            needs_redraw = 1;
        }
        keyboard_extended = 0;
        return;
    }
    if (weather_popover_open && weather_input_focused) {
        if (key == 0x01) { /* Esc */
            weather_popover_open = 0;
            weather_input_focused = 0;
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x1C) { /* Enter */
            if (weather_input_len > 0) {
                weather_set_city(weather_input_city);
                weather_input_city[0] = 0;
                weather_input_len = 0;
            }
            weather_input_focused = 0;
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x0E) { /* Backspace */
            if (weather_input_len > 0) {
                weather_input_len--;
                weather_input_city[weather_input_len] = 0;
                needs_redraw = 1;
            }
            keyboard_extended = 0;
            return;
        }
        char ch = scancode_to_ascii(key);
        if (ch >= 32 && ch <= 126 && weather_input_len < 30) {
            weather_input_city[weather_input_len++] = ch;
            weather_input_city[weather_input_len] = 0;
            needs_redraw = 1;
        }
        keyboard_extended = 0;
        return;
    }
    if (text_edit_unsaved_dialog_open) {
        if (key == 0x01) { /* Esc */
            text_edit_unsaved_dialog_open = 0;
            text_edit_closing_app_index = -1;
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x1C) { /* Enter -> Save */
            text_edit_unsaved_dialog_open = 0;
            text_edit_closing_app_index = -1;
            file_dialog_show(APP_TEXT_EDIT, FILE_DIALOG_SAVE, "untitled.txt", "/Desktop");
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        keyboard_extended = 0;
        return;
    }
    if (settings_net_focus || settings_account_focus) {
        if (key == 0x01 || key == 0x1C) { /* Esc or Enter */
            settings_net_focus = 0;
            settings_account_focus = 0;
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x0E) { /* Backspace */
            if (gui_text_input_handle_key(0, 1, 0)) needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        char c = scancode_to_ascii(key);
        if (c) {
            if (gui_text_input_handle_key(c, 0, 0)) needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
    }
    /* SPOTLIGHT INPUT */
    if (spotlight_open) {
        /* Check hotkey to toggle/close: Ctrl + Space */
        if (keyboard_ctrl && (key == 0x39 || scancode_to_ascii(key) == ' ')) {
            spotlight_close_dropdown();
            spotlight_open = 0;
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }

        if (spotlight_view_mode == SPOTLIGHT_VIEW_APP_DROPDOWN) {
            if (key == 0x01 || key == 0x0F) { /* Escape or Tab to return to search */
                spotlight_close_dropdown();
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
            if (spotlight_active_app_kind == APP_TERMINAL) {
                int is_enter    = (key == 0x1C);
                int is_bksp     = (key == 0x0E);
                int is_up       = (keyboard_extended && key == 0x48);
                int is_down     = (keyboard_extended && key == 0x50);
                int is_left     = (keyboard_extended && key == 0x4B);
                int is_right    = (keyboard_extended && key == 0x4D);
                int is_ctrl_c   = (keyboard_ctrl && key == 0x2E);
                int is_tab      = (key == 0x0F);
                unsigned char ac = (is_enter||is_bksp||is_up||is_down||is_left||is_right||is_ctrl_c||is_tab) ? 0 : scancode_to_ascii(key);
                if (is_enter||is_bksp||is_up||is_down||is_left||is_right||is_ctrl_c||is_tab||ac) {
                    terminal_key(ac, is_enter, is_bksp, is_up, is_down, is_left, is_right, is_ctrl_c, is_tab);
                }
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
            else if (spotlight_active_app_kind == APP_EXPLORER) {
                if (keyboard_extended && key == 0x48) { /* Up Arrow */
                    spotlight_explorer_move(-1);
                    needs_redraw = 1;
                    keyboard_extended = 0;
                    return;
                }
                if (keyboard_extended && key == 0x50) { /* Down Arrow */
                    spotlight_explorer_move(1);
                    needs_redraw = 1;
                    keyboard_extended = 0;
                    return;
                }
                if ((keyboard_extended && key == 0x4B) || key == 0x0E) { /* Left Arrow or Backspace: Up Directory */
                    spotlight_explorer_up();
                    needs_redraw = 1;
                    keyboard_extended = 0;
                    return;
                }
                if (key == 0x1C) { /* Enter: Descend into folder or open file */
                    spotlight_explorer_enter();
                    needs_redraw = 1;
                    keyboard_extended = 0;
                    return;
                }
            }
            else if (spotlight_active_app_kind == APP_CALCULATOR) {
                if (key == 0x01) {
                    spotlight_close_dropdown();
                    needs_redraw = 1;
                    keyboard_extended = 0;
                    return;
                }
            }
        }
        else if (spotlight_view_mode == SPOTLIGHT_VIEW_MEDIA_PREVIEW) {
            if (key == 0x01 || key == 0x0F) { /* Escape or Tab to return to search */
                spotlight_close_dropdown();
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
            if (key == 0x39) { /* Space - Toggle Play/Pause preview */
                spotlight_toggle_media_playback();
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
            if (key == 0x1C) { /* Enter - Open in full app */
                spotlight_execute(spotlight_selected_index);
                spotlight_close_dropdown();
                spotlight_open = 0;
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
        }
        else {
            /* SPOTLIGHT_VIEW_SEARCH */
            if (key == 0x01) { /* Escape */
                spotlight_open = 0;
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
            else if (key == 0x0F) { /* Tab - Dropdown View for App or Media! */
                int triggered = 0;
                if (spotlight_num_results > 0 && spotlight_selected_index >= 0 && spotlight_selected_index < spotlight_num_results) {
                    spotlight_result_t *res = &spotlight_results[spotlight_selected_index];
                    if (res->media_type == SPOTLIGHT_MEDIA_IMAGE) {
                        int ext_idx = 0;
                        for (int j = 0; res->action_arg[j]; j++) { if (res->action_arg[j] == '.') ext_idx = j + 1; }
                        if (ext_idx > 0) update_explorer_thumbnail(res->action_arg, &res->action_arg[ext_idx]);
                        spotlight_open_media_preview(res->action_arg, res->media_type, res->title);
                        triggered = 1;
                    } else if (res->media_type == SPOTLIGHT_MEDIA_VIDEO || res->media_type == SPOTLIGHT_MEDIA_AUDIO) {
                        spotlight_open_media_preview(res->action_arg, res->media_type, res->title);
                        triggered = 1;
                    } else if (res->icon_id == ICON_FOLDER || res->icon_id == ICON_DISK || res->app_kind == APP_EXPLORER || sl_str_contains_ci(res->title, "Explorer")) {
                        spotlight_open_dropdown_explorer(res->action_arg);
                        triggered = 1;
                    } else if (res->plugin_id == 3 || res->app_kind > 0) {
                        int k = res->app_kind ? res->app_kind : APP_TERMINAL;
                        spotlight_open_dropdown_app(k);
                        triggered = 1;
                    } else if (res->plugin_id == 2) {
                        spotlight_open_dropdown_app(APP_TERMINAL);
                        triggered = 1;
                    }
                }
                if (!triggered) {
                    if (sl_str_contains_ci("terminal", spotlight_query) || sl_str_contains_ci(spotlight_query, "term")) {
                        spotlight_open_dropdown_app(APP_TERMINAL);
                    } else if (sl_str_contains_ci("calculator", spotlight_query) || sl_str_contains_ci(spotlight_query, "calc")) {
                        spotlight_open_dropdown_app(APP_CALCULATOR);
                    } else if (sl_str_contains_ci("explorer", spotlight_query) || sl_str_contains_ci(spotlight_query, "file") ||
                               sl_str_contains_ci(spotlight_query, "disk") || sl_str_contains_ci(spotlight_query, "drive") ||
                               sl_str_contains_ci(spotlight_query, "hd")) {
                        spotlight_open_dropdown_explorer("FirstOS");
                    } else if (spotlight_num_results > 0) {
                        spotlight_result_t *res = &spotlight_results[0];
                        if (res->media_type == SPOTLIGHT_MEDIA_IMAGE) {
                            int ext_idx = 0;
                            for (int j = 0; res->action_arg[j]; j++) { if (res->action_arg[j] == '.') ext_idx = j + 1; }
                            if (ext_idx > 0) update_explorer_thumbnail(res->action_arg, &res->action_arg[ext_idx]);
                            spotlight_open_media_preview(res->action_arg, res->media_type, res->title);
                        } else if (res->media_type != SPOTLIGHT_MEDIA_NONE) {
                            spotlight_open_media_preview(res->action_arg, res->media_type, res->title);
                        } else if (res->icon_id == ICON_FOLDER || res->icon_id == ICON_DISK || res->app_kind == APP_EXPLORER) {
                            spotlight_open_dropdown_explorer(res->action_arg);
                        } else if (res->plugin_id == 3 || res->app_kind > 0) {
                            spotlight_open_dropdown_app(res->app_kind ? res->app_kind : APP_TERMINAL);
                        }
                    }
                }
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
            else if (key == 0x0E) { /* Backspace */
                if (spotlight_qlen > 0) {
                    spotlight_query[--spotlight_qlen] = 0;
                    spotlight_search(spotlight_query);
                }
            }
            else if (key == 0x1C) { /* Enter */
                spotlight_execute(spotlight_selected_index);
            }
            else if (keyboard_extended && key == 0x48) { /* Up Arrow */
                if (spotlight_selected_index > 0) spotlight_selected_index--;
            }
            else if (keyboard_extended && key == 0x50) { /* Down Arrow */
                if (spotlight_selected_index < spotlight_num_results - 1) spotlight_selected_index++;
            }
            else {
                c = scancode_to_ascii(key);
                if (c && spotlight_qlen < 63) {
                    spotlight_query[spotlight_qlen++] = c;
                    spotlight_query[spotlight_qlen] = 0;
                    spotlight_search(spotlight_query);
                }
            }
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
    }
    if (handle_system_shortcut(key)) { keyboard_extended = 0; return; }
    if (keyboard_alt && key == 0x0F && !task_switcher_enabled) { keyboard_extended = 0; return; }
    if (keyboard_alt && keyboard_extended) {
        app_window_t *active = active_app_window();
        if (active && !active->minimized) {
            if (key == 0x4B) { apply_snap_to_window(active, 1); needs_redraw = 1; keyboard_extended = 0; return; }
            if (key == 0x4D) { apply_snap_to_window(active, 2); needs_redraw = 1; keyboard_extended = 0; return; }
            if (key == 0x48) { apply_snap_to_window(active, 3); needs_redraw = 1; keyboard_extended = 0; return; }
            if (key == 0x50) { apply_snap_to_window(active, 7); needs_redraw = 1; keyboard_extended = 0; return; }
        }
    }
    {
        app_window_t *browser_win = active_app_window();
        if (browser_win && browser_win->open && !browser_win->minimized && browser_win->kind == APP_BROWSER) {
            char bc = scancode_to_ascii(key);
            web_browser_app_handle_key(key, bc);
            needs_redraw = 1;
            gui_window_dirty_only = 0;
            keyboard_extended = 0;
            return;
        }
    }

    if (keyboard_alt || keyboard_ctrl) { keyboard_extended = 0; return; }
    if (explorer_quick_look_open) {
        if (key == 0x01) { /* Escape - close Quick Look */
            explorer_quick_look_open = 0;
            if (hda_is_playing()) hda_stop_audio();
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x39) { /* Space - toggle audio play/pause, or close */
            if (explorer_selected >= 0 && explorer_selected < explorer_entry_count()) {
                const explorer_entry_t *e = explorer_entry_at(explorer_selected);
                if (e && !e->is_dir && explorer_is_audio_file(e->name)) {
                    if (hda_is_playing()) hda_pause_audio();
                    else if (hda_is_paused()) hda_resume_audio();
                    else hda_play_audio(e->path);
                    needs_redraw = 1;
                    keyboard_extended = 0;
                    return;
                }
            }
            explorer_quick_look_open = 0;
            if (hda_is_playing()) hda_stop_audio();
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x1C) { /* Enter - open in default app */
            if (explorer_selected >= 0 && explorer_selected < explorer_entry_count()) {
                const explorer_entry_t *e = explorer_entry_at(explorer_selected);
                if (e && !e->is_dir) {
                    explorer_quick_look_open = 0;
                    if (hda_is_playing()) hda_stop_audio();
                    explorer_open_file_default(e->path, e->name);
                    needs_redraw = 1;
                    keyboard_extended = 0;
                    return;
                }
            }
        }
    }
    if (widget_notes_focused) {
        char ac = scancode_to_ascii(key);
        if (widgets_handle_key(ac, (int)key)) {
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
    }
    if (explorer_search_focus) {
        gui_text_input_set(explorer_search_text, (int)sizeof(explorer_search_text), &explorer_search_cursor, &explorer_search_focus);
        if (key == 0x0E) {
            if (gui_text_input_handle_key(0, 1, 0)) { explorer_scroll = 0; explorer_search_result_index = -1; explorer_search_history_active = -1; needs_redraw = 1; }
            keyboard_extended = 0;
            return;
        }
        if (keyboard_extended && key == 0x48 && explorer_search_history_open && explorer_search_history_count > 0) {
            if (explorer_search_history_active < 0) explorer_search_history_active = 0;
            else if (explorer_search_history_active > 0) explorer_search_history_active--;
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (keyboard_extended && key == 0x50 && explorer_search_history_open && explorer_search_history_count > 0) {
            if (explorer_search_history_active < 0) explorer_search_history_active = 0;
            else if (explorer_search_history_active + 1 < explorer_search_history_count) explorer_search_history_active++;
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x0F) {
            explorer_search_history_active = -1;
            explorer_focus_search_result(1);
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x1C) {
            if (explorer_search_history_open && explorer_search_history_active >= 0 && explorer_search_history_active < explorer_search_history_count) {
                int j = 0;
                while (explorer_search_history[explorer_search_history_active][j] && j + 1 < (int)sizeof(explorer_search_text)) {
                    explorer_search_text[j] = explorer_search_history[explorer_search_history_active][j];
                    j++;
                }
                explorer_search_text[j] = 0;
                explorer_search_cursor = j;
                explorer_search_result_index = -1;
                explorer_scroll = 0;
                explorer_search_history_open = 0;
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
            explorer_store_search_history();
            explorer_search_history_open = 1;
            explorer_search_history_active = -1;
            explorer_focus_search_result(explorer_search_result_index < 0 ? 0 : 1);
            if (explorer_selected >= 0) {
                const explorer_entry_t *entry = explorer_entry_at(explorer_selected);
                if (entry && entry->is_dir && entry->path) {
                    explorer_open_path(entry->path);
                    explorer_path = explorer_current_path();
                    explorer_selected = -1;
                    explorer_selection_anchor = -1;
                    explorer_clear_multi_selection();
                    explorer_scroll = 0;
                    explorer_search_result_index = -1;
                }
            }
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x01) {
            explorer_search_focus = 0;
            explorer_search_history_open = 0;
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        c = scancode_to_ascii(key);
        if (c) {
            if (gui_text_input_handle_key(c, 0, 0)) { explorer_scroll = 0; explorer_search_result_index = -1; explorer_search_history_open = 1; needs_redraw = 1; }
            keyboard_extended = 0;
            return;
        }
    }

    if (!explorer_search_focus && !editor_save_name_focus && (desktop_context_open || explorer_context_open || taskbar_context_open)) {
        keyboard_extended = 0;
        return;
    }

    if (!explorer_search_focus && !editor_save_name_focus) {
        app_window_t *explorer_app = app_slot(APP_EXPLORER);
        int filtered_count = explorer_filtered_count();
        if (active_app_index == APP_EXPLORER - 1 && explorer_app && explorer_app->open && !explorer_app->minimized && filtered_count > 0) {
            int cols = explorer_grid_columns(explorer_app);
            int current_visible = 0;
            int i = 0;
            if (cols < 1) cols = 1;
            if (explorer_selected >= 0) {
                current_visible = 0;
                for (i = 0; i < explorer_entry_count(); i++) {
                    const explorer_entry_t *entry = explorer_entry_at(i);
                    if (!explorer_entry_matches_search(entry)) continue;
                    if (i == explorer_selected) break;
                    current_visible++;
                }
                if (i >= explorer_entry_count()) current_visible = 0;
            }
            if (explorer_selected < 0) current_visible = 0;
            if (key == 0x4B) {
                if (explorer_quick_look_open && hda_is_playing()) hda_stop_audio();
                if (explorer_selected < 0) current_visible = 0;
                else if (current_visible > 0) current_visible--;
                explorer_selected = explorer_filtered_index_from_visible(current_visible);
                explorer_search_result_index = current_visible;
                if (keyboard_shift && explorer_selection_anchor >= 0) explorer_select_range(explorer_selection_anchor, explorer_selected);
                else { explorer_clear_multi_selection(); explorer_selection_anchor = explorer_selected; }
                explorer_ensure_selection_visible(explorer_app);
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
            if (key == 0x4D) {
                if (explorer_quick_look_open && hda_is_playing()) hda_stop_audio();
                if (explorer_selected < 0) current_visible = 0;
                else if (current_visible + 1 < filtered_count) current_visible++;
                explorer_selected = explorer_filtered_index_from_visible(current_visible);
                explorer_search_result_index = current_visible;
                if (keyboard_shift && explorer_selection_anchor >= 0) explorer_select_range(explorer_selection_anchor, explorer_selected);
                else { explorer_clear_multi_selection(); explorer_selection_anchor = explorer_selected; }
                explorer_ensure_selection_visible(explorer_app);
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
            if (key == 0x48) {
                if (explorer_quick_look_open && hda_is_playing()) hda_stop_audio();
                if (explorer_selected < 0) current_visible = 0;
                else current_visible -= cols;
                if (current_visible < 0) current_visible = 0;
                explorer_selected = explorer_filtered_index_from_visible(current_visible);
                explorer_search_result_index = current_visible;
                if (keyboard_shift && explorer_selection_anchor >= 0) explorer_select_range(explorer_selection_anchor, explorer_selected);
                else { explorer_clear_multi_selection(); explorer_selection_anchor = explorer_selected; }
                explorer_ensure_selection_visible(explorer_app);
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
            if (key == 0x50) {
                if (explorer_quick_look_open && hda_is_playing()) hda_stop_audio();
                if (explorer_selected < 0) current_visible = 0;
                else current_visible += cols;
                if (current_visible >= filtered_count) current_visible = filtered_count - 1;
                explorer_selected = explorer_filtered_index_from_visible(current_visible);
                explorer_search_result_index = current_visible;
                if (keyboard_shift && explorer_selection_anchor >= 0) explorer_select_range(explorer_selection_anchor, explorer_selected);
                else { explorer_clear_multi_selection(); explorer_selection_anchor = explorer_selected; }
                explorer_ensure_selection_visible(explorer_app);
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
            if (key == 0x39) {
                if (explorer_selected >= 0 && explorer_selected < explorer_entry_count()) {
                    const explorer_entry_t *entry = explorer_entry_at(explorer_selected);
                    if (entry && !entry->is_dir) {
                        explorer_quick_look_open = 1;
                        needs_redraw = 1;
                        keyboard_extended = 0;
                        return;
                    }
                }
            }
            if (key == 0x1C && explorer_selected >= 0) {
                const explorer_entry_t *entry = explorer_entry_at(explorer_selected);
                if (entry && entry->is_dir && entry->path) {
                    explorer_open_path(entry->path);
                    explorer_path = explorer_current_path();
                    explorer_selected = -1;
                    explorer_selection_anchor = -1;
                    explorer_clear_multi_selection();
                    explorer_scroll = 0;
                    explorer_search_result_index = -1;
                    needs_redraw = 1;
                    keyboard_extended = 0;
                    return;
                } else if (entry && !entry->is_dir) {
                    if (explorer_quick_look_open) {
                        explorer_quick_look_open = 0;
                        if (hda_is_playing()) hda_stop_audio();
                    }
                    explorer_open_file_default(entry->path, entry->name);
                    needs_redraw = 1;
                    keyboard_extended = 0;
                    return;
                }
            }
        }
    }

    if (active_app_index == APP_PREVIEW - 1) {
        if (keyboard_extended && key == 0x4B) {
            preview_handle_key(17);
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (keyboard_extended && key == 0x4D) {
            preview_handle_key(18);
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x0C || key == 0x4A) {
            preview_handle_key('-');
            needs_redraw = 1;
            return;
        }
        if (key == 0x0D || key == 0x4E) {
            preview_handle_key('+');
            needs_redraw = 1;
            return;
        }
        if (key == 0x21) {
            preview_handle_key('f');
            needs_redraw = 1;
            return;
        }
    }

    if (active_app_index == APP_MEDIA_PLAYER - 1) {
        if (key == 0x39) { /* Space: Play/Pause */
            media_player_pause();
            needs_redraw = 1;
            return;
        }
        if (keyboard_extended && key == 0x4B) { /* Left Arrow: seek backward */
            media_player_seek(-5);
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (keyboard_extended && key == 0x4D) { /* Right Arrow: seek forward */
            media_player_seek(5);
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (keyboard_extended && key == 0x48) { /* Up Arrow: vol up */
            media_player_set_volume(5);
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (keyboard_extended && key == 0x50) { /* Down Arrow: vol down */
            media_player_set_volume(-5);
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x32) { /* M: mute toggle */
            media_player_toggle_mute();
            needs_redraw = 1;
            return;
        }
        if (key == 0x19) { /* P: toggle playlist */
            media_player_toggle_playlist();
            needs_redraw = 1;
            return;
        }
        if (key == 0x13) { /* R: rewind */
            media_player_rewind();
            needs_redraw = 1;
            return;
        }
        if (key == 0x1F) { /* S: stop */
            media_player_stop();
            needs_redraw = 1;
            return;
        }
        if (key == 0x01) { /* Escape */
            media_player_handle_key(27);
            needs_redraw = 1;
            return;
        }
    }

    if (explorer_picker_mode && editor_save_dialog_open && editor_save_name_focus) {

        int len = 0;
        while (editor_save_name[len]) len++;
        gui_text_input_set(editor_save_name, (int)sizeof(editor_save_name), &editor_save_name_cursor, &editor_save_name_focus);
        if (key == 0x0E) {
            if (gui_text_input_handle_key(0, 1, 0)) needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        if (key == 0x1C) {
            trigger_explorer_save();
            keyboard_extended = 0;
            return;
        }
        c = scancode_to_ascii(key);
        if (c) {
            if (gui_text_input_handle_key(c, 0, 0)) needs_redraw = 1;
        }
        keyboard_extended = 0;
        return;
    }
    if (explorer_rename_mode || (desktop_selected >= 0 && desktop_selected < desktop_item_count && desktop_items[desktop_selected].rename_mode)) {
        char *name = explorer_rename_mode ? explorer_rename_name : desktop_items[desktop_selected].name;
        int *cursor = explorer_rename_mode ? &explorer_rename_cursor : &desktop_items[desktop_selected].cursor;
        int i = 0;
        int len = 0;
        while (name[len]) len++;
        if (key == 0x0E) {
            if (*cursor > 0) {
                for (i = *cursor - 1; i < len; i++) name[i] = name[i + 1];
                (*cursor)--;
                needs_redraw = 1;
            }
            keyboard_extended = 0;
            return;
        }
        if (key == 0x1C) {
            if (explorer_rename_mode && explorer_rename_index >= 0 && explorer_rename_index < explorer_entry_count()) {
                const explorer_entry_t *entry = explorer_entry_at(explorer_rename_index);
                char new_path[64];
                int slash = -1;
                int p = 0;
                int n = 0;
                if (entry && entry->path && explorer_rename_name[0]) {
                    while (entry->path[p]) {
                        if (entry->path[p] == '/') slash = p;
                        p++;
                    }
                    p = 0;
                    if (slash >= 0) {
                        for (p = 0; p <= slash && p + 1 < (int)sizeof(new_path); p++) new_path[p] = entry->path[p];
                        while (explorer_rename_name[n] && p + 1 < (int)sizeof(new_path)) new_path[p++] = explorer_rename_name[n++];
                        new_path[p] = 0;
                        if (fs_rename_at(entry->path, new_path)) {
                            explorer_open_path(explorer_current_path());
                            explorer_path = explorer_current_path();
                        }
                    }
                }
                explorer_rename_index = -1;
                explorer_rename_mode = 0;
            } else if (desktop_selected >= 0 && desktop_selected < desktop_item_count && desktop_items[desktop_selected].rename_mode) {
                char new_path[64];
                int slash = -1;
                int p = 0;
                int n = 0;
                if (desktop_items[desktop_selected].path[0] && desktop_items[desktop_selected].name[0]) {
                    while (desktop_items[desktop_selected].path[p]) {
                        if (desktop_items[desktop_selected].path[p] == '/') slash = p;
                        p++;
                    }
                    p = 0;
                    if (slash >= 0) {
                        for (p = 0; p <= slash && p + 1 < (int)sizeof(new_path); p++) new_path[p] = desktop_items[desktop_selected].path[p];
                        while (desktop_items[desktop_selected].name[n] && p + 1 < (int)sizeof(new_path)) new_path[p++] = desktop_items[desktop_selected].name[n++];
                        new_path[p] = 0;
                        if (fs_rename_at(desktop_items[desktop_selected].path, new_path)) sync_desktop_icons_from_fs();
                    }
                }
                if (desktop_selected >= 0 && desktop_selected < desktop_item_count) desktop_items[desktop_selected].rename_mode = 0;
            }
            needs_redraw = 1;
            keyboard_extended = 0;
            return;
        }
        c = scancode_to_ascii(key);
        if (c && len < 39) {
            for (i = len; i >= *cursor; i--) name[i + 1] = name[i];
            name[*cursor] = c;
            (*cursor)++;
            needs_redraw = 1;
        }
        keyboard_extended = 0;
        return;
    }
    /* Route keys to Terminal if it's focused */
    {
        app_window_t *term_app = active_app_window();
        if (term_app && term_app->open && !term_app->minimized && term_app->kind == APP_TERMINAL) {
            int is_enter    = (key == 0x1C);
            int is_bksp     = (key == 0x0E);
            int is_up       = (key == 0x48);
            int is_down     = (key == 0x50);
            int is_left     = (key == 0x4B);
            int is_right    = (key == 0x4D);
            int is_ctrl_c   = (keyboard_ctrl && key == 0x2E);
            int is_tab      = (key == 0x0F);
            unsigned char ac = (is_enter||is_bksp||is_up||is_down||is_left||is_right||is_ctrl_c||is_tab) ? 0 : scancode_to_ascii(key);
            if(is_enter||is_bksp||is_up||is_down||is_left||is_right||is_ctrl_c||is_tab||ac) {
                terminal_key(ac, is_enter, is_bksp, is_up, is_down, is_left, is_right, is_ctrl_c, is_tab);
                needs_redraw = 1;
                keyboard_extended = 0;
                return;
            }
        }
    }
    if (active_app_index == APP_IDE - 1) {
        app_window_t *ide = app_slot(APP_IDE);
        if (ide && ide->open && !ide->minimized) {
            {
                extern void ide_cursor_up(void); extern void ide_cursor_down(void); extern void ide_cursor_left(void); extern void ide_cursor_right(void);
                if (key == 0x48) { ide_cursor_up(); needs_redraw = 1; keyboard_extended = 0; return; }
                if (key == 0x50) { ide_cursor_down(); needs_redraw = 1; keyboard_extended = 0; return; }
                if (key == 0x4B) { ide_cursor_left(); needs_redraw = 1; keyboard_extended = 0; return; }
                if (key == 0x4D) { ide_cursor_right(); needs_redraw = 1; keyboard_extended = 0; return; }
            }
            if (key == 0x0F) { extern void ide_autocomplete(void); ide_autocomplete(); needs_redraw = 1; keyboard_extended = 0; return; }
            if (key == 0x0E) { ide_backspace(); needs_redraw = 1; keyboard_extended = 0; return; }
            if (key == 0x1C) { ide_insert_char('\n'); needs_redraw = 1; keyboard_extended = 0; return; }
            c = scancode_to_ascii(key);
            if (c) { ide_insert_char(c); needs_redraw = 1; }
            keyboard_extended = 0;
            return;
        }
    }
    {
        app_window_t *settings_win = active_app_window();
        if (settings_win && settings_win->open && !settings_win->minimized && settings_win->kind == APP_SETTINGS) {
            c = scancode_to_ascii(key);
            if (c == '1') { settings_set_subtab(0); needs_redraw = 1; keyboard_extended = 0; return; }
            if (c == '2') { settings_set_subtab(1); needs_redraw = 1; keyboard_extended = 0; return; }
            if (c == '3') { settings_set_subtab(2); needs_redraw = 1; keyboard_extended = 0; return; }
        }
    }
    
    if (!editor || !editor->open || editor->minimized || active_app_index != APP_TEXT_EDIT - 1) { keyboard_extended = 0; return; }

    if (text_edit_is_find_focused()) {
        if (key == 0x0E) { text_edit_find_backspace(); needs_redraw = 1; keyboard_extended = 0; return; }
        c = scancode_to_ascii(key);
        if (c) { text_edit_find_insert_char(c); needs_redraw = 1; }
        keyboard_extended = 0;
        return;
    }

    {
        extern void text_edit_cursor_up(void); extern void text_edit_cursor_down(void); extern void text_edit_cursor_left(void); extern void text_edit_cursor_right(void);
        if (key == 0x48) { text_edit_cursor_up(); needs_redraw = 1; keyboard_extended = 0; return; }
        if (key == 0x50) { text_edit_cursor_down(); needs_redraw = 1; keyboard_extended = 0; return; }
        if (key == 0x4B) { text_edit_cursor_left(); needs_redraw = 1; keyboard_extended = 0; return; }
        if (key == 0x4D) { text_edit_cursor_right(); needs_redraw = 1; keyboard_extended = 0; return; }
    }
    if (key == 0x0F) { text_edit_insert_tab(); needs_redraw = 1; keyboard_extended = 0; return; }
    if (key == 0x0E) { text_edit_backspace(); needs_redraw = 1; keyboard_extended = 0; return; }
    if (key == 0x53) { text_edit_delete(); needs_redraw = 1; keyboard_extended = 0; return; }
    if (key == 0x1C) { text_edit_insert_char('\n'); needs_redraw = 1; keyboard_extended = 0; return; }
    c = scancode_to_ascii(key);
    if (c) { text_edit_insert_char(c); needs_redraw = 1; }
    keyboard_extended = 0;
}



static void sync_app_window(app_window_t *app);

// Phase 1: Window resize helpers
static int check_snap_zone(int mx, int my) {
    if (!snap_enabled) return SNAP_NONE;
    int sw = (int)fb.width;
    int bar_h = desktop_bar.h;

    // Top center of top bar -> Full Screen
    if (my <= bar_h + 8) {
        if (mx >= sw / 4 && mx <= (sw * 3) / 4) {
            return SNAP_FULLSCREEN;
        } else if (mx < sw / 4) {
            return SNAP_LEFT;
        } else {
            return SNAP_RIGHT;
        }
    }

    // Left edge -> Left 50%
    if (mx <= 18) {
        return SNAP_LEFT;
    }

    // Right edge -> Right 50%
    if (mx >= sw - 18) {
        return SNAP_RIGHT;
    }

    return SNAP_NONE;
}

static rect_t get_snap_preview_rect(int snap_mode) {
    int sw = (int)fb.width;
    int top_y = desktop_bar.h + 2;
    int bottom_y = dock_rect.y - 2;
    int usable_h = bottom_y - top_y;
    int margin = 4;
    int half_w = sw / 2 - margin - 2;

    if (snap_mode == SNAP_LEFT) {
        return (rect_t){margin, top_y, half_w, usable_h, 0};
    } else if (snap_mode == SNAP_RIGHT) {
        return (rect_t){sw / 2 + 2, top_y, half_w, usable_h, 0};
    } else if (snap_mode == SNAP_FULLSCREEN) {
        return (rect_t){margin, top_y, sw - margin * 2, usable_h, 0};
    }
    return (rect_t){0, 0, 0, 0, 0};
}

static void draw_snap_preview(void) {
    if (!window_dragging || window_snapping <= SNAP_NONE) return;
    rect_t r = get_snap_preview_rect(window_snapping);
    if (r.w <= 0 || r.h <= 0) return;

    // Translucent blue overlay
    fb_rect_alpha(r.x, r.y, r.w, r.h, 0x1A73E8, 65);

    // High-visibility borders
    outline(r.x, r.y, r.w, r.h, 0x1A73E8);
    outline(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 0x8AB4F8);
    outline(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 0x1A73E8);

    // Centered label badge
    const char *label = (window_snapping == SNAP_LEFT) ? "Left Split (50%)" :
                        (window_snapping == SNAP_RIGHT) ? "Right Split (50%)" :
                        "Full Screen (100%)";
    int bw = (window_snapping == SNAP_FULLSCREEN) ? 160 : 140;
    int bh = 24;
    int bx = r.x + (r.w - bw) / 2;
    int by = r.y + (r.h - bh) / 2;

    fb_rect_alpha(bx, by, bw, bh, 0x0D1B2A, 210);
    outline(bx, by, bw, bh, 0x415A77);
    font_text_fit_center(bx, by + 7, label, 0xFFFFFF, bw);
}

void apply_snap_to_window(app_window_t* app, int snap_side) {
    if (!app || !app->open || app->minimized) return;

    if (!app->snapped && !app->maximized) {
        app->saved_x = app->frame.x;
        app->saved_y = app->frame.y;
        app->saved_w = app->frame.w;
        app->saved_h = app->frame.h;
    }

    rect_t r = get_snap_preview_rect(snap_side);
    if (r.w > 0 && r.h > 0) {
        app->frame.x = r.x;
        app->frame.y = r.y;
        app->frame.w = r.w;
        app->frame.h = r.h;
        app->snapped = snap_side;
        app->maximized = (snap_side == SNAP_FULLSCREEN);
        sync_app_window(app);
        needs_redraw = 1;
    }
}

static int get_resize_edge(app_window_t* app, int mx, int my) {
    if (!app || !app->open || app->minimized || app->maximized) return 0;
    
    int edge_size = 8;
    int on_right = (mx >= app->frame.x + app->frame.w - edge_size && mx <= app->frame.x + app->frame.w);
    int on_bottom = (my >= app->frame.y + app->frame.h - edge_size && my <= app->frame.y + app->frame.h);
    int on_right_bottom = on_right && on_bottom;
    
    if (on_right_bottom) return 3; // resize both
    if (on_right) return 1;        // resize width
    if (on_bottom) return 2;       // resize height
    return 0; // no resize
}

static void draw_resize_indicator(app_window_t* app, int edge) {
    if (!app || !app->open || edge == 0) return;
    
    u32 indicator_color = 0x00AAFF;
    int indicator_size = 3;
    
    if (edge == 1 || edge == 3) { // right edge
        fb_rect(app->frame.x + app->frame.w - indicator_size, app->frame.y, indicator_size, app->frame.h, indicator_color);
    }
    if (edge == 2 || edge == 3) { // bottom edge
        fb_rect(app->frame.x, app->frame.y + app->frame.h - indicator_size, app->frame.w, indicator_size, indicator_color);
    }
}


/* Note: standard controls and widgets extracted to gui_widgets.c */

static void handle_mouse_wheel(void) {
    int wheel = ps2_mouse_wheel();
    int mx = ps2_mouse_x();
    int my = ps2_mouse_y();
    app_window_t *editor = app_slot(APP_TEXT_EDIT);
    app_window_t *explorer = app_slot(APP_EXPLORER);
    app_window_t *settings = app_slot(APP_SETTINGS);
    if (!wheel) return;
    if (file_dialog_is_open()) {
        file_dialog_handle_scroll(wheel > 0 ? -1 : 1);
        needs_redraw = 1;
        return;
    }
    if (editor && editor->open && !editor->minimized && point_in_rect(mx, my, editor_text_area)) {
        text_edit_scroll_lines(wheel > 0 ? -3 : 3);
        needs_redraw = 1;
        return;
    }
    app_window_t *ide_win = app_slot(APP_IDE);
    if (ide_win && ide_win->open && !ide_win->minimized) {
        int sidebar_w = 130;
        int ex = ide_win->frame.x + 16 + sidebar_w;
        int ew = ide_win->frame.w - 32 - sidebar_w;
        int p_y = ide_win->frame.y + ide_win->frame.h - 110;
        int p_h = 88;
        rect_t pane_r = {ex, p_y, ew, p_h, 0};
        if (point_in_rect(mx, my, pane_r)) {
            app_studio_scroll_output(wheel > 0 ? -2 : 2);
            needs_redraw = 1;
            return;
        }
    }
    app_window_t *browser_win = app_slot(APP_BROWSER);
    if (browser_win && browser_win->open && !browser_win->minimized && point_in_rect(mx, my, browser_win->frame)) {
        web_browser_app_handle_scroll(wheel > 0 ? -1 : 1);
        needs_redraw = 1;
        return;
    }
    if (settings && settings->open && !settings->minimized) {
        rect_t settings_area = {settings->frame.x + 16, settings->frame.y + 48, settings->frame.w - 32, settings->frame.h - 66, 0};
        if (point_in_rect(mx, my, settings_area)) {
            settings_scroll += (wheel > 0 ? -1 : 1);
            if (settings_scroll < 0) settings_scroll = 0;
            if (settings_scroll > 1) settings_scroll = 1;
            needs_redraw = 1;
            return;
        }
    }
    if (desktop_context_open && point_in_rect(mx, my, desktop_context_menu)) {
        int max_scroll = desktop_context_target ? 0 : 2;
        if (max_scroll > 0) {
            desktop_context_scroll += (wheel > 0 ? -1 : 1);
            if (desktop_context_scroll < 0) desktop_context_scroll = 0;
            if (desktop_context_scroll > max_scroll) desktop_context_scroll = max_scroll;
            needs_redraw = 1;
            return;
        }
    }
    if (explorer_context_open && point_in_rect(mx, my, explorer_context_menu)) {
        int max_scroll = 0;
        if (max_scroll > 0) {
            explorer_context_scroll += (wheel > 0 ? -1 : 1);
            if (explorer_context_scroll < 0) explorer_context_scroll = 0;
            if (explorer_context_scroll > max_scroll) explorer_context_scroll = max_scroll;
            needs_redraw = 1;
            return;
        }
    }
    if (taskbar_context_open && point_in_rect(mx, my, taskbar_context_menu)) {
        int max_scroll = 0;
        if (max_scroll > 0) {
            taskbar_context_scroll += (wheel > 0 ? -1 : 1);
            if (taskbar_context_scroll < 0) taskbar_context_scroll = 0;
            if (taskbar_context_scroll > max_scroll) taskbar_context_scroll = max_scroll;
            needs_redraw = 1;
            return;
        }
    }
    if (explorer && explorer->open && !explorer->minimized) {
        rect_t area = {explorer->frame.x + 137, explorer->frame.y + 70, explorer->frame.w - 160, explorer->frame.h - 86, 0};
        if (point_in_rect(mx, my, area)) {
            int entry_count = explorer_entry_count();
    int filtered_count = explorer_filtered_count();
            int cell_w = (area.w < 220) ? 76 : 92;
            int cell_h = 76;
            int icon_cols = (area.w - 20) / cell_w;
            int visible_rows = (area.h - (editor_save_dialog_open ? 40 : 10)) / cell_h;
            int visible_items;
            if (icon_cols < 1) icon_cols = 1;
            if (visible_rows < 1) visible_rows = 1;
            visible_items = visible_rows * icon_cols;
            explorer_scroll += (wheel > 0 ? -icon_cols : icon_cols);
            if (explorer_scroll < 0) explorer_scroll = 0;
            if (explorer_scroll > filtered_count - visible_items) explorer_scroll = filtered_count - visible_items;
            if (explorer_scroll < 0) explorer_scroll = 0;
            needs_redraw = 1;
            return;
        }
    }
}

static const char *app_name_from_kind(int kind) {
    if (kind == APP_TERMINAL) return "TERMINAL";
    if (kind == APP_FILES) return "TASK MGR";
    if (kind == APP_DISPLAY) return "KEYBOARD";
    if (kind == APP_EXPLORER) return "EXPLORER";
    if (kind == APP_SETTINGS) return "SETTINGS";
    if (kind == APP_MEDIA_PLAYER) return "MEDIA PLAYER";
    if (kind == APP_CALCULATOR) return "CALCULATOR";
    if (kind == APP_PREVIEW) return "PREVIEW";
    if (kind == APP_PHOTO_EDITOR) return "PHOTO EDITOR";
    if (kind == APP_TEXT_EDIT) return "TEXT EDITOR";
    if (kind == APP_IDE) return "APP STUDIO";
    if (kind == APP_BROWSER) return "WEB BROWSER";
    return "DESKTOP";
}

app_window_t *active_app_window(void) {
    int i = 0;
    if (active_app_index >= 0 && active_app_index < 32 && app_windows[active_app_index].open && !app_windows[active_app_index].minimized) return &app_windows[active_app_index];
    for (i = 31; i >= 0; i--) {
        if (app_windows[z_order[i]].open && !app_windows[z_order[i]].minimized) {
            active_app_index = z_order[i];
            return &app_windows[z_order[i]];
        }
    }
    return 0;
}

void apply_theme(void) {
    desktop_bg = active_theme.desktop_bg;
    desktop_panel = active_theme.desktop_panel;
    dock_bg = active_theme.bottom_bar;
    window_bg = active_theme.window_bg;
    title_bg = active_theme.title_bar;
    shadow = active_theme.desktop_shadow;
    window_border = active_theme.window_border;
    control_bg = active_theme.control_bg;
    control_bg_hi = active_theme.control_bg_hi;
    control_text = active_theme.control_text;
    accent_color = active_theme.accent;
    system_font_color = active_theme.font;
}

static void draw_wallpaper_preview_frame(int x, int y, int w, int h, u32 a, u32 b, u32 c) {
    int stripe;
    fb_rect(x, y, w, h, a);
    for (stripe = 0; stripe < w; stripe += 16) {
        fb_rect(x + stripe, y, 6, h, (stripe / 16) % 2 ? b : c);
    }
    fb_rect(x, y + h - 22, w, 22, 0x101722);
}

static int wallpaper_has_extension(const char *path, const char *ext) {
    int i = 0;
    int dot = -1;
    if (!path || !ext) return 0;
    while (path[i]) {
        if (path[i] == '.') dot = i;
        i++;
    }
    if (dot < 0) return 0;
    i = 0;
    while (ext[i] && path[dot + 1 + i]) {
        char a = path[dot + 1 + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return 0;
        i++;
    }
    return ext[i] == 0 && path[dot + 1 + i] == 0;
}

static u32 wallpaper_color_from_path(const char *path) {
    u32 hash = 2166136261u;
    int i = 0;
    if (!path || !path[0]) return 0x3A5878;
    while (path[i]) {
        hash ^= (u32)(unsigned char)path[i];
        hash *= 16777619u;
        i++;
    }
    return 0x303030u + (hash & 0x5F9FBFu);
}

static int load_wallpaper_raw_into_cache(int w, int h) {
    /* .raw files are exactly 1024*768*4 bytes, BGRX u32 pixels */
    int rc;
    if (!wallpaper_path[0]) return 0;
    if (!wallpaper_has_extension(wallpaper_path, "raw")) return 0;
    /* Read directly into cache — cache is u32[1024*768] = 3145728 bytes exactly */
    rc = fs_read_raw_at(wallpaper_path, (char*)wallpaper_cache, 1024u * 768u * 4u);
    if (rc < (int)(1024 * 768 * 4)) {
        serial_write("[wallpaper] raw load failed or too small rc=");
        serial_write_hex((u32)rc);
        serial_write("\n");
        return 0;
    }
    /* If screen is exactly 1024x768, we're done */
    if ((u32)w == 1024u && (u32)h == 768u) {
        serial_write("[wallpaper] raw loaded ok\n");
        return 1;
    }
    /* Otherwise nearest-neighbor scale in-place (rare case) */
    {
        int x, y;
        for (y = h - 1; y >= 0; y--) {
            int sy = (y * 768) / (h > 0 ? h : 1);
            for (x = w - 1; x >= 0; x--) {
                int sx = (x * 1024) / (w > 0 ? w : 1);
                wallpaper_cache[y * w + x] = wallpaper_cache[sy * 1024 + sx];
            }
        }
    }
    return 1;
}

static int load_wallpaper_bmp_into_cache(int w, int h) {
    int bytes;
    unsigned char *bmp = wallpaper_file_data;
    int data_offset;
    int dib_size;
    int width;
    int height;
    int planes;
    int bpp;
    int compression;
    int row_stride;
    int x, y;
    if (!wallpaper_path[0]) return 0;
    bytes = fs_read_at(wallpaper_path[0] == '/' ? wallpaper_path : "/wallpapers/default_wallpaper.raw", (char*)wallpaper_file_data, sizeof(wallpaper_file_data));
    if (bytes < 54) return 0;
    if (bmp[0] != 'B' || bmp[1] != 'M') return 0;
    data_offset = bmp[10] | (bmp[11] << 8) | (bmp[12] << 16) | (bmp[13] << 24);
    dib_size = bmp[14] | (bmp[15] << 8) | (bmp[16] << 16) | (bmp[17] << 24);
    width = bmp[18] | (bmp[19] << 8) | (bmp[20] << 16) | (bmp[21] << 24);
    height = bmp[22] | (bmp[23] << 8) | (bmp[24] << 16) | (bmp[25] << 24);
    planes = bmp[26] | (bmp[27] << 8);
    bpp = bmp[28] | (bmp[29] << 8);
    compression = bmp[30] | (bmp[31] << 8) | (bmp[32] << 16) | (bmp[33] << 24);
    if (dib_size < 40 || width <= 0 || height == 0 || planes != 1) return 0;
    if (bpp != 24 && bpp != 32) return 0;
    if (compression != 0) return 0;
    if (height < 0) height = -height;
    row_stride = ((width * (bpp / 8)) + 3) & ~3;
    if (data_offset + row_stride * height > bytes) return 0;
    for (y = 0; y < h; y++) {
        int src_y = ((h - 1 - y) * height) / (h > 0 ? h : 1);
        unsigned char *row = bmp + data_offset + src_y * row_stride;
        for (x = 0; x < w; x++) {
            int src_x = (x * width) / (w > 0 ? w : 1);
            unsigned char *px = row + src_x * (bpp / 8);
            wallpaper_cache[y * w + x] = ((u32)px[2] << 16) | ((u32)px[1] << 8) | (u32)px[0];
        }
    }
    wallpaper_jpg_ready = 1;
    return 1;
}

static void paint_external_wallpaper_into_cache(int w, int h) {
    int x, y;
    u32 base = wallpaper_color_from_path(wallpaper_path);
    u32 accent_a = ((base >> 1) & 0x7F7F7F) + 0x101820;
    u32 accent_b = ((base + 0x203040u) & 0xE0FFFFu) | 0x101010u;
    /* Try .raw format first (fastest — direct pixel copy) */
    if (load_wallpaper_raw_into_cache(w, h)) return;
    /* Try .bmp format */
    if (load_wallpaper_bmp_into_cache(w, h)) return;
    /* Fallback: color gradient based on filename */
#define WC_FILL(X0, Y0, W, H, COL) do { \
    int _x0=(X0),_y0=(Y0),_x1=(X0)+(W),_y1=(Y0)+(H); \
    if(_x0<0)_x0=0; if(_y0<0)_y0=0; \
    if(_x1>w)_x1=w;  if(_y1>h)_y1=h; \
    for(int _r=_y0;_r<_y1;_r++) { \
        u32 *_row=wallpaper_cache+_r*(u32)w+_x0; \
        for(int _c=0;_c<(_x1-_x0);_c++) _row[_c]=(COL); \
    } \
} while(0)
    for (y = 0; y < h; y++) {
        u32 row_color = base + (u32)((y * 24) / (h > 0 ? h : 1));
        WC_FILL(0, y, w, 1, row_color);
    }
    for (x = 0; x < w; x += 48) {
        int band = ((x / 48) & 1) ? 10 : 6;
        WC_FILL(x, 0, band, h, ((x / 48) & 1) ? accent_a : accent_b);
    }
    WC_FILL(0, h - 22, w, 22, 0x101722);
#undef WC_FILL
}

static void paint_wallpaper_into_cache(void) {
    int w = (int)fb.width;
    int h = (int)fb.height;
    int x, y, i;
    /* Cheap solid-color fill helper writing into wallpaper_cache */
#define WC_FILL(X0, Y0, W, H, COL) do { \
    int _x0=(X0),_y0=(Y0),_x1=(X0)+(W),_y1=(Y0)+(H); \
    if(_x0<0)_x0=0; if(_y0<0)_y0=0; \
    if(_x1>w)_x1=w;  if(_y1>h)_y1=h; \
    for(int _r=_y0;_r<_y1;_r++) { \
        u32 *_row=wallpaper_cache+_r*(u32)w+_x0; \
        for(int _c=0;_c<(_x1-_x0);_c++) _row[_c]=(COL); \
    } \
} while(0)

    if (wallpaper_source == 1) {
        paint_external_wallpaper_into_cache(w, h);
    } else if (wallpaper_source == 2) {
        // Solid color wallpaper
        WC_FILL(0, 0, w, h, wallpaper_solid_color);
    } else if (wallpaper_source == 3) {
        // Video wallpaper (ambient moving grid / video poster frame)
        WC_FILL(0, 0, w, h, 0x0C1524);
        for (y = 0; y < h; y += 12) WC_FILL(0, y, w, 2, 0x142236);
        for (x = 0; x < w; x += 32) WC_FILL(x, 0, 1, h, 0x18283E);
        WC_FILL(0, h - 80, w, 80, 0x080F1A);
    } else if (wallpaper_id == 1) {
        WC_FILL(0,0,w,h,0x231736);
        for(i=0;i<h;i+=18) WC_FILL(0,i,w,18,(i/18)%2?0x35204D:0x43285D);
        WC_FILL(0,h-110,w,110,0x1A132A);
    } else if (wallpaper_id == 2) {
        WC_FILL(0,0,w,h,0x163627);
        for(x=0;x<w;x+=24) WC_FILL(x,0,10,h,(x/24)%2?0x1F4A33:0x285B3F);
        WC_FILL(0,h-96,w,96,0x0F2419);
    } else {
        WC_FILL(0,0,w,h,0x102033);
        for(y=0;y<h;y+=20) WC_FILL(0,y,w,20,(y/20)%2?0x16304D:0x1C3A5D);
        WC_FILL(0,h-100,w,100,0x0D1828);
    }
#undef WC_FILL
}

static int desktop_wallpaper_dirty = 1;
void gui_mark_wallpaper_dirty(void) {
    desktop_wallpaper_dirty = 1;
    needs_redraw = 1;
}

static void draw_wallpaper(void) {
    if (!desktop_wallpaper_dirty) return;
    u32 w = fb.width, h = fb.height;
    u32 y;
    int path_changed = 0;
    /* Check if external wallpaper path changed */
    if (wallpaper_source == 1) {
        int pi = 0;
        while (wallpaper_path[pi] && wallpaper_cached_path[pi] && wallpaper_path[pi] == wallpaper_cached_path[pi]) pi++;
        path_changed = (wallpaper_path[pi] != wallpaper_cached_path[pi]);
    }
    /* Repaint cache only when wallpaper selection changes */
    if (wallpaper_cache_dirty || wallpaper_cached_id != wallpaper_id || wallpaper_cached_source != wallpaper_source || path_changed) {
        paint_wallpaper_into_cache();
        wallpaper_cached_id     = wallpaper_id;
        wallpaper_cached_source = wallpaper_source;
        wallpaper_cache_dirty   = 0;
        /* Copy path */
        {
            int pi = 0;
            while (wallpaper_path[pi] && pi + 1 < 128) { wallpaper_cached_path[pi] = wallpaper_path[pi]; pi++; }
            wallpaper_cached_path[pi] = 0;
        }
    }
    /* Blit cache into backbuffer using fast assembly memcpy */
    if (!fb.backbuffer) return;
    if (w > MAX_SCREEN_WIDTH || h > MAX_SCREEN_HEIGHT) return;
    if (fb.pitch == w * 4) {
        fast_memcpy32(fb.backbuffer, wallpaper_cache, w * h);
    } else {
        for (y = 0; y < h; y++) {
            u32 *src = wallpaper_cache + y * w;
            u32 *dst = (u32*)(fb.backbuffer + y * fb.pitch);
            fast_memcpy32(dst, src, w);
        }
    }
    /* Mark full screen dirty without overwriting pixels */
    fb_mark_dirty_full();
    desktop_wallpaper_dirty = 0;
}

void font_text_fit_center(int x, int y, const char *text, u32 color, int max_width) {
    int i = 0;
    int length = 0;
    int visible;
    int start_x;
    while (text[length]) length++;
    visible = max_width / 6;
    if (visible <= 0) return;
    if (length <= visible) {
        start_x = x + (max_width - length * 6) / 2;
        draw_label(start_x, y, text, color);
    } else {
        start_x = x + (max_width - visible * 6) / 2;
        for (i = 0; i < visible; i++) {
            char one[2];
            one[0] = text[i];
            one[1] = 0;
            draw_label(start_x + i * 6, y, one, color);
        }
    }
}

void font_text_fit(int x, int y, const char *text, u32 color, int max_width) {
    int i = 0;
    int length = 0;
    while (text[length]) length++;
    if (length * 6 <= max_width) {
        draw_label(x, y, text, color);
    } else {
        for (i = 0; i < length && i < max_width / 6; i++) {
            char one[2];
            one[0] = text[i];
            one[1] = 0;
            draw_label(x + i * 6, y, one, color);
        }
    }
}

static void time_text(char out[9]) {
    out[0] = '0' + (clock_hours / 10);
    out[1] = '0' + (clock_hours % 10);
    out[2] = ':';
    out[3] = '0' + (clock_minutes / 10);
    out[4] = '0' + (clock_minutes % 10);
    out[5] = ':';
    out[6] = '0' + (clock_seconds / 10);
    out[7] = '0' + (clock_seconds % 10);
    out[8] = 0;
}

static void fps_text(char out[16]) {
    int value = fps_value;
    if (value < 0) value = 0;
    if (value > 999) value = 999;
    out[0] = 'F';
    out[1] = 'P';
    out[2] = 'S';
    out[3] = ':';
    out[4] = ' ';
    if (value >= 100) {
        out[5] = '0' + ((value / 100) % 10);
        out[6] = '0' + ((value / 10) % 10);
        out[7] = '0' + (value % 10);
        out[8] = 0;
    } else {
        out[5] = '0' + ((value / 10) % 10);
        out[6] = '0' + (value % 10);
        out[7] = 0;
    }
}

static int month_days(int month, int year) {
    if (month == 2) {
        int leap = ((year % 4) == 0 && ((year % 160) != 0 || (year % 400) == 0));
        return leap ? 29 : 28;
    }
    if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
    return 31;
}

static int day_of_week(int day, int month, int year) {
    static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3) year -= 1;
    return (year + year/4 - year/100 + year/400 + t[month - 1] + day) % 7;
}

static void update_rtc_clock(void) {
    rtc_time_t now;
    rtc_read(&now);
    clock_hours = now.hour;
    clock_minutes = now.minute;
    clock_seconds = now.second;
    rtc_cached_second = clock_seconds;
}

int gui_needs_redraw(void) {
    int x;
    int y;
    int buttons;
    gui_capture_event(&gui_last_event);
    x = gui_last_event.x;
    y = gui_last_event.y;
    buttons = gui_last_event.button;
    update_keyboard_input(gui_last_event.key);
    while (1) {
        unsigned char next_k = ps2_last_key();
        if (!next_k) break;
        update_keyboard_input(next_k);
        needs_redraw = 1;
    }
    handle_mouse_wheel();
    if (gui_mode != GUI_MODE_LOGIN) {
        if (!post_boot_fs_loaded && post_boot_fs_attempted && clock_seconds != post_boot_fs_deadline_ticks) {
            post_boot_fs_loaded = 1;
            kernel_post_boot_fs_load();
            needs_redraw = 1;
        }
    }
    if (gui_last_event.type != GUI_EVENT_NONE) needs_redraw = 1;
    if (x != last_mouse_x || y != last_mouse_y || buttons != last_mouse_buttons) needs_redraw = 1;
    if (media_player_is_active_playing()) {
        media_player_tick();
    }
    app_window_t *browser_app = app_slot(APP_BROWSER);
    if (browser_app && browser_app->open && !browser_app->minimized) {
        web_browser_app_tick();
    }
    return needs_redraw;
}

void gui_mark_redraw(void) { needs_redraw = 1; }
int gui_power_off_requested(void) { return power_off_requested; }

static void sync_main_window_parts(void) {
    main_title_bar = (rect_t){main_window.x, main_window.y, main_window.w, 36, title_bg};
    main_button_close = (rect_t){main_window.x + 12, main_window.y + 12, 12, 12, 0xFF5F56};
    main_button_min = (rect_t){main_window.x + 32, main_window.y + 12, 12, 12, 0xFFBD2E};
    main_button_max = (rect_t){main_window.x + 52, main_window.y + 12, 12, 12, 0x27C93F};
}

static void sync_app_window(app_window_t *app) {
    int ty = (titlebar_position == 1) ? app->frame.y + app->frame.h - 28 : app->frame.y;
    app->title = (rect_t){app->frame.x, ty, app->frame.w, 28, app->title_color};
    app->close = (rect_t){app->frame.x + 12, ty + 8, 12, 12, 0xFF5F56};
    app->minimize = (rect_t){app->frame.x + 32, ty + 8, 12, 12, 0xFFBD2E};
    app->maximize = (rect_t){app->frame.x + 52, ty + 8, 12, 12, 0x27C93F};
    
    int ry = (titlebar_position == 1) ? app->frame.y + 4 : app->frame.y + app->frame.h - 14;
    app->resize = (rect_t){app->frame.x + app->frame.w - 14, ry, 10, 10, 0x5A6D82};
}

app_window_t* app_slot(int kind) {
    if (kind <= 0 || kind > 32) return 0;
    if (active_app_index >= 0 && active_app_index < 32 && app_windows[active_app_index].kind == kind && app_windows[active_app_index].open) {
        return &app_windows[active_app_index];
    }
    for (int i = 31; i >= 0; i--) {
        int idx = z_order[i];
        if (app_windows[idx].open && app_windows[idx].kind == kind) {
            return &app_windows[idx];
        }
    }
    return &app_windows[kind - 1];
}

void focus_app_window(int index) {
    int i = 0;
    int pos = -1;
    int moved;
    if (index < 0 || index >= 32 || !app_windows[index].open) return;
    for (i = 0; i < 32; i++) if (z_order[i] == index) { pos = i; break; }
    if (pos >= 0) {
        moved = z_order[pos];
        for (i = pos; i < 31; i++) z_order[i] = z_order[i + 1];
        z_order[31] = moved;
    }
    active_app_index = index;
    cursor_blink_visible = 1;
    cursor_blink_ticks = 0;
    needs_redraw = 1;
}

static void begin_window_transition(int index, int mode) {
    desktop_wallpaper_dirty = 1;
    if (!window_animations) return;
    if (index < 0 || index >= 32) return;
    window_transition_mode[index] = mode;
    window_transition_ticks[index] = 8;
    needs_redraw = 1;
}

static void toggle_taskbar_app(int kind) {
    app_window_t *app = app_slot(kind);
    if (!app || !app->open) { open_app(kind); return; }
    int idx = (int)(app - app_windows);
    if (app->minimized) { app->minimized = 0; begin_window_transition(idx, 2); focus_app_window(idx); }
    else if (active_app_window() == app) { begin_window_transition(idx, 1); app->minimized = 1; }
    else focus_app_window(idx);
    needs_redraw = 1;
}

app_window_t* open_new_window(int kind) {
    if (kind <= 0 || kind > 12) return 0;
    if (!app_windows[kind - 1].open) {
        open_app(kind);
        return &app_windows[kind - 1];
    }
    int slot = -1;
    int instance_count = 1;
    for (int i = 0; i < 32; i++) {
        if (app_windows[i].open && app_windows[i].kind == kind) {
            instance_count++;
        }
    }
    for (int i = 12; i < 32; i++) {
        if (!app_windows[i].open) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return 0;
    app_window_t *app = &app_windows[slot];
    app->open = 1;
    app->minimized = 0;
    app->maximized = 0;
    app->resizing = 0;
    app->snapped = 0;
    app->kind = kind;
    app->instance_id = instance_count;
    app->min_w = kind == APP_TEXT_EDIT ? 580 : (kind == APP_MEDIA_PLAYER ? 440 : 220);
    app->min_h = kind == APP_TEXT_EDIT ? 320 : (kind == APP_MEDIA_PLAYER ? 280 : 140);
    app->last_title_click_ticks = 0;
    int offset = (slot - 11) * 32;
    int def_x = 80 + (kind - 1) * 16 + offset;
    int def_y = 50 + (kind - 1) * 12 + offset;
    if (def_x + 400 > (int)fb.width) def_x = 80 + (offset % 100);
    if (def_y + 300 > (int)fb.height) def_y = 50 + (offset % 100);
    int w = kind == APP_TEXT_EDIT ? 680 : (kind == APP_EXPLORER ? 560 : (kind == APP_SETTINGS ? 640 : (kind == APP_PREVIEW ? 580 : (kind == APP_MEDIA_PLAYER ? 560 : (kind == APP_TERMINAL ? 520 : 320)))));
    int h = kind == APP_TEXT_EDIT ? 480 : (kind == APP_EXPLORER ? 360 : (kind == APP_SETTINGS ? 440 : (kind == APP_PREVIEW ? 440 : (kind == APP_MEDIA_PLAYER ? 380 : (kind == APP_TERMINAL ? 320 : 220)))));
    app->frame = (rect_t){def_x, def_y, w, h, 0};
    app->saved_x = def_x;
    app->saved_y = def_y;
    app->saved_w = w;
    app->saved_h = h;
    app->title_color = kind == APP_TERMINAL ? 0x315A8A : kind == APP_FILES ? 0x3B6D72 : kind == APP_DISPLAY ? 0x5A4D86 : kind == APP_EXPLORER ? 0x756235 : kind == APP_SETTINGS ? 0x465D8C : kind == APP_PREVIEW ? 0x2A3E54 : kind == APP_MEDIA_PLAYER ? 0x1E2B3D : 0x7A4D2F;
    app->tab_count = 1;
    app->active_tab = 0;
    for (int t = 0; t < MAX_WINDOW_TABS; t++) {
        app->tabs[t].used = (t == 0);
        app->tabs[t].scroll = 0;
        app->tabs[t].cursor = 0;
        app->tabs[t].title[0] = 0;
        app->tabs[t].path[0] = 0;
    }
    const char *kname = app_name_from_kind(kind);
    int ki = 0;
    while (kname[ki] && ki < 31) { app->tabs[0].title[ki] = kname[ki]; ki++; }
    app->tabs[0].title[ki] = 0;
    if (kind == APP_EXPLORER) {
        const char *rpath = "FirstOS";
        int pi = 0;
        while (rpath[pi] && pi < 127) { app->tabs[0].path[pi] = rpath[pi]; app->current_path[pi] = rpath[pi]; pi++; }
        app->tabs[0].path[pi] = 0;
        app->current_path[pi] = 0;
    } else if (kind == APP_TEXT_EDIT) {
        extern void text_edit_new_file(void);
        text_edit_new_file();
    } else if (kind == APP_TERMINAL) {
        extern void terminal_clear(void);
        terminal_clear();
    }
    sync_app_window(app);
    begin_window_transition(slot, 2);
    focus_app_window(slot);
    needs_redraw = 1;
    return app;
}

static void draw_window_tab_bar(app_window_t *app, int x, int y, int w) {
    if (!app) return;
    if (app->tab_count < 1) app->tab_count = 1;
    if (app->tab_count > MAX_WINDOW_TABS) app->tab_count = MAX_WINDOW_TABS;

    fb_rect(x, y, w, 24, 0xD4DCD4);
    fb_rect(x, y + 23, w, 1, 0xB0BCB0);

    int tab_gap = 4;
    int max_tab_w = 120;
    int available_w = w - 40;
    int tab_w = available_w / (app->tab_count > 0 ? app->tab_count : 1) - tab_gap;
    if (tab_w > max_tab_w) tab_w = max_tab_w;
    if (tab_w < 60) tab_w = 60;

    int cur_x = x + 6;
    for (int t = 0; t < app->tab_count; t++) {
        int is_active = (app->active_tab == t);
        rect_t tr = (rect_t){cur_x, y + 2, tab_w, 21, is_active ? 0xFAFCFA : 0xDFE6DF};
        app->tab_rects[t] = tr;

        fb_rect(tr.x, tr.y, tr.w, tr.h, tr.color);
        outline(tr.x, tr.y, tr.w, tr.h, is_active ? 0x7E927E : 0xB0BCB0);
        if (is_active) {
            fb_rect(tr.x + 1, tr.y + 1, tr.w - 2, 2, 0x4A8C4A);
            fb_rect(tr.x + 1, y + 23, tr.w - 2, 1, tr.color);
        }

        const char *tname = app->tabs[t].title[0] ? app->tabs[t].title : "Tab";
        u32 text_col = is_active ? 0x1A281A : 0x556655;
        font_text_fit(tr.x + 6, y + 5, tname, text_col, tab_w - 24);

        rect_t close_r = (rect_t){tr.x + tr.w - 18, y + 4, 14, 14, 0};
        app->tab_close_rects[t] = close_r;
        int pmx = ps2_mouse_x(), pmy = ps2_mouse_y();
        int hov_close = point_in_rect(pmx, pmy, close_r);
        if (hov_close) {
            fb_rect(close_r.x, close_r.y, close_r.w, close_r.h, 0xE06060);
            outline(close_r.x, close_r.y, close_r.w, close_r.h, 0xC04040);
            draw_label(close_r.x + 4, close_r.y + 2, "x", 0xFFFFFF);
        } else {
            draw_label(close_r.x + 4, close_r.y + 2, "x", is_active ? 0x667766 : 0x889988);
        }

        cur_x += tab_w + tab_gap;
    }

    if (app->tab_count < MAX_WINDOW_TABS) {
        rect_t new_r = (rect_t){cur_x, y + 3, 20, 19, 0xE2E8E2};
        app->tab_new_rect = new_r;
        int pmx = ps2_mouse_x(), pmy = ps2_mouse_y();
        int hov_new = point_in_rect(pmx, pmy, new_r);
        fb_rect(new_r.x, new_r.y, new_r.w, new_r.h, hov_new ? 0xF0F5F0 : 0xE2E8E2);
        outline(new_r.x, new_r.y, new_r.w, new_r.h, 0x90A090);
        draw_label(new_r.x + 6, new_r.y + 3, "+", 0x223322);
    } else {
        app->tab_new_rect = (rect_t){0, 0, 0, 0, 0};
    }
}

static int handle_window_tab_click(app_window_t *app, int mx, int my) {
    if (!app) return 0;

    if (app->tab_new_rect.w > 0 && point_in_rect(mx, my, app->tab_new_rect)) {
        if (app->tab_count < MAX_WINDOW_TABS) {
            int new_idx = app->tab_count;
            app->tabs[new_idx].used = 1;
            app->tabs[new_idx].scroll = 0;
            app->tabs[new_idx].cursor = 0;

            if (app->kind == APP_EXPLORER) {
                const char *def = "FirstOS";
                int pi = 0;
                while (def[pi] && pi < 127) { app->tabs[new_idx].path[pi] = def[pi]; pi++; }
                app->tabs[new_idx].path[pi] = 0;

                char tbuf[32];
                int ti = 0;
                const char *tprefix = "Explorer ";
                while (tprefix[ti] && ti < 20) { tbuf[ti] = tprefix[ti]; ti++; }
                tbuf[ti++] = '0' + (new_idx + 1);
                tbuf[ti] = 0;
                int j = 0;
                while (tbuf[j] && j < 31) { app->tabs[new_idx].title[j] = tbuf[j]; j++; }
                app->tabs[new_idx].title[j] = 0;

                explorer_open_path(app->tabs[new_idx].path);
            } else {
                char tbuf[32];
                int ti = 0;
                const char *tprefix = "Tab ";
                while (tprefix[ti] && ti < 20) { tbuf[ti] = tprefix[ti]; ti++; }
                tbuf[ti++] = '0' + (new_idx + 1);
                tbuf[ti] = 0;
                int j = 0;
                while (tbuf[j] && j < 31) { app->tabs[new_idx].title[j] = tbuf[j]; j++; }
                app->tabs[new_idx].title[j] = 0;
            }
            app->active_tab = new_idx;
            app->tab_count++;
            needs_redraw = 1;
            return 1;
        }
        return 0;
    }

    for (int t = 0; t < app->tab_count; t++) {
        if (point_in_rect(mx, my, app->tab_close_rects[t])) {
            if (app->tab_count <= 1) {
                app->open = 0;
                needs_redraw = 1;
                return 1;
            }
            for (int s = t; s < app->tab_count - 1; s++) {
                app->tabs[s] = app->tabs[s + 1];
            }
            app->tab_count--;
            app->tabs[app->tab_count].used = 0;
            if (app->active_tab >= app->tab_count) {
                app->active_tab = app->tab_count - 1;
            } else if (app->active_tab > t) {
                app->active_tab--;
            }
            if (app->kind == APP_EXPLORER && app->tabs[app->active_tab].path[0]) {
                explorer_open_path(app->tabs[app->active_tab].path);
            }
            needs_redraw = 1;
            return 1;
        }
        if (point_in_rect(mx, my, app->tab_rects[t])) {
            app->active_tab = t;
            if (app->kind == APP_EXPLORER && app->tabs[t].path[0]) {
                explorer_open_path(app->tabs[t].path);
            }
            needs_redraw = 1;
            return 1;
        }
    }
    return 0;
}

static void sync_explorer_active_tab(app_window_t *app) {
    if (!app || app->active_tab < 0 || app->active_tab >= MAX_WINDOW_TABS) return;
    const char *p = explorer_current_path();
    if (!p) return;
    int i = 0;
    while (p[i] && i < 127) {
        app->tabs[app->active_tab].path[i] = p[i];
        app->current_path[i] = p[i];
        i++;
    }
    app->tabs[app->active_tab].path[i] = 0;
    app->current_path[i] = 0;

    int last_slash = -1;
    for (int k = 0; p[k]; k++) {
        if (p[k] == '/') last_slash = k;
    }
    const char *sub = (last_slash >= 0 && p[last_slash + 1]) ? &p[last_slash + 1] : p;
    int ti = 0;
    while (sub[ti] && ti < 24) {
        app->tabs[app->active_tab].title[ti] = sub[ti];
        ti++;
    }
    app->tabs[app->active_tab].title[ti] = 0;
}

static void layout_desktop_context_menu(int mx, int my) {
    int y = my;
    int visible_items = desktop_context_target ? 4 : 10;
    int menu_h = 14 + visible_items * 26;
    if (mx + 172 > (int)fb.width) mx = (int)fb.width - 172;
    if (y + menu_h > (int)fb.height) y = (int)fb.height - menu_h;
    if (mx < 4) mx = 4;
    if (y < 30) y = 30;
    desktop_context_menu = (rect_t){mx, y, 172, menu_h, 0xD7DEE8};
    if (desktop_context_target) {
        desktop_ctx_copy = (rect_t){mx + 10, y + 10, 152, 20, 0xB4C6D6};
        desktop_ctx_paste = (rect_t){mx + 10, y + 36, 152, 20, 0xB4C6D6};
        desktop_ctx_rename = (rect_t){mx + 10, y + 62, 152, 20, 0xB4C6D6};
        desktop_ctx_delete = (rect_t){mx + 10, y + 88, 152, 20, 0xC99393};
        return;
    }
    int cur_y = y + 10;
    desktop_ctx_icon_size_s = (rect_t){mx + 68, cur_y, 26, 20, 0};
    desktop_ctx_icon_size_m = (rect_t){mx + 98, cur_y, 26, 20, 0};
    desktop_ctx_icon_size_l = (rect_t){mx + 128, cur_y, 26, 20, 0};
    cur_y += 26;

    desktop_ctx_add_widgets = (rect_t){mx + 10, cur_y, 152, 20, 0}; cur_y += 26;
    desktop_ctx_refresh     = (rect_t){mx + 10, cur_y, 152, 20, 0xBBC8D8}; cur_y += 26;
    desktop_ctx_new_folder  = (rect_t){mx + 10, cur_y, 152, 20, 0x9DBB8D}; cur_y += 26;
    desktop_ctx_new_file    = (rect_t){mx + 10, cur_y, 152, 20, 0x90A9C8}; cur_y += 26;
    desktop_ctx_change_wall = (rect_t){mx + 10, cur_y, 152, 20, 0x98B8D8}; cur_y += 26;
    desktop_ctx_sort_name   = (rect_t){mx + 10, cur_y, 152, 20, 0xB4C6D6}; cur_y += 26;
    desktop_ctx_sort_date   = (rect_t){mx + 10, cur_y, 152, 20, 0xB4C6D6}; cur_y += 26;
    desktop_ctx_sort_kind   = (rect_t){mx + 10, cur_y, 152, 20, 0xB4C6D6}; cur_y += 26;
    desktop_ctx_paste       = (rect_t){mx + 10, cur_y, 152, 20, 0xB4C6D6}; cur_y += 26;
}

static void draw_desktop_context_menu(void) {
    int base_x;
    int base_y;
    if (!desktop_context_open) return;
    fb_rect(desktop_context_menu.x + 6, desktop_context_menu.y + 6, desktop_context_menu.w, desktop_context_menu.h, shadow);
    fill(desktop_context_menu);
    outline(desktop_context_menu.x, desktop_context_menu.y, desktop_context_menu.w, desktop_context_menu.h, 0x5D738E);
    base_x = desktop_context_menu.x + 10;
    base_y = desktop_context_menu.y + 10;
    if (desktop_context_target) {
        draw_button((rect_t){base_x, base_y + 0, 152, 20, 0xB4C6D6}, 0xB4C6D6, 0xCFDCE7, "COPY", 0x20344E);
        draw_button((rect_t){base_x, base_y + 26, 152, 20, 0xB4C6D6}, 0xB4C6D6, 0xCFDCE7, "PASTE", 0x20344E);
        draw_button((rect_t){base_x, base_y + 52, 152, 20, 0xB4C6D6}, 0xB4C6D6, 0xCFDCE7, "RENAME", 0x20344E);
        draw_button((rect_t){base_x, base_y + 78, 152, 20, 0xC99393}, 0xC99393, 0xDBB0B0, "DELETE", 0x4E1F1F);
        return;
    }

    // Row 1: Icon Size Selector
    draw_label(base_x + 2, desktop_ctx_icon_size_s.y + 4, "ICONS:", 0x20344E);
    int s_act = (desktop_icon_size == ICON_SIZE_SMALL);
    fb_round_rect(desktop_ctx_icon_size_s.x, desktop_ctx_icon_size_s.y, desktop_ctx_icon_size_s.w, desktop_ctx_icon_size_s.h, 4, s_act ? 0x2563EB : 0xB4C6D6);
    fb_round_rect_border(desktop_ctx_icon_size_s.x, desktop_ctx_icon_size_s.y, desktop_ctx_icon_size_s.w, desktop_ctx_icon_size_s.h, 4, s_act ? 0x1D4ED8 : 0x7FA2C9);
    draw_label(desktop_ctx_icon_size_s.x + 9, desktop_ctx_icon_size_s.y + 4, "S", s_act ? 0xFFFFFF : 0x20344E);

    int m_act = (desktop_icon_size == ICON_SIZE_MEDIUM);
    fb_round_rect(desktop_ctx_icon_size_m.x, desktop_ctx_icon_size_m.y, desktop_ctx_icon_size_m.w, desktop_ctx_icon_size_m.h, 4, m_act ? 0x2563EB : 0xB4C6D6);
    fb_round_rect_border(desktop_ctx_icon_size_m.x, desktop_ctx_icon_size_m.y, desktop_ctx_icon_size_m.w, desktop_ctx_icon_size_m.h, 4, m_act ? 0x1D4ED8 : 0x7FA2C9);
    draw_label(desktop_ctx_icon_size_m.x + 9, desktop_ctx_icon_size_m.y + 4, "M", m_act ? 0xFFFFFF : 0x20344E);

    int l_act = (desktop_icon_size == ICON_SIZE_LARGE);
    fb_round_rect(desktop_ctx_icon_size_l.x, desktop_ctx_icon_size_l.y, desktop_ctx_icon_size_l.w, desktop_ctx_icon_size_l.h, 4, l_act ? 0x2563EB : 0xB4C6D6);
    fb_round_rect_border(desktop_ctx_icon_size_l.x, desktop_ctx_icon_size_l.y, desktop_ctx_icon_size_l.w, desktop_ctx_icon_size_l.h, 4, l_act ? 0x1D4ED8 : 0x7FA2C9);
    draw_label(desktop_ctx_icon_size_l.x + 9, desktop_ctx_icon_size_l.y + 4, "L", l_act ? 0xFFFFFF : 0x20344E);

    // Row 2: Add Widgets...
    draw_button(desktop_ctx_add_widgets, 0x8EAED4, 0xB0CEF0, "+ ADD WIDGETS...", 0x10253E);

    // Remaining rows
    draw_button(desktop_ctx_refresh, 0xBBC8D8, 0xD5E1EC, "REFRESH", 0x20344E);
    draw_button(desktop_ctx_new_folder, 0x9DBB8D, 0xC4D9BA, "NEW FOLDER", 0x24411D);
    draw_button(desktop_ctx_new_file, 0x90A9C8, 0xB3C5DB, "NEW TEXT FILE", 0x1A293E);
    draw_button(desktop_ctx_change_wall, 0x98B8D8, 0xC0D4E8, "CHANGE WALLPAPER", 0x18324E);
    draw_button(desktop_ctx_sort_name, 0xB4C6D6, 0xCFDCE7, "SORT BY: NAME", 0x20344E);
    draw_button(desktop_ctx_sort_date, 0xB4C6D6, 0xCFDCE7, "SORT BY: DATE", 0x20344E);
    draw_button(desktop_ctx_sort_kind, 0xB4C6D6, 0xCFDCE7, "SORT BY: KIND", 0x20344E);
    draw_button(desktop_ctx_paste, 0xB4C6D6, 0xCFDCE7, "PASTE", 0x20344E);
}

static void draw_widgets_window(void) {
    if (!widgets_window_open) return;
    int wx = widgets_window_rect.x;
    int wy = widgets_window_rect.y;
    int ww = widgets_window_rect.w;
    int wh = widgets_window_rect.h;

    // Window shadow
    fb_draw_window_shadow(wx, wy, ww, wh, 14, 1);

    // Glass rounded card body
    fb_round_rect_alpha(wx, wy, ww, wh, 10, 0x121A28, 240);
    fb_round_rect_border(wx, wy, ww, wh, 10, 0x3E526A);

    // Title bar
    fb_round_rect_top_alpha(wx, wy, ww, 26, 10, 0x1E2E42, 245);
    fb_rect(wx, wy + 25, ww, 1, 0x475E7B);
    draw_label(wx + 12, wy + 7, "Desktop Widgets", 0xFFFFFF);

    // Close button [X]
    int chov = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), (rect_t){wx + ww - 24, wy + 4, 18, 18, 0});
    fb_round_rect(wx + ww - 24, wy + 4, 18, 18, 4, chov ? 0xEF4444 : 0x2A3D54);
    draw_label(wx + ww - 18, wy + 5, "X", 0xFFFFFF);

    // Header tip
    draw_label(wx + 14, wy + 32, "Toggle widgets to display on desktop:", 0x93AEC6);

    // Rows for each widget
    // Row 1: Clock
    int r1_y = wy + 50;
    int c_chk = widget_clock.enabled;
    fb_round_rect(wx + 14, r1_y + 2, 16, 16, 3, c_chk ? 0x2563EB : 0x223246);
    fb_round_rect_border(wx + 14, r1_y + 2, 16, 16, 3, 0x60A5FA);
    if (c_chk) draw_label(wx + 18, r1_y + 3, "v", 0xFFFFFF);
    draw_label(wx + 38, r1_y + 4, "Clock Widget", 0xFFFFFF);
    // Mode toggle button
    fb_round_rect(wx + ww - 62, r1_y + 1, 48, 18, 4, 0x243B58);
    fb_round_rect_border(wx + ww - 62, r1_y + 1, 48, 18, 4, 0x4D6F98);
    draw_label(wx + ww - 56, r1_y + 3, widget_clock_mode ? "DIGI" : "DIAL", 0x93C5FD);

    // Row 2: Calendar
    int r2_y = wy + 78;
    int cal_chk = widget_calendar.enabled;
    fb_round_rect(wx + 14, r2_y + 2, 16, 16, 3, cal_chk ? 0x2563EB : 0x223246);
    fb_round_rect_border(wx + 14, r2_y + 2, 16, 16, 3, 0x60A5FA);
    if (cal_chk) draw_label(wx + 18, r2_y + 3, "v", 0xFFFFFF);
    draw_label(wx + 38, r2_y + 4, "Calendar Widget", 0xFFFFFF);

    // Row 3: Sticky Notes
    int r3_y = wy + 106;
    int n_chk = widget_notes.enabled;
    fb_round_rect(wx + 14, r3_y + 2, 16, 16, 3, n_chk ? 0x2563EB : 0x223246);
    fb_round_rect_border(wx + 14, r3_y + 2, 16, 16, 3, 0x60A5FA);
    if (n_chk) draw_label(wx + 18, r3_y + 3, "v", 0xFFFFFF);
    draw_label(wx + 38, r3_y + 4, "Sticky Notes Widget", 0xFFFFFF);

    // Row 4: Weather
    int r4_y = wy + 134;
    int w_chk = widget_weather.enabled;
    fb_round_rect(wx + 14, r4_y + 2, 16, 16, 3, w_chk ? 0x2563EB : 0x223246);
    fb_round_rect_border(wx + 14, r4_y + 2, 16, 16, 3, 0x60A5FA);
    if (w_chk) draw_label(wx + 18, r4_y + 3, "v", 0xFFFFFF);
    draw_label(wx + 38, r4_y + 4, "Live Weather Widget", 0xFFFFFF);

    // Bottom action button: Reset Positions
    rect_t btn_reset = {wx + 14, wy + wh - 36, ww - 28, 24, 0};
    int rhov = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), btn_reset);
    fb_round_rect(btn_reset.x, btn_reset.y, btn_reset.w, btn_reset.h, 6, rhov ? 0x3B82F6 : 0x2563EB);
    fb_round_rect_border(btn_reset.x, btn_reset.y, btn_reset.w, btn_reset.h, 6, 0x60A5FA);
    font_text_fit_center(btn_reset.x, btn_reset.y + 6, "RESET WIDGET POSITIONS", 0xFFFFFF, btn_reset.w);
}

static rect_t explorer_ctx_cut;
static rect_t explorer_ctx_set_wall;
static void layout_explorer_context_menu(int mx, int my) {
    int is_media = 0;
    int is_dir = 0;
    if (explorer_context_index >= 0) {
        const explorer_entry_t *e = explorer_entry_at(explorer_context_index);
        if (e && e->name) {
            if (e->is_dir) is_dir = 1;
            if (explorer_is_image_file(e->name) || explorer_is_video_file(e->name)) {
                is_media = 1;
            }
        }
    }

    int extra_h = 0;
    if (is_media) extra_h += 26;
    if (is_dir) extra_h += 26; // for Color
    
    explorer_context_menu = (rect_t){mx, my, 164, 156 + extra_h, 0xD7DEE8};
    explorer_ctx_terminal = (rect_t){mx + 8, my + 8, 148, 20, 0xB4C6D6};
    explorer_ctx_copy = (rect_t){mx + 8, my + 34, 148, 20, 0xB4C6D6};
    explorer_ctx_cut = (rect_t){mx + 8, my + 60, 148, 20, 0xB4C6D6};
    explorer_ctx_paste = (rect_t){mx + 8, my + 86, 148, 20, 0xB4C6D6};
    explorer_ctx_rename = (rect_t){mx + 8, my + 112, 148, 20, 0xB4C6D6};
    explorer_ctx_delete = (rect_t){mx + 8, my + 138, 148, 20, 0xC99393};
    
    int cy = my + 164;
    if (is_media) {
        explorer_ctx_set_wall = (rect_t){mx + 8, cy, 148, 20, 0xB4C6D6};
        cy += 26;
    } else explorer_ctx_set_wall = (rect_t){0,0,0,0,0};
    
    if (is_dir) {
        explorer_ctx_color = (rect_t){mx + 8, cy, 148, 20, 0xB4C6D6};
        cy += 26;
    } else explorer_ctx_color = (rect_t){0,0,0,0,0};
}

static void draw_explorer_context_menu(void) {
    if (!explorer_context_open) return;
    fb_rect(explorer_context_menu.x + 5, explorer_context_menu.y + 5, explorer_context_menu.w, explorer_context_menu.h, shadow);
    fill(explorer_context_menu);
    outline(explorer_context_menu.x, explorer_context_menu.y, explorer_context_menu.w, explorer_context_menu.h, 0x5D738E);
    draw_button(explorer_ctx_terminal, 0xB4C6D6, 0xCFDCE7, "TERMINAL HERE", 0x20344E);
    draw_button(explorer_ctx_copy, 0xB4C6D6, 0xCFDCE7, "COPY", 0x20344E);
    draw_button(explorer_ctx_cut, 0xB4C6D6, 0xCFDCE7, "CUT", 0x20344E);
    draw_button(explorer_ctx_paste, 0xB4C6D6, 0xCFDCE7, "PASTE", 0x20344E);
    draw_button(explorer_ctx_rename, 0xB4C6D6, 0xCFDCE7, "RENAME", 0x20344E);
    draw_button(explorer_ctx_delete, 0xC99393, 0xDBB0B0, "DELETE", 0x4E1F1F);
    if (explorer_ctx_set_wall.w > 0) {
        draw_button(explorer_ctx_set_wall, 0xB4C6D6, 0xCFDCE7, "CHANGE WALLPAPER", 0x20344E);
    }
    if (explorer_ctx_color.w > 0) {
        draw_button(explorer_ctx_color, 0xB4C6D6, 0xCFDCE7, "SET COLOR", 0x20344E);
    }
}
static void layout_taskbar_context_menu(int mx, int my) {
    int menu_x = mx;
    int menu_y = my;
    if (menu_x + 154 > (int)fb.width) menu_x = (int)fb.width - 154;
    if (menu_y + 104 > (int)fb.height) menu_y = (int)fb.height - 104;
    if (menu_x < 4) menu_x = 4;
    if (menu_y < 30) menu_y = 30;
    taskbar_context_menu = (rect_t){menu_x, menu_y, 154, 104, 0xD7DEE8};
    taskbar_ctx_open_restore = (rect_t){menu_x + 8, menu_y + 8, 138, 20, 0x90A9C8};
    taskbar_ctx_minimize = (rect_t){menu_x + 8, menu_y + 34, 138, 20, 0xB4C6D6};
    taskbar_ctx_maximize = (rect_t){menu_x + 8, menu_y + 60, 138, 20, 0xBBC8D8};
    taskbar_ctx_close = (rect_t){menu_x + 8, menu_y + 86, 138, 20, 0xC99393};
}
static void draw_taskbar_context_menu(void) {
    const char *open_label = "OPEN";
    app_window_t *app;
    if (!taskbar_context_open) return;
    fb_rect(taskbar_context_menu.x + 5, taskbar_context_menu.y + 5, taskbar_context_menu.w, taskbar_context_menu.h, shadow);
    fill(taskbar_context_menu);
    outline(taskbar_context_menu.x, taskbar_context_menu.y, taskbar_context_menu.w, taskbar_context_menu.h, 0x5D738E);
    app = (taskbar_context_index >= 0 && taskbar_context_index < 32) ? &app_windows[taskbar_context_index] : 0;
    if (app && !app->minimized) open_label = "RESTORE";
    draw_button(taskbar_ctx_open_restore, 0x90A9C8, 0xBED0E5, open_label, 0x223A56);
    draw_button(taskbar_ctx_minimize, 0xB4C6D6, 0xCFDCE7, "MINIMIZE", 0x20344E);
    draw_button(taskbar_ctx_maximize, 0xBBC8D8, 0xD5E1EC, "MAXIMIZE", 0x20344E);
    draw_button(taskbar_ctx_close, 0xC99393, 0xDBB0B0, "CLOSE", 0x4E1F1F);
}

static void layout_explorer_paste_dialog(void) {
    int w = 268;
    int h = 112;
    int x = ((int)fb.width - w) / 2;
    int y = ((int)fb.height - h) / 2;
    if (x < 24) x = 24;
    if (y < desktop_bar.h + 20) y = desktop_bar.h + 20;
    explorer_paste_dialog = (rect_t){x, y, w, h, 0xE8E0C8};
    explorer_paste_replace_button = (rect_t){x + 12, y + 72, 74, 24, 0xC99393};
    explorer_paste_copy_button = (rect_t){x + 96, y + 72, 74, 24, 0x8FB28A};
    explorer_paste_cancel_button = (rect_t){x + 180, y + 72, 74, 24, 0x9CB0C6};
}

static void draw_explorer_paste_dialog(void) {
    if (!explorer_paste_conflict_open) return;
    layout_explorer_paste_dialog();
    fb_rect(explorer_paste_dialog.x + 8, explorer_paste_dialog.y + 8, explorer_paste_dialog.w, explorer_paste_dialog.h, shadow);
    fill(explorer_paste_dialog);
    outline(explorer_paste_dialog.x, explorer_paste_dialog.y, explorer_paste_dialog.w, explorer_paste_dialog.h, 0x7A6A39);
    fb_rect(explorer_paste_dialog.x, explorer_paste_dialog.y, explorer_paste_dialog.w, 24, 0x8A7740);
    font_text_fit(explorer_paste_dialog.x + 80, explorer_paste_dialog.y + 8, "PASTE WARNING", 0x24344A, explorer_paste_dialog.w - 96);
    font_text_fit(explorer_paste_dialog.x + 14, explorer_paste_dialog.y + 34, "ITEM ALREADY EXISTS", 0x24344A, explorer_paste_dialog.w - 28);
    font_text_fit(explorer_paste_dialog.x + 14, explorer_paste_dialog.y + 50, "REPLACE IT OR COPY A NEW ONE?", 0x4A5870, explorer_paste_dialog.w - 28);
    draw_button(explorer_paste_replace_button, 0xC99393, 0xDBB0B0, "REPLACE", 0x4E1F1F);
    draw_button(explorer_paste_copy_button, 0x8FB28A, 0xBED7B6, "COPY", 0x203E1F);
    draw_button(explorer_paste_cancel_button, 0x9CB0C6, 0xC8D6E3, "CANCEL", 0x233B56);
}


static void set_main_window_fullscreen(int enabled) {
    if (!main_window_open || main_window_minimized) return;
    if (enabled && !fullscreen_mode) {
        saved_x = main_window.x; saved_y = main_window.y; saved_w = main_window.w; saved_h = main_window.h;
        main_window.x = 12; main_window.y = desktop_bar.h + 8;
        main_window.w = (int)fb.width - 24;
        main_window.h = (int)fb.height - desktop_bar.h - dock_rect.h - 24;
        fullscreen_mode = 1;
    } else if (!enabled && fullscreen_mode) {
        main_window.x = saved_x; main_window.y = saved_y; main_window.w = saved_w; main_window.h = saved_h;
        fullscreen_mode = 0;
    }
    sync_main_window_parts();
    needs_redraw = 1;
}

static void set_app_window_maximized(app_window_t *app, int enabled) {
    int idx;
    if (!app || !app->open || app->minimized) return;
    idx = (int)(app - app_windows);
    if (enabled && !app->maximized) {
        if (!app->snapped) {
            app->saved_x = app->frame.x;
            app->saved_y = app->frame.y;
            app->saved_w = app->frame.w;
            app->saved_h = app->frame.h;
        }
        rect_t r = get_snap_preview_rect(SNAP_FULLSCREEN);
        app->frame.x = r.x;
        app->frame.y = r.y;
        app->frame.w = r.w;
        app->frame.h = r.h;
        app->maximized = 1;
        app->snapped = SNAP_FULLSCREEN;
        begin_window_transition(idx, 2);
    } else if (!enabled && app->maximized) {
        if (app->saved_w > 0 && app->saved_h > 0) {
            app->frame.x = app->saved_x;
            app->frame.y = app->saved_y;
            app->frame.w = app->saved_w;
            app->frame.h = app->saved_h;
        }
        app->maximized = 0;
        app->snapped = 0;
        begin_window_transition(idx, 3);
    }
    sync_app_window(app);
    needs_redraw = 1;
}

void gui_set_window_maximized(app_window_t *app, int enabled) {
    set_app_window_maximized(app, enabled);
}

#define CURSOR_BACKUP_SIZE 64
static u32 cursor_backup_pixels[CURSOR_BACKUP_SIZE * CURSOR_BACKUP_SIZE];
static int cursor_backup_x = -1;
static int cursor_backup_y = -1;
static int cursor_backup_w = 0;
static int cursor_backup_h = 0;
static int cursor_backup_valid = 0;

static void gui_cursor_restore(void) {
    if (!cursor_backup_valid || !fb.backbuffer) return;
    int bw = cursor_backup_w;
    int bh = cursor_backup_h;
    int bx = cursor_backup_x;
    int by = cursor_backup_y;
    if (bw <= 0 || bh <= 0) return;

    for (int y = 0; y < bh; y++) {
        int py = by + y;
        if (py < 0 || py >= (int)fb.height) continue;
        u32 *src = cursor_backup_pixels + y * CURSOR_BACKUP_SIZE;
        u32 *dst = (u32*)(fb.backbuffer + py * fb.pitch) + bx;
        fast_memcpy32(dst, src, bw);
    }
    fb_mark_dirty_rect(bx, by, bw, bh);
    cursor_backup_valid = 0;
}

static void gui_cursor_save(int x, int y, int w, int h) {
    if (!fb.backbuffer) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > (int)fb.width) w = (int)fb.width - x;
    if (y + h > (int)fb.height) h = (int)fb.height - y;
    if (w <= 0 || h <= 0) return;
    if (w > CURSOR_BACKUP_SIZE) w = CURSOR_BACKUP_SIZE;
    if (h > CURSOR_BACKUP_SIZE) h = CURSOR_BACKUP_SIZE;

    for (int cy = 0; cy < h; cy++) {
        int py = y + cy;
        u32 *src = (u32*)(fb.backbuffer + py * fb.pitch) + x;
        u32 *dst = cursor_backup_pixels + cy * CURSOR_BACKUP_SIZE;
        fast_memcpy32(dst, src, w);
    }
    cursor_backup_x = x;
    cursor_backup_y = y;
    cursor_backup_w = w;
    cursor_backup_h = h;
    cursor_backup_valid = 1;
}

static void draw_cursor(void) {
    int x = ps2_mouse_x(), y = ps2_mouse_y();
    int s = mouse_cursor_size + 1;
    u32 color = mouse_cursor_color;
    if (ps2_mouse_left()) color = 0x4DA3FF;
    if (ps2_mouse_right()) color = 0xFF6B6B;
    if (ps2_mouse_middle()) color = 0x4CD37B;
    if (ps2_mouse_left() && ps2_mouse_right()) color = 0xFFD166;

    // Check context cursor states
    int is_dragging = mouse_context_cursors_enabled && (mouse_cursor_state == CURSOR_STATE_DRAGGING || window_dragging || (desktop_dragging && desktop_drag_moved) || (explorer_dragging && explorer_drag_moved));
    int is_loading = mouse_context_cursors_enabled && (mouse_cursor_state == CURSOR_STATE_LOADING);
    int is_opening = mouse_context_cursors_enabled && (mouse_cursor_state == CURSOR_STATE_OPENING);

    if (is_dragging) {
        // Closed Grabbing Hand
        const char *grab_hand[] = {
            "   XXXXXX   ",
            "  X......X  ",
            " X........X ",
            "X..........X",
            "X...XXXX...X",
            "X..X....X..X",
            "X..X....X..X",
            "X...XXXX...X",
            "X..........X",
            " X........X ",
            "  XXXXXXXX  "
        };
        for (int cy = 0; cy < 11; cy++) {
            for (int cx = 0; grab_hand[cy][cx]; cx++) {
                if (grab_hand[cy][cx] == 'X') fb_rect(x + cx * s, y + cy * s, s, s, 0x000000);
                else if (grab_hand[cy][cx] == '.') fb_rect(x + cx * s, y + cy * s, s, s, color);
            }
        }
        return;
    }

    if (is_loading) {
        // Animated spinning wheel
        static int spin_tick = 0;
        spin_tick++;
        int phase = (spin_tick / 3) % 8;
        static const int offsets[8][2] = {
            {6, 0}, {10, 2}, {12, 6}, {10, 10}, {6, 12}, {2, 10}, {0, 6}, {2, 2}
        };
        for (int i = 0; i < 8; i++) {
            u32 dot_col = (i == phase) ? 0xFFFFFF : ((i == (phase + 7) % 8) ? 0x88CCFF : 0x335577);
            fb_rect(x + offsets[i][0] * s, y + offsets[i][1] * s, 2 * s, 2 * s, dot_col);
        }
        return;
    }

    if (is_opening) {
        // Draw mini document badge
        fb_rect(x + 12 * s, y + 8 * s, 8 * s, 10 * s, 0xFFFFFF);
        outline(x + 12 * s, y + 8 * s, 8 * s, 10 * s, 0x203040);
        fb_rect(x + 14 * s, y + 10 * s, 4 * s, 2 * s, 0x3D78C0);
        fb_rect(x + 14 * s, y + 13 * s, 4 * s, 2 * s, 0x3D78C0);
    }

    if (mouse_cursor_style == 1) {
        // Modern Slim Precision Arrow
        const char *modern_cursor[] = {
            "X         ",
            "XX        ",
            "X.X       ",
            "X..X      ",
            "X...X     ",
            "X....X    ",
            "X.....X   ",
            "X......X  ",
            "X...XXXX  ",
            "X..X      ",
            "X.X       ",
            "XX        ",
            "X         "
        };
        for (int cy = 0; cy < 13; cy++) {
            for (int cx = 0; modern_cursor[cy][cx]; cx++) {
                if (modern_cursor[cy][cx] == 'X') fb_rect(x + cx * s, y + cy * s, s, s, 0x000000);
                else if (modern_cursor[cy][cx] == '.') fb_rect(x + cx * s, y + cy * s, s, s, color);
            }
        }
    } else if (mouse_cursor_style == 2) {
        // Precision Crosshair
        fb_rect(x + 5 * s, y, 2 * s, 14 * s, color);
        fb_rect(x - 1 * s, y + 6 * s, 14 * s, 2 * s, color);
        outline(x + 5 * s, y, 2 * s, 14 * s, 0x000000);
        outline(x - 1 * s, y + 6 * s, 14 * s, 2 * s, 0x000000);
    } else if (mouse_cursor_style == 3) {
        // Minimal Dot
        fb_rect(x + 2 * s, y + 2 * s, 6 * s, 6 * s, color);
        outline(x + 2 * s, y + 2 * s, 6 * s, 6 * s, 0x000000);
    } else if (mouse_cursor_style == 4) {
        // Retro 3D Arrow
        const char *retro_cursor[] = {
            "X            ",
            "XX           ",
            "X.X          ",
            "X..X         ",
            "X...X        ",
            "X....X       ",
            "X.....X      ",
            "X......X     ",
            "X.......X    ",
            "X....XXXX    ",
            "X..X X       ",
            "X.X   X      ",
            "XX     X     ",
            "X       X    "
        };
        for (int cy = 0; cy < 14; cy++) {
            for (int cx = 0; retro_cursor[cy][cx]; cx++) {
                if (retro_cursor[cy][cx] == 'X') fb_rect(x + cx * s, y + cy * s, s, s, 0x000000);
                else if (retro_cursor[cy][cx] == '.') fb_rect(x + cx * s, y + cy * s, s, s, color);
            }
        }
    } else {
        // Mac Classic Arrow
        const char *mac_cursor[] = {
            "X",
            "XX",
            "X.X",
            "X..X",
            "X...X",
            "X....X",
            "X.....X",
            "X......X",
            "X.......X",
            "X........X",
            "X.........X",
            "X..........X",
            "X......XXXXX",
            "X...X..X",
            "X..X X..X",
            "X.X  X..X",
            "XX    X..X",
            "X     X..X",
            "       XX"
        };
        int cy, cx;
        for (cy = 0; cy < 19; cy++) {
            for (cx = 0; mac_cursor[cy][cx]; cx++) {
                if (mac_cursor[cy][cx] == 'X') {
                    fb_rect(x + cx * s, y + cy * s, s, s, 0x000000);
                } else if (mac_cursor[cy][cx] == '.') {
                    fb_rect(x + cx * s, y + cy * s, s, s, color);
                }
            }
        }
    }
}

int load_ico_from_path(const char* path, u32* out_pixels, int* out_w, int* out_h) {
    static unsigned char ico_data[70000];
    unsigned char *img;
    int bytes, count, width, height, bpp, dib_size, bmp_height, row, col, stride, out_w_local, out_h_local, is_png;
    unsigned int size, offset;
    bytes = fs_read_at(path, (char*)ico_data, sizeof(ico_data));
    if (bytes < 22) return 0;
    if (ico_data[0] || ico_data[1] || ico_data[2] != 1 || ico_data[3] != 0) return 0;
    count = ico_data[4] | (ico_data[5] << 8);
    if (count < 1) return 0;
    width = ico_data[6] ? ico_data[6] : 256;
    height = ico_data[7] ? ico_data[7] : 256;
    bpp = ico_data[12] | (ico_data[13] << 8);
    size = (unsigned int)ico_data[14] | ((unsigned int)ico_data[15] << 8) | ((unsigned int)ico_data[16] << 16) | ((unsigned int)ico_data[17] << 24);
    offset = (unsigned int)ico_data[18] | ((unsigned int)ico_data[19] << 8) | ((unsigned int)ico_data[20] << 16) | ((unsigned int)ico_data[21] << 24);
    if (offset + 8 > (unsigned int)bytes || size < 8) return 0;
    img = ico_data + offset;
    is_png = img[0] == 0x89 && img[1] == 0x50 && img[2] == 0x4E && img[3] == 0x47 && img[4] == 0x0D && img[5] == 0x0A && img[6] == 0x1A && img[7] == 0x0A;
    if (is_png) return 0;
    if (offset + 40 > (unsigned int)bytes || size < 40) return 0;
    dib_size = img[0] | (img[1] << 8) | (img[2] << 16) | (img[3] << 24);
    if (dib_size < 40) return 0;
    bmp_height = img[8] | (img[9] << 8) | (img[10] << 16) | (img[11] << 24);
    bpp = img[14] | (img[15] << 8);
    if (bpp != 32) return 0;
    if (width == 0) width = img[4] | (img[5] << 8) | (img[6] << 16) | (img[7] << 24);
    height = bmp_height / 2;
    if (width < 1 || height < 1) return 0;
    out_w_local = width > 32 ? 32 : width;
    out_h_local = height > 32 ? 32 : height;
    stride = width * 4;
    img += dib_size;
    if (offset + dib_size + (unsigned int)(stride * height) > (unsigned int)bytes) return 0;
    for (row = 0; row < out_h_local; row++) {
        int src_y = height - 1 - ((row * height) / out_h_local);
        for (col = 0; col < out_w_local; col++) {
            int src_x = (col * width) / out_w_local;
            unsigned char *px = img + src_y * stride + src_x * 4;
            u32 color = ((u32)px[2] << 16) | ((u32)px[1] << 8) | (u32)px[0];
            if (px[3] < 128) color = 0xFF00FF; // Alpha threshold
            out_pixels[row * 32 + col] = color;
        }
        for (; col < 32; col++) out_pixels[row * 32 + col] = 0xFF00FF;
    }
    for (; row < 32; row++) {
        for (col = 0; col < 32; col++) out_pixels[row * 32 + col] = 0xFF00FF;
    }
    *out_w = out_w_local;
    *out_h = out_h_local;
    return 1;
}

static int load_start_menu_ico(void) {
    static unsigned char ico_data[70000];
    unsigned char *img;
    int bytes;
    int count;
    int width;
    int height;
    int bpp;
    unsigned int size;
    unsigned int offset;
    int dib_size;
    int bmp_height;
    int row;
    int col;
    int stride;
    int out_w;
    int out_h;
    int is_png;

    if (start_menu_ico_loaded) return start_menu_ico_width > 0 && start_menu_ico_height > 0;
    start_menu_ico_loaded = 1;
    bytes = fs_read_at("/start_menu.ico", (char*)ico_data, sizeof(ico_data));
    if (bytes < 22) bytes = fs_read_at("/icons/start_menu.ico", (char*)ico_data, sizeof(ico_data));
    if (bytes < 22) return 0;
    if (ico_data[0] || ico_data[1] || ico_data[2] != 1 || ico_data[3] != 0) return 0;
    count = ico_data[4] | (ico_data[5] << 8);
    if (count < 1) return 0;
    width = ico_data[6] ? ico_data[6] : 256;
    height = ico_data[7] ? ico_data[7] : 256;
    bpp = ico_data[12] | (ico_data[13] << 8);
    size = (unsigned int)ico_data[14] | ((unsigned int)ico_data[15] << 8) | ((unsigned int)ico_data[16] << 16) | ((unsigned int)ico_data[17] << 24);
    offset = (unsigned int)ico_data[18] | ((unsigned int)ico_data[19] << 8) | ((unsigned int)ico_data[20] << 16) | ((unsigned int)ico_data[21] << 24);
    if (offset + 8 > (unsigned int)bytes || size < 8) return 0;
    img = ico_data + offset;
    is_png = img[0] == 0x89 && img[1] == 0x50 && img[2] == 0x4E && img[3] == 0x47 && img[4] == 0x0D && img[5] == 0x0A && img[6] == 0x1A && img[7] == 0x0A;
    if (is_png) {
        for (row = 0; row < 32; row++) {
            for (col = 0; col < 32; col++) start_menu_ico_pixels[row * 32 + col] = 0xFF00FF;
        }
        start_menu_ico_width = 0;
        start_menu_ico_height = 0;
        return 0;
    }
    if (offset + 40 > (unsigned int)bytes || size < 40) return 0;
    dib_size = img[0] | (img[1] << 8) | (img[2] << 16) | (img[3] << 24);
    if (dib_size < 40) return 0;
    bmp_height = img[8] | (img[9] << 8) | (img[10] << 16) | (img[11] << 24);
    bpp = img[14] | (img[15] << 8);
    if (bpp != 32) return 0;
    if (width == 0) width = img[4] | (img[5] << 8) | (img[6] << 16) | (img[7] << 24);
    height = bmp_height / 2;
    if (width < 1 || height < 1) return 0;
    out_w = width > 32 ? 32 : width;
    out_h = height > 32 ? 32 : height;
    stride = width * 4;
    img += dib_size;
    if (offset + dib_size + (unsigned int)(stride * height) > (unsigned int)bytes) return 0;
    for (row = 0; row < out_h; row++) {
        int src_y = height - 1 - ((row * height) / out_h);
        for (col = 0; col < out_w; col++) {
            int src_x = (col * width) / out_w;
            unsigned char *px = img + src_y * stride + src_x * 4;
            u32 color = ((u32)px[2] << 16) | ((u32)px[1] << 8) | (u32)px[0];
            start_menu_ico_pixels[row * 32 + col] = color;
        }
        for (; col < 32; col++) start_menu_ico_pixels[row * 32 + col] = 0xFF00FF;
    }
    for (; row < 32; row++) {
        for (col = 0; col < 32; col++) start_menu_ico_pixels[row * 32 + col] = 0xFF00FF;
    }
    start_menu_ico_width = out_w;
    start_menu_ico_height = out_h;
    return 1;
}

static int load_brand_ico(void) {
    static unsigned char ico_data[70000];
    unsigned char *img;
    int bytes;
    int count;
    int width;
    int height;
    int bpp;
    unsigned int size;
    unsigned int offset;
    int dib_size;
    int bmp_height;
    int row;
    int col;
    int stride;
    int out_w;
    int out_h;

    if (brand_ico_loaded) return brand_ico_width > 0 && brand_ico_height > 0;
    brand_ico_loaded = 1;
    bytes = fs_read_at("/firstOS.ico", (char*)ico_data, sizeof(ico_data));
    if (bytes < 22) bytes = fs_read_at("/FirstOS.ico", (char*)ico_data, sizeof(ico_data));
    if (bytes < 22) bytes = fs_read_at("/icons/firstOS.ico", (char*)ico_data, sizeof(ico_data));
    if (bytes < 22) bytes = fs_read_at("/icons/os.ico", (char*)ico_data, sizeof(ico_data));
    if (bytes < 22) return 0;
    if (ico_data[0] || ico_data[1] || ico_data[2] != 1 || ico_data[3] != 0) return 0;
    count = ico_data[4] | (ico_data[5] << 8);
    if (count < 1) return 0;
    width = ico_data[6] ? ico_data[6] : 256;
    height = ico_data[7] ? ico_data[7] : 256;
    size = (unsigned int)ico_data[14] | ((unsigned int)ico_data[15] << 8) | ((unsigned int)ico_data[16] << 16) | ((unsigned int)ico_data[17] << 24);
    offset = (unsigned int)ico_data[18] | ((unsigned int)ico_data[19] << 8) | ((unsigned int)ico_data[20] << 16) | ((unsigned int)ico_data[21] << 24);
    if (offset + 40 > (unsigned int)bytes || size < 40) return 0;
    img = ico_data + offset;
    dib_size = img[0] | (img[1] << 8) | (img[2] << 16) | (img[3] << 24);
    if (dib_size < 40) return 0;
    bmp_height = img[8] | (img[9] << 8) | (img[10] << 16) | (img[11] << 24);
    bpp = img[14] | (img[15] << 8);
    if (bpp != 32) return 0;
    if (width == 0) width = img[4] | (img[5] << 8) | (img[6] << 16) | (img[7] << 24);
    height = bmp_height / 2;
    if (width < 1 || height < 1) return 0;
    out_w = width > 32 ? 32 : width;
    out_h = height > 32 ? 32 : height;
    stride = width * 4;
    img += dib_size;
    if (offset + dib_size + (unsigned int)(stride * height) > (unsigned int)bytes) return 0;
    for (row = 0; row < out_h; row++) {
        int src_y = height - 1 - ((row * height) / out_h);
        for (col = 0; col < out_w; col++) {
            int src_x = (col * width) / out_w;
            unsigned char *px = img + src_y * stride + src_x * 4;
            u32 a = px[3];
            u32 color = ((u32)px[2] << 16) | ((u32)px[1] << 8) | (u32)px[0];
            if (a < 16) color = 0xFF00FF;
            brand_ico_pixels[row * 32 + col] = color;
        }
        for (; col < 32; col++) brand_ico_pixels[row * 32 + col] = 0xFF00FF;
    }
    for (; row < 32; row++) {
        for (col = 0; col < 32; col++) brand_ico_pixels[row * 32 + col] = 0xFF00FF;
    }
    brand_ico_width = out_w;
    brand_ico_height = out_h;
    return 1;
}

void draw_icon_scaled(const u32 *pixels, int src_w, int src_h, int x, int y, int dst_w, int dst_h) {
    int row;
    int col;
    if (!pixels || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;
    for (row = 0; row < dst_h; row++) {
        int src_y = (row * src_h) / dst_h;
        for (col = 0; col < dst_w; col++) {
            int src_x = (col * src_w) / dst_w;
            u32 color = pixels[src_y * 32 + src_x];
            if (color != 0xFF00FF) fb_putpixel(x + col, y + row, color);
        }
    }
}

static void draw_brand_icon(int x, int y) {
    if (!brand_ico_loaded) load_brand_ico();
    if (brand_ico_width > 0 && brand_ico_height > 0) {
        draw_icon_scaled(brand_ico_pixels, brand_ico_width, brand_ico_height, x, y, 16, 16);
        return;
    }
    // Fallback: exact 16x16 vector rendering from icons/os.svg
    static const u32 os_svg_fallback[16 * 16] = {
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x899090, 0x57605D, 0x3D4644, 0x3D4644, 0x57605D, 0x8C9090, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x878E8E, 0x4E5754, 0x2D3738, 0x404845, 0x404844, 0x2D3738, 0x4E5754, 0x878E8E, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x545D5A, 0x2F3939, 0x7E806C, 0xEAE0AD, 0xEAE0AD, 0x7E806C, 0x2F3939, 0x555D5A, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x3D4745, 0x3F4845, 0xEBDFAC, 0xFFF3B7, 0xFFF3B7, 0xEBDFAC, 0x3F4845, 0x3D4745, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x343E3D, 0x474F49, 0xECE0AA, 0xFAECB2, 0xFAECB2, 0xECE0AA, 0x474F49, 0x343E3D, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x343E3D, 0x474F49, 0xECE0AA, 0xFAECB2, 0xFAECB2, 0xECE0AA, 0x474F49, 0x343E3D, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x3D4745, 0x3F4845, 0xEBDFAC, 0xFFF3B7, 0xFFF3B7, 0xEBDFAC, 0x3F4845, 0x3D4745, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x555C5A, 0x2E3939, 0x7E7F6D, 0xE9DFAC, 0xE9DFAC, 0x7E7F6D, 0x2E3939, 0x555C5A, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x88908C, 0x4F5956, 0x2F3939, 0x414946, 0x414946, 0x2F3939, 0x4F5956, 0x88908C, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x838A8A, 0x555D5A, 0x3D4644, 0x3D4644, 0x555D5A, 0x868A8A, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x676F6F, 0x676F6F, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0x9BA29E, 0x6D7474, 0x4C5552, 0x444C4B, 0x37413E, 0x37413E, 0x444C4B, 0x4C5552, 0x6D7474, 0x9BA29E, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xB1B8B8, 0x636A67, 0x434C4A, 0x3D4745, 0x3F4947, 0x48514F, 0x414A48, 0x414A48, 0x48514F, 0x3F4947, 0x3D4745, 0x434C4A, 0x636967, 0xB1B8B8, 0xFF00FF,
        0x8F9393, 0x525B59, 0x38423F, 0x424B49, 0x565D5B, 0x6B7270, 0x9AA29E, 0x9EA5A5, 0x9EA5A5, 0x9AA29E, 0x6B7272, 0x565D5B, 0x424B49, 0x38423F, 0x525B59, 0x8F9393,
        0x4D5653, 0x323C3A, 0x525B59, 0x868D8D, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x868D8D, 0x525B59, 0x323C3A, 0x4D5653,
        0x4F5756, 0x505857, 0xB4BDBD, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xB4BDBD, 0x505957, 0x4F5856
    };
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            u32 col = os_svg_fallback[r * 16 + c];
            if (col != 0xFF00FF) fb_putpixel(x + c, y + r, col);
        }
    }
}

static void draw_start_menu_icon(int x, int y) {
    if (!start_menu_ico_loaded) load_start_menu_ico();
    if (start_menu_ico_width > 0 && start_menu_ico_height > 0) {
        draw_icon_scaled(start_menu_ico_pixels, start_menu_ico_width, start_menu_ico_height, x, y, 18, 18);
        return;
    }
    fb_rect(x, y, 18, 18, 0x1F5FBF);
    fb_rect(x + 1, y + 1, 16, 16, 0x2C74DC);
    fb_rect(x + 3, y + 3, 5, 5, 0xFFFFFF);
    fb_rect(x + 10, y + 3, 5, 5, 0xDCEBFF);
    fb_rect(x + 3, y + 10, 5, 5, 0xDCEBFF);
    fb_rect(x + 10, y + 10, 5, 5, 0xFFFFFF);
}

static void draw_start_button(void) {
    if (start_button.w <= 0) return;
    fb_round_rect_alpha(start_button.x, start_button.y, start_button.w, start_button.h, 6, 0x182838, 210);
    fb_round_rect_border(start_button.x, start_button.y, start_button.w, start_button.h, 6, start_menu_open ? 0x60A5FA : 0x4B6E94);
    draw_start_menu_icon(start_button.x + 4, start_button.y + 10);
    font_text(start_button.x + 26, start_button.y + 14, "Start", 0xFFFFFF, 0);
}

static void draw_start_menu(void) {
    if (!start_menu_open) return;
    fb_rect(start_menu_rect.x + 12, start_menu_rect.y + 12, start_menu_rect.w, start_menu_rect.h, 0x102030);
    fb_rect(start_menu_rect.x, start_menu_rect.y, start_menu_rect.w, start_menu_rect.h, active_theme.start_menu_bg);
    outline(start_menu_rect.x, start_menu_rect.y, start_menu_rect.w, start_menu_rect.h, 0x8EA1B8);
    fb_rect(start_menu_rect.x, start_menu_rect.y, 74, start_menu_rect.h, 0x2F5EAA);
    fb_rect(start_menu_rect.x + 12, start_menu_rect.y + 14, start_menu_rect.w - 24, 46, 0xF4F7FB);
    outline(start_menu_rect.x + 12, start_menu_rect.y + 14, start_menu_rect.w - 24, 46, 0xC3CFDD);
    draw_start_menu_icon(start_menu_rect.x + 24, start_menu_rect.y + 27);
    draw_label(start_menu_rect.x + 58, start_menu_rect.y + 26, "Start", 0x182230);
    draw_label(start_menu_rect.x + 58, start_menu_rect.y + 40, "FirstOS quick access", 0x5C6C7E);

    draw_label(start_menu_rect.x + 92, start_menu_rect.y + 76, "Applications", 0x45576B);
    fb_rect(start_menu_rect.x + 92, start_menu_rect.y + 90, start_menu_rect.w - 110, 1, 0xC2CDDA);
    draw_button(start_apps_item, 0xDCE6F2, 0xEAF1F8, "", 0x20344E);
    if (!terminal_ico_loaded) { terminal_ico_loaded = 1; load_ico_from_path("/icons/terminal.ico", terminal_ico_pixels, &terminal_ico_width, &terminal_ico_height); }
    if (terminal_ico_width > 0) draw_icon_scaled(terminal_ico_pixels, terminal_ico_width, terminal_ico_height, start_apps_item.x + 8, start_apps_item.y + 4, 16, 16);
    else font_icon(start_apps_item.x + 10, start_apps_item.y + 6, ICON_FILE, 0x20344E);
    font_text_fit(start_apps_item.x + 30, start_apps_item.y + 8, "TERMINAL", 0x20344E, start_apps_item.w - 38);

    draw_button(start_browser_item, 0xEDE2D8, 0xF8EFE7, "", 0x8C3810);
    if (!browser_ico_loaded) { browser_ico_loaded = 1; load_ico_from_path("/icons/browser.ico", browser_ico_pixels, &browser_ico_width, &browser_ico_height); }
    if (browser_ico_width > 0) draw_icon_scaled(browser_ico_pixels, browser_ico_width, browser_ico_height, start_browser_item.x + 8, start_browser_item.y + 4, 16, 16);
    else font_icon(start_browser_item.x + 10, start_browser_item.y + 6, ICON_NETWORK, 0xD44210);
    font_text_fit(start_browser_item.x + 30, start_browser_item.y + 8, "WEB BROWSER", 0x8C3810, start_browser_item.w - 38);
    
    draw_button(start_textedit_item, 0xE6DDD3, 0xF0E8DF, "", 0x3D291A);
    if (!textedit_ico_loaded) { textedit_ico_loaded = 1; load_ico_from_path("/icons/text_edit.ico", textedit_ico_pixels, &textedit_ico_width, &textedit_ico_height); }
    if (textedit_ico_width > 0) draw_icon_scaled(textedit_ico_pixels, textedit_ico_width, textedit_ico_height, start_textedit_item.x + 8, start_textedit_item.y + 4, 16, 16);
    else font_icon(start_textedit_item.x + 10, start_textedit_item.y + 6, ICON_FILE, 0x3D291A);
    font_text_fit(start_textedit_item.x + 30, start_textedit_item.y + 8, "TEXT EDIT", 0x3D291A, start_textedit_item.w - 38);
    
    draw_button(start_explorer_item, 0xD8E3EE, 0xE7EEF5, "", 0x20344E);
    if (!explorer_ico_loaded) { explorer_ico_loaded = 1; load_ico_from_path("/icons/explorer.ico", explorer_ico_pixels, &explorer_ico_width, &explorer_ico_height); }
    if (explorer_ico_width > 0) draw_icon_scaled(explorer_ico_pixels, explorer_ico_width, explorer_ico_height, start_explorer_item.x + 8, start_explorer_item.y + 4, 16, 16);
    else font_icon(start_explorer_item.x + 10, start_explorer_item.y + 6, ICON_FOLDER, 0x20344E);
    font_text_fit(start_explorer_item.x + 30, start_explorer_item.y + 8, "EXPLORER", 0x20344E, start_explorer_item.w - 38);
    
    draw_button(start_media_item, 0xD8E3EE, 0xE7EEF5, "", 0x20344E);
    if (!audiowav_ico_loaded) { audiowav_ico_loaded = 1; load_ico_from_path("/icons/audio_wav.ico", audiowav_ico_pixels, &audiowav_ico_width, &audiowav_ico_height); }
    if (audiowav_ico_width > 0) draw_icon_scaled(audiowav_ico_pixels, audiowav_ico_width, audiowav_ico_height, start_media_item.x + 8, start_media_item.y + 4, 16, 16);
    else font_icon(start_media_item.x + 10, start_media_item.y + 6, ICON_FILE, 0x20344E);
    font_text_fit(start_media_item.x + 30, start_media_item.y + 8, "MEDIA PLAYER", 0x20344E, start_media_item.w - 38);

    draw_button(start_calc_item, 0xD8E3EE, 0xE7EEF5, "", 0x20344E);
    font_icon(start_calc_item.x + 10, start_calc_item.y + 6, ICON_DISPLAY, 0x20344E);
    font_text_fit(start_calc_item.x + 30, start_calc_item.y + 8, "CALCULATOR", 0x20344E, start_calc_item.w - 38);

        draw_button(start_ide_item, 0xD8F0E3, 0xE7F5EC, "", 0x1F4730);
    font_icon(start_ide_item.x + 10, start_ide_item.y + 6, ICON_FILE, 0x1F4730);
    font_text_fit(start_ide_item.x + 30, start_ide_item.y + 8, "APP STUDIO", 0x1F4730, start_ide_item.w - 38);

    draw_button(start_taskman_item, 0xDCE6F2, 0xEAF1F8, "", 0x20344E);
    font_icon(start_taskman_item.x + 10, start_taskman_item.y + 6, ICON_DISPLAY, 0x20344E);
    font_text_fit(start_taskman_item.x + 30, start_taskman_item.y + 8, "TASK MANAGER", 0x20344E, start_taskman_item.w - 38);

    draw_button(start_demo_item, 0xDCE6F2, 0xEAF1F8, "", 0x20344E);
    font_icon(start_demo_item.x + 10, start_demo_item.y + 6, ICON_FILE, 0x20344E);
    font_text_fit(start_demo_item.x + 30, start_demo_item.y + 8, "FIRSTLANG DEMO", 0x20344E, start_demo_item.w - 38);

    draw_label(start_menu_rect.x + 92, start_menu_rect.y + 344, "System", 0x45576B);
    fb_rect(start_menu_rect.x + 92, start_menu_rect.y + 358, start_menu_rect.w - 110, 1, 0xC2CDDA);
    draw_button(start_settings_item, 0xD7E1EC, 0xE4EBF3, "", 0x20344E);
    font_icon(start_settings_item.x + 10, start_settings_item.y + 6, ICON_SETTINGS, 0x20344E);
    font_text_fit(start_settings_item.x + 30, start_settings_item.y + 8, "SETTINGS", 0x20344E, start_settings_item.w - 38);
    draw_button(start_restart_item, 0xD3DEEA, 0xE2E9F1, "", 0x20344E);
    font_icon(start_restart_item.x + 10, start_restart_item.y + 6, ICON_DISPLAY, 0x20344E);
    font_text_fit(start_restart_item.x + 30, start_restart_item.y + 8, "RESTART", 0x20344E, start_restart_item.w - 38);
    draw_button(start_shutdown_item, 0xD4DCE8, 0xE3E9F1, "", 0x20344E);
    font_icon(start_shutdown_item.x + 10, start_shutdown_item.y + 6, ICON_DISK, 0x20344E);
    font_text_fit(start_shutdown_item.x + 30, start_shutdown_item.y + 8, "SHUTDOWN", 0x20344E, start_shutdown_item.w - 38);
}

static void draw_start_submenus(void) {
    rect_t sub;
    if (!start_menu_open) return;
    if (menu_hover(start_apps_item)) {
        sub = (rect_t){start_menu_rect.x + start_menu_rect.w + 8, start_menu_rect.y + 68, 150, 102, 0xD7DEE8};
        fb_rect(sub.x + 6, sub.y + 6, sub.w, sub.h, shadow); fill(sub); outline(sub.x, sub.y, sub.w, sub.h, 0x5D738E);
        draw_label(sub.x + 14, sub.y + 12, "LAUNCH", 0x3D4F63);
        draw_button((rect_t){sub.x + 8, sub.y + 28, sub.w - 16, 18, 0}, menu_hover((rect_t){sub.x + 8, sub.y + 28, sub.w - 16, 18, 0}) ? 0xD5E1EC : 0xC6D3E0, 0xECF3F8, "TERMINAL", 0x20344E);
        draw_button((rect_t){sub.x + 8, sub.y + 48, sub.w - 16, 18, 0}, menu_hover((rect_t){sub.x + 8, sub.y + 48, sub.w - 16, 18, 0}) ? 0xD8C5B5 : 0xC7B2A2, 0xEBDCCF, "TEXT EDIT", 0x3D291A);
        draw_button((rect_t){sub.x + 8, sub.y + 68, sub.w - 16, 18, 0}, menu_hover((rect_t){sub.x + 8, sub.y + 68, sub.w - 16, 18, 0}) ? 0xD5E1EC : 0xC6D3E0, 0xECF3F8, "EXPLORER", 0x20344E);
    }
    if (menu_hover(start_settings_item)) {
        sub = (rect_t){start_menu_rect.x + start_menu_rect.w + 8, start_menu_rect.y + 192, 158, 82, 0xD7DEE8};
        fb_rect(sub.x + 6, sub.y + 6, sub.w, sub.h, shadow); fill(sub); outline(sub.x, sub.y, sub.w, sub.h, 0x5D738E);
        draw_label(sub.x + 14, sub.y + 12, "SYSTEM", 0x3D4F63);
        draw_button((rect_t){sub.x + 8, sub.y + 28, sub.w - 16, 18, 0}, menu_hover((rect_t){sub.x + 8, sub.y + 28, sub.w - 16, 18, 0}) ? 0xD5E1EC : 0xC6D3E0, 0xECF3F8, "SETTINGS", 0x20344E);
        draw_button((rect_t){sub.x + 8, sub.y + 48, sub.w - 16, 18, 0}, menu_hover((rect_t){sub.x + 8, sub.y + 48, sub.w - 16, 18, 0}) ? 0xD5E1EC : 0xC6D3E0, 0xECF3F8, "RESTART / POWER", 0x20344E);
    }
}

static void update_top_menu_state(void) {
    app_window_t *app = active_app_window();
    top_menu_app_label = app ? app_name_from_kind(app->kind) : "Desktop";

    top_menu_app_items = 3;
    top_menu_file_items = 3;
    top_menu_edit_items = 3;

    if (!app) {
        top_menu_app_item_1 = "About Desktop";
        top_menu_app_item_2 = "Hide Desktop";
        top_menu_app_item_3 = "Show Desktop";
        top_menu_file_item_1 = "New";
        top_menu_file_item_2 = "Open";
        top_menu_file_item_3 = "Close";
        top_menu_edit_item_1 = "Copy";
        top_menu_edit_item_2 = "Paste";
        top_menu_edit_item_3 = "Delete";
        top_menu_edit_items_list[0] = "Copy";
        top_menu_edit_items_list[1] = "Paste";
        top_menu_edit_items_list[2] = "Delete";
        return;
    }

    if (app->kind == APP_TEXT_EDIT) {
        top_menu_app_item_1 = "About Text Edit";
        top_menu_app_item_2 = "Hide Text Edit";
        top_menu_app_item_3 = app->maximized ? "Exit Fullscreen" : "Enter Fullscreen";
        top_menu_file_items = 5;
        top_menu_file_item_1 = "New Tab";
        top_menu_file_item_2 = "New Window";
        top_menu_file_item_3 = "Open...";
        top_menu_file_item_4 = "Save As...";
        top_menu_file_item_5 = "Close";
        top_menu_edit_items = 14;
        top_menu_edit_items_list[0]  = "Undo";
        top_menu_edit_items_list[1]  = "Copy";
        top_menu_edit_items_list[2]  = "Paste";
        top_menu_edit_items_list[3]  = "Delete";
        top_menu_edit_items_list[4]  = "LTR (Left-to-Right)";
        top_menu_edit_items_list[5]  = "RTL (Right-to-Left)";
        top_menu_edit_items_list[6]  = "Bold";
        top_menu_edit_items_list[7]  = "Italic";
        top_menu_edit_items_list[8]  = "Underline";
        top_menu_edit_items_list[9]  = "Import Image";
        top_menu_edit_items_list[10] = "Import Date";
        top_menu_edit_items_list[11] = "Import Time";
        top_menu_edit_items_list[12] = "Insert Shape";
        top_menu_edit_items_list[13] = "Import Chart";
    } else if (app->kind == APP_IDE) {
        top_menu_app_item_1 = "About Firstlang IDE";
        top_menu_app_item_2 = "Hide Firstlang IDE";
        top_menu_app_item_3 = app->maximized ? "Exit Fullscreen" : "Enter Fullscreen";
        top_menu_file_items = 6;
        top_menu_file_item_1 = "New Tab";
        top_menu_file_item_2 = "New Window";
        top_menu_file_item_3 = "Open";
        top_menu_file_item_4 = "Save";
        top_menu_file_item_5 = "Build";
        top_menu_file_item_6 = "Close";
        top_menu_edit_items = 4;
        top_menu_edit_item_1 = "Copy";
        top_menu_edit_item_2 = "Paste";
        top_menu_edit_item_3 = "Undo";
        top_menu_edit_item_4 = "Design";
        top_menu_edit_items_list[0] = "Copy";
        top_menu_edit_items_list[1] = "Paste";
        top_menu_edit_items_list[2] = "Undo";
        top_menu_edit_items_list[3] = "Design";
    } else if (app->kind == APP_EXPLORER) {
        top_menu_app_item_1 = "About Explorer";
        top_menu_app_item_2 = "Hide Explorer";
        top_menu_app_item_3 = app->maximized ? "Exit Fullscreen" : "Enter Fullscreen";
        top_menu_file_items = 5;
        top_menu_file_item_1 = "New Tab";
        top_menu_file_item_2 = "New Window";
        top_menu_file_item_3 = "New Folder";
        top_menu_file_item_4 = "Open";
        top_menu_file_item_5 = "Close";
        top_menu_edit_items = 3;
        top_menu_edit_item_1 = "Copy";
        top_menu_edit_item_2 = "Paste";
        top_menu_edit_item_3 = "Delete";
        top_menu_edit_items_list[0] = "Copy";
        top_menu_edit_items_list[1] = "Paste";
        top_menu_edit_items_list[2] = "Delete";
    } else if (app->kind == APP_SETTINGS) {
        top_menu_app_item_1 = "About Settings";
        top_menu_app_item_2 = "Hide Settings";
        top_menu_app_item_3 = "Close Settings";
        top_menu_file_items = 3;
        top_menu_file_item_1 = "Open";
        top_menu_file_item_2 = "New Window";
        top_menu_file_item_3 = "Close";
        top_menu_edit_item_1 = "Copy";
        top_menu_edit_item_2 = "Paste";
        top_menu_edit_item_3 = "Delete";
        top_menu_edit_items_list[0] = "Copy";
        top_menu_edit_items_list[1] = "Paste";
        top_menu_edit_items_list[2] = "Delete";
    } else if (app->kind == APP_MEDIA_PLAYER) {
        top_menu_app_item_1 = "About Media Player";
        top_menu_app_item_2 = "Hide Media Player";
        top_menu_app_item_3 = app->maximized ? "Exit Fullscreen" : "Enter Fullscreen";
        top_menu_file_items = 3;
        top_menu_file_item_1 = "Open...";
        top_menu_file_item_2 = "New Window";
        top_menu_file_item_3 = "Close";
        top_menu_edit_items = 4;
        top_menu_edit_item_1 = "Play";
        top_menu_edit_item_2 = "Stop";
        top_menu_edit_item_3 = "Pause";
        top_menu_edit_item_4 = "Rewind";
        top_menu_edit_items_list[0] = "Play";
        top_menu_edit_items_list[1] = "Stop";
        top_menu_edit_items_list[2] = "Pause";
        top_menu_edit_items_list[3] = "Rewind";
    } else if (app->kind == APP_TERMINAL) {
        top_menu_app_item_1 = "About Terminal";
        top_menu_app_item_2 = "Hide Terminal";
        top_menu_app_item_3 = app->maximized ? "Exit Fullscreen" : "Enter Fullscreen";
        top_menu_file_items = 3;
        top_menu_file_item_1 = "New Tab";
        top_menu_file_item_2 = "New Window";
        top_menu_file_item_3 = "Close";
        top_menu_edit_items = 3;
        top_menu_edit_item_1 = "Copy";
        top_menu_edit_item_2 = "Paste";
        top_menu_edit_item_3 = "Clear";
        top_menu_edit_items_list[0] = "Copy";
        top_menu_edit_items_list[1] = "Paste";
        top_menu_edit_items_list[2] = "Clear";
    } else {
        top_menu_app_item_1 = "About App";
        top_menu_app_item_2 = "Hide App";
        top_menu_app_item_3 = "Close App";
        top_menu_file_items = 3;
        top_menu_file_item_1 = "Open";
        top_menu_file_item_2 = "New Window";
        top_menu_file_item_3 = "Close";
        top_menu_edit_item_1 = "Copy";
        top_menu_edit_item_2 = "Paste";
        top_menu_edit_item_3 = "Delete";
        top_menu_edit_items_list[0] = "Copy";
        top_menu_edit_items_list[1] = "Paste";
        top_menu_edit_items_list[2] = "Delete";
    }
}

static void draw_mac_menu_panel(rect_t panel) {
    fb_draw_card_shadow(panel.x, panel.y, panel.w, panel.h, 8);
    if (topbar_alpha < 255) {
        fb_round_rect_alpha(panel.x, panel.y, panel.w, panel.h, 8, 0xF8FAFC, topbar_alpha);
    } else {
        fb_round_rect(panel.x, panel.y, panel.w, panel.h, 8, 0xF8FAFC);
    }
    fb_round_rect_border(panel.x, panel.y, panel.w, panel.h, 8, 0xC3CFDD);
}

static void draw_mac_menu_item(rect_t row, const char *label, int danger) {
    int hov = menu_hover(row);
    u32 bg = hov ? (danger ? 0xEF4444 : 0x2563EB) : 0xF8FAFC;
    u32 fg = hov ? 0xFFFFFF : (danger ? 0xDC2626 : 0x1E293B);
    if (hov) {
        fb_round_rect(row.x + 2, row.y + 1, row.w - 4, row.h - 2, 5, bg);
    }
    draw_label(row.x + 10, row.y + 7, label, fg);
}

static void draw_brand_menu(void) {
    if (!brand_menu_open) return;
    draw_mac_menu_panel(top_brand_menu);
    draw_mac_menu_item(top_updates_item, "Settings", 0);
    draw_mac_menu_item(top_system_info_item, "System Info", 0);
    draw_mac_menu_item(top_logout_item, "Log Out", 1);
    draw_mac_menu_item(top_sleep_item, "Sleep", 0);
    draw_mac_menu_item(top_reboot_item, "Restart", 1);
    draw_mac_menu_item(top_shutdown_item, "Shut Down", 1);
}

static void draw_app_menu(void) {
    if (!app_menu_open) return;
    top_app_menu_rect.h = top_menu_app_items * 24 + 12;
    draw_mac_menu_panel(top_app_menu_rect);
    draw_mac_menu_item((rect_t){top_app_menu_rect.x + 6, top_app_menu_rect.y + 6, top_app_menu_rect.w - 12, 22, 0}, top_menu_app_item_1, 0);
    draw_mac_menu_item((rect_t){top_app_menu_rect.x + 6, top_app_menu_rect.y + 30, top_app_menu_rect.w - 12, 22, 0}, top_menu_app_item_2, 0);
    draw_mac_menu_item((rect_t){top_app_menu_rect.x + 6, top_app_menu_rect.y + 54, top_app_menu_rect.w - 12, 22, 0}, top_menu_app_item_3, 0);
}

static void draw_file_menu(void) {
    if (!file_menu_open) return;
    top_file_menu_rect.h = top_menu_file_items * 24 + 12;
    draw_mac_menu_panel(top_file_menu_rect);
    if (top_menu_file_items >= 1) draw_mac_menu_item((rect_t){top_file_menu_rect.x + 6, top_file_menu_rect.y + 6, top_file_menu_rect.w - 12, 22, 0}, top_menu_file_item_1, 0);
    if (top_menu_file_items >= 2) draw_mac_menu_item((rect_t){top_file_menu_rect.x + 6, top_file_menu_rect.y + 30, top_file_menu_rect.w - 12, 22, 0}, top_menu_file_item_2, 0);
    if (top_menu_file_items >= 3) draw_mac_menu_item((rect_t){top_file_menu_rect.x + 6, top_file_menu_rect.y + 54, top_file_menu_rect.w - 12, 22, 0}, top_menu_file_item_3, (top_menu_file_items == 3) ? 1 : 0);
    if (top_menu_file_items >= 4) draw_mac_menu_item((rect_t){top_file_menu_rect.x + 6, top_file_menu_rect.y + 78, top_file_menu_rect.w - 12, 22, 0}, top_menu_file_item_4, (top_menu_file_items == 4) ? 1 : 0);
    if (top_menu_file_items >= 5) draw_mac_menu_item((rect_t){top_file_menu_rect.x + 6, top_file_menu_rect.y + 102, top_file_menu_rect.w - 12, 22, 0}, top_menu_file_item_5, (top_menu_file_items == 5) ? 1 : 0);
    if (top_menu_file_items >= 6) draw_mac_menu_item((rect_t){top_file_menu_rect.x + 6, top_file_menu_rect.y + 126, top_file_menu_rect.w - 12, 22, 0}, top_menu_file_item_6, 1);
}

static void draw_edit_menu(void) {
    if (!edit_menu_open) return;
    top_edit_menu_rect.h = top_menu_edit_items * 24 + 12;
    draw_mac_menu_panel(top_edit_menu_rect);
    for (int i = 0; i < top_menu_edit_items && i < 16; i++) {
        if (top_menu_edit_items_list[i] && top_menu_edit_items_list[i][0]) {
            rect_t row = (rect_t){top_edit_menu_rect.x + 6, top_edit_menu_rect.y + 6 + i * 24, top_edit_menu_rect.w - 12, 22, 0};
            draw_mac_menu_item(row, top_menu_edit_items_list[i], 0);
        }
    }
}

static void draw_view_menu(void) {
    if (!view_menu_open) return;
    top_view_menu_rect.h = 5 * 24 + 12;
    draw_mac_menu_panel(top_view_menu_rect);
    static const char *view_items[5] = {
        "A4 Page (210 x 297 mm)",
        "A5 Page (148 x 210 mm)",
        "A6 Page (105 x 148 mm)",
        "US Letter (8.5 x 11 in)",
        "Full Window"
    };
    extern int text_edit_get_page_format(void);
    int cur_fmt = text_edit_get_page_format();
    for (int i = 0; i < 5; i++) {
        rect_t row = (rect_t){top_view_menu_rect.x + 6, top_view_menu_rect.y + 6 + i * 24, top_view_menu_rect.w - 12, 22, 0};
        draw_mac_menu_item(row, view_items[i], 0);
        int is_act = (i == 0 && cur_fmt == 1) || (i == 1 && cur_fmt == 2) || (i == 2 && cur_fmt == 3) || (i == 3 && cur_fmt == 4) || (i == 4 && cur_fmt == 0);
        if (is_act) {
            fb_rect(row.x + row.w - 16, row.y + 7, 8, 8, 0x1D4ED8);
        }
    }
}


static void draw_wifi_password_modal(void) {
    if (!wifi_pwd_modal_open) return;
    int sw = (int)fb.width;
    int sh = (int)fb.height;
    int pw = 360;
    int ph = 190;
    int px = (sw - pw) / 2;
    int py = (sh - ph) / 2;

    // Dark backdrop overlay
    fb_rect_alpha(0, 0, sw, sh, 0x0A1520, 140);

    // Dialog card
    fb_rect(px - 3, py - 3, pw + 6, ph + 6, 0x102030);
    fb_rect(px, py, pw, ph, 0xFFFFFF);
    outline(px, py, pw, ph, 0x2A4565);

    // Title bar
    fb_rect(px, py, pw, 28, 0x1E3A5F);
    font_icon(px + 10, py + 7, ICON_NETWORK, 0xFFFFFF);
    font_text(px + 28, py + 7, "Connect to Wi-Fi", 0xFFFFFF, 1);

    // Close button [X]
    int close_hover = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), (rect_t){px + pw - 24, py + 4, 20, 20, 0});
    fb_rect(px + pw - 24, py + 4, 20, 20, close_hover ? 0xE81123 : 0x1E3A5F);
    font_text(px + pw - 18, py + 6, "X", 0xFFFFFF, 1);

    const wifi_ap_t *ap = net_get_ap(wifi_pwd_ap_index);
    const char *ssid_name = ap ? ap->ssid : "Selected Network";

    // Label
    char prompt_buf[64];
    gui_fl_cpy(prompt_buf, "Enter password for \"", sizeof(prompt_buf));
    gui_fl_cat(prompt_buf, ssid_name, sizeof(prompt_buf));
    gui_fl_cat(prompt_buf, "\":", sizeof(prompt_buf));
    font_text(px + 20, py + 40, prompt_buf, 0x1E293B, 1);

    // Password input field
    rect_t input_box = {px + 20, py + 64, pw - 40, 28, 0xFFFFFF};
    fb_rect(input_box.x, input_box.y, input_box.w, input_box.h, input_box.color);
    outline(input_box.x, input_box.y, input_box.w, input_box.h, 0x0078D7);

    // Masked / unmasked display
    char disp_pass[68];
    int plen = 0;
    while (wifi_pwd_input[plen] && plen < 63) {
        disp_pass[plen] = wifi_pwd_show_pass ? wifi_pwd_input[plen] : '*';
        plen++;
    }
    disp_pass[plen] = 0;

    int max_vis = (input_box.w - 24) / 7;
    int start_ch = 0;
    if (plen > max_vis) {
        start_ch = plen - max_vis;
    }

    if (plen > 0) {
        font_text(input_box.x + 10, input_box.y + 8, disp_pass + start_ch, 0x1E293B, 1);
    }
    int cur_x = input_box.x + 10 + (plen - start_ch) * 7;
    if (cur_x > input_box.x + input_box.w - 6) cur_x = input_box.x + input_box.w - 6;
    fb_rect(cur_x, input_box.y + 6, 2, 16, 0x0078D7);

    // Show password checkbox
    rect_t chk_box = {px + 20, py + 102, 14, 14, 0xFFFFFF};
    fb_rect(chk_box.x, chk_box.y, chk_box.w, chk_box.h, chk_box.color);
    outline(chk_box.x, chk_box.y, chk_box.w, chk_box.h, 0x8FA4BA);
    if (wifi_pwd_show_pass) {
        fb_rect(chk_box.x + 3, chk_box.y + 3, 8, 8, 0x0078D7);
    }
    font_text(px + 40, py + 103, "Show password", 0x475569, 1);

    // Error text
    if (wifi_pwd_error == 1) {
        font_text(px + 20, py + 126, "Incorrect password. Please try again.", 0xD32F2F, 1);
    } else if (wifi_pwd_error == 2) {
        font_text(px + 20, py + 126, "Password must be at least 8 characters.", 0xD32F2F, 1);
    }

    // Buttons
    rect_t btn_cancel = {px + pw - 180, py + ph - 38, 70, 26, 0};
    rect_t btn_connect = {px + pw - 98, py + ph - 38, 78, 26, 0};

    int cancel_hover = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), btn_cancel);
    int conn_hover = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), btn_connect);

    fb_rect(btn_cancel.x, btn_cancel.y, btn_cancel.w, btn_cancel.h, cancel_hover ? 0xE2E8F0 : 0xEEF2F6);
    outline(btn_cancel.x, btn_cancel.y, btn_cancel.w, btn_cancel.h, 0xCBD5E1);
    font_text_fit_center(btn_cancel.x, btn_cancel.y + 7, "Cancel", 0x334155, btn_cancel.w);

    fb_rect(btn_connect.x, btn_connect.y, btn_connect.w, btn_connect.h, conn_hover ? 0x0063B1 : 0x0078D7);
    outline(btn_connect.x, btn_connect.y, btn_connect.w, btn_connect.h, 0x005A9E);
    font_text_fit_center(btn_connect.x, btn_connect.y + 7, "Connect", 0xFFFFFF, btn_connect.w);
}

static void draw_text_edit_unsaved_dialog(void) {
    if (!text_edit_unsaved_dialog_open) return;
    int sw = (int)fb.width;
    int sh = (int)fb.height;
    int pw = 380;
    int ph = 150;
    int px = (sw - pw) / 2;
    int py = (sh - ph) / 2;

    // Dark backdrop overlay
    fb_rect_alpha(0, 0, sw, sh, 0x0A1520, 140);

    // Dialog card
    fb_rect(px - 3, py - 3, pw + 6, ph + 6, 0x102030);
    fb_rect(px, py, pw, ph, 0xFFFFFF);
    outline(px, py, pw, ph, 0x2A4565);

    // Title bar
    fb_rect(px, py, pw, 28, 0x1E3A5F);
    font_icon(px + 10, py + 7, ICON_FILE, 0xFFFFFF);
    font_text(px + 28, py + 7, "Text Edit - Unsaved Changes", 0xFFFFFF, 1);

    // Prompt text
    font_text(px + 20, py + 42, "Do you want to save changes before closing?", 0x1E293B, 1);
    font_text(px + 20, py + 64, "If you close without saving, your changes will be lost.", 0x64748B, 1);

    // Buttons
    int mx = ps2_mouse_x();
    int my = ps2_mouse_y();

    text_edit_unsaved_btn_dont_save = (rect_t){px + 20, py + 102, 100, 28, 0};
    text_edit_unsaved_btn_cancel = (rect_t){px + 170, py + 102, 80, 28, 0};
    text_edit_unsaved_btn_save = (rect_t){px + 260, py + 102, 100, 28, 0};

    int hov_dont = point_in_rect(mx, my, text_edit_unsaved_btn_dont_save);
    int hov_cancel = point_in_rect(mx, my, text_edit_unsaved_btn_cancel);
    int hov_save = point_in_rect(mx, my, text_edit_unsaved_btn_save);

    // Don't Save button
    fb_rect(text_edit_unsaved_btn_dont_save.x, text_edit_unsaved_btn_dont_save.y, text_edit_unsaved_btn_dont_save.w, text_edit_unsaved_btn_dont_save.h, hov_dont ? 0xDC2626 : 0xEF4444);
    outline(text_edit_unsaved_btn_dont_save.x, text_edit_unsaved_btn_dont_save.y, text_edit_unsaved_btn_dont_save.w, text_edit_unsaved_btn_dont_save.h, 0xB91C1C);
    font_text_fit_center(text_edit_unsaved_btn_dont_save.x, text_edit_unsaved_btn_dont_save.y + 7, "Don't Save", 0xFFFFFF, text_edit_unsaved_btn_dont_save.w);

    // Cancel button
    fb_rect(text_edit_unsaved_btn_cancel.x, text_edit_unsaved_btn_cancel.y, text_edit_unsaved_btn_cancel.w, text_edit_unsaved_btn_cancel.h, hov_cancel ? 0xE2E8F0 : 0xF1F5F9);
    outline(text_edit_unsaved_btn_cancel.x, text_edit_unsaved_btn_cancel.y, text_edit_unsaved_btn_cancel.w, text_edit_unsaved_btn_cancel.h, 0xCBD5E1);
    font_text_fit_center(text_edit_unsaved_btn_cancel.x, text_edit_unsaved_btn_cancel.y + 7, "Cancel", 0x1E293B, text_edit_unsaved_btn_cancel.w);

    // Save button
    fb_rect(text_edit_unsaved_btn_save.x, text_edit_unsaved_btn_save.y, text_edit_unsaved_btn_save.w, text_edit_unsaved_btn_save.h, hov_save ? 0x2563EB : 0x3B82F6);
    outline(text_edit_unsaved_btn_save.x, text_edit_unsaved_btn_save.y, text_edit_unsaved_btn_save.w, text_edit_unsaved_btn_save.h, 0x1D4ED8);
    font_text_fit_center(text_edit_unsaved_btn_save.x, text_edit_unsaved_btn_save.y + 7, "Save", 0xFFFFFF, text_edit_unsaved_btn_save.w);
}

static void draw_wifi_menu(void) {
    if (!wifi_menu_open) return;
    int sw = (int)fb.width;
    int menu_w = 260;
    int menu_x = sw - menu_w - 10;
    int menu_y = desktop_bar.h;

    int is_on = net_is_enabled();
    int is_conn = net_is_connected();
    const net_state_t *st = net_get_state();
    int ap_count = net_get_ap_count();

    int total_h = 0;
    if (!is_on) {
        total_h = 64;
    } else if (is_conn) {
        total_h = 82 + 16 + ap_count * 24 + 34;
    } else {
        total_h = 36 + 16 + ap_count * 24 + 34;
    }

    top_wifi_menu = (rect_t){menu_x, menu_y, menu_w, total_h, window_bg};

    // Soft shadow & solid background
    fb_rect(top_wifi_menu.x - 2, top_wifi_menu.y - 1, top_wifi_menu.w + 4, top_wifi_menu.h + 3, 0x1A2A3A);
    fb_rect(top_wifi_menu.x, top_wifi_menu.y, top_wifi_menu.w, top_wifi_menu.h, 0xF4F5F7);
    outline(top_wifi_menu.x, top_wifi_menu.y, top_wifi_menu.w, top_wifi_menu.h, 0xC3CFDD);

    int cur_y = menu_y + 8;

    if (!is_on) {
        font_text(menu_x + 16, cur_y + 2, "Wi-Fi: Disabled", 0x666666, 1);
        int btn_hover = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), (rect_t){menu_x + 16, cur_y + 24, menu_w - 32, 22, 0});
        fb_rect(menu_x + 16, cur_y + 24, menu_w - 32, 22, btn_hover ? 0x0078D7 : 0xDEE5ED);
        outline(menu_x + 16, cur_y + 24, menu_w - 32, 22, 0x8FA4BA);
        font_text_fit_center(menu_x + 16, cur_y + 29, "Turn Wi-Fi On", btn_hover ? 0xFFFFFF : 0x1E293B, menu_w - 32);
        return;
    }

    if (is_conn) {
        font_icon(menu_x + 12, cur_y + 2, ICON_NETWORK, 0x0078D7);
        font_text(menu_x + 32, cur_y + 2, st->ssid, 0x1E293B, 1);
        int disconn_hover = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), (rect_t){menu_x + menu_w - 78, cur_y, 68, 16, 0});
        if (disconn_hover) {
            fb_rect(menu_x + menu_w - 78, cur_y, 68, 16, 0xFEE2E2);
            outline(menu_x + menu_w - 78, cur_y, 68, 16, 0xFCA5A5);
            font_text_fit_center(menu_x + menu_w - 78, cur_y + 3, "Disconnect", 0xDC2626, 68);
        } else {
            fb_rect(menu_x + menu_w - 78, cur_y, 68, 16, 0xD4EDDA);
            outline(menu_x + menu_w - 78, cur_y, 68, 16, 0xC3E6CB);
            font_text_fit_center(menu_x + menu_w - 78, cur_y + 3, "Connected", 0x155724, 68);
        }
        cur_y += 20;

        char ip_str[48];
        gui_fl_cpy(ip_str, "IP:  ", sizeof(ip_str));
        gui_fl_cat(ip_str, st->ip_addr, sizeof(ip_str));
        font_text(menu_x + 32, cur_y, ip_str, 0x556677, 1);
        cur_y += 18;

        char mac_str[48];
        gui_fl_cpy(mac_str, "MAC: ", sizeof(mac_str));
        gui_fl_cat(mac_str, st->mac_addr, sizeof(mac_str));
        font_text(menu_x + 32, cur_y, mac_str, 0x556677, 1);
        cur_y += 18;

        char dns_str[48];
        gui_fl_cpy(dns_str, "DNS: ", sizeof(dns_str));
        gui_fl_cat(dns_str, st->dns_addr, sizeof(dns_str));
        font_text(menu_x + 32, cur_y, dns_str, 0x556677, 1);
        cur_y += 20;
    } else {
        font_icon(menu_x + 12, cur_y + 2, ICON_NETWORK, 0x778899);
        font_text(menu_x + 32, cur_y + 2, "Wi-Fi: Not Connected", 0x556677, 1);
        cur_y += 22;
    }

    fb_rect(menu_x + 8, cur_y, menu_w - 16, 1, 0xD0D8E2);
    cur_y += 6;

    font_text(menu_x + 14, cur_y, "Available Networks", 0x7A889B, 1);
    cur_y += 16;

    for (int i = 0; i < ap_count; i++) {
        const wifi_ap_t *ap = net_get_ap(i);
        if (!ap) continue;
        int row_y = cur_y + i * 24;
        int hover = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), (rect_t){menu_x + 4, row_y, menu_w - 8, 22, 0});

        if (hover) {
            fb_rect(menu_x + 4, row_y, menu_w - 8, 22, 0x0078D7);
        } else if (ap->connected) {
            fb_rect(menu_x + 4, row_y, menu_w - 8, 22, 0xE5EFF9);
        }

        u32 col = hover ? 0xFFFFFF : (ap->connected ? 0x0078D7 : 0x223344);

        if (ap->connected) {
            font_text(menu_x + 10, row_y + 5, "v", col, 1);
        }
        font_icon(menu_x + 22, row_y + 5, ICON_NETWORK, col);
        font_text(menu_x + 38, row_y + 5, ap->ssid, col, 1);

        if (ap->encrypted) {
            font_text(menu_x + menu_w - 36, row_y + 5, "[*]", col, 1);
        }
    }
    cur_y += ap_count * 24 + 4;

    fb_rect(menu_x + 8, cur_y, menu_w - 16, 1, 0xD0D8E2);
    cur_y += 4;

    int pref_hover = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), (rect_t){menu_x + 4, cur_y, menu_w - 8, 22, 0});
    if (pref_hover) {
        fb_rect(menu_x + 4, cur_y, menu_w - 8, 22, 0x0078D7);
    }
    font_text(menu_x + 14, cur_y + 5, "Network Preferences...", pref_hover ? 0xFFFFFF : 0x334455, 1);
}

static void gui_int_to_str(int n, char *buf, int max) {
    char tmp[16]; int i = 0, j = 0;
    if (n < 0) {
        if (max > 1) buf[j++] = '-';
        n = -n;
    }
    if (n == 0) {
        if (max > j + 1) { buf[j++] = '0'; buf[j] = 0; }
        return;
    }
    while (n > 0 && i < 15) { tmp[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0 && j < max - 1) buf[j++] = tmp[--i];
    buf[j] = 0;
}

static int weather_match_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return 0;
    int hl = 0, nl = 0;
    while (haystack[hl]) hl++;
    while (needle[nl]) nl++;
    if (hl < nl) return 0;
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

static int s_weather_is_live = 0;
int weather_is_live(void) {
    return s_weather_is_live;
}

static char *weather_find_char(char *str, char c) {
    while (*str) {
        if (*str == c) return str;
        str++;
    }
    return (char*)0;
}

static void weather_copy_str(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

int weather_fetch_realtime(const char *city_override) {
    if (!net_is_connected()) return 0;

    char url[128];
    const char *target_city = city_override ? city_override : current_weather.city;
    int ui = 0;
    const char *pfx = "http://wttr.in/";
    while (*pfx && ui < 120) url[ui++] = *pfx++;
    if (target_city && target_city[0]) {
        for (int i = 0; target_city[i] && ui < 100; i++) {
            if (target_city[i] == ' ') url[ui++] = '+';
            else url[ui++] = target_city[i];
        }
    }
    const char *sfx = "?format=%l|%C|%t|%h|%w";
    while (*sfx && ui < 127) url[ui++] = *sfx++;
    url[ui] = 0;

    static char resp[512];
    int n = http_get(url, resp, sizeof(resp) - 1);
    if (n <= 0) return 0;
    resp[n] = 0;

    char *p = resp;
    char *pipe1 = weather_find_char(p, '|');
    if (!pipe1) return 0;
    *pipe1 = 0;

    char *city_str = p;
    while (*city_str == ' ') city_str++;
    char *end_city = pipe1 - 1;
    while (end_city > city_str && (*end_city == ' ' || *end_city == '\r' || *end_city == '\n')) *end_city-- = 0;
    if (*city_str) {
        int ci = 0;
        while (city_str[ci] && city_str[ci] != ',' && ci < 31) {
            current_weather.city[ci] = city_str[ci];
            ci++;
        }
        current_weather.city[ci] = 0;
    }

    p = pipe1 + 1;
    char *pipe2 = weather_find_char(p, '|');
    if (!pipe2) return 0;
    *pipe2 = 0;
    char *cond_str = p;
    while (*cond_str == ' ') cond_str++;
    char *end_cond = pipe2 - 1;
    while (end_cond > cond_str && (*end_cond == ' ' || *end_cond == '\r' || *end_cond == '\n')) *end_cond-- = 0;
    static char s_desc[32];
    weather_copy_str(s_desc, cond_str, sizeof(s_desc));
    current_weather.desc = s_desc;

    if (weather_match_ci(cond_str, "rain") || weather_match_ci(cond_str, "shower") || 
        weather_match_ci(cond_str, "drizzle") || weather_match_ci(cond_str, "storm") ||
        weather_match_ci(cond_str, "thunder") || weather_match_ci(cond_str, "snow")) {
        current_weather.condition = WEATHER_RAINY;
    } else if (weather_match_ci(cond_str, "cloud") || weather_match_ci(cond_str, "overcast") || 
               weather_match_ci(cond_str, "fog") || weather_match_ci(cond_str, "mist") ||
               weather_match_ci(cond_str, "haze")) {
        current_weather.condition = WEATHER_CLOUDY;
    } else {
        current_weather.condition = WEATHER_SUNNY;
    }

    p = pipe2 + 1;
    char *pipe3 = weather_find_char(p, '|');
    if (!pipe3) return 0;
    *pipe3 = 0;
    char *temp_str = p;
    while (*temp_str && *temp_str != '-' && *temp_str != '+' && (*temp_str < '0' || *temp_str > '9')) temp_str++;
    int sign = 1;
    if (*temp_str == '-') { sign = -1; temp_str++; }
    else if (*temp_str == '+') { temp_str++; }
    int tval = 0;
    while (*temp_str >= '0' && *temp_str <= '9') {
        tval = tval * 10 + (*temp_str - '0');
        temp_str++;
    }
    current_weather.temp_c = sign * tval;

    p = pipe3 + 1;
    char *pipe4 = weather_find_char(p, '|');
    if (!pipe4) return 0;
    *pipe4 = 0;
    char *hum_str = p;
    while (*hum_str && (*hum_str < '0' || *hum_str > '9')) hum_str++;
    int hval = 0;
    while (*hum_str >= '0' && *hum_str <= '9') {
        hval = hval * 10 + (*hum_str - '0');
        hum_str++;
    }
    if (hval >= 0 && hval <= 100) current_weather.humidity = hval;

    p = pipe4 + 1;
    char *wind_str = p;
    while (*wind_str && (*wind_str < '0' || *wind_str > '9')) wind_str++;
    int wval = 0;
    while (*wind_str >= '0' && *wind_str <= '9') {
        wval = wval * 10 + (*wind_str - '0');
        wind_str++;
    }
    if (wval >= 0) current_weather.wind_kph = wval;

    s_weather_is_live = 1;
    return 1;
}

void weather_set_city(const char *city_or_zip) {
    if (!city_or_zip || !city_or_zip[0]) return;
    int i = 0;
    while (city_or_zip[i] && i < 31) {
        current_weather.city[i] = city_or_zip[i];
        i++;
    }
    current_weather.city[i] = 0;

    if (net_is_connected()) {
        if (weather_fetch_realtime(city_or_zip)) return;
    }

    if (weather_match_ci(city_or_zip, "york") || weather_match_ci(city_or_zip, "nyc") || weather_match_ci(city_or_zip, "10001")) {
        current_weather.condition = WEATHER_SUNNY;
        current_weather.temp_c = 22;
        current_weather.humidity = 48;
        current_weather.wind_kph = 14;
        current_weather.desc = "Sunny";
    } else if (weather_match_ci(city_or_zip, "london") || weather_match_ci(city_or_zip, "lon") || weather_match_ci(city_or_zip, "sw1")) {
        current_weather.condition = WEATHER_RAINY;
        current_weather.temp_c = 15;
        current_weather.humidity = 84;
        current_weather.wind_kph = 22;
        current_weather.desc = "Rainy";
    } else if (weather_match_ci(city_or_zip, "tokyo") || weather_match_ci(city_or_zip, "tyo")) {
        current_weather.condition = WEATHER_CLOUDY;
        current_weather.temp_c = 19;
        current_weather.humidity = 62;
        current_weather.wind_kph = 11;
        current_weather.desc = "Cloudy";
    } else if (weather_match_ci(city_or_zip, "paris") || weather_match_ci(city_or_zip, "par") || weather_match_ci(city_or_zip, "75001")) {
        current_weather.condition = WEATHER_SUNNY;
        current_weather.temp_c = 23;
        current_weather.humidity = 50;
        current_weather.wind_kph = 13;
        current_weather.desc = "Clear";
    } else if (weather_match_ci(city_or_zip, "berlin") || weather_match_ci(city_or_zip, "ber") || weather_match_ci(city_or_zip, "10115")) {
        current_weather.condition = WEATHER_CLOUDY;
        current_weather.temp_c = 17;
        current_weather.humidity = 68;
        current_weather.wind_kph = 18;
        current_weather.desc = "Overcast";
    } else if (weather_match_ci(city_or_zip, "francisco") || weather_match_ci(city_or_zip, "sf") || weather_match_ci(city_or_zip, "94102")) {
        current_weather.condition = WEATHER_SUNNY;
        current_weather.temp_c = 18;
        current_weather.humidity = 55;
        current_weather.wind_kph = 19;
        current_weather.desc = "Sunny & Breezy";
    } else {
        u32 h = 5381;
        for (int k = 0; city_or_zip[k]; k++) {
            h = ((h << 5) + h) + (u8)city_or_zip[k];
        }
        current_weather.condition = (weather_cond_t)(h % 3);
        current_weather.temp_c = 14 + (int)(h % 16);
        current_weather.humidity = 40 + (int)((h >> 4) % 50);
        current_weather.wind_kph = 8 + (int)((h >> 8) % 20);
        if (current_weather.condition == WEATHER_SUNNY) current_weather.desc = "Sunny";
        else if (current_weather.condition == WEATHER_CLOUDY) current_weather.desc = "Cloudy";
        else current_weather.desc = "Light Rain";
    }
}

void weather_tick(void) {
    static int check_counter = 0;
    static int net_poll_counter = 0;
    check_counter++;

    if (check_counter >= 2) {
        check_counter = 0;
        char wbuf[256];
        int n = fs_read_at("/weather.txt", wbuf, sizeof(wbuf) - 1);
        if (n <= 0) {
            n = fs_read_at("/Desktop/weather.txt", wbuf, sizeof(wbuf) - 1);
        }
        if (n > 0) {
            wbuf[n] = 0;
            char *line = wbuf;
            while (*line) {
                char *eol = line;
                while (*eol && *eol != '\n' && *eol != '\r') eol++;
                char orig_end = *eol;
                *eol = 0;

                while (*line == ' ') line++;
                if (weather_match_ci(line, "CITY=")) {
                    char *val = line + 5;
                    while (*val == ' ') val++;
                    int vi = 0;
                    while (val[vi] && vi < 31) { current_weather.city[vi] = val[vi]; vi++; }
                    current_weather.city[vi] = 0;
                } else if (weather_match_ci(line, "TEMP=")) {
                    char *val = line + 5;
                    while (*val == ' ') val++;
                    int sign = 1, tval = 0;
                    if (*val == '-') { sign = -1; val++; }
                    while (*val >= '0' && *val <= '9') { tval = tval * 10 + (*val - '0'); val++; }
                    current_weather.temp_c = sign * tval;
                } else if (weather_match_ci(line, "COND=")) {
                    char *val = line + 5;
                    while (*val == ' ') val++;
                    if (weather_match_ci(val, "rain") || weather_match_ci(val, "storm")) {
                        current_weather.condition = WEATHER_RAINY;
                        current_weather.desc = "Rainy";
                    } else if (weather_match_ci(val, "cloud") || weather_match_ci(val, "fog") || weather_match_ci(val, "overcast")) {
                        current_weather.condition = WEATHER_CLOUDY;
                        current_weather.desc = "Cloudy";
                    } else {
                        current_weather.condition = WEATHER_SUNNY;
                        current_weather.desc = "Sunny";
                    }
                } else if (weather_match_ci(line, "HUMIDITY=") || weather_match_ci(line, "HUM=")) {
                    char *val = line + (weather_match_ci(line, "HUM=") ? 4 : 9);
                    while (*val == ' ') val++;
                    int hval = 0;
                    while (*val >= '0' && *val <= '9') { hval = hval * 10 + (*val - '0'); val++; }
                    if (hval >= 0 && hval <= 100) current_weather.humidity = hval;
                } else if (weather_match_ci(line, "WIND=")) {
                    char *val = line + 5;
                    while (*val == ' ') val++;
                    int wval = 0;
                    while (*val >= '0' && *val <= '9') { wval = wval * 10 + (*val - '0'); val++; }
                    if (wval >= 0) current_weather.wind_kph = wval;
                } else if (weather_match_ci(line, "DESC=")) {
                    char *val = line + 5;
                    while (*val == ' ') val++;
                    static char desc_buf[32];
                    int di = 0;
                    while (val[di] && di < 31) { desc_buf[di] = val[di]; di++; }
                    desc_buf[di] = 0;
                    current_weather.desc = desc_buf;
                }

                if (!orig_end) break;
                line = eol + 1;
                while (*line == '\r' || *line == '\n') line++;
            }
            s_weather_is_live = 1;
            return;
        }
    }

    // Periodic real-time network fetch
    net_poll_counter++;
    if (net_is_connected()) {
        if (!s_weather_is_live || net_poll_counter >= 300) {
            net_poll_counter = 0;
            weather_fetch_realtime((const char*)0);
        }
    }

    if (s_weather_is_live) return;

    // Diurnal temperature cycle synchronized with RTC
    rtc_time_t rtc;
    rtc_read(&rtc);
    int base_temp = 18;
    if (weather_match_ci(current_weather.city, "london")) base_temp = 14;
    else if (weather_match_ci(current_weather.city, "tokyo")) base_temp = 17;
    else if (weather_match_ci(current_weather.city, "paris")) base_temp = 19;
    else if (weather_match_ci(current_weather.city, "francisco")) base_temp = 16;
    else if (weather_match_ci(current_weather.city, "berlin")) base_temp = 15;

    int hour = rtc.hour;
    int delta = 0;
    if (hour >= 6 && hour < 14) {
        delta = ((hour - 6) * 6) / 8;
    } else if (hour >= 14 && hour < 21) {
        delta = 6 - ((hour - 14) * 5) / 7;
    } else {
        delta = -2;
    }
    current_weather.temp_c = base_temp + delta;
}

void draw_weather_icon(int cx, int cy, weather_cond_t cond, int size, u32 def_color) {
    (void)def_color;
    if (cond == WEATHER_SUNNY) {
        u32 sun_gold = 0xF59E0B;
        int r = size >= 24 ? 6 : 4;
        fb_round_rect(cx - r, cy - r, r * 2, r * 2, r, sun_gold);
        int ray_len = size >= 24 ? 3 : 2;
        int dist = r + 2;
        fb_rect(cx - 1, cy - dist - ray_len, 2, ray_len, sun_gold);
        fb_rect(cx - 1, cy + dist, 2, ray_len, sun_gold);
        fb_rect(cx - dist - ray_len, cy - 1, ray_len, 2, sun_gold);
        fb_rect(cx + dist, cy - 1, ray_len, 2, sun_gold);
        if (size >= 20) {
            int d = r + 1;
            fb_rect(cx - d - 1, cy - d - 1, 2, 2, sun_gold);
            fb_rect(cx + d, cy - d - 1, 2, 2, sun_gold);
            fb_rect(cx - d - 1, cy + d, 2, 2, sun_gold);
            fb_rect(cx + d, cy + d, 2, 2, sun_gold);
        }
    } else if (cond == WEATHER_CLOUDY) {
        u32 cloud_color = 0xCBD5E1;
        u32 cloud_shade = 0x94A3B8;
        if (size >= 24) {
            fb_round_rect(cx - 12, cy - 2, 24, 12, 6, cloud_color);
            fb_round_rect(cx - 8, cy - 8, 12, 12, 6, cloud_color);
            fb_round_rect(cx + 1, cy - 6, 10, 10, 5, cloud_color);
            fb_round_rect_border(cx - 12, cy - 2, 24, 12, 6, cloud_shade);
        } else {
            fb_round_rect(cx - 7, cy - 1, 14, 7, 3, cloud_color);
            fb_round_rect(cx - 4, cy - 5, 8, 8, 4, cloud_color);
            fb_round_rect(cx + 1, cy - 3, 6, 6, 3, cloud_color);
        }
    } else {
        u32 cloud_color = 0x94A3B8;
        u32 rain_color = 0x38BDF8;
        if (size >= 24) {
            fb_round_rect(cx - 12, cy - 8, 24, 10, 5, cloud_color);
            fb_round_rect(cx - 7, cy - 12, 11, 10, 5, cloud_color);
            fb_rect(cx - 8, cy + 4, 2, 5, rain_color);
            fb_rect(cx - 3, cy + 6, 2, 5, rain_color);
            fb_rect(cx + 2, cy + 4, 2, 5, rain_color);
            fb_rect(cx + 7, cy + 6, 2, 5, rain_color);
        } else {
            fb_round_rect(cx - 6, cy - 5, 13, 6, 3, cloud_color);
            fb_round_rect(cx - 3, cy - 8, 7, 7, 3, cloud_color);
            fb_rect(cx - 4, cy + 2, 1, 3, rain_color);
            fb_rect(cx, cy + 3, 1, 3, rain_color);
            fb_rect(cx + 4, cy + 2, 1, 3, rain_color);
        }
    }
}

static void draw_volume_menu(void) {
    if (!volume_menu_open) return;
    fb_draw_card_shadow(top_volume_menu.x, top_volume_menu.y, top_volume_menu.w, top_volume_menu.h, 10);
    fb_round_rect(top_volume_menu.x, top_volume_menu.y, top_volume_menu.w, top_volume_menu.h, 10, 0xF8FAFC);
    fb_round_rect_border(top_volume_menu.x, top_volume_menu.y, top_volume_menu.w, top_volume_menu.h, 10, 0xC3CFDD);

    // Speaker icon
    font_icon(top_volume_menu.x + 12, top_volume_menu.y + 14, ICON_DISPLAY, 0x475569);

    // Rounded slider track
    int track_y = top_volume_slider.y + 2;
    int track_h = 8;
    fb_round_rect(top_volume_slider.x, track_y, top_volume_slider.w, track_h, 4, 0xE2E8F0);

    // Rounded fill
    int fill_w = (top_volume_slider.w * system_volume) / 100;
    if (fill_w > 0) {
        fb_round_rect(top_volume_slider.x, track_y, fill_w, track_h, 4, 0x2563EB);
    }

    // Rounded circular thumb
    int thumb_x = top_volume_slider.x + fill_w;
    fb_round_rect(thumb_x - 7, top_volume_slider.y - 1, 14, 14, 7, 0xFFFFFF);
    fb_round_rect_border(thumb_x - 7, top_volume_slider.y - 1, 14, 14, 7, 0x94A3B8);
}

static void draw_clock_menu(void) {
    static const char *months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    static const char *days_full[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *weekdays[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    rtc_time_t now;
    if (!clock_menu_open) return;
    rtc_read(&now);
    if (clock_view_month == 0) {
        clock_view_month = now.month;
        clock_view_year = now.year;
    }

    int cx = top_clock_menu.x;
    int cy = top_clock_menu.y;
    int cw = top_clock_menu.w;
    int ch = top_clock_menu.h;

    // Translucent black shadow & rounded card
    fb_draw_window_shadow(cx, cy, cw, ch, 12, 1);
    fb_round_rect(cx, cy, cw, ch, 12, 0xF8FAFC);
    fb_round_rect_border(cx, cy, cw, ch, 12, 0xC3CFDD);

    // Header: Month Year + Prev/Next buttons
    char hdr_buf[32];
    gui_fl_cpy(hdr_buf, months[(clock_view_month - 1) % 12], sizeof(hdr_buf));
    gui_fl_cat(hdr_buf, " ", sizeof(hdr_buf));
    char yr_buf[16];
    gui_int_to_str(clock_view_year, yr_buf, sizeof(yr_buf));
    gui_fl_cat(hdr_buf, yr_buf, sizeof(hdr_buf));
    font_text(cx + 16, cy + 12, hdr_buf, 0x1E293B, 1);

    // Navigation buttons: [<] and [>]
    int btn_prev_hov = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), (rect_t){cx + cw - 52, cy + 10, 20, 20, 0});
    int btn_next_hov = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), (rect_t){cx + cw - 28, cy + 10, 20, 20, 0});
    fb_round_rect(cx + cw - 52, cy + 10, 20, 20, 4, btn_prev_hov ? 0xE2E8F0 : 0xEEF2F6);
    fb_round_rect_border(cx + cw - 52, cy + 10, 20, 20, 4, 0xCBD5E1);
    font_text(cx + cw - 46, cy + 14, "<", 0x334155, 1);

    fb_round_rect(cx + cw - 28, cy + 10, 20, 20, 4, btn_next_hov ? 0xE2E8F0 : 0xEEF2F6);
    fb_round_rect_border(cx + cw - 28, cy + 10, 20, 20, 4, 0xCBD5E1);
    font_text(cx + cw - 22, cy + 14, ">", 0x334155, 1);

    // Separator
    fb_rect(cx + 12, cy + 36, cw - 24, 1, 0xE2E8F0);

    // Weekday column headers (7 columns)
    int col_w = (cw - 24) / 7;
    int start_x = cx + 12;
    int header_y = cy + 44;
    for (int c = 0; c < 7; c++) {
        font_text_fit_center(start_x + c * col_w, header_y, weekdays[c], 0x64748B, col_w);
    }

    // Days grid
    int first_day = day_of_week(1, clock_view_month, clock_view_year);
    int days_in_month = month_days(clock_view_month, clock_view_year);
    int current_day = 1;
    int grid_y = header_y + 18;

    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            if (row == 0 && col < first_day) continue;
            if (current_day > days_in_month) break;

            int cell_x = start_x + col * col_w;
            int cell_y = grid_y + row * 22;
            int is_today = (current_day == now.day && clock_view_month == now.month && clock_view_year == now.year);
            int is_hov = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), (rect_t){cell_x, cell_y - 2, col_w, 20, 0});

            if (is_today) {
                fb_round_rect(cell_x + (col_w - 22) / 2, cell_y - 2, 22, 18, 9, 0x2563EB);
            } else if (is_hov) {
                fb_round_rect(cell_x + (col_w - 22) / 2, cell_y - 2, 22, 18, 5, 0xE2E8F0);
            }

            char d[4];
            if (current_day < 10) {
                d[0] = '0' + current_day;
                d[1] = 0;
            } else {
                d[0] = '0' + (current_day / 10);
                d[1] = '0' + (current_day % 10);
                d[2] = 0;
            }
            u32 fg = is_today ? 0xFFFFFF : (is_hov ? 0x0F172A : 0x334155);
            font_text_fit_center(cell_x, cell_y + 2, d, fg, col_w);

            current_day++;
        }
    }

    // Bottom summary bar
    fb_rect(cx + 12, cy + ch - 34, cw - 24, 1, 0xE2E8F0);
    int dow = day_of_week(now.day, now.month, now.year);
    char full_date_str[64];
    gui_fl_cpy(full_date_str, days_full[dow % 7], sizeof(full_date_str));
    gui_fl_cat(full_date_str, ", ", sizeof(full_date_str));
    gui_fl_cat(full_date_str, months[(now.month - 1) % 12], sizeof(full_date_str));
    gui_fl_cat(full_date_str, " ", sizeof(full_date_str));
    char d_str[8];
    gui_int_to_str(now.day, d_str, sizeof(d_str));
    gui_fl_cat(full_date_str, d_str, sizeof(full_date_str));
    font_text(cx + 16, cy + ch - 22, full_date_str, 0x64748B, 1);
}

static void draw_weather_popover(void) {
    if (!weather_popover_open) return;
    int px = weather_popover_rect.x;
    int py = weather_popover_rect.y;
    int pw = weather_popover_rect.w;
    int ph = weather_popover_rect.h;

    // Translucent black shadow & rounded card
    fb_draw_window_shadow(px, py, pw, ph, 12, 1);
    fb_round_rect(px, py, pw, ph, 12, 0xF8FAFC);
    fb_round_rect_border(px, py, pw, ph, 12, 0xC3CFDD);

    // Header: City Name + Condition
    char city_hdr[48];
    gui_fl_cpy(city_hdr, current_weather.city, sizeof(city_hdr));
    font_text(px + 16, py + 14, city_hdr, 0x0F172A, 1);
    font_text(px + 16, py + 30, current_weather.desc, 0x64748B, 1);

    // Weather Art / Large Icon + Big Temp
    draw_weather_icon(px + pw - 48, py + 32, current_weather.condition, 28, 0x000000);

    char temp_str[16];
    gui_int_to_str(current_weather.temp_c, temp_str, sizeof(temp_str));
    gui_fl_cat(temp_str, " C", sizeof(temp_str));
    font_text(px + 16, py + 52, temp_str, 0x1E293B, 2);

    // Stats row: Humidity and Wind
    char stats_str[64];
    gui_fl_cpy(stats_str, "Humidity: ", sizeof(stats_str));
    char hum_buf[16];
    gui_int_to_str(current_weather.humidity, hum_buf, sizeof(hum_buf));
    gui_fl_cat(stats_str, hum_buf, sizeof(stats_str));
    gui_fl_cat(stats_str, "%   Wind: ", sizeof(stats_str));
    char wnd_buf[16];
    gui_int_to_str(current_weather.wind_kph, wnd_buf, sizeof(wnd_buf));
    gui_fl_cat(stats_str, wnd_buf, sizeof(stats_str));
    gui_fl_cat(stats_str, " km/h", sizeof(stats_str));
    font_text(px + 16, py + 86, stats_str, 0x475569, 1);

    // Separator
    fb_rect(px + 12, py + 108, pw - 24, 1, 0xE2E8F0);

    // City / Postal Code selector prompt
    font_text(px + 16, py + 116, "City or Postal Code:", 0x334155, 1);

    // Text Input field
    rect_t input_rect = {px + 16, py + 132, pw - 32, 24, 0};
    fb_round_rect(input_rect.x, input_rect.y, input_rect.w, input_rect.h, 5, 0xFFFFFF);
    fb_round_rect_border(input_rect.x, input_rect.y, input_rect.w, input_rect.h, 5, weather_input_focused ? 0x2563EB : 0xCBD5E1);

    if (weather_input_len > 0) {
        font_text(input_rect.x + 8, input_rect.y + 6, weather_input_city, 0x0F172A, 1);
        if (weather_input_focused) {
            int cx_cur = input_rect.x + 8 + weather_input_len * 7;
            fb_rect(cx_cur, input_rect.y + 4, 2, 16, 0x2563EB);
        }
    } else {
        font_text(input_rect.x + 8, input_rect.y + 6, weather_input_focused ? "" : "e.g. 10001, Paris, London", 0x94A3B8, 1);
        if (weather_input_focused) {
            fb_rect(input_rect.x + 8, input_rect.y + 4, 2, 16, 0x2563EB);
        }
    }

    // Quick-select preset pills
    static const char *pill_names[6] = {"NYC", "LON", "TYO", "PAR", "BER", "SF"};
    int pill_w = 36;
    int pill_gap = 4;
    int p_start_x = px + 16;
    int p_y = py + 164;
    for (int p = 0; p < 6; p++) {
        rect_t pr = {p_start_x + p * (pill_w + pill_gap), p_y, pill_w, 20, 0};
        int is_hov = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), pr);
        fb_round_rect(pr.x, pr.y, pr.w, pr.h, 4, is_hov ? 0x2563EB : 0xE2E8F0);
        font_text_fit_center(pr.x, pr.y + 4, pill_names[p], is_hov ? 0xFFFFFF : 0x334155, pr.w);
    }
}

static void draw_fps_dropdown(void) {
    if (!fps_dropdown_open) return;

    int x = fps_dropdown_rect.x;
    int y = fps_dropdown_rect.y;
    int w = fps_dropdown_rect.w;
    int h = fps_dropdown_rect.h;

    // Translucent card shadow and rounded card
    fb_draw_card_shadow(x, y, w, h, 12);
    fb_round_rect(x, y, w, h, 12, 0xF8FAFC);
    fb_round_rect_border(x, y, w, h, 12, 0xC3CFDD);

    // Title
    font_text(x + 16, y + 12, "PERFORMANCE & DISPLAY", 0x0F172A, 1);
    fb_rect(x + 14, y + 30, w - 28, 1, 0xE2E8F0);

    // Row 1: Framerate & Badge
    int cur_y = y + 38;
    font_text(x + 16, cur_y + 2, "Framerate:", 0x475569, 1);
    char cur_fps_buf[16];
    fps_text(cur_fps_buf);
    int badge_w = 64;
    int badge_x = x + w - 16 - badge_w;
    fb_round_rect(badge_x, cur_y, badge_w, 20, 5, 0xDCFCE7);
    fb_round_rect_border(badge_x, cur_y, badge_w, 20, 5, 0x86EFAC);
    font_text_fit_center(badge_x, cur_y + 4, cur_fps_buf, 0x166534, badge_w);

    // Row 2: Frame Time
    cur_y += 26;
    font_text(x + 16, cur_y + 2, "Frame Time:", 0x475569, 1);
    font_text(x + w - 16 - 96, cur_y + 2, "16.6 ms (60Hz)", 0x1E293B, 1);

    // Row 3: Resolution
    cur_y += 24;
    font_text(x + 16, cur_y + 2, "Resolution:", 0x475569, 1);
    char res_buf[32];
    gui_int_to_str((int)fb.width, res_buf, sizeof(res_buf));
    gui_fl_cat(res_buf, "x", sizeof(res_buf));
    char h_buf[16];
    gui_int_to_str((int)fb.height, h_buf, sizeof(h_buf));
    gui_fl_cat(res_buf, h_buf, sizeof(res_buf));
    gui_fl_cat(res_buf, " 32bpp", sizeof(res_buf));
    font_text(x + w - 16 - 8 * 14, cur_y + 2, res_buf, 0x1E293B, 1);

    // Row 4: Graphics
    cur_y += 24;
    font_text(x + 16, cur_y + 2, "Graphics:", 0x475569, 1);
    extern int virtio_gpu_available(void);
    const char *gpu_lbl = virtio_gpu_available() ? "VirtIO-GPU" : "Standard VGA";
    font_text(x + w - 16 - 8 * 11, cur_y + 2, gpu_lbl, 0x1E293B, 1);

    // Row 5: Compositor status
    cur_y += 24;
    font_text(x + 16, cur_y + 2, "Compositor:", 0x475569, 1);
    font_text(x + w - 16 - 8 * 14, cur_y + 2, "Double-Buffered", 0x2563EB, 1);

    // Footer divider
    cur_y += 22;
    fb_rect(x + 14, cur_y, w - 28, 1, 0xE2E8F0);
    font_text(x + 16, cur_y + 6, "Smooth 60 FPS Engine Active", 0x94A3B8, 1);
}

static void init_default_notifications(void) {
    if (notif_initialized) return;
    notif_initialized = 1;
    notification_count = 4;

    gui_fl_cpy(gui_notifications[0].app, "System", sizeof(gui_notifications[0].app));
    gui_fl_cpy(gui_notifications[0].title, "FirstOS 2.1 Ready", sizeof(gui_notifications[0].title));
    gui_fl_cpy(gui_notifications[0].message, "Welcome to FirstOS workstation.", sizeof(gui_notifications[0].message));
    gui_fl_cpy(gui_notifications[0].time_ago, "Just now", sizeof(gui_notifications[0].time_ago));
    gui_notifications[0].active = 1;
    gui_notifications[0].accent = 0x2563EB;

    gui_fl_cpy(gui_notifications[1].app, "Network", sizeof(gui_notifications[1].app));
    gui_fl_cpy(gui_notifications[1].title, "Wi-Fi Connected", sizeof(gui_notifications[1].title));
    gui_fl_cpy(gui_notifications[1].message, "IP 10.0.2.15 on virtio-net adapter.", sizeof(gui_notifications[1].message));
    gui_fl_cpy(gui_notifications[1].time_ago, "2m ago", sizeof(gui_notifications[1].time_ago));
    gui_notifications[1].active = 1;
    gui_notifications[1].accent = 0x10B981;

    gui_fl_cpy(gui_notifications[2].app, "Display", sizeof(gui_notifications[2].app));
    gui_fl_cpy(gui_notifications[2].title, "Appearance & Shadows", sizeof(gui_notifications[2].title));
    gui_fl_cpy(gui_notifications[2].message, "Modern rounded GUI and drop shadows active.", sizeof(gui_notifications[2].message));
    gui_fl_cpy(gui_notifications[2].time_ago, "5m ago", sizeof(gui_notifications[2].time_ago));
    gui_notifications[2].active = 1;
    gui_notifications[2].accent = 0x8B5CF6;

    gui_fl_cpy(gui_notifications[3].app, "Storage", sizeof(gui_notifications[3].app));
    gui_fl_cpy(gui_notifications[3].title, "FirstOS HD", sizeof(gui_notifications[3].title));
    gui_fl_cpy(gui_notifications[3].message, "Mounted root filesystem with 1.0 GB capacity.", sizeof(gui_notifications[3].message));
    gui_fl_cpy(gui_notifications[3].time_ago, "10m ago", sizeof(gui_notifications[3].time_ago));
    gui_notifications[3].active = 1;
    gui_notifications[3].accent = 0xF59E0B;
}

static void draw_notification_center(void) {
    if (!notification_center_open) return;
    init_default_notifications();

    int nx = notif_center_rect.x;
    int ny = notif_center_rect.y;
    int nw = notif_center_rect.w;
    int nh = notif_center_rect.h;

    // Translucent black shadow & rounded card
    fb_draw_window_shadow(nx, ny, nw, nh, 12, 1);
    fb_round_rect(nx, ny, nw, nh, 12, 0xF8FAFC);
    fb_round_rect_border(nx, ny, nw, nh, 12, 0xC3CFDD);

    // Header Title
    font_text(nx + 16, ny + 12, "NOTIFICATION CENTER", 0x0F172A, 1);

    // Do Not Disturb Card (height 38)
    int dnd_y = ny + 32;
    fb_round_rect(nx + 12, dnd_y, nw - 24, 38, 8, 0xFFFFFF);
    fb_round_rect_border(nx + 12, dnd_y, nw - 24, 38, 8, 0xE2E8F0);

    // Moon icon / DND label
    fb_round_rect(nx + 22, dnd_y + 11, 14, 14, 7, 0x6366F1);
    fb_round_rect(nx + 26, dnd_y + 11, 11, 11, 5, 0xFFFFFF);
    font_text(nx + 44, dnd_y + 12, "Do Not Disturb", 0x1E293B, 1);

    // DND Switch: pill shape (w=40, h=20)
    rect_t dnd_sw = {nx + nw - 56, dnd_y + 9, 40, 20, 0};
    fb_round_rect(dnd_sw.x, dnd_sw.y, dnd_sw.w, dnd_sw.h, 10, notification_dnd ? 0x6366F1 : 0xCBD5E1);
    int thumb_x = notification_dnd ? (dnd_sw.x + 22) : (dnd_sw.x + 2);
    fb_round_rect(thumb_x, dnd_sw.y + 2, 16, 16, 8, 0xFFFFFF);

    // Separator
    fb_rect(nx + 12, ny + 78, nw - 24, 1, 0xE2E8F0);

    // Actions row: NOTIFICATIONS (count) and [Delete All] button
    char notif_hdr[32];
    gui_fl_cpy(notif_hdr, "NOTIFICATIONS (", sizeof(notif_hdr));
    char cnt_buf[8];
    gui_int_to_str(notification_count, cnt_buf, sizeof(cnt_buf));
    gui_fl_cat(notif_hdr, cnt_buf, sizeof(notif_hdr));
    gui_fl_cat(notif_hdr, ")", sizeof(notif_hdr));
    font_text(nx + 16, ny + 88, notif_hdr, 0x64748B, 1);

    if (notification_count > 0) {
        rect_t btn_del_all = {nx + nw - 92, ny + 84, 80, 20, 0};
        int hov_del = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), btn_del_all);
        fb_round_rect(btn_del_all.x, btn_del_all.y, btn_del_all.w, btn_del_all.h, 4, hov_del ? 0xDC2626 : 0xEF4444);
        font_text_fit_center(btn_del_all.x, btn_del_all.y + 4, "Delete All", 0xFFFFFF, btn_del_all.w);
    }

    // Notification items list
    int card_y = ny + 110;
    if (notification_count == 0) {
        fb_round_rect(nx + nw/2 - 18, card_y + 40, 36, 36, 18, 0xE2E8F0);
        font_icon(nx + nw/2 - 9, card_y + 49, ICON_SETTINGS, 0x94A3B8);
        font_text_fit_center(nx, card_y + 88, "No New Notifications", 0x64748B, nw);
        font_text_fit_center(nx, card_y + 106, "You are all caught up!", 0x94A3B8, nw);
    } else {
        for (int k = 0; k < MAX_NOTIFICATIONS; k++) {
            if (!gui_notifications[k].active) continue;
            rect_t card = {nx + 12, card_y, nw - 24, 60, 0};
            fb_round_rect(card.x, card.y, card.w, card.h, 8, 0xFFFFFF);
            fb_round_rect_border(card.x, card.y, card.w, card.h, 8, 0xE2E8F0);

            // Left colored accent strip
            fb_round_rect(card.x + 2, card.y + 6, 3, card.h - 12, 1, gui_notifications[k].accent);

            // App badge + timestamp
            font_text(card.x + 12, card.y + 8, gui_notifications[k].app, gui_notifications[k].accent, 1);
            font_text(card.x + 80, card.y + 8, gui_notifications[k].time_ago, 0x94A3B8, 1);

            // Dismiss [x] button at top-right
            rect_t btn_dism = {card.x + card.w - 22, card.y + 6, 16, 16, 0};
            int hov_dism = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), btn_dism);
            if (hov_dism) {
                fb_round_rect(btn_dism.x, btn_dism.y, btn_dism.w, btn_dism.h, 4, 0xFEE2E2);
            }
            font_text(btn_dism.x + 4, btn_dism.y + 2, "x", hov_dism ? 0xDC2626 : 0x94A3B8, 1);

            // Title & message
            font_text(card.x + 12, card.y + 24, gui_notifications[k].title, 0x0F172A, 1);
            font_text(card.x + 12, card.y + 40, gui_notifications[k].message, 0x475569, 1);

            card_y += 66;
            if (card_y + 60 > ny + nh) break;
        }
    }
}

void open_app(int kind) {
    app_window_t *app = app_slot(kind);
    int index;
    if (!app) return;
    index = kind - 1;
    app->open = 1;
    app->minimized = 0;
    app->maximized = 0;
    app->resizing = 0;
    app->kind = kind;
    app->min_w = (kind == APP_BROWSER) ? 520 : (kind == APP_TEXT_EDIT ? 580 : (kind == APP_MEDIA_PLAYER ? 440 : 220));
    app->min_h = (kind == APP_BROWSER) ? 360 : (kind == APP_TEXT_EDIT ? 320 : (kind == APP_MEDIA_PLAYER ? 280 : 140));
    app->last_title_click_ticks = 0;
    int fx = (kind == APP_BROWSER) ? 40 : (80 + (kind - 1) * 16);
    int fy = (kind == APP_BROWSER) ? 36 : (50 + (kind - 1) * 12);
    int fw = (kind == APP_BROWSER) ? 944 : (kind == APP_TEXT_EDIT ? 680 : (kind == APP_EXPLORER ? 560 : (kind == APP_SETTINGS ? 640 : (kind == APP_PREVIEW ? 580 : (kind == APP_MEDIA_PLAYER ? 560 : (kind == APP_TERMINAL ? 520 : 320))))));
    int fh = (kind == APP_BROWSER) ? 680 : (kind == APP_TEXT_EDIT ? 480 : (kind == APP_EXPLORER ? 360 : (kind == APP_SETTINGS ? 440 : (kind == APP_PREVIEW ? 440 : (kind == APP_MEDIA_PLAYER ? 380 : (kind == APP_TERMINAL ? 320 : 220))))));
    app->frame = (rect_t){fx, fy, fw, fh, 0};
    app->title_color = kind == APP_BROWSER ? 0x1E293B : (kind == APP_TERMINAL ? 0x315A8A : kind == APP_FILES ? 0x3B6D72 : kind == APP_DISPLAY ? 0x5A4D86 : kind == APP_EXPLORER ? 0x756235 : kind == APP_SETTINGS ? 0x465D8C : kind == APP_PREVIEW ? 0x2A3E54 : kind == APP_MEDIA_PLAYER ? 0x1E2B3D : 0x7A4D2F);

    app->saved_x = app->frame.x;
    app->saved_y = app->frame.y;
    app->saved_w = app->frame.w;
    app->saved_h = app->frame.h;
    sync_app_window(app);
    begin_window_transition(index, 2);
    focus_app_window(kind - 1);
    app_menu_open = 0;
    if (kind == APP_TEXT_EDIT) {
        editor_save_dialog_open = 0;
        explorer_picker_mode = 0;
        editor_save_name_focus = 0;
        extern void text_edit_new_file(void);
        text_edit_new_file();
    } else if (kind == APP_TERMINAL) {
        extern void terminal_clear(void);
        terminal_clear();
    } else if (kind == APP_BROWSER) {
        static int browser_ever_inited = 0;
        if (!browser_ever_inited) {
            web_browser_app_init();
            browser_ever_inited = 1;
        }
    }
    needs_redraw = 1;
}

static void draw_circle_filled(int cx, int cy, int r, u32 color) {
    int y, x, rr = r * r;
    for (y = -r; y <= r; y++) {
        for (x = -r; x <= r; x++) {
            if (x*x + y*y <= rr) fb_putpixel(cx + x, cy + y, color);
        }
    }
}

static void draw_window_controls(rect_t close_btn, rect_t max_btn, rect_t min_btn, rect_t resize_btn) {
    int cx = close_btn.x + close_btn.w / 2;
    int cy = close_btn.y + close_btn.h / 2;
    int cr = close_btn.w / 2;
    draw_circle_filled(cx, cy, cr, 0xFF5F56);
    draw_circle_filled(cx, cy, cr - 1, 0xFF6B63);

    int mx = min_btn.x + min_btn.w / 2;
    int my = min_btn.y + min_btn.h / 2;
    int mr = min_btn.w / 2;
    draw_circle_filled(mx, my, mr, 0xFFBD2E);
    draw_circle_filled(mx, my, mr - 1, 0xFFC73D);

    int xx = max_btn.x + max_btn.w / 2;
    int xy = max_btn.y + max_btn.h / 2;
    int xr = max_btn.w / 2;
    draw_circle_filled(xx, xy, xr, 0x27C93F);
    draw_circle_filled(xx, xy, xr - 1, 0x33D14B);
    
    // Draw the resize handle in the bottom right
    fill(resize_btn);
    fb_rect(resize_btn.x + 1, resize_btn.y + 7, 2, 2, 0xB0B8C4);
    fb_rect(resize_btn.x + 4, resize_btn.y + 4, 2, 2, 0xB0B8C4);
    fb_rect(resize_btn.x + 7, resize_btn.y + 1, 2, 2, 0xB0B8C4);
}



static char calc_display[32] = "0";
static int calc_val = 0;
static int calc_prev = 0;
static int calc_op = 0; // 0=none, 1=add, 2=sub, 3=mul, 4=div
static int calc_new_num = 1;

static void calc_format_display() {
    int v = calc_val;
    int i = 0;
    int is_neg = 0;
    char rev[32];
    if (v == 0) {
        calc_display[0] = '0';
        calc_display[1] = 0;
        return;
    }
    if (v < 0) { is_neg = 1; v = -v; }
    while (v > 0) {
        rev[i++] = '0' + (v % 16);
        v /= 10;
    }
    if (is_neg) rev[i++] = '-';
    int j;
    for (j = 0; j < i; j++) calc_display[j] = rev[i - 1 - j];
    calc_display[i] = 0;
}

static rect_t calc_btn_rect(app_window_t *app, int row, int col) {
    int bw = 40, bh = 40;
    int gap = 8;
    int cx = app->frame.x + 10;
    int cy = app->frame.y + 42 + 60; // below display
    return (rect_t){cx + col * (bw + gap), cy + row * (bh + gap), bw, bh, 0};
}

static void draw_calculator_contents(app_window_t *app) {
    int cx = app->frame.x + 10;
    int cy = app->frame.y + 42;
    int cw = app->frame.w - 20;
    int ch = app->frame.h - 52;
    int r, c;
    const char *btns[4][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"C", "0", "=", "+"}
    };
    
    fb_rect(cx, cy, cw, ch, 0xF0F4F8);
    outline(cx, cy, cw, ch, 0xC4D0DE);
    
    // Display
    fb_rect(cx + 8, cy + 8, cw - 16, 44, 0xFFFFFF);
    outline(cx + 8, cy + 8, cw - 16, 44, 0xA8B2BC);
    
    // Text right-aligned
    int len = 0; while (calc_display[len]) len++;
    font_text(cx + cw - 16 - len * 8, cy + 22, calc_display, 0x2A3C4D, 1);
    
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            rect_t btn = calc_btn_rect(app, r, c);
            int is_op = (c == 3 || (r==3 && (c==0 || c==2)));
            draw_button(btn, is_op ? 0xDEE6ED : 0xFFFFFF, is_op ? 0xEFF3F6 : 0xFFFFFF, btns[r][c], is_op ? 0x2C4662 : 0x2A3C4D);
        }
    }
}

static void handle_media_player_click(int mx, int my) {
    app_window_t *app = app_slot(APP_MEDIA_PLAYER);
    if (!app) return;
    int cx = app->frame.x + 10;
    int cy = app->frame.y + 42;
    int cw = app->frame.w - 20;
    int ch = app->frame.h - 52;
    int art_w = 128, art_h = 128;
    int art_y = cy + 20;
    int ctrl_y = art_y + art_h + 80;
    
    // Play/Pause
    if (point_in_rect(mx, my, (rect_t){cx + cw/2 - 30, ctrl_y, 60, 30})) {
        media_pause();
        extern int needs_redraw; needs_redraw = 1;
    }
    // Skip Back
    if (point_in_rect(mx, my, (rect_t){cx + cw/2 - 70, ctrl_y, 30, 30})) {
        media_seek(-100);
        extern int needs_redraw; needs_redraw = 1;
    }
    // Skip Forward
    if (point_in_rect(mx, my, (rect_t){cx + cw/2 + 40, ctrl_y, 30, 30})) {
        media_seek(100);
        extern int needs_redraw; needs_redraw = 1;
    }
    // Volume
    rect_t vol_rect = (rect_t){cx + cw/2 - 10, ctrl_y + 50, 80, 10};
    if (point_in_rect(mx, my, vol_rect)) {
        int percent = ((mx - vol_rect.x) * 100) / vol_rect.w;
        active_media.volume = percent;
        extern int needs_redraw; needs_redraw = 1;
    }
}
static void draw_media_player_contents(app_window_t *app) {
    int cx = app->frame.x + 10;
    int cy = app->frame.y + 42;
    int cw = app->frame.w - 20;
    int ch = app->frame.h - 52;
    int art_w = 128, art_h = 128;
    int art_x = cx + (cw - art_w) / 2;
    int art_y = cy + 20;
    
    fb_rect(cx, cy, cw, ch, 0xF8F9FA);
    outline(cx, cy, cw, ch, 0xC4D0DE);
    
    // Draw album art box
    fb_rect(art_x, art_y, art_w, art_h, 0xE9ECEF);
    outline(art_x, art_y, art_w, art_h, 0xDEE2E6);
    
    // Draw big icon
    if (!audiowav_ico_loaded) { audiowav_ico_loaded = 1; load_ico_from_path("/icons/audio_wav.ico", audiowav_ico_pixels, &audiowav_ico_width, &audiowav_ico_height); }
    if (audiowav_ico_width > 0) {
        draw_icon_scaled(audiowav_ico_pixels, audiowav_ico_width, audiowav_ico_height, art_x + (art_w - 64)/2, art_y + (art_h - 64)/2, 64, 64);
    } else {
        font_icon(art_x + (art_w - 16)/2, art_y + (art_h - 16)/2, ICON_FILE, 0xADB5BD);
    }
    
    // Draw File name
    if (media_player_current_file[0]) {
        font_text_fit_center(cx, art_y + art_h + 20, media_player_current_file, 0x212529, cw);
        font_text_fit_center(cx, art_y + art_h + 40, active_media.state == STATE_PLAYING ? "Playing" : (active_media.state == STATE_PAUSED ? "Paused" : "Stopped"), 0x6C757D, cw);
    } else {
        font_text_fit_center(cx, art_y + art_h + 20, "No file selected", 0x6C757D, cw);
        font_text_fit_center(cx, art_y + art_h + 40, "Open a .wav file from Explorer", 0xADB5BD, cw);
    }
    
    // Draw Controls
    int ctrl_y = art_y + art_h + 80;
    draw_button((rect_t){cx + cw/2 - 70, ctrl_y, 30, 30}, 0xE7ECF3, 0xD0D7E2, "|<", 0x20344E); // Skip Back
    draw_button((rect_t){cx + cw/2 - 30, ctrl_y, 60, 30}, 0xE7ECF3, 0xD0D7E2, active_media.state == STATE_PLAYING ? " || " : " |> ", 0x20344E); // Play/Pause
    draw_button((rect_t){cx + cw/2 + 40, ctrl_y, 30, 30}, 0xE7ECF3, 0xD0D7E2, ">|", 0x20344E); // Skip Forward

    // Draw Volume
    draw_label(cx + cw/2 - 50, ctrl_y + 48, "Vol:", 0x555555);
    draw_progress_bar((rect_t){cx + cw/2 - 10, ctrl_y + 50, 80, 10}, active_media.volume, 0, 100);
}

static void draw_preview_contents(app_window_t *app) {
    int cx = app->frame.x + 10;
    int cy = app->frame.y + 42;
    int cw = app->frame.w - 20;
    int ch = app->frame.h - 52;

    fb_rect(cx, cy, cw, ch, 0xF8F9FA);
    outline(cx, cy, cw, ch, 0xC4D0DE);

    if (preview_image_data && preview_image_w > 0 && preview_image_h > 0) {
        // Calculate scaled dimensions keeping aspect ratio (fixed point math)
        int scale = 1024;
        if (preview_image_w > cw - 20 || preview_image_h > ch - 50) {
            int scale_x = ((cw - 20) * 1024) / preview_image_w;
            int scale_y = ((ch - 50) * 1024) / preview_image_h;
            scale = scale_x < scale_y ? scale_x : scale_y;
        }
        int dw = (preview_image_w * scale) / 1024;
        int dh = (preview_image_h * scale) / 1024;
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        int dx = cx + (cw - dw) / 2;
        int dy = cy + (ch - dh - 30) / 2;

        int ext = 0;
        for (int i=0; preview_image_path[i]; i++) if (preview_image_path[i] == '.') ext = i + 1;
        int is_raw = (ext > 0 && preview_image_path[ext] == 'r' && preview_image_path[ext+1] == 'a' && preview_image_path[ext+2] == 'w');
        
        for (int y = 0; y < dh; y++) {
            int src_y = (y * preview_image_h) / dh;
            u32 *src_row = preview_image_data + (src_y * preview_image_w);
            for (int x = 0; x < dw; x++) {
                int src_x = (x * preview_image_w) / dw;
                extern void fb_putpixel(int x, int y, unsigned int color);
                u32 c = src_row[src_x];
                if (!is_raw) {
                    c = (c & 0xFF00FF00) | ((c & 0x00FF0000) >> 16) | ((c & 0x000000FF) << 16);
                }
                fb_putpixel(dx + x, dy + y, c);
            }
        }
        font_text_fit_center(cx, cy + ch - 25, preview_image_path, 0x212529, cw);
    } else {
        font_text_fit_center(cx, cy + ch / 2 - 10, "Failed to load image", 0xFF0000, cw);
    }
}

static char cached_thumbnail_path[128] = "";
static u32 *cached_thumbnail_data = 0;
static int cached_thumbnail_w = 0;
static int cached_thumbnail_h = 0;

static void update_explorer_thumbnail(const char *path, const char *ext) {
    if (cached_thumbnail_data) { extern void kfree(void*); kfree(cached_thumbnail_data); cached_thumbnail_data = 0; }
    cached_thumbnail_w = 0;
    cached_thumbnail_h = 0;
    
    int is_raw = (ext[0] == 'r' && ext[1] == 'a' && ext[2] == 'w');
    int w = 0, h = 0;
    u32 *full_img = 0;

    if (is_raw) {
        full_img = (u32*)kmalloc(1024*768*4);
        if (full_img) {
            extern int fs_read_raw_at(const char*, char*, unsigned int);
            if (fs_read_raw_at(path, (char*)full_img, 1024*768*4) != 1024*768*4) {
                extern void kfree(void*); kfree(full_img); full_img = 0;
            } else {
                w = 1024; h = 768;
            }
        }
    } else {
        extern u8 *image_load(const char*, int*, int*);
        full_img = (u32*)image_load(path, &w, &h);
        if (full_img) {
            int cnt = w * h;
            for (int idx=0; idx<cnt; idx++) {
                u32 p = full_img[idx];
                full_img[idx] = (p & 0xFF00FF00) | ((p & 0xFF) << 16) | ((p >> 16) & 0xFF);
            }
        }
    }

    if (full_img) {
        // Thumbnail target max size 100x70
        // Use fixed point / integer math to avoid FPU
        int w_scale = (100 * 1024) / w;
        int h_scale = (70 * 1024) / h;
        int scale = w_scale < h_scale ? w_scale : h_scale;
        if (scale > 1024) scale = 1024;
        
        cached_thumbnail_w = (w * scale) / 1024;
        cached_thumbnail_h = (h * scale) / 1024;
        if (cached_thumbnail_w < 1) cached_thumbnail_w = 1;
        if (cached_thumbnail_h < 1) cached_thumbnail_h = 1;
        
        cached_thumbnail_data = (u32*)kmalloc(cached_thumbnail_w * cached_thumbnail_h * 4);
        
        if (cached_thumbnail_data) {
            for (int y = 0; y < cached_thumbnail_h; y++) {
                int src_y = (y * h) / cached_thumbnail_h;
                for (int x = 0; x < cached_thumbnail_w; x++) {
                    int src_x = (x * w) / cached_thumbnail_w;
                    cached_thumbnail_data[y * cached_thumbnail_w + x] = full_img[src_y * w + src_x];
                }
            }
        }
        extern void kfree(void*);
        kfree(full_img);
    }
}

static void text_preview_update(const char *path) {
    if (!path) { text_preview_path[0] = 0; text_preview_buf[0] = 0; return; }
    int same = 1, i = 0;
    while (path[i] && text_preview_path[i]) {
        if (path[i] != text_preview_path[i]) { same = 0; break; }
        i++;
    }
    if (path[i] != text_preview_path[i]) same = 0;
    if (!same) {
        int j = 0;
        while (path[j] && j < 127) { text_preview_path[j] = path[j]; j++; }
        text_preview_path[j] = 0;
        int bytes = fs_read_at(path, text_preview_buf, 1023);
        if (bytes >= 0) text_preview_buf[bytes] = 0;
        else text_preview_buf[0] = 0;
    }
}

static void explorer_open_file_default(const char *path, const char *name) {
    if (!path || !name) return;
    int ext = 0, j = 0;
    while (name[j]) { if (name[j] == '.') ext = j + 1; j++; }

    if (explorer_is_audio_file(name) || explorer_is_video_file(name)) {
        media_player_open_file(path);
        open_app(APP_MEDIA_PLAYER);
        focus_app_window(APP_MEDIA_PLAYER - 1);
    } else if (explorer_is_image_file(name)) {
        preview_open(path);
        open_app(APP_PREVIEW);
        focus_app_window(APP_PREVIEW - 1);
    } else if (ext > 0 && name[ext] == 'f' && name[ext+1] == 'l') {
        extern void ide_open_in_new_tab(const char*);
        ide_open_in_new_tab(path);
        open_app(APP_IDE);
        focus_app_window(APP_IDE - 1);
    } else if (ext > 0 && name[ext] == 'a' && name[ext+1] == 'p' && name[ext+2] == 'p') {
        if (gui_str_contains_ci(name, "browser")) {
            web_browser_app_launch(0);
            return;
        }
        extern int explorer_is_virtual_path(const char*);
        if (explorer_is_virtual_path(path)) {
            if (name[0] == 'S') open_app(APP_SETTINGS);
            else if (name[0] == 'E') open_app(APP_EXPLORER);
            else if (name[0] == 'T' && name[1] == 'e' && name[2] == 'r') open_app(APP_TERMINAL);
            else if (name[0] == 'T' && name[1] == 'a' && name[2] == 's') open_app(APP_TASKMAN);
            else if (name[0] == 'T') open_app(APP_TEXT_EDIT);
        } else {
            char out[1024]; out[0] = 0;
            char src[4096];
            int bytes = fs_read_at(path, src, 4095);
            if (bytes >= 0) src[bytes] = 0; else src[0] = 0;
            extern int firstlang_run(const char*, char*, int);
            firstlang_run(src, out, 1024);
            extern int firstlang_has_ui(void);
            if (!firstlang_has_ui() && out[0]) {
                open_app(APP_TERMINAL);
                extern void terminal_print(const char *text);
                terminal_print(out);
                terminal_print("\n");
                focus_app_window(APP_TERMINAL - 1);
            }
        }
    } else if (ext > 0 && (name[ext] == 't' || name[ext] == 'T') && (name[ext+1] == 'm' || name[ext+1] == 'M') && (name[ext+2] == 'l' || name[ext+2] == 'L')) {
        open_app(APP_TERMINAL);
        focus_app_window(APP_TERMINAL - 1);
        extern void terminal_run_script(const char *path);
        terminal_run_script(path);
    } else {
        extern void text_edit_open_file(const char*);
        text_edit_open_file(path);
        open_app(APP_TEXT_EDIT);
        focus_app_window(APP_TEXT_EDIT - 1);
    }
}

static void draw_explorer_contents(app_window_t *app) {
    explorer_anim_ticks++;
    if (!app->current_path[0]) {
        gui_fl_cpy(app->current_path, "FirstOS", sizeof(app->current_path));
    }
    explorer_open_path(app->current_path);
    explorer_path = explorer_current_path();
    int disk_count = explorer_disk_count();
    int entry_count = explorer_entry_count();
    int filtered_count = explorer_filtered_count();
    int i = 0;
    int content_x = app->frame.x + 10;
    int content_y = app->frame.y + 42;
    int content_w = app->frame.w - 20;
    int content_h = app->frame.h - 52;
    int tab_bar_h = 24;
    draw_window_tab_bar(app, content_x, content_y, content_w);
    sync_explorer_active_tab(app);
    content_y += tab_bar_h;
    content_h -= tab_bar_h;
    int header_h = 34;
    int toolbar_h = 34;
    int sidebar_w = 134;
    int status_h = 24;
    int preview_pane_w = (explorer_preview_pane_open && content_w >= 440) ? 170 : 0;
    int main_x = content_x + sidebar_w + 1;
    int main_w = content_w - sidebar_w - preview_pane_w - (preview_pane_w ? 2 : 1);
    int preview_pane_x = main_x + main_w + 1;
    int preview_pane_y = content_y + header_h + toolbar_h;
    int preview_pane_h = content_h - header_h - toolbar_h - status_h;
    int grid_y = content_y + header_h + toolbar_h + 14;
    int grid_h = content_h - header_h - toolbar_h - status_h - 18;
    int card_w = main_w < 250 ? 82 : 96;
    int cell_h = 86;
    int icon_cols = (main_w - 24) / card_w;
    int visible_rows;
    int visible_items;
    int save_bar_h = editor_save_dialog_open ? 36 : 0;
    int nav_btn_y = content_y + 8;
    int path_x = content_x + 86;
    int path_w = content_w - 222;

    if (icon_cols < 1) icon_cols = 1;
    visible_rows = (grid_h - save_bar_h) / cell_h;
    if (visible_rows < 1) visible_rows = 1;
    visible_items = visible_rows * icon_cols;
    if (explorer_scroll > entry_count - visible_items) explorer_scroll = entry_count - visible_items;
    if (explorer_scroll < 0) explorer_scroll = 0;

    fb_rect(content_x, content_y, content_w, content_h, 0xD8DDD9);
    outline(content_x, content_y, content_w, content_h, 0x76806D);

    fb_rect(content_x, content_y, content_w, header_h, 0xE7ECE7);
    fb_rect(content_x, content_y + header_h, content_w, toolbar_h, 0xF3F6F3);
    
    int preview_btn_w = 60;
    int preview_btn_x = content_x + content_w - preview_btn_w - 8;
    int preview_btn_y = content_y + header_h + 6;
    int search_w = 140;
    int search_x = preview_btn_x - search_w - 8;
    explorer_preview_toggle_btn = (rect_t){preview_btn_x, preview_btn_y, preview_btn_w, 20, 0};
    draw_button(explorer_preview_toggle_btn,
                explorer_preview_pane_open ? 0x3E6B99 : 0xD5DDD5,
                explorer_preview_pane_open ? 0x5D8EC2 : 0xEFF4EF,
                "Preview",
                explorer_preview_pane_open ? 0xFFFFFF : 0x2A3A2A);
   fb_rect(content_x, content_y + header_h + toolbar_h, sidebar_w, content_h - header_h - toolbar_h - status_h, 0xEEF3EF);
    fb_rect(main_x, content_y + header_h + toolbar_h, main_w, content_h - header_h - toolbar_h - status_h, 0xFAFCFA);
    fb_rect(content_x, content_y + content_h - status_h, content_w, status_h, 0xE7ECE8);

    fb_rect(content_x + 8, nav_btn_y, 18, 18, 0xD2D9D2);
    fb_rect(content_x + 30, nav_btn_y, 18, 18, 0xD2D9D2);
    fb_rect(content_x + 52, nav_btn_y, 18, 18, 0xD2D9D2);
    outline(content_x + 8, nav_btn_y, 18, 18, 0x97A18F);
    outline(content_x + 30, nav_btn_y, 18, 18, 0x97A18F);
    outline(content_x + 52, nav_btn_y, 18, 18, 0x97A18F);
    draw_label(content_x + 14, nav_btn_y + 5, "<", 0x475345);
    draw_label(content_x + 36, nav_btn_y + 5, ">", 0x475345);
    draw_label(content_x + 58, nav_btn_y + 5, "^", 0x475345);
    fb_rect(path_x, content_y + 7, path_w, 20, 0xFBFCFB);
    outline(path_x, content_y + 7, path_w, 20, 0xA4AEA1);
    font_text_fit(path_x + 8, content_y + 12, app->current_path, 0x445244, path_w - 16);
    explorer_new_file_button = (rect_t){content_x + content_w - 118, content_y + 7, 30, 20, 0};
    draw_button(explorer_new_file_button, 0x81A876, 0xA7C79B, "+", 0xFFFFFF);
    draw_button((rect_t){content_x + content_w - 82, content_y + 7, 30, 20, 0}, 0xCBD4DE, 0xF2F5F8, "-", 0x465360);
    draw_button((rect_t){content_x + content_w - 46, content_y + 7, 30, 20, 0}, 0xD88A8A, 0xE9B1B1, "X", 0xFFFFFF);

    draw_label(content_x + 12, content_y + header_h + 10, "QUICK ACCESS", 0x576455);
    {
        const char *loc_title = "CURRENT FOLDER";
        if (explorer_picker_mode == 1) {
            loc_title = (editor_save_dialog_open && !ide_save_dialog_open) ? "SAVE TEXT DOCUMENT (.txt, .doc)" : "SAVE LOCATION";
        } else if (explorer_picker_mode == 2) {
            loc_title = (editor_save_dialog_open && !ide_save_dialog_open) ? "SELECT TEXT DOCUMENT (.txt, .doc)" : "OPEN LOCATION";
        } else if (explorer_picker_mode == 3) {
            loc_title = "SELECT MEDIA TO IMPORT";
        }
        draw_label(main_x + 12, content_y + header_h + 10, loc_title, 0x576455);
    }
    gui_text_input_set(explorer_search_text, (int)sizeof(explorer_search_text), &explorer_search_cursor, &explorer_search_focus);
    draw_text_input((rect_t){search_x, content_y + header_h + 6, search_w, 20, 0}, explorer_search_text, explorer_search_cursor, explorer_search_focus, "Search files");
    explorer_search_clear_button = (rect_t){search_x + search_w - 18, content_y + header_h + 9, 14, 14, 0};
    if (explorer_search_text[0]) {
        fb_round_rect(explorer_search_clear_button.x, explorer_search_clear_button.y, explorer_search_clear_button.w, explorer_search_clear_button.h, 7, explorer_search_focus ? 0xD8E3EE : 0xE7ECE7);
        fb_round_rect_border(explorer_search_clear_button.x, explorer_search_clear_button.y, explorer_search_clear_button.w, explorer_search_clear_button.h, 7, 0x98A39A);
        draw_label(explorer_search_clear_button.x + 4, explorer_search_clear_button.y + 3, "X", 0x4D5A4B);
    } else {
        explorer_search_clear_button = (rect_t){0, 0, 0, 0, 0};
    }
    explorer_search_history_rect = (rect_t){search_x, content_y + header_h + 28, search_w, explorer_search_history_open && explorer_search_history_count > 0 ? explorer_search_history_count * 18 + 6 : 0, 0};
    if (explorer_search_history_rect.h > 0) {
        int h;
        fb_rect(explorer_search_history_rect.x, explorer_search_history_rect.y, explorer_search_history_rect.w, explorer_search_history_rect.h, 0xF5F8F4);
        outline(explorer_search_history_rect.x, explorer_search_history_rect.y, explorer_search_history_rect.w, explorer_search_history_rect.h, 0xB7C0B5);
        for (h = 0; h < explorer_search_history_count; h++) {
            int row_y = explorer_search_history_rect.y + 4 + h * 18;
            if (h == explorer_search_history_active) {
                fb_rect(explorer_search_history_rect.x + 2, row_y, explorer_search_history_rect.w - 4, 16, 0xDCE8F6);
                outline(explorer_search_history_rect.x + 2, row_y, explorer_search_history_rect.w - 4, 16, 0x97AECA);
            }
            font_text_fit(explorer_search_history_rect.x + 8, row_y + 4, explorer_search_history[h], h == explorer_search_history_active ? 0x243B5A : 0x465445, explorer_search_history_rect.w - 16);
        }
    }

    fb_rect(content_x + 8, content_y + header_h + toolbar_h + 10, sidebar_w - 16, 24, 0xDCE7DD);
    draw_label(content_x + 16, content_y + header_h + toolbar_h + 18, "Desktop", 0x304530);
    fb_rect(content_x + 8, content_y + header_h + toolbar_h + 36, sidebar_w - 16, 24, 0xDCE7DD);
    draw_label(content_x + 16, content_y + header_h + toolbar_h + 44, "Wallpapers", 0x304530);
    fb_rect(content_x + 8, content_y + header_h + toolbar_h + 62, sidebar_w - 16, 24, 0xDCE7DD);
    draw_label(content_x + 16, content_y + header_h + toolbar_h + 70, "Audio", 0x304530);
    for (i = 0; i < disk_count && i < 5; i++) {
        const explorer_disk_t *disk = explorer_disk_at(i);
        int row_y = content_y + header_h + toolbar_h + 96 + i * 22;
        if (!disk || row_y + 14 > content_y + content_h - status_h - 6) continue;
        if (i == 0) fb_rect(content_x + 8, row_y - 4, sidebar_w - 16, 18, 0xD7E3F5);
        font_icon(content_x + 14, row_y - 1, ICON_DISK, disk->present ? 0x4D7A95 : 0x7A857B);
        font_text_fit(content_x + 34, row_y + 2, disk->label, 0x495748, sidebar_w - 44);
    }


        // --- PREVIEW INSPECTOR PANE ---
    explorer_preview_play_btn = (rect_t){0, 0, 0, 0, 0};
    explorer_btn_open_app = (rect_t){0, 0, 0, 0, 0};
    if (preview_pane_w > 0) {
        fb_rect(preview_pane_x, preview_pane_y, preview_pane_w, preview_pane_h, 0xEEF3EF);
        outline(preview_pane_x, preview_pane_y, preview_pane_w, preview_pane_h, 0xBAC6BA);
        draw_label(preview_pane_x + 10, preview_pane_y + 8, "INSPECTOR", 0x4D614D);
        fb_rect(preview_pane_x + 8, preview_pane_y + 24, preview_pane_w - 16, 1, 0xD0DCD0);

        if (explorer_selected >= 0 && explorer_selected < entry_count) {
            const explorer_entry_t *e = explorer_entry_at(explorer_selected);
            if (e && !e->is_dir) {
                int py = preview_pane_y + 32;
                int ext = 0, j = 0;
                while (e->name[j]) { if (e->name[j] == '.') ext = j + 1; j++; }

                if (explorer_is_image_file(e->name)) {
                    int path_changed = 0, k = 0;
                    while (e->path[k] && cached_thumbnail_path[k] && e->path[k] == cached_thumbnail_path[k]) k++;
                    if (e->path[k] != cached_thumbnail_path[k]) path_changed = 1;
                    if (path_changed) {
                        k = 0;
                        while (e->path[k] && k < 127) { cached_thumbnail_path[k] = e->path[k]; k++; }
                        cached_thumbnail_path[k] = 0;
                        if (ext > 0) update_explorer_thumbnail(e->path, &e->name[ext]);
                    }

                    int thumb_box_h = 80;
                    fb_rect(preview_pane_x + 8, py, preview_pane_w - 16, thumb_box_h, 0x1A2026);
                    outline(preview_pane_x + 8, py, preview_pane_w - 16, thumb_box_h, 0x3D4C5D);

                    if (cached_thumbnail_data && cached_thumbnail_w > 0 && cached_thumbnail_h > 0) {
                        int dx = preview_pane_x + 8 + (preview_pane_w - 16 - cached_thumbnail_w) / 2;
                        int dy = py + (thumb_box_h - cached_thumbnail_h) / 2;
                        for (int ty = 0; ty < cached_thumbnail_h; ty++) {
                            u32 *src_row = cached_thumbnail_data + (ty * cached_thumbnail_w);
                            for (int tx = 0; tx < cached_thumbnail_w; tx++) {
                                u32 c = src_row[tx];
                                fb_putpixel(dx + tx, dy + ty, c);
                            }
                        }
                    } else {
                        font_icon(preview_pane_x + (preview_pane_w - 20) / 2, py + 25, ICON_DISPLAY, 0x5C80A6);
                    }

                    font_text_fit(preview_pane_x + 10, py + thumb_box_h + 8, e->name, 0x243624, preview_pane_w - 20);
                    draw_label(preview_pane_x + 10, py + thumb_box_h + 24, "Image File", 0x667766);

                    explorer_btn_open_app = (rect_t){preview_pane_x + 10, py + thumb_box_h + 44, preview_pane_w - 20, 22, 0};
                    draw_button(explorer_btn_open_app, 0x4A6FA5, 0x6B92CB, "Open Preview", 0xFFFFFF);

                } else if (explorer_is_video_file(e->name)) {
                    int cinema_h = 75;
                    fb_rect(preview_pane_x + 8, py, preview_pane_w - 16, cinema_h, 0x141A20);
                    outline(preview_pane_x + 8, py, preview_pane_w - 16, cinema_h, 0x334455);
                    for (int fx = preview_pane_x + 12; fx < preview_pane_x + preview_pane_w - 16; fx += 14) {
                        fb_rect(fx, py + 3, 6, 3, 0x556677);
                        fb_rect(fx, py + cinema_h - 6, 6, 3, 0x556677);
                    }
                    int cx = preview_pane_x + preview_pane_w / 2;
                    int cy = py + cinema_h / 2;
                    fb_rect(cx - 14, cy - 14, 28, 28, 0x223344);
                    outline(cx - 14, cy - 14, 28, 28, 0x4D6888);
                    draw_label(cx - 3, cy - 5, ">", 0x55DDFF);

                    font_text_fit(preview_pane_x + 10, py + cinema_h + 8, e->name, 0x243624, preview_pane_w - 20);
                    draw_label(preview_pane_x + 10, py + cinema_h + 24, "AVI/MP4 Video", 0x667766);
                    draw_label(preview_pane_x + 10, py + cinema_h + 38, "320x240 @ 24fps", 0x778877);

                    explorer_btn_open_app = (rect_t){preview_pane_x + 10, py + cinema_h + 56, preview_pane_w - 20, 22, 0};
                    draw_button(explorer_btn_open_app, 0x2E7D32, 0x4CAF50, "Play Video", 0xFFFFFF);

                } else if (explorer_is_audio_file(e->name)) {
                    int wave_h = 65;
                    fb_rect(preview_pane_x + 8, py, preview_pane_w - 16, wave_h, 0x141E28);
                    outline(preview_pane_x + 8, py, preview_pane_w - 16, wave_h, 0x2A4058);

                    int is_play = hda_is_playing();
                    int num_b = 14;
                    int bw = (preview_pane_w - 28) / num_b;
                    if (bw < 3) bw = 3;
                    int base_y = py + wave_h - 8;
                    for (int b = 0; b < num_b; b++) {
                        int bh = is_play ? (6 + ((explorer_anim_ticks * 4 + b * 13) % 45)) : (6 + ((b * 19) % 25));
                        u32 bcol = is_play ? (bh > 35 ? 0xFFAA33 : 0x44DD88) : 0x406080;
                        fb_rect(preview_pane_x + 14 + b * bw, base_y - bh, bw - 2, bh, bcol);
                    }

                    font_text_fit(preview_pane_x + 10, py + wave_h + 6, e->name, 0x243624, preview_pane_w - 20);
                    draw_label(preview_pane_x + 10, py + wave_h + 20, "PCM Audio 44.1kHz", 0x667766);

                    int btn_w = (preview_pane_w - 26) / 2;
                    explorer_preview_play_btn = (rect_t){preview_pane_x + 10, py + wave_h + 36, btn_w, 22, 0};
                    draw_button(explorer_preview_play_btn, is_play ? 0xB54A3B : 0x388E3C, is_play ? 0xDD6454 : 0x54AC58, is_play ? "Pause" : "Play", 0xFFFFFF);

                    explorer_btn_open_app = (rect_t){preview_pane_x + 10 + btn_w + 6, py + wave_h + 36, btn_w, 22, 0};
                    draw_button(explorer_btn_open_app, 0x4A6FA5, 0x6B92CB, "Player", 0xFFFFFF);

                    if (is_play) draw_label(preview_pane_x + 12, py + wave_h + 62, "Playing live...", 0x2E7D32);
                    else if (hda_is_paused()) draw_label(preview_pane_x + 12, py + wave_h + 62, "Paused", 0xB8860B);

                } else {
                    text_preview_update(e->path);
                    int text_box_h = 75;
                    fb_rect(preview_pane_x + 8, py, preview_pane_w - 16, text_box_h, 0x1A2026);
                    outline(preview_pane_x + 8, py, preview_pane_w - 16, text_box_h, 0x3B4A5C);
                    fb_rect(preview_pane_x + 8, py, 16, text_box_h, 0x14181E);

                    int line_num = 1;
                    int text_i = 0;
                    int ty = py + 4;
                    while (text_preview_buf[text_i] && ty < py + text_box_h - 10 && line_num <= 5) {
                        char num_str[4];
                        num_str[0] = '0' + line_num;
                        num_str[1] = ':';
                        num_str[2] = 0;
                        draw_label(preview_pane_x + 10, ty, num_str, 0x6A7D90);

                        char line_buf[32];
                        int li = 0;
                        while (text_preview_buf[text_i] && text_preview_buf[text_i] != '\n' && li < 28) {
                            if (text_preview_buf[text_i] != '\r') line_buf[li++] = text_preview_buf[text_i];
                            text_i++;
                        }
                        if (text_preview_buf[text_i] == '\n') text_i++;
                        line_buf[li] = 0;
                        font_text_fit(preview_pane_x + 28, ty, line_buf, 0xCCE0F0, preview_pane_w - 38);
                        ty += 13;
                        line_num++;
                    }

                    font_text_fit(preview_pane_x + 10, py + text_box_h + 6, e->name, 0x243624, preview_pane_w - 20);
                    draw_label(preview_pane_x + 10, py + text_box_h + 20, "Text / Source", 0x667766);

                    explorer_btn_open_app = (rect_t){preview_pane_x + 10, py + text_box_h + 38, preview_pane_w - 20, 22, 0};
                    draw_button(explorer_btn_open_app, 0x7B5E3C, 0xA68258, "Edit in TextEdit", 0xFFFFFF);
                }
            } else if (e && e->is_dir) {
                int py = preview_pane_y + 40;
                font_icon(preview_pane_x + (preview_pane_w - 24) / 2, py, ICON_EXPLORER, 0xBC8F2C);
                font_text_fit(preview_pane_x + 10, py + 34, e->name, 0x243624, preview_pane_w - 20);
                draw_label(preview_pane_x + 10, py + 52, "Folder", 0x667766);
                draw_label(preview_pane_x + 10, py + 68, "Double click to open", 0x889988);
            }
        } else {
            int py = preview_pane_y + 40;
            draw_label(preview_pane_x + 14, py, "No item selected", 0x667766);
            draw_label(preview_pane_x + 14, py + 20, "Select a file to", 0x889988);
            draw_label(preview_pane_x + 14, py + 36, "preview details.", 0x889988);
            draw_label(preview_pane_x + 14, py + 66, "Press Spacebar", 0x4A6FA5);
            draw_label(preview_pane_x + 14, py + 82, "for Quick Look.", 0x4A6FA5);
        }
    }
    // -----------------------
    if (editor_save_dialog_open) {
        rect_t save_input = {main_x + 84, content_y + header_h + toolbar_h + 8, main_w - 180, 18, 0};
        fb_rect(main_x + 10, content_y + header_h + toolbar_h + 4, main_w - 20, 28, 0xEBF1EA);
        draw_label(main_x + 18, content_y + header_h + toolbar_h + 12, explorer_picker_mode == 2 ? "Open:" : (explorer_picker_mode == 3 ? "Import:" : "Save As:"), 0x4B5968);
        gui_text_input_set(editor_save_name, (int)sizeof(editor_save_name), &editor_save_name_cursor, &editor_save_name_focus);
        draw_text_input(save_input, editor_save_name, editor_save_name_cursor, editor_save_name_focus, (editor_save_dialog_open && !ide_save_dialog_open) ? "document.txt (.txt, .doc)" : "notes.txt");
        draw_button((rect_t){main_x + main_w - 82, content_y + header_h + toolbar_h + 8, 60, 18, 0}, 0x7BA2C7, 0xA8C5E1, explorer_picker_mode == 2 ? "OPEN" : (explorer_picker_mode == 3 ? "IMPORT" : "SAVE"), 0xFFFFFF);
    }

    if (entry_count <= 0) {
        fb_rect(main_x + 18, grid_y + 12, main_w - 36, 56, 0xF1F5F0);
        outline(main_x + 18, grid_y + 12, main_w - 36, 56, 0xC4CEC1);
        draw_label(main_x + 28, grid_y + 24, "THIS FOLDER IS EMPTY", 0x60715E);
        draw_label(main_x + 28, grid_y + 40, "CREATE A FILE OR OPEN ANOTHER LOCATION", 0x7A8879);
    } else {
        for (i = 0; i < visible_items && explorer_scroll + i < filtered_count; i++) {
            const explorer_entry_t *entry = explorer_entry_at(explorer_filtered_index_from_visible(explorer_scroll + i));
            const char *display_name = entry ? entry->name : 0;
            int actual_index = explorer_scroll + i;
            int col = i % icon_cols;
            int row = i / icon_cols;
            int tile_x = main_x + 12 + col * card_w;
            int tile_y = grid_y + row * cell_h;
            int selected = explorer_is_selected(explorer_filtered_index_from_visible(actual_index));
            int is_drop_target = actual_index == explorer_drop_target_index && explorer_dragging && explorer_drag_moved;
            int ext = 0;
            if (entry) {
                int k = 0; while (entry->name[k]) { if (entry->name[k] == '.') ext = k + 1; k++; }
            }
            int icon_kind = entry ? entry->cached_icon : ICON_FILE;
            int icon_color = entry && entry->is_dir ? 0xBC8F2C : 0x5E7FA8;
            if (entry && entry->is_dir && entry->folder_color != 0) {
                icon_color = entry->folder_color;
            } else if (entry && !entry->is_dir && ext > 0) {
                if (entry->name[ext] == 'a' && entry->name[ext+1] == 'p' && entry->name[ext+2] == 'p') {
                    icon_kind = ICON_DISPLAY; icon_color = 0x5A4D86;
                } else if (entry->name[ext] == 'f' && entry->name[ext+1] == 'l') {
                    icon_kind = ICON_FILE; icon_color = 0x1F4730;
                } else if (entry->name[ext] == 'j' && entry->name[ext+1] == 'p' && entry->name[ext+2] == 'g') {
                    icon_color = 0x4D7A95;
                }
            }
            if (!entry) continue;
            int match_start = explorer_entry_match_start(entry);
            int match_len = 0;
            int name_len = 0;
            int text_x = tile_x + 6;
            fb_rect(tile_x, tile_y, card_w - 8, 72, is_drop_target ? 0xE3F1D8 : (selected ? 0xDDE8F6 : 0xF3F6F2));
            outline(tile_x, tile_y, card_w - 8, 72, is_drop_target ? 0x78A35D : (selected ? 0x7E9CBD : 0xD4DCD3));
            if (selected) fb_rect(tile_x + 1, tile_y + 1, card_w - 10, 6, 0x7FA2C9);
            if (is_drop_target) fb_rect(tile_x + 1, tile_y + 65, card_w - 10, 4, 0x89B56B);
            if (match_start >= 0) fb_rect(tile_x + 2, tile_y + 2, card_w - 12, 10, selected ? 0x98B7DA : 0xD9E8AE);
            {
                int n_len = 0; while (entry->name && entry->name[n_len]) n_len++;
                int is_wav = (n_len > 4 && entry->name[n_len-3] == 'w' && entry->name[n_len-2] == 'a' && entry->name[n_len-1] == 'v');
                if (is_wav) {
                    if (!audiowav_ico_loaded) { audiowav_ico_loaded = 1; load_ico_from_path("/icons/audio_wav.ico", audiowav_ico_pixels, &audiowav_ico_width, &audiowav_ico_height); }
                    if (audiowav_ico_width > 0) draw_icon_scaled(audiowav_ico_pixels, audiowav_ico_width, audiowav_ico_height, tile_x + 18, tile_y + 10, 32, 32);
                    else {
                        fb_rect(tile_x + 18, tile_y + 14, 32, 24, 0xC6D8EA);
                        font_icon(tile_x + 26, tile_y + 17, icon_kind, icon_color);
                    }
                } else if (!entry->is_dir && (explorer_is_image_file(entry->name) || explorer_is_video_file(entry->name))) {
                    const file_thumbnail_t *thumb = thumbnail_get(entry->path, 28, 20);
                    if (thumb && thumb->pixels) {
                        draw_thumbnail_card(tile_x + 18, tile_y + 12, 32, 24, thumb, selected);
                    } else {
                        fb_rect(tile_x + 18, tile_y + 14, 32, 24, 0xC6D8EA);
                        font_icon(tile_x + 26, tile_y + 17, icon_kind, icon_color);
                    }
                } else {
                    u32 bg_col = entry->is_dir ? 0xE1BF6A : 0xC6D8EA;
                    if (entry->is_dir && entry->folder_color != 0) {
                        bg_col = (entry->folder_color & 0xFEFEFE) >> 1; // Quick mock darken
                    }
                    fb_rect(tile_x + 18, tile_y + 14, 32, 24, bg_col);
                    font_icon(tile_x + 26, tile_y + 17, icon_kind, icon_color);
                }
            }
            while (display_name && display_name[name_len]) name_len++;
            while (explorer_search_text[match_len]) match_len++;
            if (match_start > name_len) match_start = -1;
            if (match_start >= 0 && match_len > 0) {
                int prefix_w = match_start * 6;
                int highlight_w = match_len * 6;
                if (prefix_w > card_w - 20) prefix_w = card_w - 20;
                if (highlight_w > card_w - 20 - prefix_w) highlight_w = card_w - 20 - prefix_w;
                if (highlight_w > 0) fb_rect(text_x + prefix_w - 1, tile_y + 46, highlight_w + 2, 10, selected ? 0xC7DAEF : 0xE7E39B);
            }
            font_text_fit(text_x, tile_y + 48, display_name ? display_name : "item", selected ? 0x243B5A : 0x465445, card_w - 20);
            if (match_start >= 0) draw_label(tile_x + 8, tile_y + 60, "match", selected ? 0x4B6484 : 0x6B7B32);
            else if (entry->is_dir) draw_label(tile_x + 22, tile_y + 60, "folder", 0x7A836F);
            else draw_label(tile_x + 24, tile_y + 60, "file", 0x758091);
        }
    }

    if (entry_count > visible_items) {
        draw_scrollbar(main_x + main_w - 14, grid_y - 2, grid_h - save_bar_h + 8, entry_count, visible_items, explorer_scroll);
    }

    { draw_label(content_x + 12, content_y + content_h - 16, explorer_search_text[0] ? "Filtered results" : "Explorer ready", 0x526150); }
    explorer_preview_play_btn = (rect_t){0, 0, 0, 0, 0};
    if (explorer_selected >= 0 && explorer_selected < entry_count) {
        const explorer_entry_t *entry = explorer_entry_at(explorer_selected);
        if (entry) {
            font_text_fit(content_x + 150, content_y + content_h - 15, entry->name, 0x314230, content_w - 162);
        }
    } else {
        draw_label(content_x + 150, content_y + content_h - 16, "No item selected", 0x6A7868);
    }

    
    if (explorer_marquee_active) {
        int rx = explorer_marquee_x < explorer_marquee_mx ? explorer_marquee_x : explorer_marquee_mx;
        int ry = explorer_marquee_y < explorer_marquee_my ? explorer_marquee_y : explorer_marquee_my;
        int rw = explorer_marquee_x < explorer_marquee_mx ? explorer_marquee_mx - explorer_marquee_x : explorer_marquee_x - explorer_marquee_mx;
        int rh = explorer_marquee_y < explorer_marquee_my ? explorer_marquee_my - explorer_marquee_y : explorer_marquee_y - explorer_marquee_my;
        outline(rx, ry, rw, rh, 0x0078D7);
    }
    
    if (explorer_dragging && explorer_drag_moved && explorer_drag_index >= 0 && explorer_drag_index < entry_count) {
        const explorer_entry_t *drag_entry = explorer_entry_at(explorer_drag_index);
        int px = explorer_drag_preview_x + 12;
        int py = explorer_drag_preview_y + 12;
        int pw = 90;
        int ph = 32;
        if (drag_entry) {
            fb_rect(px + 2, py + 2, pw, ph, 0x8FA1B4);
            fb_rect(px, py, pw, ph, 0xEEF4FA);
            outline(px, py, pw, ph, 0x7B95AF);
            fb_rect(px + 8, py + 6, 18, 14, drag_entry->is_dir ? 0xE1BF6A : 0xC6D8EA);
            font_icon(px + 12, py + 8, drag_entry->is_dir ? ICON_FOLDER : ICON_FILE, drag_entry->is_dir ? 0xBC8F2C : 0x5E7FA8);
            font_text_fit(px + 30, py + 10, drag_entry->name, 0x314230, 50);
            if (explorer_drop_target_index >= 0) draw_label(px + 30, py + 20, "Drop to open", 0x5B7D44);
            else draw_label(px + 30, py + 20, "Dragging", 0x6B7886);
        }
    }
}



static void draw_taskman_window(app_window_t *app) {
    /* Delegate to taskman.c — full real Task Manager implementation */
    extern void draw_taskman_contents(app_window_t *app);
    draw_taskman_contents(app);
}


static void draw_app_window(app_window_t *app) {
    const char *label;
    const char *line;
    u32 shadow = 0x7C8B99;
    u32 system_font_color = active_theme.font;
    u32 content_color;
    u32 text_color;
    u32 frame_color;
    u32 title_color;
    u32 border_color;
    int is_focused;
    int index;
    int anim_ticks;
    int anim_mode;
    int expand;
    int shadow_pad;
    int glow_color;
    if (!app->open || app->minimized) return;
    index = (int)(app - app_windows);
    anim_ticks = (index >= 0 && index < 32) ? window_transition_ticks[index] : 0;
    anim_mode = (index >= 0 && index < 32) ? window_transition_mode[index] : 0;
    expand = 0;
    shadow_pad = 8;
    glow_color = 0xFFFFFF;
    if (anim_ticks > 0) {
        int phase = 9 - anim_ticks;
        if (phase < 1) phase = 1;
        if (phase > 8) phase = 8;
        if (anim_mode == 1) {
            expand = 8 - phase;
            shadow_pad = 8 + (expand / 2);
            glow_color = 0xA7C7E7;
        } else if (anim_mode == 2) {
            expand = phase;
            shadow_pad = 8 + phase;
            glow_color = 0xD9E8FA;
        } else if (anim_mode == 3) {
            expand = 4;
            shadow_pad = 10;
            glow_color = 0xF7E7B7;
        }
        if (window_transition_ticks[index] > 0) {
            window_transition_ticks[index]--;
            if (window_transition_ticks[index] <= 0) window_transition_mode[index] = 0;
            needs_redraw = 1;
        }
    }
    is_focused = (active_app_index >= 0 && &app_windows[active_app_index] == app);
    title_color = is_focused ? active_theme.title_bar_focused : active_theme.title_bar_inactive;
    border_color = is_focused ? active_theme.window_border_focused : active_theme.window_border_inactive;
    label = app_name_from_kind(app->kind);
    if (app->kind == APP_IDE) {
        extern int ide_active_tab;
        extern ide_tab_t ide_tabs[4];
        if (ide_tabs[ide_active_tab].filename[0]) {
            int i = 0, last_slash = -1;
            while (ide_tabs[ide_active_tab].filename[i]) {
                if (ide_tabs[ide_active_tab].filename[i] == '/') last_slash = i;
                i++;
            }
            if (last_slash >= 0) label = ide_tabs[ide_active_tab].filename + last_slash + 1;
            else label = ide_tabs[ide_active_tab].filename;
        }
    }
    line = app->kind == APP_TERMINAL ? "READY" : app->kind == APP_FILES ? "APPS" : app->kind == APP_DISPLAY ? "SCREEN" : app->kind == APP_EXPLORER ? "ROOT" : app->kind == APP_SETTINGS ? "CONTROL" : app->kind == APP_MEDIA_PLAYER ? "MEDIA" : app->kind == APP_CALCULATOR ? "CALCULATOR" : app->kind == APP_PREVIEW ? "PREVIEW" : "EDITOR";
    frame_color = 0xDCE4EE;
    if (app->kind == APP_EXPLORER) frame_color = 0xF1ECD9;
    if (app->kind == APP_SETTINGS) frame_color = 0xECF0F8;
    content_color = app->kind == APP_TERMINAL ? 0x12202C : app->kind == APP_FILES ? 0xE6EDF5 : app->kind == APP_DISPLAY ? 0xD9D9E8 : app->kind == APP_EXPLORER ? 0xECE6D1 : app->kind == APP_SETTINGS ? 0xE7EBF4 : 0xF2E8D9;
    text_color = app->kind == APP_TERMINAL ? 0xA7D7FF : 0x20344E;
    if (anim_ticks > 0) {
        int phase = 9 - anim_ticks;
        if (phase < 1) phase = 1;
        if (phase > 8) phase = 8;
        float t = (float)phase / 8.0f; // 0.0 to 1.0
        if (anim_mode == 1) { // Open/Minimize
            int dx, dy, dw, dh;
            dock_get_item_rect(app->kind, &dx, &dy, &dw, &dh);
            if (app->minimized) { // Minimizing: frame -> dock
                t = 1.0f - t; 
            } else { // Opening/Restoring: dock -> frame
                // t is already correct
            }
            int ix = dx + (app->frame.x - dx) * t;
            int iy = dy + (app->frame.y - dy) * t;
            int iw = dw + (app->frame.w - dw) * t;
            int ih = dh + (app->frame.h - dh) * t;
            outline(ix, iy, iw, ih, 0x88CCFF);
            outline(ix+1, iy+1, iw-2, ih-2, 0x88CCFF);
        } else if (anim_mode == 2) {
            fb_rect(app->frame.x - expand, app->frame.y - expand, app->frame.w + expand * 2, app->frame.h + expand * 2, glow_color);
        }
    }
    
    // Draw shadow
    fb_draw_window_shadow(app->frame.x, app->frame.y, app->frame.w, app->frame.h, WINDOW_CORNER_RADIUS, is_focused);
    
    // Draw window frame (rounded)
    fb_round_rect(app->frame.x, app->frame.y, app->frame.w, app->frame.h, WINDOW_CORNER_RADIUS, frame_color);
    fb_round_rect_border(app->frame.x, app->frame.y, app->frame.w, app->frame.h, WINDOW_CORNER_RADIUS, border_color);

    // Draw title bar
    int show_titlebar = !titlebar_autohide || point_in_rect(ps2_mouse_x(), ps2_mouse_y(), app->frame);
    if (show_titlebar) {
        u32 t_col = titlebar_color_custom ? titlebar_custom_color : title_color;
        if (titlebar_position == 1) { // Bottom title bar
            if (titlebar_alpha < 255) {
                fb_round_rect_bottom_alpha(app->title.x, app->title.y, app->title.w, app->title.h, WINDOW_CORNER_RADIUS, t_col, titlebar_alpha);
            } else {
                fb_round_rect_bottom(app->title.x, app->title.y, app->title.w, app->title.h, WINDOW_CORNER_RADIUS, t_col);
            }
        } else { // Top title bar
            if (titlebar_alpha < 255) {
                fb_round_rect_top_alpha(app->title.x, app->title.y, app->title.w, app->title.h, WINDOW_CORNER_RADIUS, t_col, titlebar_alpha);
            } else {
                fb_round_rect_top(app->title.x, app->title.y, app->title.w, app->title.h, WINDOW_CORNER_RADIUS, t_col);
            }
        }
        app->title.color = t_col;
        draw_window_controls(app->close, app->maximize, app->minimize, app->resize);
        char title_buf[64];
        if (app->instance_id > 1) {
            int li = 0;
            while (label[li] && li < 40) { title_buf[li] = label[li]; li++; }
            title_buf[li++] = ' ';
            title_buf[li++] = '(';
            title_buf[li++] = '0' + (app->instance_id % 10);
            title_buf[li++] = ')';
            title_buf[li] = 0;
            label = title_buf;
        }
        font_text_fit_center(app->frame.x, app->title.y + 10, label, system_font_color, app->frame.w);

        int pmx = ps2_mouse_x(), pmy = ps2_mouse_y();
        if (point_in_rect(pmx, pmy, app->close)) gui_tooltip_show(app->close.x + 6, app->close.y - 18, "Close Window");
        else if (point_in_rect(pmx, pmy, app->minimize)) gui_tooltip_show(app->minimize.x + 6, app->minimize.y - 18, "Minimize");
        else if (point_in_rect(pmx, pmy, app->maximize)) gui_tooltip_show(app->maximize.x + 6, app->maximize.y - 18, "Maximize");
    }
    int clip_y = (titlebar_position == 1) ? app->frame.y + 2 : (app->kind == APP_BROWSER ? app->frame.y + 28 : app->frame.y + 42);
    int clip_h = (titlebar_position == 1) ? app->frame.h - 4 : (app->kind == APP_BROWSER ? app->frame.h - 30 : app->frame.h - 44);
    fb_set_clip(app->frame.x + 2, clip_y, app->frame.w - 4, clip_h);
    if (app->kind == APP_EXPLORER) draw_explorer_contents(app);
    else if (app->kind == APP_MEDIA_PLAYER) media_player_draw(app);
    else if (app->kind == APP_PREVIEW) preview_draw(app);
    else if (app->kind == APP_PHOTO_EDITOR) {
        draw_photo_viewer(app);
    }
    else if (app->kind == APP_CALCULATOR) draw_calculator_contents(app);
    else if (app->kind == APP_SETTINGS) {
        draw_settings_contents(app);
        draw_window_scrollbar(app, 2, 1, settings_scroll);
    }
    else if (app->kind == APP_TERMINAL) {
        terminal_focused = (active_app_index >= 0 && active_app_index < 32 && app_windows[active_app_index].kind == APP_TERMINAL);
        terminal_cursor_visible = cursor_blink_visible;
        terminal_draw(app->frame.x, app->frame.y, app->frame.w, app->frame.h);
    }
    else if (app->kind == APP_TASKMAN) {
        draw_taskman_window(app);
    }
    else if (app->kind == APP_TEXT_EDIT) {
        extern void draw_text_edit_contents(app_window_t *app);
        draw_text_edit_contents(app);
    }
    else if (app->kind == APP_BROWSER) {
        web_browser_app_draw(app);
    }
    else if (app->kind == APP_IDE) {
        int is_ide = 1;
        char *txt = ide_text;
        int tlen = ide_length;
        int tcur = ide_cursor;
        int tsav = ide_saved;
        rect_t *tarea = &ide_text_area;
        int tsel_start = (ide_select_anchor >= 0 && ide_select_active >= 0 && ide_select_anchor != ide_select_active) ? (ide_select_anchor < ide_select_active ? ide_select_anchor : ide_select_active) : -1;
        int tsel_end = (ide_select_anchor >= 0 && ide_select_active >= 0 && ide_select_anchor != ide_select_active) ? (ide_select_anchor > ide_select_active ? ide_select_anchor : ide_select_active) : -1;

        int sidebar_w = 130;
        int ex = app->frame.x + 16 + sidebar_w;
        int ey = app->frame.y + 46;
        int ew = app->frame.w - 32 - sidebar_w;
        
        if (is_ide) {
            int sb_x = app->frame.x + 16;
            int sb_y = app->frame.y + 46;
            int sb_w = 120;
            int sb_h = app->frame.h - 60;
            fb_rect(sb_x, sb_y, sb_w, sb_h, 0xF0F0F0);
            outline(sb_x, sb_y, sb_w, sb_h, 0x8B735D);
            draw_label(sb_x + 4, sb_y + 6, "PROJECT TREE", 0x333333);
            fb_rect(sb_x, sb_y + 22, sb_w, 1, 0x8B735D);
            
            #include "ide.h"
            int fy = sb_y + 26;
            draw_label(sb_x + 6, fy, g_app_studio.project_name[0] ? g_app_studio.project_name : "Project", 0x2563EB);
            fy += 16;
            for(int fi = 0; fi < g_app_studio.file_count && fy < sb_y + sb_h - 20; fi++) {
                int is_sel = (fi == g_app_studio.selected_file_idx);
                if (is_sel) {
                    fb_rect(sb_x + 4, fy - 1, sb_w - 8, 14, 0xDCEAFE);
                }
                draw_label(sb_x + 10, fy, g_app_studio.files[fi].name, is_sel ? 0x1D4ED8 : 0x111111);
                fy += 16;
            }
            
            // Draw tabs
            for(int ti=0; ti<4; ti++) {
                int tx = ex + 4 + ti*60;
                int ty = ey + 34;
                fb_rect(tx, ty, 56, 18, ide_active_tab == ti ? 0xFFFDF8 : 0xE9D5BF);
                if (ide_active_tab != ti) outline(tx, ty, 56, 18, 0x8B735D);
                char tname[16]; tname[0]=0;
                
                
                gui_fl_cat(tname, "Tab ", 16); char tn[4]; gui_fl_itos(ti+1, tn, 4); gui_fl_cat(tname, tn, 16);
                
                // if filename has .fl, strip it for space
                char dname[32]; dname[0]=0;
                if (ide_tabs[ti].filename[0]) {
                    int i=0; while(ide_tabs[ti].filename[i] && ide_tabs[ti].filename[i]!='.') {
                        dname[i]=ide_tabs[ti].filename[i]; i++;
                    }
                    dname[i]=0;
                }
                draw_label(tx+4, ty+4, ide_tabs[ti].filename[0] ? dname : tname, 0x4B321E);
            }
        }

        int pos = 0;
        int line_no = 1;
        int line_y = ey + 64;
        int text_x = ex + 42;
        int select_start = tsel_start;
        int select_end = tsel_end;
        fb_rect(ex, ey, ew, 28, 0xD7B895);
        fb_rect(ex + 1, ey + 1, ew - 2, 26, 0xE9D5BF);
        draw_label(ex + 12, ey + 10, is_ide ? "Firstlang IDE" : "notes.txt", 0x4B321E);
        draw_label(ex + 82, ey + 10, tsav ? "SAVED" : "UNSAVED", tsav ? 0x3A6A3D : 0x8A6236);
        draw_button((rect_t){ex + ew - 94, ey + 5, 78, 18, 0}, tsav ? 0x4F8452 : 0x8A6236, tsav ? 0x78B07C : 0xB68658, "SAVE", 0xFFFFFF);
        draw_button((rect_t){ex + ew - 178, ey + 5, 74, 18, 0}, 0x6E8299, 0x8FA7BC, "CLEAR", 0xFFFFFF);
        fb_rect(ex, ey + 34, ew, app->frame.h - 96, 0xFFFDF8);
        if (is_ide && ide_designer_mode) {
            draw_ui_builder((rect_t){ex, ey + 34, ew, app->frame.h - 96});
        } else {
        (*tarea) = (rect_t){text_x - 4, ey + 62, ew - 56, app->frame.h - 142, 0};
        if (is_ide) { (*tarea).y += 20; (*tarea).h -= 20; }

        if (is_ide) {
            int ti;
            extern int ide_active_tab;
            extern ide_tab_t ide_tabs[4];
            for (ti = 0; ti < 4; ti++) {
                int tx = ex + 8 + ti * 80;
                int tw = 76;
                int th = 18;
                u32 tc = (ti == ide_active_tab) ? 0xFFFFFF : 0xE5D1BE;
                u32 txtc = (ti == ide_active_tab) ? 0x000000 : 0x7A5B3E;
                fb_rect(tx, ey + 42, tw, th, tc);
                outline(tx, ey + 42, tw, th, 0xC3A68B);
                char tlbl[16]; tlbl[0]=0;
                if (ide_tabs[ti].filename[0]) {
                    int p = 0, last_slash = -1;
                    while (ide_tabs[ti].filename[p]) { if(ide_tabs[ti].filename[p]=='/') last_slash=p; p++; }
                    int k = 0; p = last_slash + 1;
                    while(ide_tabs[ti].filename[p] && k < 12) tlbl[k++] = ide_tabs[ti].filename[p++];
                    tlbl[k]=0;
                } else if (ide_tabs[ti].text[0]) {
                    const char *u = "Untitled"; int j=0;
                    while(u[j] && j<12) { tlbl[j] = u[j]; j++; } tlbl[j] = 0;
                } else {
                    const char *u = "Empty"; int j=0;
                    while(u[j] && j<12) { tlbl[j] = u[j]; j++; } tlbl[j] = 0;
                }
                font_text_fit_center(tx, ey + 48, tlbl, txtc, tw);
            }
            draw_button((rect_t){ex + 8 + 4 * 80, ey + 42, 18, 18, 0}, 0xE9D5BF, 0xF5E6D8, "+", 0x7A5B3E);
        } else {
            fb_rect(ex + 8, ey + 42, ew - 16, 18, 0xF1E5D7);
            draw_label(ex + 14, ey + 48, "DOCUMENT  •  CTRL+C COPY  CTRL+X CUT  CTRL+V PASTE", 0x6A4B31);
        }
        outline(ex, ey + 34, ew, app->frame.h - 96, 0x8B735D);
        while (pos <= tlen && line_y < (*tarea).y + (*tarea).h) {
            int line_start = pos;
            int line_end = pos;
            int j;
            while (line_end < tlen && txt[line_end] != '\n') line_end++;
            draw_label(ex + 12, line_y, line_no < 10 ? "0" : "", 0x9B8067);
            if (line_no >= 10) {
                char digits[8];
                int n = line_no;
                int d = 0;
                while (n) { digits[d++] = (char)('0' + n % 16); n /= 10; }
                digits[d] = 0;
                for (j = 0; j < d / 2; j++) { char t = digits[j]; digits[j] = digits[d - 1 - j]; digits[d - 1 - j] = t; }
                draw_label(ex + 12, line_y, digits, 0x9B8067);
            }
            if (select_start >= 0 && select_end > line_start && select_start < line_end) {
                int hs = select_start > line_start ? select_start - line_start : 0;
                int he = select_end < line_end ? select_end - line_start : line_end - line_start;
                if (he > hs) fb_rect(text_x + hs * 6 - 2, line_y - 2, (he - hs) * 6 + 3, 14, 0xB8D4F2);
            }
            for (j = line_start; j < line_end; j++) {
                char one[2];
                one[0] = txt[j]; one[1] = 0;
                u32 color = 0x3A2A1C;
                if(is_ide) {
                    if(one[0]=='"' || one[0]=='\'') color = 0x1F8A4F;
                    else if(one[0]>='0' && one[0]<='9') color = 0x8A1F1F;
                    else if(one[0]==':' || one[0]=='(' || one[0]==')' || one[0]=='[' || one[0]==']') color = 0x805020;
                }
                draw_label(text_x + (j - line_start) * 6, line_y, one, color);
            }
            if(is_ide) {
                draw_button((rect_t){ex + ew - 262, ey + 5, 74, 18, 0}, 0x3B82F6, 0x60A5FA, "RUN", 0xFFFFFF);
                draw_button((rect_t){ex + ew - 344, ey + 5, 74, 18, 0}, ide_designer_mode ? 0x9333EA : 0x8B5CF6, ide_designer_mode ? 0x7E22CE : 0xA78BFA, "DESIGN", 0xFFFFFF);
            }
            if (tcur >= line_start && tcur <= line_end && cursor_blink_visible && text_input_focus) {
                fb_rect(text_x + (tcur - line_start) * 6, line_y - 2, 2, 14, 0x315A8A);
            }
            pos = line_end < tlen ? line_end + 1 : tlen + 1;
            line_y += 14;
            line_no++;
        }

        if (is_ide) {
            // Draw App Studio Live Output & Diagnostics Terminal Panel via app_framework
            int p_y = app->frame.y + app->frame.h - 110;
            int p_h = 88;
            rect_t pane_r = {ex, p_y, ew, p_h, 0};
            app_output_pane_draw(pane_r, &g_app_studio.output_pane);

            rect_t sb_r = {ex, app->frame.y + app->frame.h - 20, ew, 18, 0};
            app_draw_status_bar(sb_r, &g_app_studio.status_bar);
        }
        }
        if (!is_ide) {
            draw_label(ex + 12, app->frame.y + app->frame.h - 16, tsav ? "STATUS: SAVED TO FS IMAGE" : "STATUS: MODIFIED / SAVE READY", tsav ? 0x3A6A3D : 0x8A6236);
        }
    }
    else {
        fb_rect(app->frame.x + 14, app->frame.y + 42, app->frame.w - 28, app->frame.h - 56, content_color);
        fb_rect(app->frame.x + 22, app->frame.y + 56, app->frame.w - 44, 24, 0xF6F8FB);
        font_text_fit_center(app->frame.x + 18, app->frame.y + 63, line, text_color, app->frame.w - 36);
        fb_rect(app->frame.x + 22, app->frame.y + 92, app->frame.w - 56, app->frame.h - 118, 0xFFFFFF);
        outline(app->frame.x + 22, app->frame.y + 92, app->frame.w - 56, app->frame.h - 118, 0xA1B0BF);
        draw_scrollbar(app->frame.x + app->frame.w - 24, app->frame.y + 48, app->frame.h - 66, 8, 5, 0);
    }
    fb_reset_clip();
    outline(app->frame.x, app->frame.y, app->frame.w, app->frame.h, border_color);
}

static void draw_main_window(void) {
    int content_w;
    int half_w;
    char gpu_info[96];
    const pci_device_t *gpu;
    if (!main_window_open || main_window_minimized) return;
    content_w = main_window.w - 48;
    half_w = (main_window.w - 64) / 2;
    gpu = pci_primary_display_adapter();
    pci_describe_primary_display(gpu_info, sizeof(gpu_info));
    // Shadow
    fb_draw_window_shadow(main_window.x, main_window.y, main_window.w, main_window.h, WINDOW_CORNER_RADIUS, 0);
    
    // Main window background (rounded)
    fb_round_rect(main_window.x, main_window.y, main_window.w, main_window.h, WINDOW_CORNER_RADIUS, window_bg);

    // Title bar (rounded top)
    fb_round_rect_top(main_title_bar.x, main_title_bar.y, main_title_bar.w, main_title_bar.h, WINDOW_CORNER_RADIUS, title_bg);
    draw_window_controls(main_button_close, main_button_max, main_button_min, (rect_t){main_window.x + main_window.w - 14, main_window.y + main_window.h - 14, 10, 10, 0x5A6D82});
    fb_rect(main_window.x + 24, main_window.y + 60, content_w, 28, 0xF2F6FB);
    fb_rect(main_window.x + 24, main_window.y + 100, content_w, 72, 0xC7D3E2);
    fb_rect(main_window.x + 24, main_window.y + 184, half_w, 84, 0xBBC8D8);
    fb_rect(main_window.x + 40 + half_w, main_window.y + 184, half_w, 84, 0xA8B8CB);
    draw_button((rect_t){main_window.x + 24, main_window.y + main_window.h - 50, 132, 26, 0}, dragging && window_drag_kind == 0 ? 0x466FA0 : 0x315A8A, 0x6C92C0, "OPEN", 0xFFFFFF);
    draw_button((rect_t){main_window.x + 166, main_window.y + main_window.h - 50, 108, 26, 0}, 0x5F738D, 0x7E97B4, "INFO", 0xFFFFFF);
    font_text_fit_center(main_window.x, main_window.y + 14, "FIRSTOS WORKSPACE", system_font_color, main_window.w);
    draw_label(main_window.x + 34, main_window.y + 68, "WELCOME", 0x20344E);
    draw_label(main_window.x + 34, main_window.y + 108, "SYSTEM", 0x20344E);
    draw_label(main_window.x + 34, main_window.y + 124, "GPU", 0x20344E);
    font_text_fit(main_window.x + 78, main_window.y + 124, gpu_info, 0x20344E, content_w - 90);
    draw_label(main_window.x + 34, main_window.y + 142, "ADAPTERS", 0x20344E);
    draw_label(main_window.x + 104, main_window.y + 142, pci_has_display_adapter() ? "DETECTED" : "NONE", pci_has_display_adapter() ? 0x244462 : 0x7A2F2F);
    draw_label(main_window.x + 184, main_window.y + 142, "COUNT", 0x20344E);
    draw_label(main_window.x + 228, main_window.y + 142, pci_display_adapter_count() > 1 ? "MULTI" : "ONE", 0x20344E);
    if (gpu) {
        draw_label(main_window.x + 34, main_window.y + 156, "BUS/SLOT/FUNC", 0x20344E);
        draw_label(main_window.x + 138, main_window.y + 156, gpu->bus > 15 ? "PCI" : "PCI0", 0x20344E);
    }
    draw_label(main_window.x + 34, main_window.y + 192, "APPS", 0x20344E);
    draw_label(main_window.x + 52 + half_w, main_window.y + 192, "DISPLAY", 0x20344E);
    draw_label(main_window.x + 52 + half_w, main_window.y + 212, "FRAMEBUFFER ACTIVE", 0x20344E);
    draw_label(main_window.x + 52 + half_w, main_window.y + 228, fb.ready ? "READY" : "OFFLINE", fb.ready ? 0x244462 : 0x7A2F2F);
    draw_scrollbar(main_window.x + main_window.w - 26, main_window.y + 60, main_window.h - 84, 7, 5, 0);
    outline(main_window.x, main_window.y, main_window.w, main_window.h, 0x6E7F91);
}

static int begin_app_drag(int mx, int my) {
    int i = 0;
    for (int z = 31; z >= 0; z--) {
        i = z_order[z];
        if (app_windows[i].open && !app_windows[i].minimized && point_in_rect(mx, my, app_windows[i].title)) {
            focus_app_window(i);
            window_dragging = 1;
            window_drag_index = i;
            drag_offset_x = mx - app_windows[i].frame.x;
            drag_offset_y = my - app_windows[i].frame.y;
            return 1;
        }
    }
    return 0;
}



static void handle_explorer_click(app_window_t *app, int mx, int my) {
    serial_write("[gui_explorer] handle_explorer_click mx=");
    serial_write_hex((u32)mx);
    serial_write(" my=");
    serial_write_hex((u32)my);
    serial_write(" path=");
    serial_write(app ? app->current_path : "(null)");
    serial_write("\n");
    if (!app) return;
    if (handle_window_tab_click(app, mx, my)) {
        needs_redraw = 1;
        return;
    }
    int k;
    int content_x = app->frame.x + 10;
    int content_y = app->frame.y + 42 + 24;
    int content_w = app->frame.w - 20;
    int content_h = app->frame.h - 52 - 24;
    int header_h = 34;
    int toolbar_h = 34;
    int sidebar_w = 134;
    int status_h = 24;
    int preview_pane_w = (explorer_preview_pane_open && content_w >= 440) ? 170 : 0;
    int main_x = content_x + sidebar_w + 1;
    int main_w = content_w - sidebar_w - preview_pane_w - (preview_pane_w ? 2 : 1);
    int grid_y = content_y + header_h + toolbar_h + 14;
    int grid_h = content_h - header_h - toolbar_h - status_h - 18;
    int save_bar_h = editor_save_dialog_open ? 36 : 0;
    int card_w = main_w < 250 ? 82 : 96;
    int cell_h = 86;
    int icon_cols = (main_w - 24) / card_w;
    int visible_rows;
    int visible_items;
    int entry_count = explorer_entry_count();
    int filtered_count = explorer_filtered_count();
    int preview_btn_w = 60;
    int preview_btn_x = content_x + content_w - preview_btn_w - 8;
    int search_w = 140;
    int search_x = preview_btn_x - search_w - 8;
    int search_y = content_y + header_h + 6;
    int index;

    if (icon_cols < 1) icon_cols = 1;
    visible_rows = (grid_h - save_bar_h) / cell_h;
    if (visible_rows < 1) visible_rows = 1;
    visible_items = visible_rows * icon_cols;

    if (!app->current_path[0]) {
        gui_fl_cpy(app->current_path, "FirstOS", sizeof(app->current_path));
    }
    explorer_open_path(app->current_path);

    if (mx >= content_x + 8 && mx <= content_x + 26 && my >= content_y + 8 && my <= content_y + 26) {
        if (explorer_nav_back()) {
            gui_fl_cpy(app->current_path, explorer_current_path(), sizeof(app->current_path));
            explorer_path = app->current_path;
            sync_explorer_active_tab(app);
            needs_redraw = 1;
            return;
        }
    }
    if (mx >= content_x + 52 && mx <= content_x + 70 && my >= content_y + 8 && my <= content_y + 26) {
        explorer_open_path(explorer_parent_path());
        gui_fl_cpy(app->current_path, explorer_current_path(), sizeof(app->current_path));
        explorer_path = app->current_path;
        sync_explorer_active_tab(app);
        explorer_selected = -1;
        explorer_scroll = 0;
        needs_redraw = 1;
        return;
    }

    if (mx >= content_x + 8 && mx <= content_x + sidebar_w - 8) {
        int quick_y = content_y + header_h + toolbar_h + 10;
        if (my >= quick_y && my <= quick_y + 24) {
            explorer_open_path("FirstOS/Desktop");
            gui_fl_cpy(app->current_path, explorer_current_path(), sizeof(app->current_path));
            explorer_path = app->current_path;
            sync_explorer_active_tab(app);
            explorer_selected = -1;
            explorer_scroll = 0;
            needs_redraw = 1;
            return;
        }
        if (my >= quick_y + 26 && my <= quick_y + 50) {
            explorer_open_path("FirstOS/Wallpapers");
            gui_fl_cpy(app->current_path, explorer_current_path(), sizeof(app->current_path));
            explorer_path = app->current_path;
            sync_explorer_active_tab(app);
            explorer_selected = -1;
            explorer_scroll = 0;
            needs_redraw = 1;
            return;
        }
        if (my >= quick_y + 52 && my <= quick_y + 76) {
            explorer_open_path("FirstOS/Audio");
            gui_fl_cpy(app->current_path, explorer_current_path(), sizeof(app->current_path));
            explorer_path = app->current_path;
            sync_explorer_active_tab(app);
            explorer_selected = -1;
            explorer_scroll = 0;
            needs_redraw = 1;
            return;
        }
        for (index = 0; index < explorer_disk_count() && index < 5; index++) {
            int row_y = content_y + header_h + toolbar_h + 96 + index * 22;
            const explorer_disk_t *disk = explorer_disk_at(index);
            if (!disk) continue;
            if (my >= row_y - 4 && my <= row_y + 14) {
                explorer_open_path(disk->label);
                gui_fl_cpy(app->current_path, explorer_current_path(), sizeof(app->current_path));
                explorer_path = app->current_path;
                sync_explorer_active_tab(app);
                explorer_selected = -1;
                explorer_scroll = 0;
                needs_redraw = 1;
                return;
            }
        }
    }

    if (editor_save_dialog_open && point_in_rect(mx, my, (rect_t){main_x + main_w - 82, content_y + header_h + toolbar_h + 8, 60, 18, 0})) {
        trigger_explorer_save();
        return;
    }

    if (editor_save_dialog_open && point_in_rect(mx, my, (rect_t){main_x + 84, content_y + header_h + toolbar_h + 8, main_w - 180, 18, 0})) {
        editor_save_name_focus = 1; explorer_search_focus = 0;
        needs_redraw = 1;
        return;
    }
    
    if (explorer_search_history_rect.h > 0 && point_in_rect(mx, my, explorer_search_history_rect)) {
        int row = (my - explorer_search_history_rect.y - 4) / 18;
        int j = 0;
        if (row >= 0 && row < explorer_search_history_count) {
            while (explorer_search_history[row][j] && j + 1 < (int)sizeof(explorer_search_text)) { explorer_search_text[j] = explorer_search_history[row][j]; j++; }
            explorer_search_text[j] = 0;
            explorer_search_cursor = j;
            explorer_search_focus = 1;
            explorer_search_history_open = 0;
            explorer_search_history_active = -1;
            explorer_search_result_index = -1;
            explorer_scroll = 0;
            needs_redraw = 1;
            return;
        }
    }

    if (explorer_search_clear_button.w > 0 && point_in_rect(mx, my, explorer_search_clear_button)) {
        explorer_search_text[0] = 0;
        explorer_search_cursor = 0;
        explorer_search_focus = 1; editor_save_name_focus = 0;
        explorer_search_history_active = -1;
        explorer_search_result_index = -1;
        explorer_selected = -1;
        explorer_scroll = 0;
        needs_redraw = 1;
        return;
    }

    if (mx >= search_x && mx <= search_x + search_w && my >= search_y && my <= search_y + 20) {
        explorer_search_focus = 1;
        explorer_search_history_open = explorer_search_history_count > 0 ? 1 : 0;
        explorer_search_history_active = explorer_search_history_open ? 0 : -1;
        needs_redraw = 1;
        return;
    } else if (explorer_search_focus) {
        explorer_search_focus = 0;
        explorer_search_history_open = 0;
        explorer_search_history_active = -1;
    }

    if (explorer_preview_toggle_btn.w > 0 && point_in_rect(mx, my, explorer_preview_toggle_btn)) {
        explorer_preview_pane_open = !explorer_preview_pane_open;
        needs_redraw = 1;
        return;
    }

    if (explorer_btn_open_app.w > 0 && point_in_rect(mx, my, explorer_btn_open_app) && explorer_selected >= 0) {
        const explorer_entry_t *e = explorer_entry_at(explorer_selected);
        if (e && !e->is_dir) {
            explorer_open_file_default(e->path, e->name);
        }
        needs_redraw = 1;
        return;
    }

    if (explorer_preview_play_btn.w > 0 && point_in_rect(mx, my, explorer_preview_play_btn) && explorer_selected >= 0) {
        const explorer_entry_t *e = explorer_entry_at(explorer_selected);
        if (e && !e->is_dir) {
            if (hda_is_playing()) {
                hda_pause_audio();
            } else if (hda_is_paused()) {
                hda_resume_audio();
            } else {
                hda_play_audio(e->path);
            }
        }
        needs_redraw = 1;
        return;
    }

    if (point_in_rect(mx, my, explorer_new_file_button)) {
        int len = 0;
        const char *base = "new-file.txt";
        while (base[len] && len + 1 < (int)sizeof(editor_save_name)) { editor_save_name[len] = base[len]; len++; }
        editor_save_name[len] = 0;
        editor_save_name_cursor = len;
        editor_save_dialog_open = 1;
        sound_play_event(SOUND_EVENT_DIALOG_POPUP);
        explorer_picker_mode = 1;
        editor_save_name_focus = 1;
        explorer_selected = -1;
        explorer_dragging = 0;
        explorer_drag_index = -1;
        explorer_drag_moved = 0;
        needs_redraw = 1;
        return;
    }

    if (mx >= main_x + main_w - 18 && mx <= main_x + main_w - 2) {
        if (my <= grid_y + 10 && explorer_scroll > 0) explorer_scroll -= icon_cols;
        else if (my >= grid_y + grid_h - save_bar_h - 10 && explorer_scroll < filtered_count - visible_items) explorer_scroll += icon_cols;
        if (explorer_scroll < 0) explorer_scroll = 0;
        if (explorer_scroll > filtered_count - visible_items) explorer_scroll = filtered_count - visible_items;
        if (explorer_scroll < 0) explorer_scroll = 0;
        needs_redraw = 1;
        return;
    }

    index = explorer_item_at(app, mx, my);
    if (index >= 0 && index < entry_count) {
        const explorer_entry_t *entry = explorer_entry_at(index);
        unsigned int now = get_system_ticks();
        int dx = mx - unified_last_click_x;
        int dy = my - unified_last_click_y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        int is_double = (unified_last_click_source == 1 &&
                         unified_last_click_index == index &&
                         (now - unified_last_click_time) <= 60 &&
                         dx <= 10 && dy <= 10);
        if (keyboard_ctrl) {
            if (index >= 0 && index < 256) explorer_multi_selected[index] = explorer_multi_selected[index] ? 0 : 1;
            explorer_selected = index;
            explorer_selection_anchor = index;
        } else if (keyboard_shift && explorer_selection_anchor >= 0) {
            explorer_selected = index;
            explorer_select_range(explorer_selection_anchor, index);
        } else {
            explorer_clear_multi_selection();
            explorer_selected = index;
            explorer_selection_anchor = index;
            if (index >= 0 && index < 256) explorer_multi_selected[index] = 1;
            if (explorer_picker_mode && entry && !entry->is_dir) {
                int k = 0; while (entry->name[k] && k < 31) { editor_save_name[k] = entry->name[k]; k++; } editor_save_name[k] = 0;
                editor_save_name_cursor = k;
            }
        }
        if (is_double && entry) {
            serial_write("[mouse] explorer double click: opening ");
            if (entry->name) serial_write(entry->name);
            serial_write("\n");
            unified_last_click_time = 0;
            unified_last_click_index = -1;
            explorer_dragging = 0;
            explorer_drag_index = -1;
            explorer_drag_moved = 0;
            explorer_drag_start_time = 0;
            if (entry->is_dir && entry->path) {
                explorer_open_path(entry->path);
                gui_fl_cpy(app->current_path, explorer_current_path(), sizeof(app->current_path));
                explorer_path = app->current_path;
                sync_explorer_active_tab(app);
                explorer_selected = -1;
                explorer_clear_multi_selection();
                explorer_selection_anchor = -1;
                explorer_scroll = 0;
                explorer_last_click_index = -1;
                explorer_last_click_ticks = 0;
            } else if (explorer_picker_mode) {
                k=0; while(entry->name[k] && k < 31) { editor_save_name[k] = entry->name[k]; k++; } editor_save_name[k]=0;
                extern void trigger_explorer_save(void);
                trigger_explorer_save();
            } else {
                explorer_open_file_default(entry->path, entry->name);
                explorer_last_click_index = -1;
            }
        } else {
            serial_write("[mouse] explorer single click: primed drag\n");
            unified_last_click_source = 1;
            unified_last_click_index = index;
            unified_last_click_time = now;
            unified_last_click_x = mx;
            unified_last_click_y = my;
            explorer_last_click_index = index;
            explorer_last_click_ticks = 0;
            explorer_dragging = 1;
            explorer_drag_index = index;
            explorer_window_drag_start_x = mx;
            explorer_window_drag_start_y = my;
            explorer_drag_start_time = now;
            explorer_drag_moved = 0;
            explorer_drop_target_index = -1;
        }
        needs_redraw = 1;
        return;
    }

    // If clicked empty space, start marquee
    explorer_marquee_active = 1;
    explorer_marquee_x = mx;
    explorer_marquee_y = my;
    explorer_marquee_mx = mx;
    explorer_marquee_my = my;


    explorer_selected = -1;
    explorer_selection_anchor = -1;
    explorer_clear_multi_selection();
    needs_redraw = 1;
}

static void explorer_finish_drag_navigation(int mx, int my) {
    app_window_t *app = app_slot(APP_EXPLORER);
    int target_index;
    if (!app || !app->open || app->minimized || !explorer_dragging) return;
    target_index = explorer_item_at(app, mx, my);
    if (explorer_drag_index >= 0 && explorer_drag_moved && target_index >= 0 && target_index < explorer_entry_count()) {
        const explorer_entry_t *target = explorer_entry_at(target_index);
        if (target && target->is_dir && target_index != explorer_drag_index && target->path) {
            const explorer_entry_t *src = explorer_entry_at(explorer_drag_index);
            if (src && src->path) {
                char new_path[256];
                int i = 0, j = 0;
                const char *mapped = explorer_to_fs_path(target->path);
                while (mapped[i] && i < 255) { new_path[i] = mapped[i]; i++; }
                if (i > 0 && new_path[i-1] != '/' && i < 255) new_path[i++] = '/';
                while (src->name[j] && i < 255) { new_path[i] = src->name[j]; i++; j++; }
                new_path[i] = 0;
                extern int fs_rename_at(const char *old_path, const char *new_path);
                fs_rename_at(src->path, new_path);
                explorer_open_path(explorer_current_path());
            }
        }
    }
    explorer_dragging = 0;
    explorer_drag_index = -1;
    explorer_drag_moved = 0;
    explorer_drop_target_index = -1;
    explorer_drag_preview_x = 0;
    explorer_drag_preview_y = 0;
}

static int explorer_nav_back(void) {
    const char *path = explorer_current_path();
    char parent[64];
    int len = 0;
    int last_slash = -1;
    if (!path || !path[0]) return 0;
    while (path[len] && len + 1 < (int)sizeof(parent)) {
        parent[len] = path[len];
        if (path[len] == '/') last_slash = len;
        len++;
    }
    parent[len] = 0;
    if (path[0] == '/' && last_slash <= 0) {
        explorer_open_path("FirstOS/Desktop");
        explorer_selected = -1;
        explorer_scroll = 0;
        explorer_last_click_index = -1;
        explorer_last_click_ticks = 0;
        explorer_dragging = 0;
        explorer_drag_index = -1;
        explorer_drag_moved = 0;
        explorer_path = explorer_current_path();
        return 1;
    }
    if (last_slash < 0) {
        explorer_open_path("FirstOS");
    } else if (last_slash == 0) {
        parent[1] = 0;
        explorer_open_path(parent);
    } else {
        parent[last_slash] = 0;
        explorer_open_path(parent);
    }
    explorer_selected = -1;
    explorer_scroll = 0;
    explorer_last_click_index = -1;
    explorer_last_click_ticks = 0;
    explorer_dragging = 0;
    explorer_drag_index = -1;
    explorer_drag_moved = 0;
    explorer_path = explorer_current_path();
    return 1;
}

static void explorer_keyboard_move_selection(int delta) {
    app_window_t *app = app_slot(APP_EXPLORER);
    int content_h;
    int visible_rows;
    int count;
    if (!app || !app->open || app->minimized) return;
    count = explorer_entry_count();
    if (count <= 0) {
        explorer_selected = -1;
        explorer_scroll = 0;
        needs_redraw = 1;
        return;
    }
    content_h = app->frame.h - 56;
    visible_rows = (content_h - 90) / 18;
    if (visible_rows > 6) visible_rows = 6;
    if (visible_rows < 2) visible_rows = 2;
    if (explorer_selected < 0) explorer_selected = delta > 0 ? 0 : count - 1;
    else explorer_selected += delta;
    if (explorer_selected < 0) explorer_selected = 0;
    if (explorer_selected >= count) explorer_selected = count - 1;
    if (explorer_selected < explorer_scroll) explorer_scroll = explorer_selected;
    if (explorer_selected >= explorer_scroll + visible_rows) explorer_scroll = explorer_selected - visible_rows + 1;
    if (explorer_scroll < 0) explorer_scroll = 0;
    if (explorer_scroll > count - visible_rows) explorer_scroll = count - visible_rows;
    if (explorer_scroll < 0) explorer_scroll = 0;
    explorer_last_click_index = explorer_selected;
    explorer_last_click_ticks = 0;
    explorer_dragging = 0;
    explorer_drag_index = -1;
    explorer_drag_moved = 0;
    needs_redraw = 1;
}

static void explorer_keyboard_open_selected(void) {
    const explorer_entry_t *entry;
    if (explorer_selected < 0 || explorer_selected >= explorer_entry_count()) return;
    entry = explorer_entry_at(explorer_selected);
    if (!entry) return;
    if (entry->is_dir && entry->path) {
        explorer_open_path(entry->path);
        explorer_path = explorer_current_path();
        explorer_selected = -1;
        explorer_scroll = 0;
        explorer_last_click_index = -1;
        explorer_last_click_ticks = 0;
        explorer_dragging = 0;
        explorer_drag_index = -1;
        explorer_drag_moved = 0;
    }
    needs_redraw = 1;
}

static void explorer_copy_selected_file(int cut) {
    int i = 0;
    explorer_file_clipboard_count = 0;
    if (explorer_is_virtual_path(explorer_current_path())) return;
    for (i = 0; i < explorer_entry_count(); i++) {
        if (explorer_is_selected(i)) {
            const explorer_entry_t *entry = explorer_entry_at(i);
            if (entry && entry->path && explorer_file_clipboard_count < MAX_CLIPBOARD) {
                int c = explorer_file_clipboard_count++;
                desktop_copy_name(explorer_file_clipboard_path[c], sizeof(explorer_file_clipboard_path[c]), entry->path);
                desktop_copy_name(explorer_file_clipboard_name[c], sizeof(explorer_file_clipboard_name[c]), entry->name ? entry->name : "");
                explorer_file_clipboard_is_dir[c] = entry->is_dir ? 1 : 0;
            }
        }
    }
    explorer_file_clipboard_cut = cut ? 1 : 0;
    explorer_file_clipboard_set = explorer_file_clipboard_count > 0 ? 1 : 0;
    needs_redraw = 1;
}

static int explorer_path_exists(const char *path) {
    int count;
    int i = 0;
    const fs_entry_t *entry;
    char dir[64];
    char name[128];
    int slash = -1;
    int p = 0;
    if (!path || !path[0]) return 0;
    for (i = 0; path[i]; i++) if (path[i] == '/') slash = i;
    if (slash <= 0) return 0;
    for (i = 0; i < slash && i + 1 < (int)sizeof(dir); i++) dir[i] = path[i];
    dir[i] = 0;
    for (i = slash + 1; path[i] && p + 1 < (int)sizeof(name); i++) name[p++] = path[i];
    name[p] = 0;
    if (!name[0]) return 0;
    count = fs_count_in_dir(dir);
    for (i = 0; i < count; i++) {
        entry = fs_entry_in_dir(dir, i);
        if (!entry || !entry->used) continue;
        if (entry->name[0]) {
            int j = 0;
            while (name[j] && entry->name[j] && name[j] == entry->name[j]) j++;
            if (!name[j] && !entry->name[j]) return 1;
        }
    }
    return 0;
}

static int explorer_build_unique_paste_path(const char *dir, const char *name, char *out, int out_size) {
    char final_name[32];
    char stem[32];
    char ext[16];
    int i = 0;
    int j = 0;
    int dot = -1;
    int suffix = 1;
    int stem_len = 0;
    int ext_len = 0;
    if (!dir || !name || !name[0] || !out || out_size <= 0) return 0;
    while (name[i]) {
        if (name[i] == '.') dot = i;
        i++;
    }
    if (dot > 0) {
        while (name[stem_len] && stem_len < dot && stem_len + 1 < (int)sizeof(stem)) {
            stem[stem_len] = name[stem_len];
            stem_len++;
        }
        stem[stem_len] = 0;
        i = dot;
        while (name[i] && ext_len + 1 < (int)sizeof(ext)) ext[ext_len++] = name[i++];
        ext[ext_len] = 0;
    } else {
        while (name[stem_len] && stem_len + 1 < (int)sizeof(stem)) {
            stem[stem_len] = name[stem_len];
            stem_len++;
        }
        stem[stem_len] = 0;
        ext[0] = 0;
    }
    while (dir[j] && j + 1 < out_size) { out[j] = dir[j]; j++; }
    if (j > 0 && out[j - 1] != '/' && j + 1 < out_size) out[j++] = '/';
    i = 0;
    while (name[i] && j + 1 < out_size) { out[j++] = name[i++]; }
    out[j] = 0;
    if (!explorer_path_exists(out)) return 1;
    while (suffix < 100) {
        int p = 0;
        int n = 0;
        while (stem[n] && p + 1 < (int)sizeof(final_name)) final_name[p++] = stem[n++];
        if (p + 2 < (int)sizeof(final_name)) final_name[p++] = '-';
        if (suffix >= 10 && p + 1 < (int)sizeof(final_name)) final_name[p++] = (char)('0' + ((suffix / 10) % 16));
        if (p + 1 < (int)sizeof(final_name)) final_name[p++] = (char)('0' + (suffix % 16));
        n = 0;
        while (ext[n] && p + 1 < (int)sizeof(final_name)) final_name[p++] = ext[n++];
        final_name[p] = 0;
        j = 0;
        while (dir[j] && j + 1 < out_size) { out[j] = dir[j]; j++; }
        if (j > 0 && out[j - 1] != '/' && j + 1 < out_size) out[j++] = '/';
        n = 0;
        while (final_name[n] && j + 1 < out_size) out[j++] = final_name[n++];
        out[j] = 0;
        if (!explorer_path_exists(out)) return 1;
        suffix++;
    }
    return 0;
}

static void explorer_finish_paste_at(const char *target_dir, const char *target_path) {
    if (!target_dir || !target_dir[0] || !target_path || !target_path[0]) return;
    if (explorer_file_clipboard_cut) {
        if (!fs_rename_at(explorer_file_clipboard_path[0], target_path)) return;
        desktop_copy_name(explorer_file_clipboard_path[0], sizeof(explorer_file_clipboard_path[0]), target_path);
        explorer_file_clipboard_set = 0;
        explorer_file_clipboard_cut = 0;
        explorer_file_clipboard_is_dir[0] = 0;
        explorer_file_clipboard_name[0][0] = 0;
        explorer_file_clipboard_path[0][0] = 0;
    } else {
        if (!fs_copy_at(explorer_file_clipboard_path[0], target_path)) return;
    }
    explorer_paste_conflict_open = 0;
    explorer_paste_target_dir[0] = 0;
    explorer_paste_target_path[0] = 0;
    explorer_open_path(target_dir);
    explorer_path = explorer_current_path();
    needs_redraw = 1;
}

static void explorer_set_wallpaper_selected(void) {
    if (explorer_context_index >= 0) {
        const explorer_entry_t *entry = explorer_entry_at(explorer_context_index);
        if (entry && entry->path) {
            int i = 0;
            while (entry->path[i] && i < 127) {
                wallpaper_path[i] = entry->path[i];
                i++;
            }
            wallpaper_path[i] = 0;
            if (explorer_is_video_file(entry->name)) {
                wallpaper_source = 3;
            } else {
                wallpaper_source = 1;
            }
            wallpaper_cache_dirty = 1;
            settings_save_to_disk();
            needs_redraw = 1;
        }
    }
}

static void explorer_delete_selected_files(void) {
    int i = 0;
    int deleted = 0;
    if (explorer_is_virtual_path(explorer_current_path())) return;
    for (i = 0; i < explorer_entry_count(); i++) {
        if (explorer_is_selected(i)) {
            const explorer_entry_t *entry = explorer_entry_at(i);
            if (entry && entry->path) {
                char trash_path[256];
                int tk = 0;
                trash_path[tk++]='/'; trash_path[tk++]='T'; trash_path[tk++]='r'; trash_path[tk++]='a'; trash_path[tk++]='s'; trash_path[tk++]='h'; trash_path[tk++]='/';
                int ni = 0;
                while (entry->name[ni] && tk < 255) { trash_path[tk++] = entry->name[ni++]; }
                trash_path[tk] = 0;
                extern int fs_rename_at(const char *old_path, const char *new_path);
                fs_rename_at(entry->path, trash_path);
                deleted = 1;
            }
        }
    }
    if (deleted) {
        explorer_open_path(explorer_current_path());
        explorer_path = explorer_current_path();
        explorer_selected = -1;
        explorer_clear_multi_selection();
        needs_redraw = 1;
    }
}

static void explorer_paste_file(void) {
    const char *target_dir = explorer_current_path();
    char fs_dir[256];
    int c;
    if (!explorer_file_clipboard_set || !target_dir || !target_dir[0] || explorer_file_clipboard_count <= 0) return;
    if (explorer_is_virtual_path(target_dir)) return;
    if (target_dir[0] == 'F') {
        if (target_dir[0] == 'F' && target_dir[1] == 'i' && target_dir[2] == 'r' && target_dir[3] == 's' && target_dir[4] == 't' && target_dir[5] == 'O' && target_dir[6] == 'S' && target_dir[7] == '/' && target_dir[8]) {
            int i = 7;
            int p = 0;
            while (target_dir[i] && p + 1 < (int)sizeof(fs_dir)) fs_dir[p++] = target_dir[i++];
            fs_dir[p] = 0;
        } else return;
    } else {
        desktop_copy_name(fs_dir, sizeof(fs_dir), target_dir);
    }
    gui_progress_set(0, 0, 100);
    for (c = 0; c < explorer_file_clipboard_count; c++) {
        char unique_path[256];
        if (!explorer_build_unique_paste_path(fs_dir, explorer_file_clipboard_name[c], unique_path, sizeof(unique_path))) continue;
        if (explorer_path_exists(unique_path)) {
            // Overwrite for multi-select to avoid blocking loop
            fs_delete_at(unique_path);
        }
        
        if (explorer_file_clipboard_cut) {
            fs_rename_at(explorer_file_clipboard_path[c], unique_path);
        } else {
            fs_copy_at(explorer_file_clipboard_path[c], unique_path);
        }
    }
    if (explorer_file_clipboard_cut) {
        explorer_file_clipboard_set = 0;
        explorer_file_clipboard_cut = 0;
        explorer_file_clipboard_count = 0;
    }
    gui_progress_set(100, 0, 100);
    explorer_open_path(explorer_current_path());
    needs_redraw = 1;
}

static void desktop_paste_file(void) {
    int c;
    if (!explorer_file_clipboard_set || explorer_file_clipboard_count <= 0) return;
    gui_progress_set(0, 0, 100);
    for (c = 0; c < explorer_file_clipboard_count; c++) {
        char unique_path[256];
        if (!explorer_build_unique_paste_path("/Desktop", explorer_file_clipboard_name[c], unique_path, sizeof(unique_path))) continue;
        if (explorer_path_exists(unique_path)) {
            fs_delete_at(unique_path);
        }
        if (explorer_file_clipboard_cut) {
            fs_rename_at(explorer_file_clipboard_path[c], unique_path);
        } else {
            fs_copy_at(explorer_file_clipboard_path[c], unique_path);
        }
    }
    if (explorer_file_clipboard_cut) {
        explorer_file_clipboard_set = 0;
        explorer_file_clipboard_cut = 0;
        explorer_file_clipboard_count = 0;
    }
    gui_progress_set(100, 0, 100);
    sync_desktop_icons_from_fs();
    needs_redraw = 1;
}

static void handle_brand_menu_click(int mx, int my) {
    if (point_in_rect(mx, my, top_updates_item)) {
        app_window_t *app = app_slot(APP_SETTINGS);
        open_app(APP_SETTINGS);
        if (app) {
            app->frame.x = 190;
            app->frame.y = 78;
            sync_app_window(app);
        }
        brand_menu_open = 0;
    } else if (point_in_rect(mx, my, top_system_info_item)) {
        main_window_open = 1;
        main_window_minimized = 0;
        brand_menu_open = 0;
    } else if (point_in_rect(mx, my, top_logout_item)) {
        gui_logout();
    } else if (point_in_rect(mx, my, top_sleep_item)) {
        // Mock sleep: just turn screen black for now
        gui_mode = GUI_MODE_LOGIN;
    } else if (point_in_rect(mx, my, top_reboot_item)) {
        extern void outb(unsigned short port, unsigned char data);
        extern unsigned char inb(unsigned short port);
        extern void serial_write(const char*);
        serial_write("[PWR] Reboot\n");
        unsigned char good = 0x02;
        while (good & 0x02) good = inb(0x64);
        outb(0x64, 0xFE);
        outb(0x0CF9, 0x02);
        outb(0x0CF9, 0x06);
        while(1) __asm__ volatile("cli; hlt"); 
    } else if (point_in_rect(mx, my, top_shutdown_item)) {
        extern void outw(unsigned short port, unsigned short data);
        extern void serial_write(const char*);
        serial_write("[PWR] Shutdown\n");
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000); 
        outw(0x4004, 0x3400);
        outw(0x8900, 0x5307);
        outw(0x8900, 0x5301);
        outw(0x8900, 0x2000);
        while(1) __asm__ volatile("cli; hlt"); 
    }
    needs_redraw = 1;
}

static void handle_app_menu_click(int mx, int my) {
    app_window_t *app = active_app_window();
    rect_t row1 = (rect_t){top_app_menu_rect.x + 6, top_app_menu_rect.y + 6, top_app_menu_rect.w - 12, 22, 0};
    rect_t row2 = (rect_t){top_app_menu_rect.x + 6, top_app_menu_rect.y + 30, top_app_menu_rect.w - 12, 22, 0};
    rect_t row3 = (rect_t){top_app_menu_rect.x + 6, top_app_menu_rect.y + 54, top_app_menu_rect.w - 12, 22, 0};

    if (!app) {
        if (point_in_rect(mx, my, row1)) {
            main_window_open = 1;
            main_window_minimized = 0;
        } else if (point_in_rect(mx, my, row2)) {
            main_window_minimized = 1;
        } else if (point_in_rect(mx, my, row3)) {
            main_window_open = 1;
            main_window_minimized = 0;
        }
    } else {
        if (point_in_rect(mx, my, row1)) {
            if (app->kind == APP_MEDIA_PLAYER) {
                media_player_handle_menu(3, 0);
            } else {
                main_window_open = 1;
                main_window_minimized = 0;
            }
        } else if (point_in_rect(mx, my, row2)) {
            begin_window_transition((int)(app - app_windows), 1);
            app->minimized = 1;
        } else if (point_in_rect(mx, my, row3)) {
            if (app->kind == APP_TEXT_EDIT || app->kind == APP_EXPLORER || app->kind == APP_MEDIA_PLAYER) set_app_window_maximized(app, !app->maximized);
            else app->open = 0;
        }
    }
    app_menu_open = 0;
    needs_redraw = 1;
}

static void handle_file_menu_click(int mx, int my) {
    app_window_t *app = active_app_window();
    rect_t row1 = (rect_t){top_file_menu_rect.x + 6, top_file_menu_rect.y + 6, top_file_menu_rect.w - 12, 22, 0};
    rect_t row2 = (rect_t){top_file_menu_rect.x + 6, top_file_menu_rect.y + 30, top_file_menu_rect.w - 12, 22, 0};
    rect_t row3 = (rect_t){top_file_menu_rect.x + 6, top_file_menu_rect.y + 54, top_file_menu_rect.w - 12, 22, 0};
    rect_t row4 = (rect_t){top_file_menu_rect.x + 6, top_file_menu_rect.y + 78, top_file_menu_rect.w - 12, 22, 0};
    rect_t row5 = (rect_t){top_file_menu_rect.x + 6, top_file_menu_rect.y + 102, top_file_menu_rect.w - 12, 22, 0};
    rect_t row6 = (rect_t){top_file_menu_rect.x + 6, top_file_menu_rect.y + 126, top_file_menu_rect.w - 12, 22, 0};

    if (!app) {
        file_menu_open = 0;
        needs_redraw = 1;
        return;
    }

    if (point_in_rect(mx, my, row2)) {
        open_new_window(app->kind);
        file_menu_open = 0;
        needs_redraw = 1;
        return;
    }

    if (app->kind == APP_EXPLORER) {
        if (point_in_rect(mx, my, row1)) {
            if (app->tab_count < MAX_WINDOW_TABS) {
                int new_idx = app->tab_count;
                app->tabs[new_idx].used = 1;
                app->tabs[new_idx].scroll = 0;
                app->tabs[new_idx].cursor = 0;
                const char *def = "FirstOS";
                int pi = 0; while (def[pi] && pi < 127) { app->tabs[new_idx].path[pi] = def[pi]; pi++; }
                app->tabs[new_idx].path[pi] = 0;
                char tbuf[32]; int ti = 0; const char *tprefix = "Explorer ";
                while (tprefix[ti] && ti < 20) { tbuf[ti] = tprefix[ti]; ti++; }
                tbuf[ti++] = '0' + (new_idx + 1); tbuf[ti] = 0;
                int j = 0; while (tbuf[j] && j < 31) { app->tabs[new_idx].title[j] = tbuf[j]; j++; }
                app->tabs[new_idx].title[j] = 0;
                app->active_tab = new_idx;
                app->tab_count++;
                explorer_open_path(app->tabs[new_idx].path);
            }
        } else if (point_in_rect(mx, my, row3)) {
            explorer_create_folder_here("untitled");
            explorer_open_path(explorer_current_path());
            explorer_path = explorer_current_path();
        } else if (point_in_rect(mx, my, row4)) {
            handle_explorer_open_selected();
        } else if (point_in_rect(mx, my, row5)) {
            app->open = 0;
        }
    } else if (app->kind == APP_TEXT_EDIT) {
        if (point_in_rect(mx, my, row1)) {
            if (app->tab_count < MAX_WINDOW_TABS) {
                int new_idx = app->tab_count;
                app->tabs[new_idx].used = 1;
                app->tabs[new_idx].scroll = 0;
                app->tabs[new_idx].cursor = 0;
                char tbuf[32]; int ti = 0; const char *tprefix = "Doc ";
                while (tprefix[ti] && ti < 20) { tbuf[ti] = tprefix[ti]; ti++; }
                tbuf[ti++] = '0' + (new_idx + 1); tbuf[ti] = 0;
                int j = 0; while (tbuf[j] && j < 31) { app->tabs[new_idx].title[j] = tbuf[j]; j++; }
                app->tabs[new_idx].title[j] = 0;
                app->active_tab = new_idx;
                app->tab_count++;
                text_edit_new_file();
            }
        } else if (point_in_rect(mx, my, row3)) {
            file_dialog_show(APP_TEXT_EDIT, FILE_DIALOG_OPEN, "", "/Desktop");
        } else if (point_in_rect(mx, my, row4)) {
            file_dialog_show(APP_TEXT_EDIT, FILE_DIALOG_SAVE, "untitled.txt", "/Desktop");
        } else if (point_in_rect(mx, my, row5)) {
            if (!editor_saved && editor_length > 0) {
                text_edit_unsaved_dialog_open = 1;
                text_edit_closing_app_index = active_app_index;
            } else {
                app->open = 0;
            }
        }
    } else if (app->kind == APP_MEDIA_PLAYER) {
        if (point_in_rect(mx, my, row1)) file_dialog_show(APP_MEDIA_PLAYER, FILE_DIALOG_IMPORT, "", "/Audio");
        else if (point_in_rect(mx, my, row3)) media_player_close();
    } else if (app->kind == APP_IDE) {
        if (point_in_rect(mx, my, row1)) {
            file_dialog_show(APP_IDE, FILE_DIALOG_OPEN, "", "/Desktop");
        }
        else if (point_in_rect(mx, my, row3)) { file_dialog_show(APP_IDE, FILE_DIALOG_OPEN, "", "/Desktop"); }
        else if (point_in_rect(mx, my, row4)) { file_dialog_show(APP_IDE, FILE_DIALOG_SAVE, "script.fl", "/Desktop"); }
        else if (point_in_rect(mx, my, row5)) {
            ide_saving_ui = 2; // Special flag for building .app
            ide_save_dialog_open = 1;
            explorer_picker_mode = 1;
            int h=0; while("app.app"[h]) { editor_save_name[h] = "app.app"[h]; h++; } editor_save_name[h]=0;
            open_app(APP_EXPLORER);
        }
        else if (point_in_rect(mx, my, row6)) app->open = 0;
    } else {
        if (point_in_rect(mx, my, row3)) app->open = 0;
    }

    file_menu_open = 0;
    needs_redraw = 1;
}

static void handle_edit_menu_click(int mx, int my) {
    app_window_t *app = active_app_window();
    rect_t row1 = (rect_t){top_edit_menu_rect.x + 6, top_edit_menu_rect.y + 6, top_edit_menu_rect.w - 12, 22, 0};
    rect_t row2 = (rect_t){top_edit_menu_rect.x + 6, top_edit_menu_rect.y + 30, top_edit_menu_rect.w - 12, 22, 0};
    rect_t row3 = (rect_t){top_edit_menu_rect.x + 6, top_edit_menu_rect.y + 54, top_edit_menu_rect.w - 12, 22, 0};
    rect_t row4 = (rect_t){top_edit_menu_rect.x + 6, top_edit_menu_rect.y + 78, top_edit_menu_rect.w - 12, 22, 0};

    if (!app) {
        edit_menu_open = 0;
        needs_redraw = 1;
        return;
    }

    if (app->kind == APP_TEXT_EDIT) {
        int row = (my - (top_edit_menu_rect.y + 6)) / 24;
        if (row == 0) text_edit_undo();
        else if (row == 1) text_edit_copy_selection();
        else if (row == 2) text_edit_paste_clipboard();
        else if (row == 3) text_edit_delete();
        else if (row == 4) text_edit_set_align(0); /* LTR */
        else if (row == 5) text_edit_set_align(2); /* RTL */
        else if (row == 6) text_edit_toggle_bold();
        else if (row == 7) text_edit_toggle_italic();
        else if (row == 8) text_edit_toggle_underline();
        else if (row == 9) text_edit_insert_image();
        else if (row == 10) text_edit_insert_date();
        else if (row == 11) text_edit_insert_time();
        else if (row == 12) text_edit_insert_shape();
        else if (row == 13) text_edit_insert_chart();
        edit_menu_open = 0;
        needs_redraw = 1;
        return;
    }

    if (app->kind == APP_MEDIA_PLAYER) {
        if (point_in_rect(mx, my, row1)) media_player_play();
        else if (point_in_rect(mx, my, row2)) media_player_stop();
        else if (point_in_rect(mx, my, row3)) media_player_pause();
        else if (point_in_rect(mx, my, row4)) media_player_rewind();
        edit_menu_open = 0;
        needs_redraw = 1;
        return;
    }

    if (point_in_rect(mx, my, row1)) {
        if (app->kind == APP_EXPLORER) explorer_copy_selected_file(0);
        else if (app->kind == APP_TERMINAL) { extern void terminal_copy(void); terminal_copy(); }
    } else if (point_in_rect(mx, my, row2)) {
        if (app->kind == APP_EXPLORER) explorer_paste_file();
        else if (app->kind == APP_TERMINAL) { extern void terminal_paste(void); terminal_paste(); }
    } else if (point_in_rect(mx, my, row3)) {
        if (app->kind == APP_IDE) text_edit_undo();
        else if (app->kind == APP_EXPLORER) explorer_delete_selected_files();
        else if (app->kind == APP_TERMINAL) { extern void terminal_clear(void); terminal_clear(); }
    } else if (point_in_rect(mx, my, row4)) {
        if (app->kind == APP_IDE) { ide_designer_mode = !ide_designer_mode; }
    }

    edit_menu_open = 0;
    needs_redraw = 1;
}

static void handle_view_menu_click(int mx, int my) {
    (void)mx;
    int row = (my - (top_view_menu_rect.y + 6)) / 24;
    extern void text_edit_set_page_format(int);
    if (row == 0) text_edit_set_page_format(1); /* A4 */
    else if (row == 1) text_edit_set_page_format(2); /* A5 */
    else if (row == 2) text_edit_set_page_format(3); /* A6 */
    else if (row == 3) text_edit_set_page_format(4); /* Letter */
    else if (row == 4) text_edit_set_page_format(0); /* Full Window */
    view_menu_open = 0;
    needs_redraw = 1;
}


static void handle_explorer_open_selected(void) {
    const explorer_entry_t *entry;
    if (explorer_selected < 0 || explorer_selected >= explorer_entry_count()) return;
    entry = explorer_entry_at(explorer_selected);
    if (!entry || !entry->path) return;
    if (entry->is_dir) {
        explorer_open_path(entry->path);
        explorer_selected = -1;
    } else {
        /* Route by file type */
        if (explorer_is_image_file(entry->name)) {
            /* Open in Preview */
            preview_open(entry->path);
            open_app(APP_PREVIEW);
        } else if (explorer_is_text_file(entry->name)) {

            /* Open in Text Edit */
            text_edit_open_file(entry->path);
            open_app(APP_TEXT_EDIT);
        } else if (explorer_is_audio_file(entry->name) || explorer_is_video_file(entry->name)) {
            media_player_open_file(entry->path);
            open_app(APP_MEDIA_PLAYER);
        } else {
            int len = 0; while (entry->name && entry->name[len]) len++;
            if (len > 4 && entry->name[len-3] == 'a' && entry->name[len-2] == 'p' && entry->name[len-1] == 'p') {
                extern int explorer_is_virtual_path(const char*);
                if (explorer_is_virtual_path(entry->path)) {
                    if (entry->name[0] == 'S') open_app(APP_SETTINGS);
                    else if (entry->name[0] == 'E') open_app(APP_EXPLORER);
                    else if (entry->name[0] == 'T' && entry->name[1] == 'e' && entry->name[2] == 'r') open_app(APP_TERMINAL);
                    else if (entry->name[0] == 'T' && entry->name[1] == 'a' && entry->name[2] == 's') open_app(APP_TASKMAN);
                    else if (entry->name[0] == 'T') open_app(APP_TEXT_EDIT);
                } else {
                    char out[1024]; out[0] = 0;
                    char src[4096];
                    int bytes = fs_read_at(entry->path, src, 4095);
                    if (bytes >= 0) src[bytes] = 0; else src[0] = 0;
                    extern int firstlang_run(const char*, char*, int);
                    firstlang_run(src, out, 1024);
                    extern int firstlang_has_ui(void);
                    if (!firstlang_has_ui() && out[0]) {
                        open_app(APP_TERMINAL);
                        extern void terminal_print(const char *text);
                        terminal_print(out);
                        terminal_print("\n");
                        focus_app_window(APP_TERMINAL - 1);
                    }
                }
            }
        }
    }
}

static void begin_explorer_rename_selected(void) {
    const explorer_entry_t *entry;
    int i = 0;
    if (explorer_selected < 0 || explorer_selected >= explorer_entry_count()) return;
    entry = explorer_entry_at(explorer_selected);
    if (!entry || !entry->name || !entry->path) return;
    while (entry->name[i] && i + 1 < (int)sizeof(explorer_rename_name)) {
        explorer_rename_name[i] = entry->name[i];
        i++;
    }
    explorer_rename_name[i] = 0;
    explorer_rename_cursor = i;
    explorer_rename_index = explorer_selected;
    explorer_rename_mode = 1;
    needs_redraw = 1;
}

static void begin_desktop_item_rename(int index);
static void desktop_copy_name(char *dst, int max, const char *src) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}
static int desktop_item_cmp(const desktop_item_t *a, const desktop_item_t *b) {
    if (desktop_sort_mode == DESKTOP_SORT_NAME) {
        int i = 0;
        while (a->name[i] && b->name[i]) {
            char ca = a->name[i];
            char cb = b->name[i];
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
            i++;
        }
        return (int)(unsigned char)a->name[i] - (int)(unsigned char)b->name[i];
    } else if (desktop_sort_mode == DESKTOP_SORT_KIND) {
        if (a->is_dir != b->is_dir) return b->is_dir - a->is_dir;
        const char *ext_a = "";
        const char *ext_b = "";
        for (int i = 0; a->name[i]; i++) if (a->name[i] == '.') ext_a = &a->name[i + 1];
        for (int i = 0; b->name[i]; i++) if (b->name[i] == '.') ext_b = &b->name[i + 1];
        int i = 0;
        while (ext_a[i] && ext_b[i]) {
            char ca = ext_a[i]; char cb = ext_b[i];
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
            i++;
        }
        if (ext_a[i] != ext_b[i]) return (int)(unsigned char)ext_a[i] - (int)(unsigned char)ext_b[i];
        i = 0;
        while (a->name[i] && b->name[i]) {
            char ca = a->name[i]; char cb = b->name[i];
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
            i++;
        }
        return (int)(unsigned char)a->name[i] - (int)(unsigned char)b->name[i];
    } else if (desktop_sort_mode == DESKTOP_SORT_DATE) {
        if (a->date_order != b->date_order) return (a->date_order > b->date_order) ? 1 : -1;
    }
    return 0;
}

void sort_desktop_items(void) {
    if (desktop_sort_mode == DESKTOP_SORT_NONE || desktop_item_count <= 1) return;
    for (int i = 0; i < desktop_item_count - 1; i++) {
        for (int j = 0; j < desktop_item_count - i - 1; j++) {
            if (desktop_item_cmp(&desktop_items[j], &desktop_items[j + 1]) > 0) {
                desktop_item_t tmp = desktop_items[j];
                desktop_items[j] = desktop_items[j + 1];
                desktop_items[j + 1] = tmp;
            }
        }
    }
}

static void desktop_cycle_wallpaper(void) {
    wallpaper_id = (wallpaper_id + 1) % 5;
    wallpaper_source = 0;
    wallpaper_cache_dirty = 1;
    settings_save_to_disk();
    needs_redraw = 1;
}

static int gui_str_eq(const char *a, const char *b) {
    int i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void sync_desktop_icons_from_fs(void) {
    int i = 0, k;
    int count;
    const fs_entry_t *entry;
    desktop_item_t old_items[32];
    int old_count = desktop_item_count;
    if (!fs_ready()) return;
    for (k = 0; k < old_count && k < 32; k++) old_items[k] = desktop_items[k];

    fs_mkdir("/Desktop");
    count = fs_count_in_dir("/Desktop");
    if (count > 32) count = 32;
    desktop_item_count = 0;
    for (i = 0; i < count; i++) {
        entry = fs_entry_in_dir("/Desktop", i);
        if (!entry || !entry->used) continue;
        desktop_items[desktop_item_count].used = 1;
        desktop_items[desktop_item_count].is_dir = entry->is_dir;
        desktop_items[desktop_item_count].rename_mode = 0;
        desktop_items[desktop_item_count].cursor = 0;
        desktop_items[desktop_item_count].last_click = 0;
        desktop_items[desktop_item_count].last_click_ticks = 0;
        desktop_items[desktop_item_count].date_order = entry->offset;
        desktop_items[desktop_item_count].has_custom_pos = 0;
        desktop_items[desktop_item_count].custom_x = 0;
        desktop_items[desktop_item_count].custom_y = 0;
        for (k = 0; k < old_count; k++) {
            if (gui_str_eq(old_items[k].name, entry->name) && old_items[k].has_custom_pos) {
                desktop_items[desktop_item_count].has_custom_pos = 1;
                desktop_items[desktop_item_count].custom_x = old_items[k].custom_x;
                desktop_items[desktop_item_count].custom_y = old_items[k].custom_y;
                break;
            }
        }
        desktop_copy_name(desktop_items[desktop_item_count].name, sizeof(desktop_items[desktop_item_count].name), entry->name);
        desktop_copy_name(desktop_items[desktop_item_count].path, sizeof(desktop_items[desktop_item_count].path), entry->path);
        desktop_item_count++;
    }
    if (desktop_sort_mode != DESKTOP_SORT_NONE) {
        sort_desktop_items();
    }
    if (desktop_selected >= desktop_item_count) desktop_selected = -1;
    if (desktop_context_index >= desktop_item_count) desktop_context_index = -1;
}

static int build_unique_desktop_path(const char *base, const char *stem, const char *ext, char *out, int out_size) {
    int suffix = 0;
    int attempts;
    if (!base || !stem || !out || out_size <= 0) return 0;
    for (attempts = 0; attempts < 1000; attempts++) {
        int p = 0;
        int n = 0;
        int t = 0;
        int e = 0;
        while (base[p] && p + 1 < out_size) { out[p] = base[p]; p++; }
        if (p > 0 && out[p - 1] != '/' && p + 1 < out_size) out[p++] = '/';
        while (stem[n] && p + 1 < out_size) out[p++] = stem[n++];
        if (suffix > 0) {
            char digits[12];
            int d = 0;
            int value = suffix;
            while (value > 0 && d + 1 < (int)sizeof(digits)) {
                digits[d++] = (char)('0' + (value % 16));
                value /= 10;
            }
            while (d > 0 && p + 1 < out_size) out[p++] = digits[--d];
        }
        if (ext) while (ext[e] && p + 1 < out_size) out[p++] = ext[e++];
        out[p] = 0;
        if (fs_create_at(out, ext != 0 ? 0 : 1)) return 1;
        suffix++;
    }
    return 0;
}

static void create_desktop_text_file(void) {
    int created;
    const char *current = explorer_current_path();
    const char *base = "/Desktop";
    char path[256];
    if (!fs_ready()) return;
    if (current && current[0] == '/') base = current;
    fs_mkdir("/Desktop");
    created = build_unique_desktop_path(base, "untitled", ".txt", path, (int)sizeof(path));
    if (!created) return;
    serial_write("[gui] create file path=");
    serial_write(path);
    serial_write("\n");
    sync_desktop_icons_from_fs();
    if (explorer_current_path() && (explorer_current_path()[0] == '/' || explorer_current_path()[0] == 'F')) explorer_open_path(explorer_current_path());
    needs_redraw = 1;
}

static void create_desktop_folder(void) {
    int created;
    const char *current = explorer_current_path();
    const char *base = "/Desktop";
    char path[256];
    if (!fs_ready()) return;
    if (current && current[0] == '/') base = current;
    fs_mkdir("/Desktop");
    created = build_unique_desktop_path(base, "untitled", 0, path, (int)sizeof(path));
    if (!created) return;
    serial_write("[gui] create folder path=");
    serial_write(path);
    serial_write("\n");
    sync_desktop_icons_from_fs();
    if (explorer_current_path() && (explorer_current_path()[0] == '/' || explorer_current_path()[0] == 'F')) explorer_open_path(explorer_current_path());
    needs_redraw = 1;
}

static void draw_desktop_items(void) {
    int i = 0;
    int x = 24;
    int y = desktop_bar.h + 20;
    int cell_w = 140;
    int cell_h = 76;
    int icon_w = 48;
    int icon_h = 48;
    int thumb_sz = 40;

    if (desktop_icon_size == ICON_SIZE_SMALL) {
        cell_w = 104;
        cell_h = 66;
        icon_w = 36;
        icon_h = 36;
        thumb_sz = 30;
    } else if (desktop_icon_size == ICON_SIZE_LARGE) {
        cell_w = 180;
        cell_h = 96;
        icon_w = 64;
        icon_h = 64;
        thumb_sz = 54;
    }

    int usable_w = (int)fb.width - 270;
    int cols = usable_w / cell_w;
    if (cols < 1) cols = 1;
    for (i = 0; i < desktop_item_count; i++) {
        int item_x, item_y;
        if (desktop_items[i].has_custom_pos) {
            item_x = desktop_items[i].custom_x;
            item_y = desktop_items[i].custom_y;
        } else {
            int col = i % cols;
            int row = i / cols;
            item_x = x + col * cell_w;
            item_y = y + row * cell_h;
        }
        if (item_x + cell_w > (int)fb.width - 12) item_x = (int)fb.width - cell_w - 12;
        desktop_items[i].icon_rect = (rect_t){item_x, item_y, cell_w, cell_h, 0};

        int is_selected = (desktop_selected == i);
        int icon_x = item_x + (cell_w - icon_w) / 2;
        int icon_y = item_y + 4;

        if (is_selected) {
            fb_round_rect_alpha(icon_x - 3, icon_y - 3, icon_w + 6, icon_h + 6, 8, 0x2563EB, 110);
            fb_round_rect_border(icon_x - 3, icon_y - 3, icon_w + 6, icon_h + 6, 8, 0x60A5FA);
        }

        int ext = 0;
        int k = 0; while (desktop_items[i].name[k]) { if (desktop_items[i].name[k] == '.') ext = k + 1; k++; }
        int d_icon = desktop_items[i].is_dir ? ICON_FOLDER : ICON_FILE;
        int d_color = desktop_items[i].is_dir ? 0xD9C58F : 0xE8D7B9;
        if (!desktop_items[i].is_dir && ext > 0) {
            if (desktop_items[i].name[ext] == 'a' && desktop_items[i].name[ext+1] == 'p' && desktop_items[i].name[ext+2] == 'p') {
                d_icon = ICON_DISPLAY; d_color = 0x5A4D86;
            } else if (desktop_items[i].name[ext] == 'f' && desktop_items[i].name[ext+1] == 'l') {
                d_icon = ICON_FILE; d_color = 0x1F4730;
            } else if ((desktop_items[i].name[ext] == 't' || desktop_items[i].name[ext] == 'T') && (desktop_items[i].name[ext+1] == 'm' || desktop_items[i].name[ext+1] == 'M') && (desktop_items[i].name[ext+2] == 'l' || desktop_items[i].name[ext+2] == 'L')) {
                d_icon = ICON_TERMINAL; d_color = 0x1E4268;
            } else if (explorer_is_image_file(desktop_items[i].name)) {
                d_icon = ICON_FILE; d_color = 0xDDAA44;
            } else if (explorer_is_video_file(desktop_items[i].name) || explorer_is_audio_file(desktop_items[i].name)) {
                d_icon = ICON_DISPLAY; d_color = 0x44AADD;
            }
        }

        int is_media = !desktop_items[i].is_dir && (explorer_is_image_file(desktop_items[i].name) || explorer_is_video_file(desktop_items[i].name));
        if (is_media) {
            const file_thumbnail_t *thumb = thumbnail_get(desktop_items[i].path, thumb_sz, thumb_sz);
            if (thumb && thumb->pixels) {
                draw_thumbnail_card(icon_x, icon_y, icon_w, icon_h, thumb, is_selected);
            } else {
                fb_round_rect_alpha(icon_x, icon_y, icon_w, icon_h, 6, 0x141E2C, 190);
                fb_round_rect_border(icon_x, icon_y, icon_w, icon_h, 6, is_selected ? 0x60A5FA : 0x3E526A);
                font_icon(icon_x + (icon_w - 20) / 2, icon_y + (icon_h - 20) / 2, d_icon, d_color);
            }
        } else {
            fb_round_rect_alpha(icon_x, icon_y, icon_w, icon_h, 6, desktop_items[i].is_dir ? 0x2C2414 : 0x141E2C, 190);
            fb_round_rect_border(icon_x, icon_y, icon_w, icon_h, 6, is_selected ? 0x60A5FA : (desktop_items[i].is_dir ? 0x786034 : 0x3E526A));
            int is_browser = (!desktop_items[i].is_dir && gui_str_contains_ci(desktop_items[i].name, "browser"));
            if (is_browser) {
                if (!browser_ico_loaded) {
                    browser_ico_loaded = 1;
                    load_ico_from_path("/icons/browser.ico", browser_ico_pixels, &browser_ico_width, &browser_ico_height);
                }
                if (browser_ico_width > 0) {
                    draw_icon_scaled(browser_ico_pixels, browser_ico_width, browser_ico_height, icon_x + (icon_w - 32) / 2, icon_y + (icon_h - 32) / 2, 32, 32);
                } else {
                    font_icon(icon_x + (icon_w - 20) / 2, icon_y + (icon_h - 20) / 2, ICON_NETWORK, 0xFF7139);
                }
            } else {
                font_icon(icon_x + (icon_w - 20) / 2, icon_y + (icon_h - 20) / 2, d_icon, d_color);
            }
        }

        int label_y = icon_y + icon_h + 4;
        if (desktop_items[i].rename_mode) {
            int rw = cell_w - 12;
            int rx = item_x + (cell_w - rw) / 2;
            fb_rect(rx, label_y - 2, rw, 18, 0xF6F1E7);
            outline(rx, label_y - 2, rw, 18, 0x6C92C0);
            font_text_fit(rx + 4, label_y + 2, desktop_items[i].name, 0x24384F, rw - 8);
        } else {
            int nlen = 0; while (desktop_items[i].name[nlen]) nlen++;
            int text_px = nlen * 6;
            int pill_w = text_px + 14;
            if (pill_w > cell_w - 6) pill_w = cell_w - 6;
            if (pill_w < 36) pill_w = 36;
            int pill_x = item_x + (cell_w - pill_w) / 2;

            if (is_selected) {
                fb_round_rect(pill_x, label_y - 2, pill_w, 16, 8, 0x2563EB);
                fb_round_rect_border(pill_x, label_y - 2, pill_w, 16, 8, 0x93C5FD);
                font_text_fit_center(pill_x, label_y + 1, desktop_items[i].name, 0xFFFFFF, pill_w);
            } else {
                fb_round_rect_alpha(pill_x, label_y - 2, pill_w, 16, 8, 0x0F172A, 195);
                fb_round_rect_border(pill_x, label_y - 2, pill_w, 16, 8, 0x3A4D68);
                font_text_fit_center(pill_x, label_y + 1, desktop_items[i].name, 0xF8FAFC, pill_w);
            }
        }
    }
}

static void begin_desktop_item_rename(int index) {
    int i = 0;
    if (index < 0 || index >= desktop_item_count) return;
    while (desktop_items[index].name[i]) i++;
    desktop_items[index].cursor = i;
    desktop_items[index].rename_mode = 1;
    desktop_selected = index;
    needs_redraw = 1;
}

static void handle_start_menu_click(int mx, int my) {
    if (point_in_rect(mx, my, start_apps_item)) { open_app(APP_TERMINAL); start_menu_open = 0; }
    else if (point_in_rect(mx, my, start_browser_item)) {
        web_browser_app_launch(0);
        start_menu_open = 0;
    }
    else if (point_in_rect(mx, my, start_explorer_item)) { open_app(APP_EXPLORER); start_menu_open = 0; }
    else if (point_in_rect(mx, my, start_settings_item)) { open_app(APP_SETTINGS); start_menu_open = 0; }
    else if (point_in_rect(mx, my, start_media_item)) { open_app(APP_MEDIA_PLAYER); start_menu_open = 0; }
    else if (point_in_rect(mx, my, start_calc_item)) { open_app(APP_CALCULATOR); start_menu_open = 0; }
    else if (point_in_rect(mx, my, start_textedit_item)) { create_desktop_text_file(); start_menu_open = 0; }
        else if (point_in_rect(mx, my, start_ide_item)) { open_app(APP_IDE); start_menu_open = 0; }
    else if (point_in_rect(mx, my, start_taskman_item)) { 
        open_app(APP_TASKMAN); 
        start_menu_open = 0; 
    }
    else if (point_in_rect(mx, my, start_demo_item)) { 
        start_menu_open = 0;
        needs_redraw = 1;
        extern int firstlang_run_file(const char*, char*, int);
        static char out[1024];
        firstlang_run_file("/demo.fl", out, 1024);
    }
    else if (point_in_rect(mx, my, start_restart_item)) {
        extern void outb(unsigned short port, unsigned char data);
        extern unsigned char inb(unsigned short port);
        unsigned char good = 0x02;
        while (good & 0x02) good = inb(0x64);
        outb(0x64, 0xFE);
        outb(0x0CF9, 0x02);
        outb(0x0CF9, 0x06);
        while(1) __asm__ volatile("cli; hlt"); 
    } else if (point_in_rect(mx, my, start_shutdown_item)) {
        extern void outw(unsigned short port, unsigned short data);
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000); 
        outw(0x4004, 0x3400);
        outw(0x8900, 0x5307);
        outw(0x8900, 0x5301);
        outw(0x8900, 0x2000);
        while(1) __asm__ volatile("cli; hlt"); 
    }
}

static void settings_scrollbar_geometry(app_window_t *app, int *x, int *top, int *bottom, int *track, int *thumb) {
    int sx = app->frame.x + app->frame.w - 24;
    int st = app->frame.y + 48;
    int sh = app->frame.h - 66;
    int sb = st + sh;
    int strack = sh - 28;
    int sthumb = strack - 18;
    if (strack < 0) strack = 0;
    if (sthumb < 18) sthumb = 18;
    if (sthumb > strack) sthumb = strack;
    if (x) *x = sx;
    if (top) *top = st + 14;
    if (bottom) *bottom = sb - 14;
    if (track) *track = strack;
    if (thumb) *thumb = sthumb;
}

static int settings_scroll_hit(app_window_t *app, int mx, int my) {
    int sx, top, bottom;
    settings_scrollbar_geometry(app, &sx, &top, &bottom, 0, 0);
    return mx >= sx && mx <= sx + 12 && my >= top && my < bottom;
}

static void draw_window_scrollbar(app_window_t *app, int total_items, int visible_items, int scroll_index) {
    int sx;
    int top;
    int bottom;
    settings_scrollbar_geometry(app, &sx, &top, &bottom, 0, 0);
    if (bottom - top > 0) draw_scrollbar(sx, top - 14, bottom - top + 28, total_items, visible_items, scroll_index);
}

static void update_settings_scroll_drag(app_window_t *app, int my) {
    int top;
    int bottom;
    int track;
    int thumb;
    int usable;
    int pos;
    settings_scrollbar_geometry(app, 0, &top, &bottom, &track, &thumb);
    usable = track - thumb;
    pos = my - top - settings_scroll_drag_offset;
    if (usable <= 0) {
        settings_scroll = 0;
        needs_redraw = 1;
        return;
    }
    if (pos < 0) pos = 0;
    if (pos > usable) pos = usable;
    settings_scroll = (pos + usable / 2) / usable;
    if (settings_scroll < 0) settings_scroll = 0;
    if (settings_scroll > 1) settings_scroll = 1;
    needs_redraw = 1;
}

static int explorer_item_at(app_window_t *app, int mx, int my) {
    int content_x = app->frame.x + 10;
    int content_y = app->frame.y + 42 + 24;
    int content_w = app->frame.w - 20;
    int content_h = app->frame.h - 52 - 24;
    int header_h = 34;
    int toolbar_h = 34;
    int sidebar_w = 134;
    int status_h = 24;
    int preview_pane_w = (explorer_preview_pane_open && content_w >= 440) ? 170 : 0;
    int main_x = content_x + sidebar_w + 1;
    int main_w = content_w - sidebar_w - preview_pane_w - (preview_pane_w ? 2 : 1);
    int grid_y = content_y + header_h + toolbar_h + 14;
    int grid_h = content_h - header_h - toolbar_h - status_h - 18;
    int save_bar_h = editor_save_dialog_open ? 36 : 0;
    int card_w = main_w < 250 ? 82 : 96;
    int cell_h = 86;
    int icon_cols = (main_w - 24) / card_w;
    int visible_rows;
    int visible_items;
    int filtered_count = explorer_filtered_count();
    int i = 0;
    if (icon_cols < 1) icon_cols = 1;
    visible_rows = (grid_h - save_bar_h) / cell_h;
    if (visible_rows < 1) visible_rows = 1;
    visible_items = visible_rows * icon_cols;
    for (i = 0; i < visible_items && explorer_scroll + i < filtered_count; i++) {
        int col = i % icon_cols;
        int row = i / icon_cols;
        int tile_x = main_x + 12 + col * card_w;
        int tile_y = grid_y + row * cell_h;
        if (mx >= tile_x && mx < tile_x + card_w - 8 && my >= tile_y && my < tile_y + 72) return explorer_filtered_index_from_visible(explorer_scroll + i);
    }
    return -1;
}

static int is_text_or_doc_file(const char *name) {
    int len = 0; while (name[len]) len++;
    if (len >= 4 && (
        (name[len-4]=='.' && (name[len-3]=='t'||name[len-3]=='T') && (name[len-2]=='x'||name[len-2]=='X') && (name[len-1]=='t'||name[len-1]=='T')) ||
        (name[len-4]=='.' && (name[len-3]=='d'||name[len-3]=='D') && (name[len-2]=='o'||name[len-2]=='O') && (name[len-1]=='c'||name[len-1]=='C'))
    )) return 1;
    return 0;
}

static int explorer_entry_matches_search(const explorer_entry_t *entry) {
    int i = 0;
    int j;
    if (!entry || !entry->name) return 0;
    if (explorer_picker_mode == 2 && ide_save_dialog_open && !entry->is_dir) {
        int ext = 0; while(entry->name[ext]) ext++;
        while(ext > 0 && entry->name[ext-1] != '.') ext--;
        if (!(entry->name[ext] == 'f' && entry->name[ext+1] == 'l' && entry->name[ext+2] == 0)) return 0;
    }
    if ((explorer_picker_mode == 1 || explorer_picker_mode == 2) && editor_save_dialog_open && !ide_save_dialog_open && !entry->is_dir) {
        if (!is_text_or_doc_file(entry->name)) return 0;
    }
    if (!explorer_search_text[0]) return 1;
    while (entry->name[i]) {
        j = 0;
        while (explorer_search_text[j] && entry->name[i + j]) {
            char a = entry->name[i + j];
            char b = explorer_search_text[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            j++;
        }
        if (!explorer_search_text[j]) return 1;
        i++;
    }
    return 0;
}

static int explorer_filtered_index_from_visible(int visible_index) {
    int i = 0;
    int seen = 0;
    for (i = 0; i < explorer_entry_count(); i++) {
        const explorer_entry_t *entry = explorer_entry_at(i);
        if (!explorer_entry_matches_search(entry)) continue;
        if (seen == visible_index) return i;
        seen++;
    }
    return -1;
}

static int explorer_filtered_count(void) {
    int i = 0;
    int count = 0;
    for (i = 0; i < explorer_entry_count(); i++) {
        if (explorer_entry_matches_search(explorer_entry_at(i))) count++;
    }
    return count;
}

static int explorer_entry_match_start(const explorer_entry_t *entry) {
    int i = 0;
    int j;
    if (!explorer_search_text[0] || !entry || !entry->name) return -1;
    while (entry->name[i]) {
        j = 0;
        while (explorer_search_text[j] && entry->name[i + j]) {
            char a = entry->name[i + j];
            char b = explorer_search_text[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            j++;
        }
        if (!explorer_search_text[j]) return i;
        i++;
    }
    return -1;
}

static void explorer_focus_search_result(int step) {
    int count = explorer_filtered_count();
    if (count <= 0) {
        explorer_selected = -1;
        explorer_search_result_index = -1;
        explorer_scroll = 0;
        return;
    }
    if (explorer_search_result_index < 0 || explorer_search_result_index >= count) {
        explorer_search_result_index = 0;
    } else {
        explorer_search_result_index += step;
        if (explorer_search_result_index >= count) explorer_search_result_index = 0;
        if (explorer_search_result_index < 0) explorer_search_result_index = count - 1;
    }
    explorer_selected = explorer_filtered_index_from_visible(explorer_search_result_index);
    if (explorer_selected < 0) explorer_search_result_index = -1;
}


static void explorer_store_search_history(void) {
    int i, j, len = 0;
    char temp[32];
    if (!explorer_search_text[0]) return;
    while (explorer_search_text[len] && len + 1 < (int)sizeof(temp)) { temp[len] = explorer_search_text[len]; len++; }
    temp[len] = 0;
    for (i = 0; i < explorer_search_history_count; i++) {
        j = 0;
        while (temp[j] && explorer_search_history[i][j] && temp[j] == explorer_search_history[i][j]) j++;
        if (!temp[j] && !explorer_search_history[i][j]) {
            int k;
            for (k = i; k > 0; k--) {
                for (j = 0; j < 32; j++) explorer_search_history[k][j] = explorer_search_history[k - 1][j];
            }
            for (j = 0; j < 32; j++) explorer_search_history[0][j] = temp[j];
            return;
        }
    }
    if (explorer_search_history_count < 5) explorer_search_history_count++;
    for (i = explorer_search_history_count - 1; i > 0; i--) {
        for (j = 0; j < 32; j++) explorer_search_history[i][j] = explorer_search_history[i - 1][j];
    }
    for (j = 0; j < 32; j++) explorer_search_history[0][j] = temp[j];
}

static int explorer_grid_columns(app_window_t *app) {
    int content_x = app->frame.x + 10;
    int content_w = app->frame.w - 20;
    int sidebar_w = 134;
    int preview_pane_w = (explorer_preview_pane_open && content_w >= 440) ? 170 : 0;
    int main_w = content_w - sidebar_w - preview_pane_w - (preview_pane_w ? 2 : 1);
    int card_w = main_w < 250 ? 82 : 96;
    int icon_cols = (main_w - 24) / card_w;
    (void)content_x;
    if (icon_cols < 1) icon_cols = 1;
    return icon_cols;
}

static int explorer_visible_items_capacity(app_window_t *app) {
    int content_h = app->frame.h - 52;
    int header_h = 34;
    int toolbar_h = 34;
    int status_h = 24;
    int grid_h = content_h - header_h - toolbar_h - status_h - 18;
    int cell_h = 86;
    int save_bar_h = editor_save_dialog_open ? 36 : 0;
    int rows = (grid_h - save_bar_h) / cell_h;
    if (rows < 1) rows = 1;
    return rows * explorer_grid_columns(app);
}

static void explorer_ensure_selection_visible(app_window_t *app) {
    int count = explorer_filtered_count();
    int visible_items = explorer_visible_items_capacity(app);
    int visible_index = 0;
    int i = 0;
    if (!app || explorer_selected < 0 || count <= 0) return;
    for (i = 0; i < explorer_entry_count(); i++) {
        const explorer_entry_t *entry = explorer_entry_at(i);
        if (!explorer_entry_matches_search(entry)) continue;
        if (i == explorer_selected) break;
        visible_index++;
    }
    if (visible_index < explorer_scroll) explorer_scroll = visible_index;
    if (visible_index >= explorer_scroll + visible_items) explorer_scroll = visible_index - visible_items + 1;
    if (explorer_scroll < 0) explorer_scroll = 0;
    if (count > visible_items && explorer_scroll > count - visible_items) explorer_scroll = count - visible_items;
}

static int explorer_is_selected(int index) {
    if (index < 0 || index >= 256) return 0;
    if (index == explorer_selected) return 1;
    return explorer_multi_selected[index] ? 1 : 0;
}

static void explorer_clear_multi_selection(void) {
    int i = 0;
    for (i = 0; i < 256; i++) explorer_multi_selected[i] = 0;
}

static void explorer_select_range(int start_index, int end_index) {
    int i = 0;
    if (start_index > end_index) { int t = start_index; start_index = end_index; end_index = t; }
    explorer_clear_multi_selection();
    for (i = start_index; i <= end_index && i < 256; i++) explorer_multi_selected[i] = 1;
}

static void update_explorer_drag(app_window_t *app, int mx, int my) {
    int dx = mx - explorer_window_drag_start_x;
    int dy = my - explorer_window_drag_start_y;
    int target;
    unsigned int now = get_system_ticks();
    if (!explorer_drag_moved && (dx > 4 || dx < -4 || dy > 4 || dy < -4 || (explorer_drag_start_time > 0 && (now - explorer_drag_start_time) > 18))) {
        explorer_drag_moved = 1;
        mouse_cursor_state = CURSOR_STATE_DRAGGING;
        unified_last_click_time = 0;
        serial_write("[mouse] explorer drag mode activated\n");
    }
    if (!explorer_drag_moved) return;
    explorer_drag_preview_x = mx;
    explorer_drag_preview_y = my;
    if (explorer_drag_index >= 0 && explorer_drag_index < explorer_entry_count()) {
        target = explorer_item_at(app, mx, my);
        if (target >= 0 && target != explorer_drag_index) explorer_selected = target;
        needs_redraw = 1;
    }
}

static int get_drop_target(int mx, int my) {
    int i;
    for (i = 31; i >= 0; i--) {
        int idx = z_order[i];
        app_window_t *w = &app_windows[idx];
        if (w->open && !w->minimized && point_in_rect(mx, my, w->frame)) return idx;
    }
    return -1;
}

static const char* explorer_to_fs_path(const char *path) {
    if (!path || !path[0]) return "/";
    if (path[0] == '/') return path;
    if (path[0] == 'F' && path[1] == 'i' && path[2] == 'r' && path[3] == 's' && path[4] == 't' && path[5] == 'O' && path[6] == 'S') {
        if (path[7] == '/') return path + 7;
        return "/";
    }
    return path;
}

static void draw_drag_preview(int mx, int my) {
    const char *name = 0;
    int is_dir = 0;
    int icon = ICON_FILE;
    char hint[64];
    hint[0] = 0;

    if (desktop_dragging && desktop_drag_moved && desktop_drag_index >= 0 && desktop_drag_index < desktop_item_count) {
        name = desktop_items[desktop_drag_index].name;
        is_dir = desktop_items[desktop_drag_index].is_dir;
        icon = is_dir ? ICON_FOLDER : ICON_FILE;
        
        int target_app = get_drop_target(mx, my);
        if (target_app >= 0 && app_windows[target_app].kind == APP_EXPLORER) {
            app_window_t *app = &app_windows[target_app];
            int item = explorer_item_at(app, mx, my);
            if (item >= 0 && item < explorer_entry_count() && explorer_entry_at(item)->is_dir) {
                const char *folder = explorer_entry_at(item)->name;
                int hp = 0;
                const char *pfx = "Move to ";
                while (pfx[hp]) { hint[hp] = pfx[hp]; hp++; }
                int fp = 0;
                while (folder[fp] && hp < 50) { hint[hp++] = folder[fp++]; }
                hint[hp] = 0;
            } else {
                const char *msg = "Move to Explorer";
                int hp = 0; while (msg[hp]) { hint[hp] = msg[hp]; hp++; } hint[hp] = 0;
            }
        } else {
            int target_desktop_dir = -1;
            for (int di = 0; di < desktop_item_count; di++) {
                if (di != desktop_drag_index && desktop_items[di].used && desktop_items[di].is_dir && point_in_rect(mx, my, desktop_items[di].icon_rect)) {
                    target_desktop_dir = di;
                    break;
                }
            }
            if (target_desktop_dir >= 0) {
                int hp = 0;
                const char *pfx = "Move to ";
                while (pfx[hp]) { hint[hp] = pfx[hp]; hp++; }
                int fp = 0;
                while (desktop_items[target_desktop_dir].name[fp] && hp < 50) { hint[hp++] = desktop_items[target_desktop_dir].name[fp++]; }
                hint[hp] = 0;
            } else {
                const char *msg = "Drop on Desktop";
                int hp = 0; while (msg[hp]) { hint[hp] = msg[hp]; hp++; } hint[hp] = 0;
            }
        }
    } else if (explorer_dragging && explorer_drag_moved && explorer_drag_index >= 0 && explorer_drag_index < explorer_entry_count()) {
        const explorer_entry_t *src = explorer_entry_at(explorer_drag_index);
        if (src) {
            name = src->name;
            is_dir = src->is_dir;
            icon = is_dir ? ICON_FOLDER : ICON_FILE;
            
            int target_app = get_drop_target(mx, my);
            if (target_app == -1) {
                int target_desktop_dir = -1;
                for (int di = 0; di < desktop_item_count; di++) {
                    if (desktop_items[di].used && desktop_items[di].is_dir && point_in_rect(mx, my, desktop_items[di].icon_rect)) {
                        target_desktop_dir = di;
                        break;
                    }
                }
                if (target_desktop_dir >= 0) {
                    int hp = 0;
                    const char *pfx = "Move to ";
                    while (pfx[hp]) { hint[hp] = pfx[hp]; hp++; }
                    int fp = 0;
                    while (desktop_items[target_desktop_dir].name[fp] && hp < 50) { hint[hp++] = desktop_items[target_desktop_dir].name[fp++]; }
                    hint[hp] = 0;
                } else {
                    const char *msg = "Drop on Desktop";
                    int hp = 0; while (msg[hp]) { hint[hp] = msg[hp]; hp++; } hint[hp] = 0;
                }
            } else if (target_app >= 0 && app_windows[target_app].kind == APP_EXPLORER) {
                app_window_t *app = &app_windows[target_app];
                int item = explorer_item_at(app, mx, my);
                if (item >= 0 && item < explorer_entry_count() && item != explorer_drag_index && explorer_entry_at(item)->is_dir) {
                    const char *folder = explorer_entry_at(item)->name;
                    int hp = 0;
                    const char *pfx = "Move to ";
                    while (pfx[hp]) { hint[hp] = pfx[hp]; hp++; }
                    int fp = 0;
                    while (folder[fp] && hp < 50) { hint[hp++] = folder[fp++]; }
                    hint[hp] = 0;
                } else {
                    const char *msg = "Move to Folder";
                    int hp = 0; while (msg[hp]) { hint[hp] = msg[hp]; hp++; } hint[hp] = 0;
                }
            }
        }
    }

    if (!name) return;

    int bx = mx + 12;
    int by = my + 14;
    int bw = 160;
    int bh = 42;
    if (bx + bw > (int)fb.width) bx = mx - bw - 4;
    if (by + bh > (int)fb.height) by = my - bh - 4;

    fb_rect(bx - 2, by - 2, bw + 4, bh + 4, 0x08101C);
    fb_rect(bx, by, bw, bh, 0x1B2636);
    outline(bx, by, bw, bh, 0x4A6888);

    font_icon(bx + 8, by + 10, icon, is_dir ? 0xF4CA64 : 0xCCDDEE);
    font_text_fit(bx + 30, by + 6, name, 0xFFFFFF, bw - 36);
    if (hint[0]) {
        draw_label(bx + 30, by + 22, hint, 0x76B0E8);
    }
}

void gui_toggle_start_menu(void) {
    start_menu_open = !start_menu_open;
    brand_menu_open = 0; app_menu_open = 0; file_menu_open = 0; edit_menu_open = 0;
    clock_menu_open = 0; weather_popover_open = 0; notification_center_open = 0;
    fps_dropdown_open = 0; desktop_context_open = 0;
    needs_redraw = 1;
}

static void update_drag(void) {
    int mx = gui_last_event.x;
    int my = gui_last_event.y;
    int left = (gui_last_event.button & 1) ? 1 : 0;
    int right = (gui_last_event.button & 2) ? 1 : 0;
    int i = 0;
    int pressed_now = (gui_last_event.type == GUI_EVENT_MOUSE_DOWN) && left;

    if (gui_mode == GUI_MODE_LOGIN) {
        int cur_mx = ps2_mouse_x();
        int cur_my = ps2_mouse_y();
        int cur_left = (gui_last_event.button & 1) || ps2_mouse_left();
        if (pressed_now || (cur_left && !prev_left)) {
            login_handle_mouse(cur_mx, cur_my, 1, cur_left, right);
        }
        prev_left = cur_left;
        prev_right_button = right;
        dragging = 0;
        needs_redraw = 1;
        return;
    }

    if (file_dialog_is_open()) {
        if (pressed_now || (left && !prev_left)) {
            sound_play_event(SOUND_EVENT_CLICK);
            file_dialog_handle_click(mx, my);
        } else if (left) {
            file_dialog_handle_drag(mx, my);
        }
        prev_left = left;
        prev_right_button = right;
        dragging = 0;
        needs_redraw = 1;
        return;
    }

    if (pressed_now || (left && !prev_left)) {
        sound_play_event(SOUND_EVENT_CLICK);
    }

    if (pressed_now && firstlang_has_ui()) {
        if (firstlang_mouse_click(mx, my)) {
            needs_redraw = 1;
            goto finish;
        }
    }

    if (pressed_now && taskbar_hover_preview_index >= 0 && taskbar_hover_preview_index < 32 && point_in_rect(mx, my, taskbar_hover_preview_close)) {
        if (app_windows[taskbar_hover_preview_index].open) {
            app_windows[taskbar_hover_preview_index].open = 0;
            if (active_app_index == taskbar_hover_preview_index) active_app_index = -1;
        }
        taskbar_hover_preview_index = -1;
        needs_redraw = 1;
        goto finish;
    }

    if (pressed_now && taskbar_hover_preview_index >= 0 && taskbar_hover_preview_index < 32 && point_in_rect(mx, my, taskbar_hover_preview_card)) {
        focus_app_window(taskbar_hover_preview_index);
        if (app_windows[taskbar_hover_preview_index].minimized) {
            app_windows[taskbar_hover_preview_index].minimized = 0;
            begin_window_transition(taskbar_hover_preview_index, 2);
        }
        needs_redraw = 1;
        goto finish;
    }

    if (pressed_now && alt_tab_preview_open && point_in_rect(mx, my, alt_tab_preview_card)) {
        if (alt_tab_preview_index >= 0 && alt_tab_preview_index < 32) {
            focus_app_window(alt_tab_preview_index);
            alt_tab_preview_open = 0;
            alt_tab_preview_anim = 0;
            keyboard_alt = 0;
            needs_redraw = 1;
            goto finish;
        }
    }

    if (!left) {
        if (prev_left && desktop_dragging) {
            if (desktop_drag_moved && desktop_drag_index >= 0 && desktop_drag_index < desktop_item_count) {
                serial_write("[mouse] desktop drop mode executed\n");
                sound_play_event(SOUND_EVENT_DRAG_DROP);
                int drop_win = get_drop_target(mx, my);
                if (drop_win == -1) {
                    // Check if dropped on a desktop folder
                    int target_desktop_dir = -1;
                    for (int di = 0; di < desktop_item_count; di++) {
                        if (di != desktop_drag_index && desktop_items[di].used && desktop_items[di].is_dir && point_in_rect(mx, my, desktop_items[di].icon_rect)) {
                            target_desktop_dir = di;
                            break;
                        }
                    }
                    if (target_desktop_dir >= 0) {
                        const char *src_fs = desktop_items[desktop_drag_index].path;
                        char dest_fs[256];
                        int p = 0;
                        while (desktop_items[target_desktop_dir].path[p] && p < 240) { dest_fs[p] = desktop_items[target_desktop_dir].path[p]; p++; }
                        if (p > 0 && dest_fs[p-1] != '/') dest_fs[p++] = '/';
                        int q = 0;
                        while (desktop_items[desktop_drag_index].name[q] && p < 255) { dest_fs[p++] = desktop_items[desktop_drag_index].name[q++]; }
                        dest_fs[p] = 0;
                        extern int fs_rename_at(const char *old_path, const char *new_path);
                        fs_rename_at(src_fs, dest_fs);
                        sync_desktop_icons_from_fs();
                    } else {
                        // Dropped on empty desktop space - set custom position!
                        desktop_items[desktop_drag_index].has_custom_pos = 1;
                        desktop_items[desktop_drag_index].custom_x = mx - 24;
                        desktop_items[desktop_drag_index].custom_y = my - 24;
                        extern void desktop_set_item_pos(int index, int cx, int cy);
                        desktop_set_item_pos(desktop_drag_index, mx - 24, my - 24);
                    }
                } else if (drop_win >= 0 && app_windows[drop_win].kind == APP_EXPLORER) {
                    // Dropped on Explorer!
                    app_window_t *app = &app_windows[drop_win];
                    if (app && app->open && !app->minimized) {
                        int target_index = explorer_item_at(app, mx, my);
                        const char *dest_dir = explorer_current_path();
                        if (target_index >= 0 && target_index < explorer_entry_count()) {
                            const explorer_entry_t *target_entry = explorer_entry_at(target_index);
                            if (target_entry && target_entry->is_dir) {
                                dest_dir = target_entry->path;
                            }
                        }
                        const char *src_fs = desktop_items[desktop_drag_index].path;
                        char dest_fs[256];
                        int p = 0;
                        const char *dest_base = explorer_to_fs_path(dest_dir);
                        while (dest_base[p] && p < 240) { dest_fs[p] = dest_base[p]; p++; }
                        if (p > 0 && dest_fs[p-1] != '/') dest_fs[p++] = '/';
                        int q = 0;
                        while (desktop_items[desktop_drag_index].name[q] && p < 255) {
                            dest_fs[p++] = desktop_items[desktop_drag_index].name[q++];
                        }
                        dest_fs[p] = 0;
                        extern int fs_rename_at(const char *old_path, const char *new_path);
                        fs_rename_at(src_fs, dest_fs);
                        sync_desktop_icons_from_fs();
                        explorer_open_path(explorer_current_path());
                    }
                }
            }
            if (desktop_drag_index >= 0 && desktop_drag_index < desktop_item_count) {
                desktop_items[desktop_drag_index].last_click = 0;
            }
            desktop_drag_moved = 0;
            desktop_drag_index = -1;
            desktop_dragging = 0;
            desktop_drag_start_time = 0;
            if (mouse_cursor_state == CURSOR_STATE_DRAGGING) mouse_cursor_state = CURSOR_STATE_IDLE;
            needs_redraw = 1;
        }
        if (explorer_dragging && explorer_drag_moved) {
            serial_write("[mouse] explorer drop mode executed\n");
            sound_play_event(SOUND_EVENT_DRAG_DROP);
            int target_idx = get_drop_target(mx, my);
            if (target_idx == -1) {
                // Dropped on desktop!
                const explorer_entry_t *src = explorer_entry_at(explorer_drag_index);
                if (src) {
                    int target_desktop_dir = -1;
                    for (int di = 0; di < desktop_item_count; di++) {
                        if (desktop_items[di].used && desktop_items[di].is_dir && point_in_rect(mx, my, desktop_items[di].icon_rect)) {
                            target_desktop_dir = di;
                            break;
                        }
                    }
                    char new_path[256];
                    int i = 0;
                    if (target_desktop_dir >= 0) {
                        while (desktop_items[target_desktop_dir].path[i] && i < 240) { new_path[i] = desktop_items[target_desktop_dir].path[i]; i++; }
                        if (i > 0 && new_path[i-1] != '/') new_path[i++] = '/';
                    } else {
                        while ("/Desktop/"[i]) { new_path[i] = "/Desktop/"[i]; i++; }
                    }
                    int j = 0;
                    while (src->name[j] && i < 255) { new_path[i] = src->name[j]; i++; j++; }
                    new_path[i] = 0;
                    extern int fs_rename_at(const char *old_path, const char *new_path);
                    fs_rename_at(src->path, new_path);
                    explorer_open_path(explorer_current_path());
                    sync_desktop_icons_from_fs();
                }
            }
            explorer_finish_drag_navigation(mx, my);
        }
        if (mouse_cursor_state == CURSOR_STATE_DRAGGING) {
            mouse_cursor_state = CURSOR_STATE_IDLE;
        }
        if (window_dragging && window_drag_index >= 0 && window_drag_index < 32) {
            if (window_snapping > SNAP_NONE) {
                apply_snap_to_window(&app_windows[window_drag_index], window_snapping);
                window_snapping = SNAP_NONE;
                needs_redraw = 1;
            }
        }
        window_snapping = SNAP_NONE;
        window_dragging = 0;
        window_resizing = 0;
        settings_scroll_dragging = 0;
        explorer_dragging = 0;
        explorer_marquee_active = 0;
        desktop_marquee_active = 0;
        editor_mouse_selecting = 0;
        extern void terminal_mouse_up(void);
        terminal_mouse_up();
        if (ide_designer_mode) { handle_ui_builder_mouse_up(mx, my); needs_redraw = 1; }
        explorer_drag_index = -1;
        explorer_drag_moved = 0;
        explorer_drag_start_time = 0;
        explorer_drop_target_index = -1;
        explorer_drag_preview_x = 0;
        explorer_drag_preview_y = 0;
        window_drag_index = -1;
        window_resize_index = -1;
        widgets_handle_drag_end();
        widgets_window_dragging = 0;
    }
    if (left && widgets_window_dragging) {
        widgets_window_rect.x = mx - widgets_window_drag_ox;
        widgets_window_rect.y = my - widgets_window_drag_oy;
        if (widgets_window_rect.x < 4) widgets_window_rect.x = 4;
        if (widgets_window_rect.y < 30) widgets_window_rect.y = 30;
        if (widgets_window_rect.x + widgets_window_rect.w > (int)fb.width - 4)
            widgets_window_rect.x = (int)fb.width - 4 - widgets_window_rect.w;
        if (widgets_window_rect.y + widgets_window_rect.h > (int)fb.height - 4)
            widgets_window_rect.y = (int)fb.height - 4 - widgets_window_rect.h;
        needs_redraw = 1;
    }
    if (left && widgets_handle_drag(mx, my)) {
        needs_redraw = 1;
    }
    if (left && ide_designer_mode) {
        handle_ui_builder_mouse_move(mx, my);
        needs_redraw = 1;
    }
    if (left && editor_mouse_selecting && point_in_rect(mx, my, editor_text_area)) {
        editor_select_active = text_edit_position_from_point(mx, my);
        editor_cursor = editor_select_active;
        needs_redraw = 1;
    }
    if (left && ide_mouse_selecting && point_in_rect(mx, my, ide_text_area)) {
        ide_select_active = ide_position_from_point(mx, my);
        ide_cursor = ide_select_active;
        needs_redraw = 1;
    }
    if (left) {
        app_window_t *active = active_app_window();
        if (active && active->kind == APP_TERMINAL && point_in_rect(mx, my, active->frame)) {
            extern void terminal_mouse_drag(int mx, int my, int frame_x, int frame_y, int frame_w, int frame_h);
            terminal_mouse_drag(mx, my, active->frame.x, active->frame.y, active->frame.w, active->frame.h);
            needs_redraw = 1;
        }
    }
    if (left && settings_scroll_dragging) {
        app_window_t *settings_app = app_slot(APP_SETTINGS);
        if (settings_app && settings_app->open && !settings_app->minimized) update_settings_scroll_drag(settings_app, my);
    }
    if (left && explorer_dragging) {
        app_window_t *explorer_app = app_slot(APP_EXPLORER);
        if (explorer_app && explorer_app->open && !explorer_app->minimized) update_explorer_drag(explorer_app, mx, my);
    }

    if (left && desktop_dragging) {
        desktop_drag_mx = mx;
        desktop_drag_my = my;
        unsigned int now = get_system_ticks();
        if (!desktop_drag_moved && (mx - desktop_window_drag_start_x > 4 || mx - desktop_window_drag_start_x < -4 || my - desktop_window_drag_start_y > 4 || my - desktop_window_drag_start_y < -4 || (desktop_drag_start_time > 0 && (now - desktop_drag_start_time) > 18))) {
            desktop_drag_moved = 1;
            mouse_cursor_state = CURSOR_STATE_DRAGGING;
            unified_last_click_time = 0;
            serial_write("[mouse] desktop drag mode activated\n");
        }
        needs_redraw = 1;
    }
    if (left && desktop_marquee_active) {
        desktop_marquee_mx = mx;
        desktop_marquee_my = my;
        
        int rx = desktop_marquee_x < mx ? desktop_marquee_x : mx;
        int ry = desktop_marquee_y < my ? desktop_marquee_y : my;
        int rw = desktop_marquee_x < mx ? mx - desktop_marquee_x : desktop_marquee_x - mx;
        int rh = desktop_marquee_y < my ? my - desktop_marquee_y : desktop_marquee_y - my;
        
        extern void desktop_update_marquee_selection(int rx, int ry, int rw, int rh);
        desktop_update_marquee_selection(rx, ry, rw, rh);
        
        needs_redraw = 1;
    }
    if (left && explorer_marquee_active) {
        explorer_marquee_mx = mx;
        explorer_marquee_my = my;
        
        // Compute marquee rect
        int rx = explorer_marquee_x < mx ? explorer_marquee_x : mx;
        int ry = explorer_marquee_y < my ? explorer_marquee_y : my;
        int rw = explorer_marquee_x < mx ? mx - explorer_marquee_x : explorer_marquee_x - mx;
        int rh = explorer_marquee_y < my ? my - explorer_marquee_y : explorer_marquee_y - my;
        
        // Select items in rect
        app_window_t *app = app_slot(APP_EXPLORER);
        if (app && app->open && !app->minimized) {
            int grid_y = app->frame.y + 42 + 32;
            int grid_h = app->frame.h - 42 - 32;
            int save_bar_h = explorer_picker_mode ? 42 : 0;
            grid_h -= save_bar_h;
            
            int cell_w = 84;
            int cell_h = 72;
            int cols = app->frame.w / cell_w;
            if (cols < 1) cols = 1;
            
            int i;
            for (i = 0; i < explorer_entry_count(); i++) {
                int row = (i - explorer_scroll) / cols;
                int col = (i - explorer_scroll) % cols;
                int item_x = app->frame.x + col * cell_w;
                int item_y = grid_y + row * cell_h;
                
                if (item_y >= grid_y && item_y + cell_h <= grid_y + grid_h) {
                    // Check intersection
                    if (rx < item_x + cell_w && rx + rw > item_x && ry < item_y + cell_h && ry + rh > item_y) {
                        if (i < 256) explorer_multi_selected[i] = 1;
                    } else {
                        if (i < 256) explorer_multi_selected[i] = 0;
                    }
                }
            }
        }
        needs_redraw = 1;
    }
    if (left && window_dragging && window_drag_index >= 0 && window_drag_index < 32) {
        app_window_t *app = &app_windows[window_drag_index];
        if (app->snapped > 0 || app->maximized) {
            int old_w = app->frame.w;
            app->snapped = 0;
            app->maximized = 0;
            if (app->saved_w > 0 && app->saved_h > 0) {
                app->frame.w = app->saved_w;
                app->frame.h = app->saved_h;
            } else {
                app->frame.w = app->min_w > 480 ? app->min_w : 480;
                app->frame.h = app->min_h > 320 ? app->min_h : 320;
            }
            drag_offset_x = (drag_offset_x * app->frame.w) / (old_w > 0 ? old_w : 1);
            if (drag_offset_x < 24) drag_offset_x = 24;
            if (drag_offset_x > app->frame.w - 24) drag_offset_x = app->frame.w / 2;
        }
        app->frame.x = mx - drag_offset_x;
        app->frame.y = my - drag_offset_y;
        if (app->frame.x < 4) app->frame.x = 4;
        if (app->frame.y < desktop_bar.h + 2) app->frame.y = desktop_bar.h + 2;
        if (app->frame.x + app->frame.w > (int)fb.width - 4) app->frame.x = (int)fb.width - app->frame.w - 4;
        if (app->frame.y + app->frame.h > dock_rect.y - 2) app->frame.y = dock_rect.y - app->frame.h - 2;
        sync_app_window(app);

        int snap = check_snap_zone(mx, my);
        if (snap != window_snapping) {
            window_snapping = snap;
        }
        desktop_wallpaper_dirty = 1;
        needs_redraw = 1;
    }
    if (left && window_resizing && window_resize_index >= 0 && window_resize_index < 16) {
        app_window_t *app = &app_windows[window_resize_index];
        app->frame.w = window_resize_start_w + mx - window_resize_start_x;
        app->frame.h = window_resize_start_h + my - window_resize_start_y;
        if (app->frame.w < 180) app->frame.w = 180;
        if (app->frame.h < 110) app->frame.h = 110;
        if (app->frame.x + app->frame.w > (int)fb.width - 4) app->frame.w = (int)fb.width - app->frame.x - 4;
        if (app->frame.y + app->frame.h > dock_rect.y - 2) app->frame.h = dock_rect.y - app->frame.y - 2;
        sync_app_window(app);
        desktop_wallpaper_dirty = 1;
        needs_redraw = 1;
    }

    if (right && !prev_right_button) {
        if (dock_handle_click(mx, my, 1) != 0) { // 1 = right click
            needs_redraw = 1;
            goto finish;
        }
        app_window_t *explorer_app = 0;
        int top_hit = -1;
        for (int z = 31; z >= 0; z--) {
            int i = z_order[z];
            if (app_windows[i].open && !app_windows[i].minimized && point_in_rect(mx, my, app_windows[i].frame)) {
                top_hit = i;
                break;
            }
        }
        if (top_hit >= 0) {
            focus_app_window(top_hit);
            if (app_windows[top_hit].kind == APP_EXPLORER) explorer_app = &app_windows[top_hit];
        }
        if (!explorer_app) explorer_app = app_slot(APP_EXPLORER);
        if (explorer_app && explorer_app->open && !explorer_app->minimized && point_in_rect(mx, my, explorer_app->frame) && top_hit >= 0 && app_windows[top_hit].kind == APP_EXPLORER) {
            int hit = explorer_item_at(explorer_app, mx, my);
            if (hit >= 0) {
                explorer_context_index = hit;
                layout_explorer_context_menu(mx, my);
                explorer_context_open = 1;
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
        }
        app_window_t *media_app = (top_hit >= 0 && app_windows[top_hit].kind == APP_MEDIA_PLAYER) ? &app_windows[top_hit] : app_slot(APP_MEDIA_PLAYER);
        if (media_app && media_app->open && !media_app->minimized && point_in_rect(mx, my, media_app->frame) && top_hit >= 0 && app_windows[top_hit].kind == APP_MEDIA_PLAYER) {
            media_player_handle_right_click(media_app, mx, my);
            needs_redraw = 1;
            goto finish;
        }
        app_window_t *browser_app = (top_hit >= 0 && app_windows[top_hit].kind == APP_BROWSER) ? &app_windows[top_hit] : app_slot(APP_BROWSER);
        if (browser_app && browser_app->open && !browser_app->minimized && point_in_rect(mx, my, browser_app->frame) && top_hit >= 0 && app_windows[top_hit].kind == APP_BROWSER) {
            web_browser_app_handle_right_click(browser_app, mx, my);
            gui_window_dirty_only = 0;
            needs_redraw = 1;
            goto finish;
        }
        if (my >= dock_rect.y + 7 && my < dock_rect.y + 31) {
            int chip_x = (start_button.w > 0) ? (start_button.x + start_button.w + 12) : 10;
            int chip_w = 28;
            int chip_gap = 8;
            for (i = 0; i < 32; i++) {
                if (!app_windows[i].open) continue;
                if (mx >= chip_x && mx < chip_x + chip_w) {
                    taskbar_context_index = i;
                    taskbar_context_open = 1;
                    layout_taskbar_context_menu(mx, my);
                    explorer_context_open = 0;
                    desktop_context_open = 0;
                    needs_redraw = 1;
                    goto finish;
                }
                chip_x += chip_w + chip_gap;
            }
        }

        if (top_hit == -1 && my > desktop_bar.y + desktop_bar.h && my < dock_rect.y) {
            if (widgets_handle_click(mx, my, 1)) {
                needs_redraw = 1;
                goto finish;
            }
            desktop_context_index = -1;
            desktop_context_target = 0;
            for (i = 0; i < desktop_item_count; i++) {
                if (desktop_items[i].used && point_in_rect(mx, my, desktop_items[i].icon_rect)) {
                    desktop_context_index = i;
                    desktop_context_target = 1;
                    desktop_selected = i;
                    break;
                }
            }
            layout_desktop_context_menu(mx, my);
            desktop_context_open = 1;
            explorer_context_open = 0;
            needs_redraw = 1;
        }
    }

    if (left && !prev_left) {
        gui_click_sound();

        /* QUICK LOOK OVERLAY */
        if (explorer_quick_look_open) {
            int sw = (int)fb.width;
            int sh = (int)fb.height;
            int modal_w = 540;
            int modal_h = 380;
            if (modal_w > sw - 40) modal_w = sw - 40;
            if (modal_h > sh - 60) modal_h = sh - 60;
            int modal_x = (sw - modal_w) / 2;
            int modal_y = (sh - modal_h) / 2;

            if (point_in_rect(mx, my, explorer_ql_close_btn)) {
                explorer_quick_look_open = 0;
                if (hda_is_playing()) hda_stop_audio();
                needs_redraw = 1;
                goto finish;
            }
            if (explorer_ql_play_btn.w > 0 && point_in_rect(mx, my, explorer_ql_play_btn) && explorer_selected >= 0) {
                const explorer_entry_t *e = explorer_entry_at(explorer_selected);
                if (e && !e->is_dir) {
                    if (hda_is_playing()) {
                        hda_pause_audio();
                    } else if (hda_is_paused()) {
                        hda_resume_audio();
                    } else {
                        hda_play_audio(e->path);
                    }
                }
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, explorer_ql_open_btn) && explorer_selected >= 0) {
                const explorer_entry_t *e = explorer_entry_at(explorer_selected);
                if (e && !e->is_dir) {
                    explorer_quick_look_open = 0;
                    if (hda_is_playing()) hda_stop_audio();
                    explorer_open_file_default(e->path, e->name);
                }
                needs_redraw = 1;
                goto finish;
            }
            if (mx < modal_x || mx >= modal_x + modal_w || my < modal_y || my >= modal_y + modal_h) {
                explorer_quick_look_open = 0;
                if (hda_is_playing()) hda_stop_audio();
                needs_redraw = 1;
                goto finish;
            }
            needs_redraw = 1;
            goto finish;
        }

        /* SPOTLIGHT OVERLAY */
        if (spotlight_open) {
            if (spotlight_view_mode == SPOTLIGHT_VIEW_APP_DROPDOWN) {
                int bw = 580; int dh = 320;
                int bx = ((int)fb.width - bw) / 2;
                int by = (int)fb.height / 6;
                if (mx >= bx + bw - 115 && mx <= bx + bw - 10 && my >= by + 48 && my <= by + 68) {
                    spotlight_close_dropdown();
                    needs_redraw = 1;
                    goto finish;
                }
                if (spotlight_active_app_kind == APP_EXPLORER) {
                    /* Header Back/Up button */
                    if (mx >= bx + 185 && mx <= bx + 240 && my >= by + 48 && my <= by + 68) {
                        spotlight_explorer_up();
                        needs_redraw = 1;
                        goto finish;
                    }
                    /* Left Sidebar clicks */
                    if (mx >= bx + 2 && mx <= bx + 136 && my >= by + 72 && my <= by + 44 + dh) {
                        /* Disks */
                        if (my >= by + 90 && my <= by + 112) {
                            spotlight_explorer_navigate("FirstOS");
                            needs_redraw = 1;
                            goto finish;
                        }
                        if (my >= by + 114 && my <= by + 136) {
                            spotlight_explorer_navigate("FirstOS/System");
                            needs_redraw = 1;
                            goto finish;
                        }
                        /* Places */
                        static const char *sidebar_paths[] = {
                            "FirstOS/Apps", "FirstOS/Wallpapers", "FirstOS/Movies",
                            "FirstOS/Audio", "FirstOS/Desktop", "FirstOS/Trash"
                        };
                        for (int p = 0; p < 6; p++) {
                            int py = by + 162 + p * 22;
                            if (my >= py && my <= py + 21) {
                                spotlight_explorer_navigate(sidebar_paths[p]);
                                needs_redraw = 1;
                                goto finish;
                            }
                        }
                    }
                    /* Right File Viewport clicks */
                    if (mx >= bx + 138 && mx <= bx + bw - 4 && my >= by + 74 && my <= by + 44 + dh - 22) {
                        int scroll = 0;
                        if (spotlight_explorer_selected >= 8) scroll = spotlight_explorer_selected - 7;
                        int clicked_row = (my - (by + 76)) / 26;
                        int clicked_index = scroll + clicked_row;
                        if (clicked_index >= 0 && clicked_index < spotlight_explorer_count) {
                            if (spotlight_explorer_selected == clicked_index) {
                                spotlight_explorer_enter();
                            } else {
                                spotlight_explorer_selected = clicked_index;
                            }
                            needs_redraw = 1;
                            goto finish;
                        }
                    }
                }
                if (mx < bx || mx >= bx + bw || my < by || my >= by + 44 + dh) {
                    spotlight_close_dropdown();
                    spotlight_open = 0;
                    needs_redraw = 1;
                    goto finish;
                }
            } else if (spotlight_view_mode == SPOTLIGHT_VIEW_MEDIA_PREVIEW) {
                int bw = 560; int dh = 310;
                int bx = ((int)fb.width - bw) / 2;
                int by = (int)fb.height / 6;
                if (mx >= bx + bw - 120 && mx <= bx + bw - 10 && my >= by + 48 && my <= by + 68) {
                    spotlight_close_dropdown();
                    needs_redraw = 1;
                    goto finish;
                }
                if (spotlight_media_type == SPOTLIGHT_MEDIA_AUDIO && mx >= bx + 24 && mx <= bx + 120 && my >= by + 224 && my <= by + 250) {
                    spotlight_toggle_media_playback();
                    needs_redraw = 1;
                    goto finish;
                }
                if (spotlight_media_type == SPOTLIGHT_MEDIA_VIDEO && mx >= bx + 20 && mx <= bx + 250 && my >= by + 80 && my <= by + 240) {
                    spotlight_toggle_media_playback();
                    needs_redraw = 1;
                    goto finish;
                }
                if (spotlight_media_type == SPOTLIGHT_MEDIA_AUDIO && mx >= bx + bw - 120 && mx <= bx + bw - 24 && my >= by + 224 && my <= by + 250) {
                    spotlight_execute(spotlight_selected_index);
                    spotlight_close_dropdown();
                    spotlight_open = 0;
                    needs_redraw = 1;
                    goto finish;
                }
                if ((spotlight_media_type == SPOTLIGHT_MEDIA_VIDEO || spotlight_media_type == SPOTLIGHT_MEDIA_IMAGE) && mx >= bx + 265 && mx <= bx + 425 && my >= by + 214 && my <= by + 240) {
                    spotlight_execute(spotlight_selected_index);
                    spotlight_close_dropdown();
                    spotlight_open = 0;
                    needs_redraw = 1;
                    goto finish;
                }
                if (mx < bx || mx >= bx + bw || my < by || my >= by + 44 + dh) {
                    spotlight_close_dropdown();
                    spotlight_open = 0;
                    needs_redraw = 1;
                    goto finish;
                }
            } else {
                int bw = 480; int rh = 36;
                int bx = ((int)fb.width - bw) / 2;
                int by = (int)fb.height / 5;
                if (mx >= bx && mx < bx + bw && my >= by + 44 && my < by + 44 + spotlight_num_results * rh) {
                    int clicked_i = (my - (by + 44)) / rh;
                    if (clicked_i >= 0 && clicked_i < spotlight_num_results) {
                        spotlight_execute(clicked_i);
                    }
                } else if (mx < bx || mx >= bx + bw || my < by || my >= by + 44 + spotlight_num_results * rh) {
                    spotlight_open = 0;
                }
            }
            needs_redraw = 1;
            goto finish;
        }

        /* DESKTOP WIDGETS WINDOW CLICK */
        if (widgets_window_open) {
            int wx = widgets_window_rect.x;
            int wy = widgets_window_rect.y;
            int ww = widgets_window_rect.w;
            int wh = widgets_window_rect.h;

            rect_t close_btn = {wx + ww - 24, wy + 4, 18, 18, 0};
            if (point_in_rect(mx, my, close_btn)) {
                widgets_window_open = 0;
                needs_redraw = 1;
                goto finish;
            }

            // Title bar drag
            if (point_in_rect(mx, my, (rect_t){wx, wy, ww - 26, 26, 0})) {
                widgets_window_dragging = 1;
                widgets_window_drag_ox = mx - wx;
                widgets_window_drag_oy = my - wy;
                goto finish;
            }

            // Inside window checks
            if (point_in_rect(mx, my, widgets_window_rect)) {
                // Row 1: Clock (wy + 50)
                int r1_y = wy + 50;
                rect_t chk_clock = {wx + 14, r1_y + 2, 16, 16, 0};
                rect_t row_clock = {wx + 14, r1_y, ww - 80, 24, 0};
                rect_t mode_clock = {wx + ww - 62, r1_y + 1, 48, 18, 0};
                if (point_in_rect(mx, my, mode_clock)) {
                    widget_clock_mode = !widget_clock_mode;
                    needs_redraw = 1;
                    goto finish;
                }
                if (point_in_rect(mx, my, chk_clock) || point_in_rect(mx, my, row_clock)) {
                    widget_clock.enabled = !widget_clock.enabled;
                    needs_redraw = 1;
                    goto finish;
                }

                // Row 2: Calendar (wy + 78)
                int r2_y = wy + 78;
                rect_t row_cal = {wx + 14, r2_y, ww - 28, 24, 0};
                if (point_in_rect(mx, my, row_cal)) {
                    widget_calendar.enabled = !widget_calendar.enabled;
                    needs_redraw = 1;
                    goto finish;
                }

                // Row 3: Sticky Notes (wy + 106)
                int r3_y = wy + 106;
                rect_t row_notes = {wx + 14, r3_y, ww - 28, 24, 0};
                if (point_in_rect(mx, my, row_notes)) {
                    widget_notes.enabled = !widget_notes.enabled;
                    needs_redraw = 1;
                    goto finish;
                }

                // Row 4: Weather (wy + 134)
                int r4_y = wy + 134;
                rect_t row_weather = {wx + 14, r4_y, ww - 28, 24, 0};
                if (point_in_rect(mx, my, row_weather)) {
                    widget_weather.enabled = !widget_weather.enabled;
                    needs_redraw = 1;
                    goto finish;
                }

                // Reset button (wy + wh - 36)
                rect_t btn_reset = {wx + 14, wy + wh - 36, ww - 28, 24, 0};
                if (point_in_rect(mx, my, btn_reset)) {
                    widgets_reset_positions();
                    widget_clock.enabled = 1;
                    widget_calendar.enabled = 1;
                    widget_notes.enabled = 1;
                    widget_weather.enabled = 1;
                    needs_redraw = 1;
                    goto finish;
                }

                goto finish;
            }
        }

        /* WI-FI PASSWORD MODAL CLICK */
        if (wifi_pwd_modal_open) {
            int sw = (int)fb.width;
            int sh = (int)fb.height;
            int pw = 360;
            int ph = 190;
            int px = (sw - pw) / 2;
            int py = (sh - ph) / 2;

            rect_t close_btn = {px + pw - 24, py + 4, 20, 20, 0};
            rect_t chk_box = {px + 20, py + 102, 140, 16, 0};
            rect_t btn_cancel = {px + pw - 180, py + ph - 38, 70, 26, 0};
            rect_t btn_connect = {px + pw - 98, py + ph - 38, 78, 26, 0};

            if (point_in_rect(mx, my, close_btn) || point_in_rect(mx, my, btn_cancel)) {
                wifi_pwd_modal_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, chk_box)) {
                wifi_pwd_show_pass = !wifi_pwd_show_pass;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, btn_connect)) {
                int plen = 0;
                while (wifi_pwd_input[plen]) plen++;
                if (plen < 8) {
                    wifi_pwd_error = 2;
                } else if (net_connect(wifi_pwd_ap_index, wifi_pwd_input)) {
                    wifi_pwd_modal_open = 0;
                    current_wifi_network = wifi_pwd_ap_index;
                    wifi_pwd_error = 0;
                } else {
                    wifi_pwd_error = 1;
                }
                needs_redraw = 1;
                goto finish;
            }
            goto finish;
        }

        /* TEXT EDIT UNSAVED DIALOG CLICK */
        if (text_edit_unsaved_dialog_open) {
            if (point_in_rect(mx, my, text_edit_unsaved_btn_cancel)) {
                text_edit_unsaved_dialog_open = 0;
                text_edit_closing_app_index = -1;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, text_edit_unsaved_btn_dont_save)) {
                if (text_edit_closing_app_index >= 0 && text_edit_closing_app_index < 32) {
                    app_windows[text_edit_closing_app_index].open = 0;
                }
                text_edit_unsaved_dialog_open = 0;
                text_edit_closing_app_index = -1;
                editor_saved = 1;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, text_edit_unsaved_btn_save)) {
                text_edit_unsaved_dialog_open = 0;
                text_edit_closing_app_index = -1;
                file_dialog_show(APP_TEXT_EDIT, FILE_DIALOG_SAVE, "untitled.txt", "/Desktop");
                needs_redraw = 1;
                goto finish;
            }
            goto finish;
        }

        /* LAUNCHER OVERLAY */
        if (launcher_open) {
            if (launcher_hover >= 0) {
                static const int lapp_id[12] = {APP_TERMINAL,APP_EXPLORER,APP_TEXT_EDIT,APP_FILES,APP_SETTINGS,APP_CALCULATOR,APP_MEDIA_PLAYER,APP_PHOTO_EDITOR,APP_IDE,APP_TASKMAN,APP_PREVIEW,APP_BROWSER};
                if (lapp_id[launcher_hover] == APP_BROWSER) {
                    web_browser_app_launch(0);
                } else {
                    open_app(lapp_id[launcher_hover]);
                }
                launcher_open = 0;
            } else {
                launcher_open = 0; /* Click anywhere else to close */
            }
            needs_redraw = 1;
            goto finish;
        }

        /* ANIMATED DOCK CLICK */
        int dock_click_res = dock_handle_click(mx, my, 0); // 0 = left click
        if (dock_click_res != 0) {
            if (dock_click_res == -1) launcher_open = 1;
            needs_redraw = 1;
            goto finish;
        }

        int top_hit = -1;
        for (int z = 31; z >= 0; z--) {
            int i = z_order[z];
            if (app_windows[i].open && !app_windows[i].minimized && point_in_rect(mx, my, app_windows[i].frame)) {
                top_hit = i;
                break;
            }
        }
        
        app_window_t *clicked_app = active_app_window();
        int click_ticks = clock_hours * 3600 + clock_minutes * 60 + clock_seconds;
        if (clicked_app && clicked_app->open && !clicked_app->minimized && point_in_rect(mx, my, clicked_app->title)) {
            if (click_ticks - clicked_app->last_title_click_ticks <= 1) {
                // Phase 1: Enhanced maximize with snap support
                set_app_window_maximized(clicked_app, !clicked_app->maximized);
                clicked_app->maximized = !clicked_app->maximized;
                clicked_app->last_title_click_ticks = 0;
                needs_redraw = 1;
            } else {
                clicked_app->last_title_click_ticks = click_ticks;
            }
        }
        int chip_x = (start_button.w > 0) ? (start_button.x + start_button.w + 12) : 10;
        int dock_y = dock_rect.y + 7;
        int handled_task = 0;
        rect_t apps_button_rect = {(int)fb.width - 86, dock_rect.y + 7, 76, 24, 0};

        if (desktop_context_open && !point_in_rect(mx, my, desktop_context_menu)) {
            desktop_context_open = 0;
            needs_redraw = 1;
        }
        if (dock_context_open) {
            if (dock_context_handle_click(mx, my)) {
                needs_redraw = 1;
                goto finish;
            }
        }
        if (taskbar_context_open && !point_in_rect(mx, my, taskbar_context_menu)) {
            taskbar_context_open = 0;
            taskbar_context_index = -1;
            needs_redraw = 1;
        }
        if (web_browser_app_is_context_menu_open()) {
            app_window_t *bapp = app_slot(APP_BROWSER);
            if (!bapp || !bapp->open || !point_in_rect(mx, my, bapp->frame)) {
                web_browser_app_close_context_menu();
                gui_window_dirty_only = 0;
                needs_redraw = 1;
            }
        }

        if (point_in_rect(mx, my, apps_button_rect)) {
            pressed_button = apps_button_rect;
            pressed_button_active = 1;
            if (active_app_index >= 0 && active_app_index < 32 && app_windows[active_app_index].open) toggle_taskbar_app(app_windows[active_app_index].kind);
            handled_task = 1;
        }

        for (i = 0; i < 32 && !handled_task; i++) {
            if (!app_windows[i].open) continue;
            if (mx >= chip_x && mx < chip_x + 28 && my >= dock_y && my < dock_y + 24) {
                pressed_button = (rect_t){chip_x, dock_y, 28, 24, 0};
                pressed_button_active = 1;
                toggle_taskbar_app(app_windows[i].kind);
                handled_task = 1;
                break;
            }
            chip_x += 36;
        }
        if (handled_task) goto finish;

        if (brand_menu_open) {
            if (point_in_rect(mx, my, top_brand_menu)) { handle_brand_menu_click(mx, my); goto finish; }
            else { brand_menu_open = 0; needs_redraw = 1; }
        }
        if (app_menu_open) {
            if (point_in_rect(mx, my, top_app_menu_rect)) { handle_app_menu_click(mx, my); goto finish; }
            else { app_menu_open = 0; needs_redraw = 1; }
        }
        if (file_menu_open) {
            if (point_in_rect(mx, my, top_file_menu_rect)) { handle_file_menu_click(mx, my); goto finish; }
            else { file_menu_open = 0; needs_redraw = 1; }
        }
        if (edit_menu_open) {
            if (point_in_rect(mx, my, top_edit_menu_rect)) { handle_edit_menu_click(mx, my); goto finish; }
            else { edit_menu_open = 0; needs_redraw = 1; }
        }
        if (view_menu_open) {
            if (point_in_rect(mx, my, top_view_menu_rect)) { handle_view_menu_click(mx, my); goto finish; }
            else { view_menu_open = 0; needs_redraw = 1; }
        }

        if (explorer_paste_conflict_open) {
            layout_explorer_paste_dialog();
            if (point_in_rect(mx, my, explorer_paste_replace_button)) {
                if (explorer_paste_target_path[0]) {
                    fs_delete_at(explorer_paste_target_path);
                    explorer_finish_paste_at(explorer_paste_target_dir, explorer_paste_target_path);
                }
                explorer_context_open = 0;
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, explorer_paste_copy_button)) {
                char fs_dir[64];
                char copy_path[64];
                int slash = -1;
                int i = 0;
                int p = 0;
                while (explorer_paste_target_path[i]) {
                    if (explorer_paste_target_path[i] == '/') slash = i;
                    i++;
                }
                if (slash > 0) {
                    for (i = 0; i < slash && i + 1 < (int)sizeof(fs_dir); i++) fs_dir[i] = explorer_paste_target_path[i];
                    fs_dir[i] = 0;
                    if (explorer_build_unique_paste_path(fs_dir, explorer_file_clipboard_name[0], copy_path, sizeof(copy_path))) {
                        explorer_finish_paste_at(explorer_paste_target_dir, copy_path);
                    }
                }
                explorer_context_open = 0;
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            explorer_paste_conflict_open = 0;
            explorer_paste_target_dir[0] = 0;
            explorer_paste_target_path[0] = 0;
            explorer_context_open = 0;
            desktop_context_open = 0;
            needs_redraw = 1;
            goto finish;
        }

        if (explorer_context_open && point_in_rect(mx, my, explorer_context_menu)) {
            if (point_in_rect(mx, my, explorer_ctx_terminal)) {
                extern char tcwd[256];
                int n = 0; const char *cur = explorer_current_path();
                while (cur && cur[n] && n < 255) { tcwd[n] = cur[n]; n++; }
                tcwd[n] = 0;
                open_app(APP_TERMINAL);
            } else if (explorer_ctx_color.w > 0 && point_in_rect(mx, my, explorer_ctx_color)) {
                if (explorer_context_index >= 0) {
                    const explorer_entry_t *e = explorer_entry_at(explorer_context_index);
                    if (e && e->is_dir) {
                        char meta_path[256];
                        int m = 0; while(e->path[m]) { meta_path[m] = e->path[m]; m++; }
                        meta_path[m++] = '/';
                        const char *m_name = ".firstos_folder";
                        int n = 0; while(m_name[n]) { meta_path[m++] = m_name[n++]; }
                        meta_path[m] = 0;
                        
                        // Cycle colors: Green -> Red -> Blue -> Orange
                        unsigned char c[4] = {0};
                        if (fs_read_at(meta_path, (char*)c, 4) != 4) { c[0]=0x50; c[1]=0xC8; c[2]=0x78; c[3]=0xFF; } // Start Green
                        else if (c[0] == 0x50) { c[0]=0xC8; c[1]=0x50; c[2]=0x50; c[3]=0xFF; } // To Red
                        else if (c[0] == 0xC8) { c[0]=0x50; c[1]=0x50; c[2]=0xC8; c[3]=0xFF; } // To Blue
                        else if (c[0] == 0x50) { c[0]=0xFF; c[1]=0xA5; c[2]=0x00; c[3]=0xFF; } // To Orange
                        fs_write_at(meta_path, (char*)c, 4);
                        
                        // Force Explorer Refresh
                        explorer_open_path(explorer_current_path());
                    }
                }
            } else if (point_in_rect(mx, my, explorer_ctx_copy)) {           if (explorer_context_index >= 0) {
                    explorer_selected = explorer_context_index;
                    explorer_copy_selected_file(0);
                }
            } else if (point_in_rect(mx, my, explorer_ctx_cut)) {
                if (explorer_context_index >= 0) {
                    explorer_selected = explorer_context_index;
                    explorer_copy_selected_file(1);
                }
            } else if (point_in_rect(mx, my, explorer_ctx_paste)) {
                explorer_paste_file();
                explorer_open_path(explorer_current_path());
                explorer_path = explorer_current_path();
            } else if (point_in_rect(mx, my, explorer_ctx_rename)) {
                if (explorer_context_index >= 0) {
                    explorer_selected = explorer_context_index;
                    begin_explorer_rename_selected();
                }
            } else if (point_in_rect(mx, my, explorer_ctx_delete)) {
                if (explorer_context_index >= 0) {
                    explorer_selected = explorer_context_index;
                    explorer_delete_selected_files();
                }
            } else if (explorer_ctx_set_wall.w > 0 && point_in_rect(mx, my, explorer_ctx_set_wall)) {
                explorer_set_wallpaper_selected();
            }
            explorer_context_open = 0;
            explorer_context_index = -1;
            needs_redraw = 1;
            goto finish;
        }

        if (desktop_context_open && desktop_context_target && point_in_rect(mx, my, desktop_ctx_copy)) {
            if (desktop_context_index >= 0 && desktop_context_index < desktop_item_count) {
                char path[256];
                int slash = -1;
                int p = 0;
                int n = 0;
                desktop_copy_name(path, sizeof(path), desktop_items[desktop_context_index].path);
                while (path[p]) { if (path[p] == '/') slash = p; p++; }
                if (slash >= 0) {
                    char name[128];
                    while (desktop_items[desktop_context_index].name[n] && n + 1 < (int)sizeof(name)) { name[n] = desktop_items[desktop_context_index].name[n]; n++; }
                    name[n] = 0;
                    desktop_copy_name(explorer_file_clipboard_path[0], sizeof(explorer_file_clipboard_path[0]), path);
                    desktop_copy_name(explorer_file_clipboard_name[0], sizeof(explorer_file_clipboard_name[0]), name);
                    explorer_file_clipboard_set = 1;
                    explorer_file_clipboard_cut = 0;
                    explorer_file_clipboard_is_dir[0] = desktop_items[desktop_context_index].is_dir ? 1 : 0;
                }
            }
            desktop_context_open = 0;
            needs_redraw = 1;
            goto finish;
        }
        if (desktop_context_open && desktop_context_target && point_in_rect(mx, my, desktop_ctx_paste)) {
            if (desktop_context_index >= 0 && desktop_context_index < desktop_item_count && desktop_items[desktop_context_index].is_dir) {
                const char *dest_dir = desktop_items[desktop_context_index].path;
                int c;
                for (c = 0; c < explorer_file_clipboard_count; c++) {
                    char unique_path[256];
                    if (!explorer_build_unique_paste_path(dest_dir, explorer_file_clipboard_name[c], unique_path, sizeof(unique_path))) continue;
                    if (explorer_path_exists(unique_path)) fs_delete_at(unique_path);
                    if (explorer_file_clipboard_cut) fs_rename_at(explorer_file_clipboard_path[c], unique_path);
                    else fs_copy_at(explorer_file_clipboard_path[c], unique_path);
                }
                if (explorer_file_clipboard_cut) {
                    explorer_file_clipboard_set = 0;
                    explorer_file_clipboard_cut = 0;
                    explorer_file_clipboard_count = 0;
                }
            } else {
                desktop_paste_file();
            }
            desktop_context_open = 0;
            sync_desktop_icons_from_fs();
            needs_redraw = 1;
            goto finish;
        }
        if (taskbar_context_open && point_in_rect(mx, my, taskbar_ctx_open_restore)) {
            if (taskbar_context_index >= 0 && taskbar_context_index < 32 && app_windows[taskbar_context_index].open) {
                if (app_windows[taskbar_context_index].minimized) {
                    app_windows[taskbar_context_index].minimized = 0;
                    begin_window_transition(taskbar_context_index, 2);
                    focus_app_window(taskbar_context_index);
                } else {
                    focus_app_window(taskbar_context_index);
                }
            }
            taskbar_context_open = 0;
            taskbar_context_index = -1;
            needs_redraw = 1;
            goto finish;
        }
        if (taskbar_context_open && point_in_rect(mx, my, taskbar_ctx_minimize)) {
            if (taskbar_context_index >= 0 && taskbar_context_index < 32 && app_windows[taskbar_context_index].open) { begin_window_transition(taskbar_context_index, 1); app_windows[taskbar_context_index].minimized = 1; }
            taskbar_context_open = 0;
            taskbar_context_index = -1;
            needs_redraw = 1;
            goto finish;
        }
        if (taskbar_context_open && point_in_rect(mx, my, taskbar_ctx_maximize)) {
            if (taskbar_context_index >= 0 && taskbar_context_index < 32 && app_windows[taskbar_context_index].open) {
                app_windows[taskbar_context_index].minimized = 0;
                focus_app_window(taskbar_context_index);
                set_app_window_maximized(&app_windows[taskbar_context_index], !app_windows[taskbar_context_index].maximized);
            }
            taskbar_context_open = 0;
            taskbar_context_index = -1;
            needs_redraw = 1;
            goto finish;
        }
        if (taskbar_context_open && point_in_rect(mx, my, taskbar_ctx_close)) {
            if (taskbar_context_index >= 0 && taskbar_context_index < 32 && app_windows[taskbar_context_index].open) {
                app_windows[taskbar_context_index].open = 0;
                needs_redraw = 1;
            }
            taskbar_context_open = 0;
            taskbar_context_index = -1;
            needs_redraw = 1;
            goto finish;
        }

        if (desktop_context_open && !desktop_context_target && point_in_rect(mx, my, desktop_context_menu)) {
            if (point_in_rect(mx, my, desktop_ctx_icon_size_s)) {
                desktop_icon_size = ICON_SIZE_SMALL;
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, desktop_ctx_icon_size_m)) {
                desktop_icon_size = ICON_SIZE_MEDIUM;
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, desktop_ctx_icon_size_l)) {
                desktop_icon_size = ICON_SIZE_LARGE;
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, desktop_ctx_add_widgets)) {
                widgets_window_open = 1;
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, desktop_ctx_refresh)) {
                sync_desktop_icons_from_fs();
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, desktop_ctx_new_folder)) {
                create_desktop_folder();
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, desktop_ctx_new_file)) {
                create_desktop_text_file();
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, desktop_ctx_change_wall)) {
                desktop_cycle_wallpaper();
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, desktop_ctx_sort_name)) {
                desktop_sort_mode = DESKTOP_SORT_NAME;
                sort_desktop_items();
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, desktop_ctx_sort_date)) {
                desktop_sort_mode = DESKTOP_SORT_DATE;
                sort_desktop_items();
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, desktop_ctx_sort_kind)) {
                desktop_sort_mode = DESKTOP_SORT_KIND;
                sort_desktop_items();
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, desktop_ctx_paste)) {
                desktop_paste_file();
                desktop_context_open = 0;
                needs_redraw = 1;
                goto finish;
            }
        }

        if (desktop_context_open && desktop_context_target && point_in_rect(mx, my, desktop_ctx_rename)) {
            if (desktop_context_index >= 0 && desktop_context_index < desktop_item_count) begin_desktop_item_rename(desktop_context_index);
            desktop_context_open = 0;
            desktop_context_target = 0;
            desktop_context_index = -1;
            needs_redraw = 1;
            goto finish;
        }
        if (desktop_context_open && desktop_context_target && point_in_rect(mx, my, desktop_ctx_delete)) {
            if (fs_ready()) {
                if (desktop_context_index >= 0 && desktop_context_index < desktop_item_count) {
                    char trash_path[256];
                    int tk = 0;
                    trash_path[tk++]='/'; trash_path[tk++]='T'; trash_path[tk++]='r'; trash_path[tk++]='a'; trash_path[tk++]='s'; trash_path[tk++]='h'; trash_path[tk++]='/';
                    int ni = 0;
                    while (desktop_items[desktop_context_index].name[ni] && tk < 255) {
                        trash_path[tk++] = desktop_items[desktop_context_index].name[ni++];
                    }
                    trash_path[tk] = 0;
                    extern int fs_rename_at(const char *old_path, const char *new_path);
                    fs_rename_at(desktop_items[desktop_context_index].path, trash_path);
                    sync_desktop_icons_from_fs();
                }
            }
            desktop_context_open = 0;
            desktop_context_target = 0;
            desktop_context_index = -1;
            needs_redraw = 1;
            goto finish;
        }

        if (point_in_rect(mx, my, top_brand_button)) { brand_menu_open = !brand_menu_open; app_menu_open = 0; file_menu_open = 0; edit_menu_open = 0; view_menu_open = 0; start_menu_open = 0; clock_menu_open = 0; weather_popover_open = 0; notification_center_open = 0; fps_dropdown_open = 0; desktop_context_open = 0; needs_redraw = 1; goto finish; }
        if (point_in_rect(mx, my, top_app_menu_button)) { app_menu_open = !app_menu_open; brand_menu_open = 0; file_menu_open = 0; edit_menu_open = 0; view_menu_open = 0; start_menu_open = 0; clock_menu_open = 0; weather_popover_open = 0; notification_center_open = 0; fps_dropdown_open = 0; desktop_context_open = 0; needs_redraw = 1; goto finish; }
        if (point_in_rect(mx, my, top_file_menu_button)) { file_menu_open = !file_menu_open; brand_menu_open = 0; app_menu_open = 0; edit_menu_open = 0; view_menu_open = 0; start_menu_open = 0; clock_menu_open = 0; weather_popover_open = 0; notification_center_open = 0; fps_dropdown_open = 0; desktop_context_open = 0; needs_redraw = 1; goto finish; }
        if (point_in_rect(mx, my, top_edit_menu_button)) { edit_menu_open = !edit_menu_open; brand_menu_open = 0; app_menu_open = 0; file_menu_open = 0; view_menu_open = 0; start_menu_open = 0; clock_menu_open = 0; weather_popover_open = 0; notification_center_open = 0; fps_dropdown_open = 0; desktop_context_open = 0; needs_redraw = 1; goto finish; }
        if (point_in_rect(mx, my, top_view_menu_button)) { view_menu_open = !view_menu_open; brand_menu_open = 0; app_menu_open = 0; file_menu_open = 0; edit_menu_open = 0; start_menu_open = 0; clock_menu_open = 0; weather_popover_open = 0; notification_center_open = 0; fps_dropdown_open = 0; desktop_context_open = 0; needs_redraw = 1; goto finish; }
        if (point_in_rect(mx, my, top_search_button)) { 
            spotlight_open = !spotlight_open; 
            if (spotlight_open) { spotlight_qlen = 0; spotlight_query[0] = 0; spotlight_search(""); }
            clock_menu_open = 0; weather_popover_open = 0; notification_center_open = 0; fps_dropdown_open = 0; brand_menu_open = 0; start_menu_open = 0; app_menu_open = 0; file_menu_open = 0; edit_menu_open = 0; view_menu_open = 0; volume_menu_open = 0; wifi_menu_open = 0; desktop_context_open = 0; 
            needs_redraw = 1; goto finish; 
        }
        if (point_in_rect(mx, my, top_fps_button)) { 
            fps_dropdown_open = !fps_dropdown_open; 
            notification_center_open = 0; weather_popover_open = 0; clock_menu_open = 0; brand_menu_open = 0; start_menu_open = 0; app_menu_open = 0; file_menu_open = 0; edit_menu_open = 0; view_menu_open = 0; volume_menu_open = 0; wifi_menu_open = 0; desktop_context_open = 0; 
            needs_redraw = 1; goto finish; 
        }
        if (keyboard_caps_lock && point_in_rect(mx, my, top_caps_button)) {
            keyboard_caps_lock = 0;
            sound_play_event(SOUND_EVENT_CLICK);
            needs_redraw = 1;
            goto finish;
        }
        if (point_in_rect(mx, my, top_weather_button)) { 
            weather_popover_open = !weather_popover_open; 
            notification_center_open = 0; fps_dropdown_open = 0; clock_menu_open = 0; brand_menu_open = 0; start_menu_open = 0; app_menu_open = 0; file_menu_open = 0; edit_menu_open = 0; view_menu_open = 0; volume_menu_open = 0; wifi_menu_open = 0; desktop_context_open = 0; 
            needs_redraw = 1; goto finish; 
        }
        if (point_in_rect(mx, my, top_notif_button)) { 
            notification_center_open = !notification_center_open; 
            if (notification_center_open) sound_play_event(SOUND_EVENT_NOTIFICATION);
            weather_popover_open = 0; fps_dropdown_open = 0; clock_menu_open = 0; brand_menu_open = 0; start_menu_open = 0; app_menu_open = 0; file_menu_open = 0; edit_menu_open = 0; view_menu_open = 0; volume_menu_open = 0; wifi_menu_open = 0; desktop_context_open = 0; 
            needs_redraw = 1; goto finish; 
        }
        if (point_in_rect(mx, my, top_clock_button)) { 
            clock_menu_open = !clock_menu_open; 
            weather_popover_open = 0; fps_dropdown_open = 0; notification_center_open = 0; brand_menu_open = 0; start_menu_open = 0; app_menu_open = 0; file_menu_open = 0; edit_menu_open = 0; view_menu_open = 0; volume_menu_open = 0; wifi_menu_open = 0; desktop_context_open = 0; 
            needs_redraw = 1; goto finish; 
        }
        if (point_in_rect(mx, my, top_volume_button)) { 
            volume_menu_open = !volume_menu_open; 
            weather_popover_open = 0; fps_dropdown_open = 0; notification_center_open = 0; wifi_menu_open = 0; clock_menu_open = 0; brand_menu_open = 0; start_menu_open = 0; app_menu_open = 0; file_menu_open = 0; edit_menu_open = 0; view_menu_open = 0; desktop_context_open = 0; 
            needs_redraw = 1; goto finish; 
        }
        if (point_in_rect(mx, my, top_wifi_button)) { 
            wifi_menu_open = !wifi_menu_open; 
            if (wifi_menu_open) {
                extern void net_rescan_wifi(void);
                net_rescan_wifi();
            }
            weather_popover_open = 0; fps_dropdown_open = 0; notification_center_open = 0; volume_menu_open = 0; clock_menu_open = 0; brand_menu_open = 0; start_menu_open = 0; app_menu_open = 0; file_menu_open = 0; edit_menu_open = 0; view_menu_open = 0; desktop_context_open = 0; 
            needs_redraw = 1; goto finish; 
        }
        
        if (wifi_menu_open) {
            if (point_in_rect(mx, my, top_wifi_menu)) {
                int is_on = net_is_enabled();
                int is_conn = net_is_connected();
                int ap_count = net_get_ap_count();
                int menu_x = top_wifi_menu.x;
                int menu_w = top_wifi_menu.w;

                if (!is_on) {
                    int cur_y = top_wifi_menu.y + 8;
                    if (point_in_rect(mx, my, (rect_t){menu_x + 16, cur_y + 24, menu_w - 32, 22, 0})) {
                        net_set_enabled(1);
                        needs_redraw = 1;
                        goto finish;
                    }
                    wifi_menu_open = 0;
                    needs_redraw = 1;
                    goto finish;
                }

                if (is_conn) {
                    int pill_y = top_wifi_menu.y + 8;
                    if (my >= pill_y && my <= pill_y + 18 && mx >= menu_x + menu_w - 78 && mx <= menu_x + menu_w - 10) {
                        net_disconnect();
                        current_wifi_network = -1;
                        wifi_menu_open = 0;
                        needs_redraw = 1;
                        goto finish;
                    }
                }

                int cur_y = top_wifi_menu.y + 8;
                if (is_conn) cur_y += 20 + 18 + 18 + 20;
                else cur_y += 22;
                cur_y += 6 + 16; // separator + title

                for (int i = 0; i < ap_count; i++) {
                    int row_y = cur_y + i * 24;
                    if (my >= row_y && my < row_y + 24) {
                        const wifi_ap_t *ap = net_get_ap(i);
                        if (ap) {
                            if (ap->connected) {
                                net_disconnect();
                                current_wifi_network = -1;
                            } else if (ap->encrypted) {
                                if (ap->expected_pass[0] != 0) {
                                    int l = 0;
                                    while (ap->expected_pass[l]) l++;
                                    if (l >= 8 && net_connect(i, ap->expected_pass)) {
                                        current_wifi_network = i;
                                    } else {
                                        wifi_password_modal_open_for_ap(i);
                                    }
                                } else {
                                    wifi_password_modal_open_for_ap(i);
                                }
                            } else {
                                net_connect(i, "");
                                current_wifi_network = i;
                            }
                        }
                        wifi_menu_open = 0;
                        needs_redraw = 1;
                        goto finish;
                    }
                }

                cur_y += ap_count * 24 + 4 + 4;
                if (my >= cur_y && my < cur_y + 24) {
                    wifi_menu_open = 0;
                    open_app(APP_SETTINGS);
                    settings_set_tab(8);
                    needs_redraw = 1;
                    goto finish;
                }
            } else {
                wifi_menu_open = 0;
                needs_redraw = 1;
                goto finish;
            }
        }
        
        if (volume_menu_open) {
            if (point_in_rect(mx, my, top_volume_slider)) {
                system_volume = ((mx - top_volume_slider.x) * 100) / top_volume_slider.w;
                if (system_volume < 0) system_volume = 0;
                if (system_volume > 100) system_volume = 100;
                hda_set_volume(system_volume);
                needs_redraw = 1;
                goto finish;
            }
            if (!point_in_rect(mx, my, top_volume_menu)) {
                volume_menu_open = 0;
                needs_redraw = 1;
                goto finish;
            }
        }

        if (weather_popover_open) {
            if (point_in_rect(mx, my, weather_popover_rect)) {
                int px = weather_popover_rect.x;
                int py = weather_popover_rect.y;
                int pw = weather_popover_rect.w;
                rect_t input_rect = {px + 16, py + 132, pw - 32, 24, 0};
                if (point_in_rect(mx, my, input_rect)) {
                    weather_input_focused = 1;
                    needs_redraw = 1;
                    goto finish;
                } else {
                    weather_input_focused = 0;
                }
                static const char *pill_names[6] = {"NYC", "LON", "TYO", "PAR", "BER", "SF"};
                int pill_w = 36;
                int pill_gap = 4;
                int p_start_x = px + 16;
                int p_y = py + 164;
                for (int p = 0; p < 6; p++) {
                    rect_t pr = {p_start_x + p * (pill_w + pill_gap), p_y, pill_w, 20, 0};
                    if (point_in_rect(mx, my, pr)) {
                        weather_set_city(pill_names[p]);
                        weather_input_focused = 0;
                        needs_redraw = 1;
                        goto finish;
                    }
                }
                needs_redraw = 1;
                goto finish;
            } else if (!point_in_rect(mx, my, top_weather_button)) {
                weather_popover_open = 0;
                weather_input_focused = 0;
                needs_redraw = 1;
                goto finish;
            }
        }

        if (notification_center_open) {
            if (point_in_rect(mx, my, notif_center_rect)) {
                int nx = notif_center_rect.x;
                int ny = notif_center_rect.y;
                int nw = notif_center_rect.w;
                int nh = notif_center_rect.h;
                int dnd_y = ny + 32;
                rect_t dnd_card = {nx + 12, dnd_y, nw - 24, 38, 0};
                if (point_in_rect(mx, my, dnd_card)) {
                    notification_dnd = !notification_dnd;
                    needs_redraw = 1;
                    goto finish;
                }
                if (notification_count > 0) {
                    rect_t btn_del_all = {nx + nw - 92, ny + 84, 80, 20, 0};
                    if (point_in_rect(mx, my, btn_del_all)) {
                        for (int k = 0; k < MAX_NOTIFICATIONS; k++) {
                            gui_notifications[k].active = 0;
                        }
                        notification_count = 0;
                        needs_redraw = 1;
                        goto finish;
                    }
                }
                int card_y = ny + 110;
                for (int k = 0; k < MAX_NOTIFICATIONS; k++) {
                    if (!gui_notifications[k].active) continue;
                    rect_t card = {nx + 12, card_y, nw - 24, 60, 0};
                    rect_t btn_dism = {card.x + card.w - 22, card.y + 6, 16, 16, 0};
                    if (point_in_rect(mx, my, btn_dism)) {
                        gui_notifications[k].active = 0;
                        notification_count--;
                        if (notification_count < 0) notification_count = 0;
                        needs_redraw = 1;
                        goto finish;
                    }
                    card_y += 66;
                    if (card_y + 60 > ny + nh) break;
                }
                needs_redraw = 1;
                goto finish;
            } else if (!point_in_rect(mx, my, top_notif_button)) {
                notification_center_open = 0;
                needs_redraw = 1;
                goto finish;
            }
        }

        if (fps_dropdown_open) {
            if (point_in_rect(mx, my, fps_dropdown_rect)) {
                needs_redraw = 1;
                goto finish;
            } else if (!point_in_rect(mx, my, top_fps_button)) {
                fps_dropdown_open = 0;
                needs_redraw = 1;
                goto finish;
            }
        }

        if (clock_menu_open) {
            if (point_in_rect(mx, my, top_clock_menu)) {
                int cx = top_clock_menu.x;
                int cy = top_clock_menu.y;
                int cw = top_clock_menu.w;
                rect_t btn_prev = {cx + cw - 52, cy + 10, 20, 20, 0};
                rect_t btn_next = {cx + cw - 28, cy + 10, 20, 20, 0};
                if (point_in_rect(mx, my, btn_prev)) {
                    clock_view_month--;
                    if (clock_view_month < 1) {
                        clock_view_month = 12;
                        clock_view_year--;
                    }
                    needs_redraw = 1;
                    goto finish;
                }
                if (point_in_rect(mx, my, btn_next)) {
                    clock_view_month++;
                    if (clock_view_month > 12) {
                        clock_view_month = 1;
                        clock_view_year++;
                    }
                    needs_redraw = 1;
                    goto finish;
                }
                needs_redraw = 1;
                goto finish;
            } else if (!point_in_rect(mx, my, top_clock_button)) {
                clock_menu_open = 0;
                needs_redraw = 1;
                goto finish;
            }
        }
        if (start_button.w > 0 && point_in_rect(mx, my, start_button)) { pressed_button = start_button; pressed_button_active = 1; gui_toggle_start_menu(); goto finish; }

        if (start_menu_open) {
            handle_start_menu_click(mx, my);
            goto finish;
        }

        {
            {
                if (top_hit >= 0 && top_hit < 32 && app_windows[top_hit].kind == APP_EXPLORER && app_windows[top_hit].open && !app_windows[top_hit].minimized && point_in_rect(mx, my, app_windows[top_hit].frame) && !point_in_rect(mx, my, app_windows[top_hit].title) && !point_in_rect(mx, my, app_windows[top_hit].close) && !point_in_rect(mx, my, app_windows[top_hit].maximize) && !point_in_rect(mx, my, app_windows[top_hit].minimize)) {
                    focus_app_window(top_hit);
                    handle_explorer_click(&app_windows[top_hit], mx, my);
                    goto finish;
                }
            }
            if (top_hit == -1) {
                for (i = 0; i < desktop_item_count; i++) {
                    if (!desktop_items[i].used) continue;
                    if (!point_in_rect(mx, my, desktop_items[i].icon_rect)) continue;
                    desktop_selected = i;
                    unsigned int now = get_system_ticks();
                    int dx = mx - unified_last_click_x;
                    int dy = my - unified_last_click_y;
                    if (dx < 0) dx = -dx;
                    if (dy < 0) dy = -dy;
                    int is_double = (unified_last_click_source == 0 &&
                                     unified_last_click_index == i &&
                                     (now - unified_last_click_time) <= 60 &&
                                     dx <= 10 && dy <= 10);
                    if (is_double) {
                        serial_write("[mouse] desktop double click: opening ");
                        serial_write(desktop_items[i].name);
                        serial_write("\n");
                        unified_last_click_time = 0;
                        unified_last_click_index = -1;
                        desktop_dragging = 0;
                        desktop_drag_index = -1;
                        desktop_drag_moved = 0;
                        desktop_drag_start_time = 0;
                        desktop_items[i].last_click = 0;
                        desktop_items[i].rename_mode = 0;
                        if (desktop_items[i].is_dir) {
                            open_app(APP_EXPLORER);
                            focus_app_window(APP_EXPLORER - 1);
                            app_window_t *exp_app = app_slot(APP_EXPLORER);
                            if (exp_app) {
                                gui_fl_cpy(exp_app->current_path, desktop_items[i].path, sizeof(exp_app->current_path));
                                gui_fl_cpy(exp_app->tabs[exp_app->active_tab].path, desktop_items[i].path, sizeof(exp_app->tabs[exp_app->active_tab].path));
                            }
                            explorer_open_path(desktop_items[i].path);
                        } else {
                            explorer_open_file_default(desktop_items[i].path, desktop_items[i].name);
                        }
                    } else {
                        serial_write("[mouse] desktop single click: primed drag for ");
                        serial_write(desktop_items[i].name);
                        serial_write("\n");
                        unified_last_click_source = 0;
                        unified_last_click_index = i;
                        unified_last_click_time = now;
                        unified_last_click_x = mx;
                        unified_last_click_y = my;
                        desktop_items[i].last_click = 1;
                        desktop_items[i].last_click_ticks = 0;
                        desktop_dragging = 1;
                        desktop_drag_index = i;
                        desktop_window_drag_start_x = mx;
                        desktop_window_drag_start_y = my;
                        desktop_drag_start_time = now;
                        desktop_drag_moved = 0;
                    }
                    needs_redraw = 1;
                    goto finish;
                }
            }
            
            // If we reached here and top_hit is -1, it means we clicked empty desktop space!
            if (top_hit == -1) {
                if (widgets_handle_click(mx, my, 0)) {
                    needs_redraw = 1;
                    goto finish;
                }
                desktop_selected = -1;
                extern void desktop_clear_multi_selected(void);
                desktop_clear_multi_selected();
                desktop_marquee_active = 1;
                desktop_marquee_x = mx;
                desktop_marquee_y = my;
                desktop_marquee_mx = mx;
                desktop_marquee_my = my;
                needs_redraw = 1;
                goto finish;
            }
        }


        {
            app_window_t *calc_app = (top_hit >= 0 && top_hit < 32 && app_windows[top_hit].kind == APP_CALCULATOR) ? &app_windows[top_hit] : app_slot(APP_CALCULATOR);
            if (calc_app && calc_app->open && !calc_app->minimized && top_hit >= 0 && top_hit < 32 && app_windows[top_hit].kind == APP_CALCULATOR) {
                int calc_idx = top_hit;
                if (point_in_rect(mx, my, calc_app->close)) { calc_app->open = 0; needs_redraw = 1; goto finish; }
                if (point_in_rect(mx, my, calc_app->maximize)) { focus_app_window(calc_idx); set_app_window_maximized(calc_app, !calc_app->maximized); goto finish; }
                if (point_in_rect(mx, my, calc_app->minimize)) { begin_window_transition(calc_idx, 1); calc_app->minimized = 1; needs_redraw = 1; goto finish; }
                if (point_in_rect(mx, my, calc_app->title)) { focus_app_window(calc_idx); window_dragging = 1; window_drag_index = calc_idx; drag_offset_x = mx - calc_app->frame.x; drag_offset_y = my - calc_app->frame.y; goto finish; }
                
                if (point_in_rect(mx, my, calc_app->frame)) {
                    focus_app_window(calc_idx);
                    int r, c;
                    for (r = 0; r < 4; r++) {
                        for (c = 0; c < 4; c++) {
                            rect_t btn = calc_btn_rect(calc_app, r, c);
                            if (point_in_rect(mx, my, btn)) {
                                if (r == 3 && c == 0) { // C
                                    calc_val = 0; calc_prev = 0; calc_op = 0; calc_new_num = 1;
                                } else if (r == 3 && c == 2) { // =
                                    if (calc_op == 1) calc_val = calc_prev + calc_val;
                                    else if (calc_op == 2) calc_val = calc_prev - calc_val;
                                    else if (calc_op == 3) calc_val = calc_prev * calc_val;
                                    else if (calc_op == 4 && calc_val != 0) calc_val = calc_prev / calc_val;
                                    calc_op = 0; calc_new_num = 1;
                                } else if (c == 3) { // ops
                                    if (r == 0) calc_op = 4; // /
                                    else if (r == 1) calc_op = 3; // *
                                    else if (r == 2) calc_op = 2; // -
                                    else if (r == 3) calc_op = 1; // +
                                    calc_prev = calc_val;
                                    calc_new_num = 1;
                                } else { // digits
                                    int d = -1;
                                    if (r == 0 && c < 3) d = 7 + c;
                                    else if (r == 1 && c < 3) d = 4 + c;
                                    else if (r == 2 && c < 3) d = 1 + c;
                                    else if (r == 3 && c == 1) d = 0;
                                    if (d >= 0) {
                                        if (calc_new_num) { calc_val = d; calc_new_num = 0; }
                                        else { calc_val = calc_val * 10 + d; }
                                    }
                                }
                                calc_format_display();
                                needs_redraw = 1;
                                goto finish;
                            }
                        }
                    }
                    focus_app_window(APP_CALCULATOR - 1);
                    goto finish;
                }
            }
            
            // If we reached here and top_hit is -1, it means we clicked empty desktop space!
            if (top_hit == -1) {
                desktop_selected = -1;
                extern void desktop_clear_multi_selected(void);
                desktop_clear_multi_selected();
                desktop_marquee_active = 1;
                desktop_marquee_x = mx;
                desktop_marquee_y = my;
                desktop_marquee_mx = mx;
                desktop_marquee_my = my;
                needs_redraw = 1;
                goto finish;
            }
        }
        {
            app_window_t *media_app = (top_hit >= 0 && top_hit < 32 && app_windows[top_hit].kind == APP_MEDIA_PLAYER) ? &app_windows[top_hit] : app_slot(APP_MEDIA_PLAYER);
            if (media_app && media_app->open && !media_app->minimized && top_hit >= 0 && top_hit < 32 && app_windows[top_hit].kind == APP_MEDIA_PLAYER) {
                int media_idx = top_hit;
                if (point_in_rect(mx, my, media_app->close)) {
                    media_player_close();
                    goto finish;
                }
                if (point_in_rect(mx, my, media_app->maximize)) { focus_app_window(media_idx); set_app_window_maximized(media_app, !media_app->maximized); goto finish; }
                if (point_in_rect(mx, my, media_app->minimize)) { begin_window_transition(media_idx, 1); media_app->minimized = 1; needs_redraw = 1; goto finish; }
                if (point_in_rect(mx, my, media_app->title)) {
                    focus_app_window(media_idx);
                    window_dragging = 1;
                    window_drag_index = media_idx;
                    drag_offset_x = mx - media_app->frame.x;
                    drag_offset_y = my - media_app->frame.y;
                    goto finish;
                }
                if (point_in_rect(mx, my, media_app->frame)) {
                    focus_app_window(media_idx);
                    media_player_handle_click(media_app, mx, my);
                    needs_redraw = 1;
                    goto finish;
                }
            }
            
            // If we reached here and top_hit is -1, it means we clicked empty desktop space!
            if (top_hit == -1) {
                desktop_selected = -1;
                extern void desktop_clear_multi_selected(void);
                desktop_clear_multi_selected();
                desktop_marquee_active = 1;
                desktop_marquee_x = mx;
                desktop_marquee_y = my;
                desktop_marquee_mx = mx;
                desktop_marquee_my = my;
                needs_redraw = 1;
                goto finish;
            }
        }
        
        {
            app_window_t *preview_app = (top_hit >= 0 && top_hit < 32 && app_windows[top_hit].kind == APP_PREVIEW) ? &app_windows[top_hit] : app_slot(APP_PREVIEW);
            if (preview_app && preview_app->open && !preview_app->minimized && top_hit >= 0 && top_hit < 32 && app_windows[top_hit].kind == APP_PREVIEW) {
                int prev_idx = top_hit;
                if (point_in_rect(mx, my, preview_app->close)) {
                    preview_app->open = 0; needs_redraw = 1;
                    preview_close();
                    goto finish;
                }
                if (point_in_rect(mx, my, preview_app->maximize)) { focus_app_window(prev_idx); set_app_window_maximized(preview_app, !preview_app->maximized); goto finish; }
                if (point_in_rect(mx, my, preview_app->minimize)) { begin_window_transition(prev_idx, 1); preview_app->minimized = 1; needs_redraw = 1; goto finish; }
                if (point_in_rect(mx, my, preview_app->title)) {
                    focus_app_window(prev_idx);
                    window_dragging = 1;
                    window_drag_index = prev_idx;
                    drag_offset_x = mx - preview_app->frame.x;
                    drag_offset_y = my - preview_app->frame.y;
                    goto finish;
                }
                if (point_in_rect(mx, my, preview_app->frame)) {
                    focus_app_window(prev_idx);
                    preview_handle_click(preview_app, mx, my);
                    needs_redraw = 1;
                    goto finish;
                }
            }
            
            // If we reached here and top_hit is -1, it means we clicked empty desktop space!
            if (top_hit == -1) {
                desktop_selected = -1;
                extern void desktop_clear_multi_selected(void);
                desktop_clear_multi_selected();
                desktop_marquee_active = 1;
                desktop_marquee_x = mx;
                desktop_marquee_y = my;
                desktop_marquee_mx = mx;
                desktop_marquee_my = my;
                needs_redraw = 1;
                goto finish;
            }
        }

        
        {
            app_window_t *photo_app = (top_hit >= 0 && top_hit < 32 && app_windows[top_hit].kind == APP_PHOTO_EDITOR) ? &app_windows[top_hit] : app_slot(APP_PHOTO_EDITOR);
            if (photo_app && photo_app->open && !photo_app->minimized && top_hit >= 0 && top_hit < 32 && app_windows[top_hit].kind == APP_PHOTO_EDITOR) {
                int photo_idx = top_hit;
                if (point_in_rect(mx, my, photo_app->close)) {
                    photo_app->open = 0; needs_redraw = 1;
                    goto finish;
                }
                if (point_in_rect(mx, my, photo_app->maximize)) { focus_app_window(photo_idx); set_app_window_maximized(photo_app, !photo_app->maximized); goto finish; }
                if (point_in_rect(mx, my, photo_app->minimize)) { begin_window_transition(photo_idx, 1); photo_app->minimized = 1; needs_redraw = 1; goto finish; }
                if (point_in_rect(mx, my, photo_app->title)) {
                    focus_app_window(photo_idx);
                    window_dragging = 1;
                    window_drag_index = photo_idx;
                    drag_offset_x = mx - photo_app->frame.x;
                    drag_offset_y = my - photo_app->frame.y;
                    goto finish;
                }
                if (point_in_rect(mx, my, photo_app->frame)) {
                    focus_app_window(photo_idx);
                    handle_photo_viewer_click(photo_app, mx, my);
                    needs_redraw = 1;
                    goto finish;
                }
            }
            
            // If we reached here and top_hit is -1, it means we clicked empty desktop space!
            if (top_hit == -1) {
                desktop_selected = -1;
                extern void desktop_clear_multi_selected(void);
                desktop_clear_multi_selected();
                desktop_marquee_active = 1;
                desktop_marquee_x = mx;
                desktop_marquee_y = my;
                desktop_marquee_mx = mx;
                desktop_marquee_my = my;
                needs_redraw = 1;
                goto finish;
            }
        }
        
        {
            app_window_t *settings_app = (top_hit >= 0 && top_hit < 32 && app_windows[top_hit].kind == APP_SETTINGS) ? &app_windows[top_hit] : app_slot(APP_SETTINGS);
            if (settings_app && settings_app->open && !settings_app->minimized && top_hit >= 0 && top_hit < 32 && app_windows[top_hit].kind == APP_SETTINGS) {
                int set_idx = top_hit;
                if (point_in_rect(mx, my, settings_app->close)) { settings_app->open = 0; needs_redraw = 1; goto finish; }
                if (point_in_rect(mx, my, settings_app->maximize)) { focus_app_window(set_idx); set_app_window_maximized(settings_app, !settings_app->maximized); goto finish; }
                if (point_in_rect(mx, my, settings_app->minimize)) { begin_window_transition(set_idx, 1); settings_app->minimized = 1; needs_redraw = 1; goto finish; }
                if (point_in_rect(mx, my, settings_app->title)) {
                    focus_app_window(set_idx);
                    window_dragging = 1;
                    window_drag_index = set_idx;
                    drag_offset_x = mx - settings_app->frame.x;
                    drag_offset_y = my - settings_app->frame.y;
                    goto finish;
                }
                if (point_in_rect(mx, my, settings_app->frame)) {
                    focus_app_window(set_idx);
                    if (settings_scroll_hit(settings_app, mx, my)) {
                        int panel_y = settings_app->frame.y + 42;
                        int panel_h = settings_app->frame.h - 56;
                        int top = panel_y + 42;
                        int bottom = panel_y + panel_h - 6;
                        int track = bottom - top;
                        int thumb = track - 18;
                        int pos = settings_scroll ? (track - 18) : 0;
                        if (my >= top + pos && my <= top + pos + thumb) {
                            settings_scroll_dragging = 1;
                            settings_scroll_drag_offset = my - (top + pos);
                        } else if (my < top + track / 2) settings_scroll = 0;
                        else settings_scroll = 1;
                        needs_redraw = 1;
                    } else handle_settings_click(settings_app, mx, my);
                    goto finish;
                }
            }
            
            // If we reached here and top_hit is -1, it means we clicked empty desktop space!
            if (top_hit == -1) {
                desktop_selected = -1;
                extern void desktop_clear_multi_selected(void);
                desktop_clear_multi_selected();
                desktop_marquee_active = 1;
                desktop_marquee_x = mx;
                desktop_marquee_y = my;
                desktop_marquee_mx = mx;
                desktop_marquee_my = my;
                needs_redraw = 1;
                goto finish;
            }
        }

        if (main_window_open && !main_window_minimized) {
            if (point_in_rect(mx, my, main_button_close)) { main_window_open = 0; needs_redraw = 1; goto finish; }
            if (point_in_rect(mx, my, main_button_max)) { pressed_button = main_button_max; pressed_button_active = 1; set_main_window_fullscreen(!fullscreen_mode); goto finish; }
            if (point_in_rect(mx, my, main_button_min)) { main_window_minimized = 1; needs_redraw = 1; goto finish; }
        }

        for (int z = 31; z >= 0; z--) {
            i = z_order[z];
            if (!app_windows[i].open || app_windows[i].minimized) continue;
            
            if (app_windows[i].kind == APP_IDE) {
                
                extern int fs_count_in_dir(const char *dir);
                extern const fs_entry_t *fs_entry_in_dir(const char *dir, int visible_index);
                extern void ide_open_in_new_tab(const char *path);
                
                int sidebar_w = 130;
                int sb_x = app_windows[i].frame.x + 16;
                int sb_y = app_windows[i].frame.y + 46;
                int ew = app_windows[i].frame.w - 32 - sidebar_w;
                
                                // SAVE button
                if (point_in_rect(mx, my, (rect_t){app_windows[i].frame.x + 16 + 130 + ew - 94, app_windows[i].frame.y + 46 + 5, 78, 18, 0})) {
                    // if filename is empty, open save dialog. Else, save directly.
                    if (ide_tabs[ide_active_tab].filename[0] == 0) {
                        file_dialog_show(APP_IDE, FILE_DIALOG_SAVE, "script.fl", "/Desktop");
                    } else {
                        
                        char path[128]; path[0]=0;
                        if (ide_tabs[ide_active_tab].filename[0] != '/') gui_fl_cat(path, "/", 128);
                        gui_fl_cat(path, ide_tabs[ide_active_tab].filename, 128);
                        fs_write_at(path, ide_text, ide_length);
                        ide_saved = 1;
                    }
                    needs_redraw = 1;
                    goto finish;
                }
                // CLEAR button
                if (point_in_rect(mx, my, (rect_t){app_windows[i].frame.x + 16 + sidebar_w + ew - 178, app_windows[i].frame.y + 46 + 5, 74, 18, 0})) {
                    extern void ide_clear(void);
                    ide_clear();
                    needs_redraw = 1;
                    goto finish;
                }
                
                // Tabs
                for(int ti=0; ti<4; ti++) {
                    int tx = app_windows[i].frame.x + 16 + sidebar_w + 4 + ti*60;
                    int ty = app_windows[i].frame.y + 46 + 34;
                    if (point_in_rect(mx, my, (rect_t){tx, ty, 56, 18, 0})) {
                        ide_switch_tab(ti);
                        needs_redraw = 1;
                        goto finish;
                    }
                }
                
                // Sidebar files
                if (mx >= sb_x && mx <= sb_x + 120 && my >= sb_y + 26 && my <= sb_y + app_windows[i].frame.h - 60) {
                    int clicked_idx = (my - (sb_y + 26)) / 16;
                    int f_count = fs_count_in_dir("/");
                    int match_idx = 0;
                    for(int fi=0; fi<f_count; fi++) {
                        const fs_entry_t *fe = fs_entry_in_dir("/", fi);
                        if(fe && !fe->is_dir && gui_fl_ends(fe->name, ".fl")) {
                            if(match_idx == clicked_idx) {
                                char path[64]; path[0]=0;
                                
                                gui_fl_cat(path, "/", 64); gui_fl_cat(path, fe->name, 64);
                                ide_open_in_new_tab(path);
                                needs_redraw = 1;
                                goto finish;
                            }
                            match_idx++;
                        }
                    }
                }
            }

            i = z_order[z];
            if (!app_windows[i].open || app_windows[i].minimized) continue;
            if (point_in_rect(mx, my, app_windows[i].close)) {
                if (app_windows[i].kind == APP_TEXT_EDIT && !editor_saved && editor_length > 0) {
                    text_edit_unsaved_dialog_open = 1;
                    text_edit_closing_app_index = i;
                    needs_redraw = 1;
                    goto finish;
                }
                app_windows[i].open = 0;
                needs_redraw = 1;
                goto finish;
            }
            if (app_windows[i].kind == APP_IDE) {
                int ti;
                int tx_base = app_windows[i].frame.x + 16 + 8;
                int ty = app_windows[i].frame.y + 46 + 42;
                for (ti = 0; ti < 4; ti++) {
                    if (point_in_rect(mx, my, (rect_t){tx_base + ti * 80, ty, 76, 18, 0})) {
                        extern void ide_switch_tab(int);
                        ide_switch_tab(ti);
                        needs_redraw = 1;
                        goto finish;
                    }
                }
                if (point_in_rect(mx, my, (rect_t){tx_base + 4 * 80, ty, 18, 18, 0})) {
                    // New Tab button! Find empty tab and switch to it.
                    extern ide_tab_t ide_tabs[4];
                    extern void ide_switch_tab(int);
                    for (ti = 0; ti < 4; ti++) {
                        if (!ide_tabs[ti].filename[0] && !ide_tabs[ti].text[0]) {
                            ide_switch_tab(ti);
                            needs_redraw = 1;
                            goto finish;
                        }
                    }
                }
            }
            if (app_windows[i].kind == APP_IDE) {
                // Check clicking sidebar files in Project Tree
                int sb_x = app_windows[i].frame.x + 16;
                int sb_y = app_windows[i].frame.y + 46;
                int sb_w = 120;
                int fy = sb_y + 42;
                for (int fi = 0; fi < g_app_studio.file_count; fi++) {
                    if (point_in_rect(mx, my, (rect_t){sb_x + 4, fy - 1, sb_w - 8, 14, 0})) {
                        g_app_studio.selected_file_idx = fi;
                        extern void ide_open_file(const char *path);
                        ide_open_file(g_app_studio.files[fi].path);
                        needs_redraw = 1;
                        goto finish;
                    }
                    fy += 16;
                }

                // Check toolbar action buttons
                extern void handle_ide_click(app_window_t *app, int mx, int my);
                handle_ide_click(&app_windows[i], mx, my);
                needs_redraw = 1;
            }
            if (app_windows[i].kind == APP_IDE && point_in_rect(mx, my, (rect_t){app_windows[i].frame.x + 16 + (app_windows[i].frame.w - 32 - 130) + 130 - 344, app_windows[i].frame.y + 46 + 5, 74, 18, 0})) {
                ide_designer_mode = !ide_designer_mode; needs_redraw = 1; goto finish;
            }
            if (app_windows[i].kind == APP_IDE && ide_designer_mode) {
                if (handle_ui_builder_click(mx, my)) { needs_redraw = 1; goto finish; }
            }
            if (app_windows[i].kind == APP_IDE && point_in_rect(mx, my, (rect_t){app_windows[i].frame.x + 16 + (app_windows[i].frame.w - 32 - 130) + 130 - 262, app_windows[i].frame.y + 46 + 5, 74, 18, 0})) {
            extern int firstlang_run(const char *source, char *output_buf, int output_sz);
            char out[1024]; out[0]=0;
            firstlang_run(ide_text, out, 1024);
            
            extern int firstlang_has_ui(void);
            if (!firstlang_has_ui() && out[0]) {
                open_app(APP_TERMINAL);
                extern void terminal_print(const char *text);
                terminal_print("--- IDE Output ---\n");
                terminal_print(out);
                terminal_print("\n");
                focus_app_window(APP_TERMINAL - 1);
            }
            needs_redraw = 1;
            break;
        }
            if (point_in_rect(mx, my, app_windows[i].maximize)) { pressed_button = app_windows[i].maximize; pressed_button_active = 1; focus_app_window(i); set_app_window_maximized(&app_windows[i], !app_windows[i].maximized); goto finish; }
            if (point_in_rect(mx, my, app_windows[i].minimize)) { focus_app_window(i); app_windows[i].minimized = 1; needs_redraw = 1; goto finish; }
            if (app_windows[i].kind == APP_TEXT_EDIT) {
                if (point_in_rect(mx, my, app_windows[i].title)) {
                    focus_app_window(i);
                    window_dragging = 1;
                    window_drag_index = i;
                    drag_offset_x = mx - app_windows[i].frame.x;
                    drag_offset_y = my - app_windows[i].frame.y;
                    goto finish;
                }
                if (point_in_rect(mx, my, app_windows[i].frame)) {
                    focus_app_window(i);
                    extern void handle_text_edit_click(app_window_t *app, int mx, int my);
                    handle_text_edit_click(&app_windows[i], mx, my);
                    if (point_in_rect(mx, my, editor_text_area)) {
                        editor_mouse_selecting = 1;
                    }
                    needs_redraw = 1;
                    goto finish;
                }
            }
            if (app_windows[i].kind == APP_IDE && point_in_rect(mx, my, ide_text_area)) {
                focus_app_window(i);
                ide_cursor = ide_position_from_point(mx, my);
                ide_select_anchor = ide_cursor;
                ide_select_active = ide_cursor;
                ide_mouse_selecting = 1;
                needs_redraw = 1;
                goto finish;
            }
            if (app_windows[i].kind == APP_TASKMAN) {
                focus_app_window(i);
                handle_taskman_click(&app_windows[i], mx, my);
                needs_redraw = 1;
                goto finish;
            }
            if (app_windows[i].kind == APP_EXPLORER && point_in_rect(mx, my, app_windows[i].frame)) {
                focus_app_window(i);
                if (point_in_rect(mx, my, app_windows[i].title)) {
                    window_dragging = 1;
                    window_drag_index = i;
                    drag_offset_x = mx - app_windows[i].frame.x;
                    drag_offset_y = my - app_windows[i].frame.y;
                } else {
                    handle_explorer_click(&app_windows[i], mx, my);
                }
                goto finish;
            }
            if (app_windows[i].kind == APP_TERMINAL && point_in_rect(mx, my, app_windows[i].frame)) {
                focus_app_window(i);
                if (point_in_rect(mx, my, app_windows[i].title)) {
                    window_dragging = 1;
                    window_drag_index = i;
                    drag_offset_x = mx - app_windows[i].frame.x;
                    drag_offset_y = my - app_windows[i].frame.y;
                } else {
                    extern void terminal_mouse_down(int mx, int my, int frame_x, int frame_y, int frame_w, int frame_h);
                    terminal_mouse_down(mx, my, app_windows[i].frame.x, app_windows[i].frame.y, app_windows[i].frame.w, app_windows[i].frame.h);
                }
                needs_redraw = 1;
                goto finish;
            }
            if (app_windows[i].kind == APP_BROWSER && point_in_rect(mx, my, app_windows[i].frame)) {
                focus_app_window(i);
                if (point_in_rect(mx, my, app_windows[i].title)) {
                    window_dragging = 1;
                    window_drag_index = i;
                    drag_offset_x = mx - app_windows[i].frame.x;
                    drag_offset_y = my - app_windows[i].frame.y;
                } else {
                    web_browser_app_handle_click(&app_windows[i], mx, my);
                    gui_window_dirty_only = 0;
                }
                needs_redraw = 1;
                goto finish;
            }
            if (point_in_rect(mx, my, app_windows[i].title)) {
                focus_app_window(i);
                window_dragging = 1;
                window_drag_index = i;
                drag_offset_x = mx - app_windows[i].frame.x;
                drag_offset_y = my - app_windows[i].frame.y;
                goto finish;
            }
            if (point_in_rect(mx, my, app_windows[i].frame)) { focus_app_window(i); goto finish; }
        }

        if (left && !window_dragging && !settings_scroll_dragging && !explorer_dragging && point_in_rect(mx, my, main_title_bar)) {
            dragging = 1;
            window_drag_kind = 0;
            drag_offset_x = mx - main_window.x;
            drag_offset_y = my - main_window.y;
            goto finish;
        }

        begin_app_drag(mx, my);
    }

    if (dragging && left) {
        if (window_drag_kind == 0) {
            main_window.x = mx - drag_offset_x;
            main_window.y = my - drag_offset_y;
            sync_main_window_parts();
        } else {
            app_windows[5].frame.x = mx - drag_offset_x;
            app_windows[5].frame.y = my - drag_offset_y;
            sync_app_window(&app_windows[5]);
        }
        needs_redraw = 1;
    }

finish:
    if (!left) {
        dragging = 0;
        pressed_button_active = 0;
    }
    {
        app_window_t *app = active_app_window();
        if (app && app->open && !app->minimized) {
            int mx = ps2_mouse_x();
            int my = ps2_mouse_y();
            
            // Enhanced resize with edge detection
            int resize_edge = get_resize_edge(app, mx, my);
            if (left && !prev_left && resize_edge > 0) {
                app->resizing = 1;
                window_resizing_edge = resize_edge;
            }
            if (!left) {
                app->resizing = 0;
                window_resizing_edge = 0;
            }
            if (left && app->resizing && !app->maximized) {
                int nw = app->frame.w;
                int nh = app->frame.h;
                
                if (window_resizing_edge == 1 || window_resizing_edge == 3) {
                    nw = mx - app->frame.x;
                }
                if (window_resizing_edge == 2 || window_resizing_edge == 3) {
                    nh = my - app->frame.y;
                }
                if (nw < app->min_w) nw = app->min_w;
                if (nh < app->min_h) nh = app->min_h;
                app->frame.w = nw;
                app->frame.h = nh;
                sync_app_window(app);
                needs_redraw = 1;
            }
        }
    }
    prev_left = left;
    prev_right_button = right;
    for (i = 0; i < 16; i++) if (app_windows[i].open) sync_app_window(&app_windows[i]);
}

/* ================================================================
// Legacy appdock replaced by dock.c

/* ================================================================
   LAUNCHER OVERLAY (Launchpad)
   ================================================================ */
static void draw_launcher_overlay(int mouse_x, int mouse_y) {
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
            if (!browser_ico_loaded) {
                browser_ico_loaded = 1;
                load_ico_from_path("/icons/browser.ico", browser_ico_pixels, &browser_ico_width, &browser_ico_height);
            }
            if (browser_ico_width > 0) {
                draw_icon_scaled(browser_ico_pixels, browser_ico_width, browser_ico_height, cx + cell_w/2 - 16, cy + 16, 32, 32);
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
   SPOTLIGHT
   ================================================================ */
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

static void sp_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max-1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

// legacy spotlight removed

static void draw_spotlight_overlay(void) {
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
            extern void terminal_draw(int frame_x, int frame_y, int frame_w, int frame_h);
            extern int terminal_cursor_visible;
            extern int terminal_focused;
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

    /* Results */
    int rh = 36;
    if (spotlight_num_results == 0 && spotlight_qlen > 0) {
        fb_rect(bx, by + 44, bw, rh, 0x101E30);
        outline(bx, by + 44, bw, rh, 0x2A3A50);
        draw_label(bx + 20, by + 56, "No results", 0x3A5070);
    } else {
        fb_rect(bx, by + 44, bw, spotlight_num_results * rh, 0x101E30);
        outline(bx, by + 44, bw, spotlight_num_results * rh, 0x2A3A50);
    }
    for (int i = 0; i < spotlight_num_results; i++) {
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
    draw_label(bx + 10, by + 44 + spotlight_num_results * rh + 6, "Up/Down: navigate  Tab: Dropdown/Preview  Enter: open  Esc: close", 0x2A3A50);
}

static void draw_explorer_quick_look(void) {
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

void gui_update_screen_size(void) {
    desktop_bar = (rect_t){0, 0, (int)fb.width, 28, desktop_panel};
    dock_rect = (rect_t){0, (int)fb.height - 68, (int)fb.width, 68, dock_bg};
    top_brand_button = (rect_t){8, 4, 32, 20, desktop_panel};
    if (gui_start_button_visible) {
        start_button = (rect_t){8, (int)fb.height - 48, 64, 36, 0};
    } else {
        start_button = (rect_t){0, (int)fb.height - 48, 0, 0, 0};
    }
    top_app_menu_button = (rect_t){40, 4, 80, 20, desktop_panel};
    top_file_menu_button = (rect_t){120, 4, 46, 20, desktop_panel};
    top_edit_menu_button = (rect_t){168, 4, 46, 20, desktop_panel};
    int cur_r = (int)fb.width - 8;
    cur_r -= 24;
    top_notif_button = (rect_t){cur_r, 4, 24, 20, desktop_panel};
    cur_r -= 6;
    cur_r -= 120;
    top_clock_button = (rect_t){cur_r, 4, 120, 20, desktop_panel};
    cur_r -= 6;
    cur_r -= 76;
    top_weather_button = (rect_t){cur_r, 4, 76, 20, desktop_panel};
    cur_r -= 6;
    cur_r -= 28;
    top_volume_button = (rect_t){cur_r, 4, 28, 20, desktop_panel};
    cur_r -= 4;
    cur_r -= 28;
    top_wifi_button = (rect_t){cur_r, 4, 28, 20, desktop_panel};
    cur_r -= 4;
    cur_r -= 24;
    top_search_button = (rect_t){cur_r, 4, 24, 20, desktop_panel};
    cur_r -= 6;
    cur_r -= 70;
    top_fps_button = (rect_t){cur_r, 4, 70, 20, desktop_panel};
    cur_r -= 6;
    cur_r -= 48;
    top_caps_button = (rect_t){cur_r, 4, 48, 20, desktop_panel};

    int f_x = top_fps_button.x - 85;
    if (f_x < 8) f_x = 8;
    if (f_x + 240 > (int)fb.width - 8) f_x = (int)fb.width - 248;
    fps_dropdown_rect = (rect_t){f_x, desktop_bar.h + 4, 240, 180, 0xF8FAFC};

    top_volume_menu = (rect_t){top_volume_button.x - 60, desktop_bar.h + 4, 150, 44, window_bg};
    top_wifi_menu = (rect_t){(int)fb.width - 240, desktop_bar.h + 4, 200, 10 + 4 * 24, window_bg};
    top_volume_slider = (rect_t){top_volume_menu.x + 36, top_volume_menu.y + 16, 100, 12, 0};
    top_clock_menu = (rect_t){(int)fb.width - 296, desktop_bar.h + 4, 288, 252, 0xF8FAFC};
    weather_popover_rect = (rect_t){(int)fb.width - 280, desktop_bar.h + 4, 268, 224, 0xF8FAFC};
    notif_center_rect = (rect_t){(int)fb.width - 328, desktop_bar.h + 4, 320, 390, 0xF8FAFC};

    start_menu_rect = (rect_t){14, (int)fb.height - 476, 252, 464, 0xD7DEE8};
    start_apps_item = (rect_t){start_menu_rect.x + 22, start_menu_rect.y + 72, start_menu_rect.w - 44, 24, 0xBBC8D8};
    start_browser_item = (rect_t){start_menu_rect.x + 22, start_menu_rect.y + 102, start_menu_rect.w - 44, 24, 0xEDE2D8};
    start_textedit_item = (rect_t){start_menu_rect.x + 22, start_menu_rect.y + 132, start_menu_rect.w - 44, 24, 0xB79F8C};
    start_explorer_item = (rect_t){start_menu_rect.x + 22, start_menu_rect.y + 162, start_menu_rect.w - 44, 24, 0xB4C6D6};
    start_media_item = (rect_t){start_menu_rect.x + 22, start_menu_rect.y + 192, start_menu_rect.w - 44, 24, 0xAEBED1};
    start_calc_item = (rect_t){start_menu_rect.x + 22, start_menu_rect.y + 222, start_menu_rect.w - 44, 24, 0xAEBED1};
    start_ide_item = (rect_t){start_menu_rect.x + 22, start_menu_rect.y + 252, start_menu_rect.w - 44, 24, 0xADBFD3};
    start_taskman_item = (rect_t){start_menu_rect.x + 22, start_menu_rect.y + 282, start_menu_rect.w - 44, 24, 0xADBFD3};
    start_demo_item = (rect_t){start_menu_rect.x + 22, start_menu_rect.y + 312, start_menu_rect.w - 44, 24, 0xADBFD3};
    start_settings_item = (rect_t){start_menu_rect.x + 22, start_menu_rect.y + 368, start_menu_rect.w - 44, 24, 0xADBFD3};
    start_restart_item = (rect_t){start_menu_rect.x + 22, start_menu_rect.y + 398, start_menu_rect.w - 44, 24, 0x9EB1C8};
    start_shutdown_item = (rect_t){start_menu_rect.x + 22, start_menu_rect.y + 428, start_menu_rect.w - 44, 24, 0xA7B9CE};
    main_window = (rect_t){72, desktop_bar.h + 22, (int)fb.width - 144, (int)fb.height - desktop_bar.h - dock_rect.h - 20, window_bg};
    sync_main_window_parts();

    for (int i = 0; i < 32; i++) {
        if (app_windows[i].open) {
            if (app_windows[i].frame.x + app_windows[i].frame.w > (int)fb.width - 4) {
                app_windows[i].frame.x = (int)fb.width - app_windows[i].frame.w - 4;
                if (app_windows[i].frame.x < 4) app_windows[i].frame.x = 4;
            }
            if (app_windows[i].frame.y + app_windows[i].frame.h > (int)fb.height - 72) {
                app_windows[i].frame.y = (int)fb.height - app_windows[i].frame.h - 72;
                if (app_windows[i].frame.y < 32) app_windows[i].frame.y = 32;
            }
            sync_app_window(&app_windows[i]);
        }
    }
    wallpaper_cache_dirty = 1;
    dock_reset_positions();
    widgets_reset_positions();
    needs_redraw = 1;
}

void gui_set_resolution(u32 width, u32 height) {
    if (width == fb.width && height == fb.height) return;
    int rc = fb_set_resolution(width, height);
    if (rc == 0) {
        desktop_wallpaper_dirty = 1;
        gui_update_screen_size();
    }
}

void gui_init(void) {
    user_system_init();
    int k;
    ide_init_tabs();
    extern void ide_new_file(void); ide_new_file();
    // default code
    const char *def = "class init:\n  function Main:\n    add button \"Run Me\" at (center, center)\n  end\nend";
    int idx=0; while(def[idx]) { ide_tabs[0].text[idx]=def[idx]; idx++; }
    ide_tabs[0].text[idx]=0; ide_tabs[0].length=idx;

    int i = 0;
    serial_write("[gui] enter gui_init\n");
    serial_write("[gui] before explorer_init\n");
    explorer_init();
    serial_write("[gui] after explorer_init\n");
    terminal_init();
    dock_init();
    theme_apply();
    apply_theme();
    spotlight_init();
    keybindings_init();
    net_init();
    top_brand_menu = (rect_t){8, desktop_bar.h, 168, 156, window_bg};
    top_updates_item = (rect_t){14, desktop_bar.h + 6, 156, 22, window_bg};
    top_system_info_item = (rect_t){14, desktop_bar.h + 30, 156, 22, window_bg};
    top_logout_item = (rect_t){14, desktop_bar.h + 54, 156, 22, window_bg};
    top_sleep_item = (rect_t){14, desktop_bar.h + 78, 156, 22, window_bg};
    top_reboot_item = (rect_t){14, desktop_bar.h + 102, 156, 22, window_bg};
    top_shutdown_item = (rect_t){14, desktop_bar.h + 126, 156, 22, window_bg};
    top_app_menu_rect = (rect_t){40, desktop_bar.h, 172, 82, window_bg};
    top_file_menu_rect = (rect_t){120, desktop_bar.h, 150, 82, window_bg};
    top_edit_menu_rect = (rect_t){168, desktop_bar.h, 170, 82, window_bg};
    top_view_menu_rect = (rect_t){216, desktop_bar.h, 190, 5 * 24 + 12, window_bg};
    top_app_action_1 = (rect_t){46, desktop_bar.h + 6, 160, 22, window_bg};
    top_app_action_2 = (rect_t){46, desktop_bar.h + 30, 160, 22, window_bg};
    top_app_action_3 = (rect_t){46, desktop_bar.h + 54, 160, 22, window_bg};
    widgets_init();
    gui_update_screen_size();
    dock_terminal = (rect_t){-400, -400, 0, 0, 0};
    dock_files = (rect_t){-400, -400, 0, 0, 0};
    open_app(12);
    open_app(APP_BROWSER);
    dock_display = (rect_t){-400, -400, 0, 0, 0};
    dock_explorer = (rect_t){-400, -400, 0, 0, 0};
    dock_settings = (rect_t){-400, -400, 0, 0, 0};
    serial_write("[gui] before app_windows loop\n");
    for (i = 0; i < 32; i++) {
        serial_write("[gui] app_window iteration\n");
        if (i < 12) {
            app_windows[i].kind = i + 1;
            app_windows[i].instance_id = 1;
        } else {
            app_windows[i].kind = 0;
            app_windows[i].instance_id = 0;
        }
        app_windows[i].open = 0;
        app_windows[i].minimized = 0;
        app_windows[i].maximized = 0;
        app_windows[i].tab_count = 1;
        app_windows[i].active_tab = 0;
        app_windows[i].tabs[0].used = 1;
        app_windows[i].tabs[0].title[0] = 0;
        app_windows[i].tabs[0].path[0] = 0;
        if (i == APP_TEXT_EDIT - 1) {
            app_windows[i].frame = (rect_t){70, 50, 680, 480, 0xE9EEF4};
        } else {
            app_windows[i].frame = (rect_t){70 + (i % 12) * 18, 70 + (i % 12) * 14, 420, 280, 0xE9EEF4};
        }
        app_windows[i].title_color = 0x5D738E + ((i % 12) * 0x060606);
        sync_app_window(&app_windows[i]);
    }
    serial_write("[gui] after app_windows loop\n");
    active_app_index = -1;
    sync_desktop_icons_from_fs();
    for(k=0; k<32; k++) z_order[k] = k;
    desktop_context_open = 0;
    prev_left = 0;
    prev_right_button = 0;
    dragging = 0;
    window_dragging = 0;
    window_resizing = 0;
    window_drag_index = -1;
    window_resize_index = -1;
    needs_redraw = 1;
    sound_init();
    serial_write("[gui] exit gui_init\n");
}

void gui_pump_events(void) {
    static int in_pump = 0;
    if (in_pump) return;
    in_pump = 1;

    ps2_poll();
    hda_tick();

    int mx = ps2_mouse_x();
    int my = ps2_mouse_y();
    int mb = (ps2_mouse_left() ? 1 : 0) | (ps2_mouse_right() ? 2 : 0) | (ps2_mouse_middle() ? 4 : 0);

    static u32 last_pump_tick = 0;
    u32 now = get_system_ticks();

    if (mx != last_mouse_x || my != last_mouse_y || mb != last_mouse_buttons || (now - last_pump_tick) >= 2) {
        last_pump_tick = now;
        if (gui_needs_redraw()) {
            gui_draw();
        }
    }

    in_pump = 0;
}

void gui_draw(void) { 
    static int boot_sound_played = 0;
    if (!boot_sound_played) {
        boot_sound_played = 1;
        hda_play_audio("/audio/start_up.wav");
    }

    if (gui_mode == GUI_MODE_LOGIN) {
        int cur_btn = (ps2_mouse_left() ? 1 : 0) | (ps2_mouse_right() ? 2 : 0) | (ps2_mouse_middle() ? 4 : 0);
        int cur_x = ps2_mouse_x();
        int cur_y = ps2_mouse_y();
        int mouse_moved = (cur_x != last_mouse_x || cur_y != last_mouse_y);
        int buttons_changed = (cur_btn != last_mouse_buttons);

        update_drag();
        if (gui_mode != GUI_MODE_LOGIN) {
            needs_redraw = 1;
            return;
        }

        /* Fast path: isolated cursor motion outside the login card */
        if (cursor_backup_valid && mouse_moved && !buttons_changed &&
            !login_state.show_recovery && !login_state.show_terminal_modal &&
            !point_in_rect(cur_x, cur_y, login_card_rect) &&
            !point_in_rect(last_mouse_x, last_mouse_y, login_card_rect)) {

            gui_cursor_restore();
            gui_cursor_save(cur_x, cur_y, 48, 48);
            draw_cursor();
            fb_present();

            last_mouse_x = cur_x;
            last_mouse_y = cur_y;
            last_mouse_buttons = cur_btn;
            needs_redraw = 0;
            return;
        }

        if (!needs_redraw) return;
        fb_begin_frame();
        login_draw();
        gui_cursor_save(cur_x, cur_y, 48, 48);
        draw_cursor();
        fb_present();
        last_mouse_x = cur_x;
        last_mouse_y = cur_y;
        last_mouse_buttons = cur_btn;
        needs_redraw = 0;
        return;
    }
    int i = 0; char clock_buf[9]; char fps_buf[16];
    int mouse_x = ps2_mouse_x();
    int mouse_y = ps2_mouse_y();
    app_window_t *active = active_app_window();
    update_top_menu_state();
    if (!needs_redraw) return;
    update_rtc_clock();
    if (fps_last_second < 0) fps_last_second = clock_seconds;
    fps_counter++;
    if (clock_seconds != fps_last_second) {
        fps_value = fps_counter;
        fps_counter = 0;
        fps_last_second = clock_seconds;
        weather_tick();
        extern void compositor_tick_1hz(void);
        compositor_tick_1hz();
    }
     fb_begin_frame();
    gui_tooltip_hide();
    gui_capture_event(&gui_last_event);
    if (alt_tab_preview_open && alt_tab_preview_anim < 6) {
        alt_tab_preview_anim++;
        needs_redraw = 1;
    }
    cursor_blink_ticks++;
    if (cursor_blink_ticks >= 18) {
        cursor_blink_ticks = 0;
        cursor_blink_visible = !cursor_blink_visible;
        app_window_t *term_win = app_slot(APP_TERMINAL);
        int term_active = (term_win && term_win->open && !term_win->minimized && term_win == active);
        if (text_input_focus || term_active) {
            needs_redraw = 1;
        }
    }
    if (spotlight_open) {
        spotlight_media_tick();
        if (spotlight_media_playing) needs_redraw = 1;
    }
    if (hda_is_playing()) needs_redraw = 1;
    update_keyboard_input(gui_last_event.key);
    handle_mouse_wheel();
    update_drag();

    /* Fast path: bounded single-window redraw when only window contents changed */
    if (gui_window_dirty_only && active && active->kind == APP_BROWSER && active->open && !active->minimized &&
        !window_dragging && !spotlight_open && !brand_menu_open && !app_menu_open && !file_menu_open &&
        !edit_menu_open && !view_menu_open && !clock_menu_open && !wifi_menu_open && !volume_menu_open &&
        !weather_popover_open && !fps_dropdown_open && !notification_center_open) {
        draw_app_window(active);
        gui_cursor_save(ps2_mouse_x(), ps2_mouse_y(), 48, 48);
        draw_cursor();
        fb_present();
        last_mouse_x = ps2_mouse_x();
        last_mouse_y = ps2_mouse_y();
        last_mouse_buttons = (ps2_mouse_left() ? 1 : 0) | (ps2_mouse_right() ? 2 : 0) | (ps2_mouse_middle() ? 4 : 0);
        needs_redraw = 0;
        gui_window_dirty_only = 0;
        return;
    }

    int cur_btn = (ps2_mouse_left() ? 1 : 0) | (ps2_mouse_right() ? 2 : 0) | (ps2_mouse_middle() ? 4 : 0);
    int mouse_moved = (mouse_x != last_mouse_x || mouse_y != last_mouse_y);
    int buttons_changed = (cur_btn != last_mouse_buttons);

    /* Fast path: isolated cursor motion when only cursor coordinates changed */
    if (cursor_backup_valid && mouse_moved && !buttons_changed &&
        !window_dragging && !desktop_dragging && !explorer_dragging &&
        !alt_tab_preview_open && !spotlight_open && !brand_menu_open &&
        !app_menu_open && !file_menu_open && !edit_menu_open && !view_menu_open &&
        !clock_menu_open && !wifi_menu_open && !volume_menu_open &&
        !weather_popover_open && !fps_dropdown_open && !notification_center_open &&
        !desktop_context_open && !taskbar_context_open && !dock_context_open &&
        !file_dialog_is_open() && !gui_window_dirty_only &&
        mouse_y > (desktop_bar.h + 2) && last_mouse_y > (desktop_bar.h + 2) &&
        mouse_y < ((int)fb.height - 75) && last_mouse_y < ((int)fb.height - 75)) {

        gui_cursor_restore();
        gui_cursor_save(mouse_x, mouse_y, 48, 48);
        draw_cursor();
        fb_present();

        last_mouse_x = mouse_x;
        last_mouse_y = mouse_y;
        last_mouse_buttons = cur_btn;
        needs_redraw = 0;
        return;
    }

    gui_window_dirty_only = 0;
    int wallpaper_occluded = (active && active->open && !active->minimized && active->maximized &&
                              !brand_menu_open && !app_menu_open && !file_menu_open && !edit_menu_open &&
                              !view_menu_open && !spotlight_open);
    if (!wallpaper_occluded) {
        draw_wallpaper();
    }
    u32 bar_panel = topbar_color_custom ? topbar_color : desktop_panel;
    int t_mx = ps2_mouse_x(), t_my = ps2_mouse_y();
    if (topbar_alpha < 255) {
        if (fb.backbuffer && fb.width <= MAX_SCREEN_WIDTH) {
            u32 tbh = (u32)desktop_bar.h;
            if (tbh > fb.height) tbh = fb.height;
            for (u32 ty = 0; ty < tbh; ty++) {
                u32 *src = wallpaper_cache + ty * fb.width;
                u32 *dst = (u32*)(fb.backbuffer + ty * fb.pitch);
                fast_memcpy32(dst, src, fb.width);
            }
        }
        fb_rect_alpha(0, 0, (int)fb.width, desktop_bar.h, bar_panel, topbar_alpha);
        fb_rect_alpha(0, desktop_bar.y + desktop_bar.h - 1, (int)fb.width, 1, active_theme.window_border, topbar_alpha);
    } else {
        fb_rect(0, 0, (int)fb.width, desktop_bar.h, bar_panel);
        fb_rect(0, desktop_bar.y + desktop_bar.h - 1, (int)fb.width, 1, active_theme.window_border);
    }

    #define DRAW_TOP_BTN_STATE(btn, isOpen) do { \
        int isHov = point_in_rect(t_mx, t_my, (btn)); \
        if (isOpen) { \
            if (topbar_alpha < 255) fb_rect_alpha((btn).x, (btn).y, (btn).w, (btn).h, active_theme.control_bg_hi, topbar_alpha); \
            else fb_rect((btn).x, (btn).y, (btn).w, (btn).h, active_theme.control_bg_hi); \
        } else if (isHov) { \
            if (topbar_alpha < 255) fb_rect_alpha((btn).x, (btn).y, (btn).w, (btn).h, active_theme.control_bg_hi, (topbar_alpha * 2) / 3); \
            else fb_rect((btn).x, (btn).y, (btn).w, (btn).h, active_theme.control_bg); \
        } \
    } while (0)

    DRAW_TOP_BTN_STATE(top_brand_button, brand_menu_open);
    DRAW_TOP_BTN_STATE(top_app_menu_button, app_menu_open);
    DRAW_TOP_BTN_STATE(top_file_menu_button, file_menu_open);
    DRAW_TOP_BTN_STATE(top_edit_menu_button, edit_menu_open);
    DRAW_TOP_BTN_STATE(top_view_menu_button, view_menu_open);
    DRAW_TOP_BTN_STATE(top_search_button, spotlight_open);
    DRAW_TOP_BTN_STATE(top_fps_button, fps_dropdown_open);
    DRAW_TOP_BTN_STATE(top_volume_button, volume_menu_open);
    DRAW_TOP_BTN_STATE(top_wifi_button, wifi_menu_open);
    DRAW_TOP_BTN_STATE(top_weather_button, weather_popover_open);
    DRAW_TOP_BTN_STATE(top_clock_button, clock_menu_open);
    DRAW_TOP_BTN_STATE(top_notif_button, notification_center_open);
    #undef DRAW_TOP_BTN_STATE

    if (point_in_rect(t_mx, t_my, top_brand_button)) gui_tooltip_show(top_brand_button.x + 8, top_brand_button.y + 30, "FirstOS Menu");
    else if (point_in_rect(t_mx, t_my, top_search_button) && !spotlight_open) gui_tooltip_show(top_search_button.x - 20, top_search_button.y + 30, "Spotlight Search (Ctrl+Space)");
    else if (point_in_rect(t_mx, t_my, top_fps_button) && !fps_dropdown_open) gui_tooltip_show(top_fps_button.x - 20, top_fps_button.y + 30, "System Performance & Display");
    else if (keyboard_caps_lock && point_in_rect(t_mx, t_my, top_caps_button)) gui_tooltip_show(top_caps_button.x - 20, top_caps_button.y + 30, "Caps Lock ON (Click to toggle)");
    else if (point_in_rect(t_mx, t_my, top_volume_button) && !volume_menu_open) gui_tooltip_show(top_volume_button.x - 20, top_volume_button.y + 30, "Sound & Volume");
    else if (point_in_rect(t_mx, t_my, top_wifi_button) && !wifi_menu_open) gui_tooltip_show(top_wifi_button.x - 20, top_wifi_button.y + 30, "Network Status");
    else if (point_in_rect(t_mx, t_my, top_weather_button) && !weather_popover_open) gui_tooltip_show(top_weather_button.x - 20, top_weather_button.y + 30, "Weather Forecast");
    else if (point_in_rect(t_mx, t_my, top_clock_button) && !clock_menu_open) gui_tooltip_show(top_clock_button.x - 20, top_clock_button.y + 30, "Calendar & Clock");
    else if (point_in_rect(t_mx, t_my, top_notif_button) && !notification_center_open) gui_tooltip_show(top_notif_button.x - 20, top_notif_button.y + 30, "Notification Center");
    
    // Draw the Apple/Brand logo at the top left instead of the start button
    draw_brand_icon(top_brand_button.x + 8, top_brand_button.y + 2);
    
    u32 top_text_color = active_theme.title_text;
    
    // App Menu (Bold)
    draw_label(top_app_menu_button.x + 10, 10, top_menu_app_label, top_text_color);
    draw_label(top_app_menu_button.x + 11, 10, top_menu_app_label, top_text_color);
    
    // File & Edit & View
    draw_label(top_file_menu_button.x + 8, 10, "File", top_text_color);
    app_window_t *active_top_w = active_app_window();
    const char *edit_lbl = (active_top_w && active_top_w->kind == APP_MEDIA_PLAYER) ? "Play" : "Edit";
    draw_label(top_edit_menu_button.x + 8, 10, edit_lbl, top_text_color);
    draw_label(top_view_menu_button.x + 8, 10, "View", top_text_color);
    
    time_text(clock_buf);
    fps_text(fps_buf);
    font_text_fit_center(top_fps_button.x, top_fps_button.y + 6, fps_buf, top_text_color, top_fps_button.w);
    
    if (keyboard_caps_lock) {
        int is_hov = point_in_rect(t_mx, t_my, top_caps_button);
        fb_round_rect(top_caps_button.x + 2, top_caps_button.y + 1, top_caps_button.w - 4, 18, 5, is_hov ? 0x1D4ED8 : 0x2563EB);
        font_text_fit_center(top_caps_button.x, top_caps_button.y + 6, "CAPS", 0xFFFFFF, top_caps_button.w);
    }
    
    // Draw search icon
    {
        int sx = top_search_button.x + 4;
        int sy = top_search_button.y + 4;
        font_icon(sx, sy, ICON_APPS, top_text_color);
    }
    
    // Draw wifi icon
    {
        int wx = top_wifi_button.x + 6;
        int wy = top_wifi_button.y + 4;
        font_icon(wx, wy, ICON_NETWORK, top_text_color);
    }

    // Draw volume icon (simple speaker)
    {
        int vx = top_volume_button.x + 8;
        int vy = top_volume_button.y + 5;
        fb_rect(vx, vy + 3, 4, 4, top_text_color);
        fb_rect(vx + 4, vy + 2, 2, 6, top_text_color);
        fb_rect(vx + 6, vy + 1, 2, 8, top_text_color);
        fb_rect(vx + 8, vy, 2, 10, top_text_color);
    }

    // Draw weather icon + temp
    {
        int wx = top_weather_button.x + 6;
        int wy = top_weather_button.y + 2;
        draw_weather_icon(wx, wy, current_weather.condition, 16, top_text_color);
        char w_tmp[16];
        gui_int_to_str(current_weather.temp_c, w_tmp, sizeof(w_tmp));
        gui_fl_cat(w_tmp, " C", sizeof(w_tmp));
        draw_label(wx + 22, 10, w_tmp, top_text_color);
    }
    
    // Draw clock
    draw_label(top_clock_button.x + 10, 10, clock_buf, top_text_color);

    // Draw notification bell button
    {
        int bx = top_notif_button.x + 5;
        int by = top_notif_button.y + 3;
        if (notification_dnd) {
            fb_round_rect(bx + 2, by + 2, 10, 10, 5, 0x6366F1);
            fb_round_rect(bx + 5, by + 2, 8, 8, 4, top_text_color == 0xFFFFFF ? 0x1E293B : 0xF8FAFC);
        } else {
            fb_rect(bx + 5, by + 2, 4, 2, top_text_color);
            fb_rect(bx + 4, by + 4, 6, 5, top_text_color);
            fb_rect(bx + 2, by + 9, 10, 2, top_text_color);
            fb_rect(bx + 6, by + 11, 2, 2, top_text_color);
            if (notification_count > 0) {
                fb_round_rect(top_notif_button.x + top_notif_button.w - 8, top_notif_button.y + 2, 6, 6, 3, 0xEF4444);
            }
        }
    }
    
    draw_main_window();
    draw_desktop_items();
    draw_start_button();
    widgets_draw();
    for (i = 0; i < 32; i++) {
        int z = z_order[i];
        draw_app_window(&app_windows[z]);
    }
    draw_snap_preview();
    draw_taskbar_hover_preview();
    draw_alt_tab_preview();
    draw_start_menu();
    draw_start_submenus();
    draw_brand_menu();
    draw_app_menu();
    draw_file_menu();
    draw_edit_menu();
    draw_view_menu();
    draw_wifi_menu();
    draw_volume_menu();
    draw_clock_menu();
    draw_weather_popover();
    draw_widgets_window();
    draw_fps_dropdown();
    draw_notification_center();
    draw_desktop_context_menu();
    dock_context_draw();
    draw_explorer_context_menu();
    draw_taskbar_context_menu();
    draw_explorer_paste_dialog();
    {
    }
    firstlang_draw_ui();
    
    if (fb.backbuffer && fb.width <= MAX_SCREEN_WIDTH && dock_rect.y < (int)fb.height) {
        u32 d_y0 = (u32)(dock_rect.y > 0 ? dock_rect.y : 0);
        u32 d_y1 = fb.height;
        for (u32 dy = d_y0; dy < d_y1; dy++) {
            u32 *src = wallpaper_cache + dy * fb.width;
            u32 *dst = (u32*)(fb.backbuffer + dy * fb.pitch);
            fast_memcpy32(dst, src, fb.width);
        }
    }
    dock_update_and_render(mouse_x, mouse_y, fb.width, fb.height);
    draw_launcher_overlay(mouse_x, mouse_y);
    draw_spotlight_overlay();
    draw_explorer_quick_look();
    draw_wifi_password_modal();
    draw_text_edit_unsaved_dialog();
    file_dialog_draw();
    if (widget_tooltip_visible && !wifi_menu_open && !volume_menu_open && !clock_menu_open && !weather_popover_open && !fps_dropdown_open && !notification_center_open && !brand_menu_open && !wifi_pwd_modal_open && !text_edit_unsaved_dialog_open && !file_dialog_is_open() && !spotlight_open) {
        draw_tooltip(widget_tooltip_x, widget_tooltip_y, widget_tooltip_text);
    }
    draw_drag_preview(mouse_x, mouse_y);
    gui_cursor_save(mouse_x, mouse_y, 48, 48);
    draw_cursor();
     fb_present();
    last_mouse_x = ps2_mouse_x();
    last_mouse_y = ps2_mouse_y();
    last_mouse_buttons = (ps2_mouse_left() ? 1 : 0) | (ps2_mouse_right() ? 2 : 0) | (ps2_mouse_middle() ? 4 : 0);
    needs_redraw = 0;
}

void desktop_open_context_menu(int mx, int my, int context_type) {
    desktop_context_is_explorer = context_type;
    extern void layout_desktop_context_menu(int, int);
    layout_desktop_context_menu(mx, my);
    desktop_context_open = 1;
}
