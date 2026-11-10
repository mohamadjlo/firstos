#include "gpu_cursor.h"
#include "gpu_detect.h"
#include "serial.h"

static gpu_cursor_state_t s_cursor_state;
static u32 s_cursor_pixels[GPU_CURSOR_MAX_W * GPU_CURSOR_MAX_H];

/* Default 16x16 standard arrow cursor bitmap */
static const char *s_default_cursor_art[16] = {
    "X               ",
    "XX              ",
    "X.X             ",
    "X..X            ",
    "X...X           ",
    "X....X          ",
    "X.....X         ",
    "X......X        ",
    "X.......X       ",
    "X........X      ",
    "X.....XXXXX     ",
    "X..X..X         ",
    "X.X X..X        ",
    "XX   X..X       ",
    "X     X..X      ",
    "       XX       "
};

static void init_default_cursor_pixels(void) {
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            char ch = s_default_cursor_art[y][x];
            if (ch == 'X') {
                s_cursor_pixels[y * 16 + x] = 0xFF000000; /* opaque black outline */
            } else if (ch == '.') {
                s_cursor_pixels[y * 16 + x] = 0xFFFFFFFF; /* opaque white body */
            } else {
                s_cursor_pixels[y * 16 + x] = 0x00000000; /* transparent */
            }
        }
    }
}

int gpu_cursor_init(void) {
    s_cursor_state.enabled = 1;
    s_cursor_state.visible = 1;
    s_cursor_state.hardware = 0;
    s_cursor_state.x = 100;
    s_cursor_state.y = 100;
    s_cursor_state.width = 16;
    s_cursor_state.height = 16;
    s_cursor_state.hotspot_x = 0;
    s_cursor_state.hotspot_y = 0;

    init_default_cursor_pixels();
    serial_write("[gpu_cursor] software cursor initialized\n");

    active_gpu_device_t *act = gpu_get_active();
    if (act) {
        act->cursor_state = s_cursor_state;
    }

    if (gpu_supports_hw_cursor()) {
        int rc = -1;
        if (act && act->driver && act->driver->ops.cursor_create) {
            rc = act->driver->ops.cursor_create(act, s_cursor_pixels, 16, 16, 0, 0);
        }
        if (rc == 0) {
            s_cursor_state.hardware = 1;
            if (act) act->cursor_state.hardware = 1;
            serial_write("[gpu_cursor] hardware cursor enabled driver=");
            serial_write(gpu_active_name());
            serial_write("\n");
        } else {
            s_cursor_state.hardware = 0;
            if (act) {
                act->cursor_state.hardware = 0;
                act->cursor_fallbacks++;
            }
            serial_write("[gpu_cursor] hardware cursor failed, switching to software cursor\n");
        }
    } else {
        s_cursor_state.hardware = 0;
        if (act) act->cursor_state.hardware = 0;
        serial_write("[gpu_cursor] hardware cursor unavailable\n");
        serial_write("[gpu_cursor] using software cursor fallback\n");
    }

    return 0;
}

int gpu_cursor_set_shape(const void *pixels, u32 width, u32 height, u32 hotspot_x, u32 hotspot_y) {
    if (width == 0 || height == 0 || width > GPU_CURSOR_MAX_W || height > GPU_CURSOR_MAX_H) {
        return -1;
    }

    s_cursor_state.width = width;
    s_cursor_state.height = height;
    s_cursor_state.hotspot_x = hotspot_x;
    s_cursor_state.hotspot_y = hotspot_y;

    if (pixels) {
        const u32 *src = (const u32*)pixels;
        for (u32 i = 0; i < width * height; i++) {
            s_cursor_pixels[i] = src[i];
        }
    }

    active_gpu_device_t *act = gpu_get_active();
    if (act) {
        act->cursor_state = s_cursor_state;
        if (s_cursor_state.hardware && act->driver && act->driver->ops.cursor_create) {
            int rc = act->driver->ops.cursor_create(act, pixels, width, height, hotspot_x, hotspot_y);
            if (rc != 0) {
                s_cursor_state.hardware = 0;
                act->cursor_state.hardware = 0;
                act->cursor_fallbacks++;
                serial_write("[gpu_cursor] hardware cursor set shape failed, switching to software cursor\n");
            }
        }
    }

    /* Force redraw/damage at current position */
    int cx = s_cursor_state.x - (int)s_cursor_state.hotspot_x;
    int cy = s_cursor_state.y - (int)s_cursor_state.hotspot_y;
    extern void compositor_damage(int x, int y, int w, int h);
    extern void fb_mark_dirty_rect(int x, int y, int w, int h);
    compositor_damage(cx, cy, (int)width, (int)height);
    fb_mark_dirty_rect(cx, cy, (int)width, (int)height);
    gpu_schedule_present();
    return 0;
}

