#include "framebuffer.h"
#include "virtio_gpu_2d.h"
#include "serial.h"

framebuffer_t fb;
static u8 framebuffer_backing[32 * 1024 * 1024];
static int clip_enabled = 0;
static int clip_x0, clip_y0, clip_x1, clip_y1;
static virtio_gpu_2d_t gpu2d;
static int gpu2d_attempted = 0;
static int dirty_valid = 0;
static u32 dirty_x0, dirty_y0, dirty_x1, dirty_y1;

void fast_memcpy32(void *dst, const void *src, u32 count) {
    if (!dst || !src || !count) return;
    __asm__ volatile(
        "cld\n\t"
        "rep movsl"
        : "+D"(dst), "+S"(src), "+c"(count)
        :
        : "memory"
    );
}

void fast_fill32(void *dst, u32 val, u32 count) {
    if (!dst || !count) return;
    __asm__ volatile(
        "cld\n\t"
        "rep stosl"
        : "+D"(dst), "+c"(count)
        : "a"(val)
        : "memory"
    );
}

static void mark_dirty(int x, int y) {
    u32 ux, uy;
    if (!fb.ready || !fb.backbuffer) return;
    if (x < 0 || y < 0 || (u32)x >= fb.width || (u32)y >= fb.height) return;
    ux = (u32)x;
    uy = (u32)y;
    if (!dirty_valid) {
        dirty_x0 = ux; dirty_y0 = uy; dirty_x1 = ux + 1; dirty_y1 = uy + 1;
        dirty_valid = 1;
        return;
    }
    if (ux < dirty_x0) dirty_x0 = ux;
    if (uy < dirty_y0) dirty_y0 = uy;
    if (ux + 1 > dirty_x1) dirty_x1 = ux + 1;
    if (uy + 1 > dirty_y1) dirty_y1 = uy + 1;
}

#define FB_MAX_DIRTY_RECTS 16

typedef struct {
    u32 x;
    u32 y;
    u32 w;
    u32 h;
} fb_dirty_rect_t;

static fb_dirty_rect_t s_dirty_rects[FB_MAX_DIRTY_RECTS];
static int s_dirty_rect_count = 0;

int fb_get_dirty_rect_count(void) {
    return s_dirty_rect_count;
}

int fb_get_dirty_rect(int idx, u32 *x, u32 *y, u32 *w, u32 *h) {
    if (idx < 0 || idx >= s_dirty_rect_count) return 0;
    if (x) *x = s_dirty_rects[idx].x;
    if (y) *y = s_dirty_rects[idx].y;
    if (w) *w = s_dirty_rects[idx].w;
    if (h) *h = s_dirty_rects[idx].h;
    return 1;
}

void fb_mark_dirty_rect(int x, int y, int w, int h) {
#define mark_dirty_rect fb_mark_dirty_rect
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (!fb.ready || !fb.backbuffer || w <= 0 || h <= 0) return;
    if (clip_enabled) {
        if (x0 < clip_x0) x0 = clip_x0;
        if (y0 < clip_y0) y0 = clip_y0;
        if (x1 > clip_x1) x1 = clip_x1;
        if (y1 > clip_y1) y1 = clip_y1;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)fb.width) x1 = (int)fb.width;
    if (y1 > (int)fb.height) y1 = (int)fb.height;
    if (x0 >= x1 || y0 >= y1) return;

    u32 rx = (u32)x0;
    u32 ry = (u32)y0;
    u32 rw = (u32)(x1 - x0);
    u32 rh = (u32)(y1 - y0);

    /* Update single bounding box */
    if (!dirty_valid) {
        dirty_x0 = rx; dirty_y0 = ry; dirty_x1 = rx + rw; dirty_y1 = ry + rh;
        dirty_valid = 1;
        s_dirty_rect_count = 0;
    } else {
        if (rx < dirty_x0) dirty_x0 = rx;
        if (ry < dirty_y0) dirty_y0 = ry;
        if (rx + rw > dirty_x1) dirty_x1 = rx + rw;
        if (ry + rh > dirty_y1) dirty_y1 = ry + rh;
    }

    /* If already full screen, no need to track sub-rects */
    if (s_dirty_rect_count == 1 && s_dirty_rects[0].w >= fb.width && s_dirty_rects[0].h >= fb.height) {
        return;
    }

    /* Merge into overlapping or adjacent rectangle (threshold: 8px) */
    int merged = 0;
    for (int i = 0; i < s_dirty_rect_count; i++) {
        fb_dirty_rect_t *dr = &s_dirty_rects[i];
        u32 dr_x1 = dr->x + dr->w;
        u32 dr_y1 = dr->y + dr->h;
        u32 r_x1 = rx + rw;
        u32 r_y1 = ry + rh;

        if (!(rx > dr_x1 + 8 || r_x1 + 8 < dr->x || ry > dr_y1 + 8 || r_y1 + 8 < dr->y)) {
            u32 nx0 = (rx < dr->x) ? rx : dr->x;
            u32 ny0 = (ry < dr->y) ? ry : dr->y;
            u32 nx1 = (r_x1 > dr_x1) ? r_x1 : dr_x1;
            u32 ny1 = (r_y1 > dr_y1) ? r_y1 : dr_y1;
            dr->x = nx0;
            dr->y = ny0;
            dr->w = nx1 - nx0;
            dr->h = ny1 - ny0;
            merged = 1;
            break;
        }
    }

    if (!merged) {
        if (s_dirty_rect_count < FB_MAX_DIRTY_RECTS) {
            s_dirty_rects[s_dirty_rect_count].x = rx;
            s_dirty_rects[s_dirty_rect_count].y = ry;
            s_dirty_rects[s_dirty_rect_count].w = rw;
            s_dirty_rects[s_dirty_rect_count].h = rh;
            s_dirty_rect_count++;
        } else {
            /* Fallback to single bounding box if max capacity exceeded */
            s_dirty_rect_count = 1;
            s_dirty_rects[0].x = dirty_x0;
            s_dirty_rects[0].y = dirty_y0;
            s_dirty_rects[0].w = dirty_x1 - dirty_x0;
            s_dirty_rects[0].h = dirty_y1 - dirty_y0;
        }
    }
}


