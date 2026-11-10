#include "gpu_health.h"
#include "gpu_detect.h"
#include "serial.h"

static gpu_health_monitor_t s_health;

void gpu_health_init(void) {
    s_health.consecutive_timeouts = 0;
    s_health.consecutive_irq_failures = 0;
    s_health.queue_error_count = 0;
    s_health.last_completed_frame = 0;
    s_health.last_progress_tick = 0;
    s_health.recovery_pending = 0;
    s_health.is_degraded = 0;
}

int gpu_health_poll(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act) return 0;

    /* If device is already in a non-ready state, do not trigger nested health alerts */
    if (act->device_state == GPU_DEVICE_LOST ||
        act->device_state == GPU_DEVICE_RESETTING ||
        act->device_state == GPU_DEVICE_REINITIALIZING ||
        act->device_state == GPU_DEVICE_FAILED) {
        return -1;
    }

    /* Check driver-specific hardware health probe if supported */
    if (act->driver && act->driver->ops.device_health_check) {
        int h = act->driver->ops.device_health_check(act);
        if (h < 0) {
            act->health_probe_failures++;
            s_health.recovery_pending = 1;
            return 1;
        }
    }

    /* Check threshold counters */
    if (s_health.consecutive_irq_failures >= GPU_HEALTH_MAX_IRQ_FAILURES) {
        serial_write("[gpu_health] consecutive irq failures=3 (threshold exceeded)\n");
        s_health.recovery_pending = 1;
        return 1;
    }
    if (s_health.consecutive_timeouts >= GPU_HEALTH_MAX_CONSECUTIVE_TIMEOUTS) {
        serial_write("[gpu_health] consecutive timeouts threshold exceeded\n");
        s_health.recovery_pending = 1;
        return 1;
    }
    if (s_health.queue_error_count >= GPU_HEALTH_MAX_QUEUE_ERRORS) {
        serial_write("[gpu_health] queue errors threshold exceeded\n");
        s_health.recovery_pending = 1;
        return 1;
    }

    return 0;
}

int gpu_health_is_degraded(void) {
    return s_health.is_degraded;
}

int gpu_health_recovery_pending(void) {
    return s_health.recovery_pending;
}

void gpu_health_report_progress(u64 frame_id) {
    s_health.consecutive_timeouts = 0;
    s_health.consecutive_irq_failures = 0;
    s_health.last_completed_frame = frame_id;
    s_health.recovery_pending = 0;
    s_health.last_progress_tick++;
}

void gpu_health_report_timeout(void) {
    s_health.consecutive_timeouts++;
}

void gpu_health_report_irq_failure(void) {
    s_health.consecutive_irq_failures++;
}

void gpu_health_report_queue_error(void) {
    s_health.queue_error_count++;
}

void gpu_health_reset(void) {
    s_health.consecutive_timeouts = 0;
    s_health.consecutive_irq_failures = 0;
    s_health.queue_error_count = 0;
    s_health.recovery_pending = 0;
    s_health.is_degraded = 0;
}

const gpu_health_monitor_t* gpu_health_get_monitor(void) {
    return &s_health;
}
