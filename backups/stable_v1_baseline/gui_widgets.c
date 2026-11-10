#include "gui_widgets.h"
#include "framebuffer.h"
#include "font.h"
#include "theme.h"
#include "ps2.h"

/* Button press tracking */
rect_t pressed_button = {0, 0, 0, 0, 0};
int pressed_button_active = 0;

/* Tooltip state tracking */
int widget_tooltip_visible = 0;
int widget_tooltip_x = 0;
int widget_tooltip_y = 0;
char widget_tooltip_text[96] = {0};

/* Dialog state tracking */
int widget_dialog_visible = 0;
char widget_dialog_title[48] = {0};
char widget_dialog_message[128] = {0};
char widget_dialog_primary[32] = {0};
char widget_dialog_secondary[32] = {0};

/* Progress bar state tracking */
int widget_progress_visible = 0;
int widget_progress_value = 0;
int widget_progress_min = 0;
int widget_progress_max = 100;

/* Text input state pointers */
static char *s_text_input_buffer = 0;
static int   s_text_input_capacity = 0;
static int  *s_text_input_cursor_ptr = 0;
static int  *s_text_input_focus_ptr = 0;

static void str_copy_safe(char *dst, const char *src, int max) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (!src) src = "";
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int text_width(const char *text) {
    int len = 0;
    while (text && text[len]) len++;
    return len * 6;
}

int point_in_rect(int px, int py, rect_t r) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

void outline(int x, int y, int w, int h, u32 color) {
    fb_border(x, y, w, h, color);
}

void draw_label(int x, int y, const char *text, u32 color) {
    font_text(x, y, text, color, 1);
}

void draw_soft_box(rect_t r, u32 base, u32 hi, u32 lo) {
    fb_rect(r.x + 2, r.y + 2, r.w, r.h, active_theme.desktop_shadow);
    fb_rect(r.x, r.y + 2, r.w, r.h - 4, base);
    fb_rect(r.x + 2, r.y, r.w - 4, r.h, base);
    fb_rect(r.x + 3, r.y + 1, r.w - 6, 2, hi);
    fb_rect(r.x + 1, r.y + 3, 2, r.h - 6, hi);
    fb_rect(r.x + 3, r.y + r.h - 3, r.w - 6, 2, lo);
    fb_rect(r.x + r.w - 3, r.y + 3, 2, r.h - 6, lo);
    outline(r.x + 2, r.y + 1, r.w - 4, r.h - 2, 0x20344E);
}

void draw_button(rect_t r, u32 base, u32 hi, const char *text, u32 text_color) {
    int pressed = pressed_button_active && 
                  pressed_button.x == r.x && pressed_button.y == r.y && 
                  pressed_button.w == r.w && pressed_button.h == r.h;
    int hov = point_in_rect(ps2_mouse_x(), ps2_mouse_y(), r);
    u32 fill_color = pressed ? hi : (hov ? theme_mix_color(base, 0xFFFFFF) : base);
    int rad = r.h < 20 ? 3 : (r.h < 28 ? 5 : active_metrics.control_radius);
    
    fb_round_rect(r.x, r.y, r.w, r.h, rad, fill_color);
    fb_round_rect_border(r.x, r.y, r.w, r.h, rad, theme_mix_color(active_theme.window_border, fill_color));
    font_text_fit_center(r.x, r.y + (r.h - 8) / 2 + 1, text, text_color, r.w - 8);
}

void draw_checkbox(rect_t r, int checked, const char *label) {
    fb_round_rect(r.x, r.y + 1, 14, 14, 3, checked ? active_theme.accent : active_theme.control_bg_hi);
    fb_round_rect_border(r.x, r.y + 1, 14, 14, 3, checked ? active_theme.accent : active_theme.window_border);
    if (checked) {
        fb_rect(r.x + 3, r.y + 6, 2, 4, 0xFFFFFF);
        fb_rect(r.x + 5, r.y + 8, 2, 2, 0xFFFFFF);
        fb_rect(r.x + 7, r.y + 4, 2, 6, 0xFFFFFF);
    }
    draw_label(r.x + 20, r.y + 3, label, active_theme.font);
}

void draw_radio(rect_t r, int selected, const char *label) {
    fb_round_rect(r.x, r.y + 1, 14, 14, 7, active_theme.control_bg_hi);
    fb_round_rect_border(r.x, r.y + 1, 14, 14, 7, selected ? active_theme.accent : active_theme.window_border);
    if (selected) {
        fb_round_rect(r.x + 4, r.y + 5, 6, 6, 3, active_theme.accent);
    }
    draw_label(r.x + 20, r.y + 3, label, active_theme.font);
}