static u32 scale_channel(u32 value8, u8 bits) {
    if (bits == 0) return 0;
    if (bits >= 8) return value8;
    return (value8 * ((1u << bits) - 1u) + 127u) / 255u;
}

static u32 pack_color(u32 color) {
    u32 r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF, pixel = 0;
    if (fb.type == 0) return (scale_channel(r, 3) << 5) | (scale_channel(g, 3) << 2) | scale_channel(b, 2);
    if (fb.red_mask && fb.green_mask && fb.blue_mask) {
        pixel |= scale_channel(r, fb.red_mask) << fb.red_position;
        pixel |= scale_channel(g, fb.green_mask) << fb.green_position;
        pixel |= scale_channel(b, fb.blue_mask) << fb.blue_position;
        return pixel;
    }
    if (fb.bpp == 15) return (scale_channel(r, 5) << 10) | (scale_channel(g, 5) << 5) | scale_channel(b, 5);
    if (fb.bpp == 16) return (scale_channel(r, 5) << 11) | (scale_channel(g, 6) << 5) | scale_channel(b, 5);
    return color & 0x00FFFFFFu;
}

static void write_pixel(u8* base, int x, int y, u32 color) {
    u8* pixel;
    u32 packed, bytes_per_pixel;
    if (x < 0 || y < 0 || (u32)x >= fb.width || (u32)y >= fb.height) return;
    if (clip_enabled && (x < clip_x0 || y < clip_y0 || x >= clip_x1 || y >= clip_y1)) return;
    bytes_per_pixel = (fb.bpp + 7) / 8;
    pixel = base + ((u32)y * fb.pitch) + ((u32)x * bytes_per_pixel);
    packed = pack_color(color);
    if (fb.type == 0 || fb.bpp <= 8) *pixel = (u8)packed;
    else if (fb.bpp == 32) *((u32*)pixel) = packed;
    else if (fb.bpp == 24) { pixel[0] = (u8)packed; pixel[1] = (u8)(packed >> 8); pixel[2] = (u8)(packed >> 16); }
    else *((u16*)pixel) = (u16)packed;
    if (base == fb.backbuffer) mark_dirty(x, y);
}


static void try_init_gpu2d(void){
    int rc;
    if (gpu2d_attempted) return;
    if (!fb.ready || !fb.backbuffer || fb.bpp != 32) return;
    gpu2d_attempted = 1;
    gpu2d.pixels = fb.backbuffer;
    rc = virtio_gpu_2d_init(&gpu2d, fb.width, fb.height);
    if (rc < 0) {
        serial_write("[framebuffer] virtio-gpu 2D unavailable rc=");
        serial_write_hex((u32)rc);
        serial_write("\n");
        /* gpu2d.ready = 0; ignored timeout */
        return;
    }
    serial_write("[framebuffer] virtio-gpu 2D present path enabled\n");
}

void framebuffer_init_virtual(u32 width, u32 height) {
    u32 pitch = width * 4;
    u32 size = height * pitch;

    fb.address = 0;
    fb.backbuffer = 0;
    fb.width = width;
    fb.height = height;
    fb.pitch = pitch;
    fb.bpp = 32;
    fb.buffer_size = size;
    fb.type = 1;
    fb.red_position = 16;
    fb.red_mask = 8;
    fb.green_position = 8;
    fb.green_mask = 8;
    fb.blue_position = 0;
    fb.blue_mask = 8;
    fb.palette = 0;
    fb.palette_size = 0;
    fb.ready = 0;
     fb.drawing_backbuffer = 0; 
    gpu2d_attempted = 0;
    /* gpu2d.ready = 0; ignored timeout */
    gpu2d.pixels = 0;
    dirty_valid = 0;
    dirty_x0 = 0;
    dirty_y0 = 0;
    dirty_x1 = 0;
    dirty_y1 = 0;
    s_dirty_rect_count = 0;

    if (!width || !height || size > sizeof(framebuffer_backing)) {
        serial_write("[framebuffer] framebuffer_init_virtual: size check failed\n");
        return;
    }

    fb.address = framebuffer_backing;
    fb.backbuffer = framebuffer_backing;
    fb.ready = 1;
    serial_write("[framebuffer] framebuffer_init_virtual: ok w=");
    serial_write_hex(width);
    serial_write(" h=");
    serial_write_hex(height);
    serial_write("\n");
}

