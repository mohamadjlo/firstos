#include "gpu_irq.h"
#include "gpu_detect.h"
#include "serial.h"

int gpu_irq_init(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act) return -1;

    act->irq_events = 0;
    act->completion_irq_events = 0;
    act->error_irq_events = 0;
    act->events_dropped = 0;
    act->spurious_events = 0;
    act->stale_frame_events = 0;
    act->irq_ack_failures = 0;
    act->watchdog_timeouts = 0;
    act->polling_fallbacks = 0;
    act->recovery_attempts = 0;
    act->recovery_successes = 0;
    act->recovery_failures = 0;
    act->irq_to_recycle_ticks = 0;
    act->irq_enabled = 0;

    int rc = -1;
    if (act->driver && act->driver->ops.setup_events) {
        rc = act->driver->ops.setup_events(act);
    }

    if (rc == 0 && act->caps.has_completion_irq) {
        act->irq_enabled = 1;
        return 0;
    }

    act->caps.has_completion_irq = 0;
    act->irq_enabled = 0;
    return -1;
}

int gpu_setup_completion_path(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act) return -1;

    if (act->caps.has_completion_irq && act->irq_enabled) {
        serial_write("[gpu_phase6] completion mode=irq\n");
        serial_write("[gpu_phase6] watchdog enabled\n");
        serial_write("[gpu_phase6] async submission ready\n");
        act->scheduler_mode = GPU_SCHED_IRQ_ACTIVE;
        return 1;
    }

    serial_write("[gpu_phase6] completion mode=bounded-polling\n");
    serial_write("[gpu_phase6] async IRQ unavailable; safe fallback active\n");
    act->scheduler_mode = GPU_SCHED_POLLING;
    act->polling_fallbacks++;
    return 0;
}

void gpu_irq_shutdown(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (act && act->driver && act->driver->ops.shutdown_events) {
        act->driver->ops.shutdown_events(act);
    }
    if (act) {
        act->irq_enabled = 0;
        act->caps.has_completion_irq = 0;
    }
}

void gpu_irq_mask(int masked) {
    active_gpu_device_t *act = gpu_get_active();
    if (act && act->driver && act->driver->ops.mask_events) {
        act->driver->ops.mask_events(act, masked);
    }
}

void gpu_irq_handler(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act || !act->irq_enabled) {
        if (act) act->spurious_events++;
        return;
    }

    act->irq_events++;

    /* 1. Acknowledge interrupt on device */
    if (act->driver && act->driver->ops.ack_events) {
        int ack = act->driver->ops.ack_events(act);
        if (ack < 0) {
            act->irq_ack_failures++;
            return;
        }
    }

    /* 2. Read events and enqueue them into lightweight ring buffer */
    if (act->driver && act->driver->ops.read_event) {
        gpu_event_t ev;
        int count = 0;
        while (act->driver->ops.read_event(act, &ev)) {
            gpu_event_enqueue(&ev);
            act->completion_irq_events++;
            count++;
            if (count >= GPU_EVENT_QUEUE_CAPACITY) break;
        }
    }
}

int gpu_irq_is_enabled(void) {
    active_gpu_device_t *act = gpu_get_active();
    return (act && act->irq_enabled);
}

void gpu_irq_simulate_event(u64 frame_id, u32 buffer_index, int status) {
    active_gpu_device_t *act = gpu_get_active();
    gpu_event_t ev;
    ev.type = GPU_EVENT_PRESENT_COMPLETE;
    ev.frame_id = frame_id;
    ev.buffer_index = buffer_index;
    ev.status = status;
    ev.timestamp = 0;
    ev.generation = act ? act->device_generation : 1;

    if (act) act->irq_events++;
    gpu_event_enqueue(&ev);
}
