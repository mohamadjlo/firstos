#include "heap.h"

typedef unsigned char u8;
typedef unsigned int u32;

#define HEAP_SIZE (256u * 1024u * 1024u)
#define HEAP_ALIGN 16u

typedef struct heap_block {
    u32 size;
    u32 used;
    struct heap_block *next;
} heap_block_t;

static u8 heap_area[HEAP_SIZE] __attribute__((aligned(16)));
static heap_block_t *heap_first;
static int heap_ready;

static u32 align_up(u32 value) {
    return (value + (HEAP_ALIGN - 1u)) & ~(HEAP_ALIGN - 1u);
}

static void heap_zero(void *ptr, u32 size) {
    u8 *p = (u8 *)ptr;
    while (size--) *p++ = 0;
}

#if defined(__i386__) || defined(__x86_64__)
#define HEAP_LOCK()   __asm__ volatile ("cli")
#define HEAP_UNLOCK() __asm__ volatile ("sti")
#else
#define HEAP_LOCK()   ((void)0)
#define HEAP_UNLOCK() ((void)0)
#endif

u32 heap_total = HEAP_SIZE;
u32 heap_used = 0;
u32 heap_peak_used = 0;
u32 heap_alloc_count = 0;
u32 heap_free_count = 0;
u32 heap_oom_count = 0;

void heap_init(void) {
    heap_first = (heap_block_t *)heap_area;
    heap_first->size = HEAP_SIZE - (u32)sizeof(heap_block_t);
    heap_first->used = 0;
    heap_first->next = (heap_block_t *)0;
    heap_ready = 1;
    heap_total = HEAP_SIZE;
    heap_used = 0;
    heap_peak_used = 0;
    heap_alloc_count = 0;
    heap_free_count = 0;
    heap_oom_count = 0;
}

void *kmalloc(u32 size) {
    HEAP_LOCK();
    heap_block_t *b;
    heap_block_t *split;
    u32 wanted;

    if (!heap_ready || size == 0) {
        if (size > 0) heap_oom_count++;
        HEAP_UNLOCK();
        return (void *)0;
    }
    wanted = align_up(size);

    for (b = heap_first; b; b = b->next) {
        if (b->used || b->size < wanted) continue;
        if (b->size >= wanted + (u32)sizeof(heap_block_t) + HEAP_ALIGN) {
            split = (heap_block_t *)((u8 *)(b + 1) + wanted);
            split->size = b->size - wanted - (u32)sizeof(heap_block_t);
            split->used = 0;
            split->next = b->next;
            b->next = split;
            b->size = wanted;
        }
        b->used = 1;
        heap_used += b->size;
        if (heap_used > heap_peak_used) heap_peak_used = heap_used;
        heap_alloc_count++;
        HEAP_UNLOCK();
        return (void *)(b + 1);
    }
    heap_oom_count++;
    HEAP_UNLOCK();
    return (void *)0;
}

void kfree(void *ptr) {
    HEAP_LOCK();
    heap_block_t *b;

    if (!ptr) {
        HEAP_UNLOCK();
        return;
    }
    b = ((heap_block_t *)ptr) - 1;
    b->used = 0;
    heap_free_count++;
    if (heap_used >= b->size) heap_used -= b->size; else heap_used = 0;

    for (b = heap_first; b; b = b->next) {
        while (!b->used && b->next && !b->next->used) {
            b->size += (u32)sizeof(heap_block_t) + b->next->size;
            b->next = b->next->next;
        }
    }
    HEAP_UNLOCK();
}

void heap_get_stats(heap_stats_t *out_stats) {
    if (!out_stats) return;
    HEAP_LOCK();
    out_stats->total_bytes = heap_total;
    out_stats->used_bytes = heap_used;
    out_stats->free_bytes = (heap_total > heap_used) ? (heap_total - heap_used) : 0;
    out_stats->peak_used_bytes = heap_peak_used;
    out_stats->alloc_count = heap_alloc_count;
    out_stats->free_count = heap_free_count;
    out_stats->oom_count = heap_oom_count;
    HEAP_UNLOCK();
}

