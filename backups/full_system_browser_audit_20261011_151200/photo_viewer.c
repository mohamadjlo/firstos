#include "photo_viewer.h"
#include "image.h"
#include "framebuffer.h"
#include "font.h"
#include "serial.h"

typedef unsigned char  u8;
typedef unsigned int   u32;

extern void fb_rect(int x, int y, int w, int h, u32 color);
extern void outline(int x, int y, int w, int h, u32 color);
extern void draw_label(int x, int y, const char *text, u32 color);
extern void font_text(int x, int y, const char *text, u32 color, int scale);
extern u32 *fb_pixels(void);
extern int  fb_width(void);
extern int  fb_height(void);
extern int  fb_pitch_px(void);

/* State */
static u8  *pv_pixels   = 0;
static int  pv_img_w    = 0;
static int  pv_img_h    = 0;
static int  pv_zoom     = 1;   /* 1=fit, 2=50%, 4=100% */
static int  pv_scroll_x = 0;
static int  pv_scroll_y = 0;
static char pv_path[256];
static int  pv_error    = 0;

static rect_t btn_zoom_in;
static rect_t btn_zoom_out;
static rect_t btn_fit;
static rect_t btn_100;

/* Helpers */
static void pv_str_copy(char *dst, const char *src, int max) {
    int i=0;
    while (src && src[i] && i<max-1) { dst[i]=src[i]; i++; }
    dst[i]=0;
}

static void pv_int_to_str(int n, char *buf, int max) {
    char tmp[16]; int i=0,j=0;
    if(n==0){buf[0]='0';buf[1]=0;return;}
    if(n<0){buf[j++]='-';n=-n;}
    while(n>0&&i<15){tmp[i++]='0'+(n%10);n/=10;}
    while(i>0&&j<max-1)buf[j++]=tmp[--i];
    buf[j]=0;
}

void photo_viewer_open(const char *path) {
    pv_error = 0;
    pv_scroll_x = 0;
    pv_scroll_y = 0;
    pv_zoom = 1;
    pv_str_copy(pv_path, path, sizeof(pv_path));
    serial_write("[photo_viewer] opening: ");
    serial_write(path);
    serial_write("\n");
    if (pv_pixels) { extern void kfree(void*); kfree(pv_pixels); pv_pixels = 0; }
    
    pv_pixels = image_load(path, &pv_img_w, &pv_img_h);
    if (!pv_pixels) {
        serial_write("[photo_viewer] image_load failed\n");
        pv_error = 1;
    } else {
        serial_write("[photo_viewer] loaded OK\n");
    }
}

static void pv_blit(int dst_x, int dst_y, int dst_w, int dst_h,
                    int src_scroll_x, int src_scroll_y, int zoom) {
    if (!pv_pixels || pv_img_w <= 0 || pv_img_h <= 0 || dst_w <= 0 || dst_h <= 0) return;

    if (zoom == 4 && src_scroll_x == 0 && src_scroll_y == 0 && dst_w == pv_img_w && dst_h == pv_img_h) {
        fb_blit_image(dst_x, dst_y, (const u32*)pv_pixels, pv_img_w, pv_img_h);
        return;
    }

    /* Use fast scaled blit when no scroll offset */
    if (src_scroll_x == 0 && src_scroll_y == 0 && zoom == 1) {
        int fit_w = dst_w;
        int fit_h = (pv_img_h * dst_w) / pv_img_w;
        if (fit_h > dst_h) {
            fit_h = dst_h;
            fit_w = (pv_img_w * dst_h) / pv_img_h;
        }
        int fx = dst_x + (dst_w - fit_w) / 2;
        int fy = dst_y + (dst_h - fit_h) / 2;
        fb_blit_scaled(fx, fy, fit_w, fit_h, (const u32*)pv_pixels, pv_img_w, pv_img_h);
        return;
    }

    u8  *bb   = fb.drawing_backbuffer && fb.backbuffer ? fb.backbuffer : fb.address;
    int  fbw  = (int)fb.width;
    int  fbh  = (int)fb.height;
    if (!bb) return;

    u32 scale_x_fp, scale_y_fp;
    if (zoom == 1) {
        scale_x_fp = ((u32)pv_img_w << 16) / (u32)dst_w;
        scale_y_fp = ((u32)pv_img_h << 16) / (u32)dst_h;
        if (scale_x_fp > scale_y_fp) scale_y_fp = scale_x_fp;
        else scale_x_fp = scale_y_fp;
    } else {
        scale_x_fp = (4u << 16) / (u32)zoom;
        scale_y_fp = scale_x_fp;
    }

    const u32 *src_u32 = (const u32*)pv_pixels;
    for (int dy = 0; dy < dst_h; dy++) {
        int fb_row = dst_y + dy;
        if (fb_row < 0 || fb_row >= fbh) continue;
        int src_row = (int)(((u32)dy * scale_y_fp) >> 16) + src_scroll_y;
        if (src_row < 0 || src_row >= pv_img_h) continue;

        u32 *fb_line = (u32*)(bb + fb_row * fb.pitch);
        const u32 *src_line = src_u32 + src_row * pv_img_w;

        for (int dx = 0; dx < dst_w; dx++) {
            int fb_col = dst_x + dx;
            if (fb_col < 0 || fb_col >= fbw) continue;
            int src_col = (int)(((u32)dx * scale_x_fp) >> 16) + src_scroll_x;
            if (src_col < 0 || src_col >= pv_img_w) continue;

            fb_line[fb_col] = src_line[src_col];
        }
    }
    fb_mark_dirty_rect(dst_x, dst_y, dst_w, dst_h);
}