void framebuffer_init(u64 addr, u32 width, u32 height, u32 pitch, u32 bpp, u8 type,
                      u8 red_position, u8 red_mask, u8 green_position, u8 green_mask,
                      u8 blue_position, u8 blue_mask, u32 palette_addr, u16 palette_size) {
    u32 size = height * pitch;
    fb.address = 0; fb.backbuffer = 0; fb.width = width; fb.height = height; fb.pitch = pitch;
    fb.bpp = bpp; fb.buffer_size = size; fb.type = type; fb.red_position = red_position; fb.red_mask = red_mask;
    fb.green_position = green_position; fb.green_mask = green_mask; fb.blue_position = blue_position; fb.blue_mask = blue_mask;
    fb.palette = (fb_palette_entry_t*)(unsigned long)palette_addr; fb.palette_size = palette_size; fb.ready = 0;  fb.drawing_backbuffer = 0; 
    gpu2d_attempted = 0; /* gpu2d.ready = 0; ignored timeout */ gpu2d.pixels = 0; dirty_valid = 0; s_dirty_rect_count = 0;
    if (!addr || !width || !height || pitch < width * ((bpp + 7) / 8) || bpp == 0 || bpp > 32 || size > sizeof(framebuffer_backing)) return;
    if (type == 0 && bpp > 8) return;
    if (type == 1 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return;
    if (type == 2 && bpp != 16) return;
    fb.address = (u8*)(unsigned long)addr; fb.backbuffer = framebuffer_backing; fb.ready = 1;
}

void fb_putpixel(int x, int y, u32 color) { if (fb.ready) write_pixel(fb.drawing_backbuffer && fb.backbuffer ? fb.backbuffer : fb.address, x, y, color); }

static void fb_fill_row32(u8 *base, u32 row_y, u32 col_x, u32 count, u32 color) {
    u32 *row = (u32*)(base + row_y * fb.pitch) + col_x;
    fast_fill32(row, color, count);
}

void fb_clear(u32 color) {
    u8* base;
    u32 y;
    if (!fb.ready) return;
    base = (fb.drawing_backbuffer && fb.backbuffer) ? fb.backbuffer : fb.address;
    if (!base) return;
    if (fb.bpp == 32) {
        u32 packed = pack_color(color);
        if (fb.pitch == fb.width * 4) {
            fast_fill32(base, packed, fb.width * fb.height);
        } else {
            for (y = 0; y < fb.height; y++) fb_fill_row32(base, y, 0, fb.width, packed);
        }
    } else {
        u32 x;
        for (y = 0; y < fb.height; y++)
            for (x = 0; x < fb.width; x++)
                write_pixel(base, (int)x, (int)y, color);
    }
    if (base == fb.backbuffer) {
        dirty_valid = 1; dirty_x0 = 0; dirty_y0 = 0;
        dirty_x1 = fb.width; dirty_y1 = fb.height;
        s_dirty_rect_count = 1;
        s_dirty_rects[0].x = 0;
        s_dirty_rects[0].y = 0;
        s_dirty_rects[0].w = fb.width;
        s_dirty_rects[0].h = fb.height;
    }
}

void fb_rect(int x, int y, int w, int h, u32 color) {
    u8* base;
    int x0, y0, x1, y1, row;
    if (!fb.ready || w <= 0 || h <= 0) return;
    base = (fb.drawing_backbuffer && fb.backbuffer) ? fb.backbuffer : fb.address;
    if (!base) return;
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + w; if (x1 > (int)fb.width)  x1 = (int)fb.width;
    y1 = y + h; if (y1 > (int)fb.height) y1 = (int)fb.height;
    if (clip_enabled) {
        if (x0 < clip_x0) x0 = clip_x0;
        if (y0 < clip_y0) y0 = clip_y0;
        if (x1 > clip_x1) x1 = clip_x1;
        if (y1 > clip_y1) y1 = clip_y1;
    }
    if (x0 >= x1 || y0 >= y1) return;
    if (fb.bpp == 32) {
        u32 packed = pack_color(color);
        u32 span = (u32)(x1 - x0);
        for (row = y0; row < y1; row++) {
            u32 *row_ptr = (u32*)(base + (u32)row * fb.pitch) + x0;
            fast_fill32(row_ptr, packed, span);
        }
    } else {
        int xx, yy;
        for (yy = y0; yy < y1; yy++)
            for (xx = x0; xx < x1; xx++)
                write_pixel(base, xx, yy, color);
    }
    if (base == fb.backbuffer) mark_dirty_rect(x0, y0, x1 - x0, y1 - y0);
}

u32 fb_read_pixel(int x, int y) {
    if (!fb.ready || x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height) return 0;
    u8 *base = (fb.drawing_backbuffer && fb.backbuffer) ? fb.backbuffer : fb.address;
    if (!base) return 0;
    if (fb.bpp == 32) {
        return *(u32*)(base + (u32)y * fb.pitch + (u32)x * 4);
    }
    return 0;
}

static inline u32 blend_pixel_black_fast(u32 dst, u32 inv_alpha) {
    u32 dr = (dst >> 16) & 0xFF;
    u32 dg = (dst >> 8) & 0xFF;
    u32 db = dst & 0xFF;
    u32 nr = (dr * inv_alpha + 128) >> 8;
    u32 ng = (dg * inv_alpha + 128) >> 8;
    u32 nb = (db * inv_alpha + 128) >> 8;
    return (nr << 16) | (ng << 8) | nb;
}

static inline u32 blend_pixel_fast(u32 dst, u32 sr, u32 sg, u32 sb, u32 alpha, u32 inv_alpha) {
    u32 dr = (dst >> 16) & 0xFF;
    u32 dg = (dst >> 8) & 0xFF;
    u32 db = dst & 0xFF;
    u32 nr = (sr * alpha + dr * inv_alpha + 128) >> 8;
    u32 ng = (sg * alpha + dg * inv_alpha + 128) >> 8;
    u32 nb = (sb * alpha + db * inv_alpha + 128) >> 8;
    return (nr << 16) | (ng << 8) | nb;
}

void fb_rect_alpha(int x, int y, int w, int h, u32 color, u8 alpha) {
    if (alpha >= 255) {
        fb_rect(x, y, w, h, color);
        return;
    }
    if (alpha == 0 || !fb.ready || w <= 0 || h <= 0) return;

    u8* base = (fb.drawing_backbuffer && fb.backbuffer) ? fb.backbuffer : fb.address;
    if (!base) return;

    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > (int)fb.width)  x1 = (int)fb.width;
    int y1 = y + h; if (y1 > (int)fb.height) y1 = (int)fb.height;
    if (clip_enabled) {
        if (x0 < clip_x0) x0 = clip_x0;
        if (y0 < clip_y0) y0 = clip_y0;
        if (x1 > clip_x1) x1 = clip_x1;
        if (y1 > clip_y1) y1 = clip_y1;
    }
    if (x0 >= x1 || y0 >= y1) return;

    u32 inv_alpha = 255 - alpha;

    if (fb.bpp == 32) {
        if ((color & 0x00FFFFFFu) == 0) {
            for (int r = y0; r < y1; r++) {
                u32 *dst_row = (u32*)(base + (u32)r * fb.pitch);
                for (int c = x0; c < x1; c++) {
                    dst_row[c] = blend_pixel_black_fast(dst_row[c], inv_alpha);
                }
            }
        } else {
            u32 sr = (color >> 16) & 0xFF;
            u32 sg = (color >> 8) & 0xFF;
            u32 sb = color & 0xFF;
            for (int r = y0; r < y1; r++) {
                u32 *dst_row = (u32*)(base + (u32)r * fb.pitch);
                for (int c = x0; c < x1; c++) {
                    dst_row[c] = blend_pixel_fast(dst_row[c], sr, sg, sb, alpha, inv_alpha);
                }
            }
        }
    }
    if (base == fb.backbuffer) mark_dirty_rect(x0, y0, x1 - x0, y1 - y0);
}

