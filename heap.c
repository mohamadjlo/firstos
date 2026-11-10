#include "heap.h"

typedef unsigned char u8;
typedef unsigned int u32;

#define HEAP_SIZE (128u * 1024u * 1024u)
#define HEAP_ALIGN 16u

typedef struct heap_block {
    u32 size;
    u32 used;
    struct heap_block *next_phys;
    struct heap_block *prev_phys;
    struct heap_block *next_free;
    struct heap_block *prev_free;
} heap_block_t;

static u8 heap_area[HEAP_SIZE] __attribute__((aligned(16)));
static heap_block_t *heap_first_phys;
static heap_block_t *heap_free_list;
static int heap_ready;

static u32 align_up(u32 value) {
    return (value + (HEAP_ALIGN - 1u)) & ~(HEAP_ALIGN - 1u);
}

static void heap_zero(void *ptr, u32 size) {
    u8 *p = (u8 *)ptr;
    while (size >= 8) {
        *(unsigned long *)p = 0;
        p += 8;
        size -= 8;
    }
    while (size--) *p++ = 0;
}

#if defined(__x86_64__)
#define HEAP_LOCK() \
    unsigned long _heap_irq_flags; \
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(_heap_irq_flags) :: "memory")
#define HEAP_UNLOCK() \
    __asm__ volatile ("pushq %0; popfq" :: "r"(_heap_irq_flags) : "memory")
#elif defined(__i386__)
#define HEAP_LOCK() \
    unsigned int _heap_irq_flags; \
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(_heap_irq_flags) :: "memory")
#define HEAP_UNLOCK() \
    __asm__ volatile ("pushl %0; popfl" :: "r"(_heap_irq_flags) : "memory")
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

static inline void free_list_remove(heap_block_t *b) {
    if (b->prev_free) {
        b->prev_free->next_free = b->next_free;
    } else if (heap_free_list == b) {
        heap_free_list = b->next_free;
    }
    if (b->next_free) {
        b->next_free->prev_free = b->prev_free;
    }
    b->next_free = (heap_block_t *)0;
    b->prev_free = (heap_block_t *)0;
}

static inline void free_list_insert(heap_block_t *b) {
    b->next_free = heap_free_list;
    b->prev_free = (heap_block_t *)0;
    if (heap_free_list) {
        heap_free_list->prev_free = b;
    }
    heap_free_list = b;
}

