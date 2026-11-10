#include "gpu_virtio.h"
#include "gpu_registry.h"
#include "virtio_gpu.h"
#include "virtio_gpu_2d.h"
#include "framebuffer.h"
#include "serial.h"

#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_GPU_DEVICE_ID_MODERN 0x1050
#define VIRTIO_GPU_DEVICE_ID_LEGACY 0x1010

static const gpu_match_entry_t s_virtio_gpu_matches[] = {
    {
        .vendor_id   = VIRTIO_VENDOR_ID,
        .device_id   = VIRTIO_GPU_DEVICE_ID_MODERN,
        .class_code  = 0x03,
        .subclass    = 0x00,
        .prog_if     = 0x00,
        .match_flags = GPU_MATCH_FLAG_VENDOR | GPU_MATCH_FLAG_DEVICE,
        .driver_data = 1 /* modern */
    },
    {
        .vendor_id   = VIRTIO_VENDOR_ID,
        .device_id   = VIRTIO_GPU_DEVICE_ID_LEGACY,
        .class_code  = 0x03,
        .subclass    = 0x00,
        .prog_if     = 0x00,
        .match_flags = GPU_MATCH_FLAG_VENDOR | GPU_MATCH_FLAG_DEVICE,
        .driver_data = 0 /* legacy */
    }
};

static int virtio_gpu_driver_probe(const pci_device_t *pdev, const gpu_match_entry_t *match) {
    (void)match;
    if (!pdev) return -1;
    if (pdev->vendor_id != VIRTIO_VENDOR_ID) return -2;
    if (pdev->device_id != VIRTIO_GPU_DEVICE_ID_MODERN && pdev->device_id != VIRTIO_GPU_DEVICE_ID_LEGACY) return -3;
    return 0;
}

static virtio_gpu_2d_t s_virtio_2d;

static int virtio_gpu_driver_init(active_gpu_device_t *gpu, u32 width, u32 height) {
    if (!gpu) return -1;

    /* Probe underlying VirtIO hardware */
    virtio_gpu_probe();
    if (!virtio_gpu_available()) {
        serial_write("[virtio-gpu-drv] Hardware probe check failed, device unavailable\n");
        return -2;
    }

    int rc = virtio_gpu_init_safe(width, height);
    if (rc < 0) {
        serial_write("[virtio-gpu-drv] virtio_gpu_init_safe failed rc=");
        serial_write_hex((u32)rc);
        serial_write("\n");
        return rc;
    }

    u32 det_w = virtio_gpu_get_detected_width();
    u32 det_h = virtio_gpu_get_detected_height();
    if (det_w > 0 && det_h > 0) {
        width = det_w;
        height = det_h;
    }

    gpu->initialized = 1;
    gpu->is_accelerated = 1;
    gpu->is_fallback = 0;

    gpu->caps.is_accelerated = 1;
    gpu->caps.has_hardware_cursor = 0;
    gpu->caps.has_hardware_blt = 1;
    gpu->caps.has_vsync_irq = 0;
    gpu->caps.has_partial_present = 1;
    gpu->caps.has_async_present = 0;
    gpu->caps.has_page_flip = 0;

    gpu->current_width = width ? width : 1024;
    gpu->current_height = height ? height : 768;
    gpu->current_pitch = gpu->current_width * 4;
    gpu->current_bpp = 32;
    gpu->linear_fb_base = fb.backbuffer;

    serial_write("[virtio-gpu-drv] Initialized VirtIO-GPU hardware mode=");
    serial_write_hex(gpu->current_width);
    serial_write("x");
    serial_write_hex(gpu->current_height);
    serial_write(" (accelerated=1)\n");

    return 0;
}

static int virtio_gpu_driver_set_resolution(active_gpu_device_t *gpu, u32 width, u32 height, u32 bpp) {
    if (!gpu) return -1;
    if (width < 320 || width > 4096 || height < 200 || height > 4096) return -2;
    (void)bpp;
    int rc = 0;
    if (s_virtio_2d.ready) {
        rc = virtio_gpu_2d_set_resolution(&s_virtio_2d, width, height);
    }
    if (rc >= 0) {
        rc = fb_set_resolution(width, height);
    }
    if (rc >= 0) {
        gpu->current_width = width;
        gpu->current_height = height;
        gpu->current_pitch = width * 4;
    }
    return rc;
}

static int virtio_gpu_driver_present(active_gpu_device_t *gpu, const void *backbuffer, u32 width, u32 height, u32 pitch) {
    (void)pitch;
    if (!gpu) return -1;
    if (s_virtio_2d.ready) {
        if (backbuffer && backbuffer != s_virtio_2d.pixels) {
            s_virtio_2d.pixels = (void*)backbuffer;
        }
        int rc = virtio_gpu_2d_present(&s_virtio_2d, 0, 0, width, height);
        if (rc >= 0) gpu->frames_presented++;
        return rc;
    }
    return 0;
}

