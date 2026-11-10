#ifndef GPU_EVENT_H
#define GPU_EVENT_H

#include "io.h"

typedef enum {
    GPU_EVENT_NONE = 0,
    GPU_EVENT_PRESENT_COMPLETE,
    GPU_EVENT_CMD_ERROR,
    GPU_EVENT_DEVICE_RESET,
    GPU_EVENT_DEVICE_LOST,
    GPU_EVENT_HOTPLUG
} gpu_event_type_t;

typedef struct {
    gpu_event_type_t type;
    u64              frame_id;
    u32              buffer_index;
    int              status;
    u64              timestamp;
    u32              generation;
} gpu_event_t;

#define GPU_EVENT_QUEUE_CAPACITY 64

typedef struct {
    gpu_event_t  entries[GPU_EVENT_QUEUE_CAPACITY];
    volatile u32 head;
    volatile u32 tail;
    volatile u32 dropped;
} gpu_event_queue_t;

void                     gpu_event_init(void);
int                      gpu_event_enqueue(const gpu_event_t *ev);
int                      gpu_event_dequeue(gpu_event_t *out);
u32                      gpu_event_pending_count(void);
u32                      gpu_event_dropped_count(void);
void                     gpu_event_queue_reset(void);
const char*              gpu_event_type_name(gpu_event_type_t type);
const gpu_event_queue_t* gpu_event_get_queue(void);

#endif
