#include "bb_rasterizer.h"

/* Porter-Duff Source-Over Alpha Blending */
bb_color_t bb_blend_color(bb_color_t dst, bb_color_t src) {
    uint32_t sa = BB_GET_A(src);
    if (sa == 255) return src;
    if (sa == 0) return dst;

    uint32_t sr = BB_GET_R(src);
    uint32_t sg = BB_GET_G(src);
    uint32_t sb = BB_GET_B(src);

    uint32_t da = BB_GET_A(dst);
    uint32_t dr = BB_GET_R(dst);
    uint32_t dg = BB_GET_G(dst);
    uint32_t db = BB_GET_B(dst);

    uint32_t inv_a = 255 - sa;
    uint32_t out_r = (sr * sa + dr * inv_a) / 255;
    uint32_t out_g = (sg * sa + dg * inv_a) / 255;
    uint32_t out_b = (sb * sa + db * inv_a) / 255;
    uint32_t out_a = sa + (da * inv_a) / 255;

    return BB_ARGB(out_a, out_r, out_g, out_b);
}

void bb_raster_init(bb_raster_t *r, uint32_t *pixels, uint32_t width, uint32_t height) {
    if (!r) return;
    r->pixels = pixels;
    r->width = width;
    r->height = height;
    r->pitch = width * sizeof(uint32_t);

    r->clip_top = 0;
    r->clip_stack[0].x = 0;
    r->clip_stack[0].y = 0;
    r->clip_stack[0].w = (int32_t)width;
    r->clip_stack[0].h = (int32_t)height;
}

void bb_raster_clear(bb_raster_t *r, bb_color_t color) {
    if (!r || !r->pixels) return;
    uint32_t count = r->width * r->height;
    uint32_t *p = r->pixels;
    while (count--) {
        *p++ = color;
    }
}

void bb_raster_push_clip(bb_raster_t *r, bb_rect_t clip) {
    if (!r) return;
    if (r->clip_top + 1 >= BB_MAX_CLIP_DEPTH) return;

    bb_rect_t cur = r->clip_stack[r->clip_top];
    int32_t x0 = BB_MAX(cur.x, clip.x);
    int32_t y0 = BB_MAX(cur.y, clip.y);
    int32_t x1 = BB_MIN(cur.x + cur.w, clip.x + clip.w);
    int32_t y1 = BB_MIN(cur.y + cur.h, clip.y + clip.h);

    r->clip_top++;
    if (x1 > x0 && y1 > y0) {
        r->clip_stack[r->clip_top].x = x0;
        r->clip_stack[r->clip_top].y = y0;
        r->clip_stack[r->clip_top].w = x1 - x0;
        r->clip_stack[r->clip_top].h = y1 - y0;
    } else {
        r->clip_stack[r->clip_top].x = 0;
        r->clip_stack[r->clip_top].y = 0;
        r->clip_stack[r->clip_top].w = 0;
        r->clip_stack[r->clip_top].h = 0;
    }
}

void bb_raster_pop_clip(bb_raster_t *r) {
    if (!r) return;
    if (r->clip_top > 0) {
        r->clip_top--;
    }
}

bb_rect_t bb_raster_get_clip(const bb_raster_t *r) {
    bb_rect_t null_rect = {0, 0, 0, 0};
    if (!r || r->clip_top < 0) return null_rect;
    return r->clip_stack[r->clip_top];
}

bool bb_raster_clip_rect(const bb_raster_t *r, bb_rect_t in_rect, bb_rect_t *out_rect) {
    if (!r || !out_rect) return false;
    bb_rect_t clip = r->clip_stack[r->clip_top];

    int32_t x0 = BB_MAX(in_rect.x, clip.x);
    int32_t y0 = BB_MAX(in_rect.y, clip.y);
    int32_t x1 = BB_MIN(in_rect.x + in_rect.w, clip.x + clip.w);
    int32_t y1 = BB_MIN(in_rect.y + in_rect.h, clip.y + clip.h);

    if (x1 <= x0 || y1 <= y0) {
        out_rect->x = 0;
        out_rect->y = 0;
        out_rect->w = 0;
        out_rect->h = 0;
        return false;
    }

    out_rect->x = x0;
    out_rect->y = y0;
    out_rect->w = x1 - x0;
    out_rect->h = y1 - y0;
    return true;
}

