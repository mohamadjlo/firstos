#include "gpu_abi.h"
#include "gpu_handle.h"
#include "gpu_user_client.h"
#include "gpu_event_user.h"
#include "gpu_syscall.h"
#include "gpu_arbitration.h"
#include "gpu_resource.h"
#include "gpu_detect.h"
#include "gpu_health.h"
#include "serial.h"

/* ABI Global Telemetry Metrics */
u64 g_abi_calls = 0;
u64 g_abi_rejected = 0;
u64 g_abi_bad_versions = 0;
u64 g_abi_bad_handles = 0;
u64 g_abi_permission_denied = 0;
u64 g_abi_generation_rejected = 0;
u64 g_abi_region_rejected = 0;
u64 g_abi_quota_rejected = 0;
u64 g_abi_queue_full = 0;
u64 g_abi_events_delivered = 0;
u64 g_abi_events_dropped = 0;
u64 g_abi_copyin_failures = 0;
u64 g_abi_copyout_failures = 0;
u64 g_abi_fuzz_rejections = 0;

static void write_dec_abi(u64 val) {
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

const char* gpu_status_string(int status) {
    switch (status) {
        case GPU_STATUS_OK:                  return "OK";
        case GPU_STATUS_INVALID_ARGUMENT:    return "INVALID_ARGUMENT";
        case GPU_STATUS_BAD_VERSION:         return "BAD_VERSION";
        case GPU_STATUS_BUFFER_TOO_SMALL:    return "BUFFER_TOO_SMALL";
        case GPU_STATUS_PERMISSION_DENIED:   return "PERMISSION_DENIED";
        case GPU_STATUS_INVALID_HANDLE:      return "INVALID_HANDLE";
        case GPU_STATUS_GENERATION_MISMATCH: return "GENERATION_MISMATCH";
        case GPU_STATUS_RESOURCE_BUSY:       return "RESOURCE_BUSY";
        case GPU_STATUS_QUOTA_EXCEEDED:      return "QUOTA_EXCEEDED";
        case GPU_STATUS_QUEUE_FULL:          return "QUEUE_FULL";
        case GPU_STATUS_DEVICE_LOST:         return "DEVICE_LOST";
        case GPU_STATUS_RECOVERY:            return "RECOVERY";
        case GPU_STATUS_UNSUPPORTED:         return "UNSUPPORTED";
        case GPU_STATUS_TIMEOUT:             return "TIMEOUT";
        case GPU_STATUS_NO_PRESENT:          return "NO_PRESENT";
        default:                             return "UNKNOWN_STATUS";
    }
}

int gpu_abi_init(void) {
    gpu_handle_init();
    gpu_user_client_init();
    gpu_event_user_init();
    gpu_syscall_init();

    g_abi_calls = 0;
    g_abi_rejected = 0;
    g_abi_bad_versions = 0;
    g_abi_bad_handles = 0;
    g_abi_permission_denied = 0;
    g_abi_generation_rejected = 0;
    g_abi_region_rejected = 0;
    g_abi_quota_rejected = 0;
    g_abi_queue_full = 0;
    g_abi_events_delivered = 0;
    g_abi_events_dropped = 0;
    g_abi_copyin_failures = 0;
    g_abi_copyout_failures = 0;
    g_abi_fuzz_rejections = 0;

    serial_write("[gpu_phase10] user client ABI initialized version=1\n");
    return GPU_STATUS_OK;
}

void gpu_abi_register_default_clients(void) {
    gpu_client_handle_t comp_h = 0;
    u32 comp_gen = 0;
    u32 comp_perms = 0;
    int cr = gpu_user_client_open(GPU_CLIENT_COMPOSITOR, &comp_h, &comp_gen, &comp_perms);
    if (cr == GPU_STATUS_OK) {
        serial_write("[gpu_phase10] compositor ABI client ready\n");
    }

    gpu_client_handle_t app_h = 0;
    u32 app_gen = 0;
    u32 app_perms = 0;
    int ar = gpu_user_client_open(GPU_CLIENT_APPLICATION, &app_h, &app_gen, &app_perms);
    if (ar == GPU_STATUS_OK) {
        serial_write("[gpu_phase10] application display ABI ready\n");
    }

    serial_write("[gpu_phase10] safe user-space submission path enabled\n");
}

int gpu_abi_open_client(const gpu_client_open_t *open_req, gpu_client_open_result_t *open_res) {
    if (!open_req || !open_res) {
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    int v = gpu_validate_abi_header(&open_req->header, (u16)sizeof(gpu_client_open_t), 0);
    if (v != GPU_STATUS_OK) {
        if (v == GPU_STATUS_BAD_VERSION) g_abi_bad_versions++;
        g_abi_rejected++;
        return v;
    }

    if (open_req->reserved != 0) {
        g_abi_fuzz_rejections++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    gpu_client_handle_t handle = 0;
    u32 gen = 0;
    u32 perms = 0;

    int res = gpu_user_client_open((gpu_client_type_t)open_req->client_type, &handle, &gen, &perms);
    if (res != GPU_STATUS_OK) {
        g_abi_rejected++;
        return res;
    }

    open_res->header.version = GPU_ABI_VERSION;
    open_res->header.size = (u16)sizeof(gpu_client_open_result_t);
    open_res->header.flags = 0;
    open_res->client_handle = handle;
    open_res->generation = gen;
    open_res->permissions = perms;
    open_res->reserved = 0;

    return GPU_STATUS_OK;
}

int gpu_abi_close_client(gpu_client_handle_t handle) {
    int res = gpu_user_client_close(handle);
    if (res != GPU_STATUS_OK) {
        if (res == GPU_STATUS_GENERATION_MISMATCH) g_abi_generation_rejected++;
        else g_abi_bad_handles++;
        g_abi_rejected++;
    }
    return res;
}

int gpu_abi_query_device(gpu_client_handle_t handle, gpu_device_info_t *info) {
    if (!info) {
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    gpu_user_client_t *cli = gpu_user_client_lookup(handle);
    if (!cli) {
        g_abi_bad_handles++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_HANDLE;
    }

    if (gpu_user_client_check_perm(cli, GPU_PERM_QUERY) != GPU_STATUS_OK) {
        g_abi_permission_denied++;
        g_abi_rejected++;
        return GPU_STATUS_PERMISSION_DENIED;
    }

    active_gpu_device_t *act = gpu_get_active();
    if (!act || !act->driver) {
        g_abi_rejected++;
        return GPU_STATUS_NO_PRESENT;
    }

    if (act->device_state == GPU_DEVICE_LOST) {
        g_abi_rejected++;
        return GPU_STATUS_DEVICE_LOST;
    }
    if (act->device_state == GPU_DEVICE_RESETTING || act->device_state == GPU_DEVICE_REINITIALIZING) {
        g_abi_rejected++;
        return GPU_STATUS_RECOVERY;
    }
    if (act->device_state == GPU_DEVICE_FAILED) {
        g_abi_rejected++;
        return GPU_STATUS_DEVICE_LOST;
    }

    info->header.version = GPU_ABI_VERSION;
    info->header.size = (u16)sizeof(gpu_device_info_t);
    info->header.flags = 0;
    info->active_driver = act->is_fallback ? 1 : 2;
    info->device_state = (u32)act->device_state;
    info->width = act->current_width;
    info->height = act->current_height;
    info->bpp = act->current_bpp;
    info->flags = act->is_accelerated ? 1 : 0;
    info->caps = act->caps;

    return GPU_STATUS_OK;
}

int gpu_abi_create_resource(gpu_client_handle_t handle,
                            const gpu_resource_create_t *create_req,
                            gpu_resource_create_result_t *create_res) {
    if (!create_req || !create_res) {
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    gpu_user_client_t *cli = gpu_user_client_lookup(handle);
    if (!cli) {
        g_abi_bad_handles++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_HANDLE;
    }

    if (gpu_user_client_check_perm(cli, GPU_PERM_ALLOC) != GPU_STATUS_OK) {
        g_abi_permission_denied++;
        g_abi_rejected++;
        return GPU_STATUS_PERMISSION_DENIED;
    }

    int v = gpu_validate_abi_header(&create_req->header, (u16)sizeof(gpu_resource_create_t), 0);
    if (v != GPU_STATUS_OK) {
        if (v == GPU_STATUS_BAD_VERSION) g_abi_bad_versions++;
        g_abi_rejected++;
        return v;
    }

    if (create_req->reserved != 0) {
        g_abi_fuzz_rejections++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    if (create_req->width == 0 || create_req->height == 0 ||
        create_req->width > 4096 || create_req->height > 4096) {
        g_abi_fuzz_rejections++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    if (create_req->bpp != 16 && create_req->bpp != 24 && create_req->bpp != 32) {
        g_abi_fuzz_rejections++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    u32 bytes_per_pixel = create_req->bpp / 8;
    u32 min_pitch = create_req->width * bytes_per_pixel;
    u32 pitch = min_pitch;
    if (create_req->pitch_alignment > 0) {
        u32 align = create_req->pitch_alignment;
        if ((align & (align - 1)) != 0) {
            g_abi_fuzz_rejections++;
            g_abi_rejected++;
            return GPU_STATUS_INVALID_ARGUMENT;
        }
        pitch = (pitch + align - 1) & ~(align - 1);
    }

    u64 size = (u64)pitch * (u64)create_req->height;

    /* Validate client quota */
    gpu_client_t *kc = gpu_client_get(cli->kernel_client_id);
    if (kc && (kc->used_bytes + size > kc->quota_bytes)) {
        g_abi_quota_rejected++;
        cli->quota_rejects++;
        g_abi_rejected++;
        return GPU_STATUS_QUOTA_EXCEEDED;
    }

    gpu_resource_t *res = gpu_resource_create(create_req->width,
                                             create_req->height,
                                             create_req->bpp,
                                             create_req->pitch_alignment,
                                             GPU_OWNER_COMPOSITOR,
                                             create_req->flags);
    if (!res) {
        g_abi_quota_rejected++;
        g_abi_rejected++;
        return GPU_STATUS_QUOTA_EXCEEDED;
    }

    gpu_handle_t out_h = 0;
    int hr = gpu_handle_create_resource(cli->kernel_client_id, res, GPU_ACCESS_READ | GPU_ACCESS_WRITE, &out_h);
    if (hr != GPU_STATUS_OK) {
        gpu_resource_put(res);
        g_abi_rejected++;
        return hr;
    }

    create_res->header.version = GPU_ABI_VERSION;
    create_res->header.size = (u16)sizeof(gpu_resource_create_result_t);
    create_res->header.flags = 0;
    create_res->handle = out_h;
    create_res->generation = res->generation;
    create_res->width = res->width;
    create_res->height = res->height;
    create_res->pitch = res->pitch;
    create_res->bpp = res->format;
    create_res->size = res->size;

    return GPU_STATUS_OK;
}

int gpu_abi_close_resource(gpu_client_handle_t handle, const gpu_resource_ref_t *ref) {
    if (!ref) {
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    gpu_user_client_t *cli = gpu_user_client_lookup(handle);
    if (!cli) {
        g_abi_bad_handles++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_HANDLE;
    }

    int v = gpu_validate_abi_header(&ref->header, (u16)sizeof(gpu_resource_ref_t), 0x7);
    if (v != GPU_STATUS_OK) {
        if (v == GPU_STATUS_BAD_VERSION) g_abi_bad_versions++;
        g_abi_rejected++;
        return v;
    }

    if (ref->reserved != 0) {
        g_abi_fuzz_rejections++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    int res = gpu_handle_close(cli->kernel_client_id, ref->resource);
    if (res != GPU_STATUS_OK) {
        if (res == GPU_STATUS_GENERATION_MISMATCH) g_abi_generation_rejected++;
        else if (res == GPU_STATUS_PERMISSION_DENIED) g_abi_permission_denied++;
        else g_abi_bad_handles++;
        g_abi_rejected++;
    }

    return res;
}

int gpu_abi_submit_present(gpu_client_handle_t handle,
                           const gpu_present_submit_t *submit_req,
                           gpu_present_result_t *submit_res) {
    if (!submit_req || !submit_res) {
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    gpu_user_client_t *cli = gpu_user_client_lookup(handle);
    if (!cli) {
        g_abi_bad_handles++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_HANDLE;
    }

    if (gpu_user_client_check_perm(cli, GPU_PERM_PRESENT) != GPU_STATUS_OK) {
        g_abi_permission_denied++;
        cli->presents_denied++;
        g_abi_rejected++;
        return GPU_STATUS_PERMISSION_DENIED;
    }

    int v = gpu_validate_abi_header(&submit_req->header, (u16)sizeof(gpu_present_submit_t), 0x1F);
    if (v != GPU_STATUS_OK) {
        if (v == GPU_STATUS_BAD_VERSION) g_abi_bad_versions++;
        g_abi_rejected++;
        return v;
    }

    active_gpu_device_t *act = gpu_get_active();
    if (!act || !act->driver) {
        g_abi_rejected++;
        return GPU_STATUS_NO_PRESENT;
    }
    if (act->device_state == GPU_DEVICE_LOST) {
        g_abi_rejected++;
        return GPU_STATUS_DEVICE_LOST;
    }
    if (act->device_state == GPU_DEVICE_RESETTING || act->device_state == GPU_DEVICE_REINITIALIZING) {
        g_abi_rejected++;
        return GPU_STATUS_RECOVERY;
    }
    if (act->device_state == GPU_DEVICE_FAILED) {
        g_abi_rejected++;
        return GPU_STATUS_DEVICE_LOST;
    }

    /* Lookup resource handle */
    gpu_resource_t *res = 0;
    int hr = gpu_handle_lookup_resource(cli->kernel_client_id,
                                        submit_req->resource,
                                        submit_req->resource_generation,
                                        &res);
    if (hr != GPU_STATUS_OK) {
        if (hr == GPU_STATUS_GENERATION_MISMATCH) g_abi_generation_rejected++;
        else if (hr == GPU_STATUS_PERMISSION_DENIED) g_abi_permission_denied++;
        else g_abi_bad_handles++;
        g_abi_rejected++;
        return hr;
    }

    /* Dirty region bounds and overflow validation */
    if (submit_req->width > 0 && submit_req->height > 0) {
        if (submit_req->x + submit_req->width < submit_req->x ||
            submit_req->y + submit_req->height < submit_req->y ||
            submit_req->x + submit_req->width > res->width ||
            submit_req->y + submit_req->height > res->height) {
            g_abi_region_rejected++;
            g_abi_rejected++;
            return GPU_STATUS_INVALID_ARGUMENT;
        }
    }

    gpu_present_region_t region;
    region.x = submit_req->x;
    region.y = submit_req->y;
    region.w = submit_req->width;
    region.h = submit_req->height;

    u64 req_id = 0;
    int sres = gpu_submit_present_cookie(cli->kernel_client_id,
                                         res,
                                         &region,
                                         submit_req->flags,
                                         submit_req->user_cookie,
                                         &req_id);

    if (sres < 0) {
        g_abi_rejected++;
        if (sres == -1) return GPU_STATUS_INVALID_ARGUMENT;
        if (sres == -2) return GPU_STATUS_NO_PRESENT;
        if (sres == -3) { g_abi_generation_rejected++; return GPU_STATUS_GENERATION_MISMATCH; }
        if (sres == -4) return GPU_STATUS_INVALID_HANDLE;
        if (sres == -5) return GPU_STATUS_RESOURCE_BUSY;
        if (sres == -6) { g_abi_region_rejected++; return GPU_STATUS_INVALID_ARGUMENT; }
        if (sres == -7) { g_abi_quota_rejected++; cli->quota_rejects++; return GPU_STATUS_QUOTA_EXCEEDED; }
        if (sres == -8 || sres == -9 || sres == -11) { g_abi_queue_full++; return GPU_STATUS_QUEUE_FULL; }
        if (sres == -10) return GPU_STATUS_RECOVERY;
        return GPU_STATUS_RESOURCE_BUSY;
    }

    cli->presents_submitted++;

    submit_res->header.version = GPU_ABI_VERSION;
    submit_res->header.size = (u16)sizeof(gpu_present_result_t);
    submit_res->header.flags = 0;
    submit_res->request_id = req_id;
    submit_res->status = GPU_STATUS_OK;
    submit_res->queue_position = 0;
    submit_res->user_cookie = submit_req->user_cookie;

    return GPU_STATUS_OK;
}

int gpu_abi_poll_event(gpu_client_handle_t handle, gpu_user_event_t *event_res) {
    if (!event_res) {
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    gpu_user_client_t *cli = gpu_user_client_lookup(handle);
    if (!cli) {
        g_abi_bad_handles++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_HANDLE;
    }

    int res = gpu_event_user_poll(cli->slot, event_res);
    if (res == GPU_STATUS_OK) {
        g_abi_events_delivered++;
    }
    return res;
}

int gpu_abi_wait_event(gpu_client_handle_t handle,
                       const gpu_event_wait_t *wait_req,
                       gpu_user_event_t *event_res) {
    if (!wait_req || !event_res) {
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    gpu_user_client_t *cli = gpu_user_client_lookup(handle);
    if (!cli) {
        g_abi_bad_handles++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_HANDLE;
    }

    int v = gpu_validate_abi_header(&wait_req->header, (u16)sizeof(gpu_event_wait_t), 0);
    if (v != GPU_STATUS_OK) {
        if (v == GPU_STATUS_BAD_VERSION) g_abi_bad_versions++;
        g_abi_rejected++;
        return v;
    }

    if (wait_req->reserved != 0) {
        g_abi_fuzz_rejections++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    int res = gpu_event_user_wait(cli->slot, wait_req->timeout_ticks, event_res);
    if (res == GPU_STATUS_OK) {
        g_abi_events_delivered++;
    }
    return res;
}

int gpu_abi_get_metrics(gpu_client_handle_t handle, gpu_client_metrics_abi_t *metrics_res) {
    if (!metrics_res) {
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    gpu_user_client_t *cli = gpu_user_client_lookup(handle);
    if (!cli) {
        g_abi_bad_handles++;
        g_abi_rejected++;
        return GPU_STATUS_INVALID_HANDLE;
    }

    if (gpu_user_client_check_perm(cli, GPU_PERM_QUERY) != GPU_STATUS_OK) {
        g_abi_permission_denied++;
        g_abi_rejected++;
        return GPU_STATUS_PERMISSION_DENIED;
    }

    gpu_client_stats_t kstats;
    int sr = gpu_get_client_metrics(cli->kernel_client_id, &kstats);
    if (sr != 0) {
        g_abi_rejected++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    metrics_res->header.version = GPU_ABI_VERSION;
    metrics_res->header.size = (u16)sizeof(gpu_client_metrics_abi_t);
    metrics_res->header.flags = 0;
    metrics_res->submitted = kstats.submitted;
    metrics_res->completed = kstats.completed;
    metrics_res->dropped = kstats.dropped;
    metrics_res->throttled = kstats.throttled;
    metrics_res->coalesced = kstats.coalesced;
    metrics_res->used_bytes = kstats.used_bytes;
    metrics_res->quota_bytes = kstats.quota_bytes;
    metrics_res->inflight = kstats.inflight;
    metrics_res->max_inflight = kstats.max_inflight;
    metrics_res->queue_depth = kstats.queue_depth;
    metrics_res->reserved = 0;

    return GPU_STATUS_OK;
}

void gpu_abi_dump_metrics(void) {
    serial_write("[gpu_abi] clients=");
    write_dec_abi(gpu_user_client_active_count());
    serial_write(" calls=");
    write_dec_abi(g_abi_calls);
    serial_write(" rejected=");
    write_dec_abi(g_abi_rejected);
    serial_write(" delivered=");
    write_dec_abi(g_abi_events_delivered);
    serial_write(" dropped=");
    write_dec_abi(g_abi_events_dropped);
    serial_write("\n");

    for (u32 i = 0; i < GPU_USER_CLIENT_MAX; i++) {
        gpu_user_client_t *cli = gpu_user_client_get_by_id(i);
        if (cli && cli->in_use) {
            serial_write("[gpu_abi] client=");
            write_dec_abi(cli->slot);
            serial_write(" presents=");
            write_dec_abi(cli->presents_submitted);
            serial_write(" denied=");
            write_dec_abi(cli->presents_denied);
            serial_write(" quota_rejects=");
            write_dec_abi(cli->quota_rejects);
            serial_write("\n");
        }
    }
}
