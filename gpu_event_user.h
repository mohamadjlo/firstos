#ifndef GPU_EVENT_USER_H
#define GPU_EVENT_USER_H

#include "io.h"
#include "gpu_abi.h"

#define GPU_USER_EVENT_QUEUE_SIZE 32

typedef struct {
    gpu_user_event_t events[GPU_USER_EVENT_QUEUE_SIZE];
    u32              head;
    u32              tail;
    u32              count;
    u64              delivered;
    u64              dropped;
} gpu_user_event_queue_t;

int  gpu_event_user_init(void);
int  gpu_event_user_post(u32 user_client_id, const gpu_user_event_t *event);
int  gpu_event_user_poll(u32 user_client_id, gpu_user_event_t *out_event);
int  gpu_event_user_wait(u32 user_client_id, u32 timeout_ticks, gpu_user_event_t *out_event);
void gpu_event_user_purge(u32 user_client_id);
void gpu_event_user_post_all(gpu_user_event_type_t event_type, int status);

/* Helpers for kernel scheduler / recovery callbacks */
void gpu_event_user_post_completion(u32 kernel_client_id, u64 request_id, u64 user_cookie, u32 res_gen);
void gpu_event_user_post_dropped(u32 kernel_client_id, u64 request_id, u64 user_cookie, int status);

#endif /* GPU_EVENT_USER_H */
