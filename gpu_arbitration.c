#include "gpu_arbitration.h"
#include "gpu_client.h"
#include "gpu_resource.h"
#include "gpu_memcheck.h"
#include "gpu_detect.h"
#include "gpu_event_user.h"
#include "serial.h"

static gpu_request_t          s_request_pool[GPU_REQUEST_POOL_CAPACITY];
static u64                    s_next_request_id = 0;
static u32                    s_consecutive_high_priority = 0;
static int                    s_scheduler_paused = 0;
static gpu_scheduler_metrics_t g_sched_metrics;

static void write_dec_arbitration(u64 val) {
    char buf[24];
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

const char* gpu_request_state_name(gpu_request_state_t state) {
    switch (state) {
        case GPU_REQ_NEW:       return "NEW";
        case GPU_REQ_QUEUED:    return "QUEUED";
        case GPU_REQ_ADMITTED:  return "ADMITTED";
        case GPU_REQ_SUBMITTED: return "SUBMITTED";
        case GPU_REQ_COMPLETED: return "COMPLETED";
        case GPU_REQ_DROPPED:   return "DROPPED";
        case GPU_REQ_CANCELLED: return "CANCELLED";
        case GPU_REQ_REJECTED:  return "REJECTED";
        default:                return "UNKNOWN";
    }
}

int gpu_scheduler_init(void) {
    /* Initialize client subsystem */
    gpu_client_subsystem_init();

    /* Clear request pool */
    for (int i = 0; i < GPU_REQUEST_POOL_CAPACITY; i++) {
        s_request_pool[i].request_id = 0;
        s_request_pool[i].frame_id = 0;
        s_request_pool[i].client_id = 0;
        s_request_pool[i].generation = 0;
        s_request_pool[i].resource = 0;
        s_request_pool[i].region.x = 0;
        s_request_pool[i].region.y = 0;
        s_request_pool[i].region.w = 0;
        s_request_pool[i].region.h = 0;
        s_request_pool[i].base_priority = 0;
        s_request_pool[i].effective_priority = 0;
        s_request_pool[i].wait_ticks = 0;
        s_request_pool[i].cost = 0;
        s_request_pool[i].flags = 0;
        s_request_pool[i].state = GPU_REQ_NEW;
        s_request_pool[i].next = 0;
    }

    s_next_request_id = 0;
    s_consecutive_high_priority = 0;
    s_scheduler_paused = 0;

    /* Reset metrics */
    g_sched_metrics.requests_submitted = 0;
    g_sched_metrics.requests_completed = 0;
    g_sched_metrics.requests_rejected = 0;
    g_sched_metrics.requests_cancelled = 0;
    g_sched_metrics.requests_coalesced = 0;
    g_sched_metrics.requests_throttled = 0;
    g_sched_metrics.requests_starved = 0;
    g_sched_metrics.peak_queue_depth = 0;
    g_sched_metrics.max_wait_ticks = 0;
    g_sched_metrics.total_wait_ticks = 0;
    g_sched_metrics.bytes_admitted = 0;
    g_sched_metrics.quota_rejections = 0;
    g_sched_metrics.generation_rejections = 0;
    g_sched_metrics.resource_rejections = 0;
    g_sched_metrics.fairness_forced_dispatches = 0;

    /* Register core system clients */
    gpu_client_register(GPU_CLIENT_COMPOSITOR);  /* ID 1 */
    gpu_client_register(GPU_CLIENT_CURSOR);      /* ID 2 */
    gpu_client_register(GPU_CLIENT_SYSTEM_UI);   /* ID 3 */
    gpu_client_register(GPU_CLIENT_APPLICATION); /* ID 4 */
    gpu_client_register(GPU_CLIENT_RECOVERY);    /* ID 5 */

    serial_write("[gpu_sched] initialized clients=5 policy=priority-aging\n");
    return 0;
}

int gpu_submit_present_cookie(u32 client_id,
                              gpu_resource_t *resource,
                              const gpu_present_region_t *region,
                              u32 flags,
                              u64 user_cookie,
                              u64 *out_request_id) {
    if (out_request_id) *out_request_id = 0;
    if (s_scheduler_paused) {
        serial_write("[gpu_sched] request rejected reason=scheduler-paused\n");
        g_sched_metrics.requests_rejected++;
        return -10;
    }

    /* 1. Client Validation */
    gpu_client_t *client = gpu_client_get(client_id);
    if (!client) {
        serial_write("[gpu_sched] request rejected reason=invalid-client\n");
        g_sched_metrics.requests_rejected++;
        return -1;
    }

    if (client->state == GPU_CLIENT_BLOCKED || client->state == GPU_CLIENT_DEAD) {
        serial_write("[gpu_sched] request rejected reason=client-blocked\n");
        g_sched_metrics.requests_rejected++;
        return -1;
    }

    /* 2. Device Ready Validation */
    if (!gpu_device_can_present()) {
        serial_write("[gpu_sched] request rejected reason=device-not-ready\n");
        g_sched_metrics.requests_rejected++;
        return -2;
    }

    /* 3. Generation Validation */
    u32 cur_gen = gpu_resource_get_generation();
    if (client->generation < cur_gen) {
        serial_write("[gpu_sched] request rejected reason=generation-mismatch\n");
        g_sched_metrics.generation_rejections++;
        g_sched_metrics.requests_rejected++;
        return -3;
    }

    /* 4. Resource Validation */
    if (!resource || resource->state == GPU_RESOURCE_DEAD || resource->state == GPU_RESOURCE_FREE) {
        serial_write("[gpu_sched] request rejected reason=invalid-resource\n");
        g_sched_metrics.resource_rejections++;
        g_sched_metrics.requests_rejected++;
        return -4;
    }

    if (resource->state == GPU_RESOURCE_QUARANTINED) {
        serial_write("[gpu_sched] request rejected reason=resource-quarantined\n");
        g_sched_metrics.resource_rejections++;
        g_sched_metrics.requests_rejected++;
        return -5;
    }

    if (resource->generation != cur_gen) {
        serial_write("[gpu_sched] request rejected reason=generation-mismatch\n");
        g_sched_metrics.generation_rejections++;
        g_sched_metrics.requests_rejected++;
        return -3;
    }

    /* 5. Region Validation */
    if (region && (region->w > 0 && region->h > 0)) {
        if (gpu_validate_region(resource, region->x, region->y, region->w, region->h, resource->pitch) != 0) {
            serial_write("[gpu_sched] request rejected reason=invalid-region\n");
            g_sched_metrics.requests_rejected++;
            return -6;
        }
    }

    /* 6. Quota Validation */
    if (client->used_bytes + resource->size > client->quota_bytes) {
        serial_write("[gpu_sched] client=");
        write_dec_arbitration(client->client_id);
        serial_write(" quota exceeded\n");
        g_sched_metrics.quota_rejections++;
        g_sched_metrics.requests_rejected++;
        return -7;
    }

    /* 7. In-flight Limits (Throttling) */
    if (client->inflight >= client->max_inflight) {
        serial_write("[gpu_sched] client=");
        write_dec_arbitration(client->client_id);
        serial_write(" throttled reason=max-inflight\n");
        client->throttled++;
        g_sched_metrics.requests_throttled++;
        return -8;
    }

    /* 8. Queue Depth Limits */
    if (client->queue_depth >= client->max_queue_depth) {
        serial_write("[gpu_sched] client=");
        write_dec_arbitration(client->client_id);
        serial_write(" throttled reason=queue-full\n");
        client->throttled++;
        g_sched_metrics.requests_throttled++;
        return -9;
    }

    /* 9. Coalescing: Check if an unsubmitted request for the same client and resource already exists */
    for (int i = 0; i < GPU_REQUEST_POOL_CAPACITY; i++) {
        if (s_request_pool[i].state == GPU_REQ_QUEUED &&
            s_request_pool[i].client_id == client_id &&
            s_request_pool[i].resource == resource &&
            s_request_pool[i].generation == cur_gen) {

            gpu_request_t *existing = &s_request_pool[i];
            u64 new_rid = ++s_next_request_id;

            /* Union of regions if specified */
            if (region && region->w > 0 && region->h > 0) {
                if (existing->region.w == 0 || existing->region.h == 0) {
                    existing->region = *region;
                } else {
                    u32 x1 = existing->region.x < region->x ? existing->region.x : region->x;
                    u32 y1 = existing->region.y < region->y ? existing->region.y : region->y;
                    u32 r1 = (existing->region.x + existing->region.w) > (region->x + region->w) ?
                             (existing->region.x + existing->region.w) : (region->x + region->w);
                    u32 b1 = (existing->region.y + existing->region.h) > (region->y + region->h) ?
                             (existing->region.y + existing->region.h) : (region->y + region->h);
                    existing->region.x = x1;
                    existing->region.y = y1;
                    existing->region.w = r1 - x1;
                    existing->region.h = b1 - y1;
                }
            }

            client->coalesced++;
            g_sched_metrics.requests_coalesced++;

            serial_write("[gpu_sched] request=");
            write_dec_arbitration(new_rid);
            serial_write(" coalesced into request=");
            write_dec_arbitration(existing->request_id);
            serial_write("\n");
            if (out_request_id) *out_request_id = existing->request_id;
            return (int)existing->request_id;
        }
    }

    /* 10. Find free request slot */
    int slot = -1;
    for (int i = 0; i < GPU_REQUEST_POOL_CAPACITY; i++) {
        if (s_request_pool[i].state == GPU_REQ_NEW ||
            s_request_pool[i].state == GPU_REQ_COMPLETED ||
            s_request_pool[i].state == GPU_REQ_DROPPED ||
            s_request_pool[i].state == GPU_REQ_CANCELLED ||
            s_request_pool[i].state == GPU_REQ_REJECTED) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        serial_write("[gpu_sched] request pool exhausted\n");
        g_sched_metrics.requests_rejected++;
        return -11;
    }

    gpu_request_t *req = &s_request_pool[slot];
    req->request_id = ++s_next_request_id;
    req->frame_id = req->request_id;
    req->client_id = client_id;
    req->generation = cur_gen;
    req->resource = resource;
    if (region) req->region = *region;
    else {
        req->region.x = 0;
        req->region.y = 0;
        req->region.w = resource->width;
        req->region.h = resource->height;
    }
    req->base_priority = client->priority;
    req->effective_priority = client->priority;
    req->wait_ticks = 0;
    req->cost = (u32)(resource->size / 4096);
    req->flags = flags;
    req->user_cookie = user_cookie;
    req->state = GPU_REQ_QUEUED;
    req->next = 0;

    /* Acquire reference on resource for scheduler lifecycle */
    gpu_resource_get(resource);

    client->used_bytes += resource->size;
    client->queue_depth++;
    client->submitted++;
    g_sched_metrics.requests_submitted++;

    u32 total_depth = 0;
    for (int i = 0; i < GPU_REQUEST_POOL_CAPACITY; i++) {
        if (s_request_pool[i].state == GPU_REQ_QUEUED) total_depth++;
    }
    if (total_depth > g_sched_metrics.peak_queue_depth) {
        g_sched_metrics.peak_queue_depth = total_depth;
    }

    if (out_request_id) *out_request_id = req->request_id;
    return (int)req->request_id;
}

int gpu_submit_present(u32 client_id,
                       gpu_resource_t *resource,
                       const gpu_present_region_t *region,
                       u32 flags) {
    return gpu_submit_present_cookie(client_id, resource, region, flags, 0, 0);
}

int gpu_scheduler_submit(gpu_request_t *request) {
    if (!request) return -1;
    return gpu_submit_present(request->client_id, request->resource, &request->region, request->flags);
}

int gpu_scheduler_admit_next(gpu_request_t **out) {
    if (!out) return -1;
    *out = 0;

    if (s_scheduler_paused) return 0;

    /* 1. Fast Path: Check if Recovery client has a pending request */
    for (int i = 0; i < GPU_REQUEST_POOL_CAPACITY; i++) {
        if (s_request_pool[i].state == GPU_REQ_QUEUED &&
            s_request_pool[i].base_priority >= 100) {
            gpu_request_t *chosen = &s_request_pool[i];
            chosen->state = GPU_REQ_ADMITTED;
            gpu_client_t *c = gpu_client_get(chosen->client_id);
            if (c) {
                if (c->queue_depth > 0) c->queue_depth--;
                c->inflight++;
            }
            g_sched_metrics.bytes_admitted += chosen->resource ? chosen->resource->size : 0;
            serial_write("[gpu_sched] admitted request=");
            write_dec_arbitration(chosen->request_id);
            serial_write(" client=");
            write_dec_arbitration(chosen->client_id);
            serial_write(" priority=");
            write_dec_arbitration(chosen->effective_priority);
            serial_write("\n");
            *out = chosen;
            return 1;
        }
    }

    /* 2. Fairness Rule: If high priority clients dispatched MAX_CONSECUTIVE_HIGH_PRIORITY times,
     * force-dispatch a waiting application request to prevent starvation. */
    if (s_consecutive_high_priority >= MAX_CONSECUTIVE_HIGH_PRIORITY) {
        for (int i = 0; i < GPU_REQUEST_POOL_CAPACITY; i++) {
            if (s_request_pool[i].state == GPU_REQ_QUEUED &&
                s_request_pool[i].base_priority <= 50) {
                gpu_request_t *chosen = &s_request_pool[i];
                chosen->state = GPU_REQ_ADMITTED;
                gpu_client_t *c = gpu_client_get(chosen->client_id);
                if (c) {
                    if (c->queue_depth > 0) c->queue_depth--;
                    c->inflight++;
                }
                g_sched_metrics.bytes_admitted += chosen->resource ? chosen->resource->size : 0;
                g_sched_metrics.fairness_forced_dispatches++;
                s_consecutive_high_priority = 0;

                serial_write("[gpu_sched] admitted request=");
                write_dec_arbitration(chosen->request_id);
                serial_write(" client=");
                write_dec_arbitration(chosen->client_id);
                serial_write(" priority=");
                write_dec_arbitration(chosen->effective_priority);
                serial_write("\n");

                *out = chosen;
                return 1;
            }
        }
    }

    /* 3. Priority + Aging Selection */
    int best_slot = -1;
    u32 highest_eff_prio = 0;
    u64 earliest_rid = 0xFFFFFFFFFFFFFFFFULL;

    for (int i = 0; i < GPU_REQUEST_POOL_CAPACITY; i++) {
        if (s_request_pool[i].state == GPU_REQ_QUEUED) {
            u32 prio = s_request_pool[i].effective_priority;
            if (prio > highest_eff_prio ||
                (prio == highest_eff_prio && s_request_pool[i].request_id < earliest_rid)) {
                highest_eff_prio = prio;
                earliest_rid = s_request_pool[i].request_id;
                best_slot = i;
            }
        }
    }

    if (best_slot < 0) return 0;

    gpu_request_t *chosen = &s_request_pool[best_slot];
    chosen->state = GPU_REQ_ADMITTED;
    gpu_client_t *c = gpu_client_get(chosen->client_id);
    if (c) {
        if (c->queue_depth > 0) c->queue_depth--;
        c->inflight++;
    }
    g_sched_metrics.bytes_admitted += chosen->resource ? chosen->resource->size : 0;

    if (chosen->base_priority >= 70) {
        s_consecutive_high_priority++;
    } else {
        s_consecutive_high_priority = 0;
    }

    serial_write("[gpu_sched] admitted request=");
    write_dec_arbitration(chosen->request_id);
    serial_write(" client=");
    write_dec_arbitration(chosen->client_id);
    serial_write(" priority=");
    write_dec_arbitration(chosen->effective_priority);
    serial_write("\n");

    *out = chosen;
    return 1;
}

int gpu_scheduler_complete(gpu_request_t *request, int status) {
    (void)status;
    if (!request) return -1;

    request->state = GPU_REQ_COMPLETED;
    gpu_client_t *client = gpu_client_get(request->client_id);
    if (client) {
        if (client->inflight > 0) client->inflight--;
        if (request->resource && client->used_bytes >= request->resource->size) {
            client->used_bytes -= request->resource->size;
        } else {
            client->used_bytes = 0;
        }
        client->completed++;
    }
    g_sched_metrics.requests_completed++;

    /* Release resource reference */
    if (request->resource) {
        gpu_resource_put(request->resource);
    }

    if (request->user_cookie != 0) {
        gpu_event_user_post_completion(request->client_id, request->request_id, request->user_cookie, request->generation);
    }

    serial_write("[gpu_sched] completed request=");
    write_dec_arbitration(request->request_id);
    serial_write(" client=");
    write_dec_arbitration(request->client_id);
    serial_write("\n");

    return 0;
}

int gpu_scheduler_cancel_client(u32 client_id) {
    int cancelled = 0;
    for (int i = 0; i < GPU_REQUEST_POOL_CAPACITY; i++) {
        if ((s_request_pool[i].state == GPU_REQ_QUEUED || s_request_pool[i].state == GPU_REQ_ADMITTED) &&
            s_request_pool[i].client_id == client_id) {
            s_request_pool[i].state = GPU_REQ_CANCELLED;
            if (s_request_pool[i].user_cookie != 0) {
                gpu_event_user_post_dropped(s_request_pool[i].client_id, s_request_pool[i].request_id, s_request_pool[i].user_cookie, -10);
            }
            if (s_request_pool[i].resource) {
                gpu_resource_put(s_request_pool[i].resource);
            }
            cancelled++;
        }
    }
    g_sched_metrics.requests_cancelled += cancelled;
    return cancelled;
}

int gpu_cancel_requests(u32 client_id) {
    return gpu_scheduler_cancel_client(client_id);
}

void gpu_scheduler_tick(void) {
    for (int i = 0; i < GPU_REQUEST_POOL_CAPACITY; i++) {
        if (s_request_pool[i].state == GPU_REQ_QUEUED) {
            s_request_pool[i].wait_ticks++;
            g_sched_metrics.total_wait_ticks++;
            if (s_request_pool[i].wait_ticks > g_sched_metrics.max_wait_ticks) {
                g_sched_metrics.max_wait_ticks = s_request_pool[i].wait_ticks;
            }

            /* Priority aging formula: effective = base + min(wait_ticks / 2, 30) */
            u32 age_boost = s_request_pool[i].wait_ticks / 2;
            if (age_boost > 30) age_boost = 30;
            s_request_pool[i].effective_priority = s_request_pool[i].base_priority + age_boost;

            if (s_request_pool[i].wait_ticks >= 40) {
                g_sched_metrics.requests_starved++;
            }
        }
    }
}

void gpu_scheduler_pause_for_recovery(void) {
    s_scheduler_paused = 1;
    serial_write("[gpu_sched] paused for gpu recovery\n");

    u32 cancelled = 0;
    for (int i = 0; i < GPU_REQUEST_POOL_CAPACITY; i++) {
        if (s_request_pool[i].state == GPU_REQ_QUEUED || s_request_pool[i].state == GPU_REQ_ADMITTED) {
            s_request_pool[i].state = GPU_REQ_CANCELLED;
            if (s_request_pool[i].resource) {
                gpu_resource_put(s_request_pool[i].resource);
            }
            gpu_client_t *c = gpu_client_get(s_request_pool[i].client_id);
            if (c) {
                c->queue_depth = 0;
                c->inflight = 0;
                c->used_bytes = 0;
            }
            cancelled++;
        }
    }
    g_sched_metrics.requests_cancelled += cancelled;

    serial_write("[gpu_sched] cancelled stale requests=");
    write_dec_arbitration(cancelled);
    serial_write("\n");
}

void gpu_scheduler_refresh_generations(void) {
    gpu_client_refresh_all_generations(gpu_resource_get_generation());
    serial_write("[gpu_sched] client generations refreshed\n");
}

void gpu_scheduler_resume(void) {
    s_scheduler_paused = 0;
    s_consecutive_high_priority = 0;
    serial_write("[gpu_sched] resumed after recovery\n");
}

int gpu_scheduler_drain(u32 timeout_ticks) {
    u32 spins = 0;
    while (spins < timeout_ticks) {
        int in_flight = 0;
        for (int i = 0; i < GPU_REQUEST_POOL_CAPACITY; i++) {
            if (s_request_pool[i].state == GPU_REQ_ADMITTED ||
                s_request_pool[i].state == GPU_REQ_SUBMITTED) {
                in_flight = 1;
                break;
            }
        }
        if (!in_flight) break;
        spins++;
    }

    /* Cancel any leftover queued requests */
    u32 cancelled = 0;
    for (int i = 0; i < GPU_REQUEST_POOL_CAPACITY; i++) {
        if (s_request_pool[i].state == GPU_REQ_QUEUED) {
            s_request_pool[i].state = GPU_REQ_CANCELLED;
            if (s_request_pool[i].resource) {
                gpu_resource_put(s_request_pool[i].resource);
            }
            cancelled++;
        }
    }
    g_sched_metrics.requests_cancelled += cancelled;
    return 0;
}

int gpu_get_scheduler_metrics(gpu_scheduler_metrics_t *out) {
    if (!out) return -1;
    *out = g_sched_metrics;
    return 0;
}

u32 gpu_get_queue_depth(u32 client_id) {
    gpu_client_t *c = gpu_client_get(client_id);
    return c ? c->queue_depth : 0;
}

int gpu_get_client_metrics(u32 client_id, gpu_client_stats_t *out) {
    return gpu_get_client_stats(client_id, out);
}
