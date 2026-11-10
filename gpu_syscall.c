#include "gpu_syscall.h"
#include "gpu_user_client.h"
#include "serial.h"

extern int gpu_abi_open_client(const gpu_client_open_t *open_req, gpu_client_open_result_t *open_res);
extern int gpu_abi_close_client(gpu_client_handle_t handle);
extern int gpu_abi_query_device(gpu_client_handle_t handle, gpu_device_info_t *info);
extern int gpu_abi_create_resource(gpu_client_handle_t handle, const gpu_resource_create_t *create_req, gpu_resource_create_result_t *create_res);
extern int gpu_abi_close_resource(gpu_client_handle_t handle, const gpu_resource_ref_t *ref);
extern int gpu_abi_submit_present(gpu_client_handle_t handle, const gpu_present_submit_t *submit_req, gpu_present_result_t *submit_res);
extern int gpu_abi_poll_event(gpu_client_handle_t handle, gpu_user_event_t *event_res);
extern int gpu_abi_wait_event(gpu_client_handle_t handle, const gpu_event_wait_t *wait_req, gpu_user_event_t *event_res);
extern int gpu_abi_get_metrics(gpu_client_handle_t handle, gpu_client_metrics_abi_t *metrics_res);

extern u64 g_abi_calls;
extern u64 g_abi_rejected;
extern u64 g_abi_copyin_failures;
extern u64 g_abi_copyout_failures;

int gpu_syscall_init(void) {
    return GPU_STATUS_OK;
}

