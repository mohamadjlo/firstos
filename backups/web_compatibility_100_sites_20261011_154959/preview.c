#include "preview.h"
#include "image.h"
#include "framebuffer.h"
#include "font.h"
#include "fs.h"
#include "heap.h"
#include "serial.h"

extern void fb_rect(int x, int y, int w, int h, u32 color);
extern void outline(int x, int y, int w, int h, u32 color);
extern void draw_label(int x, int y, const char *text, u32 color);
extern void font_text(int x, int y, const char *text, u32 color, int scale);
extern void font_text_fit(int x, int y, const char *text, u32 color, int max_w);
extern void font_text_fit_center(int x, int y, const char *text, u32 color, int max_w);
extern void font_icon(int x, int y, int icon, u32 color);
extern int explorer_is_image_file(const char *name);

#define ZOOM_FIT    0
#define ZOOM_100    1
#define ZOOM_CUSTOM 2

#define MAX_SIBLINGS 32

/* Preview App State */
static char  pv_path[128] = {0};
static char  pv_filename[64] = {0};
static char  pv_format_name[16] = {0};
static u8   *pv_pixels = 0;
static int   pv_img_w = 0;
static int   pv_img_h = 0;
static u32   pv_file_size = 0;
static int   pv_is_raw = 0;

static int   pv_zoom_mode = ZOOM_FIT;
static int   pv_zoom_pct = 100;
static int   pv_scroll_x = 0;
static int   pv_scroll_y = 0;

static int   pv_error = 0;
static char  pv_error_msg[64] = {0};

static char  pv_siblings[MAX_SIBLINGS][128];
static int   pv_sibling_count = 0;
static int   pv_sibling_idx = -1;

/* UI Buttons */
static rect_t btn_prev;
static rect_t btn_next;
static rect_t btn_open;
static rect_t btn_zoom_out;
static rect_t btn_zoom_in;
static rect_t btn_fit;
static rect_t btn_100;
static rect_t canvas_rect;

