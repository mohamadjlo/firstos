#ifndef GPU_RESOURCE_H
#define GPU_RESOURCE_H

#include "io.h"
#include "gpu_dma.h"
#include "gpu_memcheck.h"

#define GPU_RESOURCE_REGISTRY_CAPACITY 64

typedef enum {
    GPU_RESOURCE_FREE = 0,
    GPU_RESOURCE_ALLOCATED,
    GPU_RESOURCE_MAPPED,
    GPU_RESOURCE_SUBMITTED,
    GPU_RESOURCE_IN_USE,
    GPU_RESOURCE_QUARANTINED,
    GPU_RESOURCE_DESTROYING,
    GPU_RESOURCE_DEAD
} gpu_resource_state_t;

typedef enum {
    GPU_OWNER_NONE = 0,
    GPU_OWNER_COMPOSITOR,
    GPU_OWNER_BUFFER_MANAGER,
    GPU_OWNER_DRIVER,
    GPU_OWNER_RECOVERY
} gpu_resource_owner_t;

typedef struct gpu_resource {
    u64                  resource_id;
    u32                  generation;
    void                *cpu_addr;
    u64                  phys_addr;
    u64                  dma_addr;
    u64                  size;
    u64                  alignment;
    u32                  width;
    u32                  height;
    u32                  pitch;
    u32                  format;
    gpu_resource_state_t state;
    u32                  refcount;
    u32                  owner;
    u32                  flags;
    u32                  virtio_res_id;
} gpu_resource_t;

int             gpu_resource_registry_init(void);
gpu_resource_t* gpu_resource_create(u32 width, u32 height, u32 bpp, u64 alignment, u32 owner, u32 flags);
gpu_resource_t* gpu_resource_lookup(u64 id, u32 generation);
int             gpu_resource_register(gpu_resource_t *resource);
int             gpu_resource_unregister(gpu_resource_t *resource);
int             gpu_resource_get(gpu_resource_t *resource);
int             gpu_resource_put(gpu_resource_t *resource);
int             gpu_resource_destroy(gpu_resource_t *resource);
u32             gpu_resource_count(void);

void            gpu_resource_quarantine_all_active(void);
void            gpu_resource_recreate_all(u32 width, u32 height, u32 bpp);
void            gpu_resource_advance_generation(void);
u32             gpu_resource_get_generation(void);
const char*     gpu_resource_state_name(gpu_resource_state_t state);

/* VirtIO Resource Association */
gpu_resource_t* gpu_resource_lookup_by_virtio_id(u32 virtio_res_id);
int             gpu_resource_bind_virtio_id(gpu_resource_t *res, u32 virtio_res_id);
int             gpu_resource_validate_virtio_id(u32 virtio_res_id);

#endif
