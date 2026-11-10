#ifndef UI_BUILDER_H
#define UI_BUILDER_H
#include "gui_internal.h"
#include "framebuffer.h"

void draw_ui_builder(rect_t bounds);
int handle_ui_builder_click(int mx, int my);
int handle_ui_builder_mouse_up(int mx, int my);
int handle_ui_builder_mouse_move(int mx, int my);
void handle_ui_builder_keypress(char c);

extern int ide_designer_mode;

#endif
