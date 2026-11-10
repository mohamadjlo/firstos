#ifndef PAGING_H
#define PAGING_H

#include "io.h"

extern u64 kernel_pml4[512];
extern u64 kernel_pdpt[512];
extern u64 kernel_pd[4][512];

void paging_init(void);
u64* create_page_directory(void);
void switch_page_directory(u64 *dir);

#endif