void fb_border(int x, int y, int w, int h, u32 color) {
    if (w <= 0 || h <= 0) return;
    fb_rect(x, y, w, 1, color); fb_rect(x, y + h - 1, w, 1, color); fb_rect(x, y, 1, h, color); fb_rect(x + w - 1, y, 1, h, color);
}

static int get_corner_offset(int dy, int r) {
    if (dy < 0 || dy >= r) return 0;
    int yd = r - 1 - dy;
    int r2 = r * r;
    int yd2 = yd * yd;
    int dx = 0;
    while (dx < r && (r - dx) * (r - dx) + yd2 > r2) {
        dx++;
    }
    return dx;
}

void fb_round_rect(int x, int y, int w, int h, int r, u32 color) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) { fb_rect(x, y, w, h, color); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    for (int dy = 0; dy < r; dy++) {
        int off = get_corner_offset(dy, r);
        if (w > off * 2) fb_rect(x + off, y + dy, w - off * 2, 1, color);
    }
    if (h > r * 2) {
        fb_rect(x, y + r, w, h - r * 2, color);
    }
    for (int dy = 0; dy < r; dy++) {
        int off = get_corner_offset(r - 1 - dy, r);
        if (w > off * 2) fb_rect(x + off, y + h - r + dy, w - off * 2, 1, color);
    }
}

