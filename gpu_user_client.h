#ifndef GPU_USER_CLIENT_H
#define GPU_USER_CLIENT_H

#include "io.h"
#include "gpu_abi.h"
#include "gpu_client.h"

#define GPU_USER_CLIENT_MAX 16

/* User Client Permissions */
#define GPU_PERM_ALLOC   (1u << 0)
#define GPU_PERM_PRESENT (1u << 1)
#define GPU_PERM_CURSOR  (1u << 2)
#define GPU_PERM_MODESET (1u << 3)
#define GPU_PERM_QUERY   (1u << 4)
#define GPU_PERM_DEBUG   (1u << 5)

typedef struct {
    u32                 slot;
    u32                 generation;
    gpu_client_handle_t handle;
    u32                 kernel_client_id;
    gpu_client_type_t   client_type;
    u32                 permissions;
    int                 in_use;
    u64                 presents_submitted;
    u64                 presents_denied;
    u64                 quota_rejects;
} gpu_user_client_t;

int                gpu_user_client_init(void);
int                gpu_user_client_open(gpu_client_type_t type,
                                        gpu_client_handle_t *out_handle,
                                        u32 *out_gen,
                                        u32 *out_perms);
int                gpu_user_client_close(gpu_client_handle_t handle);
gpu_user_client_t* gpu_user_client_lookup(gpu_client_handle_t handle);
gpu_user_client_t* gpu_user_client_get_by_id(u32 user_client_id);
gpu_user_client_t* gpu_user_client_get_by_kernel_id(u32 kernel_client_id);
int                gpu_user_client_check_perm(gpu_user_client_t *client, u32 required_perms);
void               gpu_user_client_refresh_generations(u32 new_generation);
void               gpu_user_client_quarantine_all(void);
u32                gpu_user_client_active_count(void);

#endif /* GPU_USER_CLIENT_H */
