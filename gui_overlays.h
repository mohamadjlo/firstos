#ifndef GUI_OVERLAYS_H
#define GUI_OVERLAYS_H

#include "gui_internal.h"

extern int launcher_open;
extern int launcher_hover;
extern int explorer_quick_look_open;
extern rect_t explorer_ql_close_btn;
extern rect_t explorer_ql_play_btn;
extern rect_t explorer_ql_open_btn;

void draw_launcher_overlay(int mouse_x, int mouse_y);
void draw_spotlight_overlay(void);
void draw_explorer_quick_look(void);
int  sp_str_match(const char *hay, const char *ned);

#endif /* GUI_OVERLAYS_H */
