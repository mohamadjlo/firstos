#ifndef GPU_CURSOR_H
#define GPU_CURSOR_H

#include "gpu_driver.h"

#define GPU_CURSOR_MAX_W 64
#define GPU_CURSOR_MAX_H 64

int  gpu_cursor_init(void);
int  gpu_cursor_set_shape(const void *pixels, u32 width, u32 height, u32 hotspot_x, u32 hotspot_y);
int  gpu_cursor_move(int x, int y);
int  gpu_cursor_set_visible(int visible);
const gpu_cursor_state_t* gpu_cursor_get_state(void);
void gpu_cursor_render_software(void *backbuffer, u32 screen_w, u32 screen_h, u32 pitch);

#endif