void draw_slider(rect_t r, int value, int min, int max) {
    int range = max - min;
    int knob_x;
    if (range < 1) range = 1;
    if (value < min) value = min;
    if (value > max) value = max;
    int track_h = 6;
    int track_y = r.y + (r.h - track_h) / 2;
    fb_round_rect(r.x, track_y, r.w, track_h, 3, active_theme.control_bg);
    int fill_w = (r.w * (value - min)) / range;
    if (fill_w > 0) {
        fb_round_rect(r.x, track_y, fill_w, track_h, 3, active_theme.accent);
    }
    knob_x = r.x + (r.w - 14) * (value - min) / range;
    int knob_size = 14;
    int knob_y = r.y + (r.h - knob_size) / 2;
    fb_round_rect(knob_x, knob_y, knob_size, knob_size, 7, 0xFFFFFF);
    fb_round_rect_border(knob_x, knob_y, knob_size, knob_size, 7, 0x8A9AA8);
}

void draw_scrollbar(int x, int y, int h, int total_items, int visible_items, int scroll_index) {
    int track_h;
    int thumb_h;
    int max_scroll;
    int thumb_y;
    if (total_items <= visible_items) return;
    fb_rect(x, y, active_metrics.scrollbar_width, h, 0xC8D1DC);
    outline(x, y, active_metrics.scrollbar_width, h, 0x77899B);
    fb_rect(x + 2, y + 2, 8, 10, 0xA8B9CC);
    fb_rect(x + 2, y + h - 12, 8, 10, 0xA8B9CC);
    draw_label(x + 4, y + 3, "^", 0x20344E);
    draw_label(x + 4, y + h - 11, "v", 0x20344E);
    track_h = h - 28;
    thumb_h = (visible_items * track_h) / total_items;
    if (thumb_h < 18) thumb_h = 18;
    max_scroll = total_items - visible_items;
    thumb_y = y + 14 + (scroll_index * (track_h - thumb_h)) / (max_scroll ? max_scroll : 1);
    draw_soft_box((rect_t){x + 2, thumb_y, 8, thumb_h, 0}, 0x8FA7C2, 0xC6D7EA, 0x6F849C);
}

void draw_text_input(rect_t r, const char* text, int cursor_pos, int has_focus, const char* placeholder) {
    int rad = r.h < 22 ? 4 : active_metrics.control_radius;
    fb_round_rect(r.x, r.y, r.w, r.h, rad, has_focus ? active_theme.control_bg_hi : active_theme.control_bg);
    fb_round_rect_border(r.x, r.y, r.w, r.h, rad, has_focus ? active_theme.accent : active_theme.window_border);
    
    int text_x = r.x + 8;
    int text_y = r.y + (r.h - 8) / 2 + 1;
    const char* display_text = (text && text[0]) ? text : placeholder;
    u32 text_color = (text && text[0]) ? active_theme.control_text : active_theme.control_text_muted;
    font_text(text_x, text_y, display_text, text_color, 1);
    
    if (has_focus && cursor_pos >= 0) {
        int cursor_x = text_x + (cursor_pos * 8);
        if (cursor_x < r.x + r.w - 6) {
            fb_rect(cursor_x, text_y - 1, 1, 10, active_theme.control_text);
        }
    }
}

void draw_progress_bar(rect_t r, int value, int min, int max) {
    int rad = r.h < 12 ? 3 : 5;
    fb_round_rect(r.x, r.y, r.w, r.h, rad, active_theme.control_bg);
    fb_round_rect_border(r.x, r.y, r.w, r.h, rad, active_theme.window_border);
    
    if (max > min) {
        int fill_w = (r.w * (value - min)) / (max - min);
        if (fill_w > 0) {
            fb_round_rect(r.x + 1, r.y + 1, fill_w - 2, r.h - 2, rad > 1 ? rad - 1 : 1, active_theme.accent);
        }
    }
}

void draw_tooltip(int x, int y, const char* text) {
    if (!text || !text[0]) return;
    
    int padding = 6;
    int text_w = text_width(text);
    int text_h = 14;
    int w = text_w + padding * 2;
    int h = text_h + padding * 2;
    
    rect_t tip = {x, y, w, h, 0};
    fb_rect(tip.x + 2, tip.y + 2, tip.w, tip.h, 0x000000);
    fb_rect(tip.x, tip.y, tip.w, tip.h, 0x202020);
    outline(tip.x, tip.y, tip.w, tip.h, 0x505050);
    font_text(tip.x + padding, tip.y + padding, text, 0xFFFFFF, 1);
}