/* String helpers */
static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int str_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void int_to_str(int n, char *buf, int max) {
    char tmp[16]; int i = 0, j = 0;
    if (n == 0) { buf[0] = '0'; buf[1] = 0; return; }
    if (n < 0) { buf[j++] = '-'; n = -n; }
    while (n > 0 && i < 15) { tmp[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0 && j < max - 1) buf[j++] = tmp[--i];
    buf[j] = 0;
}

static void extract_filename(const char *path, char *out, int max) {
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    const char *fn = (last_slash >= 0) ? (path + last_slash + 1) : path;
    str_copy(out, fn, max);
}

static void extract_parent_dir(const char *path, char *out, int max) {
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    if (last_slash <= 0) {
        str_copy(out, "/", max);
    } else {
        int i = 0;
        while (i < last_slash && i < max - 1) { out[i] = path[i]; i++; }
        out[i] = 0;
    }
}

static void scan_directory_siblings(const char *current_file) {
    pv_sibling_count = 0;
    pv_sibling_idx = -1;

    char parent_dir[128];
    extract_parent_dir(current_file, parent_dir, sizeof(parent_dir));

    int count = fs_count_in_dir(parent_dir);
    for (int i = 0; i < count && pv_sibling_count < MAX_SIBLINGS; i++) {
        const fs_entry_t *entry = fs_entry_in_dir(parent_dir, i);
        if (!entry || entry->is_dir) continue;
        if (explorer_is_image_file(entry->name)) {
            str_copy(pv_siblings[pv_sibling_count], entry->path, 128);
            if (str_eq(entry->path, current_file)) {
                pv_sibling_idx = pv_sibling_count;
            }
            pv_sibling_count++;
        }
    }
}

void preview_init(void) {
    pv_path[0] = 0;
    pv_filename[0] = 0;
    pv_format_name[0] = 0;
    pv_pixels = 0;
    pv_img_w = 0;
    pv_img_h = 0;
    pv_file_size = 0;
    pv_is_raw = 0;
    pv_zoom_mode = ZOOM_FIT;
    pv_zoom_pct = 100;
    pv_scroll_x = 0;
    pv_scroll_y = 0;
    pv_error = 0;
    pv_sibling_count = 0;
    pv_sibling_idx = -1;
}

void preview_close(void) {
    if (pv_pixels) {
        image_free(pv_pixels);
        pv_pixels = 0;
    }
    pv_path[0] = 0;
    pv_filename[0] = 0;
    pv_img_w = 0;
    pv_img_h = 0;
    pv_file_size = 0;
    pv_error = 0;
}

void preview_open(const char *path) {
    if (!path || !path[0]) return;

    preview_close();

    str_copy(pv_path, path, sizeof(pv_path));
    extract_filename(path, pv_filename, sizeof(pv_filename));
    scan_directory_siblings(path);

    pv_zoom_mode = ZOOM_FIT;
    pv_zoom_pct = 100;
    pv_scroll_x = 0;
    pv_scroll_y = 0;
    pv_error = 0;

    serial_write("[preview] opening image: ");
    serial_write(path);
    serial_write("\n");

    /* Check if .raw */
    int len = 0; while (path[len]) len++;
    pv_is_raw = (len > 4 && path[len-4] == '.' && path[len-3] == 'r' && path[len-2] == 'a' && path[len-1] == 'w');

    /* Query file size and header */
    char header_sample[16] = {0};
    fs_read_at(path, header_sample, sizeof(header_sample));

    int total_files = fs_count();
    pv_file_size = 0;
    for (int i = 0; i < total_files; i++) {
        const fs_entry_t *e = fs_entry_at(i);
        if (e && str_eq(e->path, path)) {
            pv_file_size = e->size;
            break;
        }
    }

    /* Identify format name */
    const char *fmt = image_detect_format_name(path, (const u8*)header_sample, sizeof(header_sample));
    str_copy(pv_format_name, fmt, sizeof(pv_format_name));

    /* Load decoded RGBA pixels */
    pv_pixels = image_load(path, &pv_img_w, &pv_img_h);
    if (!pv_pixels || pv_img_w <= 0 || pv_img_h <= 0) {
        serial_write("[preview] failed to decode image\n");
        pv_error = 1;
        str_copy(pv_error_msg, "Unsupported format or corrupt image data", sizeof(pv_error_msg));
    } else {
        serial_write("[preview] image loaded successfully\n");
    }
}

static void pv_render_image(int dst_x, int dst_y, int dst_w, int dst_h) {
    u8 *bb = fb.backbuffer;
    int fbw = (int)fb.width;
    int fbh = (int)fb.height;

    if (!bb || !pv_pixels || pv_img_w <= 0 || pv_img_h <= 0) return;

    /* Compute destination width and height based on zoom */
    int disp_w, disp_h;
    if (pv_zoom_mode == ZOOM_FIT) {
        int scale_x = ((dst_w - 20) << 10) / pv_img_w;
        int scale_y = ((dst_h - 20) << 10) / pv_img_h;
        int scale = scale_x < scale_y ? scale_x : scale_y;
        if (scale > 1024) scale = 1024; /* Don't upscale past 100% in FIT mode */
        disp_w = (pv_img_w * scale) >> 10;
        disp_h = (pv_img_h * scale) >> 10;
    } else if (pv_zoom_mode == ZOOM_100) {
        disp_w = pv_img_w;
        disp_h = pv_img_h;
    } else {
        disp_w = (pv_img_w * pv_zoom_pct) / 100;
        disp_h = (pv_img_h * pv_zoom_pct) / 100;
    }

    if (disp_w < 1) disp_w = 1;
    if (disp_h < 1) disp_h = 1;

    /* Center within canvas plus scroll offset */
    int img_x = dst_x + (dst_w - disp_w) / 2 + pv_scroll_x;
    int img_y = dst_y + (dst_h - disp_h) / 2 + pv_scroll_y;

    /* Shadow / Frame behind image */
    fb_rect(img_x - 2, img_y - 2, disp_w + 4, disp_h + 4, 0x101113);
    outline(img_x - 1, img_y - 1, disp_w + 2, disp_h + 2, 0x36383E);

    /* Fixed point 16.16 sampling */
    int step_x = (pv_img_w << 16) / disp_w;
    int step_y = (pv_img_h << 16) / disp_h;

    int clip_x0 = dst_x > img_x ? dst_x : img_x;
    int clip_y0 = dst_y > img_y ? dst_y : img_y;
    int clip_x1 = (dst_x + dst_w) < (img_x + disp_w) ? (dst_x + dst_w) : (img_x + disp_w);
    int clip_y1 = (dst_y + dst_h) < (img_y + disp_h) ? (dst_y + dst_h) : (img_y + disp_h);

    if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) return;

    for (int cy = clip_y0; cy < clip_y1; cy++) {
        if (cy < 0 || cy >= fbh) continue;
        int src_y = ((cy - img_y) * step_y) >> 16;
        if (src_y < 0 || src_y >= pv_img_h) continue;

        u32 *fb_line = (u32*)(bb + cy * fb.pitch);
        u8 *src_row = pv_pixels + src_y * pv_img_w * 4;

        for (int cx = clip_x0; cx < clip_x1; cx++) {
            if (cx < 0 || cx >= fbw) continue;
            int src_x = ((cx - img_x) * step_x) >> 16;
            if (src_x < 0 || src_x >= pv_img_w) continue;

            u8 *p = src_row + src_x * 4;
            u32 r = p[0], g = p[1], b = p[2];
            /* Write standard 0x00RRGGBB pixel */
            fb_line[cx] = (r << 16) | (g << 8) | b;
        }
    }
}

