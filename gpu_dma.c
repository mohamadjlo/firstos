#include "gpu_dma.h"
#include "gpu_memcheck.h"
#include "gpu_resource.h"
#include "gpu_detect.h"
#include "memory.h"
#include "heap.h"
#include "serial.h"

static gpu_memory_budget_t s_budget;

int gpu_dma_init(u64 budget_limit) {
    s_budget.limit = budget_limit ? budget_limit : GPU_DMA_BUDGET_DEFAULT;
    s_budget.allocated = 0;
    s_budget.mapped = 0;
    s_budget.quarantined = 0;
    s_budget.peak = 0;
    return 0;
}

const gpu_memory_budget_t* gpu_dma_get_budget(void) {
    return &s_budget;
}

void gpu_dma_set_budget_limit(u64 limit) {
    s_budget.limit = limit;
}

void gpu_dma_record_quarantine(u64 size) {
    s_budget.quarantined += size;
}

void gpu_dma_release_quarantine(u64 size) {
    if (s_budget.quarantined >= size) {
        s_budget.quarantined -= size;
    } else {
        s_budget.quarantined = 0;
    }
}

int gpu_dma_alloc(u64 size,
                  u64 alignment,
                  u64 boundary,
                  u32 flags,
                  void **cpu_addr,
                  u64 *phys_addr,
                  u64 *dma_addr) {
    (void)flags;
    active_gpu_device_t *act = gpu_get_active();

    if (!cpu_addr || !phys_addr || !dma_addr) return -1;
    if (size == 0) return -2;

    if (alignment == 0) alignment = GPU_DMA_ALIGNMENT_DEFAULT;
    if (!is_power_of_two(alignment) || alignment > GPU_DMA_ALIGNMENT_MAX) {
        serial_write("[gpu_dma] allocation rejected reason=alignment\n");
        if (act) act->alignment_failures++;
        return -3;
    }

    u64 align_check = 0;
    if (add_overflow_u64(size, alignment - 1, &align_check)) {
        serial_write("[gpu_memcheck] overflow rejected field=size\n");
        if (act) act->overflow_rejections++;
        return -4;
    }

    /* Memory budget check */
    u64 new_alloc = 0;
    if (add_overflow_u64(s_budget.allocated, size, &new_alloc) || new_alloc > s_budget.limit) {
        serial_write("[gpu_dma] allocation rejected reason=budget-exhausted\n");
        return -5;
    }

    void *ptr = kmalloc_aligned((unsigned int)size, (unsigned int)alignment);
    if (!ptr) {
        return -6;
    }

    u64 pa = virt_to_phys64(ptr);
    u64 da = pa; /* Direct identity mapping for DMA */

    /* Physical address range validation */
    if (gpu_validate_address(da, size, GPU_DMA_PHYS_LIMIT_64BIT) != 0) {
        kfree_aligned(ptr);
        return -7;
    }

    /* Check boundary crossing if requested */
    if (boundary > 0) {
        u64 b_start = pa / boundary;
        u64 b_end = (pa + size - 1) / boundary;
        if (b_start != b_end) {
            serial_write("[gpu_dma] allocation rejected reason=boundary-crossing\n");
            kfree_aligned(ptr);
            return -8;
        }
    }

    s_budget.allocated += size;
    if (s_budget.allocated > s_budget.peak) {
        s_budget.peak = s_budget.allocated;
    }
    if (act) {
        act->peak_gpu_memory = s_budget.peak;
        act->resource_allocations++;
    }

    *cpu_addr = ptr;
    *phys_addr = pa;
    *dma_addr = da;
    return 0;
}

void gpu_dma_free(void *cpu_addr, u64 size, u64 alignment) {
    (void)alignment;
    active_gpu_device_t *act = gpu_get_active();
    if (!cpu_addr) return;

    kfree_aligned(cpu_addr);

    if (s_budget.allocated >= size) {
        s_budget.allocated -= size;
    } else {
        s_budget.allocated = 0;
    }

    if (act) {
        act->resource_frees++;
    }
}

int gpu_dma_map(struct gpu_resource *resource) {
    active_gpu_device_t *act = gpu_get_active();
    if (!resource) return -1;

    if (resource->state == GPU_RESOURCE_MAPPED || resource->state == GPU_RESOURCE_SUBMITTED || resource->state == GPU_RESOURCE_IN_USE) {
        serial_write("[gpu_dma] double map rejected\n");
        if (act) act->dma_map_failures++;
        return -2;
    }

    resource->state = GPU_RESOURCE_MAPPED;
    s_budget.mapped += resource->size;

    serial_write("[gpu_dma] map success phys=");
    serial_write_hex((u32)resource->phys_addr);
    serial_write(" dma=");
    serial_write_hex((u32)resource->dma_addr);
    serial_write("\n");
    return 0;
}

int gpu_dma_unmap(struct gpu_resource *resource) {
    active_gpu_device_t *act = gpu_get_active();
    if (!resource) return -1;

    if (resource->state != GPU_RESOURCE_MAPPED && resource->state != GPU_RESOURCE_SUBMITTED && resource->state != GPU_RESOURCE_IN_USE) {
        serial_write("[gpu_dma] double unmap rejected\n");
        if (act) act->dma_unmap_failures++;
        return -2;
    }

    if (s_budget.mapped >= resource->size) {
        s_budget.mapped -= resource->size;
    } else {
        s_budget.mapped = 0;
    }

    resource->state = GPU_RESOURCE_ALLOCATED;
    return 0;
}
