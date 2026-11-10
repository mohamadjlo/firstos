#include "ui_builder.h"
#include "ide.h"
#include "file_dialog.h"

ide_tab_t ide_tabs[4];
int ide_active_tab = 0;

void ide_init_tabs(void) {
    int i;
    for(i=0; i<4; i++) {
        ide_tabs[i].length = 0;
        ide_tabs[i].cursor = 0;
        ide_tabs[i].scroll_y = 0;
        ide_tabs[i].scroll_x = 0;
        ide_tabs[i].saved = 1;
        ide_tabs[i].select_anchor = -1;
        ide_tabs[i].select_active = -1;
        ide_tabs[i].filename[0] = 0;
        ide_tabs[i].text[0] = 0;
    }
}

void ide_switch_tab(int index) {
    if (index >= 0 && index < 4) ide_active_tab = index;
}

void ide_close_tab(int index) {
    if (index >= 0 && index < 4) {
        ide_tabs[index].filename[0] = 0;
        ide_tabs[index].text[0] = 0;
        ide_tabs[index].length = 0;
        ide_tabs[index].cursor = 0;
        ide_tabs[index].scroll_x = 0;
        ide_tabs[index].scroll_y = 0;
        ide_tabs[index].saved = 1;
        ide_tabs[index].select_anchor = -1;
        ide_tabs[index].select_active = -1;
        if (ide_active_tab == index) {
            // Find another populated tab to switch to
            int ti;
            for (ti = 0; ti < 4; ti++) {
                if (ti != index && (ide_tabs[ti].text[0] || ide_tabs[ti].filename[0])) {
                    ide_active_tab = ti;
                    return;
                }
            }
        }
    }
}

#include "text_edit.h"

extern char ide_save_name[32];
extern int ide_save_dialog_open;
extern char ide_clipboard[512];

extern rect_t ide_text_area;

#include "framebuffer.h"

extern void fb_rect(int x, int y, int w, int h, u32 color);
extern void outline(int x, int y, int w, int h, u32 color);
extern void draw_label(int x, int y, const char *text, u32 color);
extern void font_text(int x, int y, const char *text, u32 color, int scale);
extern void draw_scrollbar(int x, int y, int h, int total, int visible, int scroll);

void ide_clear_selection(void) {
    ide_select_anchor = -1;
    ide_select_active = -1;
}

int ide_has_selection(void) {
    return ide_select_anchor >= 0 && ide_select_active >= 0 && ide_select_anchor != ide_select_active;
}

int ide_selection_start(void) {
    return ide_select_anchor < ide_select_active ? ide_select_anchor : ide_select_active;
}

int ide_selection_end(void) {
    return ide_select_anchor > ide_select_active ? ide_select_anchor : ide_select_active;
}

extern char ide_clipboard[512];
extern char ide_save_name[32];
extern rect_t ide_text_area;

extern int fs_read_at(const char *path, char *buf, u32 size);
extern int ide_save_dialog_open;
extern int explorer_picker_mode;


static int ide_undo_length;
static int ide_undo_cursor;
static int ide_undo_saved;
static int ide_undo_available;

static int ide_font_scale = 1;
static u32 ide_text_color = 0x2A2A2A;

static rect_t btn_new;
static rect_t btn_open;
static rect_t btn_save;
static rect_t btn_zoom_out;
static rect_t btn_zoom_in;
static rect_t btn_color;

static void ide_snapshot_undo(void) {
    int i;
    for (i = 0; i < ide_length && i < (int)sizeof(ide_undo_text) - 1; i++) ide_undo_text[i] = ide_text[i];
    ide_undo_text[i] = 0;
    ide_undo_length = ide_length;
    ide_undo_cursor = ide_cursor;
    ide_undo_saved = ide_saved;
    ide_undo_available = 1;
}

static int ide_line_height(void) { return 8 * ide_font_scale + 6; }
static int ide_col_width(void) { return 6 * ide_font_scale; }

static int ide_visible_cols_internal(void) {
    int cols = ide_text_area.w / ide_col_width();
    return cols < 1 ? 1 : cols;
}

