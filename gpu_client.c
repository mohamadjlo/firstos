#include "gpu_client.h"
#include "gpu_resource.h"
#include "serial.h"

static gpu_client_t s_clients[GPU_CLIENT_MAX];
static u32          s_next_client_id = 0;

static void write_dec_client(u64 val) {
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

const char* gpu_client_type_name(gpu_client_type_t type) {
    switch (type) {
        case GPU_CLIENT_COMPOSITOR:  return "compositor";
        case GPU_CLIENT_CURSOR:      return "cursor";
        case GPU_CLIENT_SYSTEM_UI:   return "system_ui";
        case GPU_CLIENT_APPLICATION: return "application";
        case GPU_CLIENT_RECOVERY:    return "recovery";
        default:                     return "unknown";
    }
}

const char* gpu_client_state_name(gpu_client_state_t state) {
    switch (state) {
        case GPU_CLIENT_ACTIVE:    return "ACTIVE";
        case GPU_CLIENT_THROTTLED: return "THROTTLED";
        case GPU_CLIENT_BLOCKED:   return "BLOCKED";
        case GPU_CLIENT_DYING:     return "DYING";
        case GPU_CLIENT_DEAD:      return "DEAD";
        default:                   return "UNKNOWN";
    }
}

int gpu_client_subsystem_init(void) {
    s_next_client_id = 0;
    for (int i = 0; i < GPU_CLIENT_MAX; i++) {
        s_clients[i].client_id = 0;
        s_clients[i].type = GPU_CLIENT_APPLICATION;
        s_clients[i].state = GPU_CLIENT_DEAD;
        s_clients[i].priority = 0;
        s_clients[i].weight = 0;
        s_clients[i].quota_bytes = 0;
        s_clients[i].used_bytes = 0;
        s_clients[i].max_inflight = 0;
        s_clients[i].inflight = 0;
        s_clients[i].max_queue_depth = 0;
        s_clients[i].queue_depth = 0;
        s_clients[i].submitted = 0;
        s_clients[i].completed = 0;
        s_clients[i].dropped = 0;
        s_clients[i].throttled = 0;
        s_clients[i].coalesced = 0;
        s_clients[i].generation = 0;
    }
    return 0;
}

gpu_client_t* gpu_client_register(gpu_client_type_t type) {
    int slot = -1;
    for (int i = 0; i < GPU_CLIENT_MAX; i++) {
        if (s_clients[i].state == GPU_CLIENT_DEAD || s_clients[i].client_id == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        serial_write("[gpu_client] registration failed: client pool full\n");
        return 0;
    }

    gpu_client_t *c = &s_clients[slot];
    c->client_id = ++s_next_client_id;
    c->type = type;
    c->state = GPU_CLIENT_ACTIVE;
    c->generation = gpu_resource_get_generation();
    c->used_bytes = 0;
    c->inflight = 0;
    c->queue_depth = 0;
    c->submitted = 0;
    c->completed = 0;
    c->dropped = 0;
    c->throttled = 0;
    c->coalesced = 0;
    c->max_queue_depth = GPU_CLIENT_DEFAULT_MAX_QUEUE_DEPTH;

    switch (type) {
        case GPU_CLIENT_RECOVERY:
            c->priority = 100;
            c->weight = 20;
            c->quota_bytes = 64 * 1024 * 1024ULL;
            c->max_inflight = 2;
            break;
        case GPU_CLIENT_COMPOSITOR:
            c->priority = 90;
            c->weight = 10;
            c->quota_bytes = 64 * 1024 * 1024ULL;
            c->max_inflight = 2;
            break;
        case GPU_CLIENT_CURSOR:
            c->priority = 80;
            c->weight = 8;
            c->quota_bytes = 4 * 1024 * 1024ULL;
            c->max_inflight = 4;
            break;
        case GPU_CLIENT_SYSTEM_UI:
            c->priority = 70;
            c->weight = 6;
            c->quota_bytes = 32 * 1024 * 1024ULL;
            c->max_inflight = 2;
            break;
        case GPU_CLIENT_APPLICATION:
        default:
            c->priority = 50;
            c->weight = 4;
            c->quota_bytes = 32 * 1024 * 1024ULL;
            c->max_inflight = 2;
            break;
    }

    serial_write("[gpu_client] registered id=");
    write_dec_client(c->client_id);
    serial_write(" type=");
    serial_write(gpu_client_type_name(c->type));
    serial_write(" quota=");
    write_dec_client(c->quota_bytes);
    serial_write("\n");

    return c;
}

int gpu_client_unregister(u32 client_id) {
    if (client_id == 0) return -1;
    for (int i = 0; i < GPU_CLIENT_MAX; i++) {
        if (s_clients[i].client_id == client_id && s_clients[i].state != GPU_CLIENT_DEAD) {
            extern int gpu_cancel_requests(u32 cid);
            gpu_cancel_requests(client_id);

            s_clients[i].state = GPU_CLIENT_DEAD;
            s_clients[i].client_id = 0;
            s_clients[i].used_bytes = 0;
            s_clients[i].inflight = 0;
            s_clients[i].queue_depth = 0;

            serial_write("[gpu_client] unregistered id=");
            write_dec_client(client_id);
            serial_write("\n");
            return 0;
        }
    }
    return -2;
}

gpu_client_t* gpu_client_get(u32 client_id) {
    if (client_id == 0) return 0;
    for (int i = 0; i < GPU_CLIENT_MAX; i++) {
        if (s_clients[i].client_id == client_id && s_clients[i].state != GPU_CLIENT_DEAD) {
            return &s_clients[i];
        }
    }
    return 0;
}

int gpu_set_client_quota(u32 client_id, u64 bytes, u32 max_inflight) {
    gpu_client_t *c = gpu_client_get(client_id);
    if (!c) return -1;
    if (bytes > 0) c->quota_bytes = bytes;
    if (max_inflight > 0) c->max_inflight = max_inflight;
    return 0;
}

int gpu_get_client_stats(u32 client_id, gpu_client_stats_t *out) {
    if (!out) return -1;
    gpu_client_t *c = gpu_client_get(client_id);
    if (!c) return -2;

    out->client_id = c->client_id;
    out->type = c->type;
    out->state = c->state;
    out->priority = c->priority;
    out->quota_bytes = c->quota_bytes;
    out->used_bytes = c->used_bytes;
    out->inflight = c->inflight;
    out->max_inflight = c->max_inflight;
    out->queue_depth = c->queue_depth;
    out->submitted = c->submitted;
    out->completed = c->completed;
    out->dropped = c->dropped;
    out->throttled = c->throttled;
    out->coalesced = c->coalesced;
    out->generation = c->generation;
    return 0;
}

void gpu_client_refresh_all_generations(u32 new_generation) {
    for (int i = 0; i < GPU_CLIENT_MAX; i++) {
        if (s_clients[i].state != GPU_CLIENT_DEAD && s_clients[i].client_id != 0) {
            s_clients[i].generation = new_generation;
        }
    }
}

u32 gpu_client_count(void) {
    u32 count = 0;
    for (int i = 0; i < GPU_CLIENT_MAX; i++) {
        if (s_clients[i].state != GPU_CLIENT_DEAD && s_clients[i].client_id != 0) {
            count++;
        }
    }
    return count;
}
