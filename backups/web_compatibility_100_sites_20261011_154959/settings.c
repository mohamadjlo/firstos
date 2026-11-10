#include "settings.h"
#include "serial.h"
#include "hw_discovery.h"
#include "gui_internal.h"
#include "framebuffer.h"
#include "font.h"
#include "ps2.h"
#include "dock.h"
#include "widgets.h"
#include "keybindings.h"
#include "fs.h"
#include "theme.h"
#include "net.h"
#include "ports.h"
#include "firewall.h"
#include "gui.h"
#include "user.h"
#include "file_dialog.h"

// Globals
static int current_settings_tab = 0;
static int settings_net_subtab = 0; // 0 = Wi-Fi, 1 = Firewall & Security, 2 = Active Ports
int titlebar_position = 0;
int titlebar_autohide = 0;
int appearance_theme_index = 0;
int appearance_accent_index = 0;
u32 appearance_custom_accent = 0x3D78C0;
int settings_night_mode = 0;
int settings_taskbar_autohide = 0;
int settings_clock_24h = 0;
int settings_date_dmy = 1;
int settings_net_focus = 0;
int settings_net_cursor = 0;
int settings_login_bg_mode = 0;
int settings_account_focus = 0;

static char acct_username[32] = {0};
static char acct_display_name[32] = {0};
static char acct_cur_pass[32] = {0};
static char acct_new_pass[32] = {0};
static char acct_confirm_pass[32] = {0};
static int acct_username_cursor = 0;
static int acct_display_cursor = 0;
static int acct_cur_pass_cursor = 0;
static int acct_new_pass_cursor = 0;
static int acct_confirm_pass_cursor = 0;
static int acct_last_user_idx = -1;
static char acct_status_msg[64] = {0};
static u32 acct_status_color = 0x10B981;
static int acct_show_logout_modal = 0;