int ide_visible_cols(void) {
    return ide_visible_cols_internal();
}

int ide_longest_line_cols(void) {
    int i;
    int col = 0;
    int max_col = 0;
    for (i = 0; i < ide_length; i++) {
        if (ide_text[i] == '\n') {
            if (col > max_col) max_col = col;
            col = 0;
        } else col++;
    }
    if (col > max_col) max_col = col;
    return max_col;
}

static int ide_visible_lines(void) {
    int lines = ide_text_area.h / ide_line_height();
    return lines < 1 ? 1 : lines;
}

int ide_position_from_point(int x, int y) {
    int col = (x - ide_text_area.x) / ide_col_width();
    int line = (y - ide_text_area.y) / ide_line_height();
    int current_line = 0;
    int current_col = 0;
    int i;
    col += ide_scroll_x;
    line += ide_scroll_y;
    if (line < 0) line = 0;
    if (col < 0) col = 0;
    for (i = 0; i < ide_length; i++) {
        if (current_line == line && current_col >= col) return i;
        if (ide_text[i] == '\n') {
            if (current_line == line) return i;
            current_line++;
            current_col = 0;
        } else {
            current_col++;
        }
    }
    return ide_length;
}

static int ide_total_lines(void) {
    int i;
    int lines = 1;
    for (i = 0; i < ide_length; i++) if (ide_text[i] == '\n') lines++;
    return lines;
}

static void ide_clamp_scroll(void) {
    int max_scroll = ide_total_lines() - ide_visible_lines();
    if (max_scroll < 0) max_scroll = 0;
    if (ide_scroll_y < 0) ide_scroll_y = 0;
    if (ide_scroll_y > max_scroll) ide_scroll_y = max_scroll;
}

static void ide_clamp_scroll_x(void) {
    int max_scroll = ide_longest_line_cols() - ide_visible_cols();
    if (max_scroll < 0) max_scroll = 0;
    if (ide_scroll_x < 0) ide_scroll_x = 0;
    if (ide_scroll_x > max_scroll) ide_scroll_x = max_scroll;
}

static void ide_cursor_metrics(int *line_out, int *col_out) {
    int i;
    int line = 0;
    int col = 0;
    for (i = 0; i < ide_cursor && i < ide_length; i++) {
        if (ide_text[i] == '\n') { line++; col = 0; }
        else col++;
    }
    if (line_out) *line_out = line;
    if (col_out) *col_out = col;
}

static void ide_scroll_into_view(void) {
    int line;
    int col;
    ide_cursor_metrics(&line, &col);
    if (line < ide_scroll_y) ide_scroll_y = line;
    if (line >= ide_scroll_y + ide_visible_lines()) ide_scroll_y = line - ide_visible_lines() + 1;
    if (col < ide_scroll_x) ide_scroll_x = col;
    if (col >= ide_scroll_x + ide_visible_cols()) ide_scroll_x = col - ide_visible_cols() + 1;
    ide_clamp_scroll();
    ide_clamp_scroll_x();
}

