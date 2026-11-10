#include "gpu_resource.h"
#include "gpu_detect.h"
#include "serial.h"

static gpu_resource_t s_registry[GPU_RESOURCE_REGISTRY_CAPACITY];
static u64            s_next_resource_id = 0;
static u32            s_current_generation = 1;

static void write_dec_res(u64 val) {
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

const char* gpu_resource_state_name(gpu_resource_state_t state) {
    switch (state) {
        case GPU_RESOURCE_FREE:        return "FREE";
        case GPU_RESOURCE_ALLOCATED:   return "ALLOCATED";
        case GPU_RESOURCE_MAPPED:      return "MAPPED";
        case GPU_RESOURCE_SUBMITTED:   return "SUBMITTED";
        case GPU_RESOURCE_IN_USE:      return "IN_USE";
        case GPU_RESOURCE_QUARANTINED: return "QUARANTINED";
        case GPU_RESOURCE_DESTROYING:  return "DESTROYING";
        case GPU_RESOURCE_DEAD:        return "DEAD";
        default:                       return "UNKNOWN";
    }
}

int gpu_resource_registry_init(void) {
    for (int i = 0; i < GPU_RESOURCE_REGISTRY_CAPACITY; i++) {
        s_registry[i].resource_id = 0;
        s_registry[i].generation = s_current_generation;
        s_registry[i].cpu_addr = 0;
        s_registry[i].phys_addr = 0;
        s_registry[i].dma_addr = 0;
        s_registry[i].size = 0;
        s_registry[i].alignment = 0;
        s_registry[i].width = 0;
        s_registry[i].height = 0;
        s_registry[i].pitch = 0;
        s_registry[i].format = 0;
        s_registry[i].state = GPU_RESOURCE_FREE;
        s_registry[i].refcount = 0;
        s_registry[i].owner = GPU_OWNER_NONE;
        s_registry[i].flags = 0;
        s_registry[i].virtio_res_id = 0;
    }
    return 0;
}

u32 gpu_resource_get_generation(void) {
    return s_current_generation;
}

void gpu_resource_advance_generation(void) {
    u32 old_gen = s_current_generation;
    s_current_generation++;

    serial_write("[gpu_resource] generation advanced old=");
    write_dec_res(old_gen);
    serial_write(" new=");
    write_dec_res(s_current_generation);
    serial_write("\n");
    serial_write("[gpu_resource] old mappings detached\n");
}

u32 gpu_resource_count(void) {
    u32 count = 0;
    for (int i = 0; i < GPU_RESOURCE_REGISTRY_CAPACITY; i++) {
        if (s_registry[i].state != GPU_RESOURCE_FREE && s_registry[i].state != GPU_RESOURCE_DEAD) {
            count++;
        }
    }
    return count;
}

gpu_resource_t* gpu_resource_create(u32 width, u32 height, u32 bpp, u64 alignment, u32 owner, u32 flags) {
    u32 pitch = 0;
    u64 size = 0;
    if (gpu_validate_dimensions(width, height, bpp, (u32)alignment, &pitch, &size) != 0) {
        return 0;
    }

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < GPU_RESOURCE_REGISTRY_CAPACITY; i++) {
        if (s_registry[i].state == GPU_RESOURCE_FREE || s_registry[i].state == GPU_RESOURCE_DEAD) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        serial_write("[gpu_resource] registry full\n");
        return 0;
    }

    void *cpu_addr = 0;
    u64 phys_addr = 0;
    u64 dma_addr = 0;
    int rc = gpu_dma_alloc(size, alignment, 0, flags, &cpu_addr, &phys_addr, &dma_addr);
    if (rc != 0) {
        return 0;
    }

    gpu_resource_t *res = &s_registry[slot];
    res->resource_id = ++s_next_resource_id;
    res->generation = s_current_generation;
    res->cpu_addr = cpu_addr;
    res->phys_addr = phys_addr;
    res->dma_addr = dma_addr;
    res->size = size;
    res->alignment = alignment ? alignment : 4096;
    res->width = width;
    res->height = height;
    res->pitch = pitch;
    res->format = bpp;
    res->state = GPU_RESOURCE_ALLOCATED;
    res->refcount = 1;
    res->owner = owner;
    res->flags = flags;
    res->virtio_res_id = 0;

    serial_write("[gpu_resource] alloc id=");
    write_dec_res(res->resource_id);
    serial_write(" size=");
    write_dec_res(res->size);
    serial_write(" align=");
    write_dec_res(res->alignment);
    serial_write("\n");

    /* Map for DMA */
    gpu_dma_map(res);

    serial_write("[gpu_resource] state=MAPPED generation=");
    write_dec_res(res->generation);
    serial_write("\n");

    return res;
}

gpu_resource_t* gpu_resource_lookup(u64 id, u32 generation) {
    active_gpu_device_t *act = gpu_get_active();
    if (id == 0) return 0;

    for (int i = 0; i < GPU_RESOURCE_REGISTRY_CAPACITY; i++) {
        if (s_registry[i].resource_id == id) {
            gpu_resource_t *res = &s_registry[i];

            if (res->state == GPU_RESOURCE_DEAD) {
                serial_write("[gpu_resource] use-after-free rejected\n");
                if (act) act->use_after_free_rejections++;
                return 0;
            }

            if (res->generation != generation || res->generation < s_current_generation) {
                serial_write("[gpu_resource] generation mismatch\n");
                if (act) act->generation_mismatches++;
                return 0;
            }

            return res;
        }
    }

    return 0;
}