void draw_photo_viewer(app_window_t *app) {
    int x = app->frame.x + 12;
    int y = app->frame.y + 42;
    int w = app->frame.w - 24;
    int h = app->frame.h - 54;

    /* Background */
    fb_rect(x, y, w, h, 0x1E1E1E);

    /* Toolbar */
    fb_rect(x, y, w, 30, 0x2D2D2D);
    outline(x, y, w, 30, 0x444444);

    btn_zoom_out = (rect_t){x + 8,   y + 6, 28, 20, 0};
    btn_zoom_in  = (rect_t){x + 40,  y + 6, 28, 20, 0};
    btn_fit      = (rect_t){x + 76,  y + 6, 44, 20, 0};
    btn_100      = (rect_t){x + 124, y + 6, 44, 20, 0};

    fb_rect(btn_zoom_out.x, btn_zoom_out.y, btn_zoom_out.w, btn_zoom_out.h, 0x444444);
    outline(btn_zoom_out.x, btn_zoom_out.y, btn_zoom_out.w, btn_zoom_out.h, 0x666666);
    draw_label(btn_zoom_out.x+10, btn_zoom_out.y+5, "-", 0xEEEEEE);

    fb_rect(btn_zoom_in.x, btn_zoom_in.y, btn_zoom_in.w, btn_zoom_in.h, 0x444444);
    outline(btn_zoom_in.x, btn_zoom_in.y, btn_zoom_in.w, btn_zoom_in.h, 0x666666);
    draw_label(btn_zoom_in.x+10, btn_zoom_in.y+5, "+", 0xEEEEEE);

    fb_rect(btn_fit.x, btn_fit.y, btn_fit.w, btn_fit.h, pv_zoom==1?0x3B5E8C:0x444444);
    outline(btn_fit.x, btn_fit.y, btn_fit.w, btn_fit.h, 0x666666);
    draw_label(btn_fit.x+6, btn_fit.y+5, "FIT", 0xEEEEEE);

    fb_rect(btn_100.x, btn_100.y, btn_100.w, btn_100.h, pv_zoom==4?0x3B5E8C:0x444444);
    outline(btn_100.x, btn_100.y, btn_100.w, btn_100.h, 0x666666);
    draw_label(btn_100.x+6, btn_100.y+5, "100%", 0xEEEEEE);

    /* Zoom info */
    {
        char info[32]; char tmp[12];
        info[0]=0;
        if (pv_img_w > 0) {
            pv_int_to_str(pv_img_w, tmp, sizeof(tmp));
            int i=0; while(tmp[i]) { info[i]=tmp[i]; i++; }
            info[i++]='x';
            pv_int_to_str(pv_img_h, tmp, sizeof(tmp));
            int j=0; while(tmp[j]&&i<30) { info[i++]=tmp[j++]; }
            info[i]=0;
        }
        draw_label(x+w-80, y+10, info, 0xAAAAAA);
    }

    /* Image area */
    int ia_x = x + 4;
    int ia_y = y + 34;
    int ia_w = w - 8;
    int ia_h = h - 38;

    if (pv_error || !pv_pixels) {
        /* Error state */
        draw_label(ia_x + ia_w/2 - 60, ia_y + ia_h/2 - 6, "Cannot load image", 0xFF6666);
        draw_label(ia_x + ia_w/2 - 40, ia_y + ia_h/2 + 10, pv_path, 0x888888);
    } else {
        pv_blit(ia_x, ia_y, ia_w, ia_h, pv_scroll_x, pv_scroll_y, pv_zoom);
    }
}

static int pv_in(int mx, int my, rect_t r) {
    return mx>=r.x && mx<r.x+r.w && my>=r.y && my<r.y+r.h;
}

void handle_photo_viewer_click(app_window_t *app, int mx, int my) {
    if (pv_in(mx, my, btn_zoom_out)) {
        if (pv_zoom > 1) pv_zoom--;
        return;
    }
    if (pv_in(mx, my, btn_zoom_in)) {
        if (pv_zoom < 8) pv_zoom++;
        return;
    }
    if (pv_in(mx, my, btn_fit)) {
        pv_zoom = 1; pv_scroll_x = 0; pv_scroll_y = 0;
        return;
    }
    if (pv_in(mx, my, btn_100)) {
        pv_zoom = 4; pv_scroll_x = 0; pv_scroll_y = 0;
        return;
    }
}