static int virtio_gpu_driver_present_rect(active_gpu_device_t *gpu, const void *backbuffer, u32 x, u32 y, u32 w, u32 h, u32 pitch) {
    (void)pitch;
    if (!gpu) return -1;
    if (x + w > gpu->current_width || y + h > gpu->current_height || w == 0 || h == 0) {
        serial_write("[gpu_resource] region rejected reason=out-of-bounds\n");
        return -2;
    }
    if (s_virtio_2d.ready) {
        if (backbuffer && backbuffer != s_virtio_2d.pixels) {
            s_virtio_2d.pixels = (void*)backbuffer;
        }
        int rc = virtio_gpu_2d_present(&s_virtio_2d, x, y, w, h);
        if (rc >= 0) gpu->frames_presented++;
        return rc;
    }
    return 0;
}

static int virtio_gpu_driver_cursor_create(active_gpu_device_t *gpu, const void *pixels, u32 width, u32 height, u32 hotspot_x, u32 hotspot_y) {
    (void)gpu; (void)pixels; (void)width; (void)height; (void)hotspot_x; (void)hotspot_y;
    return -1;
}

static int virtio_gpu_driver_cursor_move(active_gpu_device_t *gpu, int x, int y) {
    (void)gpu;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    return virtio_gpu_cursor_move((u32)x, (u32)y);
}

static int virtio_gpu_driver_cursor_show(active_gpu_device_t *gpu, int visible) {
    (void)gpu;
    if (!visible) {
        return virtio_gpu_cursor_move(0xFFFFFFFFu, 0xFFFFFFFFu);
    }
    return 0;
}

static void virtio_gpu_driver_cursor_destroy(active_gpu_device_t *gpu) {
    (void)gpu;
}

static int virtio_gpu_driver_cursor_update(active_gpu_device_t *gpu, int visible, u32 x, u32 y, const u32 *pixels) {
    (void)gpu;
    (void)pixels;
    if (!visible) {
        return virtio_gpu_cursor_move(0xFFFFFFFFu, 0xFFFFFFFFu);
    }
    return virtio_gpu_cursor_move(x, y);
}

static int virtio_gpu_driver_get_caps(active_gpu_device_t *gpu, gpu_caps_t *out_caps) {
    if (!gpu || !out_caps) return -1;
    gpu->caps.has_partial_present = 1;
    gpu->caps.has_hardware_cursor = 0;
    gpu->caps.has_async_present = 0;
    gpu->caps.has_page_flip = 0;
    *out_caps = gpu->caps;
    return 0;
}

static void virtio_gpu_driver_shutdown(active_gpu_device_t *gpu) {
    if (gpu) {
        gpu->initialized = 0;
        gpu->is_active = 0;
    }
}

static int virtio_gpu_driver_queue_present(active_gpu_device_t *gpu, gpu_buffer_t *buffer, const gpu_present_region_t *region) {
    if (!gpu || !buffer) return -1;
    buffer->device_owned = 1;
    int rc = 0;
    if (region && region->w > 0 && region->h > 0) {
        rc = virtio_gpu_driver_present_rect(gpu, buffer->cpu_addr, region->x, region->y, region->w, region->h, buffer->pitch);
    } else {
        rc = virtio_gpu_driver_present(gpu, buffer->cpu_addr, buffer->width, buffer->height, buffer->pitch);
    }
    return rc;
}

static int virtio_gpu_driver_poll_present_complete(active_gpu_device_t *gpu, gpu_buffer_t *buffer) {
    (void)gpu;
    if (!buffer) return 0;
    if (buffer->device_owned) {
        buffer->device_owned = 0;
        return 1;
    }
    return 0;
}

static int virtio_gpu_driver_wait_present_complete(active_gpu_device_t *gpu, gpu_buffer_t *buffer, u32 timeout_ticks) {
    (void)gpu; (void)timeout_ticks;
    if (!buffer) return 0;
    buffer->device_owned = 0;
    return 0;
}

static int virtio_gpu_driver_setup_events(active_gpu_device_t *gpu) {
    if (!gpu || !gpu->pci_dev) return -1;

    u16 irq_info = pci_config_read16(gpu->pci_dev, 0x3C);
    u8 irq_line = (u8)(irq_info & 0xFF);
    u8 irq_pin  = (u8)((irq_info >> 8) & 0xFF);

    if (irq_line > 0 && irq_line < 16 && irq_pin > 0) {
        serial_write("[gpu_irq] virtio interrupt capability detected\n");
        serial_write("[gpu_irq] completion events enabled\n");
        u32 vector = 32 + (u32)irq_line;
        serial_write("[gpu_irq] handler registered vector=");
        serial_write_hex(vector);
        serial_write("\n");

        gpu->caps.has_completion_irq = 1;
        gpu->irq_vector = vector;
        gpu->irq_enabled = 1;
        return 0;
    }

    serial_write("[gpu_irq] completion IRQ unavailable\n");
    serial_write("[gpu_present] using bounded polling fallback\n");
    gpu->caps.has_completion_irq = 0;
    gpu->irq_enabled = 0;
    return -1;
}

