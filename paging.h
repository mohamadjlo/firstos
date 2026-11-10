#ifndef PAGING_H
#define PAGING_H

#include "io.h"

extern u64 kernel_pml4[512];
extern u64 kernel_pdpt[512];
extern u64 kernel_pd[8][512];

void paging_init(void);
u64* create_page_directory(void);
void switch_page_directory(u64 *dir);

int paging_map_user_range(u64 *pml4, u64 virt_start, u64 phys_start, u64 size, int is_write, int is_user);
int paging_unmap_user_range(u64 *pml4, u64 virt_start, u64 size);
int paging_validate_user_address(u64 virt_addr, u64 len);

#endif