void fb_round_rect_alpha(int x, int y, int w, int h, int r, u32 color, u8 alpha) {
    if (w <= 0 || h <= 0 || alpha == 0) return;
    if (alpha >= 255) { fb_round_rect(x, y, w, h, r, color); return; }
    if (r <= 0) { fb_rect_alpha(x, y, w, h, color, alpha); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    for (int dy = 0; dy < r; dy++) {
        int off = get_corner_offset(dy, r);
        if (w > off * 2) fb_rect_alpha(x + off, y + dy, w - off * 2, 1, color, alpha);
    }
    if (h > r * 2) {
        fb_rect_alpha(x, y + r, w, h - r * 2, color, alpha);
    }
    for (int dy = 0; dy < r; dy++) {
        int off = get_corner_offset(r - 1 - dy, r);
        if (w > off * 2) fb_rect_alpha(x + off, y + h - r + dy, w - off * 2, 1, color, alpha);
    }
}

void fb_round_rect_top(int x, int y, int w, int h, int r, u32 color) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) { fb_rect(x, y, w, h, color); return; }
    if (r * 2 > w) r = w / 2;
    if (r > h) r = h;

    for (int dy = 0; dy < r; dy++) {
        int off = get_corner_offset(dy, r);
        if (w > off * 2) fb_rect(x + off, y + dy, w - off * 2, 1, color);
    }
    if (h > r) {
        fb_rect(x, y + r, w, h - r, color);
    }
}

void fb_round_rect_top_alpha(int x, int y, int w, int h, int r, u32 color, u8 alpha) {
    if (w <= 0 || h <= 0 || alpha == 0) return;
    if (alpha >= 255) { fb_round_rect_top(x, y, w, h, r, color); return; }
    if (r <= 0) { fb_rect_alpha(x, y, w, h, color, alpha); return; }
    if (r * 2 > w) r = w / 2;
    if (r > h) r = h;

    for (int dy = 0; dy < r; dy++) {
        int off = get_corner_offset(dy, r);
        if (w > off * 2) fb_rect_alpha(x + off, y + dy, w - off * 2, 1, color, alpha);
    }
    if (h > r) {
        fb_rect_alpha(x, y + r, w, h - r, color, alpha);
    }
}

void fb_round_rect_bottom(int x, int y, int w, int h, int r, u32 color) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) { fb_rect(x, y, w, h, color); return; }
    if (r * 2 > w) r = w / 2;
    if (r > h) r = h;

    if (h > r) {
        fb_rect(x, y, w, h - r, color);
    }
    for (int dy = 0; dy < r; dy++) {
        int off = get_corner_offset(r - 1 - dy, r);
        if (w > off * 2) fb_rect(x + off, y + h - r + dy, w - off * 2, 1, color);
    }
}

void fb_round_rect_bottom_alpha(int x, int y, int w, int h, int r, u32 color, u8 alpha) {
    if (w <= 0 || h <= 0 || alpha == 0) return;
    if (alpha >= 255) { fb_round_rect_bottom(x, y, w, h, r, color); return; }
    if (r <= 0) { fb_rect_alpha(x, y, w, h, color, alpha); return; }
    if (r * 2 > w) r = w / 2;
    if (r > h) r = h;

    if (h > r) {
        fb_rect_alpha(x, y, w, h - r, color, alpha);
    }
    for (int dy = 0; dy < r; dy++) {
        int off = get_corner_offset(r - 1 - dy, r);
        if (w > off * 2) fb_rect_alpha(x + off, y + h - r + dy, w - off * 2, 1, color, alpha);
    }
}

void fb_round_rect_border(int x, int y, int w, int h, int r, u32 color) {
    if (w <= 0 || h <= 0) return;
    if (r <= 0) { fb_border(x, y, w, h, color); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    for (int dy = 0; dy < r; dy++) {
        int off = get_corner_offset(dy, r);
        int next_off = (dy + 1 < r) ? get_corner_offset(dy + 1, r) : 0;
        int span = (next_off > off) ? next_off - off : 1;
        fb_rect(x + off, y + dy, span, 1, color);
        fb_rect(x + w - off - span, y + dy, span, 1, color);
    }
    int top_off = get_corner_offset(0, r);
    if (w > top_off * 2) {
        fb_rect(x + top_off, y, w - top_off * 2, 1, color);
    }

    if (h > r * 2) {
        fb_rect(x, y + r, 1, h - r * 2, color);
        fb_rect(x + w - 1, y + r, 1, h - r * 2, color);
    }

    for (int dy = 0; dy < r; dy++) {
        int off = get_corner_offset(r - 1 - dy, r);
        int prev_off = (dy > 0) ? get_corner_offset(r - dy, r) : 0;
        int span = (prev_off > off) ? prev_off - off : 1;
        fb_rect(x + off, y + h - r + dy, span, 1, color);
        fb_rect(x + w - off - span, y + h - r + dy, span, 1, color);
    }
    int bot_off = get_corner_offset(0, r);
    if (w > bot_off * 2) {
        fb_rect(x + bot_off, y + h - 1, w - bot_off * 2, 1, color);
    }
}

static void fb_round_rect_alpha_hollow(int sx, int sy, int sw, int sh, int sr, int ix, int iy, int iw, int ih, u32 color, u8 alpha) {
    if (sw <= 0 || sh <= 0 || alpha == 0) return;
    if (sr * 2 > sw) sr = sw / 2;
    if (sr * 2 > sh) sr = sh / 2;

    for (int dy = 0; dy < sh; dy++) {
        int y = sy + dy;
        int off = 0;
        if (dy < sr) {
            off = get_corner_offset(dy, sr);
        } else if (dy >= sh - sr) {
            off = get_corner_offset(sh - 1 - dy, sr);
        }
        int row_x0 = sx + off;
        int row_x1 = sx + sw - off;
        if (row_x1 <= row_x0) continue;

        // If this row is completely above or below the inner window, draw the full horizontal span
        if (y < iy || y >= iy + ih) {
            fb_rect_alpha(row_x0, y, row_x1 - row_x0, 1, color, alpha);
        } else {
            // Row overlaps with the inner window horizontally [ix, ix + iw]
            // Left segment: from row_x0 to min(row_x1, ix)
            if (ix > row_x0) {
                int left_x1 = (row_x1 < ix) ? row_x1 : ix;
                fb_rect_alpha(row_x0, y, left_x1 - row_x0, 1, color, alpha);
            }
            // Right segment: from max(row_x0, ix + iw) to row_x1
            int right_x0 = (row_x0 > ix + iw) ? row_x0 : (ix + iw);
            if (row_x1 > right_x0) {
                fb_rect_alpha(right_x0, y, row_x1 - right_x0, 1, color, alpha);
            }
        }
    }
}

void fb_draw_window_shadow(int x, int y, int w, int h, int r, int is_focused) {
    if (w <= 0 || h <= 0) return;
    int layers = is_focused ? 5 : 3;
    int max_spread = is_focused ? 10 : 6;
    int dy_offset = is_focused ? 4 : 2;
    u8 base_alpha = is_focused ? 20 : 14;
    for (int i = 0; i < layers; i++) {
        int spread = max_spread - (i * max_spread) / layers;
        int off_y = (dy_offset * (layers - i)) / layers;
        u8 alpha = base_alpha + (i * (is_focused ? 15 : 10));
        fb_round_rect_alpha_hollow(x - spread, y - spread + off_y, w + spread * 2, h + spread * 2, r + spread,
                                   x, y, w, h, 0x000000, alpha);
    }
}

void fb_draw_card_shadow(int x, int y, int w, int h, int r) {
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < 3; i++) {
        int spread = 5 - i * 2;
        int off_y = 2 + (2 - i);
        u8 alpha = 14 + i * 16;
        fb_round_rect_alpha_hollow(x - spread, y - spread + off_y, w + spread * 2, h + spread * 2, r + spread,
                                   x, y, w, h, 0x000000, alpha);
    }
}