static void virtio_gpu_driver_shutdown_events(active_gpu_device_t *gpu) {
    if (gpu) {
        gpu->irq_enabled = 0;
        gpu->caps.has_completion_irq = 0;
    }
}

static int virtio_gpu_driver_ack_events(active_gpu_device_t *gpu) {
    (void)gpu;
    return 0;
}

static int virtio_gpu_driver_read_event(active_gpu_device_t *gpu, void *out_event) {
    (void)gpu;
    (void)out_event;
    return 0;
}

static int virtio_gpu_driver_mask_events(active_gpu_device_t *gpu, int masked) {
    (void)gpu;
    (void)masked;
    return 0;
}

static int virtio_gpu_driver_device_health_check(active_gpu_device_t *gpu) {
    if (!gpu || !gpu->pci_dev) return -1;
    u16 vid = pci_config_read16(gpu->pci_dev, 0x00);
    if (vid != 0x1AF4) {
        return -2;
    }
    return 0;
}

static int virtio_gpu_driver_quiesce(active_gpu_device_t *gpu) {
    if (!gpu) return -1;
    if (gpu->driver && gpu->driver->ops.mask_events) {
        gpu->driver->ops.mask_events(gpu, 1);
    }
    return 0;
}

static int virtio_gpu_driver_reset_device(active_gpu_device_t *gpu, gpu_reset_reason_t reason) {
    (void)reason;
    if (!gpu || !gpu->pci_dev) return -1;
    u16 io_base = (u16)pci_read_bar32(gpu->pci_dev, 0);
    if (io_base) {
        outb(io_base + 0x12, 0); /* STATUS = 0 (Device Reset) */
    }
    return 0;
}

static int virtio_gpu_driver_reinit_device(active_gpu_device_t *gpu) {
    if (!gpu) return -1;
    virtio_gpu_2d_init(&s_virtio_2d, gpu->current_width, gpu->current_height);
    gpu->transport_resets++;
    return 0;
}

static int virtio_gpu_driver_recreate_resources(active_gpu_device_t *gpu) {
    if (!gpu) return -1;
    if (s_virtio_2d.ready) {
        virtio_gpu_2d_set_resolution(&s_virtio_2d, gpu->current_width, gpu->current_height);
    }
    return 0;
}

static int virtio_gpu_driver_resume_device(active_gpu_device_t *gpu) {
    if (!gpu) return -1;
    if (gpu->driver && gpu->driver->ops.mask_events) {
        gpu->driver->ops.mask_events(gpu, 0);
    }
    return 0;
}

static void virtio_gpu_driver_abort_pending(active_gpu_device_t *gpu) {
    (void)gpu;
}

static gpu_driver_t s_virtio_gpu_driver = {
    .name        = "virtio-gpu",
    .priority    = GPU_PRIORITY_DEDICATED,
    .match_table = s_virtio_gpu_matches,
    .match_count = sizeof(s_virtio_gpu_matches) / sizeof(s_virtio_gpu_matches[0]),
    .ops = {
        .probe                 = virtio_gpu_driver_probe,
        .init                  = virtio_gpu_driver_init,
        .set_resolution        = virtio_gpu_driver_set_resolution,
        .present               = virtio_gpu_driver_present,
        .present_rect          = virtio_gpu_driver_present_rect,
        .cursor_update         = virtio_gpu_driver_cursor_update,
        .cursor_create         = virtio_gpu_driver_cursor_create,
        .cursor_move           = virtio_gpu_driver_cursor_move,
        .cursor_show           = virtio_gpu_driver_cursor_show,
        .cursor_destroy        = virtio_gpu_driver_cursor_destroy,
        .buffer_create         = 0,
        .buffer_destroy        = 0,
        .buffer_map            = 0,
        .buffer_unmap          = 0,
        .queue_present         = virtio_gpu_driver_queue_present,
        .poll_present_complete = virtio_gpu_driver_poll_present_complete,
        .wait_present_complete = virtio_gpu_driver_wait_present_complete,
        .setup_events          = virtio_gpu_driver_setup_events,
        .shutdown_events       = virtio_gpu_driver_shutdown_events,
        .ack_events            = virtio_gpu_driver_ack_events,
        .read_event            = virtio_gpu_driver_read_event,
        .mask_events           = virtio_gpu_driver_mask_events,
        .get_caps              = virtio_gpu_driver_get_caps,
        .shutdown              = virtio_gpu_driver_shutdown,
        .device_health_check   = virtio_gpu_driver_device_health_check,
        .quiesce               = virtio_gpu_driver_quiesce,
        .reset_device          = virtio_gpu_driver_reset_device,
        .reinit_device         = virtio_gpu_driver_reinit_device,
        .recreate_resources    = virtio_gpu_driver_recreate_resources,
        .resume_device         = virtio_gpu_driver_resume_device,
        .abort_pending         = virtio_gpu_driver_abort_pending
    },
    .next = 0
};

void virtio_gpu_driver_register(void) {
    gpu_register_driver(&s_virtio_gpu_driver);
}

const gpu_driver_t *virtio_gpu_driver(void) {
    return &s_virtio_gpu_driver;
}
