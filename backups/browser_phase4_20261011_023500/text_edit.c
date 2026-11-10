#include "text_edit.h"
#include "framebuffer.h"
#include "rtc.h"
#include "file_dialog.h"

extern void fb_rect(int x, int y, int w, int h, u32 color);
extern void outline(int x, int y, int w, int h, u32 color);
extern void draw_label(int x, int y, const char *text, u32 color);
extern void font_text(int x, int y, const char *text, u32 color, int scale);
extern void font_text_rich(int x, int y, const char *text, u32 color, int scale, int bold, int italic, int underline);
extern void draw_scrollbar(int x, int y, int h, int total, int visible, int scroll);
extern void open_app(int id);

void text_edit_clear_selection(void) {
    editor_select_anchor = -1;
    editor_select_active = -1;
}

int text_edit_has_selection(void) {
    return editor_select_anchor >= 0 && editor_select_active >= 0 && editor_select_anchor != editor_select_active;
}

int text_edit_selection_start(void) {
    return editor_select_anchor < editor_select_active ? editor_select_anchor : editor_select_active;
}

int text_edit_selection_end(void) {
    return editor_select_anchor > editor_select_active ? editor_select_anchor : editor_select_active;
}

extern char editor_text[32768];
extern char editor_style[32768];
extern int editor_current_style;
extern int editor_length;
extern int editor_cursor;
extern int editor_saved;
extern int editor_scroll_y;
extern int editor_scroll_x;
extern int editor_select_anchor;
extern int editor_select_active;
extern char editor_filename[32];
extern char editor_clipboard[512];
extern char editor_save_name[32];
extern rect_t editor_text_area;

extern int fs_read_at(const char *path, char *buf, u32 size);
extern int editor_save_dialog_open;
extern int explorer_picker_mode;

/* ── undo ── */
static char editor_undo_text[32768];
static int  editor_undo_length;
static int  editor_undo_cursor;
static int  editor_undo_saved;
static int  editor_undo_available;

/* ── formatting & view modes ── */
#define ALIGN_LEFT 0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT 2

#define PAGE_FORMAT_FULL 0
#define PAGE_FORMAT_A4 1
#define PAGE_FORMAT_A5 2
#define PAGE_FORMAT_A6 3
#define PAGE_FORMAT_LETTER 4

static int  editor_font_scale   = 1;
static u32  editor_text_color   = 0x2A2A2A;
static int  editor_align        = ALIGN_LEFT;
static int  editor_page_format  = PAGE_FORMAT_FULL;

static const u32 editor_palette[] = {
    0x2A2A2A, /* Charcoal / Default */
    0x0D47A1, /* Royal Navy */
    0x1B5E20, /* Forest Green */
    0xB71C1C, /* Crimson Red */
    0x6A1B9A, /* Deep Purple */
    0xE65100  /* Warm Amber */
};
#define EDITOR_PALETTE_COUNT 6
static int editor_palette_idx = 0;

/* ── click & timing metrics ── */
static int  editor_ticks            = 0;
static int  editor_last_click_ticks = 0;
static int  editor_last_click_pos   = -1;
static int  editor_click_count      = 0;

/* ── find bar ── */
static int  editor_show_find_bar  = 0;
static char editor_find_text[64]  = {0};
static int  editor_find_focused   = 0;

/* ── toolbar button rects ── */
static rect_t btn_new;
static rect_t btn_open;
static rect_t btn_save;
static rect_t btn_bold;
static rect_t btn_italic;
static rect_t btn_underline;
static rect_t btn_zoom_out;
static rect_t btn_zoom_in;
static rect_t btn_align_l;
static rect_t btn_align_c;
static rect_t btn_align_r;
static rect_t btn_color;
static rect_t btn_bullet_list;
static rect_t btn_number_list;
static rect_t btn_find;

/* ── find-bar button rects ── */
static rect_t fnd_box;
static rect_t fnd_prev;
static rect_t fnd_next;
static rect_t fnd_close;

/* ─────────────────────────── helpers ─────────────────────────── */

static void editor_snapshot_undo(void) {
    int i;
    for (i = 0; i < editor_length && i < (int)sizeof(editor_undo_text) - 1; i++)
        editor_undo_text[i] = editor_text[i];
    editor_undo_text[i] = 0;
    editor_undo_length    = editor_length;
    editor_undo_cursor    = editor_cursor;
    editor_undo_saved     = editor_saved;
    editor_undo_available = 1;
}

static int editor_line_height(void) { return 8 * editor_font_scale + 6; }
static int editor_col_width(void)   { return 6 * editor_font_scale; }

static int editor_visible_cols_internal(void) {
    int cols = editor_text_area.w / editor_col_width();
    return cols < 1 ? 1 : cols;
}

int text_edit_visible_cols(void) { return editor_visible_cols_internal(); }

int text_edit_longest_line_cols(void) {
    int i, col = 0, max_col = 0;
    for (i = 0; i < editor_length; i++) {
        if (editor_text[i] == '\n') { if (col > max_col) max_col = col; col = 0; }
        else col++;
    }
    if (col > max_col) max_col = col;
    return max_col;
}

static int editor_visible_lines(void) {
    int lines = editor_text_area.h / editor_line_height();
    return lines < 1 ? 1 : lines;
}

static int line_length_of_line_index(int target_line) {
    int line = 0, len = 0, i;
    for (i = 0; i < editor_length; i++) {
        if (editor_text[i] == '\n') {
            if (line == target_line) return len;
            line++;
            len = 0;
        } else {
            len++;
        }
    }
    if (line == target_line) return len;
    return 0;
}

static int line_col_offset(int line_idx, int visible_cols) {
    if (editor_align == ALIGN_LEFT) return 0;
    int len = line_length_of_line_index(line_idx);
    if (visible_cols <= len) return 0;
    if (editor_align == ALIGN_CENTER) return (visible_cols - len) / 2;
    if (editor_align == ALIGN_RIGHT) return (visible_cols - len);
    return 0;
}

int text_edit_position_from_point(int x, int y) {
    int line = (y - editor_text_area.y) / editor_line_height();
    int col  = (x - editor_text_area.x) / editor_col_width();
    int current_line = 0, current_col = 0, i;
    line += editor_scroll_y;
    col  += editor_scroll_x;
    if (line < 0) line = 0;
    int offset = line_col_offset(line, text_edit_visible_cols());
    col -= offset;
    if (col  < 0) col  = 0;
    for (i = 0; i < editor_length; i++) {
        if (current_line == line && current_col >= col) return i;
        if (editor_text[i] == '\n') { if (current_line == line) return i; current_line++; current_col = 0; }
        else current_col++;
    }
    return editor_length;
}