#define FB_CLIP_STACK_MAX 16
static struct {
    int x0, y0, x1, y1, enabled;
} s_clip_stack[FB_CLIP_STACK_MAX];
static int s_clip_stack_depth = 0;

void fb_set_clip(int x, int y, int w, int h) {
    int sx0 = x < 0 ? 0 : x;
    int sy0 = y < 0 ? 0 : y;
    int sx1 = x + w;
    int sy1 = y + h;
    if (sx1 > (int)fb.width) sx1 = (int)fb.width;
    if (sy1 > (int)fb.height) sy1 = (int)fb.height;
    clip_x0 = sx0; clip_y0 = sy0; clip_x1 = sx1; clip_y1 = sy1;
    clip_enabled = (clip_x1 > clip_x0 && clip_y1 > clip_y0);
}

void fb_reset_clip(void) { 
    clip_enabled = 0; 
    s_clip_stack_depth = 0;
}

void fb_push_clip(int x, int y, int w, int h) {
    if (s_clip_stack_depth < FB_CLIP_STACK_MAX) {
        s_clip_stack[s_clip_stack_depth].x0 = clip_x0;
        s_clip_stack[s_clip_stack_depth].y0 = clip_y0;
        s_clip_stack[s_clip_stack_depth].x1 = clip_x1;
        s_clip_stack[s_clip_stack_depth].y1 = clip_y1;
        s_clip_stack[s_clip_stack_depth].enabled = clip_enabled;
        s_clip_stack_depth++;
    }
    int sx0 = x < 0 ? 0 : x;
    int sy0 = y < 0 ? 0 : y;
    int sx1 = x + w;
    int sy1 = y + h;
    if (sx1 > (int)fb.width) sx1 = (int)fb.width;
    if (sy1 > (int)fb.height) sy1 = (int)fb.height;
    if (clip_enabled) {
        if (sx0 < clip_x0) sx0 = clip_x0;
        if (sy0 < clip_y0) sy0 = clip_y0;
        if (sx1 > clip_x1) sx1 = clip_x1;
        if (sy1 > clip_y1) sy1 = clip_y1;
    }
    clip_x0 = sx0; clip_y0 = sy0; clip_x1 = sx1; clip_y1 = sy1;
    clip_enabled = (clip_x1 > clip_x0 && clip_y1 > clip_y0);
}

void fb_pop_clip(void) {
    if (s_clip_stack_depth > 0) {
        s_clip_stack_depth--;
        clip_x0 = s_clip_stack[s_clip_stack_depth].x0;
        clip_y0 = s_clip_stack[s_clip_stack_depth].y0;
        clip_x1 = s_clip_stack[s_clip_stack_depth].x1;
        clip_y1 = s_clip_stack[s_clip_stack_depth].y1;
        clip_enabled = s_clip_stack[s_clip_stack_depth].enabled;
    } else {
        clip_enabled = 0;
    }
}

