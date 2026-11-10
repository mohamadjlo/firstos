#include "gui_login.h"
#include "gui.h"
#include "gui_internal.h"
#include "framebuffer.h"
#include "font.h"
#include "ps2.h"
#include "sound.h"
#include "serial.h"
#include "user.h"
#include "terminal.h"
#include "rtc.h"
#include "fs.h"
#include "settings.h"
#include "io.h"

/* External hooks implemented in gui.c */
char scancode_to_ascii(unsigned char key);
void gui_desktop_on_logout(void);
void gui_trigger_post_boot_fs(void);

/* Global subsystem state */
gui_mode_t gui_mode = GUI_MODE_LOGIN;
login_state_t login_state = {
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
rect_t login_card_rect;

/* Internal hitboxes */
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

/* String helpers */
static void login_fl_cpy(char *dst, const char *src, int max) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (src) {
        while (src[i] && i + 1 < max) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static void login_fl_cat(char *dst, const char *src, int max) {
    int i = 0;
    int j = 0;
    if (!dst || max <= 0) return;
    while (dst[i] && i < max) i++;
    if (src) {
        while (src[j] && i + 1 < max) {
            dst[i++] = src[j++];
        }
    }
    if (i < max) dst[i] = '\0';
    else dst[max - 1] = '\0';
}

static int login_fl_eq(const char *a, const char *b) {
    int i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void t2d(int v, char *out) {
    if (!out) return;
    if (v < 0) v = 0;
    out[0] = '0' + (char)((v / 10) % 10);
    out[1] = '0' + (char)(v % 10);
    out[2] = 0;
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
        gui_trigger_post_boot_fs();
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
        gui_trigger_post_boot_fs();
        serial_write("[gui] automated unlock to desktop mode\n");
        needs_redraw = 1;
    }
}

static void login_term_submit(void) {
    if (login_fl_eq(login_state.term_pin, "1234") || user_check_password(0, login_state.term_pin)) {
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
        login_fl_cpy(login_state.term_error, "Incorrect PIN. Default admin PIN is 1234.", sizeof(login_state.term_error));
    }
    needs_redraw = 1;
}

void login_handle_key(unsigned char key) {
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

void login_handle_mouse(int mx, int my, int pressed_now, int left, int right) {
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
    gui_desktop_on_logout();
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

void login_draw(void) {
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
    t2d(t.hour, n_buf); login_fl_cat(time_str, n_buf, sizeof(time_str)); login_fl_cat(time_str, ":", sizeof(time_str));
    t2d(t.minute, n_buf); login_fl_cat(time_str, n_buf, sizeof(time_str));
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
    if (gui_keyboard_caps_lock()) {
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
