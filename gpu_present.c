#include "gpu_present.h"
#include "gpu_cursor.h"
#include "gpu_detect.h"
#include "gpu_buffer.h"
#include "compositor.h"
#include "framebuffer.h"
#include "serial.h"

static gpu_present_scheduler_t s_scheduler;

void gpu_present_scheduler_init(void) {
    s_scheduler.pending = 0;
    s_scheduler.flush_requested = 0;
    s_scheduler.vsync_enabled = 0;
    s_scheduler.frame_in_flight = 0;
    s_scheduler.frame_id = 0;
    s_scheduler.last_present_tick = 0;
    s_scheduler.max_fps = 60;
    s_scheduler.coalesced_requests = 0;
    s_scheduler.dropped_clean_frames = 0;

    serial_write("[gpu_phase4] present scheduler initialized\n");
    extern int gpu_scheduler_init(void);
    gpu_scheduler_init();
    gpu_cursor_init();

    extern int gpu_abi_init(void);
    extern void gpu_abi_register_default_clients(void);
    if (gpu_abi_init() == 0) {
        gpu_abi_register_default_clients();
    } else {
        serial_write("[gpu_phase10] ABI unavailable; kernel compositor path retained\n");
    }

    if (gpu_is_accelerated()) {
        serial_write("[gpu_present] vsync=0 async=0 coalescing=1\n");
    } else {
        serial_write("[gpu_present] bounded flush mode enabled\n");
    }
    serial_write("[gpu_phase4] runtime policy ready\n");
}

void gpu_schedule_present(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (act) act->present_requests++;

    if (s_scheduler.pending || s_scheduler.frame_in_flight) {
        s_scheduler.coalesced_requests++;
        if (act) act->presents_coalesced++;
        s_scheduler.flush_requested = 1;
        return;
    }

    s_scheduler.pending = 1;
    s_scheduler.flush_requested = 1;
}

int gpu_flush_if_needed(void) {
    if (!s_scheduler.flush_requested) return 0;

    active_gpu_device_t *act = gpu_get_active();

    /* Phase 7: Do not present if device is in recovery/lost/resetting/failed */
    if (!gpu_device_can_present()) {
        s_scheduler.flush_requested = 0;
        s_scheduler.pending = 0;
        return 0;
    }

    /* Phase 7: Poll health monitor and trigger recovery if pending */
    extern int gpu_health_poll(void);
    extern int gpu_recovery_trigger(gpu_reset_reason_t reason);
    if (gpu_health_poll() > 0) {
        gpu_recovery_trigger(GPU_RESET_TIMEOUT);
        s_scheduler.flush_requested = 0;
        s_scheduler.pending = 0;
        return 0;
    }

    /* Poll in-flight completions first */
    gpu_poll_completions();

    if (s_scheduler.frame_in_flight) {
        s_scheduler.coalesced_requests++;
        if (act) act->presents_coalesced++;
        return 0;
    }

    /* Check clean frame: skip present if no dirty rects or damage */
    int dirty_count = fb_get_dirty_rect_count();
    int dmg_count = compositor_damage_count();
    if (dirty_count <= 0 && dmg_count <= 0) {
        s_scheduler.dropped_clean_frames++;
        if (act) act->presents_dropped_clean++;
        s_scheduler.flush_requested = 0;
        s_scheduler.pending = 0;
        return 0;
    }

    /* Acquire draw buffer from Phase 5 Buffer Manager */
    gpu_buffer_t *draw_buf = gpu_acquire_draw_buffer();
    if (!draw_buf) {
        s_scheduler.coalesced_requests++;
        if (act) act->presents_coalesced++;
        return 0;
    }

    s_scheduler.frame_in_flight = 1;

    /* Render software cursor on draw buffer before presenting if active */
    if (draw_buf->cpu_addr && draw_buf->width && draw_buf->height) {
        gpu_cursor_render_software(draw_buf->cpu_addr, draw_buf->width, draw_buf->height, draw_buf->pitch);
    }

    /* Queue buffer present with dirty region or full screen */
    gpu_present_region_t region;
    u32 rx = 0, ry = 0, rw = 0, rh = 0;
    int has_rect = fb_get_dirty_rect(0, &rx, &ry, &rw, &rh);
    int is_partial = (has_rect && rw > 0 && rh > 0 && (rw < draw_buf->width || rh < draw_buf->height));

    int rc = 0;
    if (is_partial && gpu_supports_partial_present()) {
        region.x = rx;
        region.y = ry;
        region.w = rw;
        region.h = rh;
        rc = gpu_queue_buffer_present(draw_buf, &region);
    } else {
        region.x = 0;
        region.y = 0;
        region.w = draw_buf->width;
        region.h = draw_buf->height;
        rc = gpu_queue_buffer_present(draw_buf, &region);
    }

    if (rc < 0 && act) {
        act->present_retries++;
    }

    compositor_reset_damage();
    gpu_poll_completions();
    gpu_process_events();

    s_scheduler.frame_id++;
    s_scheduler.frame_in_flight = 0;
    s_scheduler.pending = 0;
    s_scheduler.flush_requested = 0;

    return (rc >= 0) ? 1 : 0;
}