void bb_raster_draw_pixel(bb_raster_t *r, int32_t x, int32_t y, bb_color_t color) {
    if (!r || !r->pixels) return;
    bb_rect_t clip = r->clip_stack[r->clip_top];
    if (x < clip.x || x >= clip.x + clip.w || y < clip.y || y >= clip.y + clip.h) {
        return;
    }

    uint32_t offset = y * r->width + x;
    uint32_t sa = BB_GET_A(color);
    if (sa == 255) {
        r->pixels[offset] = color;
    } else if (sa > 0) {
        r->pixels[offset] = bb_blend_color(r->pixels[offset], color);
    }
}

void bb_raster_fill_rect(bb_raster_t *r, bb_rect_t rect, bb_color_t color) {
    if (!r || !r->pixels) return;
    uint32_t sa = BB_GET_A(color);
    if (sa == 0) return;

    bb_rect_t clipped;
    if (!bb_raster_clip_rect(r, rect, &clipped)) return;

    int32_t y;
    if (sa == 255) {
        /* Fast solid color fill */
        for (y = clipped.y; y < clipped.y + clipped.h; y++) {
            uint32_t *line = &r->pixels[y * r->width + clipped.x];
            int32_t w = clipped.w;
            while (w--) {
                *line++ = color;
            }
        }
    } else {
        /* Alpha blended fill */
        for (y = clipped.y; y < clipped.y + clipped.h; y++) {
            uint32_t *line = &r->pixels[y * r->width + clipped.x];
            int32_t w = clipped.w;
            while (w--) {
                *line = bb_blend_color(*line, color);
                line++;
            }
        }
    }
}

void bb_raster_draw_rect_outline(bb_raster_t *r, bb_rect_t rect, int32_t border_width, bb_color_t color) {
    if (border_width <= 0) return;
    bb_raster_draw_border_sides(r, rect, border_width, border_width, border_width, border_width, color);
}

void bb_raster_draw_border_sides(bb_raster_t *r, bb_rect_t rect, 
                                 int32_t top_w, int32_t right_w, int32_t bot_w, int32_t left_w,
                                 bb_color_t color) {
    if (!r) return;
    /* Top border */
    if (top_w > 0) {
        bb_rect_t top_rect = {rect.x, rect.y, rect.w, top_w};
        bb_raster_fill_rect(r, top_rect, color);
    }
    /* Bottom border */
    if (bot_w > 0) {
        bb_rect_t bot_rect = {rect.x, rect.y + rect.h - bot_w, rect.w, bot_w};
        bb_raster_fill_rect(r, bot_rect, color);
    }
    /* Left border */
    if (left_w > 0) {
        bb_rect_t left_rect = {rect.x, rect.y + top_w, left_w, rect.h - top_w - bot_w};
        bb_raster_fill_rect(r, left_rect, color);
    }
    /* Right border */
    if (right_w > 0) {
        bb_rect_t right_rect = {rect.x + rect.w - right_w, rect.y + top_w, right_w, rect.h - top_w - bot_w};
        bb_raster_fill_rect(r, right_rect, color);
    }
}

