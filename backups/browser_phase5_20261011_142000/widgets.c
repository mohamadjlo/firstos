#include "widgets.h"
#include "framebuffer.h"
#include "font.h"
#include "rtc.h"
#include "theme.h"

desktop_widget_t widget_clock = {1, 780, 42, 226, 92, 0, 0, 0};
desktop_widget_t widget_calendar = {1, 780, 142, 226, 182, 0, 0, 0};
desktop_widget_t widget_notes = {1, 780, 332, 226, 172, 0, 0, 0};
desktop_widget_t widget_weather = {1, 780, 516, 226, 110, 0, 0, 0};

int widget_clock_mode = 0; // 0 = Digital, 1 = Analog
char widget_notes_text[512] = "Welcome to FirstOS!\n- Check Settings\n- Press Ctrl+Space for search\n- Enjoy audio & video!";
int widget_notes_focused = 0;
int widget_notes_cursor = 94;

static int cal_month_offset = 0;
static int widget_active_drag = -1;

void widgets_init(void) {
    widget_clock.enabled = 1;
    widget_calendar.enabled = 1;
    widget_notes.enabled = 1;
    widget_weather.enabled = 1;
    widgets_reset_positions();
}

void widgets_reset_positions(void) {
    int sw = (int)fb.width;
    int margin = 20;
    int right_x = sw > 320 ? sw - 226 - margin : 10;
    
    widget_clock.x = right_x;
    widget_clock.y = 42;
    widget_clock.w = 226;
    widget_clock.h = 92;

    widget_calendar.x = right_x;
    widget_calendar.y = widget_clock.y + widget_clock.h + 10; // 144
    widget_calendar.w = 226;
    widget_calendar.h = 176;

    widget_notes.x = right_x;
    widget_notes.y = widget_calendar.y + widget_calendar.h + 10; // 330
    widget_notes.w = 226;
    widget_notes.h = 160;

    widget_weather.x = right_x;
    widget_weather.y = widget_notes.y + widget_notes.h + 10; // 500
    widget_weather.w = 226;
    widget_weather.h = 110;
}

/* Helper to format numbers into buffers */
static void fmt_2d(int n, char *buf) {
    if (n < 0) n = 0;
    buf[0] = '0' + (n / 10) % 10;
    buf[1] = '0' + (n % 10);
    buf[2] = 0;
}

static void draw_widget_card(desktop_widget_t *w, const char *title, u32 header_color) {
    // Shadow
    fb_draw_card_shadow(w->x, w->y, w->w, w->h, 12);
    // Frosted glass body with rounded corners
    fb_round_rect_alpha(w->x, w->y, w->w, w->h, 12, 0x121A28, 195);
    fb_round_rect_border(w->x, w->y, w->w, w->h, 12, 0x3A4D68);
    // Rounded top header
    fb_round_rect_top_alpha(w->x, w->y, w->w, 22, 12, header_color, 220);
    fb_rect(w->x, w->y + 21, w->w, 1, 0x4B6280);
    draw_label(w->x + 10, w->y + 5, title, 0xFFFFFF);
}

