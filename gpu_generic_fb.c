#include "gpu_generic_fb.h"
#include "gpu_registry.h"
#include "framebuffer.h"
#include "serial.h"

static const gpu_match_entry_t s_generic_fb_matches[] = {
    {
        .vendor_id   = GPU_MATCH_ANY,
        .device_id   = GPU_MATCH_ANY,
        .class_code  = 0x03,
        .subclass    = 0x00, /* VGA compatible controller */
        .prog_if     = 0x00,
        .match_flags = GPU_MATCH_FLAG_CLASS | GPU_MATCH_FLAG_SUBCLASS,
        .driver_data = 0
    },
    {
        .vendor_id   = GPU_MATCH_ANY,
        .device_id   = GPU_MATCH_ANY,
        .class_code  = 0x03,
        .subclass    = 0x80, /* Other display controller */
        .prog_if     = 0x00,
        .match_flags = GPU_MATCH_FLAG_CLASS | GPU_MATCH_FLAG_SUBCLASS,
        .driver_data = 0
    },
    {
        .vendor_id   = GPU_MATCH_ANY,
        .device_id   = GPU_MATCH_ANY,
        .class_code  = 0x03,
        .subclass    = 0x00,
        .prog_if     = 0x00,
        .match_flags = GPU_MATCH_FLAG_CLASS, /* Any display class */
        .driver_data = 0
    }
};

static int generic_fb_probe(const pci_device_t *pdev, const gpu_match_entry_t *match) {
    (void)match;
    if (!pdev) return 0; /* Fallback driver accepts even null device (e.g. non-PCI linear framebuffer) */
    if (pdev->class_code == 0x03) return 0;
    return -1;
}

static int generic_fb_init(active_gpu_device_t *gpu, u32 width, u32 height) {
    if (!gpu) return -1;
    
    gpu->initialized = 1;
    gpu->is_accelerated = 0;
    gpu->is_fallback = 1;

    gpu->caps.is_accelerated = 0;
    gpu->caps.has_hardware_cursor = 0;
    gpu->caps.has_hardware_blt = 0;
    gpu->caps.has_vsync_irq = 0;
    gpu->caps.has_partial_present = 1;
    gpu->caps.has_async_present = 0;
    gpu->caps.has_page_flip = 0;

    gpu->current_width = width ? width : (fb.width ? fb.width : 1024);
    gpu->current_height = height ? height : (fb.height ? fb.height : 768);
    gpu->current_pitch = fb.pitch ? fb.pitch : (gpu->current_width * 4);
    gpu->current_bpp = fb.bpp ? fb.bpp : 32;
    gpu->linear_fb_base = fb.address ? fb.address : fb.backbuffer;

    serial_write("[generic-fb] Initialized linear framebuffer mode=");
    serial_write_hex(gpu->current_width);
    serial_write("x");
    serial_write_hex(gpu->current_height);
    serial_write(" (fallback=1, accelerated=0)\n");
    return 0;
}

static int generic_fb_set_resolution(active_gpu_device_t *gpu, u32 width, u32 height, u32 bpp) {
    if (!gpu) return -1;
    if (width < 320 || width > 4096 || height < 200 || height > 4096) return -2;
    (void)bpp;
    int rc = fb_set_resolution(width, height);
    if (rc >= 0) {
        gpu->current_width = width;
        gpu->current_height = height;
        gpu->current_pitch = width * 4;
    }
    return rc;
}

static int generic_fb_present(active_gpu_device_t *gpu, const void *backbuffer, u32 width, u32 height, u32 pitch) {
    if (!gpu || !fb.address || !backbuffer) return -1;
    u32 rows = (height < fb.height) ? height : fb.height;
    u32 span = (width < fb.width) ? width : fb.width;
    u32 src_pitch = pitch ? pitch : (width * 4);
    u32 dst_pitch = fb.pitch;

    for (u32 y = 0; y < rows; y++) {
        const u32 *s = (const u32*)((const u8*)backbuffer + y * src_pitch);
        u32 *d = (u32*)(fb.address + y * dst_pitch);
        fast_memcpy32(d, s, span);
    }
    gpu->frames_presented++;
    return 0;
}

static int generic_fb_present_rect(active_gpu_device_t *gpu, const void *backbuffer, u32 x, u32 y, u32 w, u32 h, u32 pitch) {
    if (!gpu || !fb.address || !backbuffer) return -1;
    u32 x1 = x + w;
    u32 y1 = y + h;
    if (x1 > fb.width) x1 = fb.width;
    if (y1 > fb.height) y1 = fb.height;
    if (x >= x1 || y >= y1) return 0;

    u32 span = x1 - x;
    u32 src_pitch = pitch ? pitch : (fb.width * 4);
    u32 dst_pitch = fb.pitch;

    for (u32 row = y; row < y1; row++) {
        const u32 *s = (const u32*)((const u8*)backbuffer + row * src_pitch) + x;
        u32 *d = (u32*)(fb.address + row * dst_pitch) + x;
        fast_memcpy32(d, s, span);
    }
    gpu->frames_presented++;
    return 0;
}

