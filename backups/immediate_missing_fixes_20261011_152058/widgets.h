#ifndef WIDGETS_H
#define WIDGETS_H

#include "gui_internal.h"

typedef struct {
    int enabled;
    int x;
    int y;
    int w;
    int h;
    int dragging;
    int drag_ox;
    int drag_oy;
} desktop_widget_t;

extern desktop_widget_t widget_clock;
extern desktop_widget_t widget_calendar;
extern desktop_widget_t widget_notes;
extern desktop_widget_t widget_weather;

extern int widget_clock_mode; // 0 = Digital, 1 = Analog
extern char widget_notes_text[512];
extern int widget_notes_focused;
extern int widget_notes_cursor;

void widgets_init(void);
void widgets_draw(void);
int  widgets_handle_click(int mx, int my, int is_right);
int  widgets_handle_drag(int mx, int my);
void widgets_handle_drag_end(void);
int  widgets_handle_key(char c, int scancode);
void widgets_reset_positions(void);

#endif /* WIDGETS_H */
