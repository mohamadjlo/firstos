#ifndef MEMORY_H
#define MEMORY_H

typedef unsigned int u32;
typedef unsigned long long u64;

u32 virt_to_phys(const void *ptr);
u64 virt_to_phys64(const void *ptr);

#endif
