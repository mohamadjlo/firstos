#include "memory.h"

u32 virt_to_phys(const void *ptr) {
    return (u32)(unsigned long)ptr;
}

u64 virt_to_phys64(const void *ptr) {
    return (u64)(unsigned long)ptr;
}
