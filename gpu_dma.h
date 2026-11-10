#ifndef GPU_DMA_H
#define GPU_DMA_H

#include "io.h"

#define GPU_DMA_ALIGNMENT_DEFAULT  4096
#define GPU_DMA_ALIGNMENT_MAX      (2 * 1024 * 1024) /* 2MB */
#define GPU_DMA_PHYS_LIMIT_64BIT   0xFFFFFFFFFFFFFFFFULL
#define GPU_DMA_PHYS_LIMIT_32BIT   0xFFFFFFFFULL
#define GPU_DMA_BUDGET_DEFAULT     (128 * 1024 * 1024ULL) /* 128 MB */

typedef struct {
    u64 limit;
    u64 allocated;
    u64 mapped;
    u64 quarantined;
    u64 peak;
} gpu_memory_budget_t;

struct gpu_resource;

int  gpu_dma_init(u64 budget_limit);
int  gpu_dma_alloc(u64 size,
                   u64 alignment,
                   u64 boundary,
                   u32 flags,
                   void **cpu_addr,
                   u64 *phys_addr,
                   u64 *dma_addr);

void gpu_dma_free(void *cpu_addr,
                  u64 size,
                  u64 alignment);

int  gpu_dma_map(struct gpu_resource *resource);
int  gpu_dma_unmap(struct gpu_resource *resource);

const gpu_memory_budget_t* gpu_dma_get_budget(void);
void gpu_dma_set_budget_limit(u64 limit);
void gpu_dma_record_quarantine(u64 size);
void gpu_dma_release_quarantine(u64 size);

#endif
