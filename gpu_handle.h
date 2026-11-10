#ifndef GPU_HANDLE_H
#define GPU_HANDLE_H

#include "io.h"
#include "gpu_abi.h"
#include "gpu_resource.h"

#define GPU_HANDLE_TABLE_CAPACITY 128

#define GPU_ACCESS_READ   (1u << 0)
#define GPU_ACCESS_WRITE  (1u << 1)
#define GPU_ACCESS_MAP    (1u << 2)

typedef struct {
    u32                  index;
    u32                  generation;
    u32                  client_id;
    u32                  access;
    struct gpu_resource *resource;
    int                  in_use;
} gpu_handle_entry_t;

int  gpu_handle_init(void);
int  gpu_handle_create_resource(u32 client_id,
                                struct gpu_resource *resource,
                                u32 access,
                                gpu_handle_t *out);

int  gpu_handle_lookup_resource(u32 client_id,
                                gpu_handle_t handle,
                                u32 generation,
                                struct gpu_resource **out);

int  gpu_handle_close(u32 client_id,
                      gpu_handle_t handle);

void gpu_handle_close_client_all(u32 client_id);
void gpu_handle_quarantine_all(void);
u32  gpu_handle_active_count(void);

#endif /* GPU_HANDLE_H */