void preview_draw(app_window_t *app) {
    int x = app->frame.x + 8;
    int y = app->frame.y + 36;
    int w = app->frame.w - 16;
    int h = app->frame.h - 44;

    /* Window Body Background */
    fb_rect(x, y, w, h, 0x1A1B1E);
    outline(x, y, w, h, 0x303238);

    /* Toolbar Panel (34px height) */
    int tb_h = 34;
    fb_rect(x, y, w, tb_h, 0x24262B);
    outline(x, y, w, tb_h, 0x383B42);

    /* Buttons Layout */
    btn_prev     = (rect_t){x + 8,   y + 5, 26, 24, 0};
    btn_next     = (rect_t){x + 38,  y + 5, 26, 24, 0};
    btn_open     = (rect_t){x + 70,  y + 5, 48, 24, 0};

    btn_zoom_out = (rect_t){x + w - 180, y + 5, 26, 24, 0};
    btn_zoom_in  = (rect_t){x + w - 150, y + 5, 26, 24, 0};
    btn_fit      = (rect_t){x + w - 120, y + 5, 38, 24, 0};
    btn_100      = (rect_t){x + w - 78,  y + 5, 44, 24, 0};

    /* Draw Navigation Buttons */
    int has_prev = (pv_sibling_count > 1 && pv_sibling_idx > 0);
    int has_next = (pv_sibling_count > 1 && pv_sibling_idx < pv_sibling_count - 1);

    fb_rect(btn_prev.x, btn_prev.y, btn_prev.w, btn_prev.h, has_prev ? 0x32353D : 0x2A2C32);
    outline(btn_prev.x, btn_prev.y, btn_prev.w, btn_prev.h, 0x40444E);
    draw_label(btn_prev.x + 9, btn_prev.y + 6, "<", has_prev ? 0xFFFFFF : 0x666870);

    fb_rect(btn_next.x, btn_next.y, btn_next.w, btn_next.h, has_next ? 0x32353D : 0x2A2C32);
    outline(btn_next.x, btn_next.y, btn_next.w, btn_next.h, 0x40444E);
    draw_label(btn_next.x + 9, btn_next.y + 6, ">", has_next ? 0xFFFFFF : 0x666870);

    /* Draw Open / Browse Button */
    fb_rect(btn_open.x, btn_open.y, btn_open.w, btn_open.h, 0x32353D);
    outline(btn_open.x, btn_open.y, btn_open.w, btn_open.h, 0x40444E);
    draw_label(btn_open.x + 9, btn_open.y + 6, "Open", 0xE0E4EC);

    /* Draw Zoom Buttons */
    fb_rect(btn_zoom_out.x, btn_zoom_out.y, btn_zoom_out.w, btn_zoom_out.h, 0x32353D);
    outline(btn_zoom_out.x, btn_zoom_out.y, btn_zoom_out.w, btn_zoom_out.h, 0x40444E);
    draw_label(btn_zoom_out.x + 9, btn_zoom_out.y + 6, "-", 0xFFFFFF);

    fb_rect(btn_zoom_in.x, btn_zoom_in.y, btn_zoom_in.w, btn_zoom_in.h, 0x32353D);
    outline(btn_zoom_in.x, btn_zoom_in.y, btn_zoom_in.w, btn_zoom_in.h, 0x40444E);
    draw_label(btn_zoom_in.x + 8, btn_zoom_in.y + 6, "+", 0xFFFFFF);

    fb_rect(btn_fit.x, btn_fit.y, btn_fit.w, btn_fit.h, pv_zoom_mode == ZOOM_FIT ? 0x1A73E8 : 0x32353D);
    outline(btn_fit.x, btn_fit.y, btn_fit.w, btn_fit.h, pv_zoom_mode == ZOOM_FIT ? 0x4285F4 : 0x40444E);
    draw_label(btn_fit.x + 8, btn_fit.y + 6, "FIT", 0xFFFFFF);

    fb_rect(btn_100.x, btn_100.y, btn_100.w, btn_100.h, pv_zoom_mode == ZOOM_100 ? 0x1A73E8 : 0x32353D);
    outline(btn_100.x, btn_100.y, btn_100.w, btn_100.h, pv_zoom_mode == ZOOM_100 ? 0x4285F4 : 0x40444E);
    draw_label(btn_100.x + 7, btn_100.y + 6, "100%", 0xFFFFFF);

    /* Middle Info Area */
    int info_x = x + 126;
    int info_w = btn_zoom_out.x - info_x - 12;
    if (info_w > 40 && pv_filename[0]) {
        /* Format Badge */
        int badge_w = 42;
        fb_rect(info_x, y + 7, badge_w, 20, 0x1E3A5A);
        outline(info_x, y + 7, badge_w, 20, 0x3B6899);
        font_text_fit_center(info_x, y + 11, pv_format_name[0] ? pv_format_name : "IMG", 0x8AB4F8, badge_w);

        /* Filename & Dimensions */
        char meta_str[96];
        char num_buf[16];
        str_copy(meta_str, pv_filename, 48);

        if (pv_img_w > 0 && pv_img_h > 0) {
            int mlen = 0; while (meta_str[mlen]) mlen++;
            meta_str[mlen++] = ' '; meta_str[mlen++] = '(';
            int_to_str(pv_img_w, num_buf, 16);
            int n = 0; while (num_buf[n]) meta_str[mlen++] = num_buf[n++];
            meta_str[mlen++] = 'x';
            int_to_str(pv_img_h, num_buf, 16);
            n = 0; while (num_buf[n]) meta_str[mlen++] = num_buf[n++];

            if (pv_file_size > 0) {
                meta_str[mlen++] = ','; meta_str[mlen++] = ' ';
                if (pv_file_size >= 1024 * 1024) {
                    int_to_str(pv_file_size / (1024 * 1024), num_buf, 16);
                    n = 0; while (num_buf[n]) meta_str[mlen++] = num_buf[n++];
                    meta_str[mlen++] = 'M'; meta_str[mlen++] = 'B';
                } else {
                    int_to_str(pv_file_size / 1024, num_buf, 16);
                    n = 0; while (num_buf[n]) meta_str[mlen++] = num_buf[n++];
                    meta_str[mlen++] = 'K'; meta_str[mlen++] = 'B';
                }
            }
            meta_str[mlen++] = ')';
            meta_str[mlen] = 0;
        }

        font_text_fit(info_x + badge_w + 8, y + 11, meta_str, 0xD0D4DE, info_w - badge_w - 8);
    }

    /* Canvas Viewport */
    canvas_rect = (rect_t){x + 2, y + tb_h + 2, w - 4, h - tb_h - 4, 0};
    fb_rect(canvas_rect.x, canvas_rect.y, canvas_rect.w, canvas_rect.h, 0x121316);

    if (pv_error) {
        /* Error State */
        int cy = canvas_rect.y + canvas_rect.h / 2 - 24;
        font_text_fit_center(canvas_rect.x, cy, "Cannot Load Image", 0xFF6B6B, canvas_rect.w);
        font_text_fit_center(canvas_rect.x, cy + 20, pv_error_msg[0] ? pv_error_msg : "File could not be decoded", 0x9AA0A6, canvas_rect.w);
        font_text_fit_center(canvas_rect.x, cy + 40, pv_path, 0x5F6368, canvas_rect.w);
    } else if (!pv_pixels || !pv_path[0]) {
        /* Welcome / Empty State */
        int cy = canvas_rect.y + canvas_rect.h / 2 - 40;
        font_icon(canvas_rect.x + canvas_rect.w / 2 - 16, cy - 20, ICON_PHOTO, 0x4285F4);
        font_text_fit_center(canvas_rect.x, cy + 18, "Preview Image Viewer", 0xE8EAED, canvas_rect.w);
        font_text_fit_center(canvas_rect.x, cy + 38, "Supports JPG, PNG, BMP, TIFF, GIF, and RAW", 0x9AA0A6, canvas_rect.w);

        /* Quick open sample button */
        rect_t btn_sample = {canvas_rect.x + canvas_rect.w / 2 - 60, cy + 62, 120, 26, 0};
        fb_rect(btn_sample.x, btn_sample.y, btn_sample.w, btn_sample.h, 0x1A73E8);
        outline(btn_sample.x, btn_sample.y, btn_sample.w, btn_sample.h, 0x4285F4);
        font_text_fit_center(btn_sample.x, btn_sample.y + 7, "Open Wallpaper", 0xFFFFFF, btn_sample.w);
    } else {
        /* Active Image Viewport */
        pv_render_image(canvas_rect.x, canvas_rect.y, canvas_rect.w, canvas_rect.h);
    }
}