void draw_dialog(const char* title, const char* message, const char* btn1, const char* btn2) {
    int w = 320;
    int h = 160;
    int x = ((int)fb.width - w) / 2;
    int y = ((int)fb.height - h) / 2;
    
    rect_t dialog = {x, y, w, h, active_theme.window_bg};
    fb_rect(dialog.x + 8, dialog.y + 8, dialog.w, dialog.h, 0x000000);
    fb_rect(dialog.x, dialog.y, dialog.w, dialog.h, dialog.color);
    outline(dialog.x, dialog.y, dialog.w, dialog.h, active_theme.window_border);
    
    rect_t title_bar = {dialog.x, dialog.y, dialog.w, 32, active_theme.title_bar};
    fb_rect(title_bar.x, title_bar.y, title_bar.w, title_bar.h, title_bar.color);
    font_text(dialog.x + 12, dialog.y + 10, title, active_theme.title_text, 1);
    font_text(dialog.x + 20, dialog.y + 50, message, active_theme.control_text, 1);
    
    int btn_w = 100;
    int btn_h = 32;
    int btn_y = dialog.y + dialog.h - 50;
    
    if (btn1 && btn1[0]) {
        rect_t btn1_rect = {dialog.x + dialog.w - 220 - 20, btn_y, btn_w, btn_h, active_theme.control_bg};
        draw_button(btn1_rect, active_theme.control_bg, active_theme.control_bg_hi, btn1, active_theme.control_text);
    }
    
    if (btn2 && btn2[0]) {
        rect_t btn2_rect = {dialog.x + dialog.w - 110 - 10, btn_y, btn_w, btn_h, active_theme.control_bg};
        draw_button(btn2_rect, active_theme.control_bg, active_theme.control_bg_hi, btn2, active_theme.control_text);
    }
}

void draw_toast(int x, int y, const char* message, int toast_type) {
    int padding = 10;
    int text_w = text_width(message);
    int w = text_w + padding * 2;
    int h = 40;
    rect_t toast = {x, y, w, h, 0};
    
    u32 bg_color;
    switch (toast_type) {
        case 1: bg_color = 0x00AA44; break;
        case 2: bg_color = 0xAA6600; break;
        case 3: bg_color = 0xAA2200; break;
        default: bg_color = 0x333333; break;
    }
    
    fb_rect(toast.x + 2, toast.y + 2, toast.w, toast.h, 0x000000);
    fb_rect(toast.x, toast.y, toast.w, toast.h, bg_color);
    outline(toast.x, toast.y, toast.w, toast.h, 0x666666);
    font_text(toast.x + padding, toast.y + 13, message, 0xFFFFFF, 1);
}

void gui_text_input_set(char *buffer, int capacity, int *cursor_ptr, int *focus_ptr) {
    s_text_input_buffer = buffer;
    s_text_input_capacity = capacity;
    s_text_input_cursor_ptr = cursor_ptr;
    s_text_input_focus_ptr = focus_ptr;
}

int gui_text_input_handle_key(char key, int backspace, int enter) {
    char *buffer = s_text_input_buffer;
    int *cursor_ptr = s_text_input_cursor_ptr;
    int *focus_ptr = s_text_input_focus_ptr;
    int len = 0;
    int i = 0;
    (void)enter;
    if (!buffer || !cursor_ptr || !focus_ptr || !*focus_ptr) return 0;
    while (buffer[len]) len++;
    if (backspace) {
        if (*cursor_ptr > 0 && len > 0) {
            for (i = *cursor_ptr - 1; i < len; i++) buffer[i] = buffer[i + 1];
            (*cursor_ptr)--;
            return 1;
        }
        return 0;
    }
    if (!key) return 0;
    if (len >= s_text_input_capacity - 1) return 0;
    for (i = len; i >= *cursor_ptr; i--) buffer[i + 1] = buffer[i];
    buffer[*cursor_ptr] = key;
    (*cursor_ptr)++;
    return 1;
}

void gui_progress_set(int value, int min, int max) {
    widget_progress_min = min;
    widget_progress_max = (max > min) ? max : (min + 1);
    if (value < widget_progress_min) value = widget_progress_min;
    if (value > widget_progress_max) value = widget_progress_max;
    widget_progress_value = value;
    widget_progress_visible = 1;
    gui_mark_redraw();
}

void gui_dialog_show(const char *title, const char *message, const char *primary, const char *secondary) {
    str_copy_safe(widget_dialog_title, title, (int)sizeof(widget_dialog_title));
    str_copy_safe(widget_dialog_message, message, (int)sizeof(widget_dialog_message));
    str_copy_safe(widget_dialog_primary, primary, (int)sizeof(widget_dialog_primary));
    str_copy_safe(widget_dialog_secondary, secondary, (int)sizeof(widget_dialog_secondary));
    widget_dialog_visible = 1;
}

void gui_dialog_close(void) {
    widget_dialog_visible = 0;
}

void gui_tooltip_show(int x, int y, const char *text) {
    widget_tooltip_x = x;
    widget_tooltip_y = y;
    str_copy_safe(widget_tooltip_text, text, (int)sizeof(widget_tooltip_text));
    widget_tooltip_visible = widget_tooltip_text[0] ? 1 : 0;
}

void gui_tooltip_hide(void) {
    widget_tooltip_visible = 0;
    widget_tooltip_text[0] = 0;
}
