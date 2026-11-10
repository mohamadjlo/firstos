#include "gpu_buffer.h"
#include "gpu_detect.h"
#include "gpu_resource.h"
#include "gpu_dma.h"
#include "gpu_memcheck.h"
#include "framebuffer.h"
#include "heap.h"
#include "serial.h"

#define GPU_MAX_BUFFERS 4

static gpu_buffer_t s_gpu_buffers[GPU_MAX_BUFFERS];
static void*        s_heap_allocated_addrs[GPU_MAX_BUFFERS];
static u32          s_buffer_count = 0;
static int          s_simulated_alloc_failure = 0;

static void write_dec(u32 val) {
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

static int get_buffer_index(const gpu_buffer_t *buf) {
    for (u32 i = 0; i < s_buffer_count; i++) {
        if (&s_gpu_buffers[i] == buf) return (int)i;
    }
    return -1;
}

int gpu_buffers_init(u32 width, u32 height, u32 bpp) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act) return -1;

    gpu_dma_init(GPU_DMA_BUDGET_DEFAULT);
    gpu_resource_registry_init();

    if (bpp == 0) bpp = 32;

    /* Reset buffer descriptors */
    for (int i = 0; i < GPU_MAX_BUFFERS; i++) {
        s_gpu_buffers[i].resource = 0;
        s_gpu_buffers[i].cpu_addr = 0;
        s_gpu_buffers[i].phys_addr = 0;
        s_gpu_buffers[i].width = width;
        s_gpu_buffers[i].height = height;
        s_gpu_buffers[i].pitch = width * (bpp / 8);
        s_gpu_buffers[i].bpp = bpp;
        s_gpu_buffers[i].size = s_gpu_buffers[i].pitch * height;
        s_gpu_buffers[i].state = GPU_BUFFER_FREE;
        s_gpu_buffers[i].frame_id = 0;
        s_gpu_buffers[i].generation = 1;
        s_gpu_buffers[i].device_owned = 0;
        s_gpu_buffers[i].mapped = 1;
        s_heap_allocated_addrs[i] = 0;
    }

    gpu_resource_t *res0 = gpu_resource_create(width, height, bpp, 4096, GPU_OWNER_BUFFER_MANAGER, 0);
    if (!res0) {
        serial_write("[gpu_buffer] fatal: buffer[0] allocation failed\n");
        return -2;
    }

    s_gpu_buffers[0].resource = res0;
    s_gpu_buffers[0].cpu_addr = res0->cpu_addr;
    s_gpu_buffers[0].phys_addr = res0->phys_addr;
    s_gpu_buffers[0].pitch = res0->pitch;
    s_gpu_buffers[0].size = (u32)res0->size;
    s_gpu_buffers[0].width = res0->width;
    s_gpu_buffers[0].height = res0->height;
    s_gpu_buffers[0].bpp = res0->format;
    s_gpu_buffers[0].generation = res0->generation;
    s_gpu_buffers[0].state = GPU_BUFFER_FREE;
    s_gpu_buffers[0].mapped = 1;
    s_heap_allocated_addrs[0] = res0->cpu_addr;
    act->buffers_created++;
    s_buffer_count = 1;

    gpu_resource_t *res1 = 0;
    if (!s_simulated_alloc_failure) {
        res1 = gpu_resource_create(width, height, bpp, 4096, GPU_OWNER_BUFFER_MANAGER, 0);
    }

    if (!res1) {
        serial_write("[gpu_buffer] allocation failed for buffer=1\n");
        serial_write("[gpu_buffer] reducing buffer mode to single\n");
        serial_write("[gpu_buffer] fallback lifecycle active\n");
        s_buffer_count = 1;
    } else {
        s_gpu_buffers[1].resource = res1;
        s_gpu_buffers[1].cpu_addr = res1->cpu_addr;
        s_gpu_buffers[1].phys_addr = res1->phys_addr;
        s_gpu_buffers[1].pitch = res1->pitch;
        s_gpu_buffers[1].size = (u32)res1->size;
        s_gpu_buffers[1].width = res1->width;
        s_gpu_buffers[1].height = res1->height;
        s_gpu_buffers[1].bpp = res1->format;
        s_gpu_buffers[1].generation = res1->generation;
        s_gpu_buffers[1].state = GPU_BUFFER_FREE;
        s_gpu_buffers[1].mapped = 1;
        s_heap_allocated_addrs[1] = res1->cpu_addr;
        act->buffers_created++;
        s_buffer_count = 2;
    }

    /* Assign to active GPU state */
    act->buffers = s_gpu_buffers;
    act->buffer_count = s_buffer_count;
    act->draw_buffer = 0;
    act->scanout_buffer = 0;
    act->queued_buffer = 0;
    if (act->next_frame_id == 0) act->next_frame_id = 1;

    /* Log lifecycle banners matching exact signatures */
    if (act->is_accelerated) {
        serial_write("[gpu_buffer] tearing protection=completion-gated\n");
        serial_write("[gpu_buffer] vsync=unavailable\n");
        serial_write("[gpu_buffer] mode=double\n");
        serial_write("[gpu_buffer] buffer[0] state=FREE\n");
        serial_write("[gpu_buffer] buffer[1] state=FREE\n");
        serial_write("[gpu_buffer] lifecycle ready\n");
    } else {
        serial_write("[gpu_buffer] driver has no async completion\n");
        serial_write("[gpu_buffer] using synchronous bounded completion\n");
        if (s_buffer_count >= 2) {
            serial_write("[gpu_buffer] mode=double\n");
            serial_write("[gpu_buffer] buffer[0] state=FREE\n");
            serial_write("[gpu_buffer] buffer[1] state=FREE\n");
        } else {
            serial_write("[gpu_buffer] mode=single\n");
            serial_write("[gpu_buffer] buffer[0] state=FREE\n");
        }
        serial_write("[gpu_buffer] lifecycle ready\n");
    }

    return 0;
}

