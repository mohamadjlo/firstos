#include "paging.h"
#include "serial.h"
#include "heap.h"
#include "include/firstos/uapi/memory.h"

extern void load_page_directory(u64*);
extern void enable_paging(void);
extern u64 early_pml4[512];

/*
 * 64-bit Kernel Page Tables:
 * kernel_pml4: Level 4 PML4 table (512 entries of 64-bit pointers)
 * kernel_pdpt: Level 3 PDPT table (entry 0 maps first 512GB)
 * kernel_pd:   Level 2 Page Directories (4 tables mapping 4GB with 2MB huge pages)
 */
u64 kernel_pml4[512] __attribute__((aligned(4096)));
u64 kernel_pdpt[512] __attribute__((aligned(4096)));
u64 kernel_pd[4][512] __attribute__((aligned(4096)));

void paging_init(void) {
    int i, j;
    for (i = 0; i < 512; i++) {
        kernel_pml4[i] = 0;
        kernel_pdpt[i] = 0;
    }

    /* PML4[0] points to kernel_pdpt (Present + R/W + User) */
    kernel_pml4[0] = ((u64)(uintptr_t)kernel_pdpt) | 0x07;

    /* PDPT[0..3] point to kernel_pd[0..3] (Present + R/W + User) */
    for (i = 0; i < 4; i++) {
        kernel_pdpt[i] = ((u64)(uintptr_t)&kernel_pd[i][0]) | 0x07;
        for (j = 0; j < 512; j++) {
            /* 2MB huge pages: 0x87 = Present(1) | R/W(2) | User(4) | PageSize-2MB(0x80) */
            u64 phys_addr = ((u64)(i * 512 + j)) * 0x200000ULL;
            kernel_pd[i][j] = phys_addr | 0x87;
        }
    }

    serial_write("[paging] 64-bit 4-level PML4 page tables built (0..4GB identity mapped)\n");
    load_page_directory(kernel_pml4);
    serial_write("[paging] cr3 loaded with kernel_pml4\n");
}

u64* create_page_directory(void) {
    /* Allocate 4KB-aligned PML4 table for new address space */
    u64 *new_pml4 = (u64*)kmalloc_aligned(4096, 4096);
    if (!new_pml4) return (void*)0;

    /* Copy kernel PML4 mappings so the kernel address space is identical */
    for (int i = 0; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }

    return new_pml4;
}

void switch_page_directory(u64 *dir) {
    load_page_directory(dir);
}

int paging_validate_user_address(u64 virt_addr, u64 len) {
    if (len == 0) return 0;
    u64 end = virt_addr + len;
    if (end < virt_addr) return 0; /* overflow */
    if (virt_addr < FIRSTOS_USER_BASE || end > FIRSTOS_USER_TOTAL_END) {
        return 0;
    }
    return 1;
}

int paging_map_user_range(u64 *pml4, u64 virt_start, u64 phys_start, u64 size, int is_write, int is_user) {
    if (!pml4 || size == 0) return -1;
    (void)phys_start;
    (void)is_write;
    (void)is_user;

    serial_write("[paging] map user range: virt=");
    serial_write_hex((u32)virt_start);
    serial_write(" phys=");
    serial_write_hex((u32)phys_start);
    serial_write(" size=");
    serial_write_hex((u32)size);
    serial_write("\n");

    return 0;
}

int paging_unmap_user_range(u64 *pml4, u64 virt_start, u64 size) {
    (void)pml4;
    serial_write("[paging] unmap user range: virt=");
    serial_write_hex((u32)virt_start);
    serial_write(" size=");
    serial_write_hex((u32)size);
    serial_write("\n");
    return 0;
}
