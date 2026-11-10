#include "gpu_event.h"
#include "gpu_detect.h"
#include "serial.h"

static gpu_event_queue_t s_event_queue;

void gpu_event_init(void) {
    s_event_queue.head = 0;
    s_event_queue.tail = 0;
    s_event_queue.dropped = 0;
    serial_write("[gpu_phase6] event queue initialized\n");
}

int gpu_event_enqueue(const gpu_event_t *ev) {
    if (!ev) return -1;

    u32 next = (s_event_queue.head + 1) % GPU_EVENT_QUEUE_CAPACITY;
    if (next == s_event_queue.tail) {
        /* Queue full: drop event non-blockingly, never spin inside IRQ */
        s_event_queue.dropped++;
        active_gpu_device_t *act = gpu_get_active();
        if (act) act->events_dropped++;
        return -2;
    }

    s_event_queue.entries[s_event_queue.head] = *ev;
    __asm__ volatile("" ::: "memory");
    s_event_queue.head = next;
    return 0;
}

int gpu_event_dequeue(gpu_event_t *out) {
    if (s_event_queue.head == s_event_queue.tail) {
        return 0;
    }

    if (out) {
        *out = s_event_queue.entries[s_event_queue.tail];
    }
    __asm__ volatile("" ::: "memory");
    s_event_queue.tail = (s_event_queue.tail + 1) % GPU_EVENT_QUEUE_CAPACITY;
    return 1;
}

u32 gpu_event_pending_count(void) {
    return (s_event_queue.head - s_event_queue.tail + GPU_EVENT_QUEUE_CAPACITY) % GPU_EVENT_QUEUE_CAPACITY;
}

u32 gpu_event_dropped_count(void) {
    return s_event_queue.dropped;
}

void gpu_event_queue_reset(void) {
    s_event_queue.head = 0;
    s_event_queue.tail = 0;
    s_event_queue.dropped = 0;
}

const char* gpu_event_type_name(gpu_event_type_t type) {
    switch (type) {
        case GPU_EVENT_NONE:             return "NONE";
        case GPU_EVENT_PRESENT_COMPLETE: return "PRESENT_COMPLETE";
        case GPU_EVENT_CMD_ERROR:        return "CMD_ERROR";
        case GPU_EVENT_DEVICE_RESET:     return "DEVICE_RESET";
        case GPU_EVENT_DEVICE_LOST:      return "DEVICE_LOST";
        case GPU_EVENT_HOTPLUG:          return "HOTPLUG";
        default:                         return "UNKNOWN";
    }
}

const gpu_event_queue_t* gpu_event_get_queue(void) {
    return &s_event_queue;
}