static int editor_total_lines(void) {
    int i, lines = 1;
    for (i = 0; i < editor_length; i++) if (editor_text[i] == '\n') lines++;
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
    int i, line = 0, col = 0;
    for (i = 0; i < editor_cursor && i < editor_length; i++) {
        if (editor_text[i] == '\n') { line++; col = 0; } else col++;
    }
    if (line_out) *line_out = line;
    if (col_out)  *col_out  = col;
}

static void editor_scroll_into_view(void) {
    int line, col;
    editor_cursor_metrics(&line, &col);
    if (line < editor_scroll_y) editor_scroll_y = line;
    if (line >= editor_scroll_y + editor_visible_lines()) editor_scroll_y = line - editor_visible_lines() + 1;
    if (col  < editor_scroll_x) editor_scroll_x = col;
    if (col  >= editor_scroll_x + text_edit_visible_cols()) editor_scroll_x = col - text_edit_visible_cols() + 1;
    editor_clamp_scroll();
    editor_clamp_scroll_x();
}

static int editor_position_for_line_col(int target_line, int target_col) {
    int pos, line = 0, col = 0;
    if (target_line < 0) target_line = 0;
    if (target_col  < 0) target_col  = 0;
    for (pos = 0; pos < editor_length; pos++) {
        if (line == target_line && col >= target_col) return pos;
        if (editor_text[pos] == '\n') { if (line == target_line) return pos; line++; col = 0; }
        else col++;
    }
    return editor_length;
}

