#ifndef GUI_WIDGETS_H
#define GUI_WIDGETS_H

#include "framebuffer.h"
#include "font.h"
#include "theme.h"
#include "gui_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Button state tracking */
extern rect_t pressed_button;
extern int pressed_button_active;

/* Tooltip state tracking */
extern int widget_tooltip_visible;
extern int widget_tooltip_x;
extern int widget_tooltip_y;
extern char widget_tooltip_text[96];

/* Dialog state tracking */
extern int widget_dialog_visible;
extern char widget_dialog_title[48];
extern char widget_dialog_message[128];
extern char widget_dialog_primary[32];
extern char widget_dialog_secondary[32];

/* Progress bar state tracking */
extern int widget_progress_visible;
extern int widget_progress_value;
extern int widget_progress_min;
extern int widget_progress_max;

/* Core Drawing Primitives */
int  point_in_rect(int px, int py, rect_t r);
void outline(int x, int y, int w, int h, u32 color);
void draw_soft_box(rect_t r, u32 base, u32 hi, u32 lo);
void draw_label(int x, int y, const char *text, u32 color);

/* Standard Controls & Widgets */
void draw_button(rect_t r, u32 base, u32 hi, const char *text, u32 text_color);
void draw_checkbox(rect_t r, int checked, const char *label);
void draw_radio(rect_t r, int selected, const char *label);
void draw_slider(rect_t r, int value, int min, int max);
void draw_scrollbar(int x, int y, int h, int total_items, int visible_items, int scroll_index);
void draw_text_input(rect_t r, const char* text, int cursor_pos, int has_focus, const char* placeholder);
void draw_progress_bar(rect_t r, int value, int min, int max);
void draw_tooltip(int x, int y, const char* text);
void draw_dialog(const char* title, const char* message, const char* btn1, const char* btn2);
void draw_toast(int x, int y, const char* message, int toast_type);

/* Widget State Management APIs */
void gui_text_input_set(char *buffer, int capacity, int *cursor_ptr, int *focus_ptr);
int  gui_text_input_handle_key(char key, int backspace, int enter);
void gui_progress_set(int value, int min, int max);
void gui_dialog_show(const char *title, const char *message, const char *primary, const char *secondary);
void gui_dialog_close(void);
void gui_tooltip_show(int x, int y, const char *text);
void gui_tooltip_hide(void);
void gui_tooltip_restore(void);
int  gui_tooltip_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* GUI_WIDGETS_H */
