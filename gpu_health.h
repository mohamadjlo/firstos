#ifndef GPU_HEALTH_H
#define GPU_HEALTH_H

#include "gpu_driver.h"

#define GPU_HEALTH_MAX_CONSECUTIVE_TIMEOUTS 3
#define GPU_HEALTH_MAX_IRQ_FAILURES         3
#define GPU_HEALTH_MAX_QUEUE_ERRORS         2

typedef struct {
    u32 consecutive_timeouts;
    u32 consecutive_irq_failures;
    u32 queue_error_count;
    u64 last_completed_frame;
    u64 last_progress_tick;
    int recovery_pending;
    int is_degraded;
} gpu_health_monitor_t;

void                        gpu_health_init(void);
int                         gpu_health_poll(void);
int                         gpu_health_is_degraded(void);
int                         gpu_health_recovery_pending(void);
void                        gpu_health_report_progress(u64 frame_id);
void                        gpu_health_report_timeout(void);
void                        gpu_health_report_irq_failure(void);
void                        gpu_health_report_queue_error(void);
void                        gpu_health_reset(void);
const gpu_health_monitor_t* gpu_health_get_monitor(void);

#endif
