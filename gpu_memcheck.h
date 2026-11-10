#ifndef GPU_MEMCHECK_H
#define GPU_MEMCHECK_H

#include "io.h"

struct gpu_resource;

/* Basic arithmetic and alignment validation helpers */
static inline int is_power_of_two(u64 value) {
    return (value > 0) && ((value & (value - 1)) == 0);
}

static inline int range_valid(u64 base, u64 size, u64 limit) {
    if (size == 0) return 0;
    u64 end = base + size;
    if (end < base) return 0; /* overflow */
    if (limit > 0 && end > limit) return 0;
    return 1;
}

static inline int add_overflow_u64(u64 a, u64 b, u64 *out) {
    u64 res = a + b;
    if (res < a) return 1;
    if (out) *out = res;
    return 0;
}

static inline int mul_overflow_u64(u64 a, u64 b, u64 *out) {
    if (a == 0 || b == 0) {
        if (out) *out = 0;
        return 0;
    }
    u64 res = a * b;
    if (res / a != b) return 1;
    if (out) *out = res;
    return 0;
}

static inline u64 align_up_u64(u64 val, u64 alignment) {
    if (alignment == 0) return val;
    return (val + alignment - 1) & ~(alignment - 1);
}

/* Validation prototypes */
int gpu_validate_dimensions(u32 width, u32 height, u32 bpp, u32 pitch_alignment, u32 *out_pitch, u64 *out_size);
int gpu_validate_region(const struct gpu_resource *resource, u32 x, u32 y, u32 width, u32 height, u32 pitch);
int gpu_validate_address(u64 addr, u64 size, u64 limit);

#endif