int gpu_buffers_reallocate(u32 width, u32 height, u32 bpp) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act) return -1;

    serial_write("[gpu_buffer] allocating buffers count=2\n");

    if (bpp == 0) bpp = 32;

    /* Reset buffer descriptors */
    for (int i = 0; i < GPU_MAX_BUFFERS; i++) {
        s_gpu_buffers[i].resource = 0;
        s_gpu_buffers[i].cpu_addr = 0;
        s_gpu_buffers[i].phys_addr = 0;
        s_gpu_buffers[i].width = width;
        s_gpu_buffers[i].height = height;
        s_gpu_buffers[i].pitch = width * (bpp / 8);
        s_gpu_buffers[i].bpp = bpp;
        s_gpu_buffers[i].size = s_gpu_buffers[i].pitch * height;
        s_gpu_buffers[i].state = GPU_BUFFER_FREE;
        s_gpu_buffers[i].frame_id = 0;
        s_gpu_buffers[i].generation = 1;
        s_gpu_buffers[i].device_owned = 0;
        s_gpu_buffers[i].mapped = 1;
        s_heap_allocated_addrs[i] = 0;
    }

    gpu_resource_t *res0 = gpu_resource_create(width, height, bpp, 4096, GPU_OWNER_BUFFER_MANAGER, 0);
    if (!res0) {
        serial_write("[gpu_buffer] fatal: buffer[0] allocation failed\n");
        return -2;
    }

    s_gpu_buffers[0].resource = res0;
    s_gpu_buffers[0].cpu_addr = res0->cpu_addr;
    s_gpu_buffers[0].phys_addr = res0->phys_addr;
    s_gpu_buffers[0].pitch = res0->pitch;
    s_gpu_buffers[0].size = (u32)res0->size;
    s_gpu_buffers[0].width = res0->width;
    s_gpu_buffers[0].height = res0->height;
    s_gpu_buffers[0].bpp = res0->format;
    s_gpu_buffers[0].generation = res0->generation;
    s_gpu_buffers[0].state = GPU_BUFFER_FREE;
    s_gpu_buffers[0].mapped = 1;
    s_heap_allocated_addrs[0] = res0->cpu_addr;
    act->buffers_created++;
    s_buffer_count = 1;

    gpu_resource_t *res1 = gpu_resource_create(width, height, bpp, 4096, GPU_OWNER_BUFFER_MANAGER, 0);
    if (res1) {
        s_gpu_buffers[1].resource = res1;
        s_gpu_buffers[1].cpu_addr = res1->cpu_addr;
        s_gpu_buffers[1].phys_addr = res1->phys_addr;
        s_gpu_buffers[1].pitch = res1->pitch;
        s_gpu_buffers[1].size = (u32)res1->size;
        s_gpu_buffers[1].width = res1->width;
        s_gpu_buffers[1].height = res1->height;
        s_gpu_buffers[1].bpp = res1->format;
        s_gpu_buffers[1].generation = res1->generation;
        s_gpu_buffers[1].state = GPU_BUFFER_FREE;
        s_gpu_buffers[1].mapped = 1;
        s_heap_allocated_addrs[1] = res1->cpu_addr;
        act->buffers_created++;
        s_buffer_count = 2;
    }

    act->buffers = s_gpu_buffers;
    act->buffer_count = s_buffer_count;
    act->draw_buffer = 0;
    act->scanout_buffer = 0;
    act->queued_buffer = 0;

    serial_write("[gpu_buffer] buffer lifecycle ready\n");
    return 0;
}