void fb_mark_dirty_full(void) {
    if (!fb.ready || !fb.backbuffer) return;
    dirty_valid = 1; dirty_x0 = 0; dirty_y0 = 0;
    dirty_x1 = fb.width; dirty_y1 = fb.height;
    s_dirty_rect_count = 1;
    s_dirty_rects[0].x = 0;
    s_dirty_rects[0].y = 0;
    s_dirty_rects[0].w = fb.width;
    s_dirty_rects[0].h = fb.height;
}

void fb_begin_frame(void) { if (fb.ready && fb.backbuffer) fb.drawing_backbuffer = 1; }

void fb_present(void) { 
    u32 y, x0, y0, x1, y1, row_bytes, bytes_per_pixel;
    u8* src;
    u8* dst;
    if (!fb.ready || !fb.backbuffer) return;
    if (!dirty_valid) {
        fb.drawing_backbuffer = 0; 
        return;
    }
    
    try_init_gpu2d();

    x0 = dirty_x0; y0 = dirty_y0; x1 = dirty_x1; y1 = dirty_y1;
    extern void compositor_record_present(int is_full, u32 w, u32 h);
    int is_full = ((x1 - x0) >= fb.width && (y1 - y0) >= fb.height);
    compositor_record_present(is_full, x1 - x0, y1 - y0);

    u32 bbox_w = x1 - x0;
    u32 bbox_h = y1 - y0;
    u32 bbox_area = bbox_w * bbox_h;
    u32 sum_rect_area = 0;
    for (int i = 0; i < s_dirty_rect_count; i++) {
        sum_rect_area += s_dirty_rects[i].w * s_dirty_rects[i].h;
    }

    int use_multi_rect = (s_dirty_rect_count > 1 && s_dirty_rect_count <= 4 && (sum_rect_area * 2 < bbox_area));

    if (gpu2d.ready) {
        int rc = 0;
        if (use_multi_rect) {
            for (int i = 0; i < s_dirty_rect_count; i++) {
                rc = virtio_gpu_2d_transfer_rect(&gpu2d, s_dirty_rects[i].x, s_dirty_rects[i].y,
                                                s_dirty_rects[i].w, s_dirty_rects[i].h);
                if (rc < 0) break;
            }
            if (rc >= 0) {
                rc = virtio_gpu_2d_flush_rect(&gpu2d, x0, y0, bbox_w, bbox_h);
                if (rc >= 0) {
                    extern void compositor_record_virtio(int batched, int transfers, int flushes);
                    compositor_record_virtio(0, s_dirty_rect_count, 1);
                }
            }
        } else {
            rc = virtio_gpu_2d_present(&gpu2d, x0, y0, bbox_w, bbox_h);
        }

        if (rc >= 0) {
            dirty_valid = 0;
            s_dirty_rect_count = 0;
            fb.drawing_backbuffer = 0; 
            return;
        }
        serial_write("[framebuffer] virtio-gpu present failed rc=");
        serial_write_hex((u32)rc);
        serial_write("\n");
        /* gpu2d.ready = 0; ignored timeout */
    }

    if (fb.bpp == 32) {
        if (use_multi_rect) {
            for (int i = 0; i < s_dirty_rect_count; i++) {
                u32 rx0 = s_dirty_rects[i].x;
                u32 ry0 = s_dirty_rects[i].y;
                u32 rx1 = rx0 + s_dirty_rects[i].w;
                u32 ry1 = ry0 + s_dirty_rects[i].h;
                u32 rspan = rx1 - rx0;
                for (y = ry0; y < ry1; y++) {
                    u32 *s = (u32*)(fb.backbuffer + y * fb.pitch) + rx0;
                    u32 *d = (u32*)(fb.address + y * fb.pitch) + rx0;
                    fast_memcpy32(d, s, rspan);
                }
            }
        } else {
            u32 span = x1 - x0;
            for (y = y0; y < y1; y++) {
                u32 *s = (u32*)(fb.backbuffer + y * fb.pitch) + x0;
                u32 *d = (u32*)(fb.address + y * fb.pitch) + x0;
                fast_memcpy32(d, s, span);
            }
        }
    } else {
        bytes_per_pixel = (fb.bpp + 7) / 8;
        row_bytes = (x1 - x0) * bytes_per_pixel;
        for (y = y0; y < y1; y++) {
            src = fb.backbuffer + y * fb.pitch + x0 * bytes_per_pixel;
            dst = fb.address + y * fb.pitch + x0 * bytes_per_pixel;
            for (u32 i = 0; i < row_bytes; i++) dst[i] = src[i];
        }
    }
    dirty_valid = 0;
    s_dirty_rect_count = 0;
    fb.drawing_backbuffer = 0; 
}

