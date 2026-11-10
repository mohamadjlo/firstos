#include "theme.h"

int theme_id = 0;
theme_colors_t active_theme;
theme_profile_t theme_profiles[THEME_PROFILE_COUNT];

static void theme_load_preset(int id) {
    if (id == 1) {
        // Ubuntu Style
        active_theme.top_bar = 0x1E1E1E;
        active_theme.bottom_bar = 0x1E1E1E;
        active_theme.start_menu_bg = 0x2C2C2C;
        active_theme.title_bar = 0x3D3D3D;
        active_theme.title_text = 0xFFFFFF;
        active_theme.title_bar_focused = 0x3D3D3D;
        active_theme.title_bar_inactive = 0x2D2D2D;
        active_theme.window_bg = 0xF2F1EF;
        active_theme.window_border = 0x1E1E1E;
        active_theme.window_border_focused = 0xE95420; // Ubuntu Orange
        active_theme.window_border_inactive = 0x1E1E1E;
        active_theme.control_bg = 0xD4D4D4;
        active_theme.control_bg_hi = 0xE4E4E4;
        active_theme.control_text = 0x333333;
        active_theme.control_text_muted = 0x7A7A7A;
        active_theme.accent = 0xE95420;
        active_theme.font = 0x333333;
        active_theme.desktop_bg = 0x5C2C4E; // Aubergine
        active_theme.desktop_panel = 0x1E1E1E;
        active_theme.desktop_shadow = 0x202020;
    } else if (id == 2) {
        active_theme.top_bar = 0x204A34;
        active_theme.bottom_bar = 0x204A34;
        active_theme.start_menu_bg = 0xE6F2E9;
        active_theme.title_bar = 0x2E6B49;
        active_theme.title_text = 0xFFFFFF;
        active_theme.title_bar_focused = 0x3A855A;
        active_theme.title_bar_inactive = 0x6C8C77;
        active_theme.window_bg = 0xEEF6F0;
        active_theme.window_border = 0x294E36;
        active_theme.window_border_focused = 0x5FA676;
        active_theme.window_border_inactive = 0x587060;
        active_theme.control_bg = 0xC7DDCF;
        active_theme.control_bg_hi = 0xDCF0E2;
        active_theme.control_text = 0x173022;
        active_theme.control_text_muted = 0x507A60;
        active_theme.accent = 0x5FA676;
        active_theme.font = 0x13261A;
        active_theme.desktop_bg = 0x467A59;
        active_theme.desktop_panel = 0x2C5A3A;
        active_theme.desktop_shadow = 0x294E36;
    } else {
        // macOS Style Default
        active_theme.top_bar = 0xE6EBF2;
        active_theme.bottom_bar = 0x162435;
        active_theme.start_menu_bg = 0xF2F6FA;
        active_theme.title_bar = 0xE2E7ED;
        active_theme.title_text = 0x202124;
        active_theme.title_bar_focused = 0xE2E7ED;
        active_theme.title_bar_inactive = 0xF0F4F8;
        active_theme.window_bg = 0xF2F6FA;
        active_theme.window_border = 0x909BA6;
        active_theme.window_border_focused = 0x707B86;
        active_theme.window_border_inactive = 0xA0ABB6;
        active_theme.control_bg = 0xD0D7DF;
        active_theme.control_bg_hi = 0xE0E7EF;
        active_theme.control_text = 0x20344E;
        active_theme.control_text_muted = 0x71859C;
        active_theme.accent = 0x3B82F6; // Blue accent
        active_theme.font = 0x202124;
        active_theme.desktop_bg = 0x456B9A;
        active_theme.desktop_panel = 0xE6EBF2;
        active_theme.desktop_shadow = 0x7C8B99;
    }
}

void theme_apply(void) {
    theme_load_preset(theme_id);
}

void theme_set_preset(int id) {
    if (id < 0) id = 0;
    if (id > 2) id = 2;
    theme_id = id;
    theme_apply();
}

void theme_profile_capture(int slot, int base_preset) {
    if (slot < 0 || slot >= THEME_PROFILE_COUNT) return;
    if (base_preset < 0) base_preset = theme_id;
    if (base_preset > 2) base_preset = 2;
    theme_profiles[slot].used = 1;
    theme_profiles[slot].base_preset = base_preset;
    theme_profiles[slot].colors = active_theme;
}

int theme_profile_apply(int slot) {
    if (slot < 0 || slot >= THEME_PROFILE_COUNT) return 0;
    if (!theme_profiles[slot].used) return 0;
    theme_id = theme_profiles[slot].base_preset;
    active_theme = theme_profiles[slot].colors;
    return 1;
}

int theme_profile_is_used(int slot) {
    if (slot < 0 || slot >= THEME_PROFILE_COUNT) return 0;
    return theme_profiles[slot].used ? 1 : 0;
}

void theme_profile_set_used(int slot, int used) {
    if (slot < 0 || slot >= THEME_PROFILE_COUNT) return;
    theme_profiles[slot].used = used ? 1 : 0;
    if (!theme_profiles[slot].used) theme_profiles[slot].base_preset = 0;
}

void theme_set_top_bar(u32 color) { active_theme.top_bar = color; active_theme.desktop_panel = color; }
void theme_set_bottom_bar(u32 color) { active_theme.bottom_bar = color; }
void theme_set_start_menu_bg(u32 color) { active_theme.start_menu_bg = color; }
void theme_set_title_bar(u32 color) { 
    active_theme.title_bar = color; 
    active_theme.title_bar_focused = color;
    active_theme.title_bar_inactive = color; // Just an example, or could set to slightly different
}
void theme_set_window_bg(u32 color) { active_theme.window_bg = color; }
void theme_set_accent(u32 color) { active_theme.accent = color; }
void theme_set_font_color(u32 color) { active_theme.font = color; }
