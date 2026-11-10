#include "color_lcd.h"
#include "framebuffer.h"
#include "font.h"
#include "serial.h"

static color_profile_id_t s_active_profile = COLOR_PROFILE_COLOR_LCD;
static int s_subpixel_enabled = 1;

static u8 s_lut_r[256];
static u8 s_lut_g[256];
static u8 s_lut_b[256];

static const char *s_profile_names[COLOR_PROFILE_COUNT] = {
    "Color LCD (Apple Cinema)",
    "sRGB IEC61966-2.1",
    "Display P3 (Wide Gamut)",
    "True Tone (Warm Paper)",
    "High Contrast LCD"
};

static const char *s_profile_descs[COLOR_PROFILE_COUNT] = {
    "Calibrated for Apple Cinema & Retina LCD with D65 white point and subpixel rendering.",
    "Industry-standard sRGB color space for general monitors and web content.",
    "Wide color gamut delivering 25% richer reds, emerald greens, and cinematic vibrancy.",
    "Warm 4500K color temperature reducing blue-light exposure and eye fatigue.",
    "Enhanced dynamic contrast curve for bright ambient light and high legibility."
};

static void build_luts_for_profile(color_profile_id_t profile_id) {
    for (int i = 0; i < 256; i++) {
        if (profile_id == COLOR_PROFILE_COLOR_LCD) {
            /* Apple Color LCD D65 Calibration */
            int r = (i * 258) / 256;
            int g = (i * 255) / 256;
            int b = (i * 258) / 256;
            s_lut_r[i] = (u8)(r > 255 ? 255 : r);
            s_lut_g[i] = (u8)(g > 255 ? 255 : g);
            s_lut_b[i] = (u8)(b > 255 ? 255 : b);
        } else if (profile_id == COLOR_PROFILE_SRGB) {
            /* 1:1 Standard sRGB */
            s_lut_r[i] = (u8)i;
            s_lut_g[i] = (u8)i;
            s_lut_b[i] = (u8)i;
        } else if (profile_id == COLOR_PROFILE_DISPLAY_P3) {
            /* Display P3 Wide Gamut: expanded red and green saturation */
            int r = (i * 272) / 256;
            int g = (i * 268) / 256;
            int b = (i * 256) / 256;
            s_lut_r[i] = (u8)(r > 255 ? 255 : r);
            s_lut_g[i] = (u8)(g > 255 ? 255 : g);
            s_lut_b[i] = (u8)(b > 255 ? 255 : b);
        } else if (profile_id == COLOR_PROFILE_TRUE_TONE) {
            /* True Tone / Warm Paper: warm sepia shift, reduced blue */
            int r = (i * 266) / 256;
            int g = (i * 252) / 256;
            int b = (i * 210) / 256;
            s_lut_r[i] = (u8)(r > 255 ? 255 : r);
            s_lut_g[i] = (u8)(g > 255 ? 255 : g);
            s_lut_b[i] = (u8)(b > 255 ? 255 : b);
        } else if (profile_id == COLOR_PROFILE_HIGH_CONTRAST) {
            /* High Contrast LCD: S-Curve contrast stretch */
            int v = i;
            if (v < 128) {
                v = (v * v) / 140;
            } else {
                int d = 255 - v;
                v = 255 - ((d * d) / 140);
            }
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            s_lut_r[i] = (u8)v;
            s_lut_g[i] = (u8)v;
            s_lut_b[i] = (u8)v;
        }
    }
}

void color_lcd_init(void) {
    s_active_profile = COLOR_PROFILE_COLOR_LCD;
    s_subpixel_enabled = 1;
    build_luts_for_profile(s_active_profile);
    serial_write("[color_lcd] Subpixel rendering & Color LCD profile initialized\n");
}

void color_lcd_set_profile(color_profile_id_t profile_id) {
    if ((int)profile_id < 0 || profile_id >= COLOR_PROFILE_COUNT) return;
    s_active_profile = profile_id;
    build_luts_for_profile(s_active_profile);

    serial_write("[color_lcd] Switched display color profile to: ");
    serial_write(s_profile_names[s_active_profile]);
    serial_write("\n");
}

color_profile_id_t color_lcd_get_profile(void) {
    return s_active_profile;
}