int gpu_cursor_move(int x, int y) {
    if (x == s_cursor_state.x && y == s_cursor_state.y) {
        return 0; /* suppress duplicate / identical moves */
    }

    active_gpu_device_t *act = gpu_get_active();

    if (s_cursor_state.hardware) {
        if (act && act->driver && act->driver->ops.cursor_move) {
            int rc = act->driver->ops.cursor_move(act, x, y);
            if (rc == 0) {
                s_cursor_state.x = x;
                s_cursor_state.y = y;
                if (act) {
                    act->cursor_hw_moves++;
                    act->cursor_state = s_cursor_state;
                }
                return 0;
            }
        }
        /* Fallback on hardware move failure */
        s_cursor_state.hardware = 0;
        if (act) {
            act->cursor_state.hardware = 0;
            act->cursor_fallbacks++;
        }
        serial_write("[gpu_cursor] hardware cursor move failed, switching to software cursor\n");
    }

    /* Software cursor path */
    if (act) act->cursor_sw_moves++;

    int old_x = s_cursor_state.x - (int)s_cursor_state.hotspot_x;
    int old_y = s_cursor_state.y - (int)s_cursor_state.hotspot_y;
    int old_w = (int)s_cursor_state.width;
    int old_h = (int)s_cursor_state.height;

    s_cursor_state.x = x;
    s_cursor_state.y = y;
    if (act) act->cursor_state = s_cursor_state;

    int new_x = x - (int)s_cursor_state.hotspot_x;
    int new_y = y - (int)s_cursor_state.hotspot_y;
    int new_w = (int)s_cursor_state.width;
    int new_h = (int)s_cursor_state.height;

    /* Generate damage for old and new cursor regions */
    extern void compositor_damage(int x, int y, int w, int h);
    extern void fb_mark_dirty_rect(int x, int y, int w, int h);
    compositor_damage(old_x, old_y, old_w, old_h);
    compositor_damage(new_x, new_y, new_w, new_h);
    fb_mark_dirty_rect(old_x, old_y, old_w, old_h);
    fb_mark_dirty_rect(new_x, new_y, new_w, new_h);

    gpu_schedule_present();
    return 0;
}

int gpu_cursor_set_visible(int visible) {
    if (s_cursor_state.visible == visible) return 0;
    s_cursor_state.visible = visible;

    active_gpu_device_t *act = gpu_get_active();
    if (act) act->cursor_state = s_cursor_state;

    if (s_cursor_state.hardware) {
        if (act && act->driver && act->driver->ops.cursor_show) {
            act->driver->ops.cursor_show(act, visible);
        }
    } else {
        int cx = s_cursor_state.x - (int)s_cursor_state.hotspot_x;
        int cy = s_cursor_state.y - (int)s_cursor_state.hotspot_y;
        extern void compositor_damage(int x, int y, int w, int h);
        extern void fb_mark_dirty_rect(int x, int y, int w, int h);
        compositor_damage(cx, cy, (int)s_cursor_state.width, (int)s_cursor_state.height);
        fb_mark_dirty_rect(cx, cy, (int)s_cursor_state.width, (int)s_cursor_state.height);
        gpu_schedule_present();
    }
    return 0;
}

const gpu_cursor_state_t* gpu_cursor_get_state(void) {
    return &s_cursor_state;
}

void gpu_cursor_render_software(void *backbuffer, u32 screen_w, u32 screen_h, u32 pitch) {
    if (!s_cursor_state.visible || s_cursor_state.hardware || !backbuffer) return;

    int start_x = s_cursor_state.x - (int)s_cursor_state.hotspot_x;
    int start_y = s_cursor_state.y - (int)s_cursor_state.hotspot_y;

    for (u32 cy = 0; cy < s_cursor_state.height; cy++) {
        int py = start_y + (int)cy;
        if (py < 0 || py >= (int)screen_h) continue;

        for (u32 cx = 0; cx < s_cursor_state.width; cx++) {
            int px = start_x + (int)cx;
            if (px < 0 || px >= (int)screen_w) continue;

            u32 col = s_cursor_pixels[cy * s_cursor_state.width + cx];
            u8 a = (col >> 24) & 0xFF;
            if (a == 0) continue;

            u32 *dst = (u32*)((u8*)backbuffer + py * pitch) + px;
            if (a == 255) {
                *dst = col & 0x00FFFFFF;
            } else {
                u32 bg = *dst;
                u32 r1 = (bg >> 16) & 0xFF, g1 = (bg >> 8) & 0xFF, b1 = bg & 0xFF;
                u32 r2 = (col >> 16) & 0xFF, g2 = (col >> 8) & 0xFF, b2 = col & 0xFF;
                u32 r = (r1 * (255 - a) + r2 * a) >> 8;
                u32 g = (g1 * (255 - a) + g2 * a) >> 8;
                u32 b = (b1 * (255 - a) + b2 * a) >> 8;
                *dst = (r << 16) | (g << 8) | b;
            }
        }
    }
}
