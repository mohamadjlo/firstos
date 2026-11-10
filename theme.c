#include "theme.h"

int theme_id = 0;
theme_colors_t active_theme;
theme_metrics_t active_metrics = {
    .window_radius = 10,
    .control_radius = 6,
    .button_height = 28,
    .input_height = 24,
    .titlebar_height = 28,
    .scrollbar_width = 12,
    .shadow_depth = 4
};
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
    } else if (id == 3) {
        /* Color LCD Sonoma (Dark Frosted Glass / P3 Blue) */
        active_theme.top_bar = 0x181B20;
        active_theme.bottom_bar = 0x121418;
        active_theme.start_menu_bg = 0x222630;
        active_theme.title_bar = 0x282C37;
        active_theme.title_text = 0xF1F5F9;
        active_theme.title_bar_focused = 0x2D323E;
        active_theme.title_bar_inactive = 0x1E222A;
        active_theme.window_bg = 0x1E222B;
        active_theme.window_border = 0x374151;
        active_theme.window_border_focused = 0x38BDF8;
        active_theme.window_border_inactive = 0x272B35;
        active_theme.control_bg = 0x2F3542;
        active_theme.control_bg_hi = 0x3B4252;
        active_theme.control_text = 0xF8FAFC;
        active_theme.control_text_muted = 0x94A3B8;
        active_theme.accent = 0x0EA5E9;
        active_theme.font = 0xF8FAFC;
        active_theme.desktop_bg = 0x0F172A;
        active_theme.desktop_panel = 0x181B20;
        active_theme.desktop_shadow = 0x080A0E;
    } else if (id == 4) {
        /* Midnight OLED (Pure Black & Neon Cyan) */
        active_theme.top_bar = 0x0A0A0A;
        active_theme.bottom_bar = 0x050505;
        active_theme.start_menu_bg = 0x121212;
        active_theme.title_bar = 0x141414;
        active_theme.title_text = 0x00F0FF;
        active_theme.title_bar_focused = 0x1A1A1A;
        active_theme.title_bar_inactive = 0x0D0D0D;
        active_theme.window_bg = 0x000000;
        active_theme.window_border = 0x222222;
        active_theme.window_border_focused = 0x00F0FF;
        active_theme.window_border_inactive = 0x181818;
        active_theme.control_bg = 0x1A1A1A;
        active_theme.control_bg_hi = 0x2A2A2A;
        active_theme.control_text = 0xEEEEEE;
        active_theme.control_text_muted = 0x777777;
        active_theme.accent = 0x00F0FF;
        active_theme.font = 0xFFFFFF;
        active_theme.desktop_bg = 0x000000;
        active_theme.desktop_panel = 0x0A0A0A;
        active_theme.desktop_shadow = 0x000000;
    } else if (id == 5) {
        /* Nordic Frost (Pastel Slate & Ice Blue) */
        active_theme.top_bar = 0xDCE5EE;
        active_theme.bottom_bar = 0x243242;
        active_theme.start_menu_bg = 0xEAF0F6;
        active_theme.title_bar = 0xD3DFEB;
        active_theme.title_text = 0x1E293B;
        active_theme.title_bar_focused = 0xC8D7E6;
        active_theme.title_bar_inactive = 0xE2EAF2;
        active_theme.window_bg = 0xF1F5F9;
        active_theme.window_border = 0x94A3B8;
        active_theme.window_border_focused = 0x0284C7;
        active_theme.window_border_inactive = 0xCBD5E1;
        active_theme.control_bg = 0xCBD5E1;
        active_theme.control_bg_hi = 0xE2E8F0;
        active_theme.control_text = 0x0F172A;
        active_theme.control_text_muted = 0x64748B;
        active_theme.accent = 0x0284C7;
        active_theme.font = 0x0F172A;
        active_theme.desktop_bg = 0x334155;
        active_theme.desktop_panel = 0xDCE5EE;
        active_theme.desktop_shadow = 0x64748B;
    } else if (id == 6) {
        /* Solarized LCD (Warm Low-Glare Amber & Cyan) */
        active_theme.top_bar = 0x002B36;
        active_theme.bottom_bar = 0x073642;
        active_theme.start_menu_bg = 0x073642;
        active_theme.title_bar = 0x073642;
        active_theme.title_text = 0x93A1A1;
        active_theme.title_bar_focused = 0x002B36;
        active_theme.title_bar_inactive = 0x073642;
        active_theme.window_bg = 0x002B36;
        active_theme.window_border = 0x586E75;
        active_theme.window_border_focused = 0xB58900;
        active_theme.window_border_inactive = 0x073642;
        active_theme.control_bg = 0x073642;
        active_theme.control_bg_hi = 0x586E75;
        active_theme.control_text = 0x93A1A1;
        active_theme.control_text_muted = 0x657B83;
        active_theme.accent = 0xB58900;
        active_theme.font = 0x93A1A1;
        active_theme.desktop_bg = 0x002B36;
        active_theme.desktop_panel = 0x002B36;
        active_theme.desktop_shadow = 0x001A20;
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
    if (id > 6) id = 6;
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

const theme_colors_t *theme_get_colors(void) {
    return &active_theme;
}

const theme_metrics_t *theme_get_metrics(void) {
    return &active_metrics;
}

u32 theme_mix_color(u32 a, u32 b) {
    u32 ra = (a >> 16) & 0xFF;
    u32 ga = (a >> 8) & 0xFF;
    u32 ba = a & 0xFF;
    u32 rb = (b >> 16) & 0xFF;
    u32 gb = (b >> 8) & 0xFF;
    u32 bb = b & 0xFF;
    return (((ra + rb) / 2) << 16) | (((ga + gb) / 2) << 8) | ((ba + bb) / 2);
}

