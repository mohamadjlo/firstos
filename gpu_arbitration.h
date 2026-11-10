#ifndef GPU_ARBITRATION_H
#define GPU_ARBITRATION_H

#include "io.h"
#include "gpu_client.h"
#include "gpu_resource.h"
#include "gpu_driver.h"

#define GPU_REQUEST_POOL_CAPACITY          64
#define MAX_CONSECUTIVE_HIGH_PRIORITY      8

typedef enum {
    GPU_REQ_NEW = 0,
    GPU_REQ_QUEUED,
    GPU_REQ_ADMITTED,
    GPU_REQ_SUBMITTED,
    GPU_REQ_COMPLETED,
    GPU_REQ_DROPPED,
    GPU_REQ_CANCELLED,
    GPU_REQ_REJECTED
} gpu_request_state_t;

typedef struct gpu_request {
    u64                  request_id;
    u64                  frame_id;
    u32                  client_id;
    u32                  generation;
    gpu_resource_t      *resource;
    gpu_present_region_t region;
    u32                  base_priority;
    u32                  effective_priority;
    u32                  wait_ticks;
    u32                  cost;
    u32                  flags;
    u64                  user_cookie;
    gpu_request_state_t  state;
    struct gpu_request  *next;
} gpu_request_t;

typedef struct {
    u64 requests_submitted;
    u64 requests_completed;
    u64 requests_rejected;
    u64 requests_cancelled;
    u64 requests_coalesced;
    u64 requests_throttled;
    u64 requests_starved;
    u64 peak_queue_depth;
    u64 max_wait_ticks;
    u64 total_wait_ticks;
    u64 bytes_admitted;
    u64 quota_rejections;
    u64 generation_rejections;
    u64 resource_rejections;
    u64 fairness_forced_dispatches;
} gpu_scheduler_metrics_t;

/* Scheduler & Arbitration API */
int  gpu_scheduler_init(void);
int  gpu_submit_present(u32 client_id,
                        gpu_resource_t *resource,
                        const gpu_present_region_t *region,
                        u32 flags);
int  gpu_submit_present_cookie(u32 client_id,
                               gpu_resource_t *resource,
                               const gpu_present_region_t *region,
                               u32 flags,
                               u64 user_cookie,
                               u64 *out_request_id);
int  gpu_scheduler_submit(gpu_request_t *request);
int  gpu_scheduler_admit_next(gpu_request_t **out);
int  gpu_scheduler_complete(gpu_request_t *request, int status);
int  gpu_scheduler_cancel_client(u32 client_id);
int  gpu_cancel_requests(u32 client_id);
void gpu_scheduler_tick(void);

/* Recovery & Lifecycle integration */
void gpu_scheduler_pause_for_recovery(void);
void gpu_scheduler_refresh_generations(void);
void gpu_scheduler_resume(void);
int  gpu_scheduler_drain(u32 timeout_ticks);

/* Query & Metrics API */
int  gpu_get_scheduler_metrics(gpu_scheduler_metrics_t *out);
u32  gpu_get_queue_depth(u32 client_id);
int  gpu_get_client_metrics(u32 client_id, gpu_client_stats_t *out);
const char* gpu_request_state_name(gpu_request_state_t state);

#endif /* GPU_ARBITRATION_H */