static int generic_fb_get_caps(active_gpu_device_t *gpu, gpu_caps_t *out_caps) {
    if (!gpu || !out_caps) return -1;
    gpu->caps.has_partial_present = 1;
    gpu->caps.has_hardware_cursor = 0;
    gpu->caps.has_async_present = 0;
    gpu->caps.has_page_flip = 0;
    gpu->caps.has_device_reset = 0;
    *out_caps = gpu->caps;
    return 0;
}

static void generic_fb_shutdown(active_gpu_device_t *gpu) {
    if (gpu) {
        gpu->initialized = 0;
        gpu->is_active = 0;
    }
}

static int generic_fb_queue_present(active_gpu_device_t *gpu, gpu_buffer_t *buffer, const gpu_present_region_t *region) {
    if (!gpu || !buffer) return -1;
    buffer->device_owned = 1;
    int rc = 0;
    if (region && region->w > 0 && region->h > 0) {
        rc = generic_fb_present_rect(gpu, buffer->cpu_addr, region->x, region->y, region->w, region->h, buffer->pitch);
    } else {
        rc = generic_fb_present(gpu, buffer->cpu_addr, buffer->width, buffer->height, buffer->pitch);
    }
    return rc;
}

static int generic_fb_poll_present_complete(active_gpu_device_t *gpu, gpu_buffer_t *buffer) {
    (void)gpu;
    if (!buffer) return 0;
    if (buffer->device_owned) {
        buffer->device_owned = 0;
        return 1;
    }
    return 0;
}

static int generic_fb_wait_present_complete(active_gpu_device_t *gpu, gpu_buffer_t *buffer, u32 timeout_ticks) {
    (void)gpu; (void)timeout_ticks;
    if (!buffer) return 0;
    buffer->device_owned = 0;
    return 0;
}

static int generic_fb_setup_events(active_gpu_device_t *gpu) {
    if (gpu) {
        gpu->caps.has_completion_irq = 0;
        gpu->irq_enabled = 0;
    }
    serial_write("[gpu_irq] no completion IRQ for generic-fb\n");
    serial_write("[gpu_present] synchronous completion path active\n");
    return 0;
}

static int generic_fb_device_health_check(active_gpu_device_t *gpu) {
    if (!gpu || !fb.address) return -1;
    return 0;
}

static int generic_fb_recreate_resources(active_gpu_device_t *gpu) {
    (void)gpu;
    return 0;
}

static gpu_driver_t s_generic_fb_driver = {
    .name        = "generic-fb",
    .priority    = GPU_PRIORITY_FALLBACK,
    .match_table = s_generic_fb_matches,
    .match_count = sizeof(s_generic_fb_matches) / sizeof(s_generic_fb_matches[0]),
    .ops = {
        .probe                 = generic_fb_probe,
        .init                  = generic_fb_init,
        .set_resolution        = generic_fb_set_resolution,
        .present               = generic_fb_present,
        .present_rect          = generic_fb_present_rect,
        .cursor_update         = 0,
        .cursor_create         = 0,
        .cursor_move           = 0,
        .cursor_show           = 0,
        .cursor_destroy        = 0,
        .buffer_create         = 0,
        .buffer_destroy        = 0,
        .buffer_map            = 0,
        .buffer_unmap          = 0,
        .queue_present         = generic_fb_queue_present,
        .poll_present_complete = generic_fb_poll_present_complete,
        .wait_present_complete = generic_fb_wait_present_complete,
        .setup_events          = generic_fb_setup_events,
        .shutdown_events       = 0,
        .ack_events            = 0,
        .read_event            = 0,
        .mask_events           = 0,
        .get_caps              = generic_fb_get_caps,
        .shutdown              = generic_fb_shutdown,
        .device_health_check   = generic_fb_device_health_check,
        .quiesce               = 0,
        .reset_device          = 0,
        .reinit_device         = 0,
        .recreate_resources    = generic_fb_recreate_resources,
        .resume_device         = 0,
        .abort_pending         = 0
    },
    .next = 0
};

void gpu_generic_fb_register(void) {
    gpu_register_driver(&s_generic_fb_driver);
    gpu_registry_set_fallback(&s_generic_fb_driver);
}

const gpu_driver_t *gpu_generic_fb_driver(void) {
    return &s_generic_fb_driver;
}
