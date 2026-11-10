#ifndef THEME_H
#define THEME_H

#include "framebuffer.h"

typedef struct {
    u32 top_bar;
    u32 bottom_bar;
    u32 start_menu_bg;
    u32 title_bar;
    u32 title_text;
    u32 title_bar_focused;
    u32 title_bar_inactive;
    u32 window_bg;
    u32 window_border;
    u32 window_border_focused;
    u32 window_border_inactive;
    u32 control_bg;
    u32 control_bg_hi;
    u32 control_text;
    u32 control_text_muted;
    u32 accent;
    u32 font;
    u32 desktop_bg;
    u32 desktop_panel;
    u32 desktop_shadow;
} theme_colors_t;

typedef struct {
    int window_radius;
    int control_radius;
    int button_height;
    int input_height;
    int titlebar_height;
    int scrollbar_width;
    int shadow_depth;
} theme_metrics_t;

extern int theme_id;
extern theme_colors_t active_theme;
extern theme_metrics_t active_metrics;

const theme_colors_t *theme_get_colors(void);
const theme_metrics_t *theme_get_metrics(void);
u32 theme_mix_color(u32 a, u32 b);

#define THEME_PROFILE_COUNT 3

typedef struct {
    int used;
    int base_preset;
    theme_colors_t colors;
} theme_profile_t;

extern theme_profile_t theme_profiles[THEME_PROFILE_COUNT];

void theme_apply(void);
void theme_set_preset(int id);
void theme_profile_capture(int slot, int base_preset);
int theme_profile_apply(int slot);
int theme_profile_is_used(int slot);
void theme_profile_set_used(int slot, int used);
void theme_set_top_bar(u32 color);
void theme_set_bottom_bar(u32 color);
void theme_set_start_menu_bg(u32 color);
void theme_set_title_bar(u32 color);
void theme_set_window_bg(u32 color);
void theme_set_accent(u32 color);
void theme_set_font_color(u32 color);

#endif