/* Draw Clock Widget */
static void draw_clock_widget(void) {
    if (!widget_clock.enabled) return;
    draw_widget_card(&widget_clock, "CLOCK", 0x1E3554);

    rtc_time_t now;
    rtc_read(&now);

    if (widget_clock_mode == 0) {
        /* Large digital clock */
        int hour = now.hour;
        const char *ampm = (hour >= 12) ? "PM" : "AM";
        if (hour > 12) hour -= 12;
        if (hour == 0) hour = 12;

        char hbuf[4], mbuf[4], sbuf[4];
        fmt_2d(hour, hbuf);
        fmt_2d(now.minute, mbuf);
        fmt_2d(now.second, sbuf);

        char time_str[32];
        int ti = 0;
        time_str[ti++] = hbuf[0]; time_str[ti++] = hbuf[1]; time_str[ti++] = ':';
        time_str[ti++] = mbuf[0]; time_str[ti++] = mbuf[1]; time_str[ti++] = ':';
        time_str[ti++] = sbuf[0]; time_str[ti++] = sbuf[1]; time_str[ti++] = ' ';
        time_str[ti++] = ampm[0]; time_str[ti++] = ampm[1]; time_str[ti++] = 0;

        // Draw big glowing clock time
        font_text_fit_center(widget_clock.x, widget_clock.y + 30, time_str, 0x00E5FF, widget_clock.w);

        // Date text
        static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        int mi = (now.month >= 1 && now.month <= 12) ? now.month - 1 : 8;
        char date_str[48];
        char daybuf[4]; fmt_2d(now.day, daybuf);
        int di = 0;
        date_str[di++] = '2'; date_str[di++] = '0'; date_str[di++] = '2'; date_str[di++] = '6';
        date_str[di++] = ' '; date_str[di++] = '-'; date_str[di++] = ' ';
        const char *mn = months[mi];
        while (*mn) date_str[di++] = *mn++;
        date_str[di++] = ' ';
        date_str[di++] = daybuf[0]; date_str[di++] = daybuf[1];
        date_str[di++] = 0;

        font_text_fit_center(widget_clock.x, widget_clock.y + 60, date_str, 0xC4D8EE, widget_clock.w);
    } else {
        /* Analog dial face */
        int cx = widget_clock.x + widget_clock.w / 2;
        int cy = widget_clock.y + 54;
        int r = 28;
        // Dial circle outline
        outline(cx - r, cy - r, r * 2, r * 2, 0x5C80A6);
        fb_rect(cx - 1, cy - 1, 3, 3, 0x00E5FF); // center pin

        // Hour marks
        fb_rect(cx, cy - r + 3, 1, 3, 0xFFFFFF); // 12
        fb_rect(cx + r - 6, cy, 3, 1, 0xFFFFFF); // 3
        fb_rect(cx, cy + r - 6, 1, 3, 0xFFFFFF); // 6
        fb_rect(cx - r + 3, cy, 3, 1, 0xFFFFFF); // 9

        // Approximate hands using simple rect vectors
        int s = now.second;
        int m = now.minute;
        int h = (now.hour % 12);
        // Second hand
        int sx = cx + (s >= 15 && s <= 45 ? ((30 - s) * r) / 30 : ((s > 45 ? s - 60 : s) * r) / 30);
        int sy = cy + (s <= 30 ? ((s - 15) * r) / 15 : ((45 - s) * r) / 15);
        fb_rect(sx, sy, 2, 2, 0xFF5252);

        // Minute hand
        int mx = cx + ((m - 30) * (r - 8)) / 30;
        int my = cy + ((m <= 30 ? (m - 15) : (45 - m)) * (r - 8)) / 15;
        fb_rect(mx, my, 3, 3, 0x00E5FF);

        // Hour hand
        int hx = cx + ((h - 6) * (r - 14)) / 6;
        int hy = cy + ((h <= 6 ? (h - 3) : (9 - h)) * (r - 14)) / 3;
        fb_rect(hx, hy, 4, 4, 0xFFFFFF);
    }

    // Toggle button for mode at top right of widget
    fb_round_rect(widget_clock.x + widget_clock.w - 44, widget_clock.y + 3, 40, 16, 4, 0x2A456A);
    fb_round_rect_border(widget_clock.x + widget_clock.w - 44, widget_clock.y + 3, 40, 16, 4, 0x4D6F98);
    draw_label(widget_clock.x + widget_clock.w - 41, widget_clock.y + 5, widget_clock_mode ? "DIGI" : "DIAL", 0xDDEEFF);
}