static int ide_position_for_line_col(int target_line, int target_col) {
    int pos = 0;
    int line = 0;
    int col = 0;
    if (target_line < 0) target_line = 0;
    if (target_col < 0) target_col = 0;
    for (pos = 0; pos < ide_length; pos++) {
        if (line == target_line && col >= target_col) return pos;
        if (ide_text[pos] == '\n') {
            if (line == target_line) return pos;
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    return ide_length;
}

void ide_cursor_up(void) {
    int line, col;
    ide_cursor_metrics(&line, &col);
    if (line > 0) ide_cursor = ide_position_for_line_col(line - 1, col);
    else ide_cursor = 0;
    ide_clear_selection();
    ide_scroll_into_view();
}

void ide_cursor_down(void) {
    int line, col;
    ide_cursor_metrics(&line, &col);
    if (line < ide_total_lines() - 1) ide_cursor = ide_position_for_line_col(line + 1, col);
    else ide_cursor = ide_length;
    ide_clear_selection();
    ide_scroll_into_view();
}

void ide_cursor_left(void) {
    if (ide_cursor > 0) ide_cursor--;
    ide_clear_selection();
    ide_scroll_into_view();
}

void ide_cursor_right(void) {
    if (ide_cursor < ide_length) ide_cursor++;
    ide_clear_selection();
    ide_scroll_into_view();
}

void ide_scroll_cols(int delta) {
    ide_scroll_x += delta;
    ide_clamp_scroll_x();
}

void ide_scroll_lines(int delta) {
    ide_scroll_y += delta;
    ide_clamp_scroll();
}

static void ide_delete_selection(void) {
    ide_snapshot_undo();
    int start;
    int end;
    int i;
    if (!ide_has_selection()) return;
    start = ide_selection_start();
    end = ide_selection_end();
    for (i = end; i <= ide_length; i++) ide_text[start + (i - end)] = ide_text[i];
    ide_length -= (end - start);
    ide_cursor = start;
    ide_clear_selection();
    ide_saved = 0;
    ide_scroll_into_view();
}

void ide_insert_char(char c) {
    ide_snapshot_undo();
    int i;
    if (ide_has_selection()) ide_delete_selection();
    if (ide_length >= 32767) return;
    for (i = ide_length; i >= ide_cursor; i--) ide_text[i + 1] = ide_text[i];
    ide_text[ide_cursor] = c;
    ide_cursor++;
    ide_length++;
    ide_saved = 0;
    ide_scroll_into_view();
}

void ide_backspace(void) {
    ide_snapshot_undo();
    int i;
    if (ide_has_selection()) { ide_delete_selection(); return; }
    if (ide_cursor <= 0 || ide_length <= 0) return;
    for (i = ide_cursor - 1; i < ide_length; i++) ide_text[i] = ide_text[i + 1];
    ide_cursor--;
    ide_length--;
    ide_saved = 0;
    ide_scroll_into_view();
}

void ide_copy_selection(void) {
    int start;
    int end;
    int len;
    int i;
    if (!ide_has_selection()) return;
    start = ide_selection_start();
    end = ide_selection_end();
    len = end - start;
    if (len >= 511) len = 511;
    for (i = 0; i < len; i++) ide_clipboard[i] = ide_text[start + i];
    ide_clipboard[len] = 0;
}

void ide_cut_selection(void) {
    if (!ide_has_selection()) return;
    ide_copy_selection();
    ide_delete_selection();
}

void ide_paste_clipboard(void) {
    ide_snapshot_undo();
    int i = 0;
    while (ide_clipboard[i]) {
        ide_insert_char(ide_clipboard[i]);
        i++;
    }
}

void ide_select_all(void) {
    ide_select_anchor = 0;
    ide_select_active = ide_length;
    ide_cursor = ide_length;
    ide_scroll_into_view();
}

void ide_mark_saved(void) {
    ide_saved = 1;
}

void draw_ide_contents(app_window_t *app) {
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
    rect_t btn_design = (rect_t){x + 332, y + 6, 80, 20, 0};
    fb_rect(btn_design.x, btn_design.y, btn_design.w, btn_design.h, ide_designer_mode ? 0xCCE5FF : 0xFFFFFF); 
    outline(btn_design.x, btn_design.y, btn_design.w, btn_design.h, ide_designer_mode ? 0x73A1D3 : 0xAAAAAA);
    draw_label(btn_design.x + 12, btn_design.y + 5, "DESIGN", 0x333333);

    fb_rect(btn_new.x, btn_new.y, btn_new.w, btn_new.h, 0xFFFFFF); outline(btn_new.x, btn_new.y, btn_new.w, btn_new.h, 0xAAAAAA);
    draw_label(btn_new.x + 12, btn_new.y + 5, "NEW", 0x333333);
    
    fb_rect(btn_open.x, btn_open.y, btn_open.w, btn_open.h, 0xFFFFFF); outline(btn_open.x, btn_open.y, btn_open.w, btn_open.h, 0xAAAAAA);
    draw_label(btn_open.x + 8, btn_open.y + 5, "OPEN", 0x333333);
    
    fb_rect(btn_save.x, btn_save.y, btn_save.w, btn_save.h, ide_saved ? 0xEFEFEF : 0xCCE5FF); outline(btn_save.x, btn_save.y, btn_save.w, btn_save.h, ide_saved ? 0xAAAAAA : 0x73A1D3);
    draw_label(btn_save.x + 8, btn_save.y + 5, "SAVE", 0x333333);
    
    fb_rect(btn_zoom_out.x, btn_zoom_out.y, btn_zoom_out.w, btn_zoom_out.h, 0xFFFFFF); outline(btn_zoom_out.x, btn_zoom_out.y, btn_zoom_out.w, btn_zoom_out.h, 0xAAAAAA);
    draw_label(btn_zoom_out.x + 12, btn_zoom_out.y + 5, "-", 0x333333);

    fb_rect(btn_zoom_in.x, btn_zoom_in.y, btn_zoom_in.w, btn_zoom_in.h, 0xFFFFFF); outline(btn_zoom_in.x, btn_zoom_in.y, btn_zoom_in.w, btn_zoom_in.h, 0xAAAAAA);
    draw_label(btn_zoom_in.x + 12, btn_zoom_in.y + 5, "+", 0x333333);

    fb_rect(btn_color.x, btn_color.y, btn_color.w, btn_color.h, 0xFFFFFF); outline(btn_color.x, btn_color.y, btn_color.w, btn_color.h, ide_text_color);
    draw_label(btn_color.x + 6, btn_color.y + 5, "COLOR", ide_text_color);

    draw_label(x + w - 96, y + 10, ide_saved ? "SAVED" : "EDITED", 0x777777);

    // Text Area
    fb_rect(x + 8, y + 42, w - 16, h - 50, 0xFFFFFF);
    outline(x + 8, y + 42, w - 16, h - 50, 0xD8C9B7);
    ide_text_area.x = x + 16;
    ide_text_area.y = y + 52;
    ide_text_area.w = w - 38;
    ide_text_area.h = h - 70;
    ide_text_area.color = 0;
    visible_cols = ide_visible_cols();
    ide_clamp_scroll();
    ide_clamp_scroll_x();

    for (i = 0; i < ide_length; i++) {
        char c = ide_text[i];
        int selected = ide_has_selection() && i >= ide_selection_start() && i < ide_selection_end();
        int draw_line = line - ide_scroll_y;
        int draw_col = col - ide_scroll_x;
        if (selected && draw_line >= 0 && draw_line < ide_visible_lines() && draw_col >= 0 && draw_col < visible_cols) {
            fb_rect(ide_text_area.x + draw_col * ide_col_width() - 1, ide_text_area.y + draw_line * ide_line_height() - 1, ide_col_width()+1, ide_line_height()-1, 0xCFE1F7);
        }
        
        // draw cursor
        if (i == ide_cursor && draw_line >= 0 && draw_line < ide_visible_lines() && draw_col >= 0 && draw_col < visible_cols) {
            fb_rect(ide_text_area.x + draw_col * ide_col_width(), ide_text_area.y + draw_line * ide_line_height(), 2, ide_line_height()-2, 0x000000);
        }
        if (c == '\n') {
            line++;
            col = 0;
            continue;
        }
        if (draw_line >= 0 && draw_line < ide_visible_lines() && draw_col >= 0 && draw_col < visible_cols) {
            char one[2];
            one[0] = c;
            one[1] = 0;
            font_text(ide_text_area.x + draw_col * ide_col_width(), ide_text_area.y + draw_line * ide_line_height(), one, ide_text_color, ide_font_scale);
        }
        col++;
    }

    draw_scrollbar(app->frame.x + app->frame.w - 20, ide_text_area.y, ide_text_area.h, ide_total_lines(), ide_visible_lines(), ide_scroll_y);
    if (ide_longest_line_cols() > ide_visible_cols()) {
        int sx = ide_text_area.x;
        int sy = ide_text_area.y + ide_text_area.h + 4;
        int sw = ide_text_area.w;
        int track = sw - 28;
        int max_scroll = ide_longest_line_cols() - ide_visible_cols();
        int thumb = (ide_visible_cols() * track) / ide_longest_line_cols();
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
        thumb_x = sx + 14 + (max_scroll ? (ide_scroll_x * (track - thumb)) / max_scroll : 0);
        fb_rect(thumb_x, sy + 2, thumb, 8, 0x8EA4BA);
        outline(thumb_x, sy + 2, thumb, 8, 0x52677F);
    }
    
    // Auto-complete logic
    if (ide_cursor > 0) {
        int w_start = ide_cursor - 1;
        while (w_start > 0 && ide_text[w_start-1] != ' ' && ide_text[w_start-1] != '\n') w_start--;
        int w_len = ide_cursor - w_start;
        if (w_len > 0 && w_len < 16) {
            char word[32];
            int wi = 0; for(wi=0; wi<w_len; wi++) word[wi] = ide_text[w_start + wi]; word[wi]=0;
            
            const char* suggestions[] = {
                "function", "class", "let", "put", "increase", "decrease", "multiply", "divide", 
                "say", "show", "display", "create", "draw", "whenever", "wait", "delay",
                "allocate", "free", "read", "write", "execute", "memory", "file", 
                "button", "label", "slider", "checkbox", "chart", "image", "radio",
                "add", "set", "if", "while", "end", "call"
            };
            int num_sug = 36;
            const char* match = 0;
            int found = 0;
            for(int si=0; si<num_sug; si++) {
                int match_len = 0;
                while(word[match_len] && suggestions[si][match_len] && word[match_len] == suggestions[si][match_len]) match_len++;
                if (match_len == w_len && suggestions[si][match_len] != 0) {
                    match = suggestions[si];
                    found++;
                    break;
                }
            }
            if (match) {
                int cursor_col = col - ide_scroll_x;
                int cursor_line = line - ide_scroll_y;
                if (cursor_line >= 0 && cursor_line < ide_visible_lines() && cursor_col >= 0 && cursor_col < visible_cols) {
                    int px = ide_text_area.x + cursor_col * ide_col_width();
                    int py = ide_text_area.y + (cursor_line + 1) * ide_line_height();
                    fb_rect(px, py, 100, 20, 0xFFFFDD);
                    outline(px, py, 100, 20, 0xAAAA44);
                    font_text_fit(px + 4, py + 4, match, 0x000000, 92);
                }
            }
        }
    }
}

static int point_in_rect_local(int x, int y, rect_t r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void ide_open_file(const char *path);
void ide_open_in_new_tab(const char *path) {
    int free_tab = -1;
    for (int ti=0; ti<4; ti++) {
        if (!ide_tabs[ti].text[0] && !ide_tabs[ti].filename[0]) {
            free_tab = ti; break;
        }
    }
    if (free_tab >= 0) {
        ide_active_tab = free_tab;
    }
    ide_open_file(path);
}

void ide_open_file(const char *path) {
    ide_snapshot_undo();
    int i = 0;
    int bytes = fs_read_at(path, ide_text, 32767);
    if (bytes >= 0) {
        ide_text[bytes] = 0;
        ide_length = bytes;
        ide_cursor = 0;
        ide_saved = 1;
        ide_scroll_y = 0;
        ide_scroll_x = 0;
        ide_clear_selection();
        while (path[i] && i < 255) { ide_filename[i] = path[i]; i++; }
        ide_filename[i] = 0;
    }
}

void handle_ide_click(app_window_t *app, int mx, int my) {
    (void)app;
    
    if (point_in_rect_local(mx, my, btn_new)) {
        void ide_new_file(void);
        ide_new_file();
        return;
    }
    if (point_in_rect_local(mx, my, btn_save)) {
        file_dialog_show(APP_IDE, FILE_DIALOG_SAVE, "script.fl", "/Desktop");
        return;
    }
    if (point_in_rect_local(mx, my, btn_open)) {
        file_dialog_show(APP_IDE, FILE_DIALOG_OPEN, "", "/Desktop");
        return;
    }
    if (point_in_rect_local(mx, my, btn_zoom_out)) {
        if (ide_font_scale > 1) ide_font_scale--;
        return;
    }
    if (point_in_rect_local(mx, my, btn_zoom_in)) {
        if (ide_font_scale < 4) ide_font_scale++;
        return;
    }
    if (point_in_rect_local(mx, my, btn_color)) {
        if (ide_text_color == 0x2A2A2A) ide_text_color = 0x2980B9;
        else if (ide_text_color == 0x2980B9) ide_text_color = 0x27AE60;
        else if (ide_text_color == 0x27AE60) ide_text_color = 0xC0392B;
        else if (ide_text_color == 0xC0392B) ide_text_color = 0x2A2A2A;
        return;
    }

    if (mx >= ide_text_area.x && mx < ide_text_area.x + ide_text_area.w && my >= ide_text_area.y && my < ide_text_area.y + ide_text_area.h) {
        ide_cursor = ide_position_from_point(mx, my);
        ide_select_anchor = ide_cursor;
        ide_select_active = ide_cursor;
        ide_scroll_into_view();
    }
}

void handle_ide_drag(int mx, int my) {
    ide_select_active = ide_position_from_point(mx, my);
    ide_cursor = ide_select_active;
    ide_scroll_into_view();
}

void ide_undo(void) {
    int i;
    if (!ide_undo_available) return;
    for (i = 0; i <= ide_undo_length && i < 32767; i++) ide_text[i] = ide_undo_text[i];
    ide_length = ide_undo_length;
    ide_cursor = ide_undo_cursor;
    ide_saved = ide_undo_saved;
    ide_undo_available = 0;
}

void ide_new_file(void) {
    ide_snapshot_undo();
    ide_filename[0] = 0;
    ide_save_name[0] = 0;
    ide_text[0] = 0;
    ide_length = 0;
    ide_cursor = 0;
    ide_saved = 1;
    ide_scroll_y = 0;
    ide_scroll_x = 0;
    ide_clear_selection();
}

void ide_open_sample(void) {
    static const char *sample = "Opened file in Text Edit.\n\nThis is a sample loaded from the App Menu.\nUse Save to store your changes.";
    int i = 0;
    ide_snapshot_undo();
    while (sample[i] && i < 32767) { ide_text[i] = sample[i]; i++; }
    ide_text[i] = 0;
    ide_length = i;
    ide_cursor = i;
    ide_saved = 1;
    ide_scroll_y = 0;
    ide_scroll_x = 0;
    ide_clear_selection();
}

void ide_clear(void) {
    ide_new_file();
}

void ide_autocomplete(void) {
    if (ide_cursor > 0) {
        int w_start = ide_cursor - 1;
        while (w_start > 0 && ide_text[w_start-1] != ' ' && ide_text[w_start-1] != '\n') w_start--;
        int w_len = ide_cursor - w_start;
        if (w_len > 0 && w_len < 16) {
            char word[32];
            int wi = 0; for(wi=0; wi<w_len; wi++) word[wi] = ide_text[w_start + wi]; word[wi]=0;
            const char* suggestions[] = {
                "function", "class", "let", "put", "increase", "decrease", "multiply", "divide", 
                "say", "show", "display", "create", "draw", "whenever", "wait", "delay",
                "allocate", "free", "read", "write", "execute", "memory", "file", 
                "button", "label", "slider", "checkbox", "chart", "image", "radio",
                "add", "set", "if", "while", "end", "call"
            };
            int num_sug = 36;
            const char* match = 0;
            for(int si=0; si<num_sug; si++) {
                int match_len = 0;
                while(word[match_len] && suggestions[si][match_len] && word[match_len] == suggestions[si][match_len]) match_len++;
                if (match_len == w_len && suggestions[si][match_len] != 0) {
                    match = suggestions[si];
                    break;
                }
            }
            if (match) {
                // Insert the rest of the word
                int rest_len = 0;
                while(match[w_len + rest_len]) {
                    ide_insert_char(match[w_len + rest_len]);
                    rest_len++;
                }
                ide_insert_char(' ');
            }
        }
    }
}
