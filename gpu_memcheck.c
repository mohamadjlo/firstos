#include "gpu_memcheck.h"
#include "gpu_resource.h"
#include "gpu_detect.h"
#include "serial.h"

int gpu_validate_dimensions(u32 width, u32 height, u32 bpp, u32 pitch_alignment, u32 *out_pitch, u64 *out_size) {
    active_gpu_device_t *act = gpu_get_active();
    if (width == 0 || height == 0) return -1;
    if (bpp != 16 && bpp != 24 && bpp != 32) bpp = 32;
    u32 bytes_per_pixel = bpp / 8;

    u64 row_bytes = 0;
    if (mul_overflow_u64((u64)width, (u64)bytes_per_pixel, &row_bytes)) {
        serial_write("[gpu_memcheck] overflow rejected field=pitch\n");
        if (act) act->overflow_rejections++;
        return -2;
    }

    if (pitch_alignment == 0) pitch_alignment = 64;
    if (!is_power_of_two(pitch_alignment)) {
        serial_write("[gpu_dma] allocation rejected reason=alignment\n");
        if (act) act->alignment_failures++;
        return -3;
    }

    u64 pitch = align_up_u64(row_bytes, pitch_alignment);
    if (pitch > 0xFFFFFFFFULL || pitch < row_bytes) {
        serial_write("[gpu_memcheck] overflow rejected field=pitch\n");
        if (act) act->overflow_rejections++;
        return -4;
    }

    u64 total_bytes = 0;
    if (mul_overflow_u64(pitch, (u64)height, &total_bytes) || total_bytes > 0x100000000ULL || total_bytes < pitch) {
        serial_write("[gpu_memcheck] overflow rejected field=size\n");
        if (act) act->overflow_rejections++;
        return -5;
    }

    if (out_pitch) *out_pitch = (u32)pitch;
    if (out_size) *out_size = total_bytes;
    return 0;
}

int gpu_validate_region(const struct gpu_resource *resource, u32 x, u32 y, u32 width, u32 height, u32 pitch) {
    if (!resource) {
        serial_write("[gpu_resource] region rejected reason=null-resource\n");
        return -1;
    }
    if (width == 0 || height == 0) {
        serial_write("[gpu_resource] region rejected reason=zero-dimension\n");
        return -2;
    }

    u64 x_end = (u64)x + (u64)width;
    u64 y_end = (u64)y + (u64)height;
    if (x_end < x || y_end < y) {
        serial_write("[gpu_memcheck] overflow rejected field=region\n");
        return -3;
    }

    if (x_end > resource->width || y_end > resource->height) {
        serial_write("[gpu_resource] region rejected reason=out-of-bounds\n");
        return -4;
    }

    if (pitch > 0 && pitch < (width * 4)) {
        serial_write("[gpu_resource] region rejected reason=invalid-pitch\n");
        return -5;
    }

    u64 last_row_offset = 0;
    u64 effective_pitch = pitch ? pitch : resource->pitch;
    if (mul_overflow_u64((u64)(y_end - 1), effective_pitch, &last_row_offset)) {
        serial_write("[gpu_memcheck] overflow rejected field=pitch\n");
        return -6;
    }

    u64 last_byte_offset = last_row_offset + (u64)x_end * 4;
    if (last_byte_offset > resource->size) {
        serial_write("[gpu_resource] region rejected reason=out-of-bounds\n");
        return -7;
    }

    return 0;
}

int gpu_validate_address(u64 addr, u64 size, u64 limit) {
    active_gpu_device_t *act = gpu_get_active();
    if (!range_valid(addr, size, limit)) {
        serial_write("[gpu_dma] address range rejected\n");
        if (act) act->address_validation_failures++;
        return -1;
    }
    return 0;
}