const char *color_lcd_get_profile_name(color_profile_id_t profile_id) {
    if ((int)profile_id >= 0 && profile_id < COLOR_PROFILE_COUNT) {
        return s_profile_names[profile_id];
    }
    return "Custom";
}

const char *color_lcd_get_profile_desc(color_profile_id_t profile_id) {
    if ((int)profile_id >= 0 && profile_id < COLOR_PROFILE_COUNT) {
        return s_profile_descs[profile_id];
    }
    return "";
}

u32 color_lcd_apply_profile(u32 color) {
    u32 a = (color >> 24) & 0xFF;
    u32 r = (color >> 16) & 0xFF;
    u32 g = (color >> 8)  & 0xFF;
    u32 b = color & 0xFF;

    u32 nr = s_lut_r[r];
    u32 ng = s_lut_g[g];
    u32 nb = s_lut_b[b];

    return (a << 24) | (nr << 16) | (ng << 8) | nb;
}

void color_lcd_set_subpixel_enabled(int enabled) {
    s_subpixel_enabled = enabled ? 1 : 0;
    serial_write("[color_lcd] Subpixel anti-aliasing ");
    serial_write(s_subpixel_enabled ? "ENABLED\n" : "DISABLED\n");
}

int color_lcd_get_subpixel_enabled(void) {
    return s_subpixel_enabled;
}

/* Subpixel blend helper for LCD horizontal stripe geometry [R | G | B] */
static void blend_subpixel(int x, int y, u32 fg_color, u8 r_cov, u8 g_cov, u8 b_cov) {
    u32 bg = fb_read_pixel(x, y);
    u32 fg = color_lcd_apply_profile(fg_color);

    u32 bg_r = (bg >> 16) & 0xFF;
    u32 bg_g = (bg >> 8)  & 0xFF;
    u32 bg_b = bg & 0xFF;

    u32 fg_r = (fg >> 16) & 0xFF;
    u32 fg_g = (fg >> 8)  & 0xFF;
    u32 fg_b = fg & 0xFF;

    u32 out_r = ((fg_r * r_cov) + (bg_r * (255 - r_cov))) / 255;
    u32 out_g = ((fg_g * g_cov) + (bg_g * (255 - g_cov))) / 255;
    u32 out_b = ((fg_b * b_cov) + (bg_b * (255 - b_cov))) / 255;

    fb_putpixel(x, y, (out_r << 16) | (out_g << 8) | out_b);
}

void color_lcd_draw_text(int x, int y, const char *text, u32 color, int scale) {
    if (!text || scale < 1) return;

    /* When subpixel smoothing is disabled or scale > 1, fall back to font_text */
    if (!s_subpixel_enabled || scale > 1) {
        font_text(x, y, text, color_lcd_apply_profile(color), scale);
        return;
    }

    int ox = x;
    u32 calibrated_color = color_lcd_apply_profile(color);

    for (int i = 0; text[i]; i++) {
        char c = text[i];
        if (c == '\n') {
            y += 11;
            x = ox;
            continue;
        }
        if (c == ' ') {
            x += 4;
            continue;
        }

        /* Standard 5x7 glyph rasterization with Color LCD subpixel fringe filter */
        extern const u8 *glyph_for_char(char c);
        const u8 *g = glyph_for_char(c);

        for (int row = 0; row < 7; row++) {
            u8 row_bits = g[row];
            for (int col = 0; col < 5; col++) {
                int on = (row_bits & (1 << (4 - col))) ? 1 : 0;
                int left_on  = (col > 0) ? ((row_bits & (1 << (5 - col))) ? 1 : 0) : 0;
                int right_on = (col < 4) ? ((row_bits & (1 << (3 - col))) ? 1 : 0) : 0;

                int px = x + col;
                int py = y + row;

                if (on) {
                    /* Main solid pixel */
                    u8 r_cov = 255;
                    u8 g_cov = 255;
                    u8 b_cov = 255;
                    blend_subpixel(px, py, calibrated_color, r_cov, g_cov, b_cov);
                } else if (left_on && !right_on) {
                    /* Left neighbor was on: soft red/green bleed on left subpixels */
                    blend_subpixel(px, py, calibrated_color, 110, 50, 0);
                } else if (right_on && !left_on) {
                    /* Right neighbor is on: soft green/blue bleed on right subpixels */
                    blend_subpixel(px, py, calibrated_color, 0, 50, 110);
                }
            }
        }
        x += 6;
    }
}