int fb_set_resolution(u32 width, u32 height) {
    u32 pitch = width * 4;
    u32 size = height * pitch;

    if (!width || !height || size > sizeof(framebuffer_backing)) {
        serial_write("[framebuffer] fb_set_resolution: size check failed w=");
        serial_write_hex(width);
        serial_write(" h=");
        serial_write_hex(height);
        serial_write("\n");
        return -1;
    }

    try_init_gpu2d();

    if (gpu2d.ready) {
        int rc = virtio_gpu_2d_set_resolution(&gpu2d, width, height);
        if (rc < 0) {
            serial_write("[framebuffer] fb_set_resolution: virtio-gpu set resolution failed rc=");
            serial_write_hex((u32)rc);
            serial_write("\n");
            return rc;
        }
    }

    fb.width = width;
    fb.height = height;
    fb.pitch = pitch;
    fb.buffer_size = size;

    clip_enabled = 0;
    fb_clear(0x000000);
    dirty_valid = 1;
    dirty_x0 = 0;
    dirty_y0 = 0;
    dirty_x1 = width;
    dirty_y1 = height;

    serial_write("[framebuffer] fb_set_resolution: ok w=");
    serial_write_hex(width);
    serial_write(" h=");
    serial_write_hex(height);
    serial_write("\n");
    return 0;
}

void fb_blit_image(int dst_x, int dst_y, const u32 *src_pixels, int src_w, int src_h) {
    if (!fb.ready || !src_pixels || src_w <= 0 || src_h <= 0) return;
    u8 *base = (fb.drawing_backbuffer && fb.backbuffer) ? fb.backbuffer : fb.address;
    if (!base) return;

    int x0 = dst_x < 0 ? 0 : dst_x;
    int y0 = dst_y < 0 ? 0 : dst_y;
    int x1 = dst_x + src_w; if (x1 > (int)fb.width)  x1 = (int)fb.width;
    int y1 = dst_y + src_h; if (y1 > (int)fb.height) y1 = (int)fb.height;

    if (clip_enabled) {
        if (x0 < clip_x0) x0 = clip_x0;
        if (y0 < clip_y0) y0 = clip_y0;
        if (x1 > clip_x1) x1 = clip_x1;
        if (y1 > clip_y1) y1 = clip_y1;
    }
    if (x0 >= x1 || y0 >= y1) return;

    u32 span = (u32)(x1 - x0);
    int src_start_x = x0 - dst_x;
    for (int y = y0; y < y1; y++) {
        int src_y = y - dst_y;
        const u32 *s = src_pixels + src_y * src_w + src_start_x;
        u32 *d = (u32*)(base + (u32)y * fb.pitch) + x0;
        fast_memcpy32(d, s, span);
    }

    if (base == fb.backbuffer) mark_dirty_rect(x0, y0, x1 - x0, y1 - y0);
}

void fb_blit_scaled(int dst_x, int dst_y, int dst_w, int dst_h, const u32 *src_pixels, int src_w, int src_h) {
    if (!fb.ready || !src_pixels || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return;
    u8 *base = (fb.drawing_backbuffer && fb.backbuffer) ? fb.backbuffer : fb.address;
    if (!base) return;

    /* Exact 1:1 match fast path */
    if (dst_w == src_w && dst_h == src_h) {
        fb_blit_image(dst_x, dst_y, src_pixels, src_w, src_h);
        return;
    }

    int x0 = dst_x < 0 ? 0 : dst_x;
    int y0 = dst_y < 0 ? 0 : dst_y;
    int x1 = dst_x + dst_w; if (x1 > (int)fb.width)  x1 = (int)fb.width;
    int y1 = dst_y + dst_h; if (y1 > (int)fb.height) y1 = (int)fb.height;

    if (clip_enabled) {
        if (x0 < clip_x0) x0 = clip_x0;
        if (y0 < clip_y0) y0 = clip_y0;
        if (x1 > clip_x1) x1 = clip_x1;
        if (y1 > clip_y1) y1 = clip_y1;
    }
    if (x0 >= x1 || y0 >= y1) return;

    /* 16.16 fixed-point scaling ratios for blazing fast lookups */
    u32 scale_x_fp = ((u32)src_w << 16) / (u32)dst_w;
    u32 scale_y_fp = ((u32)src_h << 16) / (u32)dst_h;

    /* Precalculate horizontal source index map to avoid multiplying per-pixel inside row loops */
    static u32 s_x_map[1920];
    int span = x1 - x0;
    if (span > 1920) span = 1920;
    for (int i = 0; i < span; i++) {
        u32 sx = (((u32)(x0 + i - dst_x) * scale_x_fp) >> 16);
        if (sx >= (u32)src_w) sx = (u32)src_w - 1;
        s_x_map[i] = sx;
    }

    for (int y = y0; y < y1; y++) {
        u32 sy = (((u32)(y - dst_y) * scale_y_fp) >> 16);
        if (sy >= (u32)src_h) sy = (u32)src_h - 1;
        const u32 *src_row = src_pixels + sy * src_w;
        u32 *dst_row = (u32*)(base + (u32)y * fb.pitch) + x0;

        int i = 0;
        /* 4x loop unrolling */
        for (; i <= span - 4; i += 4) {
            dst_row[i + 0] = src_row[s_x_map[i + 0]];
            dst_row[i + 1] = src_row[s_x_map[i + 1]];
            dst_row[i + 2] = src_row[s_x_map[i + 2]];
            dst_row[i + 3] = src_row[s_x_map[i + 3]];
        }
        for (; i < span; i++) {
            dst_row[i] = src_row[s_x_map[i]];
        }
    }

    if (base == fb.backbuffer) mark_dirty_rect(x0, y0, span, y1 - y0);
}
