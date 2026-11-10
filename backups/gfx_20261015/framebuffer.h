#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef struct { u8 red; u8 green; u8 blue; } fb_palette_entry_t;

typedef struct {
    u8* address;
    u8* backbuffer;
    u32 width;
    u32 height;
    u32 pitch;
    u32 bpp;
    u32 buffer_size;
    u8 type;
    u8 red_position;
    u8 red_mask;
    u8 green_position;
    u8 green_mask;
    u8 blue_position;
    u8 blue_mask;
    fb_palette_entry_t* palette;
    u16 palette_size;
    int ready;
    int drawing_backbuffer;
} framebuffer_t;

extern framebuffer_t fb;

void framebuffer_init_virtual(u32 width, u32 height);
void framebuffer_init(u64 addr, u32 width, u32 height, u32 pitch, u32 bpp, u8 type,
                      u8 red_position, u8 red_mask, u8 green_position, u8 green_mask,
                      u8 blue_position, u8 blue_mask, u32 palette_addr, u16 palette_size);
void fb_putpixel(int x, int y, u32 color);
void fb_clear(u32 color);
void fb_rect(int x, int y, int w, int h, u32 color);
void fb_rect_alpha(int x, int y, int w, int h, u32 color, u8 alpha);
u32 fb_read_pixel(int x, int y);
void fb_border(int x, int y, int w, int h, u32 color);
void fb_round_rect(int x, int y, int w, int h, int r, u32 color);
void fb_round_rect_alpha(int x, int y, int w, int h, int r, u32 color, u8 alpha);
void fb_round_rect_top(int x, int y, int w, int h, int r, u32 color);
void fb_round_rect_top_alpha(int x, int y, int w, int h, int r, u32 color, u8 alpha);
void fb_round_rect_bottom(int x, int y, int w, int h, int r, u32 color);
void fb_round_rect_bottom_alpha(int x, int y, int w, int h, int r, u32 color, u8 alpha);
void fb_round_rect_border(int x, int y, int w, int h, int r, u32 color);
void fb_draw_window_shadow(int x, int y, int w, int h, int r, int is_focused);
void fb_draw_card_shadow(int x, int y, int w, int h, int r);
void fb_begin_frame(void);
void fb_present(void);
void fb_set_clip(int x, int y, int w, int h);
void fb_reset_clip(void);
void fb_push_clip(int x, int y, int w, int h);
void fb_pop_clip(void);
void fb_mark_dirty_full(void);
void fb_mark_dirty_rect(int x, int y, int w, int h);
int fb_set_resolution(u32 width, u32 height);
void fast_memcpy32(void *dst, const void *src, u32 count);
void fast_fill32(void *dst, u32 val, u32 count);
void fb_blit_image(int dst_x, int dst_y, const u32 *src_pixels, int src_w, int src_h);
void fb_blit_scaled(int dst_x, int dst_y, int dst_w, int dst_h, const u32 *src_pixels, int src_w, int src_h);

#endif