static int point_in_btn(int mx, int my, rect_t r) {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}

void preview_handle_click(app_window_t *app, int mx, int my) {
    (void)app;

    if (point_in_btn(mx, my, btn_prev)) {
        if (pv_sibling_count > 1 && pv_sibling_idx > 0) {
            preview_open(pv_siblings[pv_sibling_idx - 1]);
        }
        return;
    }

    if (point_in_btn(mx, my, btn_next)) {
        if (pv_sibling_count > 1 && pv_sibling_idx < pv_sibling_count - 1) {
            preview_open(pv_siblings[pv_sibling_idx + 1]);
        }
        return;
    }

    if (point_in_btn(mx, my, btn_open)) {
        /* Open next image or default wallpaper */
        if (pv_sibling_count > 0) {
            int next_idx = (pv_sibling_idx + 1) % pv_sibling_count;
            preview_open(pv_siblings[next_idx]);
        } else {
            preview_open("/wallpapers/default_wallpaper.jpg");
        }
        return;
    }

    if (point_in_btn(mx, my, btn_zoom_out)) {
        pv_zoom_mode = ZOOM_CUSTOM;
        if (pv_zoom_pct > 25) pv_zoom_pct -= 25;
        return;
    }

    if (point_in_btn(mx, my, btn_zoom_in)) {
        pv_zoom_mode = ZOOM_CUSTOM;
        if (pv_zoom_pct < 400) pv_zoom_pct += 25;
        return;
    }

    if (point_in_btn(mx, my, btn_fit)) {
        pv_zoom_mode = ZOOM_FIT;
        pv_zoom_pct = 100;
        pv_scroll_x = 0;
        pv_scroll_y = 0;
        return;
    }

    if (point_in_btn(mx, my, btn_100)) {
        pv_zoom_mode = ZOOM_100;
        pv_zoom_pct = 100;
        pv_scroll_x = 0;
        pv_scroll_y = 0;
        return;
    }

    /* Empty state sample button */
    if (!pv_pixels) {
        int cy = canvas_rect.y + canvas_rect.h / 2 - 40;
        rect_t btn_sample = {canvas_rect.x + canvas_rect.w / 2 - 60, cy + 62, 120, 26, 0};
        if (point_in_btn(mx, my, btn_sample)) {
            preview_open("/wallpapers/default_wallpaper.jpg");
            if (!pv_pixels) preview_open("/wallpapers/default_wallpaper.raw");
            return;
        }
    }
}

