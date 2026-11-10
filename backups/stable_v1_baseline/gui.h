#ifndef GUI_H
#define GUI_H

#include "gui_internal.h"
#include "user.h"

void gui_init(void);
void gui_draw(void);
int gui_needs_redraw(void);
void gui_mark_redraw(void);
void gui_mark_wallpaper_dirty(void);
int gui_power_off_requested(void);
void gui_pump_events(void);

/* Phase 2 widget primitives. */
void gui_text_input_set(char *buffer, int capacity, int *cursor_ptr, int *focus_ptr);
int gui_text_input_handle_key(char key, int backspace, int enter);
void gui_progress_set(int value, int min, int max);
void gui_dialog_show(const char *title, const char *message, const char *primary, const char *secondary);
void gui_dialog_close(void);
void gui_tooltip_show(int x, int y, const char *text);
void gui_tooltip_hide(void);

void gui_draw_avatar(int x, int y, int diameter, const user_profile_t *user);
void gui_invalidate_avatar_cache(void);
void gui_logout(void);
void gui_unlock_to_desktop(void);

extern int snap_enabled;
extern int window_animations;
extern int task_switcher_enabled;

void gui_set_resolution(u32 width, u32 height);
void gui_update_screen_size(void);
int  gui_keyboard_caps_lock(void);
void gui_keyboard_set_caps_lock(int enabled);
int  gui_is_ctrl_pressed(void);
int  gui_is_alt_pressed(void);
int  gui_is_shift_pressed(void);

#endif