void heap_init(void) {
    heap_first_phys = (heap_block_t *)heap_area;
    heap_first_phys->size = HEAP_SIZE - (u32)sizeof(heap_block_t);
    heap_first_phys->used = 0;
    heap_first_phys->next_phys = (heap_block_t *)0;
    heap_first_phys->prev_phys = (heap_block_t *)0;
    heap_first_phys->next_free = (heap_block_t *)0;
    heap_first_phys->prev_free = (heap_block_t *)0;

    heap_free_list = heap_first_phys;
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

    for (b = heap_free_list; b; b = b->next_free) {
        if (b->size < wanted) continue;

        if (b->size >= wanted + (u32)sizeof(heap_block_t) + HEAP_ALIGN) {
            split = (heap_block_t *)((u8 *)(b + 1) + wanted);
            split->size = b->size - wanted - (u32)sizeof(heap_block_t);
            split->used = 0;

            // Update physical adjacency
            split->next_phys = b->next_phys;
            split->prev_phys = b;
            if (b->next_phys) b->next_phys->prev_phys = split;
            b->next_phys = split;

            b->size = wanted;

            // Replace b with split in free list
            split->prev_free = b->prev_free;
            split->next_free = b->next_free;
            if (b->prev_free) b->prev_free->next_free = split;
            else heap_free_list = split;
            if (b->next_free) b->next_free->prev_free = split;

            b->next_free = (heap_block_t *)0;
            b->prev_free = (heap_block_t *)0;
        } else {
            free_list_remove(b);
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
    if ((u8 *)ptr < (u8 *)(heap_area + sizeof(heap_block_t)) ||
        (u8 *)ptr >= (u8 *)(heap_area + HEAP_SIZE)) {
        HEAP_UNLOCK();
        return;
    }
    b = ((heap_block_t *)ptr) - 1;
    if (!b->used) {
        HEAP_UNLOCK();
        return;
    }
    b->used = 0;
    heap_free_count++;
    if (heap_used >= b->size) heap_used -= b->size; else heap_used = 0;

    // O(1) forward coalesce with adjacent physical block
    if (b->next_phys && !b->next_phys->used) {
        heap_block_t *next = b->next_phys;
        free_list_remove(next);
        b->size += (u32)sizeof(heap_block_t) + next->size;
        b->next_phys = next->next_phys;
        if (b->next_phys) b->next_phys->prev_phys = b;
    }

    // O(1) backward coalesce with adjacent physical block
    if (b->prev_phys && !b->prev_phys->used) {
        heap_block_t *prev = b->prev_phys;
        prev->size += (u32)sizeof(heap_block_t) + b->size;
        prev->next_phys = b->next_phys;
        if (b->next_phys) b->next_phys->prev_phys = prev;
    } else {
        free_list_insert(b);
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
    u32 total_size = size + alignment + (u32)sizeof(void*);
    void *raw = kmalloc(total_size);
    if (!raw) return (void *)0;
    
    unsigned long addr = (unsigned long)raw;
    addr += sizeof(void*);
    unsigned long aligned_addr = (addr + alignment - 1) & ~((unsigned long)alignment - 1);
    void **orig_ptr_storage = (void **)(aligned_addr - sizeof(void*));
    *orig_ptr_storage = raw;
    
    return (void *)aligned_addr;
}

void kfree_aligned(void *ptr) {
    if (!ptr) return;
    if ((u8 *)ptr < (u8 *)(heap_area + sizeof(void*) + sizeof(heap_block_t)) ||
        (u8 *)ptr >= (u8 *)(heap_area + HEAP_SIZE)) return;
    void **orig_ptr_storage = (void **)((unsigned long)ptr - sizeof(void*));
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
static slab_cache_t cache_256 = {256, 0};

void* kmalloc_fast(u32 size) {
    slab_cache_t* cache = 0;
    if (size <= 32) cache = &cache_32;
    else if (size <= 64) cache = &cache_64;
    else if (size <= 128) cache = &cache_128;
    else if (size <= 256) cache = &cache_256;
    
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
    else if (size <= 256) cache = &cache_256;
    
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

    HEAP_LOCK();
    if ((u8 *)ptr < (u8 *)(heap_area + sizeof(heap_block_t)) ||
        (u8 *)ptr >= (u8 *)(heap_area + HEAP_SIZE)) {
        HEAP_UNLOCK();
        return (void *)0;
    }
    heap_block_t *b = ((heap_block_t *)ptr) - 1;
    u32 wanted = align_up(new_size);

    // If current block already fits, return it immediately without copying
    if (b->size >= wanted) {
        HEAP_UNLOCK();
        return ptr;
    }

    // In-place expansion: check if next physical block is free and large enough
    if (b->next_phys && !b->next_phys->used) {
        u32 old_size = b->size;
        u32 total_avail = old_size + (u32)sizeof(heap_block_t) + b->next_phys->size;
        if (total_avail >= wanted) {
            heap_block_t *next = b->next_phys;
            heap_block_t *after_next = next->next_phys;
            free_list_remove(next);

            if (total_avail >= wanted + (u32)sizeof(heap_block_t) + HEAP_ALIGN) {
                heap_block_t *split = (heap_block_t *)((u8 *)(b + 1) + wanted);
                split->size = total_avail - wanted - (u32)sizeof(heap_block_t);
                split->used = 0;
                split->next_phys = after_next;
                split->prev_phys = b;
                if (after_next) after_next->prev_phys = split;
                b->next_phys = split;
                b->size = wanted;
                free_list_insert(split);
            } else {
                b->size = total_avail;
                b->next_phys = after_next;
                if (after_next) after_next->prev_phys = b;
            }

            heap_used += (b->size - old_size);
            if (heap_used > heap_peak_used) heap_peak_used = heap_used;
            HEAP_UNLOCK();
            return ptr;
        }
    }
    u32 copy_size = b->size < new_size ? b->size : new_size;
    HEAP_UNLOCK();

    void *new_ptr = kmalloc(new_size);
    if (new_ptr) {
        u8 *dst = (u8*)new_ptr;
        const u8 *src = (const u8*)ptr;
        u32 rem = copy_size;
        while (rem >= 8) {
            *(unsigned long *)dst = *(const unsigned long *)src;
            dst += 8;
            src += 8;
            rem -= 8;
        }
        while (rem--) *dst++ = *src++;
        kfree(ptr);
    }
    return new_ptr;
}
