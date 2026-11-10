#ifndef GPU_PRESENT_H
#define GPU_PRESENT_H

#include "gpu_driver.h"
#include "gpu_event.h"

typedef struct {
    int pending;
    int flush_requested;
    int vsync_enabled;
    int frame_in_flight;
    u64 frame_id;
    u64 last_present_tick;
    u32 max_fps;
    u32 coalesced_requests;
    u32 dropped_clean_frames;
} gpu_present_scheduler_t;

void gpu_present_scheduler_init(void);
void gpu_schedule_present(void);
int  gpu_flush_if_needed(void);
int  gpu_set_vsync(int enabled);
int  gpu_set_max_fps(u32 fps);
const gpu_present_scheduler_t* gpu_get_scheduler(void);
int  gpu_process_events(void);
void gpu_check_watchdog(void);

#endif