static int acct_str_len(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static int acct_str_eq(const char *a, const char *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static void acct_sync_fields(void) {
    int cur_idx = user_get_active_index();
    if (cur_idx != acct_last_user_idx) {
        const user_profile_t *u = user_get_active();
        if (u) {
            int i = 0;
            while (u->username[i] && i < 31) { acct_username[i] = u->username[i]; i++; }
            acct_username[i] = 0;
            i = 0;
            while (u->display_name[i] && i < 31) { acct_display_name[i] = u->display_name[i]; i++; }
            acct_display_name[i] = 0;
        } else {
            acct_username[0] = 0;
            acct_display_name[0] = 0;
        }
        acct_cur_pass[0] = 0;
        acct_new_pass[0] = 0;
        acct_confirm_pass[0] = 0;
        acct_username_cursor = 0;
        acct_display_cursor = 0;
        acct_cur_pass_cursor = 0;
        acct_new_pass_cursor = 0;
        acct_confirm_pass_cursor = 0;
        settings_account_focus = 0;
        acct_status_msg[0] = 0;
        acct_last_user_idx = cur_idx;
    }
}

static void net_str_cpy(char *d, const char *s, int max) {
    int i = 0;
    if (!d || max <= 0) return;
    if (s) {
        while (i < max - 1 && s[i]) { d[i] = s[i]; i++; }
    }
    d[i] = 0;
}
static void net_str_cat(char *d, const char *s, int max) {
    int i = 0;
    while (d[i] && i < max - 1) i++;
    int j = 0;
    while (s && s[j] && i < max - 1) { d[i++] = s[j++]; }
    d[i] = 0;
}
static void net_str_itos(int num, char *str, int max) {
    if (num == 0) { if (max > 1) { str[0] = '0'; str[1] = 0; } return; }
    char tmp[32]; int i = 0, j = 0;
    while (num > 0 && i < 31) { tmp[i++] = (num % 10) + '0'; num /= 10; }
    while (i > 0 && j < max - 1) str[j++] = tmp[--i];
    str[j] = 0;
}

static void settings_layout(app_window_t *app, int *x, int *y, int *w, int *h, int *sidebar_w, int *content_x, int *content_w) {
    *x = app->frame.x + 2;
    *y = app->frame.y + 24;
    *w = app->frame.w - 4;
    *h = app->frame.h - 26;
    *sidebar_w = 170;
    *content_x = *x + *sidebar_w;
    *content_w = *w - *sidebar_w;
}

static void draw_sidebar(int x, int y, int h, int sidebar_w) {
    fb_rect(x, y, sidebar_w, h, 0xEEF2F6);
    fb_rect(x + sidebar_w - 1, y, 1, h, 0xD0D7E0);
    draw_label(x + 16, y + 14, "SETTINGS", 0x1E293B);

    const char *tabs[10] = {
        "Appearance", "Account", "Dock & Desktop", "Desktop Widgets",
        "Mouse & Trackpad", "Keyboard", "Display", "Audio",
        "Connectivity", "About FirstOS"
    };
    for (int i = 0; i < 10; i++) {
        int tab_y = y + 40 + i * 32;
        int sel = (i == current_settings_tab);
        if (sel) {
            fb_rect(x + 8, tab_y - 4, sidebar_w - 16, 26, 0xCAD8EB);
            outline(x + 8, tab_y - 4, sidebar_w - 16, 26, 0x8FAFCF);
        }
        draw_label(x + 20, tab_y + 1, tabs[i], sel ? 0x1E3A5F : 0x334155);
    }
}

/* Helper to render pill buttons */
static void draw_pill(int x, int y, int w, int h, const char *label, int active) {
    u32 bg = active ? 0x3D78C0 : 0xFFFFFF;
    u32 text_col = active ? 0xFFFFFF : 0x223344;
    int rad = h < 20 ? 4 : (h < 26 ? 6 : 8);
    fb_round_rect(x, y, w, h, rad, bg);
    fb_round_rect_border(x, y, w, h, rad, active ? 0x265899 : 0xCCD2DA);
    font_text_fit_center(x, y + (h - 8) / 2 + 1, label, text_col, w);
}

/* Helper to render color swatches */
static void draw_swatch(int x, int y, int w, int h, u32 col, int selected) {
    int rad = 4;
    fb_round_rect(x, y, w, h, rad, col);
    fb_round_rect_border(x, y, w, h, rad, selected ? 0xFFFFFF : 0x778899);
    if (selected) {
        fb_round_rect_border(x - 2, y - 2, w + 4, h + 4, rad + 2, 0x3D78C0);
    }
}

/* -------------------------------------------------------------
 * TAB 0: APPEARANCE
 * ------------------------------------------------------------- */
static void draw_appearance_pane(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    int cur_y = cy + 14;

    draw_label(cx + 20, cur_y, "Appearance & Personalization", 0x1E293B);
    cur_y += 30;

    // 1. Title Bar
    draw_label(cx + 20, cur_y + 4, "Title Bar Position:", 0x475569);
    draw_pill(cx + 170, cur_y, 70, 22, "Top", titlebar_position == 0);
    draw_pill(cx + 248, cur_y, 70, 22, "Bottom", titlebar_position == 1);
    cur_y += 30;

    draw_label(cx + 20, cur_y + 4, "Auto-Hide Title Bar:", 0x475569);
    draw_pill(cx + 170, cur_y, 70, 22, "OFF", titlebar_autohide == 0);
    draw_pill(cx + 248, cur_y, 90, 22, "ON (Hover)", titlebar_autohide == 1);
    cur_y += 30;

    draw_label(cx + 20, cur_y + 4, "Title Bar Color:", 0x475569);
    static const u32 t_colors[5] = {0x315A8A, 0x20242C, 0x1B4D3E, 0x4A2560, 0x782028};
    for (int i = 0; i < 5; i++) {
        int sel = (titlebar_color_custom && titlebar_custom_color == t_colors[i]);
        draw_swatch(cx + 170 + i * 36, cur_y, 26, 20, t_colors[i], sel);
    }
    cur_y += 30;

    draw_label(cx + 20, cur_y + 4, "Title Bar Opacity:", 0x475569);
    draw_pill(cx + 170, cur_y, 60, 22, "100%", titlebar_alpha >= 250);
    draw_pill(cx + 238, cur_y, 60, 22, "85%", titlebar_alpha >= 200 && titlebar_alpha < 250);
    draw_pill(cx + 306, cur_y, 60, 22, "65%", titlebar_alpha < 200);
    cur_y += 36;

    // 2. Top Menu Bar
    draw_label(cx + 20, cur_y + 4, "Top Bar Color:", 0x475569);
    static const u32 top_colors[4] = {0x182432, 0x0D1117, 0x0E2A47, 0x3D1418};
    for (int i = 0; i < 4; i++) {
        int sel = (topbar_color_custom && topbar_color == top_colors[i]);
        draw_swatch(cx + 170 + i * 36, cur_y, 26, 20, top_colors[i], sel);
    }
    cur_y += 30;

    draw_label(cx + 20, cur_y + 4, "Top Bar Opacity:", 0x475569);
    draw_pill(cx + 170, cur_y, 60, 22, "100%", topbar_alpha >= 250);
    draw_pill(cx + 238, cur_y, 60, 22, "85%", topbar_alpha >= 200 && topbar_alpha < 250);
    draw_pill(cx + 306, cur_y, 60, 22, "65%", topbar_alpha < 200);
    cur_y += 36;

    // 3. Wallpaper
    draw_label(cx + 20, cur_y + 4, "Wallpaper Mode:", 0x475569);
    draw_pill(cx + 170, cur_y, 75, 22, "Theme", wallpaper_source == 0);
    draw_pill(cx + 253, cur_y, 90, 22, "Solid Color", wallpaper_source == 2);
    draw_pill(cx + 351, cur_y, 85, 22, "Live Video", wallpaper_source == 3);
    cur_y += 30;

    if (wallpaper_source == 2) {
        draw_label(cx + 20, cur_y + 4, "Solid Palette:", 0x475569);
        static const u32 wall_colors[5] = {0x102033, 0x231736, 0x163627, 0x1A365D, 0x1F2937};
        for (int i = 0; i < 5; i++) {
            int sel = (wallpaper_solid_color == wall_colors[i]);
            draw_swatch(cx + 170 + i * 36, cur_y, 26, 20, wall_colors[i], sel);
        }
        cur_y += 30;
    }

    // Login Screen Background
    draw_label(cx + 20, cur_y + 4, "Login Screen BG:", 0x475569);
    draw_pill(cx + 170, cur_y, 75, 22, "Wallpaper", settings_login_bg_mode == 0);
    draw_pill(cx + 253, cur_y, 60, 22, "Aurora", settings_login_bg_mode == 1);
    draw_pill(cx + 321, cur_y, 55, 22, "Navy", settings_login_bg_mode == 2);
    draw_pill(cx + 384, cur_y, 55, 22, "Dark", settings_login_bg_mode == 3);
    cur_y += 30;

    // 4. Cursor
    draw_label(cx + 20, cur_y + 4, "Cursor Style:", 0x475569);
    draw_pill(cx + 130, cur_y, 50, 22, "Mac", mouse_cursor_style == 0);
    draw_pill(cx + 188, cur_y, 65, 22, "Modern", mouse_cursor_style == 1);
    draw_pill(cx + 261, cur_y, 75, 22, "Crosshair", mouse_cursor_style == 2);
    draw_pill(cx + 344, cur_y, 45, 22, "Dot", mouse_cursor_style == 3);
    draw_pill(cx + 397, cur_y, 70, 22, "Retro 3D", mouse_cursor_style == 4);
    cur_y += 30;

    draw_label(cx + 20, cur_y + 4, "Cursor Color:", 0x475569);
    static const u32 cur_colors[5] = {0xFFFFFF, 0x00E5FF, 0xFFD600, 0xFF4081, 0x00E676};
    for (int i = 0; i < 5; i++) {
        int sel = (mouse_cursor_color == cur_colors[i]);
        draw_swatch(cx + 170 + i * 36, cur_y, 26, 20, cur_colors[i], sel);
    }
}

static void draw_text_box(int x, int y, int w, int h, const char *text, const char *placeholder, int focused, int cursor_pos, int is_password) {
    int rad = h < 22 ? 4 : 6;
    fb_round_rect(x, y, w, h, rad, 0xFFFFFF);
    fb_round_rect_border(x, y, w, h, rad, focused ? 0x2563EB : 0xCBD5E1);
    if (text && text[0]) {
        if (is_password) {
            char bullets[34];
            int len = 0;
            while (text[len] && len < 32) { bullets[len] = '*'; len++; }
            bullets[len] = 0;
            font_text(x + 8, y + 6, bullets, 0x1E293B, 1);
        } else {
            font_text(x + 8, y + 6, text, 0x1E293B, 1);
        }
    } else if (placeholder && !focused) {
        font_text(x + 8, y + 6, placeholder, 0x94A3B8, 1);
    }
    if (focused) {
        int cx = x + 8 + cursor_pos * 8;
        if (cx > x + w - 4) cx = x + w - 4;
        fb_rect(cx, y + 4, 1, h - 8, 0x2563EB);
    }
}

/* -------------------------------------------------------------
 * TAB 1: ACCOUNT & SECURITY
 * ------------------------------------------------------------- */
static void draw_account_pane(int cx, int cy, int cw, int ch) {
    (void)ch;
    acct_sync_fields();
    const user_profile_t *u = user_get_active();
    if (!u) return;

    int cur_y = cy + 12;
    draw_label(cx + 20, cur_y, "Account & Security", 0x1E293B);
    cur_y += 24;

    int card_w = cw - 40;
    if (card_w > 490) card_w = 490;

    /* 1. Profile Overview Card */
    fb_rect(cx + 20, cur_y, card_w, 72, 0xF8FAFC);
    outline(cx + 20, cur_y, card_w, 72, 0xCBD5E1);

    extern void gui_draw_avatar(int, int, int, const user_profile_t*);
    gui_draw_avatar(cx + 32, cur_y + 6, 60, u);

    draw_label(cx + 104, cur_y + 14, u->display_name[0] ? u->display_name : u->username, 0x0F172A);
    draw_label(cx + 104, cur_y + 32, u->is_root ? "Administrator (Root)" : "Standard User", 0x64748B);

    draw_pill(cx + card_w - 145, cur_y + 22, 135, 26, "Change Picture...", 0);
    cur_y += 82;

    /* 2. Account Information Card */
    draw_label(cx + 20, cur_y, "Account Information", 0x334155);
    cur_y += 18;

    fb_rect(cx + 20, cur_y, card_w, 78, 0xF8FAFC);
    outline(cx + 20, cur_y, card_w, 78, 0xCBD5E1);

    // Username row
    draw_label(cx + 32, cur_y + 12, "Username:", 0x475569);
    draw_text_box(cx + 140, cur_y + 8, 175, 24, acct_username, "username", settings_account_focus == 1, acct_username_cursor, 0);

    // Display Name row
    draw_label(cx + 32, cur_y + 42, "Display Name:", 0x475569);
    draw_text_box(cx + 140, cur_y + 38, 175, 24, acct_display_name, "Display Name", settings_account_focus == 2, acct_display_cursor, 0);

    // Save Name Button
    draw_pill(cx + 328, cur_y + 38, 96, 24, "Save Name", 1);
    cur_y += 88;

    /* 3. Password Card */
    draw_label(cx + 20, cur_y, "Change Password", 0x334155);
    cur_y += 18;

    fb_rect(cx + 20, cur_y, card_w, 134, 0xF8FAFC);
    outline(cx + 20, cur_y, card_w, 134, 0xCBD5E1);

    draw_label(cx + 32, cur_y + 12, "Current Password:", 0x475569);
    draw_text_box(cx + 170, cur_y + 8, 170, 24, acct_cur_pass, "Current password", settings_account_focus == 3, acct_cur_pass_cursor, 1);

    draw_label(cx + 32, cur_y + 42, "New Password:", 0x475569);
    draw_text_box(cx + 170, cur_y + 38, 170, 24, acct_new_pass, "New password", settings_account_focus == 4, acct_new_pass_cursor, 1);

    draw_label(cx + 32, cur_y + 72, "Confirm Password:", 0x475569);
    draw_text_box(cx + 170, cur_y + 68, 170, 24, acct_confirm_pass, "Confirm new password", settings_account_focus == 5, acct_confirm_pass_cursor, 1);

    draw_pill(cx + 170, cur_y + 100, 140, 24, "Update Password", 1);

    if (acct_status_msg[0]) {
        draw_label(cx + 322, cur_y + 104, acct_status_msg, acct_status_color);
    }
    cur_y += 144;

    /* 4. Logout Confirmation Modal Overlay (if active) */
    if (acct_show_logout_modal) {
        fb_rect_alpha(cx, cy, cw, ch, 0x000000, 140);
        int mw = 360, mh = 140;
        int mx = cx + (cw - mw) / 2;
        int my = cy + (ch - mh) / 2 - 10;
        fb_round_rect(mx, my, mw, mh, 10, 0xFFFFFF);
        fb_round_rect_border(mx, my, mw, mh, 10, 0x94A3B8);

        draw_label(mx + 20, my + 18, "Password Changed Successfully", 0x0F172A);
        draw_label(mx + 20, my + 44, "Log out now to confirm your new credentials?", 0x475569);

        // Buttons
        draw_pill(mx + 30, my + 84, 130, 28, "Stay Logged In", 0);
        draw_pill(mx + mw - 160, my + 84, 130, 28, "Log Out Now", 1);
    }
}

/* -------------------------------------------------------------
 * TAB 2: DOCK & DESKTOP
 * ------------------------------------------------------------- */
static void draw_dock_pane(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    int cur_y = cy + 14;

    draw_label(cx + 20, cur_y, "Dock & Shell Customization", 0x1E293B);
    cur_y += 32;

    // Dock Color
    draw_label(cx + 20, cur_y + 4, "Dock Color:", 0x475569);
    static const u32 d_colors[4] = {0x1A2D46, 0x1E293B, 0x0F172A, 0x2B3544};
    for (int i = 0; i < 4; i++) {
        int sel = (dock_bg_color == d_colors[i]);
        draw_swatch(cx + 170 + i * 36, cur_y, 26, 20, d_colors[i], sel);
    }
    cur_y += 32;

    // Dock Opacity
    draw_label(cx + 20, cur_y + 4, "Dock Opacity:", 0x475569);
    draw_pill(cx + 170, cur_y, 60, 22, "100%", dock_bg_alpha >= 250);
    draw_pill(cx + 238, cur_y, 60, 22, "75%", dock_bg_alpha >= 180 && dock_bg_alpha < 250);
    draw_pill(cx + 306, cur_y, 60, 22, "50%", dock_bg_alpha < 180);
    cur_y += 32;

    // Base Size
    draw_label(cx + 20, cur_y + 4, "Dock Icon Size:", 0x475569);
    draw_pill(cx + 170, cur_y, 90, 22, "Small (36px)", dock_base_size <= 40);
    draw_pill(cx + 268, cur_y, 95, 22, "Normal (48px)", dock_base_size > 40 && dock_base_size <= 54);
    draw_pill(cx + 371, cur_y, 90, 22, "Large (60px)", dock_base_size > 54);
    cur_y += 32;

    // Magnification
    draw_label(cx + 20, cur_y + 4, "Magnification:", 0x475569);
    draw_pill(cx + 170, cur_y, 60, 22, "OFF", dock_magnification_enabled == 0);
    draw_pill(cx + 238, cur_y, 60, 22, "ON", dock_magnification_enabled == 1);
    cur_y += 36;

    // Animation Mode
    draw_label(cx + 20, cur_y + 4, "Dock Animation:", 0x475569);
    draw_pill(cx + 140, cur_y, 75, 22, "Magnify", dock_animation_mode == DOCK_ANIM_MAGNIFY);
    draw_pill(cx + 223, cur_y, 85, 22, "Genie Warp", dock_animation_mode == DOCK_ANIM_GENIE);
    draw_pill(cx + 316, cur_y, 95, 22, "Bounce Wave", dock_animation_mode == DOCK_ANIM_BOUNCE);
    draw_pill(cx + 419, cur_y, 80, 22, "Slide / Pop", dock_animation_mode == DOCK_ANIM_SLIDE);
}


/* -------------------------------------------------------------
 * TAB 2: DESKTOP WIDGETS
 * ------------------------------------------------------------- */
static void draw_widgets_pane(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    int cur_y = cy + 14;

    draw_label(cx + 20, cur_y, "Desktop Interactive Widgets", 0x1E293B);
    cur_y += 32;

    // Clock
    draw_label(cx + 20, cur_y + 4, "Clock Widget:", 0x475569);
    draw_pill(cx + 170, cur_y, 80, 22, widget_clock.enabled ? "Visible" : "Hidden", widget_clock.enabled);
    draw_pill(cx + 260, cur_y, 75, 22, "Digital", widget_clock_mode == 0);
    draw_pill(cx + 343, cur_y, 75, 22, "Analog", widget_clock_mode == 1);
    cur_y += 36;

    // Calendar
    draw_label(cx + 20, cur_y + 4, "Calendar Widget:", 0x475569);
    draw_pill(cx + 170, cur_y, 80, 22, widget_calendar.enabled ? "Visible" : "Hidden", widget_calendar.enabled);
    cur_y += 36;

    // Notes
    draw_label(cx + 20, cur_y + 4, "Sticky Notes Widget:", 0x475569);
    draw_pill(cx + 170, cur_y, 80, 22, widget_notes.enabled ? "Visible" : "Hidden", widget_notes.enabled);
    draw_pill(cx + 260, cur_y, 80, 22, "Clear Note", 0);
    cur_y += 40;

    // Reset layout
    draw_label(cx + 20, cur_y + 4, "Widget Arrangement:", 0x475569);
    draw_pill(cx + 170, cur_y, 220, 26, "Reset Positions to Right Edge", 0);
}

/* -------------------------------------------------------------
 * TAB 3: MOUSE & TRACKPAD
 * ------------------------------------------------------------- */
static void draw_mouse_pane(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    int cur_y = cy + 14;

    draw_label(cx + 20, cur_y, "Mouse & Trackpad Configuration", 0x1E293B);
    cur_y += 32;

    // Swap Buttons
    draw_label(cx + 20, cur_y + 4, "Primary Button:", 0x475569);
    draw_pill(cx + 170, cur_y, 140, 22, "Left (Standard)", mouse_swap_buttons == 0);
    draw_pill(cx + 318, cur_y, 140, 22, "Right (Left-Hand)", mouse_swap_buttons == 1);
    cur_y += 32;

    // Pointer Speed
    draw_label(cx + 20, cur_y + 4, "Pointer Speed:", 0x475569);
    draw_pill(cx + 170, cur_y, 75, 22, "Slow (0.6x)", mouse_sensitivity <= 7);
    draw_pill(cx + 253, cur_y, 85, 22, "Normal (1.0x)", mouse_sensitivity > 7 && mouse_sensitivity <= 12);
    draw_pill(cx + 346, cur_y, 75, 22, "Fast (1.8x)", mouse_sensitivity > 12 && mouse_sensitivity <= 20);
    draw_pill(cx + 429, cur_y, 75, 22, "Ultra (2.5x)", mouse_sensitivity > 20);
    cur_y += 32;

    // Scroll Speed
    draw_label(cx + 20, cur_y + 4, "Scroll Speed:", 0x475569);
    draw_pill(cx + 170, cur_y, 45, 22, "1x", mouse_scroll_multiplier == 1);
    draw_pill(cx + 223, cur_y, 45, 22, "2x", mouse_scroll_multiplier == 2);
    draw_pill(cx + 276, cur_y, 45, 22, "3x", mouse_scroll_multiplier == 3);
    draw_pill(cx + 329, cur_y, 45, 22, "5x", mouse_scroll_multiplier == 5);
    cur_y += 36;

    // Context Cursors
    draw_label(cx + 20, cur_y + 4, "Context Cursors:", 0x475569);
    draw_pill(cx + 170, cur_y, 60, 22, "OFF", mouse_context_cursors_enabled == 0);
    draw_pill(cx + 238, cur_y, 60, 22, "ON", mouse_context_cursors_enabled == 1);
    cur_y += 32;

    draw_label(cx + 20, cur_y + 4, "Test Cursor State:", 0x475569);
    draw_pill(cx + 170, cur_y, 55, 22, "Idle", mouse_cursor_state == CURSOR_STATE_IDLE);
    draw_pill(cx + 233, cur_y, 65, 22, "Loading", mouse_cursor_state == CURSOR_STATE_LOADING);
    draw_pill(cx + 306, cur_y, 65, 22, "Opening", mouse_cursor_state == CURSOR_STATE_OPENING);
    draw_pill(cx + 379, cur_y, 70, 22, "Dragging", mouse_cursor_state == CURSOR_STATE_DRAGGING);
}

/* -------------------------------------------------------------
 * TAB 4: KEYBOARD SHORTCUTS
 * ------------------------------------------------------------- */
static void draw_keyboard_pane(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    int cur_y = cy + 14;

    draw_label(cx + 20, cur_y, "Keyboard Shortcuts & Hotkeys", 0x1E293B);
    cur_y += 28;

    for (int i = 0; i < num_keybindings && i < 8; i++) {
        keybinding_t *kb = &keybindings[i];
        int ry = cur_y + i * 28;
        fb_rect(cx + 20, ry, cw - 40, 24, i % 2 == 0 ? 0xF8FAFC : 0xFFFFFF);
        outline(cx + 20, ry, cw - 40, 24, 0xE2E8F0);

        draw_label(cx + 30, ry + 5, kb->name, 0x1E293B);

        // Shortcut pill
        char pill_txt[32];
        int pi = 0;
        if (kb->mod_ctrl) { pill_txt[pi++] = 'C'; pill_txt[pi++] = 't'; pill_txt[pi++] = 'r'; pill_txt[pi++] = 'l'; pill_txt[pi++] = '+'; }
        if (kb->mod_alt)  { pill_txt[pi++] = 'A'; pill_txt[pi++] = 'l'; pill_txt[pi++] = 't'; pill_txt[pi++] = '+'; }
        if (kb->mod_shift){ pill_txt[pi++] = 'S'; pill_txt[pi++] = 'h'; pill_txt[pi++] = 'i'; pill_txt[pi++] = 'f'; pill_txt[pi++] = 't'; pill_txt[pi++] = '+'; }
        if (kb->key == ' ') { pill_txt[pi++] = 'S'; pill_txt[pi++] = 'p'; pill_txt[pi++] = 'a'; pill_txt[pi++] = 'c'; pill_txt[pi++] = 'e'; }
        else if (kb->key == KEY_SNAP_LEFT) { pill_txt[pi++] = 'L'; pill_txt[pi++] = 'e'; pill_txt[pi++] = 'f'; pill_txt[pi++] = 't'; }
        else if (kb->key == KEY_SNAP_RIGHT){ pill_txt[pi++] = 'R'; pill_txt[pi++] = 'i'; pill_txt[pi++] = 'g'; pill_txt[pi++] = 'h'; pill_txt[pi++] = 't'; }
        else if (kb->key >= 'a' && kb->key <= 'z') { pill_txt[pi++] = (char)(kb->key - ('a' - 'A')); }
        else { pill_txt[pi++] = (char)kb->key; }
        pill_txt[pi] = 0;

        draw_pill(cx + cw - 180, ry + 2, 70, 20, pill_txt, 0);
        draw_pill(cx + cw - 100, ry + 2, 60, 20, "Rebind", 0);
    }
    cur_y += 8 * 28 + 10;
    draw_pill(cx + 20, cur_y, 140, 26, "+ Add Shortcut", 1);
}

/* -------------------------------------------------------------
 * TAB 5: DISPLAY
 * ------------------------------------------------------------- */
static void draw_display_pane(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    int cur_y = cy + 14;

    draw_label(cx + 20, cur_y, "Display & Graphics Adapter", 0x1E293B);
    cur_y += 32;

    display_info_t disp = hw_get_display_info();
    draw_label(cx + 20, cur_y, "Graphics Device:", 0x475569);
    draw_label(cx + 160, cur_y, disp.adapter_name[0] ? disp.adapter_name : "VirtIO-GPU Standard PCI", 0x1E293B);
    cur_y += 32;

    draw_label(cx + 20, cur_y, "Current Resolution:", 0x475569);
    draw_pill(cx + 160, cur_y - 4, 94, 24, "1024x768", fb.width == 1024 && fb.height == 768);
    draw_pill(cx + 260, cur_y - 4, 94, 24, "1280x720", fb.width == 1280 && fb.height == 720);
    draw_pill(cx + 360, cur_y - 4, 94, 24, "1280x800", fb.width == 1280 && fb.height == 800);
    cur_y += 32;

    draw_pill(cx + 160, cur_y - 4, 94, 24, "1440x900", fb.width == 1440 && fb.height == 900);
    draw_pill(cx + 260, cur_y - 4, 94, 24, "1600x900", fb.width == 1600 && fb.height == 900);
    draw_pill(cx + 360, cur_y - 4, 94, 24, "1920x1080", fb.width == 1920 && fb.height == 1080);
    cur_y += 36;

    draw_label(cx + 20, cur_y, "Color Depth:", 0x475569);
    draw_label(cx + 160, cur_y, "32-bit True Color (ARGB8888)", 0x1E293B);
}

/* -------------------------------------------------------------
 * TAB 6: AUDIO
 * ------------------------------------------------------------- */
static void draw_audio_pane(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    int cur_y = cy + 14;

    draw_label(cx + 20, cur_y, "Sound & Audio Devices", 0x1E293B);
    cur_y += 32;

    draw_label(cx + 20, cur_y + 4, "Master Volume:", 0x475569);
    draw_pill(cx + 150, cur_y, 30, 22, "-", 0);
    // Rounded Volume Slider
    int bar_x = cx + 190;
    int bar_y = cur_y + 7;
    int bar_w = 120;
    int bar_h = 8;
    fb_round_rect(bar_x, bar_y, bar_w, bar_h, 4, 0xE2E8F0);
    int fill_w = (bar_w * system_volume) / 100;
    if (fill_w > 0) {
        fb_round_rect(bar_x, bar_y, fill_w, bar_h, 4, 0x3D78C0);
    }
    int knob_x = bar_x + fill_w;
    fb_round_rect(knob_x - 7, cur_y + 4, 14, 14, 7, 0xFFFFFF);
    fb_round_rect_border(knob_x - 7, cur_y + 4, 14, 14, 7, 0x8FAFCF);
    draw_pill(cx + 320, cur_y, 30, 22, "+", 0);
    cur_y += 36;

    draw_label(cx + 20, cur_y, "Audio Device:", 0x475569);
    draw_label(cx + 150, cur_y, "Intel High Definition Audio (HDA)", 0x1E293B);
}

/* -------------------------------------------------------------
 * TAB 7: ABOUT
 * ------------------------------------------------------------- */
static void draw_about_pane(int cx, int cy, int cw, int ch) {
    (void)cw; (void)ch;
    int cur_y = cy + 14;

    draw_label(cx + 20, cur_y, "About FirstOS", 0x1E293B);
    cur_y += 32;

    draw_label(cx + 20, cur_y, "OS Version:", 0x475569);
    draw_label(cx + 160, cur_y, "FirstOS 2.1 Modern Desktop", 0x1E293B);
    cur_y += 28;

    draw_label(cx + 20, cur_y, "Architecture:", 0x475569);
    draw_label(cx + 160, cur_y, "i686 (32-bit x86 Monolithic)", 0x1E293B);
    cur_y += 28;

    draw_label(cx + 20, cur_y, "Installed RAM:", 0x475569);
    draw_label(cx + 160, cur_y, "4.0 GB High Memory", 0x1E293B);
    cur_y += 28;

    draw_label(cx + 20, cur_y, "Compositor:", 0x475569);
    draw_label(cx + 160, cur_y, "32-bit ARGB Software Alpha Compositor", 0x1E293B);
}

/* -------------------------------------------------------------
 * TAB 8: CONNECTIVITY & FIREWALL
 * ------------------------------------------------------------- */
static void draw_connectivity_pane(int cx, int cy, int cw, int ch) {
    (void)ch;
    int cur_y = cy + 20;

    draw_label(cx + 20, cur_y, "Network & Security", 0x1E293B);
    cur_y += 28;

    // Subtab navigation segmented pills
    draw_pill(cx + 20, cur_y, 120, 24, "Wi-Fi Networks", settings_net_subtab == 0);
    draw_pill(cx + 148, cur_y, 140, 24, "Firewall & Security", settings_net_subtab == 1);
    draw_pill(cx + 296, cur_y, 110, 24, "Active Ports", settings_net_subtab == 2);
    cur_y += 34;

    int card_w = cw - 40;
    if (card_w > 560) card_w = 560;

    /* SUBTAB 0: WI-FI NETWORKS */
    if (settings_net_subtab == 0) {
        int is_enabled = net_is_enabled();
        int is_connected = net_is_connected();
        const net_state_t *st = net_get_state();

        // Master Toggle
        draw_label(cx + 20, cur_y + 4, "Wi-Fi Networking:", 0x475569);
        draw_pill(cx + 170, cur_y, 70, 22, "OFF", !is_enabled);
        draw_pill(cx + 248, cur_y, 70, 22, "ON", is_enabled);
        cur_y += 32;

        if (!is_enabled) {
            fb_rect(cx + 20, cur_y, card_w, 70, 0xF1F5F9);
            outline(cx + 20, cur_y, card_w, 70, 0xCBD5E1);
            draw_label(cx + 36, cur_y + 16, "Wi-Fi hardware is turned off", 0x334155);
            draw_label(cx + 36, cur_y + 38, "Click 'ON' above to enable Wi-Fi and scan for wireless networks.", 0x64748B);
            return;
        }

        int card_h = 135;
        fb_rect(cx + 20, cur_y, card_w, card_h, 0xFFFFFF);
        outline(cx + 20, cur_y, card_w, card_h, 0xCBD5E1);

        if (is_connected) {
            fb_rect(cx + 36, cur_y + 16, 8, 8, 0x10B981);
            draw_label(cx + 48, cur_y + 14, "Connected to:", 0x64748B);
            draw_label(cx + 140, cur_y + 14, st->ssid, 0x1E293B);

            char sig_str[32];
            char num[16];
            int sig_val = 97;
            int cur_ap_total = net_get_ap_count();
            if (st->current_network_idx >= 0 && st->current_network_idx < cur_ap_total) {
                const wifi_ap_t *cap = net_get_ap(st->current_network_idx);
                if (cap) sig_val = cap->signal_strength;
            }
            net_str_cpy(sig_str, "Signal: ", sizeof(sig_str));
            net_str_itos(sig_val, num, sizeof(num));
            net_str_cat(sig_str, num, sizeof(sig_str));
            net_str_cat(sig_str, "%", sizeof(sig_str));
            draw_label(cx + card_w - 180, cur_y + 14, sig_str, 0x059669);

            draw_pill(cx + card_w - 85, cur_y + 10, 75, 20, "Disconnect", 0);

            char info_str[100];
            net_str_cpy(info_str, "IP: ", sizeof(info_str));
            net_str_cat(info_str, st->ip_addr, sizeof(info_str));
            net_str_cat(info_str, "   MAC: ", sizeof(info_str));
            net_str_cat(info_str, st->mac_addr, sizeof(info_str));
            net_str_cat(info_str, "   Mask: ", sizeof(info_str));
            net_str_cat(info_str, st->subnet_mask, sizeof(info_str));
            draw_label(cx + 36, cur_y + 36, info_str, 0x475569);

            draw_label(cx + 36, cur_y + 64, "DNS Server:", 0x475569);
            rect_t dns_box = {cx + 125, cur_y + 60, 110, 22, 0xFFFFFF};
            fb_rect(dns_box.x, dns_box.y, dns_box.w, dns_box.h, dns_box.color);
            outline(dns_box.x, dns_box.y, dns_box.w, dns_box.h, settings_net_focus == 1 ? 0x0078D7 : 0xCBD5E1);
            font_text(dns_box.x + 8, dns_box.y + 6, st->dns_addr, 0x1E293B, 1);
            if (settings_net_focus == 1) {
                int dlen = 0; while (st->dns_addr[dlen]) dlen++;
                fb_rect(dns_box.x + 8 + dlen * 6, dns_box.y + 4, 2, 14, 0x0078D7);
            }
            draw_pill(cx + 245, cur_y + 60, 55, 22, "8.8.8.8", 0);
            draw_pill(cx + 306, cur_y + 60, 55, 22, "1.1.1.1", 0);
            draw_pill(cx + 367, cur_y + 60, 55, 22, "9.9.9.9", 0);

            draw_label(cx + 36, cur_y + 96, "IP-Proxy:", 0x475569);
            rect_t proxy_box = {cx + 125, cur_y + 92, 140, 22, 0xFFFFFF};
            fb_rect(proxy_box.x, proxy_box.y, proxy_box.w, proxy_box.h, proxy_box.color);
            outline(proxy_box.x, proxy_box.y, proxy_box.w, proxy_box.h, settings_net_focus == 2 ? 0x0078D7 : 0xCBD5E1);
            font_text(proxy_box.x + 8, proxy_box.y + 6, st->proxy_addr, 0x1E293B, 1);
            if (settings_net_focus == 2) {
                int plen = 0; while (st->proxy_addr[plen]) plen++;
                fb_rect(proxy_box.x + 8 + plen * 6, proxy_box.y + 4, 2, 14, 0x0078D7);
            }
            draw_pill(cx + 275, cur_y + 92, 50, 22, "None", 0);
            draw_pill(cx + 332, cur_y + 92, 95, 22, "192.168.1.1:8080", 0);
        } else {
            fb_rect(cx + 36, cur_y + 24, 8, 8, 0x94A3B8);
            draw_label(cx + 50, cur_y + 22, "Not Connected", 0x64748B);
            draw_label(cx + 50, cur_y + 48, "Select a Wi-Fi network below to connect.", 0x94A3B8);
        }

        cur_y += card_h + 16;

        draw_label(cx + 20, cur_y, "Other Available Networks", 0x1E293B);
        cur_y += 24;

        int ap_count = net_get_ap_count();
        for (int i = 0; i < ap_count; i++) {
            const wifi_ap_t *ap = net_get_ap(i);
            if (!ap) continue;

            int row_y = cur_y + i * 32;
            fb_rect(cx + 20, row_y, card_w, 28, i % 2 == 0 ? 0xF8FAFC : 0xFFFFFF);
            outline(cx + 20, row_y, card_w, 28, 0xE2E8F0);

            font_icon(cx + 30, row_y + 6, ICON_NETWORK, ap->connected ? 0x0078D7 : 0x64748B);
            draw_label(cx + 48, row_y + 7, ap->ssid, 0x1E293B);

            if (ap->encrypted) {
                draw_label(cx + 190, row_y + 7, "[WPA2 Lock]", 0xD97706);
            } else {
                draw_label(cx + 190, row_y + 7, "[Open]", 0x059669);
            }

            char sig_buf[16];
            net_str_cpy(sig_buf, "Signal: ", sizeof(sig_buf));
            char num[8];
            net_str_itos(ap->signal_strength, num, sizeof(num));
            net_str_cat(sig_buf, num, sizeof(sig_buf));
            net_str_cat(sig_buf, "%", sizeof(sig_buf));
            draw_label(cx + 280, row_y + 7, sig_buf, 0x64748B);

            if (ap->connected) {
                draw_pill(cx + card_w - 85, row_y + 4, 80, 20, "Disconnect", 1);
            } else {
                draw_pill(cx + card_w - 85, row_y + 4, 80, 20, "Connect", 0);
            }
        }
        return;
    }

    /* SUBTAB 1: FIREWALL & SECURITY */
    if (settings_net_subtab == 1) {
        const fw_state_t *fw = firewall_get_state();
        int fw_on = fw ? fw->enabled : 0;
        fw_profile_t prof = fw ? fw->profile : FW_PROFILE_BALANCED;
        int stealth_on = fw ? fw->stealth_mode : 0;

        // Master Control Card
        int top_card_h = 96;
        fb_rect(cx + 20, cur_y, card_w, top_card_h, 0xFFFFFF);
        outline(cx + 20, cur_y, card_w, top_card_h, 0xCBD5E1);

        // Status Indicator
        fb_rect(cx + 34, cur_y + 14, 10, 10, fw_on ? 0x10B981 : 0xEF4444);
        draw_label(cx + 52, cur_y + 12, fw_on ? "Firewall Protection is ACTIVE" : "Firewall Protection is DISABLED", fw_on ? 0x059669 : 0xDC2626);

        // ON / OFF Pills
        draw_pill(cx + card_w - 140, cur_y + 8, 60, 22, "OFF", !fw_on);
        draw_pill(cx + card_w - 75, cur_y + 8, 60, 22, "ON", fw_on);

        // Profile Selector
        draw_label(cx + 34, cur_y + 40, "Profile:", 0x475569);
        draw_pill(cx + 92, cur_y + 36, 75, 22, "Balanced", prof == FW_PROFILE_BALANCED);
        draw_pill(cx + 172, cur_y + 36, 60, 22, "Strict", prof == FW_PROFILE_STRICT);
        draw_pill(cx + 237, cur_y + 36, 55, 22, "Open", prof == FW_PROFILE_OPEN);

        // Stealth Ping Toggle
        draw_label(cx + 310, cur_y + 40, "Stealth Mode:", 0x475569);
        draw_pill(cx + 410, cur_y + 36, 45, 22, "OFF", !stealth_on);
        draw_pill(cx + 460, cur_y + 36, 45, 22, "ON", stealth_on);

        // Live Stats Counters
        char stats_buf[100];
        char num[16];
        net_str_cpy(stats_buf, "Accepted: ", sizeof(stats_buf));
        net_str_itos(fw ? fw->packets_accepted : 0, num, sizeof(num));
        net_str_cat(stats_buf, num, sizeof(stats_buf));
        net_str_cat(stats_buf, "   Dropped: ", sizeof(stats_buf));
        net_str_itos(fw ? fw->packets_dropped : 0, num, sizeof(num));
        net_str_cat(stats_buf, num, sizeof(stats_buf));
        net_str_cat(stats_buf, "   Rejected: ", sizeof(stats_buf));
        net_str_itos(fw ? fw->packets_rejected : 0, num, sizeof(num));
        net_str_cat(stats_buf, num, sizeof(stats_buf));
        draw_label(cx + 34, cur_y + 70, stats_buf, 0x64748B);

        cur_y += top_card_h + 12;

        // Rules Section Title & Quick Action Pills
        draw_label(cx + 20, cur_y, "Active Rules & Policies", 0x1E293B);
        draw_pill(cx + 215, cur_y - 2, 95, 20, "+ Allow 8080", 0);
        draw_pill(cx + 315, cur_y - 2, 85, 20, "+ Deny 22", 0);
        draw_pill(cx + 405, cur_y - 2, 95, 20, "Reset Rules", 0);
        cur_y += 24;

        // Rules Table Header
        fb_rect(cx + 20, cur_y, card_w, 22, 0xF1F5F9);
        outline(cx + 20, cur_y, card_w, 22, 0xCBD5E1);
        draw_label(cx + 26, cur_y + 5, "#", 0x475569);
        draw_label(cx + 46, cur_y + 5, "Rule Name", 0x475569);
        draw_label(cx + 155, cur_y + 5, "Proto", 0x475569);
        draw_label(cx + 210, cur_y + 5, "Port", 0x475569);
        draw_label(cx + 270, cur_y + 5, "Dir", 0x475569);
        draw_label(cx + 320, cur_y + 5, "Action", 0x475569);
        draw_label(cx + 390, cur_y + 5, "Hits", 0x475569);
        draw_label(cx + 440, cur_y + 5, "Delete", 0x475569);
        cur_y += 22;

        int rcount = fw ? fw->rule_count : 0;
        if (rcount == 0) {
            fb_rect(cx + 20, cur_y, card_w, 36, 0xFFFFFF);
            outline(cx + 20, cur_y, card_w, 36, 0xE2E8F0);
            draw_label(cx + 36, cur_y + 11, "No custom rules defined (Default policy active).", 0x94A3B8);
        } else {
            int max_display = rcount > 6 ? 6 : rcount;
            for (int i = 0; i < max_display; i++) {
                const fw_rule_t *r = &fw->rules[i];
                int ry = cur_y + i * 26;
                fb_rect(cx + 20, ry, card_w, 26, (i % 2 == 0) ? 0xFFFFFF : 0xF8FAFC);
                outline(cx + 20, ry, card_w, 26, 0xE2E8F0);

                char ibuf[8];
                net_str_itos(i + 1, ibuf, sizeof(ibuf));
                draw_label(cx + 26, ry + 7, ibuf, 0x64748B);

                draw_label(cx + 46, ry + 7, r->name, 0x1E293B);

                const char *pstr = r->proto == FW_PROTO_TCP ? "TCP" : (r->proto == FW_PROTO_UDP ? "UDP" : (r->proto == FW_PROTO_ICMP ? "ICMP" : "ANY"));
                draw_label(cx + 155, ry + 7, pstr, 0x334155);

                char pbuf[16];
                if (r->port_start == 0 && r->port_end == 0) {
                    net_str_cpy(pbuf, "*", sizeof(pbuf));
                } else {
                    net_str_itos(r->port_start, pbuf, sizeof(pbuf));
                    if (r->port_end > r->port_start) {
                        net_str_cat(pbuf, "-", sizeof(pbuf));
                        char endbuf[8];
                        net_str_itos(r->port_end, endbuf, sizeof(endbuf));
                        net_str_cat(pbuf, endbuf, sizeof(pbuf));
                    }
                }
                draw_label(cx + 210, ry + 7, pbuf, 0x334155);

                const char *dstr = r->dir == FW_DIR_IN ? "IN" : (r->dir == FW_DIR_OUT ? "OUT" : "BOTH");
                draw_label(cx + 270, ry + 7, dstr, 0x64748B);

                const char *act_str = r->action == FW_ACTION_ACCEPT ? "ACCEPT" : (r->action == FW_ACTION_DROP ? "DROP" : "REJECT");
                u32 act_col = r->action == FW_ACTION_ACCEPT ? 0x059669 : (r->action == FW_ACTION_DROP ? 0xDC2626 : 0xD97706);
                draw_label(cx + 320, ry + 7, act_str, act_col);

                char hit_str[12];
                net_str_itos(r->match_count, hit_str, sizeof(hit_str));
                draw_label(cx + 390, ry + 7, hit_str, 0x64748B);

                // Delete button
                draw_pill(cx + 440, ry + 3, 38, 20, "Del", 0);
            }
        }
        return;
    }

    /* SUBTAB 2: ACTIVE PORTS & SOCKETS */
    if (settings_net_subtab == 2) {
        port_entry_t port_list[16];
        int nports = ports_get_active_list(port_list, 16);

        // Header info card
        int top_card_h = 44;
        fb_rect(cx + 20, cur_y, card_w, top_card_h, 0xFFFFFF);
        outline(cx + 20, cur_y, card_w, top_card_h, 0xCBD5E1);

        char summary[64];
        char num[16];
        net_str_cpy(summary, "Active Open Ports & Bindings: ", sizeof(summary));
        net_str_itos(nports, num, sizeof(num));
        net_str_cat(summary, num, sizeof(summary));
        net_str_cat(summary, " socket(s)", sizeof(summary));
        draw_label(cx + 34, cur_y + 14, summary, 0x1E293B);

        // Refresh button
        draw_pill(cx + card_w - 85, cur_y + 10, 75, 24, "Refresh", 0);

        cur_y += top_card_h + 14;

        // Table Header
        fb_rect(cx + 20, cur_y, card_w, 24, 0xF1F5F9);
        outline(cx + 20, cur_y, card_w, 24, 0xCBD5E1);
        draw_label(cx + 28, cur_y + 6, "Proto", 0x475569);
        draw_label(cx + 80, cur_y + 6, "Local Address", 0x475569);
        draw_label(cx + 195, cur_y + 6, "Foreign Address", 0x475569);
        draw_label(cx + 325, cur_y + 6, "State", 0x475569);
        draw_label(cx + 430, cur_y + 6, "Service", 0x475569);
        cur_y += 24;

        if (nports == 0) {
            fb_rect(cx + 20, cur_y, card_w, 40, 0xFFFFFF);
            outline(cx + 20, cur_y, card_w, 40, 0xE2E8F0);
            draw_label(cx + 36, cur_y + 14, "No active ports or listening sockets currently open.", 0x94A3B8);
        } else {
            for (int i = 0; i < nports && i < 10; i++) {
                const port_entry_t *p = &port_list[i];
                int ry = cur_y + i * 28;
                fb_rect(cx + 20, ry, card_w, 28, (i % 2 == 0) ? 0xFFFFFF : 0xF8FAFC);
                outline(cx + 20, ry, card_w, 28, 0xE2E8F0);

                // Proto badge
                if (p->proto == PORT_TYPE_TCP) {
                    draw_pill(cx + 26, ry + 4, 38, 20, "TCP", 1);
                } else {
                    fb_round_rect(cx + 26, ry + 4, 38, 20, 4, 0x8B5CF6);
                    font_text_fit_center(cx + 26, ry + 7, "UDP", 0xFFFFFF, 38);
                }

                // Local Port
                char lbuf[24];
                net_str_cpy(lbuf, "*:", sizeof(lbuf));
                char numbuf[8];
                net_str_itos(p->local_port, numbuf, sizeof(numbuf));
                net_str_cat(lbuf, numbuf, sizeof(lbuf));
                draw_label(cx + 80, ry + 7, lbuf, 0x1E293B);

                // Foreign Address
                char fbuf[32];
                if (p->remote_ip == 0 && p->remote_port == 0) {
                    net_str_cpy(fbuf, "*:*", sizeof(fbuf));
                } else {
                    u8 b1 = (p->remote_ip >> 24) & 0xFF;
                    u8 b2 = (p->remote_ip >> 16) & 0xFF;
                    u8 b3 = (p->remote_ip >> 8) & 0xFF;
                    u8 b4 = p->remote_ip & 0xFF;
                    char tb[8];
                    net_str_itos(b1, tb, sizeof(tb)); net_str_cpy(fbuf, tb, sizeof(fbuf)); net_str_cat(fbuf, ".", sizeof(fbuf));
                    net_str_itos(b2, tb, sizeof(tb)); net_str_cat(fbuf, tb, sizeof(fbuf)); net_str_cat(fbuf, ".", sizeof(fbuf));
                    net_str_itos(b3, tb, sizeof(tb)); net_str_cat(fbuf, tb, sizeof(fbuf)); net_str_cat(fbuf, ".", sizeof(fbuf));
                    net_str_itos(b4, tb, sizeof(tb)); net_str_cat(fbuf, tb, sizeof(fbuf)); net_str_cat(fbuf, ":", sizeof(fbuf));
                    net_str_itos(p->remote_port, tb, sizeof(tb)); net_str_cat(fbuf, tb, sizeof(fbuf));
                }
                draw_label(cx + 195, ry + 7, fbuf, 0x475569);

                // State
                const char *st_str = "BOUND";
                u32 st_col = 0xD97706;
                if (p->state == PORT_STATE_LISTEN) { st_str = "LISTEN"; st_col = 0x059669; }
                else if (p->state == PORT_STATE_ESTABLISHED) { st_str = "ESTABLISHED"; st_col = 0x2563EB; }
                else if (p->state == PORT_STATE_SYN_SENT) { st_str = "SYN_SENT"; st_col = 0x9333EA; }
                else if (p->state == PORT_STATE_CLOSE_WAIT) { st_str = "CLOSE_WAIT"; st_col = 0xDC2626; }
                else if (p->state == PORT_STATE_TIME_WAIT) { st_str = "TIME_WAIT"; st_col = 0x64748B; }
                draw_label(cx + 325, ry + 7, st_str, st_col);

                // Service
                draw_label(cx + 430, ry + 7, p->service[0] ? p->service : "-", 0x334155);
            }
        }
        return;
    }
}

/* -------------------------------------------------------------
 * MAIN ENTRYPOINTS
 * ------------------------------------------------------------- */
void draw_settings_contents(app_window_t *app) {
    int x, y, w, h, sidebar_w, content_x, content_w;
    settings_layout(app, &x, &y, &w, &h, &sidebar_w, &content_x, &content_w);

    fb_rect(x, y, w, h, 0xF8FAFC);
    fb_rect(content_x, y, content_w, h, 0xFFFFFF);
    draw_sidebar(x, y, h, sidebar_w);

    switch (current_settings_tab) {
        case 0: draw_appearance_pane(content_x, y, content_w, h); break;
        case 1: draw_account_pane(content_x, y, content_w, h); break;
        case 2: draw_dock_pane(content_x, y, content_w, h); break;
        case 3: draw_widgets_pane(content_x, y, content_w, h); break;
        case 4: draw_mouse_pane(content_x, y, content_w, h); break;
        case 5: draw_keyboard_pane(content_x, y, content_w, h); break;
        case 6: draw_display_pane(content_x, y, content_w, h); break;
        case 7: draw_audio_pane(content_x, y, content_w, h); break;
        case 8: draw_connectivity_pane(content_x, y, content_w, h); break;
        case 9: draw_about_pane(content_x, y, content_w, h); break;
        default: break;
    }
}

void handle_settings_click(app_window_t *app, int mx, int my) {
    int x, y, w, h, sidebar_w, content_x, content_w;
    settings_layout(app, &x, &y, &w, &h, &sidebar_w, &content_x, &content_w);

    // Sidebar Click
    if (mx >= x && mx < x + sidebar_w) {
        if (my >= y + 36 && my < y + 36 + 10 * 32) {
            current_settings_tab = (my - (y + 36)) / 32;
            if (current_settings_tab < 0) current_settings_tab = 0;
            if (current_settings_tab > 9) current_settings_tab = 9;
            settings_account_focus = 0;
            needs_redraw = 1;
            return;
        }
    }

    int cx = content_x;
    int cy = y;

    serial_write("[settings_click] tab=");
    serial_write_hex((u32)current_settings_tab);
    serial_write(" mx=");
    serial_write_hex((u32)mx);
    serial_write(" my=");
    serial_write_hex((u32)my);
    serial_write("\n");

    // TAB 0: Appearance
    if (current_settings_tab == 0) {
        int cur_y = cy + 44;
        // Title bar position
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 240) { titlebar_position = 0; needs_redraw = 1; }
            else if (mx >= cx + 248 && mx <= cx + 318) { titlebar_position = 1; needs_redraw = 1; }
        }
        cur_y += 30;
        // Title bar autohide
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 240) { titlebar_autohide = 0; needs_redraw = 1; }
            else if (mx >= cx + 248 && mx <= cx + 338) { titlebar_autohide = 1; needs_redraw = 1; }
        }
        cur_y += 30;
        // Title bar color swatches
        if (my >= cur_y && my <= cur_y + 20) {
            static const u32 t_colors[5] = {0x315A8A, 0x20242C, 0x1B4D3E, 0x4A2560, 0x782028};
            for (int i = 0; i < 5; i++) {
                if (mx >= cx + 170 + i * 36 && mx <= cx + 170 + i * 36 + 26) {
                    titlebar_custom_color = t_colors[i];
                    titlebar_color_custom = 1;
                    needs_redraw = 1;
                }
            }
        }
        cur_y += 30;
        // Title bar opacity
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 230) { titlebar_alpha = 255; needs_redraw = 1; }
            else if (mx >= cx + 238 && mx <= cx + 298) { titlebar_alpha = 215; needs_redraw = 1; }
            else if (mx >= cx + 306 && mx <= cx + 366) { titlebar_alpha = 165; needs_redraw = 1; }
        }
        cur_y += 36;
        // Top bar color
        if (my >= cur_y && my <= cur_y + 20) {
            static const u32 top_colors[4] = {0x182432, 0x0D1117, 0x0E2A47, 0x3D1418};
            for (int i = 0; i < 4; i++) {
                if (mx >= cx + 170 + i * 36 && mx <= cx + 170 + i * 36 + 26) {
                    topbar_color = top_colors[i];
                    topbar_color_custom = 1;
                    needs_redraw = 1;
                }
            }
        }
        cur_y += 30;
        // Top bar opacity
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 230) { topbar_alpha = 255; needs_redraw = 1; }
            else if (mx >= cx + 238 && mx <= cx + 298) { topbar_alpha = 215; needs_redraw = 1; }
            else if (mx >= cx + 306 && mx <= cx + 366) { topbar_alpha = 165; needs_redraw = 1; }
        }
        cur_y += 36;
        // Wallpaper mode
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 245) { wallpaper_source = 0; needs_redraw = 1; }
            else if (mx >= cx + 253 && mx <= cx + 343) { wallpaper_source = 2; needs_redraw = 1; }
            else if (mx >= cx + 351 && mx <= cx + 436) { wallpaper_source = 3; needs_redraw = 1; }
        }
        cur_y += 30;
        if (wallpaper_source == 2) {
            if (my >= cur_y && my <= cur_y + 20) {
                static const u32 wall_colors[5] = {0x102033, 0x231736, 0x163627, 0x1A365D, 0x1F2937};
                for (int i = 0; i < 5; i++) {
                    if (mx >= cx + 170 + i * 36 && mx <= cx + 170 + i * 36 + 26) {
                        wallpaper_solid_color = wall_colors[i];
                        needs_redraw = 1;
                    }
                }
            }
            cur_y += 30;
        }
        // Login Screen BG
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 245) { settings_login_bg_mode = 0; needs_redraw = 1; }
            else if (mx >= cx + 253 && mx <= cx + 313) { settings_login_bg_mode = 1; needs_redraw = 1; }
            else if (mx >= cx + 321 && mx <= cx + 376) { settings_login_bg_mode = 2; needs_redraw = 1; }
            else if (mx >= cx + 384 && mx <= cx + 439) { settings_login_bg_mode = 3; needs_redraw = 1; }
        }
        cur_y += 30;
        // Cursor style
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 130 && mx <= cx + 180) { mouse_cursor_style = 0; needs_redraw = 1; }
            else if (mx >= cx + 188 && mx <= cx + 253) { mouse_cursor_style = 1; needs_redraw = 1; }
            else if (mx >= cx + 261 && mx <= cx + 336) { mouse_cursor_style = 2; needs_redraw = 1; }
            else if (mx >= cx + 344 && mx <= cx + 389) { mouse_cursor_style = 3; needs_redraw = 1; }
            else if (mx >= cx + 397 && mx <= cx + 467) { mouse_cursor_style = 4; needs_redraw = 1; }
        }
        cur_y += 30;
        // Cursor color
        if (my >= cur_y && my <= cur_y + 20) {
            static const u32 cur_colors[5] = {0xFFFFFF, 0x00E5FF, 0xFFD600, 0xFF4081, 0x00E676};
            for (int i = 0; i < 5; i++) {
                if (mx >= cx + 170 + i * 36 && mx <= cx + 170 + i * 36 + 26) {
                    mouse_cursor_color = cur_colors[i];
                    needs_redraw = 1;
                }
            }
        }
    }

    // TAB 1: Account
    else if (current_settings_tab == 1) {
        int card_w = content_w - 40;
        if (card_w > 490) card_w = 490;

        /* If Logout Modal is open, handle its clicks exclusively */
        if (acct_show_logout_modal) {
            int mw = 360, mh = 140;
            int modal_x = cx + (content_w - mw) / 2;
            int modal_y = cy + (h - mh) / 2 - 10;

            // [Stay Logged In] button
            if (my >= modal_y + 84 && my <= modal_y + 112 && mx >= modal_x + 30 && mx <= modal_x + 160) {
                acct_show_logout_modal = 0;
                needs_redraw = 1;
                return;
            }
            // [Log Out Now] button
            if (my >= modal_y + 84 && my <= modal_y + 112 && mx >= modal_x + mw - 160 && mx <= modal_x + mw - 30) {
                acct_show_logout_modal = 0;
                extern void gui_logout(void);
                gui_logout();
                needs_redraw = 1;
                return;
            }
            return;
        }

        int cur_y = cy + 12 + 24;

        /* 1. Change Picture button */
        if (my >= cur_y + 22 && my <= cur_y + 48 && mx >= cx + card_w - 145 && mx <= cx + card_w - 10) {
            settings_account_focus = 0;
            extern void file_dialog_show(int caller, int mode, const char *initial_name, const char *initial_dir);
            file_dialog_show(APP_SETTINGS, FILE_DIALOG_OPEN, "", "/wallpapers");
            needs_redraw = 1;
            return;
        }
        cur_y += 82 + 18;

        /* 2. Profile Information */
        // Username textbox click
        if (my >= cur_y + 8 && my <= cur_y + 32 && mx >= cx + 140 && mx <= cx + 315) {
            settings_account_focus = 1;
            acct_username_cursor = acct_str_len(acct_username);
            gui_text_input_set(acct_username, sizeof(acct_username), &acct_username_cursor, &settings_account_focus);
            needs_redraw = 1;
            return;
        }
        // Display Name textbox click
        if (my >= cur_y + 38 && my <= cur_y + 62 && mx >= cx + 140 && mx <= cx + 315) {
            settings_account_focus = 2;
            acct_display_cursor = acct_str_len(acct_display_name);
            gui_text_input_set(acct_display_name, sizeof(acct_display_name), &acct_display_cursor, &settings_account_focus);
            needs_redraw = 1;
            return;
        }
        // Save Name button
        if (my >= cur_y + 38 && my <= cur_y + 62 && mx >= cx + 328 && mx <= cx + 424) {
            settings_account_focus = 0;
            if (acct_username[0] == 0) {
                net_str_cpy(acct_status_msg, "Username cannot be empty", sizeof(acct_status_msg));
                acct_status_color = 0xEF4444;
            } else {
                int active_idx = user_get_active_index();
                const char *d_name = acct_display_name[0] ? acct_display_name : acct_username;
                if (user_update_name(active_idx, acct_username, d_name)) {
                    net_str_cpy(acct_status_msg, "Name updated successfully", sizeof(acct_status_msg));
                    acct_status_color = 0x10B981;
                } else {
                    net_str_cpy(acct_status_msg, "Invalid username or already taken", sizeof(acct_status_msg));
                    acct_status_color = 0xEF4444;
                }
            }
            needs_redraw = 1;
            return;
        }
        cur_y += 88 + 18;

        /* 3. Password Card */
        // Current Password textbox click
        if (my >= cur_y + 8 && my <= cur_y + 32 && mx >= cx + 170 && mx <= cx + 340) {
            settings_account_focus = 3;
            acct_cur_pass_cursor = acct_str_len(acct_cur_pass);
            gui_text_input_set(acct_cur_pass, sizeof(acct_cur_pass), &acct_cur_pass_cursor, &settings_account_focus);
            needs_redraw = 1;
            return;
        }
        // New Password textbox click
        if (my >= cur_y + 38 && my <= cur_y + 62 && mx >= cx + 170 && mx <= cx + 340) {
            settings_account_focus = 4;
            acct_new_pass_cursor = acct_str_len(acct_new_pass);
            gui_text_input_set(acct_new_pass, sizeof(acct_new_pass), &acct_new_pass_cursor, &settings_account_focus);
            needs_redraw = 1;
            return;
        }
        // Confirm Password textbox click
        if (my >= cur_y + 68 && my <= cur_y + 92 && mx >= cx + 170 && mx <= cx + 340) {
            settings_account_focus = 5;
            acct_confirm_pass_cursor = acct_str_len(acct_confirm_pass);
            gui_text_input_set(acct_confirm_pass, sizeof(acct_confirm_pass), &acct_confirm_pass_cursor, &settings_account_focus);
            needs_redraw = 1;
            return;
        }
        // Update Password button
        if (my >= cur_y + 100 && my <= cur_y + 124 && mx >= cx + 170 && mx <= cx + 310) {
            settings_account_focus = 0;
            int active_idx = user_get_active_index();
            const user_profile_t *u = user_get_active();
            if (!user_check_password(active_idx, acct_cur_pass)) {
                net_str_cpy(acct_status_msg, "Current password incorrect", sizeof(acct_status_msg));
                acct_status_color = 0xEF4444;
            } else if (acct_new_pass[0] == 0) {
                net_str_cpy(acct_status_msg, "New password cannot be empty", sizeof(acct_status_msg));
                acct_status_color = 0xEF4444;
            } else if (!acct_str_eq(acct_new_pass, acct_confirm_pass)) {
                net_str_cpy(acct_status_msg, "Passwords do not match", sizeof(acct_status_msg));
                acct_status_color = 0xEF4444;
            } else {
                if (user_update_password(active_idx, acct_new_pass, u ? u->hint : 0)) {
                    acct_cur_pass[0] = 0; acct_cur_pass_cursor = 0;
                    acct_new_pass[0] = 0; acct_new_pass_cursor = 0;
                    acct_confirm_pass[0] = 0; acct_confirm_pass_cursor = 0;
                    acct_status_msg[0] = 0;
                    acct_show_logout_modal = 1;
                } else {
                    net_str_cpy(acct_status_msg, "Failed to update password", sizeof(acct_status_msg));
                    acct_status_color = 0xEF4444;
                }
            }
            needs_redraw = 1;
            return;
        }

        settings_account_focus = 0;
    }

    // TAB 2: Dock & Desktop
    else if (current_settings_tab == 2) {
        int cur_y = cy + 46;
        // Dock color
        if (my >= cur_y && my <= cur_y + 20) {
            static const u32 d_colors[4] = {0x1A2D46, 0x1E293B, 0x0F172A, 0x2B3544};
            for (int i = 0; i < 4; i++) {
                if (mx >= cx + 170 + i * 36 && mx <= cx + 170 + i * 36 + 26) {
                    dock_bg_color = d_colors[i];
                    needs_redraw = 1;
                }
            }
        }
        cur_y += 32;
        // Dock opacity
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 230) { dock_bg_alpha = 255; needs_redraw = 1; }
            else if (mx >= cx + 238 && mx <= cx + 298) { dock_bg_alpha = 190; needs_redraw = 1; }
            else if (mx >= cx + 306 && mx <= cx + 366) { dock_bg_alpha = 128; needs_redraw = 1; }
        }
        cur_y += 32;
        // Dock size
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 260) { dock_base_size = 36; needs_redraw = 1; }
            else if (mx >= cx + 268 && mx <= cx + 363) { dock_base_size = 48; needs_redraw = 1; }
            else if (mx >= cx + 371 && mx <= cx + 461) { dock_base_size = 60; needs_redraw = 1; }
        }
        cur_y += 32;
        // Magnification
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 230) { dock_magnification_enabled = 0; needs_redraw = 1; }
            else if (mx >= cx + 238 && mx <= cx + 298) { dock_magnification_enabled = 1; needs_redraw = 1; }
        }
        cur_y += 36;
        // Animation
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 140 && mx <= cx + 215) { dock_animation_mode = DOCK_ANIM_MAGNIFY; needs_redraw = 1; }
            else if (mx >= cx + 223 && mx <= cx + 308) { dock_animation_mode = DOCK_ANIM_GENIE; needs_redraw = 1; }
            else if (mx >= cx + 316 && mx <= cx + 411) { dock_animation_mode = DOCK_ANIM_BOUNCE; needs_redraw = 1; }
            else if (mx >= cx + 419 && mx <= cx + 499) { dock_animation_mode = DOCK_ANIM_SLIDE; needs_redraw = 1; }
        }
    }

    // TAB 3: Widgets
    else if (current_settings_tab == 3) {
        int cur_y = cy + 46;
        // Clock
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 250) { widget_clock.enabled ^= 1; needs_redraw = 1; }
            else if (mx >= cx + 260 && mx <= cx + 335) { widget_clock_mode = 0; needs_redraw = 1; }
            else if (mx >= cx + 343 && mx <= cx + 418) { widget_clock_mode = 1; needs_redraw = 1; }
        }
        cur_y += 36;
        // Calendar
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 250) { widget_calendar.enabled ^= 1; needs_redraw = 1; }
        }
        cur_y += 36;
        // Notes
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 250) { widget_notes.enabled ^= 1; needs_redraw = 1; }
            else if (mx >= cx + 260 && mx <= cx + 340) { widget_notes_text[0] = 0; widget_notes_cursor = 0; needs_redraw = 1; }
        }
        cur_y += 40;
        // Reset
        if (my >= cur_y && my <= cur_y + 26 && mx >= cx + 170 && mx <= cx + 390) {
            widgets_reset_positions();
            needs_redraw = 1;
        }
    }

    // TAB 4: Mouse
    else if (current_settings_tab == 4) {
        int cur_y = cy + 46;
        // Swap
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 310) { ps2_set_swap_buttons(0); needs_redraw = 1; }
            else if (mx >= cx + 318 && mx <= cx + 458) { ps2_set_swap_buttons(1); needs_redraw = 1; }
        }
        cur_y += 32;
        // Speed
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 245) { ps2_set_sensitivity(6); needs_redraw = 1; }
            else if (mx >= cx + 253 && mx <= cx + 338) { ps2_set_sensitivity(10); needs_redraw = 1; }
            else if (mx >= cx + 346 && mx <= cx + 421) { ps2_set_sensitivity(18); needs_redraw = 1; }
            else if (mx >= cx + 429 && mx <= cx + 504) { ps2_set_sensitivity(25); needs_redraw = 1; }
        }
        cur_y += 32;
        // Scroll
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 215) { ps2_set_scroll_multiplier(1); needs_redraw = 1; }
            else if (mx >= cx + 223 && mx <= cx + 268) { ps2_set_scroll_multiplier(2); needs_redraw = 1; }
            else if (mx >= cx + 276 && mx <= cx + 321) { ps2_set_scroll_multiplier(3); needs_redraw = 1; }
            else if (mx >= cx + 329 && mx <= cx + 374) { ps2_set_scroll_multiplier(5); needs_redraw = 1; }
        }
        cur_y += 36;
        // Context Cursors
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 230) { mouse_context_cursors_enabled = 0; needs_redraw = 1; }
            else if (mx >= cx + 238 && mx <= cx + 298) { mouse_context_cursors_enabled = 1; needs_redraw = 1; }
        }
        cur_y += 32;
        // State preview
        if (my >= cur_y && my <= cur_y + 22) {
            if (mx >= cx + 170 && mx <= cx + 225) { mouse_cursor_state = CURSOR_STATE_IDLE; needs_redraw = 1; }
            else if (mx >= cx + 233 && mx <= cx + 298) { mouse_cursor_state = CURSOR_STATE_LOADING; needs_redraw = 1; }
            else if (mx >= cx + 306 && mx <= cx + 371) { mouse_cursor_state = CURSOR_STATE_OPENING; needs_redraw = 1; }
            else if (mx >= cx + 379 && mx <= cx + 449) { mouse_cursor_state = CURSOR_STATE_DRAGGING; needs_redraw = 1; }
        }
    }

    // TAB 5: Keyboard
    else if (current_settings_tab == 5) {
        int cur_y = cy + 42;
        for (int i = 0; i < num_keybindings && i < 8; i++) {
            int ry = cur_y + i * 28;
            if (my >= ry && my <= ry + 24 && mx >= cx + content_w - 100 && mx <= cx + content_w - 40) {
                keybindings[i].mod_alt ^= 1;
                needs_redraw = 1;
                return;
            }
        }
        cur_y += 8 * 28 + 10;
        if (my >= cur_y && my <= cur_y + 26 && mx >= cx + 20 && mx <= cx + 160) {
            keybindings_add("Custom App", ACTION_CUSTOM, 1, 1, 0, 'c', "terminal");
            needs_redraw = 1;
        }
    }

    // TAB 6: Display
    else if (current_settings_tab == 6) {
        if (my >= cy + 74 && my <= cy + 98) {
            if (mx >= cx + 160 && mx <= cx + 254) { gui_set_resolution(1024, 768); resolution_id = 0; }
            else if (mx >= cx + 260 && mx <= cx + 354) { gui_set_resolution(1280, 720); resolution_id = 1; }
            else if (mx >= cx + 360 && mx <= cx + 454) { gui_set_resolution(1280, 800); resolution_id = 2; }
        } else if (my >= cy + 106 && my <= cy + 130) {
            if (mx >= cx + 160 && mx <= cx + 254) { gui_set_resolution(1440, 900); resolution_id = 3; }
            else if (mx >= cx + 260 && mx <= cx + 354) { gui_set_resolution(1600, 900); resolution_id = 4; }
            else if (mx >= cx + 360 && mx <= cx + 454) { gui_set_resolution(1920, 1080); resolution_id = 5; }
        }
    }

    // TAB 7: Audio
    else if (current_settings_tab == 7) {
        if (my >= cy + 46 && my <= cy + 68) {
            if (mx >= cx + 150 && mx <= cx + 180) {
                if (system_volume >= 10) system_volume -= 10;
                needs_redraw = 1;
            } else if (mx >= cx + 320 && mx <= cx + 350) {
                if (system_volume <= 90) system_volume += 10;
                needs_redraw = 1;
            }
        }
    }

    // TAB 8: Connectivity & Firewall
    else if (current_settings_tab == 8) {
        int cur_y = cy + 20 + 28; // cy + 48: Subtab row

        // Subtab pills click
        if (my >= cur_y - 6 && my <= cur_y + 32) {
            if (mx >= cx + 10 && mx < cx + 145) {
                settings_net_subtab = 0;
                settings_net_focus = 0;
                serial_write("[settings_subtab] switched to 0\n");
                needs_redraw = 1;
                return;
            } else if (mx >= cx + 145 && mx < cx + 295) {
                settings_net_subtab = 1;
                settings_net_focus = 0;
                serial_write("[settings_subtab] switched to 1\n");
                needs_redraw = 1;
                return;
            } else if (mx >= cx + 295 && mx <= cx + 420) {
                settings_net_subtab = 2;
                settings_net_focus = 0;
                serial_write("[settings_subtab] switched to 2\n");
                needs_redraw = 1;
                return;
            }
        }

        cur_y += 34; // cy + 82
        int card_w = content_w - 40;
        if (card_w > 560) card_w = 560;

        /* SUBTAB 0: WI-FI NETWORKS */
        if (settings_net_subtab == 0) {
            int is_enabled = net_is_enabled();
            int is_connected = net_is_connected();

            // Master toggle
            if (my >= cur_y && my <= cur_y + 22) {
                if (mx >= cx + 170 && mx <= cx + 240) {
                    net_set_enabled(0);
                    settings_net_focus = 0;
                    needs_redraw = 1;
                    return;
                } else if (mx >= cx + 248 && mx <= cx + 318) {
                    net_set_enabled(1);
                    needs_redraw = 1;
                    return;
                }
            }
            cur_y += 32;

            if (!is_enabled) return;

            if (is_connected) {
                // Disconnect button
                if (my >= cur_y + 10 && my <= cur_y + 30 && mx >= cx + card_w - 85 && mx <= cx + card_w - 10) {
                    net_disconnect();
                    settings_net_focus = 0;
                    needs_redraw = 1;
                    return;
                }

                // DNS text field click
                if (my >= cur_y + 60 && my <= cur_y + 82 && mx >= cx + 125 && mx <= cx + 235) {
                    settings_net_focus = 1;
                    net_state_t *st = net_get_state_mut();
                    settings_net_cursor = 0;
                    while (st->dns_addr[settings_net_cursor]) settings_net_cursor++;
                    gui_text_input_set(st->dns_addr, (int)sizeof(st->dns_addr), &settings_net_cursor, &settings_net_focus);
                    needs_redraw = 1;
                    return;
                }

                // DNS Presets
                if (my >= cur_y + 60 && my <= cur_y + 82) {
                    if (mx >= cx + 245 && mx <= cx + 300) { net_set_dns("8.8.8.8"); settings_net_focus = 0; needs_redraw = 1; return; }
                    if (mx >= cx + 306 && mx <= cx + 361) { net_set_dns("1.1.1.1"); settings_net_focus = 0; needs_redraw = 1; return; }
                    if (mx >= cx + 367 && mx <= cx + 422) { net_set_dns("9.9.9.9"); settings_net_focus = 0; needs_redraw = 1; return; }
                }

                // IP Proxy text field click
                if (my >= cur_y + 92 && my <= cur_y + 114 && mx >= cx + 125 && mx <= cx + 265) {
                    settings_net_focus = 2;
                    net_state_t *st = net_get_state_mut();
                    settings_net_cursor = 0;
                    while (st->proxy_addr[settings_net_cursor]) settings_net_cursor++;
                    gui_text_input_set(st->proxy_addr, (int)sizeof(st->proxy_addr), &settings_net_cursor, &settings_net_focus);
                    needs_redraw = 1;
                    return;
                }
                // Proxy Presets
                if (my >= cur_y + 92 && my <= cur_y + 114) {
                    if (mx >= cx + 275 && mx <= cx + 325) { net_set_proxy("None"); settings_net_focus = 0; needs_redraw = 1; return; }
                    if (mx >= cx + 332 && mx <= cx + 427) { net_set_proxy("192.168.1.1:8080"); settings_net_focus = 0; needs_redraw = 1; return; }
                }
            }

            settings_net_focus = 0;

            cur_y += 135 + 16 + 24;
            int ap_count = net_get_ap_count();
            for (int i = 0; i < ap_count; i++) {
                int row_y = cur_y + i * 32;
                if (my >= row_y + 4 && my <= row_y + 24 && mx >= cx + card_w - 85 && mx <= cx + card_w - 5) {
                    const wifi_ap_t *ap = net_get_ap(i);
                    if (ap) {
                        if (ap->connected) {
                            net_disconnect();
                        } else if (ap->encrypted) {
                            if (ap->expected_pass[0] != 0) {
                                int l = 0;
                                while (ap->expected_pass[l]) l++;
                                if (l >= 8 && net_connect(i, ap->expected_pass)) {
                                    /* Connected with saved password */
                                } else {
                                    extern void wifi_password_modal_open_for_ap(int);
                                    wifi_password_modal_open_for_ap(i);
                                }
                            } else {
                                extern void wifi_password_modal_open_for_ap(int);
                                wifi_password_modal_open_for_ap(i);
                            }
                        } else {
                            net_connect(i, "");
                        }
                        needs_redraw = 1;
                        return;
                    }
                }
            }
            return;
        }

        /* SUBTAB 1: FIREWALL & SECURITY */
        if (settings_net_subtab == 1) {
            // Master ON/OFF
            if (my >= cur_y + 8 && my <= cur_y + 30) {
                if (mx >= cx + card_w - 140 && mx <= cx + card_w - 80) {
                    firewall_set_enabled(0);
                    needs_redraw = 1;
                    return;
                } else if (mx >= cx + card_w - 75 && mx <= cx + card_w - 15) {
                    firewall_set_enabled(1);
                    needs_redraw = 1;
                    return;
                }
            }

            // Profiles: Balanced, Strict, Open
            if (my >= cur_y + 36 && my <= cur_y + 58) {
                if (mx >= cx + 92 && mx <= cx + 167) {
                    firewall_set_profile(FW_PROFILE_BALANCED);
                    needs_redraw = 1;
                    return;
                } else if (mx >= cx + 172 && mx <= cx + 232) {
                    firewall_set_profile(FW_PROFILE_STRICT);
                    needs_redraw = 1;
                    return;
                } else if (mx >= cx + 237 && mx <= cx + 292) {
                    firewall_set_profile(FW_PROFILE_OPEN);
                    needs_redraw = 1;
                    return;
                }
                // Stealth Mode OFF / ON
                if (mx >= cx + 410 && mx <= cx + 455) {
                    firewall_set_stealth(0);
                    needs_redraw = 1;
                    return;
                } else if (mx >= cx + 460 && mx <= cx + 505) {
                    firewall_set_stealth(1);
                    needs_redraw = 1;
                    return;
                }
            }

            // Quick rules buttons
            int qy = cur_y + 96 + 12;
            if (my >= qy - 2 && my <= qy + 20) {
                if (mx >= cx + 215 && mx <= cx + 310) {
                    // + Allow 8080
                    fw_rule_t r;
                    for (int k = 0; k < (int)sizeof(r); k++) ((u8*)&r)[k] = 0;
                    r.enabled = 1;
                    net_str_cpy(r.name, "HTTP-Alt", sizeof(r.name));
                    r.dir = FW_DIR_IN;
                    r.proto = FW_PROTO_TCP;
                    r.port_start = 8080;
                    r.port_end = 8080;
                    r.action = FW_ACTION_ACCEPT;
                    firewall_add_rule(&r);
                    needs_redraw = 1;
                    return;
                } else if (mx >= cx + 315 && mx <= cx + 400) {
                    // + Deny 22
                    fw_rule_t r;
                    for (int k = 0; k < (int)sizeof(r); k++) ((u8*)&r)[k] = 0;
                    r.enabled = 1;
                    net_str_cpy(r.name, "SSH-Block", sizeof(r.name));
                    r.dir = FW_DIR_IN;
                    r.proto = FW_PROTO_TCP;
                    r.port_start = 22;
                    r.port_end = 22;
                    r.action = FW_ACTION_DROP;
                    firewall_add_rule(&r);
                    needs_redraw = 1;
                    return;
                } else if (mx >= cx + 405 && mx <= cx + 500) {
                    // Reset Rules
                    firewall_reset_defaults();
                    needs_redraw = 1;
                    return;
                }
            }

            // Delete pills on rules
            int table_y = qy + 24 + 22;
            const fw_state_t *fw = firewall_get_state();
            int rcount = fw ? fw->rule_count : 0;
            int max_display = rcount > 6 ? 6 : rcount;
            for (int i = 0; i < max_display; i++) {
                int ry = table_y + i * 26;
                if (my >= ry + 3 && my <= ry + 23 && mx >= cx + 440 && mx <= cx + 478) {
                    firewall_delete_rule(i);
                    needs_redraw = 1;
                    return;
                }
            }
            return;
        }

        /* SUBTAB 2: ACTIVE PORTS & SOCKETS */
        if (settings_net_subtab == 2) {
            // Refresh button
            if (my >= cur_y + 10 && my <= cur_y + 34 && mx >= cx + card_w - 85 && mx <= cx + card_w - 10) {
                needs_redraw = 1;
                return;
            }
            return;
        }
    }
}

void settings_handle_avatar_selected(const char *path) {
    if (!path || !path[0]) return;
    int active_idx = user_get_active_index();
    if (user_update_avatar(active_idx, path)) {
        extern void gui_invalidate_avatar_cache(void);
        gui_invalidate_avatar_cache();
        net_str_cpy(acct_status_msg, "Picture updated", sizeof(acct_status_msg));
        acct_status_color = 0x10B981;
    } else {
        net_str_cpy(acct_status_msg, "Failed to update picture", sizeof(acct_status_msg));
        acct_status_color = 0xEF4444;
    }
    needs_redraw = 1;
}

void settings_save_to_disk(void) { }
void settings_load_from_disk(void) { }

void settings_set_tab(int tab) {
    if (tab >= 0 && tab < 10) current_settings_tab = tab;
}

void settings_set_subtab(int subtab) {
    if (subtab >= 0 && subtab < 3) settings_net_subtab = subtab;
}