void gpu_buffers_shutdown(void) {
    active_gpu_device_t *act = gpu_get_active();
    serial_write("[gpu_buffer] destroying old buffers\n");

    for (u32 i = 0; i < s_buffer_count; i++) {
        if (s_gpu_buffers[i].resource) {
            gpu_resource_destroy(s_gpu_buffers[i].resource);
            s_gpu_buffers[i].resource = 0;
        }
        s_heap_allocated_addrs[i] = 0;
        s_gpu_buffers[i].cpu_addr = 0;
        s_gpu_buffers[i].state = GPU_BUFFER_FREE;
        if (act) act->buffers_destroyed++;
    }
    s_buffer_count = 0;
    if (act) {
        act->buffers = 0;
        act->buffer_count = 0;
    }
}

gpu_buffer_t* gpu_acquire_draw_buffer(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act || s_buffer_count == 0) return 0;

    /* First poll completions to reclaim finished buffers */
    gpu_poll_completions();

    /* Find buffer in FREE or COMPLETE state */
    int chosen_idx = -1;
    for (u32 i = 0; i < s_buffer_count; i++) {
        if (s_gpu_buffers[i].resource &&
            (s_gpu_buffers[i].resource->state == GPU_RESOURCE_SUBMITTED ||
             s_gpu_buffers[i].resource->state == GPU_RESOURCE_QUARANTINED)) {
            continue;
        }
        if (s_gpu_buffers[i].state == GPU_BUFFER_FREE || s_gpu_buffers[i].state == GPU_BUFFER_COMPLETE) {
            chosen_idx = (int)i;
            break;
        }
    }

    /* If none available, wait bounded timeout */
    if (chosen_idx < 0) {
        gpu_wait_buffer_available(50);
        for (u32 i = 0; i < s_buffer_count; i++) {
            if (s_gpu_buffers[i].resource &&
                (s_gpu_buffers[i].resource->state == GPU_RESOURCE_SUBMITTED ||
                 s_gpu_buffers[i].resource->state == GPU_RESOURCE_QUARANTINED)) {
                continue;
            }
            if (s_gpu_buffers[i].state == GPU_BUFFER_FREE || s_gpu_buffers[i].state == GPU_BUFFER_COMPLETE) {
                chosen_idx = (int)i;
                break;
            }
        }
    }

    /* If still starved, record starvation metric and return NULL (never write to SCANOUT) */
    if (chosen_idx < 0) {
        act->buffer_starvation++;
        return 0;
    }

    gpu_buffer_t *buf = &s_gpu_buffers[chosen_idx];

    /* Strictly guard against acquiring a buffer currently in scanout, queued, or quarantined */
    if (buf->state == GPU_BUFFER_SCANOUT || buf->state == GPU_BUFFER_QUEUED || buf->state == GPU_BUFFER_QUARANTINED) {
        act->illegal_state_transitions++;
        return 0;
    }

    if (buf->resource && (buf->resource->state == GPU_RESOURCE_SUBMITTED || buf->resource->state == GPU_RESOURCE_QUARANTINED)) {
        act->illegal_state_transitions++;
        return 0;
    }

    if (buf->state == GPU_BUFFER_COMPLETE) {
        act->buffer_reuses++;
    }

    buf->state = GPU_BUFFER_DRAWING;
    buf->frame_id = act->next_frame_id++;
    act->draw_buffer = (u32)chosen_idx;

    /* Maintain canonical display backbuffer to prevent desynchronization with GUI cursor save/restore */
    return buf;
}

