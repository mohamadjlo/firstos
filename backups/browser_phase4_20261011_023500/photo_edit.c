#include "gui_internal.h"
#include "framebuffer.h"
#include "fs.h"
#include "font.h"

extern u8 *image_load(const char *path, int *w, int *h);
extern void kfree(void* ptr);
extern void *kmalloc(u32 size);

static char current_path[128];
static u32 *image_pixels = 0;
static int img_w = 0, img_h = 0;
static int is_raw = 0;

static void unload_image(void) {
    if (image_pixels) {
        kfree(image_pixels);
        image_pixels = 0;
    }
}

void photo_editor_open(const char *path) {
    unload_image();
    int i;
    for (i = 0; path[i] && i < 127; i++) current_path[i] = path[i];
    current_path[i] = 0;
    
    int ext = 0;
    for (i = 0; current_path[i]; i++) if (current_path[i] == '.') ext = i + 1;
    is_raw = (ext > 0 && current_path[ext] == 'r' && current_path[ext+1] == 'a' && current_path[ext+2] == 'w');
    
    if (is_raw) {
        img_w = 1024;
        img_h = 768;
        image_pixels = kmalloc(1024 * 768 * 4);
        if (image_pixels) {
            fs_read_raw_at(current_path, (char*)image_pixels, 1024 * 768 * 4 + 1);
        }
    } else {
        image_pixels = (u32*)image_load(current_path, &img_w, &img_h);
    }
}

static void apply_grayscale(void) {
    if (!image_pixels) return;
    for (int i = 0; i < img_w * img_h; i++) {
        u32 c = image_pixels[i];
        u32 r, g, b, a;
        if (is_raw) {
            r = (c >> 16) & 0xFF; g = (c >> 8) & 0xFF; b = c & 0xFF; a = (c >> 24) & 0xFF;
        } else {
            r = c & 0xFF; g = (c >> 8) & 0xFF; b = (c >> 16) & 0xFF; a = (c >> 24) & 0xFF;
        }
        u32 v = (r * 77 + g * 150 + b * 29) / 256;
        if (is_raw) {
            image_pixels[i] = (a << 24) | (v << 16) | (v << 8) | v;
        } else {
            image_pixels[i] = (a << 24) | (v << 16) | (v << 8) | v;
        }
    }
}

static void apply_invert(void) {
    if (!image_pixels) return;
    for (int i = 0; i < img_w * img_h; i++) {
        u32 c = image_pixels[i];
        u32 a = c & 0xFF000000;
        u32 rgb = c & 0x00FFFFFF;
        image_pixels[i] = a | (~rgb & 0x00FFFFFF);
    }
}

void photo_editor_draw(app_window_t *app) {
    int cx = app->frame.x + 2;
    int cy = app->frame.y + 22;
    int cw = app->frame.w - 4;
    int ch = app->frame.h - 24;
    
    fb_rect(cx, cy, cw, ch, 0x1A1A1A); // Dark bg
    
    // Toolbar
    int toolbar_h = 40;
    fb_rect(cx, cy, cw, toolbar_h, 0x2D2D30);
    fb_rect(cx, cy + toolbar_h, cw, 1, 0x111111);
    
    rect_t btn_gray = { cx + 10, cy + 8, 80, 24, 0 };
    rect_t btn_inv = { cx + 100, cy + 8, 80, 24, 0 };
    
    fb_rect(btn_gray.x, btn_gray.y, btn_gray.w, btn_gray.h, 0x404040);
    font_text(btn_gray.x + 8, btn_gray.y + 6, "Grayscale", 0xFFFFFF, 1);
    
    fb_rect(btn_inv.x, btn_inv.y, btn_inv.w, btn_inv.h, 0x404040);
    font_text(btn_inv.x + 15, btn_inv.y + 6, "Invert", 0xFFFFFF, 1);
    
    // Draw image
    if (image_pixels && img_w > 0 && img_h > 0) {
        int avail_h = ch - toolbar_h - 10;
        int avail_w = cw - 10;
        int scale = 1024;
        if (img_w > avail_w || img_h > avail_h) {
            int sx = (avail_w * 1024) / img_w;
            int sy = (avail_h * 1024) / img_h;
            scale = sx < sy ? sx : sy;
        }
        int dw = (img_w * scale) / 1024;
        int dh = (img_h * scale) / 1024;
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        
        int dx = cx + (cw - dw) / 2;
        int dy = cy + toolbar_h + 5 + (avail_h - dh) / 2;
        
        for (int y = 0; y < dh; y++) {
            int src_y = (y * img_h) / dh;
            u32 *src_row = image_pixels + (src_y * img_w);
            for (int x = 0; x < dw; x++) {
                int src_x = (x * img_w) / dw;
                u32 c = src_row[src_x];
                if (!is_raw) {
                    c = (c & 0xFF00FF00) | ((c & 0x00FF0000) >> 16) | ((c & 0x000000FF) << 16);
                }
                extern void fb_putpixel(int, int, unsigned int);
                fb_putpixel(dx + x, dy + y, c);
            }
        }
    } else {
        font_text_fit_center(cx, cy + ch/2, "No image loaded", 0xAAAAAA, cw);
    }
}

void photo_editor_click(app_window_t *app, int mx, int my) {
    int cx = app->frame.x + 2;
    int cy = app->frame.y + 22;
    
    rect_t btn_gray = { cx + 10, cy + 8, 80, 24, 0 };
    rect_t btn_inv = { cx + 100, cy + 8, 80, 24, 0 };
    
    if (mx >= btn_gray.x && mx <= btn_gray.x + btn_gray.w && my >= btn_gray.y && my <= btn_gray.y + btn_gray.h) {
        apply_grayscale();
        extern void fb_mark_dirty_full(void);
        fb_mark_dirty_full();
    }
    if (mx >= btn_inv.x && mx <= btn_inv.x + btn_inv.w && my >= btn_inv.y && my <= btn_inv.y + btn_inv.h) {
        apply_invert();
        extern void fb_mark_dirty_full(void);
        fb_mark_dirty_full();
    }
}
