#include "gpu_handle.h"
#include "gpu_resource.h"
#include "serial.h"

static gpu_handle_entry_t s_handle_table[GPU_HANDLE_TABLE_CAPACITY];
static int                s_handle_initialized = 0;

int gpu_handle_init(void) {
    for (int i = 0; i < GPU_HANDLE_TABLE_CAPACITY; i++) {
        s_handle_table[i].index = (u32)i;
        s_handle_table[i].generation = 1;
        s_handle_table[i].client_id = 0;
        s_handle_table[i].access = 0;
        s_handle_table[i].resource = 0;
        s_handle_table[i].in_use = 0;
    }
    s_handle_initialized = 1;
    serial_write("[gpu_phase10] handle table initialized capacity=128\n");
    return GPU_STATUS_OK;
}

int gpu_handle_create_resource(u32 client_id,
                               struct gpu_resource *resource,
                               u32 access,
                               gpu_handle_t *out) {
    if (!out) return GPU_STATUS_INVALID_ARGUMENT;
    *out = 0;

    if (!s_handle_initialized) gpu_handle_init();
    if (!resource || resource->state == GPU_RESOURCE_DEAD || resource->state == GPU_RESOURCE_FREE) {
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    /* Find an available slot */
    int slot = -1;
    for (int i = 0; i < GPU_HANDLE_TABLE_CAPACITY; i++) {
        if (!s_handle_table[i].in_use) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        return GPU_STATUS_QUEUE_FULL;
    }

    gpu_handle_entry_t *entry = &s_handle_table[slot];
    entry->client_id = client_id;
    entry->access = access;
    entry->resource = resource;
    entry->in_use = 1;

    /* Acquire reference on the underlying resource */
    gpu_resource_get(resource);

    /* Construct token: (generation << 16) | slot */
    gpu_handle_t handle = ((entry->generation & 0xFFFF) << 16) | ((u32)slot & 0xFFFF);
    *out = handle;

    return GPU_STATUS_OK;
}

int gpu_handle_lookup_resource(u32 client_id,
                               gpu_handle_t handle,
                               u32 generation,
                               struct gpu_resource **out) {
    if (!out) return GPU_STATUS_INVALID_ARGUMENT;
    *out = 0;

    if (handle == 0) return GPU_STATUS_INVALID_HANDLE;

    u32 index = handle & 0xFFFF;
    u32 handle_gen = (handle >> 16) & 0xFFFF;

    if (index >= GPU_HANDLE_TABLE_CAPACITY) {
        return GPU_STATUS_INVALID_HANDLE;
    }

    gpu_handle_entry_t *entry = &s_handle_table[index];
    if (!entry->in_use) {
        return GPU_STATUS_INVALID_HANDLE;
    }

    if ((entry->generation & 0xFFFF) != handle_gen) {
        return GPU_STATUS_GENERATION_MISMATCH;
    }

    if (client_id != 0 && entry->client_id != client_id) {
        return GPU_STATUS_PERMISSION_DENIED;
    }

    struct gpu_resource *res = entry->resource;
    if (!res || res->state == GPU_RESOURCE_DEAD || res->state == GPU_RESOURCE_FREE) {
        return GPU_STATUS_INVALID_HANDLE;
    }

    if (res->state == GPU_RESOURCE_QUARANTINED) {
        return GPU_STATUS_RESOURCE_BUSY;
    }

    if (generation > 0 && res->generation != generation) {
        return GPU_STATUS_GENERATION_MISMATCH;
    }

    *out = res;
    return GPU_STATUS_OK;
}

int gpu_handle_close(u32 client_id, gpu_handle_t handle) {
    if (handle == 0) return GPU_STATUS_INVALID_HANDLE;

    u32 index = handle & 0xFFFF;
    u32 handle_gen = (handle >> 16) & 0xFFFF;

    if (index >= GPU_HANDLE_TABLE_CAPACITY) {
        return GPU_STATUS_INVALID_HANDLE;
    }

    gpu_handle_entry_t *entry = &s_handle_table[index];
    if (!entry->in_use) {
        return GPU_STATUS_INVALID_HANDLE;
    }

    if ((entry->generation & 0xFFFF) != handle_gen) {
        return GPU_STATUS_GENERATION_MISMATCH;
    }

    if (client_id != 0 && entry->client_id != client_id) {
        return GPU_STATUS_PERMISSION_DENIED;
    }

    if (entry->resource) {
        gpu_resource_put(entry->resource);
        entry->resource = 0;
    }

    entry->in_use = 0;
    entry->client_id = 0;
    entry->access = 0;

    /* Advance generation to prevent ABA handle reuse */
    entry->generation = (entry->generation + 1) & 0xFFFF;
    if (entry->generation == 0) entry->generation = 1;

    return GPU_STATUS_OK;
}

void gpu_handle_close_client_all(u32 client_id) {
    if (client_id == 0) return;
    for (int i = 0; i < GPU_HANDLE_TABLE_CAPACITY; i++) {
        if (s_handle_table[i].in_use && s_handle_table[i].client_id == client_id) {
            if (s_handle_table[i].resource) {
                gpu_resource_put(s_handle_table[i].resource);
                s_handle_table[i].resource = 0;
            }
            s_handle_table[i].in_use = 0;
            s_handle_table[i].client_id = 0;
            s_handle_table[i].generation = (s_handle_table[i].generation + 1) & 0xFFFF;
            if (s_handle_table[i].generation == 0) s_handle_table[i].generation = 1;
        }
    }
}

void gpu_handle_quarantine_all(void) {
    for (int i = 0; i < GPU_HANDLE_TABLE_CAPACITY; i++) {
        if (s_handle_table[i].in_use) {
            if (s_handle_table[i].resource) {
                gpu_resource_put(s_handle_table[i].resource);
                s_handle_table[i].resource = 0;
            }
            s_handle_table[i].in_use = 0;
            s_handle_table[i].generation = (s_handle_table[i].generation + 1) & 0xFFFF;
            if (s_handle_table[i].generation == 0) s_handle_table[i].generation = 1;
        }
    }
}

u32 gpu_handle_active_count(void) {
    u32 count = 0;
    for (int i = 0; i < GPU_HANDLE_TABLE_CAPACITY; i++) {
        if (s_handle_table[i].in_use) count++;
    }
    return count;
}