/* Draw Calendar Widget */
static void draw_calendar_widget(void) {
    if (!widget_calendar.enabled) return;
    draw_widget_card(&widget_calendar, "CALENDAR", 0x243E38);

    rtc_time_t now;
    rtc_read(&now);

    int month = now.month + cal_month_offset;
    int year = 2026;
    while (month > 12) { month -= 12; year++; }
    while (month < 1)  { month += 12; year--; }

    static const char *month_names[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    // Month Navigation Header
    fb_round_rect(widget_calendar.x + 8, widget_calendar.y + 24, 18, 16, 4, 0x1A302A);
    fb_round_rect_border(widget_calendar.x + 8, widget_calendar.y + 24, 18, 16, 4, 0x3F6255);
    draw_label(widget_calendar.x + 14, widget_calendar.y + 26, "<", 0xFFFFFF);

    fb_round_rect(widget_calendar.x + widget_calendar.w - 26, widget_calendar.y + 24, 18, 16, 4, 0x1A302A);
    fb_round_rect_border(widget_calendar.x + widget_calendar.w - 26, widget_calendar.y + 24, 18, 16, 4, 0x3F6255);
    draw_label(widget_calendar.x + widget_calendar.w - 20, widget_calendar.y + 26, ">", 0xFFFFFF);

    font_text_fit_center(widget_calendar.x, widget_calendar.y + 26, month_names[month - 1], 0x66FFB2, widget_calendar.w);

    // Days of Week Header
    static const char *dow[] = {"S","M","T","W","T","F","S"};
    int cell_w = 28;
    int start_x = widget_calendar.x + (widget_calendar.w - cell_w * 7) / 2;
    int start_y = widget_calendar.y + 46;

    for (int d = 0; d < 7; d++) {
        draw_label(start_x + d * cell_w + 10, start_y, dow[d], (d == 0 || d == 6) ? 0xFFA07A : 0x8FBAA8);
    }
    fb_rect(widget_calendar.x + 10, start_y + 14, widget_calendar.w - 20, 1, 0x2F4E40);

    // Simple day offset for Sep 2026 (Sep 1 = Tuesday = 2)
    int first_dow = (2 + (month - 9) * 2) % 7;
    if (first_dow < 0) first_dow += 7;

    int total_days = days_in_month[month - 1];
    int day_num = 1;
    for (int row = 0; row < 6 && day_num <= total_days; row++) {
        for (int col = 0; col < 7 && day_num <= total_days; col++) {
            if (row == 0 && col < first_dow) continue;

            int dx = start_x + col * cell_w + 4;
            int dy = start_y + 18 + row * 18;

            int is_today = (cal_month_offset == 0 && day_num == now.day);
            if (is_today) {
                fb_round_rect(dx - 2, dy - 2, 20, 16, 4, 0x00E5FF);
            }

            char num_buf[4];
            if (day_num < 10) {
                num_buf[0] = ' ';
                num_buf[1] = '0' + day_num;
                num_buf[2] = 0;
            } else {
                num_buf[0] = '0' + (day_num / 10);
                num_buf[1] = '0' + (day_num % 10);
                num_buf[2] = 0;
            }
            draw_label(dx, dy, num_buf, is_today ? 0x000000 : 0xD8E8E0);
            day_num++;
        }
    }
}

/* Draw Sticky Notes Widget */
static void draw_notes_widget(void) {
    if (!widget_notes.enabled) return;
    draw_widget_card(&widget_notes, "STICKY NOTE", 0x4A3E1E);

    // Clear note button [X]
    fb_round_rect(widget_notes.x + widget_notes.w - 24, widget_notes.y + 3, 18, 14, 4, 0x6A5420);
    fb_round_rect_border(widget_notes.x + widget_notes.w - 24, widget_notes.y + 3, 18, 14, 4, 0x8F7535);
    draw_label(widget_notes.x + widget_notes.w - 18, widget_notes.y + 4, "X", 0xFFFFFF);

    // Note paper area
    int text_x = widget_notes.x + 10;
    int text_y = widget_notes.y + 26;
    int max_w = widget_notes.w - 20;

    // Render multiline note text
    int cur_y = text_y;
    int line_start = 0;
    int i = 0;
    char line_buf[64];
    int li = 0;

    while (widget_notes_text[i] && cur_y < widget_notes.y + widget_notes.h - 18) {
        char ch = widget_notes_text[i];
        if (ch == '\n' || li >= 30) {
            line_buf[li] = 0;
            draw_label(text_x, cur_y, line_buf, 0xFFE082);
            cur_y += 14;
            li = 0;
            if (ch == '\n') { i++; line_start = i; continue; }
        } else {
            line_buf[li++] = ch;
        }
        i++;
    }
    if (li > 0 && cur_y < widget_notes.y + widget_notes.h - 18) {
        line_buf[li] = 0;
        draw_label(text_x, cur_y, line_buf, 0xFFE082);
        // Draw blinking cursor if focused
        if (widget_notes_focused) {
            int cx = text_x + li * 6;
            fb_rect(cx, cur_y + 1, 2, 10, 0xFFFFFF);
        }
    } else if (widget_notes_focused && cur_y < widget_notes.y + widget_notes.h - 18) {
        fb_rect(text_x, cur_y + 1, 2, 10, 0xFFFFFF);
    }
}

/* Draw Weather Widget */
static void draw_weather_widget(void) {
    if (!widget_weather.enabled) return;
    draw_widget_card(&widget_weather, weather_is_live() ? "WEATHER [LIVE]" : "WEATHER", 0x1B384A);

    int wx = widget_weather.x;
    int wy = widget_weather.y;
    int ww = widget_weather.w;

    // City and condition description
    draw_label(wx + 12, wy + 26, current_weather.city, 0xFFFFFF);
    draw_label(wx + 12, wy + 42, current_weather.desc, 0x8CB8D8);

    // Weather condition icon (28px) on the right
    draw_weather_icon(wx + ww - 36, wy + 44, current_weather.condition, 28, 0xFFFFFF);

    // Temperature display in Celsius
    char temp_str[16];
    int t = current_weather.temp_c;
    int neg = 0;
    if (t < 0) { neg = 1; t = -t; }
    int ti = 0;
    if (neg) temp_str[ti++] = '-';
    if (t >= 100) temp_str[ti++] = '0' + (t / 100) % 10;
    if (t >= 10) temp_str[ti++] = '0' + (t / 10) % 10;
    temp_str[ti++] = '0' + (t % 10);
    temp_str[ti++] = ' ';
    temp_str[ti++] = 'C';
    temp_str[ti] = 0;
    font_text_fit(wx + 12, wy + 60, temp_str, 0x00E5FF, 80);

    // Divider
    fb_rect(wx + 10, wy + 82, ww - 20, 1, 0x2E4A62);

    // Stats: Humidity & Wind
    char stats_str[48];
    int si = 0;
    const char *h_lbl = "Hum: ";
    while (*h_lbl) stats_str[si++] = *h_lbl++;
    if (current_weather.humidity >= 10) stats_str[si++] = '0' + (current_weather.humidity / 10) % 10;
    stats_str[si++] = '0' + (current_weather.humidity % 10);
    stats_str[si++] = '%';
    stats_str[si++] = ' ';
    stats_str[si++] = '|';
    stats_str[si++] = ' ';
    const char *w_lbl = "Wnd: ";
    while (*w_lbl) stats_str[si++] = *w_lbl++;
    if (current_weather.wind_kph >= 10) stats_str[si++] = '0' + (current_weather.wind_kph / 10) % 10;
    stats_str[si++] = '0' + (current_weather.wind_kph % 10);
    stats_str[si++] = 'k';
    stats_str[si++] = 'm';
    stats_str[si++] = '/';
    stats_str[si++] = 'h';
    stats_str[si] = 0;
    draw_label(wx + 12, wy + 88, stats_str, 0xABC6DB);
}

void widgets_draw(void) {
    draw_clock_widget();
    draw_calendar_widget();
    draw_notes_widget();
    draw_weather_widget();
}

int widgets_handle_click(int mx, int my, int is_right) {
    (void)is_right;
    widget_notes_focused = 0;

    // Check Clock Widget
    if (widget_clock.enabled && mx >= widget_clock.x && mx <= widget_clock.x + widget_clock.w &&
        my >= widget_clock.y && my <= widget_clock.y + widget_clock.h) {
        if (my <= widget_clock.y + 20) {
            // Clicked header -> start drag
            widget_active_drag = 0;
            widget_clock.dragging = 1;
            widget_clock.drag_ox = mx - widget_clock.x;
            widget_clock.drag_oy = my - widget_clock.y;
            return 1;
        }
        // Mode toggle button
        if (mx >= widget_clock.x + widget_clock.w - 42 && mx <= widget_clock.x + widget_clock.w - 4 &&
            my >= widget_clock.y + 3 && my <= widget_clock.y + 18) {
            widget_clock_mode = !widget_clock_mode;
            return 1;
        }
        return 1;
    }

    // Check Calendar Widget
    if (widget_calendar.enabled && mx >= widget_calendar.x && mx <= widget_calendar.x + widget_calendar.w &&
        my >= widget_calendar.y && my <= widget_calendar.y + widget_calendar.h) {
        if (my <= widget_calendar.y + 20) {
            // Drag header
            widget_active_drag = 1;
            widget_calendar.dragging = 1;
            widget_calendar.drag_ox = mx - widget_calendar.x;
            widget_calendar.drag_oy = my - widget_calendar.y;
            return 1;
        }
        // Month navigation buttons
        if (my >= widget_calendar.y + 24 && my <= widget_calendar.y + 40) {
            if (mx >= widget_calendar.x + 8 && mx <= widget_calendar.x + 28) {
                cal_month_offset--;
                return 1;
            }
            if (mx >= widget_calendar.x + widget_calendar.w - 26 && mx <= widget_calendar.x + widget_calendar.w - 6) {
                cal_month_offset++;
                return 1;
            }
        }
        return 1;
    }

    // Check Notes Widget
    if (widget_notes.enabled && mx >= widget_notes.x && mx <= widget_notes.x + widget_notes.w &&
        my >= widget_notes.y && my <= widget_notes.y + widget_notes.h) {
        if (my <= widget_notes.y + 20) {
            // Drag header or clear button
            if (mx >= widget_notes.x + widget_notes.w - 24 && mx <= widget_notes.x + widget_notes.w - 6 &&
                my >= widget_notes.y + 3 && my <= widget_notes.y + 18) {
                widget_notes_text[0] = 0;
                widget_notes_cursor = 0;
                return 1;
            }
            widget_active_drag = 2;
            widget_notes.dragging = 1;
            widget_notes.drag_ox = mx - widget_notes.x;
            widget_notes.drag_oy = my - widget_notes.y;
            return 1;
        }
        // Click body to focus note editor
        widget_notes_focused = 1;
        return 1;
    }

    // Check Weather Widget
    if (widget_weather.enabled && mx >= widget_weather.x && mx <= widget_weather.x + widget_weather.w &&
        my >= widget_weather.y && my <= widget_weather.y + widget_weather.h) {
        if (my <= widget_weather.y + 20) {
            // Drag header
            widget_active_drag = 3;
            widget_weather.dragging = 1;
            widget_weather.drag_ox = mx - widget_weather.x;
            widget_weather.drag_oy = my - widget_weather.y;
            return 1;
        }
        weather_fetch_realtime(0);
        return 1;
    }

    return 0;
}

int widgets_handle_drag(int mx, int my) {
    if (widget_active_drag == 0 && widget_clock.dragging) {
        widget_clock.x = mx - widget_clock.drag_ox;
        widget_clock.y = my - widget_clock.drag_oy;
        return 1;
    } else if (widget_active_drag == 1 && widget_calendar.dragging) {
        widget_calendar.x = mx - widget_calendar.drag_ox;
        widget_calendar.y = my - widget_calendar.drag_oy;
        return 1;
    } else if (widget_active_drag == 2 && widget_notes.dragging) {
        widget_notes.x = mx - widget_notes.drag_ox;
        widget_notes.y = my - widget_notes.drag_oy;
        return 1;
    } else if (widget_active_drag == 3 && widget_weather.dragging) {
        widget_weather.x = mx - widget_weather.drag_ox;
        widget_weather.y = my - widget_weather.drag_oy;
        return 1;
    }
    return 0;
}

void widgets_handle_drag_end(void) {
    widget_active_drag = -1;
    widget_clock.dragging = 0;
    widget_calendar.dragging = 0;
    widget_notes.dragging = 0;
    widget_weather.dragging = 0;
}

int widgets_handle_key(char c, int scancode) {
    if (!widget_notes_focused) return 0;

    int len = 0;
    while (widget_notes_text[len]) len++;

    if (scancode == 0x0E) { // Backspace
        if (len > 0) {
            widget_notes_text[len - 1] = 0;
            return 1;
        }
    } else if (scancode == 0x1C) { // Enter
        if (len < 500) {
            widget_notes_text[len] = '\n';
            widget_notes_text[len + 1] = 0;
            return 1;
        }
    } else if (c >= 32 && c <= 126 && len < 500) {
        widget_notes_text[len] = c;
        widget_notes_text[len + 1] = 0;
        return 1;
    }
    return 0;
}
