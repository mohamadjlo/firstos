#include "app_framework.h"
#include "framebuffer.h"
#include "font.h"
#include "theme.h"
#include "gui_widgets.h"

static void app_str_copy(char *dst, const char *src, int max) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* =========================================================================
 * Status Bar Implementation
 * ========================================================================= */

void app_status_bar_init(app_status_bar_t *bar) {
    if (!bar) return;
    bar->text[0] = '\0';
    bar->details[0] = '\0';
    bar->type = APP_STATUS_INFO;
    bar->dirty = 1;
}

void app_status_bar_set(app_status_bar_t *bar, app_status_type_t type, const char *text, const char *details) {
    if (!bar) return;
    bar->type = type;
    app_str_copy(bar->text, text, sizeof(bar->text));
    app_str_copy(bar->details, details, sizeof(bar->details));
    bar->dirty = 1;
}

void app_draw_status_bar(rect_t r, const app_status_bar_t *bar) {
    if (!bar) return;
    u32 bg_color = 0x222222;
    u32 text_color = 0xCCCCCC;
    u32 badge_color = 0x3B82F6;
    const char *badge = "INFO";

    switch (bar->type) {
        case APP_STATUS_SUCCESS:
            badge_color = 0x16A34A;
            badge = "OK";
            text_color = 0xE2E8F0;
            break;
        case APP_STATUS_WARNING:
            badge_color = 0xD97706;
            badge = "WARN";
            text_color = 0xFEF3C7;
            break;
        case APP_STATUS_ERROR:
            badge_color = 0xDC2626;
            badge = "FAIL";
            text_color = 0xFEE2E2;
            break;
        case APP_STATUS_INFO:
        default:
            badge_color = 0x475569;
            badge = "INFO";
            text_color = 0xCBD5E1;
            break;
    }

    fb_rect(r.x, r.y, r.w, r.h, bg_color);
    fb_rect(r.x, r.y, r.w, 1, 0x383838);

    /* Badge */
    int bw = 42;
    fb_round_rect(r.x + 6, r.y + (r.h - 16) / 2, bw, 16, 3, badge_color);
    font_text_fit_center(r.x + 6, r.y + (r.h - 16) / 2 + 4, badge, 0xFFFFFF, bw);

    /* Main text */
    if (bar->text[0]) {
        font_text_fit(r.x + 54, r.y + (r.h - 8) / 2 + 1, bar->text, text_color, r.w - 180);
    }

    /* Details on right */
    if (bar->details[0]) {
        int dw = 120;
        font_text_fit_center(r.x + r.w - dw - 8, r.y + (r.h - 8) / 2 + 1, bar->details, 0x94A3B8, dw);
    }
}

/* =========================================================================
 * Reusable Output & Diagnostics Pane Implementation
 * ========================================================================= */

void app_output_pane_init(app_output_pane_t *pane, const char *title) {
    if (!pane) return;
    pane->buffer[0] = '\0';
    pane->length = 0;
    pane->line_starts[0] = 0;
    pane->line_count = 0;
    pane->scroll_y = 0;
    pane->auto_scroll = 1;
    app_str_copy(pane->title, title ? title : "OUTPUT", sizeof(pane->title));
}

void app_output_pane_append(app_output_pane_t *pane, const char *str) {
    if (!pane || !str || !str[0]) return;
    while (*str) {
        if (pane->length >= APP_OUTPUT_MAX_BYTES - 2) {
            /* Shift buffer to discard oldest 1024 bytes */
            int drop = 1024;
            int rem = pane->length - drop;
            if (rem < 0) rem = 0;
            for (int i = 0; i < rem; i++) {
                pane->buffer[i] = pane->buffer[drop + i];
            }
            pane->length = rem;
            pane->buffer[pane->length] = '\0';
            
            /* Rebuild line index */
            pane->line_count = 0;
            pane->line_starts[0] = 0;
            for (int i = 0; i < pane->length && pane->line_count < APP_OUTPUT_MAX_LINES - 1; i++) {
                if (pane->buffer[i] == '\n') {
                    pane->line_starts[++pane->line_count] = i + 1;
                }
            }
        }

        if (pane->line_count == 0 && pane->length == 0) {
            pane->line_starts[0] = 0;
            pane->line_count = 1;
        }

        char c = *str++;
        pane->buffer[pane->length++] = c;
        pane->buffer[pane->length] = '\0';

        if (c == '\n' && pane->line_count < APP_OUTPUT_MAX_LINES - 1) {
            pane->line_starts[pane->line_count++] = pane->length;
        }
    }

    if (pane->auto_scroll) {
        pane->scroll_y = pane->line_count > 4 ? pane->line_count - 4 : 0;
    }
}

