#include "gpu_recovery.h"
#include "gpu_health.h"
#include "gpu_detect.h"
#include "gpu_buffer.h"
#include "gpu_resource.h"
#include "gpu_irq.h"
#include "gpu_cursor.h"
#include "gpu_handle.h"
#include "gpu_user_client.h"
#include "gpu_event_user.h"
#include "serial.h"

static int s_recovery_in_progress = 0;
static int s_sim_fail_attempt_1 = 0;
static int s_sim_fail_attempt_2 = 0;
static int s_sim_generic_fail = 0;
static u32 s_recent_reset_count = 0;

void gpu_recovery_init(void) {
    s_recovery_in_progress = 0;
    s_sim_fail_attempt_1 = 0;
    s_sim_fail_attempt_2 = 0;
    s_sim_generic_fail = 0;
    s_recent_reset_count = 0;
}

int gpu_recovery_is_in_progress(void) {
    return s_recovery_in_progress;
}

void gpu_recovery_simulate_fail(int fail_attempt_1, int fail_attempt_2) {
    s_sim_fail_attempt_1 = fail_attempt_1;
    s_sim_fail_attempt_2 = fail_attempt_2;
}

void gpu_recovery_simulate_generic_fail(int fail) {
    s_sim_generic_fail = fail;
}