void bb_raster_draw_line(bb_raster_t *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, bb_color_t color) {
    if (!r) return;
    int32_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;

    while (1) {
        bb_raster_draw_pixel(r, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void bb_raster_fill_round_rect(bb_raster_t *r, bb_rect_t rect, int32_t radius, bb_color_t color) {
    if (!r) return;
    if (radius <= 0) {
        bb_raster_fill_rect(r, rect, color);
        return;
    }
    if (radius > rect.w / 2) radius = rect.w / 2;
    if (radius > rect.h / 2) radius = rect.h / 2;

    /* Fill center body */
    bb_rect_t center = {rect.x, rect.y + radius, rect.w, rect.h - 2 * radius};
    bb_raster_fill_rect(r, center, color);

    /* Fill top and bottom center stripes */
    bb_rect_t top_center = {rect.x + radius, rect.y, rect.w - 2 * radius, radius};
    bb_raster_fill_rect(r, top_center, color);
    bb_rect_t bot_center = {rect.x + radius, rect.y + rect.h - radius, rect.w - 2 * radius, radius};
    bb_raster_fill_rect(r, bot_center, color);

    /* Fill 4 corner quadrants */
    int32_t r2 = radius * radius;
    int32_t cy, cx;
    for (cy = 0; cy < radius; cy++) {
        for (cx = 0; cx < radius; cx++) {
            int32_t dx = radius - cx - 1;
            int32_t dy = radius - cy - 1;
            if (dx * dx + dy * dy <= r2) {
                /* Top-left */
                bb_raster_draw_pixel(r, rect.x + cx, rect.y + cy, color);
                /* Top-right */
                bb_raster_draw_pixel(r, rect.x + rect.w - 1 - cx, rect.y + cy, color);
                /* Bottom-left */
                bb_raster_draw_pixel(r, rect.x + cx, rect.y + rect.h - 1 - cy, color);
                /* Bottom-right */
                bb_raster_draw_pixel(r, rect.x + rect.w - 1 - cx, rect.y + rect.h - 1 - cy, color);
            }
        }
    }
}

void bb_raster_draw_round_rect_outline(bb_raster_t *r, bb_rect_t rect, int32_t radius, int32_t border_width, bb_color_t color) {
    if (!r || border_width <= 0) return;
    if (radius <= 0) {
        bb_raster_draw_rect_outline(r, rect, border_width, color);
        return;
    }
    /* Simple rounded outline via outer rounded rect and inner clip or rasterization */
    int i;
    for (i = 0; i < border_width; i++) {
        bb_rect_t curr = {rect.x + i, rect.y + i, rect.w - 2 * i, rect.h - 2 * i};
        int32_t rad = radius - i;
        if (rad < 0) rad = 0;
        
        /* Top horizontal */
        bb_raster_draw_line(r, curr.x + rad, curr.y, curr.x + curr.w - 1 - rad, curr.y, color);
        /* Bottom horizontal */
        bb_raster_draw_line(r, curr.x + rad, curr.y + curr.h - 1, curr.x + curr.w - 1 - rad, curr.y + curr.h - 1, color);
        /* Left vertical */
        bb_raster_draw_line(r, curr.x, curr.y + rad, curr.x, curr.y + curr.h - 1 - rad, color);
        /* Right vertical */
        bb_raster_draw_line(r, curr.x + curr.w - 1, curr.y + rad, curr.x + curr.w - 1, curr.y + curr.h - 1 - rad, color);
        
        /* Corners */
        int32_t r2 = rad * rad;
        int32_t r_inner2 = (rad > 1) ? ((rad - 1) * (rad - 1)) : 0;
        int32_t cy, cx;
        for (cy = 0; cy <= rad; cy++) {
            for (cx = 0; cx <= rad; cx++) {
                int32_t dx = rad - cx;
                int32_t dy = rad - cy;
                int32_t dist = dx * dx + dy * dy;
                if (dist <= r2 && dist >= r_inner2) {
                    bb_raster_draw_pixel(r, curr.x + cx, curr.y + cy, color);
                    bb_raster_draw_pixel(r, curr.x + curr.w - 1 - cx, curr.y + cy, color);
                    bb_raster_draw_pixel(r, curr.x + cx, curr.y + curr.h - 1 - cy, color);
                    bb_raster_draw_pixel(r, curr.x + curr.w - 1 - cx, curr.y + curr.h - 1 - cy, color);
                }
            }
        }
    }
}

void bb_raster_blit(bb_raster_t *dst, int32_t dx, int32_t dy, const bb_raster_t *src, bb_rect_t src_rect) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;

    /* Clip source bounds */
    if (src_rect.x < 0) { src_rect.w += src_rect.x; dx -= src_rect.x; src_rect.x = 0; }
    if (src_rect.y < 0) { src_rect.h += src_rect.y; dy -= src_rect.y; src_rect.y = 0; }
    if (src_rect.x + src_rect.w > (int32_t)src->width) src_rect.w = src->width - src_rect.x;
    if (src_rect.y + src_rect.h > (int32_t)src->height) src_rect.h = src->height - src_rect.y;
    if (src_rect.w <= 0 || src_rect.h <= 0) return;

    /* Target rect and clipping */
    bb_rect_t dst_rect = {dx, dy, src_rect.w, src_rect.h};
    bb_rect_t clipped_dst;
    if (!bb_raster_clip_rect(dst, dst_rect, &clipped_dst)) return;

    int32_t src_start_x = src_rect.x + (clipped_dst.x - dx);
    int32_t src_start_y = src_rect.y + (clipped_dst.y - dy);

    int32_t row;
    for (row = 0; row < clipped_dst.h; row++) {
        uint32_t *d_line = &dst->pixels[(clipped_dst.y + row) * dst->width + clipped_dst.x];
        const uint32_t *s_line = &src->pixels[(src_start_y + row) * src->width + src_start_x];
        int32_t col;
        for (col = 0; col < clipped_dst.w; col++) {
            bb_color_t sc = s_line[col];
            uint32_t sa = BB_GET_A(sc);
            if (sa == 255) {
                d_line[col] = sc;
            } else if (sa > 0) {
                d_line[col] = bb_blend_color(d_line[col], sc);
            }
        }
    }
}

void bb_raster_blit_scaled(bb_raster_t *dst, bb_rect_t dst_rect, const bb_raster_t *src, bb_rect_t src_rect) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;
    if (dst_rect.w <= 0 || dst_rect.h <= 0 || src_rect.w <= 0 || src_rect.h <= 0) return;

    bb_rect_t clipped_dst;
    if (!bb_raster_clip_rect(dst, dst_rect, &clipped_dst)) return;

    /* Fixed-point 16.16 step sizes */
    uint32_t step_x = ((uint32_t)src_rect.w << 16) / (uint32_t)dst_rect.w;
    uint32_t step_y = ((uint32_t)src_rect.h << 16) / (uint32_t)dst_rect.h;

    int32_t start_dx = clipped_dst.x - dst_rect.x;
    int32_t start_dy = clipped_dst.y - dst_rect.y;

    uint32_t curr_sy = ((uint32_t)start_dy * step_y);

    int32_t row;
    for (row = 0; row < clipped_dst.h; row++) {
        uint32_t *d_line = &dst->pixels[(clipped_dst.y + row) * dst->width + clipped_dst.x];
        int32_t sy_idx = src_rect.y + (int32_t)(curr_sy >> 16);
        if (sy_idx >= (int32_t)src->height) sy_idx = (int32_t)src->height - 1;
        const uint32_t *s_line = &src->pixels[sy_idx * src->width];

        uint32_t curr_sx = ((uint32_t)start_dx * step_x);
        int32_t col;
        for (col = 0; col < clipped_dst.w; col++) {
            int32_t sx_idx = src_rect.x + (int32_t)(curr_sx >> 16);
            if (sx_idx >= (int32_t)src->width) sx_idx = (int32_t)src->width - 1;

            bb_color_t sc = s_line[sx_idx];
            uint32_t sa = BB_GET_A(sc);
            if (sa == 255) {
                d_line[col] = sc;
            } else if (sa > 0) {
                d_line[col] = bb_blend_color(d_line[col], sc);
            }
            curr_sx += step_x;
        }
        curr_sy += step_y;
    }
}
