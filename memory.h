#ifndef MEMORY_H
#define MEMORY_H

typedef unsigned int u32;
typedef unsigned long long u64;

typedef struct __attribute__((packed)) {
    u32 size;
    u64 base_addr;
    u64 length;
    u32 type;
} multiboot_mmap_entry_t;

extern u64 g_phys_ram_total;
extern u64 g_phys_ram_usable;
extern u64 g_phys_ram_highest_addr;

void memory_init(void *multiboot_info);

u32 virt_to_phys(const void *ptr);
u64 virt_to_phys64(const void *ptr);

#endif