void app_output_pane_clear(app_output_pane_t *pane) {
    if (!pane) return;
    pane->buffer[0] = '\0';
    pane->length = 0;
    pane->line_count = 0;
    pane->line_starts[0] = 0;
    pane->scroll_y = 0;
}

void app_output_pane_scroll(app_output_pane_t *pane, int delta_lines) {
    if (!pane) return;
    pane->scroll_y += delta_lines;
    if (pane->scroll_y < 0) pane->scroll_y = 0;
    int max_s = pane->line_count > 4 ? pane->line_count - 4 : 0;
    if (pane->scroll_y > max_s) pane->scroll_y = max_s;
    pane->auto_scroll = (pane->scroll_y >= max_s);
}

void app_output_pane_draw(rect_t r, const app_output_pane_t *pane) {
    if (!pane) return;
    fb_rect(r.x, r.y, r.w, r.h, 0x181818);
    fb_border(r.x, r.y, r.w, r.h, 0x333333);

    /* Header Bar */
    int hdr_h = 20;
    fb_rect(r.x + 1, r.y + 1, r.w - 2, hdr_h, 0x222222);
    fb_rect(r.x + 1, r.y + hdr_h, r.w - 2, 1, 0x383838);

    draw_label(r.x + 8, r.y + 6, pane->title[0] ? pane->title : "OUTPUT", 0x9CDCFE);

    /* Auto/manual scroll indicator on right */
    font_text_fit_center(r.x + r.w - 80, r.y + 6, pane->auto_scroll ? "[AUTO]" : "[MANUAL]", 0x64748B, 70);

    /* Body text lines */
    int line_h = 14;
    int visible_lines = (r.h - hdr_h - 6) / line_h;
    if (visible_lines < 1) visible_lines = 1;

    int start_line = pane->scroll_y;
    if (start_line + visible_lines > pane->line_count) {
        start_line = pane->line_count > visible_lines ? pane->line_count - visible_lines : 0;
    }
    if (start_line < 0) start_line = 0;

    int cy = r.y + hdr_h + 4;
    for (int li = start_line; li < pane->line_count && li < start_line + visible_lines; li++) {
        int s = pane->line_starts[li];
        int e = (li + 1 < pane->line_count) ? pane->line_starts[li + 1] - 1 : pane->length;
        if (e < s) e = s;

        char line_buf[128];
        int len = e - s;
        if (len > 120) len = 120;
        for (int j = 0; j < len; j++) line_buf[j] = pane->buffer[s + j];
        line_buf[len] = '\0';

        u32 color = 0xD4D4D4;
        if (line_buf[0] == '[' && (line_buf[1] == 'F' || line_buf[1] == 'E' || line_buf[1] == 'e')) {
            color = 0xF87171; /* Error / Fail */
        } else if (line_buf[0] == '[' && (line_buf[1] == 'P' || line_buf[1] == 'O' || line_buf[1] == 'S')) {
            color = 0x34D399; /* Pass / OK / Success */
        } else if (line_buf[0] == '[' && line_buf[1] == 'W') {
            color = 0xFBBF24; /* Warn */
        }

        font_text_fit(r.x + 8, cy, line_buf, color, r.w - 24);
        cy += line_h;
    }

    /* Scrollbar */
    if (pane->line_count > visible_lines) {
        draw_scrollbar(r.x + r.w - 14, r.y + hdr_h + 2, r.h - hdr_h - 4, pane->line_count, visible_lines, pane->scroll_y);
    }
}

/* =========================================================================
 * Common Keyboard Shortcut Dispatch
 * ========================================================================= */

int app_handle_common_shortcuts(unsigned char key, int ctrl, int shift) {
    if (ctrl) {
        if (key == 's' || key == 'S') return APP_ACTION_SAVE;
        if (key == 'o' || key == 'O') return APP_ACTION_OPEN;
        if (key == 'n' || key == 'N') return APP_ACTION_NEW;
        if (key == 'w' || key == 'W') return APP_ACTION_CLOSE;
        if (key == 'f' || key == 'F') return APP_ACTION_FIND;
        if (shift && (key == 'b' || key == 'B')) return APP_ACTION_BUILD;
    }
    /* F5 = Build & Run */
    if (key == 0x3F || key == 0x7F) {
        if (shift) return APP_ACTION_STOP;
        return APP_ACTION_BUILD;
    }
    return APP_ACTION_NONE;
}
