#ifndef SETTINGS_H
#define SETTINGS_H

#include "framebuffer.h"
#include "gui_internal.h"

typedef unsigned int u32;

extern int appearance_theme_index;
extern int appearance_accent_index;
extern u32 appearance_custom_accent;
extern int settings_night_mode;
extern int settings_taskbar_autohide;
extern int settings_clock_24h;
extern int settings_date_dmy;
extern int settings_net_focus;
extern int settings_login_bg_mode;
extern int settings_account_focus;

void draw_settings_contents(app_window_t *app);
void handle_settings_click(app_window_t *app, int mx, int my);
void settings_save_to_disk(void);
void settings_load_from_disk(void);
void settings_set_tab(int tab);
void settings_set_subtab(int subtab);
void settings_handle_avatar_selected(const char *path);

#endif