static void write_dec_present(u32 val) {
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

int gpu_process_events(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act) return 0;

    gpu_event_t ev;
    int processed = 0;
    while (gpu_event_dequeue(&ev)) {
        processed++;

        /* Phase 7: Generation check - reject events from past generations */
        if (ev.generation < act->device_generation) {
            act->stale_frame_events++;
            serial_write("[gpu_recovery] stale generation event ignored\n");
            continue;
        }

        /* Phase 7: Handle asynchronous device error and loss events in scheduler context */
        if (ev.type == GPU_EVENT_DEVICE_LOST) {
            extern int gpu_recovery_trigger(gpu_reset_reason_t reason);
            gpu_recovery_trigger(GPU_RESET_DEVICE_LOSS);
            continue;
        }
        if (ev.type == GPU_EVENT_CMD_ERROR) {
            extern int gpu_recovery_trigger(gpu_reset_reason_t reason);
            gpu_recovery_trigger(GPU_RESET_QUEUE_ERROR);
            continue;
        }

        if (ev.type != GPU_EVENT_PRESENT_COMPLETE) continue;

        /* Validate event buffer index */
        if (ev.buffer_index >= act->buffer_count || !act->buffers) {
            act->stale_frame_events++;
            serial_write("[gpu_irq] stale completion ignored frame=");
            write_dec_present((u32)ev.frame_id);
            serial_write("\n");
            continue;
        }

        gpu_buffer_t *buf = &act->buffers[ev.buffer_index];

        /* Check for duplicate or stale frame_id */
        if (buf->state != GPU_BUFFER_SCANOUT && buf->state != GPU_BUFFER_QUEUED) {
            act->stale_frame_events++;
            serial_write("[gpu_irq] stale completion ignored frame=");
            write_dec_present((u32)ev.frame_id);
            serial_write("\n");
            continue;
        }

        if (ev.frame_id != buf->frame_id) {
            act->stale_frame_events++;
            serial_write("[gpu_irq] stale completion ignored frame=");
            write_dec_present((u32)ev.frame_id);
            serial_write("\n");
            continue;
        }

        /* Valid completion event processed in safe scheduler context */
        serial_write("[gpu_irq] completion event frame=");
        write_dec_present((u32)ev.frame_id);
        serial_write("\n");

        buf->state = GPU_BUFFER_COMPLETE;
        buf->device_owned = 0;
        act->frames_completed++;

        serial_write("[gpu_buffer] frame=");
        write_dec_present((u32)ev.frame_id);
        serial_write(" recycled buffer=");
        write_dec_present(ev.buffer_index);
        serial_write("\n");

        buf->state = GPU_BUFFER_FREE;

        /* Phase 7: Report healthy progress to health monitor */
        extern void gpu_health_report_progress(u64 frame_id);
        gpu_health_report_progress(ev.frame_id);
    }
    return processed;
}