void heap_dump_diagnostics(void) {
    heap_stats_t s;
    heap_get_stats(&s);
    extern void serial_write(const char *);
    extern void serial_write_hex(u32);
    serial_write("[heap] telemetry: total="); serial_write_hex(s.total_bytes);
    serial_write(" used="); serial_write_hex(s.used_bytes);
    serial_write(" peak="); serial_write_hex(s.peak_used_bytes);
    serial_write(" allocs="); serial_write_hex(s.alloc_count);
    serial_write(" frees="); serial_write_hex(s.free_count);
    serial_write(" oom="); serial_write_hex(s.oom_count);
    serial_write("\n");
}

void *kcalloc(u32 count, u32 size) {
    u32 total;
    void *ptr;
    if (count == 0 || size == 0 || size > 0xFFFFFFFFu / count) return (void *)0;
    total = count * size;
    ptr = kmalloc(total);
    if (ptr) heap_zero(ptr, total);
    return ptr;
}

void *kmalloc_aligned(u32 size, u32 alignment) {
    if (!heap_ready || size == 0) return (void *)0;
    u32 total_size = size + alignment + sizeof(void*);
    void *raw = kmalloc(total_size);
    if (!raw) return (void *)0;
    
    u32 addr = (u32)(unsigned long)raw;
    addr += sizeof(void*);
    u32 aligned_addr = (addr + alignment - 1) & ~(alignment - 1);
    void **orig_ptr_storage = (void **)(aligned_addr - sizeof(void*));
    *orig_ptr_storage = raw;
    
    return (void *)aligned_addr;
}

void kfree_aligned(void *ptr) {
    if (!ptr) return;
    void **orig_ptr_storage = (void **)((u32)ptr - sizeof(void*));
    kfree(*orig_ptr_storage);
}

// ---------------------------------------------------------
// Slab Cache Subsystem (High-Performance Memory Allocator)
// ---------------------------------------------------------

typedef struct slab_node {
    struct slab_node* next;
} slab_node_t;

typedef struct {
    int block_size;
    slab_node_t* free_list;
} slab_cache_t;

static slab_cache_t cache_32 = {32, 0};
static slab_cache_t cache_64 = {64, 0};
static slab_cache_t cache_128 = {128, 0};

void* kmalloc_fast(u32 size) {
    slab_cache_t* cache = 0;
    if (size <= 32) cache = &cache_32;
    else if (size <= 64) cache = &cache_64;
    else if (size <= 128) cache = &cache_128;
    
    if (cache && cache->free_list) {
        HEAP_LOCK();
        void* ptr = cache->free_list;
        cache->free_list = cache->free_list->next;
        HEAP_UNLOCK();
        return ptr;
    }
    
    // Fallback to traditional heap if fast caches are empty
    return kmalloc(size);
}

void kfree_fast(void* ptr, u32 size) {
    if (!ptr) return;
    slab_cache_t* cache = 0;
    if (size <= 32) cache = &cache_32;
    else if (size <= 64) cache = &cache_64;
    else if (size <= 128) cache = &cache_128;
    
    if (cache) {
        HEAP_LOCK();
        slab_node_t* node = (slab_node_t*)ptr;
        node->next = cache->free_list;
        cache->free_list = node;
        HEAP_UNLOCK();
        return;
    }
    
    kfree(ptr);
}

void *krealloc(void *ptr, unsigned int new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return (void*)0; }
    heap_block_t *b = ((heap_block_t *)ptr) - 1;
    u32 copy_size = b->size < new_size ? b->size : new_size;
    void *new_ptr = kmalloc(new_size);
    if (new_ptr) {
        u8 *dst = (u8*)new_ptr;
        const u8 *src = (const u8*)ptr;
        for (u32 i = 0; i < copy_size; i++) dst[i] = src[i];
        kfree(ptr);
    }
    return new_ptr;
}

