#include "gpu_user_client.h"
#include "gpu_handle.h"
#include "gpu_arbitration.h"
#include "gpu_client.h"
#include "gpu_event_user.h"
#include "serial.h"

static gpu_user_client_t s_user_clients[GPU_USER_CLIENT_MAX];
static int               s_user_clients_initialized = 0;

int gpu_user_client_init(void) {
    for (int i = 0; i < GPU_USER_CLIENT_MAX; i++) {
        s_user_clients[i].slot = (u32)i;
        s_user_clients[i].generation = 1;
        s_user_clients[i].handle = 0;
        s_user_clients[i].kernel_client_id = 0;
        s_user_clients[i].client_type = GPU_CLIENT_APPLICATION;
        s_user_clients[i].permissions = 0;
        s_user_clients[i].in_use = 0;
        s_user_clients[i].presents_submitted = 0;
        s_user_clients[i].presents_denied = 0;
        s_user_clients[i].quota_rejects = 0;
    }
    s_user_clients_initialized = 1;
    return GPU_STATUS_OK;
}

int gpu_user_client_open(gpu_client_type_t type,
                         gpu_client_handle_t *out_handle,
                         u32 *out_gen,
                         u32 *out_perms) {
    if (!out_handle || !out_gen || !out_perms) {
        return GPU_STATUS_INVALID_ARGUMENT;
    }
    *out_handle = 0;
    *out_gen = 0;
    *out_perms = 0;

    if (!s_user_clients_initialized) {
        gpu_user_client_init();
    }

    u32 perms = 0;
    switch (type) {
        case GPU_CLIENT_COMPOSITOR:
            perms = GPU_PERM_ALLOC | GPU_PERM_PRESENT | GPU_PERM_CURSOR |
                    GPU_PERM_MODESET | GPU_PERM_QUERY | GPU_PERM_DEBUG;
            break;
        case GPU_CLIENT_SYSTEM_UI:
            perms = GPU_PERM_ALLOC | GPU_PERM_PRESENT | GPU_PERM_CURSOR | GPU_PERM_QUERY;
            break;
        case GPU_CLIENT_APPLICATION:
            perms = GPU_PERM_ALLOC | GPU_PERM_PRESENT | GPU_PERM_QUERY;
            break;
        case GPU_CLIENT_CURSOR:
            perms = GPU_PERM_CURSOR | GPU_PERM_QUERY;
            break;
        case GPU_CLIENT_RECOVERY:
            perms = GPU_PERM_QUERY | GPU_PERM_DEBUG;
            break;
        default:
            return GPU_STATUS_INVALID_ARGUMENT;
    }

    int free_slot = -1;
    for (int i = 0; i < GPU_USER_CLIENT_MAX; i++) {
        if (!s_user_clients[i].in_use) {
            free_slot = i;
            break;
        }
    }

    if (free_slot < 0) {
        return GPU_STATUS_QUEUE_FULL;
    }

    gpu_client_t *kclient = gpu_client_register(type);
    if (!kclient) {
        return GPU_STATUS_QUOTA_EXCEEDED;
    }

    gpu_user_client_t *ucli = &s_user_clients[free_slot];
    ucli->kernel_client_id = kclient->client_id;
    ucli->client_type = type;
    ucli->permissions = perms;
    ucli->in_use = 1;
    ucli->presents_submitted = 0;
    ucli->presents_denied = 0;
    ucli->quota_rejects = 0;

    gpu_event_user_purge((u32)free_slot);

    gpu_client_handle_t h = ((ucli->generation & 0xFFFF) << 16) | ((u32)free_slot & 0xFFFF);
    ucli->handle = h;

    *out_handle = h;
    *out_gen = ucli->generation;
    *out_perms = perms;

    return GPU_STATUS_OK;
}

