#ifndef TEXT_EDIT_H
#define TEXT_EDIT_H

#include "gui_internal.h"

/* Drawing & Click Handling */
void draw_text_edit_contents(app_window_t *app);
void handle_text_edit_click(app_window_t *app, int mx, int my);
void handle_text_edit_drag(int mx, int my);

/* Clipboard & Selection */
void text_edit_copy_selection(void);
void text_edit_cut_selection(void);
void text_edit_paste_clipboard(void);
void text_edit_select_all(void);
int  text_edit_has_selection(void);
int  text_edit_selection_start(void);
int  text_edit_selection_end(void);
void text_edit_clear_selection(void);

/* Navigation & Metrics */
void text_edit_scroll_lines(int delta);
void text_edit_scroll_cols(int delta);
int  text_edit_visible_cols(void);
int  text_edit_longest_line_cols(void);
int  text_edit_position_from_point(int mx, int my);
void text_edit_cursor_up(void);
void text_edit_cursor_down(void);
void text_edit_cursor_left(void);
void text_edit_cursor_right(void);

/* Editing Operations */
void text_edit_backspace(void);
void text_edit_insert_char(char c);
void text_edit_insert_tab(void);
void text_edit_delete(void);
void text_edit_undo(void);
void text_edit_mark_saved(void);

/* File Operations */
void text_edit_new_file(void);
void text_edit_open_sample(void);
void text_edit_clear(void);
void text_edit_open_file(const char *path);

/* Find Bar Helpers */
void text_edit_find_insert_char(char c);
void text_edit_find_backspace(void);
int  text_edit_is_find_focused(void);

/* Formatting & Styling */
void text_edit_toggle_bold(void);
void text_edit_toggle_italic(void);
void text_edit_toggle_underline(void);
void text_edit_cycle_color(void);
void text_edit_set_align(int align);
int  text_edit_get_align(void);
void text_edit_set_page_format(int format);
int  text_edit_get_page_format(void);

/* Lists & Rich Content */
void text_edit_insert_bullet_list(void);
void text_edit_insert_number_list(void);
void text_edit_insert_image(void);
void text_edit_insert_date(void);
void text_edit_insert_time(void);
void text_edit_insert_shape(void);
void text_edit_insert_chart(void);

/* Smart Selection */
void text_edit_select_word_at(int pos);
void text_edit_select_paragraph_at(int pos);
void text_edit_select_capital(void);

#endif
