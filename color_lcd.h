#ifndef COLOR_LCD_H
#define COLOR_LCD_H

#include "io.h"

/* Display Color Profiles */
typedef enum {
    COLOR_PROFILE_COLOR_LCD = 0,    /* Apple Cinema / Color LCD (D65, Gamma 2.2, Subpixel Anti-Aliasing) */
    COLOR_PROFILE_SRGB = 1,         /* Standard sRGB IEC61966-2.1 */
    COLOR_PROFILE_DISPLAY_P3 = 2,   /* Wide Color Gamut (Display P3, rich reds & greens) */
    COLOR_PROFILE_TRUE_TONE = 3,    /* True Tone / Warm Paper (4500K color temp, eye strain relief) */
    COLOR_PROFILE_HIGH_CONTRAST = 4 /* High Contrast LCD (Enhanced legibility for bright light) */
} color_profile_id_t;

#define COLOR_PROFILE_COUNT 5

void color_lcd_init(void);
void color_lcd_set_profile(color_profile_id_t profile_id);
color_profile_id_t color_lcd_get_profile(void);
const char *color_lcd_get_profile_name(color_profile_id_t profile_id);
const char *color_lcd_get_profile_desc(color_profile_id_t profile_id);

u32  color_lcd_apply_profile(u32 color);
void color_lcd_set_subpixel_enabled(int enabled);
int  color_lcd_get_subpixel_enabled(void);

/* Color LCD Subpixel Text Rendering */
void color_lcd_draw_text(int x, int y, const char *text, u32 color, int scale);

#endif /* COLOR_LCD_H */
