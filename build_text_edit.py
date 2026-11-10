import sys

with open('text_edit.c', 'r') as f:
    original = f.read()

new_code = """#include "text_edit.h"
#include "framebuffer.h"

extern u32 fb_rect(int x, int y, int w, int h, u32 color);
extern void outline(int x, int y, int w, int h, u32 color);
extern void draw_label(int x, int y, const char *text, u32 color);
extern void font_text(int x, int y, const char *text, u32 color, int scale);
extern void draw_scrollbar(int x, int y, int h, int total, int visible, int scroll);
extern int text_edit_has_selection(void);
extern int text_edit_selection_start(void);
extern int text_edit_selection_end(void);
extern void text_edit_clear_selection(void);

extern char editor_text[32768];
extern int editor_length;
extern int editor_cursor;
extern int editor_saved;
extern int editor_scroll_y;
extern int editor_scroll_x;
extern int editor_select_anchor;
extern int editor_select_active;
extern char editor_filename[256];
extern char editor_clipboard[4096];
extern char editor_save_name[64];
extern rect_t editor_text_area;

extern int fs_read_at(const char *path, char *buf, int size);
extern int editor_save_dialog_open;
extern int explorer_picker_mode;

static char editor_undo_text[32768];
static int editor_undo_length;
static int editor_undo_cursor;
static int editor_undo_saved;
static int editor_undo_available;

static int editor_font_scale = 1;
static u32 editor_text_color = 0x2A2A2A;

static rect_t btn_new;
static rect_t btn_open;
static rect_t btn_save;
static rect_t btn_zoom_out;
static rect_t btn_zoom_in;
static rect_t btn_color;

static void editor_snapshot_undo(void) {
    int i;
    for (i = 0; i < editor_length && i < (int)sizeof(editor_undo_text) - 1; i++) editor_undo_text[i] = editor_text[i];
    editor_undo_text[i] = 0;
    editor_undo_length = editor_length;
    editor_undo_cursor = editor_cursor;
    editor_undo_saved = editor_saved;
    editor_undo_available = 1;
}

static int editor_line_height(void) { return 8 * editor_font_scale + 6; }
static int editor_col_width(void) { return 6 * editor_font_scale; }

static int editor_visible_cols_internal(void) {
    int cols = editor_text_area.w / editor_col_width();
    return cols < 1 ? 1 : cols;
}

int text_edit_visible_cols(void) {
    return editor_visible_cols_internal();
}

int text_edit_longest_line_cols(void) {
    int i;
    int col = 0;
    int max_col = 0;
    for (i = 0; i < editor_length; i++) {
        if (editor_text[i] == '\\n') {
            if (col > max_col) max_col = col;
            col = 0;
        } else col++;
    }
    if (col > max_col) max_col = col;
    return max_col;
}

static int editor_visible_lines(void) {
    int lines = editor_text_area.h / editor_line_height();
    return lines < 1 ? 1 : lines;
}

int text_edit_position_from_point(int x, int y) {
    int col = (x - editor_text_area.x) / editor_col_width();
    int line = (y - editor_text_area.y) / editor_line_height();
    int current_line = 0;
    int current_col = 0;
    int i;
    col += editor_scroll_x;
    line += editor_scroll_y;
    if (line < 0) line = 0;
    if (col < 0) col = 0;
    for (i = 0; i < editor_length; i++) {
        if (current_line == line && current_col >= col) return i;
        if (editor_text[i] == '\\n') {
            if (current_line == line) return i;
            current_line++;
            current_col = 0;
        } else {
            current_col++;
        }
    }
    return editor_length;
}

static int editor_total_lines(void) {
    int i;
    int lines = 1;
    for (i = 0; i < editor_length; i++) if (editor_text[i] == '\\n') lines++;
    return lines;
}

static void editor_clamp_scroll(void) {
    int max_scroll = editor_total_lines() - editor_visible_lines();
    if (max_scroll < 0) max_scroll = 0;
    if (editor_scroll_y < 0) editor_scroll_y = 0;
    if (editor_scroll_y > max_scroll) editor_scroll_y = max_scroll;
}

static void editor_clamp_scroll_x(void) {
    int max_scroll = text_edit_longest_line_cols() - text_edit_visible_cols();
    if (max_scroll < 0) max_scroll = 0;
    if (editor_scroll_x < 0) editor_scroll_x = 0;
    if (editor_scroll_x > max_scroll) editor_scroll_x = max_scroll;
}

static void editor_cursor_metrics(int *line_out, int *col_out) {
    int i;
    int line = 0;
    int col = 0;
    for (i = 0; i < editor_cursor && i < editor_length; i++) {
        if (editor_text[i] == '\\n') { line++; col = 0; }
        else col++;
    }
    if (line_out) *line_out = line;
    if (col_out) *col_out = col;
}

static void editor_scroll_into_view(void) {
    int line;
    int col;
    editor_cursor_metrics(&line, &col);
    if (line < editor_scroll_y) editor_scroll_y = line;
    if (line >= editor_scroll_y + editor_visible_lines()) editor_scroll_y = line - editor_visible_lines() + 1;
    if (col < editor_scroll_x) editor_scroll_x = col;
    if (col >= editor_scroll_x + text_edit_visible_cols()) editor_scroll_x = col - text_edit_visible_cols() + 1;
    editor_clamp_scroll();
    editor_clamp_scroll_x();
}

static int editor_position_for_line_col(int target_line, int target_col) {
    int pos = 0;
    int line = 0;
    int col = 0;
    if (target_line < 0) target_line = 0;
    if (target_col < 0) target_col = 0;
    for (pos = 0; pos < editor_length; pos++) {
        if (line == target_line && col >= target_col) return pos;
        if (editor_text[pos] == '\\n') {
            if (line == target_line) return pos;
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    return editor_length;
}

void text_edit_cursor_up(void) {
    int line, col;
    editor_cursor_metrics(&line, &col);
    if (line > 0) editor_cursor = editor_position_for_line_col(line - 1, col);
    else editor_cursor = 0;
    text_edit_clear_selection();
    editor_scroll_into_view();
}

void text_edit_cursor_down(void) {
    int line, col;
    editor_cursor_metrics(&line, &col);
    if (line < editor_total_lines() - 1) editor_cursor = editor_position_for_line_col(line + 1, col);
    else editor_cursor = editor_length;
    text_edit_clear_selection();
    editor_scroll_into_view();
}

void text_edit_cursor_left(void) {
    if (editor_cursor > 0) editor_cursor--;
    text_edit_clear_selection();
    editor_scroll_into_view();
}

void text_edit_cursor_right(void) {
    if (editor_cursor < editor_length) editor_cursor++;
    text_edit_clear_selection();
    editor_scroll_into_view();
}

void text_edit_scroll_cols(int delta) {
    editor_scroll_x += delta;
    editor_clamp_scroll_x();
}

void text_edit_scroll_lines(int delta) {
    editor_scroll_y += delta;
    editor_clamp_scroll();
}

static void editor_delete_selection(void) {
    editor_snapshot_undo();
    int start;
    int end;
    int i;
    if (!text_edit_has_selection()) return;
    start = text_edit_selection_start();
    end = text_edit_selection_end();
    for (i = end; i <= editor_length; i++) editor_text[start + (i - end)] = editor_text[i];
    editor_length -= (end - start);
    editor_cursor = start;
    text_edit_clear_selection();
    editor_saved = 0;
    editor_scroll_into_view();
}

void text_edit_insert_char(char c) {
    editor_snapshot_undo();
    int i;
    if (text_edit_has_selection()) editor_delete_selection();
    if (editor_length >= 32767) return;
    for (i = editor_length; i >= editor_cursor; i--) editor_text[i + 1] = editor_text[i];
    editor_text[editor_cursor] = c;
    editor_cursor++;
    editor_length++;
    editor_saved = 0;
    editor_scroll_into_view();
}

void text_edit_backspace(void) {
    editor_snapshot_undo();
    int i;
    if (text_edit_has_selection()) { editor_delete_selection(); return; }
    if (editor_cursor <= 0 || editor_length <= 0) return;
    for (i = editor_cursor - 1; i < editor_length; i++) editor_text[i] = editor_text[i + 1];
    editor_cursor--;
    editor_length--;
    editor_saved = 0;
    editor_scroll_into_view();
}

void text_edit_copy_selection(void) {
    int start;
    int end;
    int len;
    int i;
    if (!text_edit_has_selection()) return;
    start = text_edit_selection_start();
    end = text_edit_selection_end();
    len = end - start;
    if (len >= 4095) len = 4095;
    for (i = 0; i < len; i++) editor_clipboard[i] = editor_text[start + i];
    editor_clipboard[len] = 0;
}

void text_edit_cut_selection(void) {
    if (!text_edit_has_selection()) return;
    text_edit_copy_selection();
    editor_delete_selection();
}

void text_edit_paste_clipboard(void) {
    editor_snapshot_undo();
    int i = 0;
    while (editor_clipboard[i]) {
        text_edit_insert_char(editor_clipboard[i]);
        i++;
    }
}

void text_edit_select_all(void) {
    editor_select_anchor = 0;
    editor_select_active = editor_length;
    editor_cursor = editor_length;
    editor_scroll_into_view();
}

void text_edit_mark_saved(void) {
    editor_saved = 1;
}

void draw_text_edit_contents(app_window_t *app) {
    int x = app->frame.x + 12;
    int y = app->frame.y + 42;
    int w = app->frame.w - 24;
    int h = app->frame.h - 54;
    int i;
    int line = 0;
    int col = 0;
    int visible_cols;

    fb_rect(x, y, w, h, 0xFFFDF8);
    
    // Modern Toolbar
    fb_rect(x, y, w, 32, 0xEBEBEB);
    outline(x, y, w, 32, 0xC4C4C4);
    
    btn_new = (rect_t){x + 8, y + 6, 60, 20, 0};
    btn_open = (rect_t){x + 72, y + 6, 60, 20, 0};
    btn_save = (rect_t){x + 136, y + 6, 60, 20, 0};
    btn_zoom_out = (rect_t){x + 200, y + 6, 30, 20, 0};
    btn_zoom_in = (rect_t){x + 234, y + 6, 30, 20, 0};
    btn_color = (rect_t){x + 268, y + 6, 60, 20, 0};

    fb_rect(btn_new.x, btn_new.y, btn_new.w, btn_new.h, 0xFFFFFF); outline(btn_new.x, btn_new.y, btn_new.w, btn_new.h, 0xAAAAAA);
    draw_label(btn_new.x + 12, btn_new.y + 5, "NEW", 0x333333);
    
    fb_rect(btn_open.x, btn_open.y, btn_open.w, btn_open.h, 0xFFFFFF); outline(btn_open.x, btn_open.y, btn_open.w, btn_open.h, 0xAAAAAA);
    draw_label(btn_open.x + 8, btn_open.y + 5, "OPEN", 0x333333);
    
    fb_rect(btn_save.x, btn_save.y, btn_save.w, btn_save.h, editor_saved ? 0xEFEFEF : 0xCCE5FF); outline(btn_save.x, btn_save.y, btn_save.w, btn_save.h, editor_saved ? 0xAAAAAA : 0x73A1D3);
    draw_label(btn_save.x + 8, btn_save.y + 5, "SAVE", 0x333333);
    
    fb_rect(btn_zoom_out.x, btn_zoom_out.y, btn_zoom_out.w, btn_zoom_out.h, 0xFFFFFF); outline(btn_zoom_out.x, btn_zoom_out.y, btn_zoom_out.w, btn_zoom_out.h, 0xAAAAAA);
    draw_label(btn_zoom_out.x + 12, btn_zoom_out.y + 5, "-", 0x333333);

    fb_rect(btn_zoom_in.x, btn_zoom_in.y, btn_zoom_in.w, btn_zoom_in.h, 0xFFFFFF); outline(btn_zoom_in.x, btn_zoom_in.y, btn_zoom_in.w, btn_zoom_in.h, 0xAAAAAA);
    draw_label(btn_zoom_in.x + 12, btn_zoom_in.y + 5, "+", 0x333333);

    fb_rect(btn_color.x, btn_color.y, btn_color.w, btn_color.h, 0xFFFFFF); outline(btn_color.x, btn_color.y, btn_color.w, btn_color.h, editor_text_color);
    draw_label(btn_color.x + 6, btn_color.y + 5, "COLOR", editor_text_color);

    draw_label(x + w - 96, y + 10, editor_saved ? "SAVED" : "EDITED", 0x777777);

    // Text Area
    fb_rect(x + 8, y + 42, w - 16, h - 50, 0xFFFFFF);
    outline(x + 8, y + 42, w - 16, h - 50, 0xD8C9B7);
    editor_text_area.x = x + 16;
    editor_text_area.y = y + 52;
    editor_text_area.w = w - 38;
    editor_text_area.h = h - 70;
    editor_text_area.color = 0;
    visible_cols = text_edit_visible_cols();
    editor_clamp_scroll();
    editor_clamp_scroll_x();

    for (i = 0; i < editor_length; i++) {
        char c = editor_text[i];
        int selected = text_edit_has_selection() && i >= text_edit_selection_start() && i < text_edit_selection_end();
        int draw_line = line - editor_scroll_y;
        int draw_col = col - editor_scroll_x;
        if (selected && draw_line >= 0 && draw_line < editor_visible_lines() && draw_col >= 0 && draw_col < visible_cols) {
            fb_rect(editor_text_area.x + draw_col * editor_col_width() - 1, editor_text_area.y + draw_line * editor_line_height() - 1, editor_col_width()+1, editor_line_height()-1, 0xCFE1F7);
        }
        if (c == '\\n') {
            line++;
            col = 0;
            continue;
        }
        if (draw_line >= 0 && draw_line < editor_visible_lines() && draw_col >= 0 && draw_col < visible_cols) {
            char one[2];
            one[0] = c;
            one[1] = 0;
            font_text(editor_text_area.x + draw_col * editor_col_width(), editor_text_area.y + draw_line * editor_line_height(), one, editor_text_color, editor_font_scale);
        }
        col++;
    }

    draw_scrollbar(app->frame.x + app->frame.w - 20, editor_text_area.y, editor_text_area.h, editor_total_lines(), editor_visible_lines(), editor_scroll_y);
    if (text_edit_longest_line_cols() > text_edit_visible_cols()) {
        int sx = editor_text_area.x;
        int sy = editor_text_area.y + editor_text_area.h + 4;
        int sw = editor_text_area.w;
        int track = sw - 28;
        int max_scroll = text_edit_longest_line_cols() - text_edit_visible_cols();
        int thumb = (text_edit_visible_cols() * track) / text_edit_longest_line_cols();
        int thumb_x;
        if (thumb < 12) thumb = 12;
        if (thumb > track) thumb = track;
        fb_rect(sx, sy, sw, 12, 0xC8D1DC);
        outline(sx, sy, sw, 12, 0x77899B);
        fb_rect(sx + 2, sy + 2, 10, 8, 0xA8B9CC);
        fb_rect(sx + sw - 12, sy + 2, 10, 8, 0xA8B9CC);
        draw_label(sx + 4, sy + 2, "<", 0x20344E);
        draw_label(sx + sw - 10, sy + 2, ">", 0x20344E);
        fb_rect(sx + 14, sy + 2, track, 8, 0xDCE4EE);
        thumb_x = sx + 14 + (max_scroll ? (editor_scroll_x * (track - thumb)) / max_scroll : 0);
        fb_rect(thumb_x, sy + 2, thumb, 8, 0x8EA4BA);
        outline(thumb_x, sy + 2, thumb, 8, 0x52677F);
    }
}

static int point_in_rect_local(int x, int y, rect_t r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void text_edit_open_file(const char *path) {
    editor_snapshot_undo();
    int i = 0;
    int bytes = fs_read_at(path, editor_text, 32767);
    if (bytes >= 0) {
        editor_text[bytes] = 0;
        editor_length = bytes;
        editor_cursor = 0;
        editor_saved = 1;
        editor_scroll_y = 0;
        editor_scroll_x = 0;
        text_edit_clear_selection();
        while (path[i] && i < 255) { editor_filename[i] = path[i]; i++; }
        editor_filename[i] = 0;
    }
}

void handle_text_edit_click(app_window_t *app, int mx, int my) {
    (void)app;
    
    if (point_in_rect_local(mx, my, btn_new)) {
        void text_edit_new_file(void);
        text_edit_new_file();
        return;
    }
    if (point_in_rect_local(mx, my, btn_save)) {
        editor_save_dialog_open = 1;
        explorer_picker_mode = 1;
        return;
    }
    if (point_in_rect_local(mx, my, btn_open)) {
        editor_save_dialog_open = 1;
        explorer_picker_mode = 2; // custom mode for OPEN
        return;
    }
    if (point_in_rect_local(mx, my, btn_zoom_out)) {
        if (editor_font_scale > 1) editor_font_scale--;
        return;
    }
    if (point_in_rect_local(mx, my, btn_zoom_in)) {
        if (editor_font_scale < 4) editor_font_scale++;
        return;
    }
    if (point_in_rect_local(mx, my, btn_color)) {
        if (editor_text_color == 0x2A2A2A) editor_text_color = 0x2980B9;
        else if (editor_text_color == 0x2980B9) editor_text_color = 0x27AE60;
        else if (editor_text_color == 0x27AE60) editor_text_color = 0xC0392B;
        else if (editor_text_color == 0xC0392B) editor_text_color = 0x2A2A2A;
        return;
    }

    if (mx >= editor_text_area.x && mx < editor_text_area.x + editor_text_area.w && my >= editor_text_area.y && my < editor_text_area.y + editor_text_area.h) {
        editor_cursor = text_edit_position_from_point(mx, my);
        editor_select_anchor = editor_cursor;
        editor_select_active = editor_cursor;
        editor_scroll_into_view();
    }
}

void handle_text_edit_drag(int mx, int my) {
    editor_select_active = text_edit_position_from_point(mx, my);
    editor_cursor = editor_select_active;
    editor_scroll_into_view();
}

void text_edit_undo(void) {
    int i;
    if (!editor_undo_available) return;
    for (i = 0; i <= editor_undo_length && i < 32767; i++) editor_text[i] = editor_undo_text[i];
    editor_length = editor_undo_length;
    editor_cursor = editor_undo_cursor;
    editor_saved = editor_undo_saved;
    editor_undo_available = 0;
}

void text_edit_new_file(void) {
    editor_snapshot_undo();
    editor_filename[0] = 0;
    editor_save_name[0] = 0;
    editor_text[0] = 0;
    editor_length = 0;
    editor_cursor = 0;
    editor_saved = 1;
    editor_scroll_y = 0;
    editor_scroll_x = 0;
    text_edit_clear_selection();
}

void text_edit_open_sample(void) {
    static const char *sample = "Opened file in Text Edit.\\n\\nThis is a sample loaded from the App Menu.\\nUse Save to store your changes.";
    int i = 0;
    editor_snapshot_undo();
    while (sample[i] && i < 32767) { editor_text[i] = sample[i]; i++; }
    editor_text[i] = 0;
    editor_length = i;
    editor_cursor = i;
    editor_saved = 1;
    editor_scroll_y = 0;
    editor_scroll_x = 0;
    text_edit_clear_selection();
}

void text_edit_clear(void) {
    text_edit_new_file();
}
"""

with open('text_edit.c', 'w') as f:
    f.write(new_code)
