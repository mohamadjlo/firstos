#include "memory.h"
#include "serial.h"

u64 g_phys_ram_total = 0;
u64 g_phys_ram_usable = 0;
u64 g_phys_ram_highest_addr = 0;

typedef struct __attribute__((packed)) {
    u32 flags;
    u32 mem_lower;
    u32 mem_upper;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    u32 syms[4];
    u32 mmap_length;
    u32 mmap_addr;
} mb_header_t;

void memory_init(void *multiboot_info) {
    g_phys_ram_total = 0;
    g_phys_ram_usable = 0;
    g_phys_ram_highest_addr = 0;

    if (!multiboot_info) return;

    mb_header_t *mb = (mb_header_t *)multiboot_info;

    if (mb->flags & (1u << 6)) {
        u32 mmap_addr = mb->mmap_addr;
        u32 mmap_len = mb->mmap_length;
        u32 offset = 0;

        while (offset < mmap_len) {
            multiboot_mmap_entry_t *entry = (multiboot_mmap_entry_t *)(unsigned long)(mmap_addr + offset);
            g_phys_ram_total += entry->length;

            if (entry->type == 1) { // 1 = Usable RAM
                g_phys_ram_usable += entry->length;
                u64 top = entry->base_addr + entry->length;
                if (top > g_phys_ram_highest_addr) {
                    g_phys_ram_highest_addr = top;
                }
            }
            offset += entry->size + sizeof(u32);
        }
    } else if (mb->flags & (1u << 0)) {
        // Fallback to mem_lower and mem_upper (in KB)
        g_phys_ram_usable = (u64)(mb->mem_lower + mb->mem_upper) * 1024ULL;
        g_phys_ram_total = g_phys_ram_usable;
        g_phys_ram_highest_addr = g_phys_ram_usable;
    } else {
        // Fallback default: 2GB
        g_phys_ram_usable = 2048ULL * 1024ULL * 1024ULL;
        g_phys_ram_total = g_phys_ram_usable;
        g_phys_ram_highest_addr = g_phys_ram_usable;
    }

    serial_write("[memory] physical RAM: usable=");
    serial_write_hex((u32)(g_phys_ram_usable / (1024ULL * 1024ULL)));
    serial_write("MB total=");
    serial_write_hex((u32)(g_phys_ram_total / (1024ULL * 1024ULL)));
    serial_write("MB highest=");
    serial_write_hex((u32)(g_phys_ram_highest_addr / (1024ULL * 1024ULL)));
    serial_write("MB\n");
}

u32 virt_to_phys(const void *ptr) {
    return (u32)(unsigned long)ptr;
}

u64 virt_to_phys64(const void *ptr) {
    return (u64)(unsigned long)ptr;
}
