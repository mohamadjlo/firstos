#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "framebuffer.h"

typedef unsigned int u32;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} comp_rect_t;

typedef struct {
    int id;
    int x;
    int y;
    int w;
    int h;
    u32 color;
    int visible;
} comp_surface_t;

void compositor_init(int screen_w, int screen_h);
int compositor_enabled(void);
int compositor_create_surface(int x, int y, int w, int h, u32 color);
void compositor_move_surface(int id, int x, int y);
void compositor_damage(int x, int y, int w, int h);
int compositor_damage_count(void);
const comp_rect_t *compositor_damage_at(int index);
void compositor_reset_damage(void);
int compositor_surface_count(void);
const comp_surface_t *compositor_surface_at(int index);

#include "io.h"

#define REDRAW_REASON_UNKNOWN       0
#define REDRAW_REASON_MOUSE_MOVE    1
#define REDRAW_REASON_MOUSE_BUTTON  2
#define REDRAW_REASON_WINDOW_DRAG   3
#define REDRAW_REASON_TEXT_BLINK    4
#define REDRAW_REASON_FPS_TICK      5
#define REDRAW_REASON_APP_UPDATE    6
#define REDRAW_REASON_WALLPAPER     7
#define REDRAW_REASON_COUNT         8

typedef struct {
    u32 frame_count;
    u32 full_repaints;
    u32 partial_repaints;
    u32 cursor_only_presents;
    u32 virtio_flushes;
    u32 virtio_transfers;
    u32 virtio_batched;
    u32 last_damage_w;
    u32 last_damage_h;
    u64 pixels_submitted_total;
    u32 mouse_moves_count;
    u32 keyboard_events_count;
    u32 dropped_frames;
    u32 redraw_reasons[REDRAW_REASON_COUNT];
    u32 current_fps;
    u32 current_mouse_rate;
    u32 mmio_notify_count;
    u64 bytes_submitted_total;
    u32 damage_rect_count_last;
    u32 idle_wakeups_total;
    u32 input_queue_depth;
    u32 dropped_events_total;
    u64 last_render_cycles;
    u64 last_present_cycles;
} compositor_stats_t;

void compositor_record_request(int reason);
void compositor_record_present(int is_full, u32 w, u32 h);
void compositor_record_present_detailed(int is_full, int is_cursor_only, u32 w, u32 h, u32 pixels);
void compositor_record_virtio(int batched, int transfers, int flushes);
void compositor_record_input(int is_mouse, int is_kb);
int  compositor_present(const void *backbuffer, u32 pitch);
void compositor_tick_1hz(void);
const compositor_stats_t *compositor_get_stats(void);
void compositor_log_stats(void);

#ifndef FIRSTOS_GFX_TELEMETRY_V2
#define FIRSTOS_GFX_TELEMETRY_V2 1
#endif

#if FIRSTOS_GFX_TELEMETRY_V2
static inline u64 gfx_rdtsc(void) {
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | (u64)lo;
}
void gfx_telemetry_render_begin(void);
void gfx_telemetry_render_end(void);
void gfx_telemetry_idle_wakeup(void);
void gfx_telemetry_record_damage_rects(u32 count);
void gfx_telemetry_record_input_queue(u32 depth, u32 dropped);
#else
#define gfx_telemetry_render_begin() ((void)0)
#define gfx_telemetry_render_end() ((void)0)
#define gfx_telemetry_idle_wakeup() ((void)0)
#define gfx_telemetry_record_damage_rects(c) ((void)0)
#define gfx_telemetry_record_input_queue(d, dr) ((void)0)
#endif

// New additions
void fb_blend_rect_simd(int x, int y, int w, int h, u32 color, unsigned char alpha);

#endif