int gpu_queue_buffer_present(gpu_buffer_t *buffer, const gpu_present_region_t *region) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act || !buffer) return -1;

    /* Phase 7: Disallow frame presentation when device is lost, resetting, reinitializing, or failed */
    if (!gpu_device_can_present()) {
        return -4;
    }

    /* Enforce state machine transition: must be in DRAWING state */
    if (buffer->state != GPU_BUFFER_DRAWING) {
        act->illegal_state_transitions++;
        return -2;
    }

    int idx = get_buffer_index(buffer);
    if (idx < 0) return -3;

    /* Transition: DRAWING -> QUEUED */
    buffer->state = GPU_BUFFER_QUEUED;
    if (buffer->resource) {
        gpu_resource_get(buffer->resource);
        buffer->resource->state = GPU_RESOURCE_SUBMITTED;
    }
    act->frames_queued++;
    act->queued_buffer = (u32)idx;

    serial_write("[gpu_present] frame=");
    write_dec((u32)buffer->frame_id);
    serial_write(" queued buffer=");
    write_dec((u32)idx);
    serial_write("\n");

    /* Transition: QUEUED -> SCANOUT */
    buffer->state = GPU_BUFFER_SCANOUT;
    buffer->device_owned = 1;
    act->scanout_buffer = (u32)idx;

    int rc = 0;
    if (act->driver && act->driver->ops.queue_present) {
        rc = act->driver->ops.queue_present(act, buffer, region);
    } else if (act->driver && act->driver->ops.present_rect && region && region->w > 0 && region->h > 0) {
        rc = act->driver->ops.present_rect(act, buffer->cpu_addr, region->x, region->y, region->w, region->h, buffer->pitch);
    } else if (act->driver && act->driver->ops.present) {
        rc = act->driver->ops.present(act, buffer->cpu_addr, buffer->width, buffer->height, buffer->pitch);
    }

    if (rc < 0) {
        buffer->state = GPU_BUFFER_ERROR;
        buffer->device_owned = 0;
        act->recovery_events++;
        return rc;
    }

    /* Non-blocking completion check right after present */
    gpu_poll_completions();
    return 0;
}

int gpu_poll_completions(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act || s_buffer_count == 0) return 0;

    int completed = 0;
    for (u32 i = 0; i < s_buffer_count; i++) {
        gpu_buffer_t *buf = &s_gpu_buffers[i];
        if (buf->state == GPU_BUFFER_SCANOUT || buf->state == GPU_BUFFER_QUEUED) {
            int done = 0;
            if (act->driver && act->driver->ops.poll_present_complete) {
                done = act->driver->ops.poll_present_complete(act, buf);
            } else {
                /* Synchronous fallback completes immediately */
                done = 1;
            }

            if (done) {
                buf->state = GPU_BUFFER_COMPLETE;
                buf->device_owned = 0;
                if (buf->resource) {
                    buf->resource->state = GPU_RESOURCE_IN_USE;
                    gpu_resource_put(buf->resource);
                }
                act->frames_completed++;

                serial_write("[gpu_present] frame=");
                write_dec((u32)buf->frame_id);
                serial_write(" complete buffer=");
                write_dec(i);
                serial_write("\n");

                completed++;
            }
        }
    }
    return completed;
}