/* ── string helpers ── */
static int str_len(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void int_to_str(int n, char *buf, int max) {
    char tmp[16]; int i = 0, j = 0;
    if (n == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (n > 0 && i < 15) { tmp[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0 && j < max - 1) buf[j++] = tmp[--i];
    buf[j] = 0;
}

static void str_cat(char *dst, const char *src, int max) {
    int d = str_len(dst);
    int i = 0;
    while (src[i] && d + i < max - 1) { dst[d + i] = src[i]; i++; }
    dst[d + i] = 0;
}

static int str_find(const char *haystack, int hlen, const char *needle, int nlen, int start) {
    int i, j;
    if (nlen == 0) return -1;
    for (i = start; i <= hlen - nlen; i++) {
        int ok = 1;
        for (j = 0; j < nlen; j++) { if (haystack[i + j] != needle[j]) { ok = 0; break; } }
        if (ok) return i;
    }
    return -1;
}

static int str_find_backward(const char *haystack, int hlen, const char *needle, int nlen, int before) {
    int i, j;
    if (nlen == 0) return -1;
    for (i = before - 1; i >= 0; i--) {
        if (i + nlen > hlen) continue;
        int ok = 1;
        for (j = 0; j < nlen; j++) { if (haystack[i + j] != needle[j]) { ok = 0; break; } }
        if (ok) return i;
    }
    return -1;
}

static const char *scale_to_pt(int s) {
    if (s == 1) return "8pt";
    if (s == 2) return "12pt";
    if (s == 3) return "16pt";
    return "24pt";
}

/* ── cursor movement ── */
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

void text_edit_scroll_cols(int delta)  { editor_scroll_x += delta; editor_clamp_scroll_x(); }
void text_edit_scroll_lines(int delta) { editor_scroll_y += delta; editor_clamp_scroll();   }

/* ── edit ops ── */
void text_edit_delete_selection(void) {
    int start, end, i;
    editor_snapshot_undo();
    if (!text_edit_has_selection()) return;
    start = text_edit_selection_start();
    end   = text_edit_selection_end();
    for (i = end; i <= editor_length; i++) editor_text[start + (i - end)] = editor_text[i];
    for (i = end; i <= editor_length; i++) editor_style[start + (i - end)] = editor_style[i];
    editor_length -= (end - start);
    editor_cursor  = start;
    text_edit_clear_selection();
    editor_saved = 0;
    editor_scroll_into_view();
}

void text_edit_insert_char(char c) {
    int i;
    editor_snapshot_undo();
    if (text_edit_has_selection()) text_edit_delete_selection();
    if (editor_length >= 32767) return;
    for (i = editor_length; i >= editor_cursor; i--) {
        editor_text[i + 1]  = editor_text[i];
        editor_style[i + 1] = editor_style[i];
    }
    editor_text[editor_cursor]  = c;
    editor_style[editor_cursor] = editor_current_style;
    editor_cursor++;
    editor_length++;
    editor_saved = 0;
    editor_scroll_into_view();
}

void text_edit_insert_tab(void) {
    int i, s;
    editor_snapshot_undo();
    if (text_edit_has_selection()) text_edit_delete_selection();
    for (s = 0; s < 4; s++) {
        if (editor_length >= 32767) break;
        for (i = editor_length; i >= editor_cursor; i--) {
            editor_text[i + 1]  = editor_text[i];
            editor_style[i + 1] = editor_style[i];
        }
        editor_text[editor_cursor]  = ' ';
        editor_style[editor_cursor] = editor_current_style;
        editor_cursor++;
        editor_length++;
    }
    editor_saved = 0;
    editor_scroll_into_view();
}

void text_edit_backspace(void) {
    int i;
    editor_snapshot_undo();
    if (text_edit_has_selection()) { text_edit_delete_selection(); return; }
    if (editor_cursor <= 0 || editor_length <= 0) return;
    for (i = editor_cursor - 1; i < editor_length; i++) {
        editor_text[i]  = editor_text[i + 1];
        editor_style[i] = editor_style[i + 1];
    }
    editor_cursor--;
    editor_length--;
    editor_saved = 0;
    editor_scroll_into_view();
}

void text_edit_delete(void) {
    int i;
    if (text_edit_has_selection()) {
        text_edit_delete_selection();
        return;
    }
    if (editor_cursor < editor_length) {
        editor_snapshot_undo();
        for (i = editor_cursor; i < editor_length; i++) {
            editor_text[i]  = editor_text[i + 1];
            editor_style[i] = editor_style[i + 1];
        }
        editor_length--;
        editor_saved = 0;
        editor_scroll_into_view();
    }
}

void text_edit_copy_selection(void) {
    int start, end, len, i;
    if (!text_edit_has_selection()) return;
    start = text_edit_selection_start();
    end   = text_edit_selection_end();
    len   = end - start;
    if (len >= 511) len = 511;
    for (i = 0; i < len; i++) editor_clipboard[i] = editor_text[start + i];
    editor_clipboard[len] = 0;
}

void text_edit_cut_selection(void) {
    if (!text_edit_has_selection()) return;
    text_edit_copy_selection();
    text_edit_delete_selection();
}

void text_edit_paste_clipboard(void) {
    int i = 0;
    editor_snapshot_undo();
    if (text_edit_has_selection()) text_edit_delete_selection();
    while (editor_clipboard[i]) {
        if (editor_length >= 32767) break;
        for (int k = editor_length; k >= editor_cursor; k--) {
            editor_text[k + 1]  = editor_text[k];
            editor_style[k + 1] = editor_style[k];
        }
        editor_text[editor_cursor]  = editor_clipboard[i];
        editor_style[editor_cursor] = editor_current_style;
        editor_cursor++;
        editor_length++;
        i++;
    }
    editor_saved = 0;
    editor_scroll_into_view();
}

void text_edit_select_all(void) {
    editor_select_anchor = 0;
    editor_select_active = editor_length;
    editor_cursor = editor_length;
    editor_scroll_into_view();
}

void text_edit_mark_saved(void) { editor_saved = 1; }

/* ── find bar input ── */
void text_edit_find_insert_char(char c) {
    int len = str_len(editor_find_text);
    if (len >= 63) return;
    editor_find_text[len]     = c;
    editor_find_text[len + 1] = 0;
}

void text_edit_find_backspace(void) {
    int len = str_len(editor_find_text);
    if (len > 0) editor_find_text[len - 1] = 0;
}

int text_edit_is_find_focused(void) {
    return editor_show_find_bar && editor_find_focused;
}

/* ── find next / previous ── */
static void editor_find_next(void) {
    int nlen = str_len(editor_find_text);
    int found;
    if (nlen == 0) return;
    found = str_find(editor_text, editor_length, editor_find_text, nlen, editor_cursor + 1);
    if (found < 0) found = str_find(editor_text, editor_length, editor_find_text, nlen, 0);
    if (found >= 0) {
        editor_cursor        = found;
        editor_select_anchor = found;
        editor_select_active = found + nlen;
        editor_scroll_into_view();
    }
}

static void editor_find_prev(void) {
    int nlen = str_len(editor_find_text);
    int found;
    if (nlen == 0) return;
    found = str_find_backward(editor_text, editor_length, editor_find_text, nlen, editor_cursor);
    if (found < 0) found = str_find_backward(editor_text, editor_length, editor_find_text, nlen, editor_length);
    if (found >= 0) {
        editor_cursor        = found;
        editor_select_anchor = found;
        editor_select_active = found + nlen;
        editor_scroll_into_view();
    }
}

/* ── formatting & styles ── */
void text_edit_toggle_bold(void) {
    int s, e, k;
    editor_current_style ^= 1;
    if (text_edit_has_selection()) {
        s = text_edit_selection_start();
        e = text_edit_selection_end();
        for (k = s; k < e; k++) editor_style[k] ^= 1;
    }
}

void text_edit_toggle_italic(void) {
    int s, e, k;
    editor_current_style ^= 2;
    if (text_edit_has_selection()) {
        s = text_edit_selection_start();
        e = text_edit_selection_end();
        for (k = s; k < e; k++) editor_style[k] ^= 2;
    }
}

void text_edit_toggle_underline(void) {
    int s, e, k;
    editor_current_style ^= 4;
    if (text_edit_has_selection()) {
        s = text_edit_selection_start();
        e = text_edit_selection_end();
        for (k = s; k < e; k++) editor_style[k] ^= 4;
    }
}

void text_edit_cycle_color(void) {
    editor_palette_idx = (editor_palette_idx + 1) % EDITOR_PALETTE_COUNT;
    editor_text_color = editor_palette[editor_palette_idx];
}

void text_edit_set_align(int align) {
    if (align >= 0 && align <= 2) editor_align = align;
}

int text_edit_get_align(void) {
    return editor_align;
}

void text_edit_set_page_format(int format) {
    if (format >= 0 && format <= 4) editor_page_format = format;
}

int text_edit_get_page_format(void) {
    return editor_page_format;
}

/* ── lists & rich insertion ── */
void text_edit_insert_bullet_list(void) {
    editor_snapshot_undo();
    int start = editor_cursor;
    while (start > 0 && editor_text[start - 1] != '\n') start--;
    int old_cursor = editor_cursor;
    editor_cursor = start;
    text_edit_insert_char('*');
    text_edit_insert_char(' ');
    editor_cursor = old_cursor + 2;
    editor_scroll_into_view();
}

void text_edit_insert_number_list(void) {
    editor_snapshot_undo();
    int start = editor_cursor;
    while (start > 0 && editor_text[start - 1] != '\n') start--;
    
    int num = 1;
    if (start > 1) {
        int prev_start = start - 1;
        while (prev_start > 0 && editor_text[prev_start - 1] != '\n') prev_start--;
        if (editor_text[prev_start] >= '0' && editor_text[prev_start] <= '9') {
            num = editor_text[prev_start] - '0';
            if (editor_text[prev_start + 1] >= '0' && editor_text[prev_start + 1] <= '9') {
                num = num * 10 + (editor_text[prev_start + 1] - '0');
            }
            num++;
        }
    }
    char num_buf[16];
    int_to_str(num, num_buf, 16);
    str_cat(num_buf, ". ", 16);
    
    int old_cursor = editor_cursor;
    editor_cursor = start;
    int k = 0;
    while (num_buf[k]) {
        text_edit_insert_char(num_buf[k]);
        k++;
    }
    editor_cursor = old_cursor + k;
    editor_scroll_into_view();
}

void text_edit_insert_image(void) {
    static const char *img = "[IMAGE: photo.bmp]";
    for (int i = 0; img[i]; i++) text_edit_insert_char(img[i]);
}

void text_edit_insert_date(void) {
    rtc_time_t t;
    rtc_read(&t);
    char buf[16];
    char tmp[16];
    buf[0] = 0;
    int_to_str(t.year ? t.year : 2026, tmp, 16);
    str_cat(buf, tmp, 16);
    str_cat(buf, "-", 16);
    if (t.month < 10) str_cat(buf, "0", 16);
    int_to_str(t.month ? t.month : 9, tmp, 16);
    str_cat(buf, tmp, 16);
    str_cat(buf, "-", 16);
    if (t.day < 10) str_cat(buf, "0", 16);
    int_to_str(t.day ? t.day : 12, tmp, 16);
    str_cat(buf, tmp, 16);

    for (int i = 0; buf[i]; i++) text_edit_insert_char(buf[i]);
}

void text_edit_insert_time(void) {
    rtc_time_t t;
    rtc_read(&t);
    char buf[16];
    char tmp[16];
    buf[0] = 0;
    if (t.hour < 10) str_cat(buf, "0", 16);
    int_to_str(t.hour, tmp, 16);
    str_cat(buf, tmp, 16);
    str_cat(buf, ":", 16);
    if (t.minute < 10) str_cat(buf, "0", 16);
    int_to_str(t.minute, tmp, 16);
    str_cat(buf, tmp, 16);
    str_cat(buf, ":", 16);
    if (t.second < 10) str_cat(buf, "0", 16);
    int_to_str(t.second, tmp, 16);
    str_cat(buf, tmp, 16);

    for (int i = 0; buf[i]; i++) text_edit_insert_char(buf[i]);
}

void text_edit_insert_shape(void) {
    static const char *shape =
        "\n+------------------------------------------+\n"
        "|                 [ SHAPE ]                |\n"
        "+------------------------------------------+\n";
    for (int i = 0; shape[i]; i++) text_edit_insert_char(shape[i]);
}

void text_edit_insert_chart(void) {
    static const char *chart =
        "\n+--------------------+----------+\n"
        "| Category           | Value    |\n"
        "+--------------------+----------+\n"
        "| Product Alpha      |    125   |\n"
        "| Product Beta       |    280   |\n"
        "| Product Gamma      |    195   |\n"
        "+--------------------+----------+\n";
    for (int i = 0; chart[i]; i++) text_edit_insert_char(chart[i]);
}

/* ── smart selection ── */
static int is_word_char(char c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '_') return 1;
    return 0;
}

void text_edit_select_word_at(int pos) {
    if (pos < 0 || pos > editor_length) return;
    int start = pos;
    int end = pos;
    if (start > 0 && !is_word_char(editor_text[start]) && is_word_char(editor_text[start - 1])) {
        start--;
        end--;
    }
    while (start > 0 && is_word_char(editor_text[start - 1])) start--;
    while (end < editor_length && is_word_char(editor_text[end])) end++;
    editor_select_anchor = start;
    editor_select_active = end;
    editor_cursor = end;
}

void text_edit_select_paragraph_at(int pos) {
    if (pos < 0 || pos > editor_length) return;
    int start = pos;
    int end = pos;
    while (start > 0 && editor_text[start - 1] != '\n') start--;
    while (end < editor_length && editor_text[end] != '\n') end++;
    if (end < editor_length && editor_text[end] == '\n') end++;
    editor_select_anchor = start;
    editor_select_active = end;
    editor_cursor = end;
}

void text_edit_select_capital(void) {
    int i;
    if (text_edit_has_selection()) {
        editor_snapshot_undo();
        int s = text_edit_selection_start();
        int e = text_edit_selection_end();
        int has_lower = 0;
        for (i = s; i < e; i++) {
            if (editor_text[i] >= 'a' && editor_text[i] <= 'z') { has_lower = 1; break; }
        }
        for (i = s; i < e; i++) {
            if (has_lower) {
                if (editor_text[i] >= 'a' && editor_text[i] <= 'z') editor_text[i] -= 32;
            } else {
                if (editor_text[i] >= 'A' && editor_text[i] <= 'Z') editor_text[i] += 32;
            }
        }
        editor_saved = 0;
        return;
    }
    for (i = editor_cursor; i < editor_length; i++) {
        if (editor_text[i] >= 'A' && editor_text[i] <= 'Z') {
            text_edit_select_word_at(i);
            return;
        }
    }
    for (i = 0; i < editor_cursor; i++) {
        if (editor_text[i] >= 'A' && editor_text[i] <= 'Z') {
            text_edit_select_word_at(i);
            return;
        }
    }
}

/* ─────────────────────────── drawing ─────────────────────────── */

static int point_in_rect_local(int x, int y, rect_t r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void draw_text_edit_contents(app_window_t *app) {
    editor_ticks++;

    int x = app->frame.x + 12;
    int y = app->frame.y + 42;
    int w = app->frame.w - 24;
    int h = app->frame.h - 54;
    int i;
    int line = 0;
    int col  = 0;
    int visible_cols;

    /* ── gutter width ── */
    int gutter_w = 42;

    /* ── find bar height (0 or 24) ── */
    int fnd_h = editor_show_find_bar ? 24 : 0;

    /* ── status bar height ── */
    int status_h = 16;

    /* main background */
    fb_rect(x, y, w, h, 0xF2F4F7);

    /* ── Toolbar (32px tall) ── */
    fb_rect(x, y, w, 32, 0xEAEAEA);
    outline(x, y, w, 32, 0xC4C4C4);

    /* Toolbar Layout:
       [New 38] [Open 42] [Save 42] | [B 24] [I 24] [U 24] | [- 22] [12pt 34] [+ 22] | [L 24] [C 24] [R 24] | [Color 52] | [* 26] [1. 28] | [Find 44]
    */
    int tx = x + 8;
    int ty = y + 6;

    btn_new  = (rect_t){tx, ty, 38, 20, 0}; tx += 42;
    btn_open = (rect_t){tx, ty, 42, 20, 0}; tx += 46;
    btn_save = (rect_t){tx, ty, 42, 20, 0}; tx += 48;

    fb_rect(tx, ty + 2, 1, 16, 0xD0D0D0); tx += 8;

    btn_bold      = (rect_t){tx, ty, 24, 20, 0}; tx += 26;
    btn_italic    = (rect_t){tx, ty, 24, 20, 0}; tx += 26;
    btn_underline = (rect_t){tx, ty, 24, 20, 0}; tx += 30;

    fb_rect(tx, ty + 2, 1, 16, 0xD0D0D0); tx += 8;

    btn_zoom_out  = (rect_t){tx, ty, 22, 20, 0}; tx += 24;
    int pt_label_x = tx; tx += 34;
    btn_zoom_in   = (rect_t){tx, ty, 22, 20, 0}; tx += 28;

    fb_rect(tx, ty + 2, 1, 16, 0xD0D0D0); tx += 8;

    btn_align_l   = (rect_t){tx, ty, 24, 20, 0}; tx += 26;
    btn_align_c   = (rect_t){tx, ty, 24, 20, 0}; tx += 26;
    btn_align_r   = (rect_t){tx, ty, 24, 20, 0}; tx += 30;

    fb_rect(tx, ty + 2, 1, 16, 0xD0D0D0); tx += 8;

    btn_color     = (rect_t){tx, ty, 52, 20, 0}; tx += 58;

    fb_rect(tx, ty + 2, 1, 16, 0xD0D0D0); tx += 8;

    btn_bullet_list = (rect_t){tx, ty, 26, 20, 0}; tx += 28;
    btn_number_list = (rect_t){tx, ty, 28, 20, 0}; tx += 34;

    fb_rect(tx, ty + 2, 1, 16, 0xD0D0D0); tx += 8;

    btn_find      = (rect_t){tx, ty, 44, 20, 0};

    /* Render Buttons */
    /* New / Open / Save */
    fb_round_rect(btn_new.x, btn_new.y, btn_new.w, btn_new.h, 4, 0xFFFFFF);
    fb_round_rect_border(btn_new.x, btn_new.y, btn_new.w, btn_new.h, 4, 0xB0B8C0);
    draw_label(btn_new.x + 6, btn_new.y + 5, "New", 0x333333);

    fb_round_rect(btn_open.x, btn_open.y, btn_open.w, btn_open.h, 4, 0xFFFFFF);
    fb_round_rect_border(btn_open.x, btn_open.y, btn_open.w, btn_open.h, 4, 0xB0B8C0);
    draw_label(btn_open.x + 6, btn_open.y + 5, "Open", 0x333333);

    fb_round_rect(btn_save.x, btn_save.y, btn_save.w, btn_save.h, 4, editor_saved ? 0xFFFFFF : 0xD8EAF8);
    fb_round_rect_border(btn_save.x, btn_save.y, btn_save.w, btn_save.h, 4, editor_saved ? 0xB0B8C0 : 0x5C9CD4);
    draw_label(btn_save.x + 6, btn_save.y + 5, "Save", 0x333333);

    /* Formatting: Bold, Italic, Underline */
    fb_round_rect(btn_bold.x, btn_bold.y, btn_bold.w, btn_bold.h, 4, (editor_current_style & 1) ? 0xCCE0F4 : 0xFFFFFF);
    fb_round_rect_border(btn_bold.x, btn_bold.y, btn_bold.w, btn_bold.h, 4, (editor_current_style & 1) ? 0x3B82F6 : 0xB0B8C0);
    font_text_rich(btn_bold.x + 8, btn_bold.y + 5, "B", (editor_current_style & 1) ? 0x1D4ED8 : 0x333333, 1, 1, 0, 0);

    fb_round_rect(btn_italic.x, btn_italic.y, btn_italic.w, btn_italic.h, 4, (editor_current_style & 2) ? 0xCCE0F4 : 0xFFFFFF);
    fb_round_rect_border(btn_italic.x, btn_italic.y, btn_italic.w, btn_italic.h, 4, (editor_current_style & 2) ? 0x3B82F6 : 0xB0B8C0);
    font_text_rich(btn_italic.x + 8, btn_italic.y + 5, "I", (editor_current_style & 2) ? 0x1D4ED8 : 0x333333, 1, 0, 1, 0);

    fb_round_rect(btn_underline.x, btn_underline.y, btn_underline.w, btn_underline.h, 4, (editor_current_style & 4) ? 0xCCE0F4 : 0xFFFFFF);
    fb_round_rect_border(btn_underline.x, btn_underline.y, btn_underline.w, btn_underline.h, 4, (editor_current_style & 4) ? 0x3B82F6 : 0xB0B8C0);
    font_text_rich(btn_underline.x + 8, btn_underline.y + 5, "U", (editor_current_style & 4) ? 0x1D4ED8 : 0x333333, 1, 0, 0, 1);

    /* Font Size: [-] 12pt [+] */
    fb_round_rect(btn_zoom_out.x, btn_zoom_out.y, btn_zoom_out.w, btn_zoom_out.h, 4, 0xFFFFFF);
    fb_round_rect_border(btn_zoom_out.x, btn_zoom_out.y, btn_zoom_out.w, btn_zoom_out.h, 4, 0xB0B8C0);
    draw_label(btn_zoom_out.x + 7, btn_zoom_out.y + 5, "-", 0x333333);

    draw_label(pt_label_x + 2, ty + 5, scale_to_pt(editor_font_scale), 0x444444);

    fb_round_rect(btn_zoom_in.x, btn_zoom_in.y, btn_zoom_in.w, btn_zoom_in.h, 4, 0xFFFFFF);
    fb_round_rect_border(btn_zoom_in.x, btn_zoom_in.y, btn_zoom_in.w, btn_zoom_in.h, 4, 0xB0B8C0);
    draw_label(btn_zoom_in.x + 6, btn_zoom_in.y + 5, "+", 0x333333);

    /* Alignment: L / C / R */
    fb_round_rect(btn_align_l.x, btn_align_l.y, btn_align_l.w, btn_align_l.h, 4, (editor_align == ALIGN_LEFT) ? 0xCCE0F4 : 0xFFFFFF);
    fb_round_rect_border(btn_align_l.x, btn_align_l.y, btn_align_l.w, btn_align_l.h, 4, (editor_align == ALIGN_LEFT) ? 0x3B82F6 : 0xB0B8C0);
    draw_label(btn_align_l.x + 8, btn_align_l.y + 5, "L", (editor_align == ALIGN_LEFT) ? 0x1D4ED8 : 0x333333);

    fb_round_rect(btn_align_c.x, btn_align_c.y, btn_align_c.w, btn_align_c.h, 4, (editor_align == ALIGN_CENTER) ? 0xCCE0F4 : 0xFFFFFF);
    fb_round_rect_border(btn_align_c.x, btn_align_c.y, btn_align_c.w, btn_align_c.h, 4, (editor_align == ALIGN_CENTER) ? 0x3B82F6 : 0xB0B8C0);
    draw_label(btn_align_c.x + 8, btn_align_c.y + 5, "C", (editor_align == ALIGN_CENTER) ? 0x1D4ED8 : 0x333333);

    fb_round_rect(btn_align_r.x, btn_align_r.y, btn_align_r.w, btn_align_r.h, 4, (editor_align == ALIGN_RIGHT) ? 0xCCE0F4 : 0xFFFFFF);
    fb_round_rect_border(btn_align_r.x, btn_align_r.y, btn_align_r.w, btn_align_r.h, 4, (editor_align == ALIGN_RIGHT) ? 0x3B82F6 : 0xB0B8C0);
    draw_label(btn_align_r.x + 8, btn_align_r.y + 5, "R", (editor_align == ALIGN_RIGHT) ? 0x1D4ED8 : 0x333333);

    /* Color button with color swatch */
    fb_round_rect(btn_color.x, btn_color.y, btn_color.w, btn_color.h, 4, 0xFFFFFF);
    fb_round_rect_border(btn_color.x, btn_color.y, btn_color.w, btn_color.h, 4, 0xB0B8C0);
    fb_round_rect(btn_color.x + 4, btn_color.y + 4, 10, 12, 2, editor_text_color);
    fb_round_rect_border(btn_color.x + 4, btn_color.y + 4, 10, 12, 2, 0x888888);
    draw_label(btn_color.x + 18, btn_color.y + 5, "Color", 0x333333);

    /* Lists: Bullet and Numbered */
    fb_round_rect(btn_bullet_list.x, btn_bullet_list.y, btn_bullet_list.w, btn_bullet_list.h, 4, 0xFFFFFF);
    fb_round_rect_border(btn_bullet_list.x, btn_bullet_list.y, btn_bullet_list.w, btn_bullet_list.h, 4, 0xB0B8C0);
    draw_label(btn_bullet_list.x + 8, btn_bullet_list.y + 5, "*", 0x333333);

    fb_round_rect(btn_number_list.x, btn_number_list.y, btn_number_list.w, btn_number_list.h, 4, 0xFFFFFF);
    fb_round_rect_border(btn_number_list.x, btn_number_list.y, btn_number_list.w, btn_number_list.h, 4, 0xB0B8C0);
    draw_label(btn_number_list.x + 6, btn_number_list.y + 5, "1.", 0x333333);

    /* Find button */
    fb_round_rect(btn_find.x, btn_find.y, btn_find.w, btn_find.h, 4, editor_show_find_bar ? 0xDDEEFF : 0xFFFFFF);
    fb_round_rect_border(btn_find.x, btn_find.y, btn_find.w, btn_find.h, 4, editor_show_find_bar ? 0x5599CC : 0xB0B8C0);
    draw_label(btn_find.x + 6, btn_find.y + 5, "Find", 0x333333);

    /* ── Find Bar (y+32 .. y+32+24) ── */
    if (editor_show_find_bar) {
        int fy = y + 32;
        fb_rect(x, fy, w, 24, 0xE0E8F0);
        outline(x, fy, w, 24, 0xB0BFC8);

        draw_label(x + 6, fy + 6, "Find:", 0x444444);

        fnd_box   = (rect_t){x + 42,     fy + 4, w - 130, 16, 0};
        fnd_prev  = (rect_t){x + w - 82, fy + 4, 22, 16, 0};
        fnd_next  = (rect_t){x + w - 56, fy + 4, 22, 16, 0};
        fnd_close = (rect_t){x + w - 28, fy + 4, 22, 16, 0};

        fb_round_rect(fnd_box.x, fnd_box.y, fnd_box.w, fnd_box.h, 4, 0xFFFFFF);
        fb_round_rect_border(fnd_box.x, fnd_box.y, fnd_box.w, fnd_box.h, 4, editor_find_focused ? 0x5599CC : 0xAAAAAA);
        {
            char disp[65];
            int flen = str_len(editor_find_text);
            int max_chars = (fnd_box.w - 4) / 6;
            int offset = flen > max_chars ? flen - max_chars : 0;
            int di = 0;
            while (editor_find_text[offset + di] && di < 64) { disp[di] = editor_find_text[offset + di]; di++; }
            disp[di] = 0;
            draw_label(fnd_box.x + 3, fnd_box.y + 3, disp, 0x222222);
        }

        fb_round_rect(fnd_prev.x, fnd_prev.y, fnd_prev.w, fnd_prev.h, 4, 0xFFFFFF);
        fb_round_rect_border(fnd_prev.x, fnd_prev.y, fnd_prev.w, fnd_prev.h, 4, 0xAAAAAA);
        draw_label(fnd_prev.x + 5, fnd_prev.y + 3, "^", 0x333333);

        fb_round_rect(fnd_next.x, fnd_next.y, fnd_next.w, fnd_next.h, 4, 0xFFFFFF);
        fb_round_rect_border(fnd_next.x, fnd_next.y, fnd_next.w, fnd_next.h, 4, 0xAAAAAA);
        draw_label(fnd_next.x + 5, fnd_next.y + 3, "v", 0x333333);

        fb_round_rect(fnd_close.x, fnd_close.y, fnd_close.w, fnd_close.h, 4, 0xFFE0E0);
        fb_round_rect_border(fnd_close.x, fnd_close.y, fnd_close.w, fnd_close.h, 4, 0xCC8888);
        draw_label(fnd_close.x + 5, fnd_close.y + 3, "X", 0xAA3333);
    }

    /* ── Text Area & Paper Sheet Formatting ── */
    {
        int ta_x = x + 8;
        int ta_y = y + 32 + fnd_h + 8;
        int ta_w = w - 16;
        int ta_h = h - 32 - fnd_h - 8 - status_h - 6;

        int sheet_w = ta_w;
        if (editor_page_format == PAGE_FORMAT_A4) sheet_w = 460;
        else if (editor_page_format == PAGE_FORMAT_A5) sheet_w = 340;
        else if (editor_page_format == PAGE_FORMAT_A6) sheet_w = 240;
        else if (editor_page_format == PAGE_FORMAT_LETTER) sheet_w = 480;

        if (editor_page_format != PAGE_FORMAT_FULL && ta_w > sheet_w + 24) {
            /* Desk / Canvas Area */
            fb_rect(ta_x, ta_y, ta_w, ta_h, 0xDEE2E8);
            outline(ta_x, ta_y, ta_w, ta_h, 0xC4CAD2);

            int sheet_x = ta_x + (ta_w - sheet_w) / 2;
            /* Shadow */
            fb_rect(sheet_x + 3, ta_y + 3, sheet_w, ta_h, 0xB4BAC2);
            /* Sheet */
            fb_rect(sheet_x, ta_y, sheet_w, ta_h, 0xFFFFFF);
            outline(sheet_x, ta_y, sheet_w, ta_h, 0x909CA8);

            /* Gutter */
            fb_rect(sheet_x, ta_y, gutter_w, ta_h, 0xF4F4F4);
            fb_rect(sheet_x + gutter_w - 1, ta_y, 1, ta_h, 0xD0D0D0);

            editor_text_area.x = sheet_x + gutter_w + 4;
            editor_text_area.y = ta_y + 4;
            editor_text_area.w = sheet_w - gutter_w - 12;
            editor_text_area.h = ta_h - 8;
        } else {
            /* Full Width Mode */
            fb_rect(ta_x, ta_y, ta_w, ta_h, 0xFFFFFF);
            outline(ta_x, ta_y, ta_w, ta_h, 0xD8C9B7);

            fb_rect(ta_x, ta_y, gutter_w, ta_h, 0xF0F0F0);
            fb_rect(ta_x + gutter_w - 1, ta_y, 1, ta_h, 0xCCCCCC);

            editor_text_area.x = ta_x + gutter_w + 4;
            editor_text_area.y = ta_y + 4;
            editor_text_area.w = ta_w - gutter_w - 20;
            editor_text_area.h = ta_h - 8;
        }
        editor_text_area.color = 0;
    }

    visible_cols = text_edit_visible_cols();
    editor_clamp_scroll();
    editor_clamp_scroll_x();

    /* draw text + gutter line numbers */
    {
        int ta_x = editor_text_area.x - gutter_w - 4;
        int ta_y = editor_text_area.y;
        int vis  = editor_visible_lines();

        /* Line numbers in gutter */
        for (int ln = 0; ln < vis; ln++) {
            int doc_line = editor_scroll_y + ln;
            int line_y   = ta_y + ln * editor_line_height();
            char lnbuf[8];
            int_to_str(doc_line + 1, lnbuf, 8);
            font_text(ta_x + 4, line_y, lnbuf, 0x999999, editor_font_scale);
        }

        /* Characters */
        for (i = 0; i < editor_length; i++) {
            char c = editor_text[i];
            int selected = text_edit_has_selection() && i >= text_edit_selection_start() && i < text_edit_selection_end();
            int draw_line = line - editor_scroll_y;
            int offset = line_col_offset(line, visible_cols);
            int draw_col  = col - editor_scroll_x + offset;

            if (selected && draw_line >= 0 && draw_line < editor_visible_lines() && draw_col >= 0 && draw_col < visible_cols) {
                fb_rect(editor_text_area.x + draw_col * editor_col_width() - 1,
                        editor_text_area.y + draw_line * editor_line_height() - 1,
                        editor_col_width() + 1, editor_line_height() - 1, 0xCFE1F7);
            }
            if (c == '\n') { line++; col = 0; continue; }
            if (draw_line >= 0 && draw_line < editor_visible_lines() && draw_col >= 0 && draw_col < visible_cols) {
                char one[2];
                int  style     = editor_style[i];
                int  bold      = style & 1;
                int  italic    = (style >> 1) & 1;
                int  underline = (style >> 2) & 1;
                one[0] = c; one[1] = 0;
                font_text_rich(editor_text_area.x + draw_col * editor_col_width(),
                               editor_text_area.y + draw_line * editor_line_height(),
                               one, editor_text_color, editor_font_scale, bold, italic, underline);
            }
            col++;
        }
    }

    /* cursor */
    {
        int cur_line, cur_col;
        editor_cursor_metrics(&cur_line, &cur_col);
        {
            int dl = cur_line - editor_scroll_y;
            int offset = line_col_offset(cur_line, visible_cols);
            int dc = cur_col - editor_scroll_x + offset;
            if (dl >= 0 && dl < editor_visible_lines() && dc >= 0 && dc <= visible_cols) {
                fb_rect(editor_text_area.x + dc * editor_col_width(),
                        editor_text_area.y + dl * editor_line_height(),
                        2, editor_line_height() - 2, 0x1D4ED8);
            }
        }
    }

    /* vertical scrollbar */
    draw_scrollbar(app->frame.x + app->frame.w - 18, editor_text_area.y,
                   editor_text_area.h, editor_total_lines(), editor_visible_lines(), editor_scroll_y);

    /* horizontal scrollbar */
    if (text_edit_longest_line_cols() > text_edit_visible_cols()) {
        int sx    = editor_text_area.x;
        int sy    = editor_text_area.y + editor_text_area.h + 2;
        int sw    = editor_text_area.w;
        int track = sw - 28;
        int max_scroll = text_edit_longest_line_cols() - text_edit_visible_cols();
        int thumb = (text_edit_visible_cols() * track) / text_edit_longest_line_cols();
        int thumb_x;
        if (thumb < 12) thumb = 12;
        if (thumb > track) thumb = track;
        fb_rect(sx, sy, sw, 10, 0xC8D1DC);
        outline(sx, sy, sw, 10, 0x77899B);
        fb_rect(sx + 2, sy + 1, 10, 8, 0xA8B9CC);
        fb_rect(sx + sw - 12, sy + 1, 10, 8, 0xA8B9CC);
        draw_label(sx + 4, sy + 1, "<", 0x20344E);
        draw_label(sx + sw - 10, sy + 1, ">", 0x20344E);
        fb_rect(sx + 14, sy + 1, track, 8, 0xDCE4EE);
        thumb_x = sx + 14 + (max_scroll ? (editor_scroll_x * (track - thumb)) / max_scroll : 0);
        fb_rect(thumb_x, sy + 1, thumb, 8, 0x8EA4BA);
        outline(thumb_x, sy + 1, thumb, 8, 0x52677F);
    }

    /* ── Status Bar ── */
    {
        int sb_y = app->frame.y + 42 + h - status_h - 2;
        int cur_line, cur_col;
        char status[128];
        char tmp[16];

        editor_cursor_metrics(&cur_line, &cur_col);
        status[0] = 0;
        str_cat(status, "Line: ", 128);
        int_to_str(cur_line + 1, tmp, 16); str_cat(status, tmp, 128);
        str_cat(status, "  Col: ", 128);
        int_to_str(cur_col + 1, tmp, 16); str_cat(status, tmp, 128);
        str_cat(status, "  |  Chars: ", 128);
        int_to_str(editor_length, tmp, 16); str_cat(status, tmp, 128);

        str_cat(status, "  |  ", 128);
        if (editor_align == ALIGN_CENTER) str_cat(status, "Align: Center", 128);
        else if (editor_align == ALIGN_RIGHT) str_cat(status, "Align: Right (RTL)", 128);
        else str_cat(status, "Align: Left (LTR)", 128);

        str_cat(status, "  |  ", 128);
        if (editor_page_format == PAGE_FORMAT_A4) str_cat(status, "A4", 128);
        else if (editor_page_format == PAGE_FORMAT_A5) str_cat(status, "A5", 128);
        else if (editor_page_format == PAGE_FORMAT_A6) str_cat(status, "A6", 128);
        else if (editor_page_format == PAGE_FORMAT_LETTER) str_cat(status, "Letter", 128);
        else str_cat(status, "Full View", 128);

        str_cat(status, "  |  ", 128);
        str_cat(status, editor_saved ? "SAVED" : "EDITED", 128);

        fb_rect(x, sb_y, w, status_h, 0xE6E8EB);
        outline(x, sb_y, w, status_h, 0xC4C8CE);
        draw_label(x + 8, sb_y + 3, status, 0x555555);
    }
}

/* ─────────────────────────── click handler ─────────────────────────── */

void text_edit_open_file(const char *path) {
    int i = 0;
    editor_snapshot_undo();
    int bytes = fs_read_at(path, editor_text, 32767);
    if (bytes >= 0) {
        editor_text[bytes] = 0;
        editor_length      = bytes;
        editor_cursor      = 0;
        editor_saved       = 1;
        editor_scroll_y    = 0;
        editor_scroll_x    = 0;
        text_edit_clear_selection();
        while (path[i] && i < 255) { editor_filename[i] = path[i]; i++; }
        editor_filename[i] = 0;
    }
}

void handle_text_edit_click(app_window_t *app, int mx, int my) {
    (void)app;

    /* ── find bar buttons ── */
    if (editor_show_find_bar) {
        if (point_in_rect_local(mx, my, fnd_close)) {
            editor_show_find_bar = 0;
            editor_find_text[0]  = 0;
            editor_find_focused  = 0;
            return;
        }
        if (point_in_rect_local(mx, my, fnd_next)) { editor_find_next(); return; }
        if (point_in_rect_local(mx, my, fnd_prev)) { editor_find_prev(); return; }
        if (point_in_rect_local(mx, my, fnd_box))  { editor_find_focused = 1; return; }
        if (point_in_rect_local(mx, my, editor_text_area)) { editor_find_focused = 0; }
    }

    /* ── toolbar buttons ── */
    if (point_in_rect_local(mx, my, btn_find)) {
        editor_show_find_bar ^= 1;
        editor_find_focused   = 0;
        return;
    }
    if (point_in_rect_local(mx, my, btn_new)) {
        text_edit_new_file();
        return;
    }
    if (point_in_rect_local(mx, my, btn_save)) {
        file_dialog_show(APP_TEXT_EDIT, FILE_DIALOG_SAVE, "untitled.txt", "/Desktop");
        return;
    }
    if (point_in_rect_local(mx, my, btn_open)) {
        file_dialog_show(APP_TEXT_EDIT, FILE_DIALOG_OPEN, "", "/Desktop");
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
    if (point_in_rect_local(mx, my, btn_bold)) {
        text_edit_toggle_bold();
        return;
    }
    if (point_in_rect_local(mx, my, btn_italic)) {
        text_edit_toggle_italic();
        return;
    }
    if (point_in_rect_local(mx, my, btn_underline)) {
        text_edit_toggle_underline();
        return;
    }
    if (point_in_rect_local(mx, my, btn_align_l)) {
        editor_align = ALIGN_LEFT;
        return;
    }
    if (point_in_rect_local(mx, my, btn_align_c)) {
        editor_align = ALIGN_CENTER;
        return;
    }
    if (point_in_rect_local(mx, my, btn_align_r)) {
        editor_align = ALIGN_RIGHT;
        return;
    }
    if (point_in_rect_local(mx, my, btn_color)) {
        text_edit_cycle_color();
        return;
    }
    if (point_in_rect_local(mx, my, btn_bullet_list)) {
        text_edit_insert_bullet_list();
        return;
    }
    if (point_in_rect_local(mx, my, btn_number_list)) {
        text_edit_insert_number_list();
        return;
    }

    /* ── text area click with double/triple click detection ── */
    if (mx >= editor_text_area.x && mx < editor_text_area.x + editor_text_area.w &&
        my >= editor_text_area.y && my < editor_text_area.y + editor_text_area.h) {
        editor_find_focused = 0;
        int pos = text_edit_position_from_point(mx, my);
        if (editor_ticks - editor_last_click_ticks < 22 && pos == editor_last_click_pos) {
            editor_click_count++;
        } else {
            editor_click_count = 1;
        }
        editor_last_click_ticks = editor_ticks;
        editor_last_click_pos   = pos;

        if (editor_click_count == 2) {
            text_edit_select_word_at(pos);
        } else if (editor_click_count >= 3) {
            text_edit_select_paragraph_at(pos);
        } else {
            editor_cursor        = pos;
            editor_select_anchor = pos;
            editor_select_active = pos;
        }
        editor_scroll_into_view();
    }
}

void handle_text_edit_drag(int mx, int my) {
    editor_select_active = text_edit_position_from_point(mx, my);
    editor_cursor        = editor_select_active;
    editor_scroll_into_view();
}

/* ─────────────────────────── undo ─────────────────────────── */

void text_edit_undo(void) {
    int i;
    if (!editor_undo_available) return;
    for (i = 0; i <= editor_undo_length && i < 32767; i++) editor_text[i] = editor_undo_text[i];
    editor_length        = editor_undo_length;
    editor_cursor        = editor_undo_cursor;
    editor_saved         = editor_undo_saved;
    editor_undo_available = 0;
}

/* ─────────────────────────── file ops ─────────────────────────── */

void text_edit_new_file(void) {
    editor_snapshot_undo();
    editor_filename[0]  = 0;
    editor_save_name[0] = 0;
    editor_text[0]      = 0;
    editor_length       = 0;
    editor_cursor       = 0;
    editor_saved        = 1;
    editor_scroll_y     = 0;
    editor_scroll_x     = 0;
    text_edit_clear_selection();
}

void text_edit_open_sample(void) {
    static const char *sample = "Welcome to Text Edit!\n\nUse the formatting toolbar above to customize text styles:\n* Bold, Italic, and Underline\n* Font sizes (8pt to 24pt)\n* Text alignment (Left, Center, Right)\n* Color palette and Lists\n* Paper formats (A4, A5, A6, Letter) in View menu.";
    int i = 0;
    editor_snapshot_undo();
    while (sample[i] && i < 32767) { editor_text[i] = sample[i]; i++; }
    editor_text[i]  = 0;
    editor_length   = i;
    editor_cursor   = i;
    editor_saved    = 1;
    editor_scroll_y = 0;
    editor_scroll_x = 0;
    text_edit_clear_selection();
}

void text_edit_clear(void) { text_edit_new_file(); }
