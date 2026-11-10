#ifndef BB_TYPES_H
#define BB_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef NULL
  #define NULL ((void*)0)
#endif

/* ARGB (0xAARRGGBB) 32-bpp Color Representation */
typedef uint32_t bb_color_t;

#define BB_ARGB(a, r, g, b) \
    ((((bb_color_t)(a) & 0xFF) << 24) | \
     (((bb_color_t)(r) & 0xFF) << 16) | \
     (((bb_color_t)(g) & 0xFF) << 8)  | \
     ((bb_color_t)(b) & 0xFF))

#define BB_RGB(r, g, b) BB_ARGB(255, (r), (g), (b))

#define BB_GET_A(c) (((c) >> 24) & 0xFF)
#define BB_GET_R(c) (((c) >> 16) & 0xFF)
#define BB_GET_G(c) (((c) >> 8) & 0xFF)
#define BB_GET_B(c) ((c) & 0xFF)

/* Common Web & UI Colors */
#define BB_COLOR_TRANSPARENT  BB_ARGB(0, 0, 0, 0)
#define BB_COLOR_WHITE        BB_RGB(255, 255, 255)
#define BB_COLOR_BLACK        BB_RGB(0, 0, 0)
#define BB_COLOR_DARK_GRAY    BB_RGB(45, 45, 48)
#define BB_COLOR_MID_GRAY     BB_RGB(128, 128, 128)
#define BB_COLOR_LIGHT_GRAY   BB_RGB(240, 240, 242)
#define BB_COLOR_BORDER_GRAY  BB_RGB(210, 212, 216)
#define BB_COLOR_CHROME_BG    BB_RGB(232, 234, 237)
#define BB_COLOR_CHROME_BAR   BB_RGB(248, 249, 250)
#define BB_COLOR_URLBAR_FOCUS BB_RGB(26, 115, 232)
#define BB_COLOR_BLUE         BB_RGB(30, 100, 220)
#define BB_COLOR_LINK         BB_RGB(26, 82, 178)
#define BB_COLOR_BUTTON_HOVER BB_RGB(220, 224, 230)
#define BB_COLOR_BUTTON_PRESS BB_RGB(200, 206, 214)

/* 2D Geometry Structures */
typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} bb_rect_t;

typedef struct {
    int32_t x;
    int32_t y;
} bb_point_t;

/* Min / Max / Clamp */
#define BB_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define BB_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define BB_CLAMP(val, min_v, max_v) BB_MAX((min_v), BB_MIN((val), (max_v)))

#endif /* BB_TYPES_H */
