#include "gpu_event_user.h"
#include "gpu_user_client.h"
#include "serial.h"

static gpu_user_event_queue_t s_user_queues[GPU_USER_CLIENT_MAX];
static int                    s_event_initialized = 0;

int gpu_event_user_init(void) {
    for (int i = 0; i < GPU_USER_CLIENT_MAX; i++) {
        s_user_queues[i].head = 0;
        s_user_queues[i].tail = 0;
        s_user_queues[i].count = 0;
        s_user_queues[i].delivered = 0;
        s_user_queues[i].dropped = 0;
    }
    s_event_initialized = 1;
    return GPU_STATUS_OK;
}

int gpu_event_user_post(u32 user_client_id, const gpu_user_event_t *event) {
    if (!event) return GPU_STATUS_INVALID_ARGUMENT;
    if (user_client_id >= GPU_USER_CLIENT_MAX) return GPU_STATUS_INVALID_ARGUMENT;

    if (!s_event_initialized) gpu_event_user_init();

    gpu_user_event_queue_t *q = &s_user_queues[user_client_id];

    /* Duplicate rejection: check if identical request_id and event_type already queued */
    if (event->request_id != 0) {
        for (u32 i = 0; i < q->count; i++) {
            u32 idx = (q->head + i) % GPU_USER_EVENT_QUEUE_SIZE;
            if (q->events[idx].request_id == event->request_id &&
                q->events[idx].event_type == event->event_type) {
                /* Duplicate event, reject silently to avoid flooding */
                return GPU_STATUS_OK;
            }
        }
    }

    /* Queue capacity limit (backpressure) */
    if (q->count >= GPU_USER_EVENT_QUEUE_SIZE) {
        q->dropped++;
        return GPU_STATUS_QUEUE_FULL;
    }

    q->events[q->tail] = *event;
    q->tail = (q->tail + 1) % GPU_USER_EVENT_QUEUE_SIZE;
    q->count++;

    return GPU_STATUS_OK;
}

int gpu_event_user_poll(u32 user_client_id, gpu_user_event_t *out_event) {
    if (!out_event) return GPU_STATUS_INVALID_ARGUMENT;
    if (user_client_id >= GPU_USER_CLIENT_MAX) return GPU_STATUS_INVALID_ARGUMENT;

    if (!s_event_initialized) gpu_event_user_init();

    gpu_user_event_queue_t *q = &s_user_queues[user_client_id];
    if (q->count == 0) {
        return GPU_STATUS_TIMEOUT;
    }

    *out_event = q->events[q->head];
    q->head = (q->head + 1) % GPU_USER_EVENT_QUEUE_SIZE;
    q->count--;
    q->delivered++;

    return GPU_STATUS_OK;
}

int gpu_event_user_wait(u32 user_client_id, u32 timeout_ticks, gpu_user_event_t *out_event) {
    if (!out_event) return GPU_STATUS_INVALID_ARGUMENT;
    if (user_client_id >= GPU_USER_CLIENT_MAX) return GPU_STATUS_INVALID_ARGUMENT;

    if (!s_event_initialized) gpu_event_user_init();

    /* Bounded wait with maximum cap to guarantee no unbounded busy-waiting */
    u32 max_ticks = timeout_ticks;
    if (max_ticks == 0) max_ticks = 1;
    if (max_ticks > 1000000) max_ticks = 1000000;

    for (u32 t = 0; t < max_ticks; t++) {
        int res = gpu_event_user_poll(user_client_id, out_event);
        if (res == GPU_STATUS_OK) {
            return GPU_STATUS_OK;
        }
        /* CPU relax / pause */
        __asm__ volatile("pause");
    }

    return GPU_STATUS_TIMEOUT;
}

void gpu_event_user_purge(u32 user_client_id) {
    if (user_client_id >= GPU_USER_CLIENT_MAX) return;
    gpu_user_event_queue_t *q = &s_user_queues[user_client_id];
    q->head = 0;
    q->tail = 0;
    q->count = 0;
}

void gpu_event_user_post_all(gpu_user_event_type_t event_type, int status) {
    for (u32 i = 0; i < GPU_USER_CLIENT_MAX; i++) {
        gpu_user_client_t *cli = gpu_user_client_get_by_id(i);
        if (cli && cli->in_use) {
            gpu_user_event_t ev;
            ev.header.version = GPU_ABI_VERSION;
            ev.header.size = (u16)sizeof(gpu_user_event_t);
            ev.header.flags = 0;
            ev.request_id = 0;
            ev.event_type = (u32)event_type;
            ev.status = (u32)status;
            ev.resource_generation = cli->generation;
            ev.reserved = 0;
            ev.user_cookie = 0;
            gpu_event_user_post(i, &ev);
        }
    }
}

void gpu_event_user_post_completion(u32 kernel_client_id, u64 request_id, u64 user_cookie, u32 res_gen) {
    gpu_user_client_t *cli = gpu_user_client_get_by_kernel_id(kernel_client_id);
    if (!cli || !cli->in_use) return;

    gpu_user_event_t ev;
    ev.header.version = GPU_ABI_VERSION;
    ev.header.size = (u16)sizeof(gpu_user_event_t);
    ev.header.flags = 0;
    ev.request_id = request_id;
    ev.event_type = GPU_EVENT_COMPLETION;
    ev.status = GPU_STATUS_OK;
    ev.resource_generation = res_gen;
    ev.reserved = 0;
    ev.user_cookie = user_cookie;

    gpu_event_user_post(cli->slot, &ev);
}

void gpu_event_user_post_dropped(u32 kernel_client_id, u64 request_id, u64 user_cookie, int status) {
    gpu_user_client_t *cli = gpu_user_client_get_by_kernel_id(kernel_client_id);
    if (!cli || !cli->in_use) return;

    gpu_user_event_t ev;
    ev.header.version = GPU_ABI_VERSION;
    ev.header.size = (u16)sizeof(gpu_user_event_t);
    ev.header.flags = 0;
    ev.request_id = request_id;
    ev.event_type = GPU_EVENT_DROPPED;
    ev.status = (u32)status;
    ev.resource_generation = cli->generation;
    ev.reserved = 0;
    ev.user_cookie = user_cookie;

    gpu_event_user_post(cli->slot, &ev);
}
