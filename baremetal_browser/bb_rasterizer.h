#ifndef BB_RASTERIZER_H
#define BB_RASTERIZER_H

#include "bb_types.h"
#include "bb_libc.h"

#define BB_MAX_CLIP_DEPTH 32

typedef struct {
    uint32_t *pixels;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;
    bb_rect_t clip_stack[BB_MAX_CLIP_DEPTH];
    int       clip_top;
} bb_raster_t;

/* Rasterizer Lifecycle */
void bb_raster_init(bb_raster_t *r, uint32_t *pixels, uint32_t width, uint32_t height);
void bb_raster_clear(bb_raster_t *r, bb_color_t color);

/* Clipping Rectangle Management */
void      bb_raster_push_clip(bb_raster_t *r, bb_rect_t clip);
void      bb_raster_pop_clip(bb_raster_t *r);
bb_rect_t bb_raster_get_clip(const bb_raster_t *r);
bool      bb_raster_clip_rect(const bb_raster_t *r, bb_rect_t in_rect, bb_rect_t *out_rect);

/* Color Compositing */
bb_color_t bb_blend_color(bb_color_t dst, bb_color_t src);

/* Drawing Primitives */
void bb_raster_draw_pixel(bb_raster_t *r, int32_t x, int32_t y, bb_color_t color);
void bb_raster_fill_rect(bb_raster_t *r, bb_rect_t rect, bb_color_t color);
void bb_raster_draw_rect_outline(bb_raster_t *r, bb_rect_t rect, int32_t border_width, bb_color_t color);
void bb_raster_draw_border_sides(bb_raster_t *r, bb_rect_t rect, 
                                 int32_t top_w, int32_t right_w, int32_t bot_w, int32_t left_w,
                                 bb_color_t color);
void bb_raster_draw_line(bb_raster_t *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, bb_color_t color);
void bb_raster_fill_round_rect(bb_raster_t *r, bb_rect_t rect, int32_t radius, bb_color_t color);
void bb_raster_draw_round_rect_outline(bb_raster_t *r, bb_rect_t rect, int32_t radius, int32_t border_width, bb_color_t color);
void bb_raster_blit(bb_raster_t *dst, int32_t dx, int32_t dy, const bb_raster_t *src, bb_rect_t src_rect);
void bb_raster_blit_scaled(bb_raster_t *dst, bb_rect_t dst_rect, const bb_raster_t *src, bb_rect_t src_rect);

#endif /* BB_RASTERIZER_H */
