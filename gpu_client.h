#ifndef GPU_CLIENT_H
#define GPU_CLIENT_H

#include "io.h"

#define GPU_CLIENT_MAX                     16
#define GPU_CLIENT_DEFAULT_QUOTA           (64 * 1024 * 1024ULL) /* 64 MB */
#define GPU_CLIENT_DEFAULT_MAX_INFLIGHT    2
#define GPU_CLIENT_DEFAULT_MAX_QUEUE_DEPTH 8

typedef enum {
    GPU_CLIENT_COMPOSITOR = 0,
    GPU_CLIENT_CURSOR,
    GPU_CLIENT_SYSTEM_UI,
    GPU_CLIENT_APPLICATION,
    GPU_CLIENT_RECOVERY,
    GPU_CLIENT_COUNT
} gpu_client_type_t;

typedef enum {
    GPU_CLIENT_ACTIVE = 0,
    GPU_CLIENT_THROTTLED,
    GPU_CLIENT_BLOCKED,
    GPU_CLIENT_DYING,
    GPU_CLIENT_DEAD
} gpu_client_state_t;

typedef struct {
    u32                client_id;
    gpu_client_type_t  type;
    gpu_client_state_t state;
    u32                priority;
    u32                weight;
    u64                quota_bytes;
    u64                used_bytes;
    u32                max_inflight;
    u32                inflight;
    u32                max_queue_depth;
    u32                queue_depth;
    u64                submitted;
    u64                completed;
    u64                dropped;
    u64                throttled;
    u64                coalesced;
    u32                generation;
} gpu_client_t;

typedef struct {
    u32                client_id;
    gpu_client_type_t  type;
    gpu_client_state_t state;
    u32                priority;
    u64                quota_bytes;
    u64                used_bytes;
    u32                inflight;
    u32                max_inflight;
    u32                queue_depth;
    u64                submitted;
    u64                completed;
    u64                dropped;
    u64                throttled;
    u64                coalesced;
    u32                generation;
} gpu_client_stats_t;

/* Client Management API */
int           gpu_client_subsystem_init(void);
gpu_client_t* gpu_client_register(gpu_client_type_t type);
int           gpu_client_unregister(u32 client_id);
gpu_client_t* gpu_client_get(u32 client_id);
int           gpu_set_client_quota(u32 client_id, u64 bytes, u32 max_inflight);
int           gpu_get_client_stats(u32 client_id, gpu_client_stats_t *out);
void          gpu_client_refresh_all_generations(u32 new_generation);
const char*   gpu_client_type_name(gpu_client_type_t type);
const char*   gpu_client_state_name(gpu_client_state_t state);
u32           gpu_client_count(void);

#endif /* GPU_CLIENT_H */