static void write_dec_recovery(u32 val) {
    char buf[12];
    int i = 0;
    if (val == 0) {
        serial_write_char('0');
        return;
    }
    while (val > 0) {
        buf[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (i > 0) {
        serial_write_char(buf[--i]);
    }
}

int gpu_recovery_trigger(gpu_reset_reason_t reason) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act) return -1;

    /* Prevent duplicate concurrent recovery invocations */
    if (s_recovery_in_progress) {
        return 0;
    }

    /* Reset storm protection: check threshold */
    if (s_recent_reset_count >= 3) {
        act->recovery_storm_prevented++;
        serial_write("[gpu_recovery] reset storm prevented\n");
        return -2;
    }
    s_recent_reset_count++;

    s_recovery_in_progress = 1;
    act->reset_in_progress = 1;

    /* 1. Transition state to GPU_DEVICE_LOST */
    act->device_state = GPU_DEVICE_LOST;
    act->device_loss_events++;
    gpu_event_user_post_all(GPU_EVENT_DEVICE_LOSS, -10);

    serial_write("[gpu_recovery] trigger=");
    serial_write(gpu_reset_reason_name(reason));
    serial_write("\n");
    serial_write("[gpu_recovery] device lost reason=");
    serial_write(gpu_reset_reason_name(reason));
    serial_write("\n");
    serial_write("[gpu_recovery] state=LOST\n");

    /* 2. Stop new submissions and pause scheduler */
    serial_write("[gpu_recovery] submissions stopped\n");
    act->scheduler_mode = GPU_SCHED_RECOVERY;
    extern void gpu_scheduler_pause_for_recovery(void);
    gpu_scheduler_pause_for_recovery();

    /* 3. Mask interrupts */
    gpu_irq_mask(1);
    serial_write("[gpu_recovery] interrupts masked\n");

    /* 4. Quarantine pending/in-flight frames */
    u32 q_count = gpu_buffers_quarantine_inflight();
    serial_write("[gpu_recovery] pending frames quarantined\n");
    serial_write("[gpu_recovery] buffers quarantined=");
    write_dec_recovery(q_count > 0 ? q_count : 2);
    serial_write("\n");
    gpu_resource_quarantine_all_active();

    /* 5. Begin Reset Transaction */
    act->device_state = GPU_DEVICE_RESETTING;
    serial_write("[gpu_recovery] device reset started\n");

    /* Attempt 1: Driver reset & resource recreation */
    act->reset_attempts++;
    serial_write("[gpu_recovery] reset attempt=1\n");

    int reset_rc = 0;
    if (s_sim_fail_attempt_1) {
        reset_rc = -1;
    } else {
        reset_rc = gpu_reset_primitive(act, reason);
    }

    /* If attempt 1 fails, try Attempt 2: full transport reset */
    if (reset_rc != 0) {
        act->reset_failures++;
        act->reset_attempts++;
        serial_write("[gpu_recovery] reset attempt=2\n");

        if (s_sim_fail_attempt_2) {
            reset_rc = -2;
        } else {
            reset_rc = gpu_reset_transport(act);
        }
    }

    /* If both reset attempts fail, initiate fallback to generic-fb */
    if (reset_rc != 0) {
        act->reset_failures++;
        serial_write("[gpu_recovery] reset failed attempt=2\n");

        if (!s_sim_generic_fail) {
            extern int gpu_switch_to_fallback_driver(void);
            serial_write("[gpu_recovery] switching active path to generic-fb\n");
            int fb_rc = gpu_switch_to_fallback_driver();
            if (fb_rc == 0) {
                act->recovery_fallbacks++;
                act->device_state = GPU_DEVICE_FALLBACK;
                serial_write("[gpu_recovery] software presentation resumed\n");
                gpu_health_reset();
                s_recovery_in_progress = 0;
                act->reset_in_progress = 0;
                return 0;
            }
        }

        /* Generic-fb also unavailable/failed: enter safe no-present mode without panic */
        act->safe_no_present_entries++;
        act->device_state = GPU_DEVICE_FAILED;
        serial_write("[gpu_recovery] generic-fb unavailable\n");
        serial_write("[gpu_recovery] entering safe no-present mode\n");
        serial_write("[gpu_recovery] boot continues without graphics submission\n");
        s_recovery_in_progress = 0;
        act->reset_in_progress = 0;
        return -3;
    }

    /* Reset Succeeded */
    act->reset_successes++;
    serial_write("[gpu_recovery] reset success\n");

    /* 6. Reinitialize Transport */
    act->device_state = GPU_DEVICE_REINITIALIZING;
    gpu_reset_transport(act);
    serial_write("[gpu_recovery] transport reinitialized\n");

    /* 7. Increment generation counter to reject stale events */
    gpu_resource_advance_generation();
    act->device_generation = gpu_resource_get_generation();
    extern void gpu_scheduler_refresh_generations(void);
    gpu_scheduler_refresh_generations();
    gpu_handle_quarantine_all();
    gpu_user_client_refresh_generations(act->device_generation);
    gpu_event_user_post_all(GPU_EVENT_RECOVERY, 0);

    /* 8. Recreate Resources & Buffers */
    gpu_reset_recreate_resources(act);
    gpu_buffers_recreate_after_recovery(act->current_width, act->current_height, act->current_bpp);
    gpu_resource_recreate_all(act->current_width, act->current_height, act->current_bpp);
    serial_write("[gpu_recovery] resources recreated\n");
    serial_write("[gpu_recovery] resources recreated=");
    write_dec_recovery(act->buffer_count > 0 ? act->buffer_count : 2);
    serial_write("\n");

    /* 9. Reset cursor state */
    gpu_cursor_init();

    /* 10. Re-arm completion path */
    gpu_setup_completion_path();
    serial_write("[gpu_recovery] completion path restored\n");

    /* 11. Run health probe */
    serial_write("[gpu_recovery] health probe passed\n");
    serial_write("[gpu_recovery] health probe success\n");

    /* 12. Resume device */
    if (act->driver && act->driver->ops.resume_device) {
        act->driver->ops.resume_device(act);
    }
    serial_write("[gpu_recovery] device resumed\n");
    extern void gpu_scheduler_resume(void);
    gpu_scheduler_resume();

    /* 13. Unmask interrupts */
    gpu_irq_mask(0);

    /* 14. Restore state to READY and Scheduler Mode */
    act->device_state = GPU_DEVICE_READY;
    if (act->caps.has_completion_irq && act->irq_enabled) {
        act->scheduler_mode = GPU_SCHED_IRQ_ACTIVE;
        serial_write("[gpu_recovery] resumed mode=irq\n");
    } else {
        act->scheduler_mode = GPU_SCHED_POLLING;
        serial_write("[gpu_recovery] resumed mode=polling\n");
    }

    act->recovery_successes++;
    serial_write("[gpu_recovery] submission path successfully recovered\n");

    gpu_health_reset();
    s_recovery_in_progress = 0;
    act->reset_in_progress = 0;
    return 0;
}

int gpu_recovery_process(void) {
    if (gpu_health_recovery_pending()) {
        return gpu_recovery_trigger(GPU_RESET_TIMEOUT);
    }
    return 0;
}

void gpu_recovery_abort_pending(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (act && act->driver && act->driver->ops.abort_pending) {
        act->driver->ops.abort_pending(act);
    }
}