int gpu_copy_from_user(void *kdst, const void *usrc, u32 size) {
    if (!kdst || !usrc || size == 0) {
        g_abi_copyin_failures++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    uintptr_t src_addr = (uintptr_t)usrc;
    if (src_addr + size < src_addr) {
        g_abi_copyin_failures++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    const u8 *s = (const u8 *)usrc;
    u8 *d = (u8 *)kdst;
    for (u32 i = 0; i < size; i++) {
        d[i] = s[i];
    }

    return GPU_STATUS_OK;
}

int gpu_copy_to_user(void *udst, const void *ksrc, u32 size) {
    if (!udst || !ksrc || size == 0) {
        g_abi_copyout_failures++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    uintptr_t dst_addr = (uintptr_t)udst;
    if (dst_addr + size < dst_addr) {
        g_abi_copyout_failures++;
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    const u8 *s = (const u8 *)ksrc;
    u8 *d = (u8 *)udst;
    for (u32 i = 0; i < size; i++) {
        d[i] = s[i];
    }

    return GPU_STATUS_OK;
}

int gpu_validate_abi_header(const gpu_abi_header_t *hdr, u16 expected_size, u32 allowed_flags) {
    if (!hdr) return GPU_STATUS_INVALID_ARGUMENT;

    if (hdr->version != GPU_ABI_VERSION) {
        return GPU_STATUS_BAD_VERSION;
    }

    if (hdr->size < expected_size) {
        return GPU_STATUS_BUFFER_TOO_SMALL;
    }

    if ((hdr->flags & ~allowed_flags) != 0) {
        return GPU_STATUS_INVALID_ARGUMENT;
    }

    return GPU_STATUS_OK;
}

int gpu_syscall_dispatch(u32 op,
                         gpu_client_handle_t client_handle,
                         const void *user_in,
                         u32 in_size,
                         void *user_out,
                         u32 out_size) {
    g_abi_calls++;

    int status = GPU_STATUS_OK;

    switch (op) {
        case GPU_OP_OPEN_CLIENT: {
            if (in_size < sizeof(gpu_client_open_t) || out_size < sizeof(gpu_client_open_result_t)) {
                g_abi_rejected++;
                return GPU_STATUS_BUFFER_TOO_SMALL;
            }
            gpu_client_open_t open_req;
            if (gpu_copy_from_user(&open_req, user_in, sizeof(open_req)) != GPU_STATUS_OK) {
                g_abi_rejected++;
                return GPU_STATUS_INVALID_ARGUMENT;
            }
            gpu_client_open_result_t open_res;
            status = gpu_abi_open_client(&open_req, &open_res);
            if (status == GPU_STATUS_OK) {
                if (gpu_copy_to_user(user_out, &open_res, sizeof(open_res)) != GPU_STATUS_OK) {
                    g_abi_rejected++;
                    return GPU_STATUS_INVALID_ARGUMENT;
                }
            }
            break;
        }

        case GPU_OP_CLOSE_CLIENT: {
            status = gpu_abi_close_client(client_handle);
            break;
        }

        case GPU_OP_QUERY_DEVICE: {
            if (out_size < sizeof(gpu_device_info_t)) {
                g_abi_rejected++;
                return GPU_STATUS_BUFFER_TOO_SMALL;
            }
            gpu_device_info_t info;
            status = gpu_abi_query_device(client_handle, &info);
            if (status == GPU_STATUS_OK) {
                if (gpu_copy_to_user(user_out, &info, sizeof(info)) != GPU_STATUS_OK) {
                    g_abi_rejected++;
                    return GPU_STATUS_INVALID_ARGUMENT;
                }
            }
            break;
        }

        case GPU_OP_CREATE_RESOURCE: {
            if (in_size < sizeof(gpu_resource_create_t) || out_size < sizeof(gpu_resource_create_result_t)) {
                g_abi_rejected++;
                return GPU_STATUS_BUFFER_TOO_SMALL;
            }
            gpu_resource_create_t create_req;
            if (gpu_copy_from_user(&create_req, user_in, sizeof(create_req)) != GPU_STATUS_OK) {
                g_abi_rejected++;
                return GPU_STATUS_INVALID_ARGUMENT;
            }
            gpu_resource_create_result_t create_res;
            status = gpu_abi_create_resource(client_handle, &create_req, &create_res);
            if (status == GPU_STATUS_OK) {
                if (gpu_copy_to_user(user_out, &create_res, sizeof(create_res)) != GPU_STATUS_OK) {
                    g_abi_rejected++;
                    return GPU_STATUS_INVALID_ARGUMENT;
                }
            }
            break;
        }

        case GPU_OP_CLOSE_RESOURCE: {
            if (in_size < sizeof(gpu_resource_ref_t)) {
                g_abi_rejected++;
                return GPU_STATUS_BUFFER_TOO_SMALL;
            }
            gpu_resource_ref_t ref;
            if (gpu_copy_from_user(&ref, user_in, sizeof(ref)) != GPU_STATUS_OK) {
                g_abi_rejected++;
                return GPU_STATUS_INVALID_ARGUMENT;
            }
            status = gpu_abi_close_resource(client_handle, &ref);
            break;
        }

        case GPU_OP_SUBMIT_PRESENT: {
            if (in_size < sizeof(gpu_present_submit_t) || out_size < sizeof(gpu_present_result_t)) {
                g_abi_rejected++;
                return GPU_STATUS_BUFFER_TOO_SMALL;
            }
            gpu_present_submit_t submit_req;
            if (gpu_copy_from_user(&submit_req, user_in, sizeof(submit_req)) != GPU_STATUS_OK) {
                g_abi_rejected++;
                return GPU_STATUS_INVALID_ARGUMENT;
            }
            gpu_present_result_t submit_res;
            status = gpu_abi_submit_present(client_handle, &submit_req, &submit_res);
            if (status == GPU_STATUS_OK) {
                if (gpu_copy_to_user(user_out, &submit_res, sizeof(submit_res)) != GPU_STATUS_OK) {
                    g_abi_rejected++;
                    return GPU_STATUS_INVALID_ARGUMENT;
                }
            }
            break;
        }

        case GPU_OP_POLL_EVENT: {
            if (out_size < sizeof(gpu_user_event_t)) {
                g_abi_rejected++;
                return GPU_STATUS_BUFFER_TOO_SMALL;
            }
            gpu_user_event_t ev;
            status = gpu_abi_poll_event(client_handle, &ev);
            if (status == GPU_STATUS_OK) {
                if (gpu_copy_to_user(user_out, &ev, sizeof(ev)) != GPU_STATUS_OK) {
                    g_abi_rejected++;
                    return GPU_STATUS_INVALID_ARGUMENT;
                }
            }
            break;
        }

        case GPU_OP_WAIT_EVENT: {
            if (in_size < sizeof(gpu_event_wait_t) || out_size < sizeof(gpu_user_event_t)) {
                g_abi_rejected++;
                return GPU_STATUS_BUFFER_TOO_SMALL;
            }
            gpu_event_wait_t wait_req;
            if (gpu_copy_from_user(&wait_req, user_in, sizeof(wait_req)) != GPU_STATUS_OK) {
                g_abi_rejected++;
                return GPU_STATUS_INVALID_ARGUMENT;
            }
            gpu_user_event_t ev;
            status = gpu_abi_wait_event(client_handle, &wait_req, &ev);
            if (status == GPU_STATUS_OK) {
                if (gpu_copy_to_user(user_out, &ev, sizeof(ev)) != GPU_STATUS_OK) {
                    g_abi_rejected++;
                    return GPU_STATUS_INVALID_ARGUMENT;
                }
            }
            break;
        }

        case GPU_OP_GET_METRICS: {
            if (out_size < sizeof(gpu_client_metrics_abi_t)) {
                g_abi_rejected++;
                return GPU_STATUS_BUFFER_TOO_SMALL;
            }
            gpu_client_metrics_abi_t metrics;
            status = gpu_abi_get_metrics(client_handle, &metrics);
            if (status == GPU_STATUS_OK) {
                if (gpu_copy_to_user(user_out, &metrics, sizeof(metrics)) != GPU_STATUS_OK) {
                    g_abi_rejected++;
                    return GPU_STATUS_INVALID_ARGUMENT;
                }
            }
            break;
        }

        default:
            g_abi_rejected++;
            return GPU_STATUS_UNSUPPORTED;
    }

    if (status != GPU_STATUS_OK) {
        g_abi_rejected++;
    }

    return status;
}