void preview_handle_drag(app_window_t *app, int dx, int dy) {
    (void)app;
    if (pv_pixels && (pv_zoom_mode == ZOOM_100 || pv_zoom_mode == ZOOM_CUSTOM)) {
        pv_scroll_x += dx;
        pv_scroll_y += dy;
    }
}

void preview_handle_key(int key) {
    if (key == 17) { /* Left arrow */
        if (pv_sibling_count > 1 && pv_sibling_idx > 0) {
            preview_open(pv_siblings[pv_sibling_idx - 1]);
        }
    } else if (key == 18) { /* Right arrow */
        if (pv_sibling_count > 1 && pv_sibling_idx < pv_sibling_count - 1) {
            preview_open(pv_siblings[pv_sibling_idx + 1]);
        }
    } else if (key == '+' || key == '=') {
        pv_zoom_mode = ZOOM_CUSTOM;
        if (pv_zoom_pct < 400) pv_zoom_pct += 25;
    } else if (key == '-' || key == '_') {
        pv_zoom_mode = ZOOM_CUSTOM;
        if (pv_zoom_pct > 25) pv_zoom_pct -= 25;
    } else if (key == '0' || key == 'f' || key == 'F') {
        pv_zoom_mode = ZOOM_FIT;
        pv_zoom_pct = 100;
        pv_scroll_x = 0;
        pv_scroll_y = 0;
    }
}
