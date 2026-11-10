#ifndef HEAP_H
#define HEAP_H

typedef unsigned int u32;

typedef struct {
    u32 total_bytes;
    u32 used_bytes;
    u32 free_bytes;
    u32 peak_used_bytes;
    u32 alloc_count;
    u32 free_count;
    u32 oom_count;
} heap_stats_t;

void heap_init(void);
void *kmalloc(u32 size);
void kfree(void *ptr);
void *kcalloc(u32 count, u32 size);
void *kmalloc_aligned(unsigned int size, unsigned int alignment);
void kfree_aligned(void *ptr);
void *krealloc(void *ptr, unsigned int new_size);
void *kmalloc_fast(u32 size);
void kfree_fast(void *ptr, u32 size);

void heap_get_stats(heap_stats_t *out_stats);
void heap_dump_diagnostics(void);

#endif