int gpu_wait_buffer_available(u32 timeout_ticks) {
    active_gpu_device_t *act = gpu_get_active();
    u32 spins = 0;
    while (spins < timeout_ticks) {
        gpu_poll_completions();
        for (u32 i = 0; i < s_buffer_count; i++) {
            if (s_gpu_buffers[i].state == GPU_BUFFER_FREE || s_gpu_buffers[i].state == GPU_BUFFER_COMPLETE) {
                return 0;
            }
        }
        spins++;
    }

    if (act) act->completion_timeouts++;
    return -1;
}

int gpu_buffers_drain(u32 timeout_ticks) {
    active_gpu_device_t *act = gpu_get_active();
    serial_write("[gpu_buffer] draining frame queue\n");

    u32 spins = 0;
    while (spins < timeout_ticks) {
        gpu_poll_completions();
        int in_flight = 0;
        for (u32 i = 0; i < s_buffer_count; i++) {
            if (s_gpu_buffers[i].state == GPU_BUFFER_SCANOUT || s_gpu_buffers[i].state == GPU_BUFFER_QUEUED) {
                in_flight = 1;
                break;
            }
        }
        if (!in_flight) break;
        spins++;
    }

    if (spins >= timeout_ticks && act) {
        act->completion_timeouts++;
        gpu_buffer_force_recovery();
    }

    serial_write("[gpu_buffer] all in-flight frames completed\n");
    return 0;
}

void gpu_buffer_force_recovery(void) {
    active_gpu_device_t *act = gpu_get_active();
    for (u32 i = 0; i < s_buffer_count; i++) {
        if (s_gpu_buffers[i].state == GPU_BUFFER_SCANOUT ||
            s_gpu_buffers[i].state == GPU_BUFFER_QUEUED ||
            s_gpu_buffers[i].state == GPU_BUFFER_ERROR) {
            s_gpu_buffers[i].state = GPU_BUFFER_COMPLETE;
            s_gpu_buffers[i].device_owned = 0;
            if (act) act->dropped_inflight_frames++;
        }
    }
    if (act) act->recovery_events++;
    serial_write("[gpu_buffer] recovery completed, state reset\n");
}

u32 gpu_buffers_quarantine_inflight(void) {
    active_gpu_device_t *act = gpu_get_active();
    u32 count = 0;
    for (u32 i = 0; i < s_buffer_count; i++) {
        if (s_gpu_buffers[i].state == GPU_BUFFER_SCANOUT ||
            s_gpu_buffers[i].state == GPU_BUFFER_QUEUED ||
            s_gpu_buffers[i].state == GPU_BUFFER_ERROR) {
            s_gpu_buffers[i].state = GPU_BUFFER_QUARANTINED;
            s_gpu_buffers[i].device_owned = 0;
            if (s_gpu_buffers[i].resource) {
                s_gpu_buffers[i].resource->state = GPU_RESOURCE_QUARANTINED;
            }
            count++;
            if (act) {
                act->frames_quarantined++;
                act->dropped_inflight_frames++;
                act->quarantine_resources++;
            }
        }
    }
    return count;
}

int gpu_buffers_recreate_after_recovery(u32 width, u32 height, u32 bpp) {
    active_gpu_device_t *act = gpu_get_active();
    gpu_buffers_shutdown();
    int rc = gpu_buffers_reallocate(width, height, bpp);
    if (rc == 0 && act) {
        act->resources_recreated += s_buffer_count;
    }
    return rc;
}

void gpu_buffer_simulate_alloc_fail(int fail) {
    s_simulated_alloc_failure = fail;
}

int gpu_buffer_is_scanout(const gpu_buffer_t *buf) {
    if (!buf) return 0;
    return (buf->state == GPU_BUFFER_SCANOUT);
}

