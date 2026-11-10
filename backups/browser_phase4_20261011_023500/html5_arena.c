#include "html5_arena.h"

/* Canonical empty string */
static const char g_canonical_empty_str[1] = "";

const char *html5_empty_string(void) {
    return g_canonical_empty_str;
}

/* Default allocator fallback */
static html5_allocator_t g_default_allocator = { NULL, NULL, NULL };

void html5_allocator_set_default(const html5_allocator_t *allocator) {
    if (allocator) {
        g_default_allocator = *allocator;
    } else {
        g_default_allocator.alloc = NULL;
        g_default_allocator.free = NULL;
        g_default_allocator.user_data = NULL;
    }
}

const html5_allocator_t *html5_allocator_get_default(void) {
    return &g_default_allocator;
}

/* Freestanding helper routines */
static inline void *arena_memset(void *dst, int c, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
    return dst;
}

static inline void *arena_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static inline size_t arena_strlen(const char *s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/* ========================================================================= */
/* Chunked Bump Arena Implementation                                         */
/* ========================================================================= */

void html5_arena_init(html5_arena_t *arena, const html5_allocator_t *allocator, size_t chunk_size) {
    if (!arena) return;
    if (allocator && allocator->alloc) {
        arena->allocator = *allocator;
    } else {
        arena->allocator = g_default_allocator;
    }
    arena->head = NULL;
    arena->default_chunk_size = (chunk_size >= 1024) ? chunk_size : 65536;
    arena->total_allocated = 0;
    arena->peak_allocated = 0;
}

void *html5_arena_alloc(html5_arena_t *arena, size_t size, size_t alignment) {
    if (!arena || size == 0 || !arena->allocator.alloc) return NULL;

    if (alignment == 0) alignment = 4;
    /* Alignment must be a power of 2 */
    if ((alignment & (alignment - 1)) != 0) alignment = 4;

    html5_arena_chunk_t *chunk = arena->head;
    if (chunk) {
        uint8_t *data_start = (uint8_t *)chunk + sizeof(html5_arena_chunk_t);
        uintptr_t cur_addr = (uintptr_t)(data_start + chunk->used);
        uintptr_t aligned_addr = (cur_addr + alignment - 1) & ~(alignment - 1);
        size_t pad = (size_t)(aligned_addr - cur_addr);

        if (chunk->used + pad + size <= chunk->capacity) {
            chunk->used += pad + size;
            arena->total_allocated += pad + size;
            if (arena->total_allocated > arena->peak_allocated) {
                arena->peak_allocated = arena->total_allocated;
            }
            return (void *)aligned_addr;
        }
    }

    /* Allocate new chunk */
    size_t alloc_cap = arena->default_chunk_size;
    if (size + alignment > alloc_cap) {
        alloc_cap = size + alignment;
    }

    size_t chunk_total = sizeof(html5_arena_chunk_t) + alloc_cap;
    html5_arena_chunk_t *new_chunk = (html5_arena_chunk_t *)arena->allocator.alloc(chunk_total, arena->allocator.user_data);
    if (!new_chunk) return NULL;

    new_chunk->capacity = alloc_cap;
    new_chunk->used = 0;
    new_chunk->next = arena->head;
    arena->head = new_chunk;

    uint8_t *data_start = (uint8_t *)new_chunk + sizeof(html5_arena_chunk_t);
    uintptr_t cur_addr = (uintptr_t)data_start;
    uintptr_t aligned_addr = (cur_addr + alignment - 1) & ~(alignment - 1);
    size_t pad = (size_t)(aligned_addr - cur_addr);

    new_chunk->used = pad + size;
    arena->total_allocated += pad + size;
    if (arena->total_allocated > arena->peak_allocated) {
        arena->peak_allocated = arena->total_allocated;
    }

    return (void *)aligned_addr;
}

char *html5_arena_strndup(html5_arena_t *arena, const char *str, size_t len) {
    if (!str || len == 0) return (char *)html5_empty_string();
    if (!arena) return NULL;

    char *buf = (char *)html5_arena_alloc(arena, len + 1, 1);
    if (!buf) return NULL;

    arena_memcpy(buf, str, len);
    buf[len] = '\0';
    return buf;
}

char *html5_arena_strdup(html5_arena_t *arena, const char *str) {
    if (!str || !str[0]) return (char *)html5_empty_string();
    return html5_arena_strndup(arena, str, arena_strlen(str));
}

void html5_arena_reset(html5_arena_t *arena) {
    if (!arena || !arena->allocator.free) return;
    html5_arena_chunk_t *c = arena->head;
    while (c) {
        html5_arena_chunk_t *next = c->next;
        arena->allocator.free(c, arena->allocator.user_data);
        c = next;
    }
    arena->head = NULL;
    arena->total_allocated = 0;
}

void html5_arena_destroy(html5_arena_t *arena) {
    html5_arena_reset(arena);
}

/* ========================================================================= */
/* Stable Node Slab Implementation                                           */
/* ========================================================================= */

void html5_node_slab_init(html5_node_slab_t *slab, const html5_allocator_t *allocator, size_t node_size) {
    if (!slab) return;
    if (allocator && allocator->alloc) {
        slab->allocator = *allocator;
    } else {
        slab->allocator = g_default_allocator;
    }
    slab->head = NULL;
    slab->tail = NULL;
    slab->node_size = (node_size > 0) ? node_size : 256;
    slab->total_nodes = 0;
}

struct html_node *html5_node_slab_alloc(html5_node_slab_t *slab) {
    if (!slab || !slab->allocator.alloc) return NULL;

    if (!slab->tail || slab->tail->count >= HTML5_NODES_PER_SLAB_CHUNK) {
        size_t total_size = sizeof(html_node_chunk_t) + (slab->node_size * HTML5_NODES_PER_SLAB_CHUNK);
        html_node_chunk_t *new_chunk = (html_node_chunk_t *)slab->allocator.alloc(total_size, slab->allocator.user_data);
        if (!new_chunk) return NULL;

        new_chunk->next = NULL;
        new_chunk->count = 0;
        new_chunk->nodes = (void *)((uint8_t *)new_chunk + sizeof(html_node_chunk_t));

        if (!slab->head) {
            slab->head = new_chunk;
        } else {
            slab->tail->next = new_chunk;
        }
        slab->tail = new_chunk;
    }

    html_node_chunk_t *chunk = slab->tail;
    void *node_ptr = (void *)((uint8_t *)chunk->nodes + (chunk->count * slab->node_size));
    arena_memset(node_ptr, 0, slab->node_size);

    chunk->count++;
    slab->total_nodes++;
    return (struct html_node *)node_ptr;
}

struct html_node *html5_node_slab_get(html5_node_slab_t *slab, int index) {
    if (!slab || index < 0 || index >= slab->total_nodes) return NULL;

    int chunk_idx = index / HTML5_NODES_PER_SLAB_CHUNK;
    int offset = index % HTML5_NODES_PER_SLAB_CHUNK;

    html_node_chunk_t *c = slab->head;
    while (c && chunk_idx > 0) {
        c = c->next;
        chunk_idx--;
    }

    if (!c || offset >= c->count) return NULL;
    return (struct html_node *)((uint8_t *)c->nodes + (offset * slab->node_size));
}

void html5_node_slab_reset(html5_node_slab_t *slab) {
    if (!slab || !slab->allocator.free) return;
    html_node_chunk_t *c = slab->head;
    while (c) {
        html_node_chunk_t *next = c->next;
        slab->allocator.free(c, slab->allocator.user_data);
        c = next;
    }
    slab->head = NULL;
    slab->tail = NULL;
    slab->total_nodes = 0;
}

void html5_node_slab_destroy(html5_node_slab_t *slab) {
    html5_node_slab_reset(slab);
}
