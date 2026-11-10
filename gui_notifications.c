#include "gui_notifications.h"
#include "gui_widgets.h"
#include "framebuffer.h"
#include "font.h"
#include "ps2.h"

extern int needs_redraw;

static gui_notification_t gui_notifications[MAX_NOTIFICATIONS];
int notification_count = 0;
int notification_center_open = 0;
int notification_dnd = 0;
static int notif_initialized = 0;
rect_t top_notif_button;
rect_t notif_center_rect;

static void notif_cpy(char *dst, const char *src, int max) {
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

static void notif_cat(char *dst, const char *src, int max) {
    int i = 0;
    while (dst[i] && i < max - 1) i++;
    int j = 0;
    while (src[j] && i < max - 1) { dst[i] = src[j]; i++; j++; }
    dst[i] = 0;
}

static void notif_int_to_str(int n, char *buf, int max) {
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

static void init_default_notifications(void) {
    if (notif_initialized) return;
    notif_initialized = 1;
    notification_count = 4;

    notif_cpy(gui_notifications[0].app, "System", sizeof(gui_notifications[0].app));
    notif_cpy(gui_notifications[0].title, "FirstOS 2.1 Ready", sizeof(gui_notifications[0].title));
    notif_cpy(gui_notifications[0].message, "Welcome to FirstOS workstation.", sizeof(gui_notifications[0].message));
    notif_cpy(gui_notifications[0].time_ago, "Just now", sizeof(gui_notifications[0].time_ago));
    gui_notifications[0].active = 1;
    gui_notifications[0].accent = 0x2563EB;

    notif_cpy(gui_notifications[1].app, "Network", sizeof(gui_notifications[1].app));
    notif_cpy(gui_notifications[1].title, "Wi-Fi Connected", sizeof(gui_notifications[1].title));
    notif_cpy(gui_notifications[1].message, "IP 10.0.2.15 on virtio-net adapter.", sizeof(gui_notifications[1].message));
    notif_cpy(gui_notifications[1].time_ago, "2m ago", sizeof(gui_notifications[1].time_ago));
    gui_notifications[1].active = 1;
    gui_notifications[1].accent = 0x10B981;

    notif_cpy(gui_notifications[2].app, "Display", sizeof(gui_notifications[2].app));
    notif_cpy(gui_notifications[2].title, "Appearance & Shadows", sizeof(gui_notifications[2].title));
    notif_cpy(gui_notifications[2].message, "Modern rounded GUI and drop shadows active.", sizeof(gui_notifications[2].message));
    notif_cpy(gui_notifications[2].time_ago, "5m ago", sizeof(gui_notifications[2].time_ago));
    gui_notifications[2].active = 1;
    gui_notifications[2].accent = 0x8B5CF6;

    notif_cpy(gui_notifications[3].app, "Storage", sizeof(gui_notifications[3].app));
    notif_cpy(gui_notifications[3].title, "FirstOS HD", sizeof(gui_notifications[3].title));
    notif_cpy(gui_notifications[3].message, "Mounted root filesystem with 1.0 GB capacity.", sizeof(gui_notifications[3].message));
    notif_cpy(gui_notifications[3].time_ago, "10m ago", sizeof(gui_notifications[3].time_ago));
    gui_notifications[3].active = 1;
    gui_notifications[3].accent = 0xF59E0B;
}

void draw_notification_center(void) {
    if (!notification_center_open) return;
    init_default_notifications();

    int nx = notif_center_rect.x;
    int ny = notif_center_rect.y;
    int nw = notif_center_rect.w;
    int nh = notif_center_rect.h;

    /* Translucent black shadow & rounded card */
    fb_draw_window_shadow(nx, ny, nw, nh, 12, 1);
    fb_round_rect(nx, ny, nw, nh, 12, 0xF8FAFC);
    fb_round_rect_border(nx, ny, nw, nh, 12, 0xC3CFDD);

    /* Header Title */
    font_text(nx + 16, ny + 12, "NOTIFICATION CENTER", 0x0F172A, 1);

    /* Do Not Disturb Card (height 38) */
    int dnd_y = ny + 32;
    fb_round_rect(nx + 12, dnd_y, nw - 24, 38, 8, 0xFFFFFF);
    fb_round_rect_border(nx + 12, dnd_y, nw - 24, 38, 8, 0xE2E8F0);

    /* Moon icon / DND label */
    fb_round_rect(nx + 22, dnd_y + 11, 14, 14, 7, 0x6366F1);
    fb_round_rect(nx + 26, dnd_y + 11, 11, 11, 5, 0xFFFFFF);
    font_text(nx + 44, dnd_y + 12, "Do Not Disturb", 0x1E293B, 1);

    /* DND Switch: pill shape (w=40, h=20) */
    rect_t dnd_sw = {nx + nw - 56, dnd_y + 9, 40, 20, 0};
    fb_round_rect(dnd_sw.x, dnd_sw.y, dnd_sw.w, dnd_sw.h, 10, notification_dnd ? 0x6366F1 : 0xCBD5E1);
    int thumb_x = notification_dnd ? (dnd_sw.x + 22) : (dnd_sw.x + 2);
    fb_round_rect(thumb_x, dnd_sw.y + 2, 16, 16, 8, 0xFFFFFF);

    /* Separator */
    fb_rect(nx + 12, ny + 78, nw - 24, 1, 0xE2E8F0);

    /* Actions row: NOTIFICATIONS (count) and [Delete All] button */
    char notif_hdr[32];
    notif_cpy(notif_hdr, "NOTIFICATIONS (", sizeof(notif_hdr));
    char cnt_buf[8];
    notif_int_to_str(notification_count, cnt_buf, sizeof(cnt_buf));
    notif_cat(notif_hdr, cnt_buf, sizeof(notif_hdr));
    notif_cat(notif_hdr, ")", sizeof(notif_hdr));
    font_text(nx + 16, ny + 88, notif_hdr, 0x64748B, 1);

    if (notification_count > 0) {
        rect_t btn_del_all = {nx + nw - 92, ny + 84, 80, 20, 0};
        int hov_del = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), btn_del_all);
        fb_round_rect(btn_del_all.x, btn_del_all.y, btn_del_all.w, btn_del_all.h, 4, hov_del ? 0xDC2626 : 0xEF4444);
        font_text_fit_center(btn_del_all.x, btn_del_all.y + 4, "Delete All", 0xFFFFFF, btn_del_all.w);
    }

    /* Notification items list */
    int card_y = ny + 110;
    if (notification_count == 0) {
        fb_round_rect(nx + nw / 2 - 18, card_y + 40, 36, 36, 18, 0xE2E8F0);
        font_icon(nx + nw / 2 - 9, card_y + 49, ICON_SETTINGS, 0x94A3B8);
        font_text_fit_center(nx, card_y + 88, "No New Notifications", 0x64748B, nw);
        font_text_fit_center(nx, card_y + 106, "You are all caught up!", 0x94A3B8, nw);
    } else {
        for (int k = 0; k < MAX_NOTIFICATIONS; k++) {
            if (!gui_notifications[k].active) continue;
            rect_t card = {nx + 12, card_y, nw - 24, 60, 0};
            fb_round_rect(card.x, card.y, card.w, card.h, 8, 0xFFFFFF);
            fb_round_rect_border(card.x, card.y, card.w, card.h, 8, 0xE2E8F0);

            /* Left colored accent strip */
            fb_round_rect(card.x + 2, card.y + 6, 3, card.h - 12, 1, gui_notifications[k].accent);

            /* App badge + timestamp */
            font_text(card.x + 12, card.y + 8, gui_notifications[k].app, gui_notifications[k].accent, 1);
            font_text(card.x + 80, card.y + 8, gui_notifications[k].time_ago, 0x94A3B8, 1);

            /* Dismiss [x] button at top-right */
            rect_t btn_dism = {card.x + card.w - 22, card.y + 6, 16, 16, 0};
            int hov_dism = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), btn_dism);
            if (hov_dism) {
                fb_round_rect(btn_dism.x, btn_dism.y, btn_dism.w, btn_dism.h, 4, 0xFEE2E2);
            }
            font_text(btn_dism.x + 4, btn_dism.y + 2, "x", hov_dism ? 0xDC2626 : 0x94A3B8, 1);

            /* Title & message */
            font_text(card.x + 12, card.y + 24, gui_notifications[k].title, 0x0F172A, 1);
            font_text(card.x + 12, card.y + 40, gui_notifications[k].message, 0x475569, 1);

            card_y += 66;
            if (card_y + 60 > ny + nh) break;
        }
    }
}

int notification_center_handle_click(int mx, int my) {
    if (!notification_center_open) return 0;
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
            return 1;
        }
        if (notification_count > 0) {
            rect_t btn_del_all = {nx + nw - 92, ny + 84, 80, 20, 0};
            if (point_in_rect(mx, my, btn_del_all)) {
                for (int k = 0; k < MAX_NOTIFICATIONS; k++) {
                    gui_notifications[k].active = 0;
                }
                notification_count = 0;
                needs_redraw = 1;
                return 1;
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
                return 1;
            }
            card_y += 66;
            if (card_y + 60 > ny + nh) break;
        }
        needs_redraw = 1;
        return 1;
    } else if (!point_in_rect(mx, my, top_notif_button)) {
        notification_center_open = 0;
        needs_redraw = 1;
        return 1;
    }
    return 0;
}
