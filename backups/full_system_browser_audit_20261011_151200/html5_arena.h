#ifndef HTML5_ARENA_H
#define HTML5_ARENA_H

#include <stddef.h>
#include <stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* 1. Explicit Allocator Interface                                           */
/* ========================================================================= */

typedef struct html5_allocator_s {
    void *(*alloc)(size_t size, void *user_data);
    void  (*free)(void *ptr, void *user_data);
    void *user_data;
} html5_allocator_t;

/* Global default allocator fallback */
void html5_allocator_set_default(const html5_allocator_t *allocator);
const html5_allocator_t *html5_allocator_get_default(void);

/* ========================================================================= */
/* 2. Chunked Bump-Pointer Arena                                             */
/* ========================================================================= */

typedef struct html5_arena_chunk_s {
    struct html5_arena_chunk_s *next;
    size_t capacity;
    size_t used;
} html5_arena_chunk_t;

typedef struct html5_arena_s {
    html5_allocator_t allocator;
    html5_arena_chunk_t *head;
    size_t default_chunk_size;
    size_t total_allocated;
    size_t peak_allocated;
} html5_arena_t;

void  html5_arena_init(html5_arena_t *arena, const html5_allocator_t *allocator, size_t chunk_size);
void *html5_arena_alloc(html5_arena_t *arena, size_t size, size_t alignment);
char *html5_arena_strdup(html5_arena_t *arena, const char *str);
char *html5_arena_strndup(html5_arena_t *arena, const char *str, size_t len);
void  html5_arena_reset(html5_arena_t *arena);
void  html5_arena_destroy(html5_arena_t *arena);

/* Returns a canonical empty string constant */
const char *html5_empty_string(void);

/* ========================================================================= */
/* 3. Non-Relocating Stable Node Slab Allocator                              */
/* ========================================================================= */

#define HTML5_NODES_PER_SLAB_CHUNK 128

struct html_node;

typedef struct html_node_chunk_s {
    struct html_node_chunk_s *next;
    int count;
    void *nodes; /* Array of HTML5_NODES_PER_SLAB_CHUNK nodes */
} html_node_chunk_t;

typedef struct html5_node_slab_s {
    html5_allocator_t allocator;
    html_node_chunk_t *head;
    html_node_chunk_t *tail;
    size_t node_size;
    int total_nodes;
} html5_node_slab_t;

void  html5_node_slab_init(html5_node_slab_t *slab, const html5_allocator_t *allocator, size_t node_size);
struct html_node *html5_node_slab_alloc(html5_node_slab_t *slab);
struct html_node *html5_node_slab_get(html5_node_slab_t *slab, int index);
void  html5_node_slab_reset(html5_node_slab_t *slab);
void  html5_node_slab_destroy(html5_node_slab_t *slab);

#ifdef __cplusplus
}
#endif

#endif /* HTML5_ARENA_H */