int gpu_user_client_close(gpu_client_handle_t handle) {
    if (handle == 0) return GPU_STATUS_INVALID_HANDLE;

    u32 slot = handle & 0xFFFF;
    u32 gen = (handle >> 16) & 0xFFFF;

    if (slot >= GPU_USER_CLIENT_MAX) {
        return GPU_STATUS_INVALID_HANDLE;
    }

    gpu_user_client_t *ucli = &s_user_clients[slot];
    if (!ucli->in_use) {
        return GPU_STATUS_INVALID_HANDLE;
    }

    if ((ucli->generation & 0xFFFF) != gen) {
        return GPU_STATUS_GENERATION_MISMATCH;
    }

    /* Cancel any in-flight or queued requests from this client */
    gpu_cancel_requests(ucli->kernel_client_id);

    /* Close all open handles belonging to this client */
    gpu_handle_close_client_all(ucli->kernel_client_id);

    /* Purge event queue */
    gpu_event_user_purge(slot);

    /* Unregister kernel client */
    gpu_client_unregister(ucli->kernel_client_id);

    ucli->in_use = 0;
    ucli->kernel_client_id = 0;
    ucli->permissions = 0;
    ucli->handle = 0;

    /* Advance generation */
    ucli->generation = (ucli->generation + 1) & 0xFFFF;
    if (ucli->generation == 0) ucli->generation = 1;

    return GPU_STATUS_OK;
}

gpu_user_client_t* gpu_user_client_lookup(gpu_client_handle_t handle) {
    if (handle == 0) return 0;
    u32 slot = handle & 0xFFFF;
    u32 gen = (handle >> 16) & 0xFFFF;

    if (slot >= GPU_USER_CLIENT_MAX) return 0;

    gpu_user_client_t *ucli = &s_user_clients[slot];
    if (!ucli->in_use) return 0;
    if ((ucli->generation & 0xFFFF) != gen) return 0;

    return ucli;
}

gpu_user_client_t* gpu_user_client_get_by_id(u32 user_client_id) {
    if (user_client_id >= GPU_USER_CLIENT_MAX) return 0;
    if (!s_user_clients[user_client_id].in_use) return 0;
    return &s_user_clients[user_client_id];
}

gpu_user_client_t* gpu_user_client_get_by_kernel_id(u32 kernel_client_id) {
    for (int i = 0; i < GPU_USER_CLIENT_MAX; i++) {
        if (s_user_clients[i].in_use && s_user_clients[i].kernel_client_id == kernel_client_id) {
            return &s_user_clients[i];
        }
    }
    return 0;
}

int gpu_user_client_check_perm(gpu_user_client_t *client, u32 required_perms) {
    if (!client) return GPU_STATUS_PERMISSION_DENIED;
    if ((client->permissions & required_perms) == required_perms) {
        return GPU_STATUS_OK;
    }
    return GPU_STATUS_PERMISSION_DENIED;
}

void gpu_user_client_refresh_generations(u32 new_generation) {
    (void)new_generation;
    for (int i = 0; i < GPU_USER_CLIENT_MAX; i++) {
        if (s_user_clients[i].in_use) {
            s_user_clients[i].generation = (s_user_clients[i].generation + 1) & 0xFFFF;
            if (s_user_clients[i].generation == 0) s_user_clients[i].generation = 1;
            s_user_clients[i].handle = ((s_user_clients[i].generation & 0xFFFF) << 16) | ((u32)i & 0xFFFF);
        }
    }
}

void gpu_user_client_quarantine_all(void) {
    for (int i = 0; i < GPU_USER_CLIENT_MAX; i++) {
        if (s_user_clients[i].in_use) {
            gpu_cancel_requests(s_user_clients[i].kernel_client_id);
            gpu_handle_close_client_all(s_user_clients[i].kernel_client_id);
            gpu_client_unregister(s_user_clients[i].kernel_client_id);
            s_user_clients[i].in_use = 0;
            s_user_clients[i].generation = (s_user_clients[i].generation + 1) & 0xFFFF;
            if (s_user_clients[i].generation == 0) s_user_clients[i].generation = 1;
        }
    }
}

u32 gpu_user_client_active_count(void) {
    u32 count = 0;
    for (int i = 0; i < GPU_USER_CLIENT_MAX; i++) {
        if (s_user_clients[i].in_use) count++;
    }
    return count;
}
