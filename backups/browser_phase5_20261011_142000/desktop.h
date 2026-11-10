#ifndef DESKTOP_H
#define DESKTOP_H

#include "gui.h"
#include "gui_internal.h"

void desktop_init(void);
void desktop_draw(void);
void desktop_sync_icons(void);
void desktop_render_shell(void);
void desktop_apply_theme(void);
int desktop_get_bar_height(void);
u32 desktop_get_background_color(void);
u32 desktop_get_panel_color(void);
int desktop_handle_left_click(int mx, int my);
void desktop_handle_right_click(int mx, int my, int *selected_index, const rect_t *dock_rect);
int desktop_is_start_menu_open(void);
void desktop_close_menus(void);
int desktop_point_in_context_menu(int mx, int my);
int desktop_handle_start_menu_click(int mx, int my);
void desktop_layout_context_menu(int mx, int my);
int desktop_handle_context_click(int mx, int my);
int desktop_get_selected(void);
void desktop_set_selected(int index);
int desktop_get_item_count(void);

#endif
void desktop_set_item_pos(int index, int cx, int cy);
void desktop_update_marquee_selection(int rx, int ry, int rw, int rh);
void desktop_clear_multi_selected(void);
