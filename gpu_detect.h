#ifndef GPU_DETECT_H
#define GPU_DETECT_H

#include "gpu_driver.h"

void gpu_runtime_init(void);
int  gpu_detect_scan(void);
active_gpu_device_t* gpu_get_active(void);
const gpu_driver_t*  gpu_get_active_driver(void);
int  gpu_init_active(u32 width, u32 height);
int  gpu_present_active(const void* backbuffer, u32 width, u32 height, u32 pitch);
int  gpu_present_rect_active(const void* backbuffer, u32 x, u32 y, u32 w, u32 h, u32 pitch);
int  gpu_is_accelerated(void);
const char* gpu_active_name(void);
u32  gpu_get_active_width(void);
u32  gpu_get_active_height(void);
void gpu_notify_present(void);

/* Phase 3: Capabilities snapshot, Mode management, and Runtime Metrics */
const gpu_caps_t* gpu_get_caps(void);
int  gpu_supports_partial_present(void);
int  gpu_supports_hw_cursor(void);
int  gpu_supports_hw_blt(void);
int  gpu_set_active_resolution(u32 width, u32 height, u32 bpp);
u32  gpu_get_frames_presented(void);
u32  gpu_get_partial_presents(void);
u32  gpu_get_full_presents(void);
u32  gpu_get_fallback_events(void);

/* Phase 4: Unified Cursor, Present Scheduler & Metrics APIs */
int  gpu_cursor_init(void);
int  gpu_cursor_set_shape(const void *pixels, u32 width, u32 height, u32 hotspot_x, u32 hotspot_y);
int  gpu_cursor_move(int x, int y);
int  gpu_cursor_set_visible(int visible);
const gpu_cursor_state_t* gpu_cursor_get_state(void);

void gpu_present_scheduler_init(void);
void gpu_schedule_present(void);
int  gpu_flush_if_needed(void);
int  gpu_set_vsync(int enabled);
int  gpu_set_max_fps(u32 fps);

u32  gpu_get_present_requests(void);
u32  gpu_get_coalesced_requests(void);
u32  gpu_get_cursor_fallbacks(void);
u32  gpu_get_vsync_waits(void);

#endif
