#ifndef GPU_BUFFER_H
#define GPU_BUFFER_H

#include "gpu_driver.h"

/* Core Buffer Management APIs */
int           gpu_buffers_init(u32 width, u32 height, u32 bpp);
int           gpu_buffers_reallocate(u32 width, u32 height, u32 bpp);
void          gpu_buffers_shutdown(void);
int           gpu_buffers_drain(u32 timeout_ticks);
gpu_buffer_t* gpu_acquire_draw_buffer(void);
int           gpu_queue_buffer_present(gpu_buffer_t *buffer, const gpu_present_region_t *region);
int           gpu_poll_completions(void);
int           gpu_wait_buffer_available(u32 timeout_ticks);
gpu_buffer_t* gpu_get_draw_buffer(void);
gpu_buffer_t* gpu_get_scanout_buffer(void);

/* O(1) Metrics Query Getters */
u32           gpu_get_buffer_starvation(void);
u32           gpu_get_frames_queued(void);
u32           gpu_get_frames_completed(void);
u32           gpu_get_completion_timeouts(void);
u32           gpu_get_recovery_events(void);

/* Diagnostic & Recovery Hooks */
void          gpu_buffer_force_recovery(void);
void          gpu_buffer_simulate_alloc_fail(int fail);
int           gpu_buffer_is_scanout(const gpu_buffer_t *buf);
void          gpu_notify_frame_presented(void);
u32           gpu_buffers_quarantine_inflight(void);
int           gpu_buffers_recreate_after_recovery(u32 width, u32 height, u32 bpp);

#endif
