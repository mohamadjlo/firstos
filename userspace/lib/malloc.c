#include "../include/firstos/syscall.h"

typedef struct block_header {
    unsigned int size;
    int is_free;
    struct block_header *next;
} block_header_t;

static block_header_t *free_list_head = 0;

void *firstos_malloc(unsigned int size) {
    if (size == 0) return 0;
    /* 16-byte align the payload size for x86_64 SysV ABI */
    unsigned int total_size = (size + 15) & ~15;

    /* First-fit search in free list */
    block_header_t *curr = free_list_head;
    while (curr) {
        if (curr->is_free && curr->size >= total_size) {
            curr->is_free = 0;
            return (void *)(curr + 1);
        }
        curr = curr->next;
    }

    /* Allocate from OS via sbrk */
    unsigned int alloc_size = sizeof(block_header_t) + total_size;
    void *block = firstos_sbrk((int)alloc_size);
    if (block == (void *)-1) return 0;

    block_header_t *header = (block_header_t *)block;
    header->size = total_size;
    header->is_free = 0;
    header->next = free_list_head;
    free_list_head = header;

    return (void *)(header + 1);
}

void firstos_free(void *ptr) {
    if (!ptr) return;
    block_header_t *header = ((block_header_t *)ptr) - 1;
    header->is_free = 1;
}