void gpu_check_watchdog(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act || !act->buffers) return;

    for (u32 i = 0; i < act->buffer_count; i++) {
        gpu_buffer_t *buf = &act->buffers[i];
        if (buf->state == GPU_BUFFER_SCANOUT || buf->state == GPU_BUFFER_QUEUED) {
            act->watchdog_timeouts++;
            serial_write("[gpu_watchdog] frame=");
            write_dec_present((u32)buf->frame_id);
            serial_write(" completion timeout\n");
            serial_write("[gpu_watchdog] bounded poll retry=1\n");

            int polled = gpu_poll_completions();
            if (polled > 0 && buf->state == GPU_BUFFER_COMPLETE) {
                buf->state = GPU_BUFFER_FREE;
                return;
            }

            serial_write("[gpu_watchdog] recovery required\n");
            /* Phase 7: Trigger atomic fault-tolerant recovery transaction */
            extern int gpu_recovery_trigger(gpu_reset_reason_t reason);
            gpu_recovery_trigger(GPU_RESET_TIMEOUT);
            return;
        }
    }
}

int gpu_recover_submission_path(active_gpu_device_t *gpu) {
    if (!gpu) return -1;
    gpu->recovery_attempts++;

    /* 1. Stop new submissions */
    gpu->scheduler_mode = GPU_SCHED_RECOVERY;

    /* 2. Mask interrupts */
    if (gpu->driver && gpu->driver->ops.mask_events) {
        gpu->driver->ops.mask_events(gpu, 1);
    }

    /* 3. Drain remaining events from event queue */
    gpu_event_t ev;
    while (gpu_event_dequeue(&ev)) {}

    /* 4. Reset stalled buffers */
    for (u32 i = 0; i < gpu->buffer_count; i++) {
        if (gpu->buffers && (gpu->buffers[i].state == GPU_BUFFER_SCANOUT ||
            gpu->buffers[i].state == GPU_BUFFER_QUEUED ||
            gpu->buffers[i].state == GPU_BUFFER_ERROR)) {
            gpu->buffers[i].state = GPU_BUFFER_FREE;
            gpu->buffers[i].device_owned = 0;
            gpu->dropped_inflight_frames++;
        }
    }

    /* 5. Reinitialize event path */
    if (gpu->driver && gpu->driver->ops.setup_events) {
        gpu->driver->ops.setup_events(gpu);
    }

    /* 6. Restore scheduler */
    if (gpu->caps.has_completion_irq && gpu->irq_enabled) {
        gpu->scheduler_mode = GPU_SCHED_IRQ_ACTIVE;
    } else {
        gpu->scheduler_mode = GPU_SCHED_POLLING;
        gpu->polling_fallbacks++;
    }

    /* 7. Unmask interrupts */
    if (gpu->driver && gpu->driver->ops.mask_events) {
        gpu->driver->ops.mask_events(gpu, 0);
    }

    gpu->recovery_successes++;
    serial_write("[gpu_recovery] submission path successfully recovered\n");
    return 0;
}

int gpu_set_vsync(int enabled) {
    if (enabled && !gpu_get_caps()->has_vsync_irq) {
        serial_write("[gpu_present] vsync unavailable, using bounded flush policy\n");
        s_scheduler.vsync_enabled = 0;
        return -1;
    }
    s_scheduler.vsync_enabled = enabled;
    return 0;
}

int gpu_set_max_fps(u32 fps) {
    if (fps > 0 && fps <= 240) {
        s_scheduler.max_fps = fps;
        return 0;
    }
    return -1;
}

const gpu_present_scheduler_t* gpu_get_scheduler(void) {
    return &s_scheduler;
}

u32 gpu_get_present_requests(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->present_requests : 0;
}

u32 gpu_get_coalesced_requests(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->presents_coalesced : 0;
}

u32 gpu_get_cursor_fallbacks(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->cursor_fallbacks : 0;
}

u32 gpu_get_vsync_waits(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->vsync_waits : 0;
}

u32 gpu_get_irq_events(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->irq_events : 0;
}

u32 gpu_get_completion_irq_events(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->completion_irq_events : 0;
}

u32 gpu_get_events_dropped(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->events_dropped : 0;
}

u32 gpu_get_watchdog_timeouts(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->watchdog_timeouts : 0;
}

u32 gpu_get_recovery_attempts(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->recovery_attempts : 0;
}

u32 gpu_get_recovery_failures(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->recovery_failures : 0;
}