int gpu_resource_register(gpu_resource_t *resource) {
    if (!resource) return -1;
    return 0;
}

int gpu_resource_unregister(gpu_resource_t *resource) {
    if (!resource) return -1;
    return gpu_resource_destroy(resource);
}

int gpu_resource_get(gpu_resource_t *resource) {
    if (!resource || resource->state == GPU_RESOURCE_DEAD || resource->state == GPU_RESOURCE_FREE) {
        return -1;
    }
    resource->refcount++;
    return (int)resource->refcount;
}

int gpu_resource_put(gpu_resource_t *resource) {
    if (!resource || resource->refcount == 0) {
        return -1;
    }
    resource->refcount--;
    if (resource->refcount == 0 && resource->state == GPU_RESOURCE_DESTROYING) {
        return gpu_resource_destroy(resource);
    }
    return (int)resource->refcount;
}

int gpu_resource_destroy(gpu_resource_t *resource) {
    active_gpu_device_t *act = gpu_get_active();
    if (!resource) return -1;

    if (resource->state == GPU_RESOURCE_FREE || resource->state == GPU_RESOURCE_DEAD) {
        serial_write("[gpu_resource] double free rejected\n");
        if (act) act->double_free_rejections++;
        return -2;
    }

    if (resource->refcount > 0) {
        resource->state = GPU_RESOURCE_DESTROYING;
        serial_write("[gpu_resource] destroy deferred refcount=");
        write_dec_res(resource->refcount);
        serial_write("\n");
        if (act) act->resource_destroy_deferred++;
        return 1;
    }

    /* refcount == 0: Safe to release DMA and CPU memory */
    gpu_dma_unmap(resource);
    gpu_dma_free(resource->cpu_addr, resource->size, resource->alignment);

    resource->state = GPU_RESOURCE_DEAD;
    resource->cpu_addr = 0;
    resource->phys_addr = 0;
    resource->dma_addr = 0;
    return 0;
}

void gpu_resource_quarantine_all_active(void) {
    u32 count = 0;
    for (int i = 0; i < GPU_RESOURCE_REGISTRY_CAPACITY; i++) {
        gpu_resource_t *res = &s_registry[i];
        if (res->state == GPU_RESOURCE_ALLOCATED ||
            res->state == GPU_RESOURCE_MAPPED ||
            res->state == GPU_RESOURCE_SUBMITTED ||
            res->state == GPU_RESOURCE_IN_USE) {
            res->state = GPU_RESOURCE_QUARANTINED;
            gpu_dma_record_quarantine(res->size);
            count++;
        }
    }
    serial_write("[gpu_resource] quarantining in-flight resources count=");
    write_dec_res(count);
    serial_write("\n");
}

void gpu_resource_recreate_all(u32 width, u32 height, u32 bpp) {
    u32 count = 0;
    for (int i = 0; i < GPU_RESOURCE_REGISTRY_CAPACITY; i++) {
        gpu_resource_t *res = &s_registry[i];
        if (res->state == GPU_RESOURCE_QUARANTINED || res->state == GPU_RESOURCE_DEAD) {
            if (res->cpu_addr) {
                gpu_dma_unmap(res);
                gpu_dma_free(res->cpu_addr, res->size, res->alignment);
                res->cpu_addr = 0;
            }
            res->state = GPU_RESOURCE_FREE;
        }
    }

    /* Allocate fresh resources for double buffering under current generation */
    for (int b = 0; b < 2; b++) {
        gpu_resource_t *nr = gpu_resource_create(width, height, bpp, 4096, GPU_OWNER_BUFFER_MANAGER, 0);
        if (nr) {
            count++;
        }
    }

    serial_write("[gpu_resource] resources recreated count=");
    write_dec_res(count);
    serial_write("\n");
    serial_write("[gpu_resource] registry ready generation=");
    write_dec_res(s_current_generation);
    serial_write("\n");
}

int gpu_resource_bind_virtio_id(gpu_resource_t *res, u32 virtio_res_id) {
    if (!res) return -1;
    res->virtio_res_id = virtio_res_id;
    return 0;
}

gpu_resource_t* gpu_resource_lookup_by_virtio_id(u32 virtio_res_id) {
    if (virtio_res_id == 0) {
        serial_write("[gpu_resource] invalid virtio resource id=0\n");
        return 0;
    }
    for (int i = 0; i < GPU_RESOURCE_REGISTRY_CAPACITY; i++) {
        if (s_registry[i].virtio_res_id == virtio_res_id &&
            s_registry[i].state != GPU_RESOURCE_FREE &&
            s_registry[i].state != GPU_RESOURCE_DEAD) {
            return &s_registry[i];
        }
    }
    serial_write("[gpu_resource] invalid virtio resource id=");
    write_dec_res(virtio_res_id);
    serial_write("\n");
    return 0;
}

int gpu_resource_validate_virtio_id(u32 virtio_res_id) {
    if (virtio_res_id == 0 || virtio_res_id > 1024) {
        serial_write("[gpu_resource] invalid virtio resource id=");
        write_dec_res(virtio_res_id);
        serial_write("\n");
        return -1;
    }
    return 0;
}