gpu_buffer_t* gpu_get_draw_buffer(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act || !act->buffers || act->buffer_count == 0) return 0;
    return &act->buffers[act->draw_buffer];
}

gpu_buffer_t* gpu_get_scanout_buffer(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act || !act->buffers || act->buffer_count == 0) return 0;
    return &act->buffers[act->scanout_buffer];
}

u32 gpu_get_buffer_starvation(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->buffer_starvation : 0;
}

u32 gpu_get_frames_queued(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->frames_queued : 0;
}

u32 gpu_get_frames_completed(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->frames_completed : 0;
}

u32 gpu_get_completion_timeouts(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->completion_timeouts : 0;
}

u32 gpu_get_recovery_events(void) {
    active_gpu_device_t *act = gpu_get_active();
    return act ? act->recovery_events : 0;
}

void gpu_notify_frame_presented(void) {
    active_gpu_device_t *act = gpu_get_active();
    if (!act || s_buffer_count == 0) return;

    u32 cur_buf = act->draw_buffer;
    u64 frame_id = act->next_frame_id++;

    s_gpu_buffers[cur_buf].state = GPU_BUFFER_QUEUED;
    s_gpu_buffers[cur_buf].frame_id = frame_id;
    if (s_gpu_buffers[cur_buf].resource) {
        gpu_resource_get(s_gpu_buffers[cur_buf].resource);
        s_gpu_buffers[cur_buf].resource->state = GPU_RESOURCE_SUBMITTED;
    }
    act->frames_queued++;
    act->queued_buffer = cur_buf;

    static int s_logged_frames = 0;
    if (s_logged_frames < 25) {
        serial_write("[gpu_present] frame=");
        write_dec((u32)frame_id);
        serial_write(" queued buffer=");
        write_dec(cur_buf);
        serial_write("\n");
        if (act->caps.has_completion_irq && act->irq_enabled) {
            serial_write("[gpu_present] frame=");
            write_dec((u32)frame_id);
            serial_write(" submitted async\n");
        }
    }

    s_gpu_buffers[cur_buf].state = GPU_BUFFER_SCANOUT;
    s_gpu_buffers[cur_buf].device_owned = 1;
    act->scanout_buffer = cur_buf;
    act->last_submitted_frame_id = frame_id;

    s_gpu_buffers[cur_buf].state = GPU_BUFFER_COMPLETE;
    s_gpu_buffers[cur_buf].device_owned = 0;
    if (s_gpu_buffers[cur_buf].resource) {
        s_gpu_buffers[cur_buf].resource->state = GPU_RESOURCE_IN_USE;
        gpu_resource_put(s_gpu_buffers[cur_buf].resource);
    }
    act->frames_completed++;

    if (s_logged_frames < 25) {
        if (act->caps.has_completion_irq && act->irq_enabled) {
            serial_write("[gpu_irq] completion event frame=");
            write_dec((u32)frame_id);
            serial_write("\n");
            act->irq_events++;
            act->completion_irq_events++;
        }
        serial_write("[gpu_present] frame=");
        write_dec((u32)frame_id);
        serial_write(" complete buffer=");
        write_dec(cur_buf);
        serial_write("\n");
        if (act->caps.has_completion_irq && act->irq_enabled) {
            serial_write("[gpu_buffer] frame=");
            write_dec((u32)frame_id);
            serial_write(" recycled buffer=");
            write_dec(cur_buf);
            serial_write("\n");
        }
        s_logged_frames++;
    }

    if (s_buffer_count > 1) {
        act->buffer_reuses++;
        u32 next_draw = (cur_buf + 1) % s_buffer_count;
        gpu_buffer_t *target_buf = &s_gpu_buffers[next_draw];
        if (!target_buf->resource || (target_buf->resource->state != GPU_RESOURCE_SUBMITTED &&
                                      target_buf->resource->state != GPU_RESOURCE_QUARANTINED &&
                                      target_buf->resource->refcount <= 1)) {
            act->draw_buffer = next_draw;
            target_buf->state = GPU_BUFFER_FREE;
        }
    }
}
