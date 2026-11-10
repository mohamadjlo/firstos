#include "gpu_detect.h"
#include "gpu_registry.h"
#include "gpu_generic_fb.h"
#include "gpu_buffer.h"
#include "gpu_event.h"
#include "gpu_irq.h"
#include "pci.h"
#include "serial.h"

static active_gpu_device_t s_active_gpu;
static int s_first_present_logged = 0;

void gpu_runtime_init(void) {
    s_active_gpu.is_active = 0;
    s_active_gpu.initialized = 0;
    s_active_gpu.is_accelerated = 0;
    s_active_gpu.is_fallback = 0;
    s_active_gpu.pci_dev = 0;
    s_active_gpu.driver = 0;
    s_active_gpu.current_width = 0;
    s_active_gpu.current_height = 0;
    s_active_gpu.current_pitch = 0;
    s_active_gpu.current_bpp = 32;
    s_active_gpu.linear_fb_base = 0;
    s_active_gpu.priv_data = 0;
    s_active_gpu.caps.is_accelerated = 0;
    s_active_gpu.caps.has_hardware_cursor = 0;
    s_active_gpu.caps.has_hardware_blt = 0;
    s_active_gpu.caps.has_vsync_irq = 0;
    s_active_gpu.caps.has_partial_present = 0;
    s_active_gpu.caps.has_async_present = 0;
    s_active_gpu.caps.has_page_flip = 0;
    s_active_gpu.cursor_state.enabled = 0;
    s_active_gpu.cursor_state.visible = 0;
    s_active_gpu.cursor_state.hardware = 0;
    s_active_gpu.cursor_state.x = 0;
    s_active_gpu.cursor_state.y = 0;
    s_active_gpu.cursor_state.width = 16;
    s_active_gpu.cursor_state.height = 16;
    s_active_gpu.cursor_state.hotspot_x = 0;
    s_active_gpu.cursor_state.hotspot_y = 0;
    s_active_gpu.frames_presented = 0;
    s_active_gpu.partial_presents = 0;
    s_active_gpu.full_presents = 0;
    s_active_gpu.fallback_events = 0;
    s_active_gpu.last_error_code = 0;
    s_active_gpu.cursor_hw_moves = 0;
    s_active_gpu.cursor_sw_moves = 0;
    s_active_gpu.cursor_fallbacks = 0;
    s_active_gpu.present_requests = 0;
    s_active_gpu.presents_coalesced = 0;
    s_active_gpu.presents_dropped_clean = 0;
    s_active_gpu.present_retries = 0;
    s_active_gpu.vsync_waits = 0;
    s_active_gpu.scheduler_overruns = 0;
    s_first_present_logged = 0;
}

int gpu_detect_scan(void) {
    serial_write("\n[gpu_detect] ========================================\n");
    serial_write("[gpu_detect] Starting FirstOS Auto GPU Detection Scan\n");
    serial_write("[gpu_detect] ========================================\n");

    gpu_runtime_init();

    int gpu_count = pci_display_adapter_count();
    serial_write("[gpu_detect] Discovered PCI display adapters: ");
    serial_write_hex((u32)gpu_count);
    serial_write("\n");

    if (gpu_count == 0) {
        serial_write("[gpu_detect] No PCI display adapters found! Defaulting to Generic FB.\n");
        const gpu_driver_t *fallback = gpu_registry_get_fallback();
        if (fallback) {
            s_active_gpu.driver = fallback;
            s_active_gpu.is_active = 1;
            s_active_gpu.is_fallback = 1;
        }
        serial_write("[gpu_detect] ========================================\n\n");
        return 0;
    }

    const gpu_driver_t *best_driver_overall = 0;
    const pci_device_t *best_device_overall = 0;
    int highest_priority_overall = -1;

    for (int i = 0; i < gpu_count; i++) {
        const pci_device_t *pdev = pci_get_display_adapter(i);
        if (!pdev) continue;

        serial_write("[gpu_detect] Adapter [");
        serial_write_hex((u32)i);
        serial_write("]: VID=");
        serial_write_hex(pdev->vendor_id);
        serial_write(" DID=");
        serial_write_hex(pdev->device_id);
        serial_write(" B:S.F=");
        serial_write_hex(pdev->bus);
        serial_write(":");
        serial_write_hex(pdev->slot);
        serial_write(".");
        serial_write_hex(pdev->function);
        serial_write(" Class=");
        serial_write_hex(pdev->class_code);
        serial_write(" Sub=");
        serial_write_hex(pdev->subclass);
        serial_write("\n");

        const gpu_match_entry_t *match = 0;
        const gpu_driver_t *matched_drv = gpu_registry_find_best(pdev, &match);

        if (matched_drv) {
            serial_write("[gpu_detect]   -> Matched Driver: '");
            serial_write(matched_drv->name);
            serial_write("' (Priority=");
            serial_write_hex((u32)matched_drv->priority);
            serial_write(")\n");

            int probe_res = 0;
            if (matched_drv->ops.probe) {
                probe_res = matched_drv->ops.probe(pdev, match);
            }

            if (probe_res == 0) {
                serial_write("[gpu_detect]   -> Probe status: SUCCESS\n");
                if (matched_drv->priority > highest_priority_overall) {
                    highest_priority_overall = matched_drv->priority;
                    best_driver_overall = matched_drv;
                    best_device_overall = pdev;
                }
            } else {
                serial_write("[gpu_detect]   -> Probe status: FAILED (rc=");
                serial_write_hex((u32)probe_res);
                serial_write(") - Non-fatal fallback ready.\n");
            }
        } else {
            serial_write("[gpu_detect]   -> No specialized driver matched for this device.\n");
        }
    }

    if (!best_driver_overall) {
        serial_write("[gpu_detect] No specialized driver passed probe. Falling back to Generic FB.\n");
        best_driver_overall = gpu_registry_get_fallback();
        best_device_overall = pci_primary_display_adapter();
        s_active_gpu.is_fallback = 1;
    }

    if (best_driver_overall) {
        s_active_gpu.pci_dev = best_device_overall;
        s_active_gpu.driver = best_driver_overall;
        s_active_gpu.is_active = 1;

        serial_write("[gpu_detect] Active Driver Selected: '");
        serial_write(best_driver_overall->name);
        serial_write("'\n");
    }

    serial_write("[gpu_detect] Auto GPU Detection completed cleanly.\n");
    serial_write("[gpu_detect] ========================================\n\n");
    return 0;
}

int gpu_init_active(u32 width, u32 height) {
    if (!s_active_gpu.driver) {
        serial_write("[gpu_runtime] No driver selected! Binding to fallback generic-fb\n");
        s_active_gpu.driver = gpu_registry_get_fallback();
        s_active_gpu.is_fallback = 1;
        s_active_gpu.is_active = 1;
    }

    if (!s_active_gpu.driver) {
        serial_write("[gpu_runtime] Error: No fallback driver registered!\n");
        return -1;
    }

    serial_write("[gpu_runtime] initializing active driver '");
    serial_write(s_active_gpu.driver->name);
    serial_write("'\n");

    int rc = -1;
    if (s_active_gpu.driver->ops.init) {
        rc = s_active_gpu.driver->ops.init(&s_active_gpu, width, height);
    }

    /* Fallback on runtime init failure */
    if (rc != 0 && s_active_gpu.driver != gpu_registry_get_fallback()) {
        serial_write("[gpu_runtime] init failed for '");
        serial_write(s_active_gpu.driver->name);
        serial_write("', falling back to 'generic-fb'\n");

        s_active_gpu.fallback_events++;
        s_active_gpu.driver = gpu_registry_get_fallback();
        s_active_gpu.is_fallback = 1;
        s_active_gpu.is_accelerated = 0;

        if (s_active_gpu.driver && s_active_gpu.driver->ops.init) {
            rc = s_active_gpu.driver->ops.init(&s_active_gpu, width, height);
        }
    }

    if (rc == 0) {
        s_active_gpu.initialized = 1;
        if (s_active_gpu.driver->ops.get_caps) {
            s_active_gpu.driver->ops.get_caps(&s_active_gpu, &s_active_gpu.caps);
        }
        serial_write("[gpu_runtime] init success, mode=");
        serial_write_hex(s_active_gpu.current_width);
        serial_write("x");
        serial_write_hex(s_active_gpu.current_height);
        serial_write(" accelerated=");
        serial_write_hex((u32)s_active_gpu.is_accelerated);
        serial_write("\n");
        serial_write("[gpu_runtime] capabilities: accel=");
        serial_write_hex((u32)s_active_gpu.caps.is_accelerated);
        serial_write(" cursor=");
        serial_write_hex((u32)s_active_gpu.caps.has_hardware_cursor);
        serial_write(" blt=");
        serial_write_hex((u32)s_active_gpu.caps.has_hardware_blt);
        serial_write(" partial=");
        serial_write_hex((u32)s_active_gpu.caps.has_partial_present);
        serial_write("\n");

        /* Phase 5: Initialize Buffer Lifecycle Subsystem */
        gpu_buffers_init(s_active_gpu.current_width, s_active_gpu.current_height, s_active_gpu.current_bpp);

        /* Phase 4: Present Scheduler and Cursor Subsystem Initialization */
        extern void gpu_present_scheduler_init(void);
        gpu_present_scheduler_init();

        /* Phase 6: Initialize Event Queue and Completion Path */
        gpu_irq_init();
        gpu_event_init();
        gpu_setup_completion_path();

        /* Phase 7: Initialize Health Monitor and Fault-Tolerant Recovery */
        extern void gpu_health_init(void);
        extern void gpu_recovery_init(void);
        gpu_health_init();
        gpu_recovery_init();
        s_active_gpu.device_state = GPU_DEVICE_READY;
        s_active_gpu.device_generation = 1;

        return 0;
    }

    serial_write("[gpu_runtime] CRITICAL: Both primary and fallback drivers failed init!\n");
    return -2;
}

int gpu_present_active(const void* backbuffer, u32 width, u32 height, u32 pitch) {
    if (!s_active_gpu.initialized || !s_active_gpu.driver) return -1;

    if (!s_first_present_logged) {
        serial_write("[gpu_runtime] present via '");
        serial_write(s_active_gpu.driver->name);
        serial_write("' completed\n");
        s_first_present_logged = 1;
    }

    s_active_gpu.frames_presented++;
    s_active_gpu.full_presents++;
    if (s_active_gpu.driver->ops.present) {
        return s_active_gpu.driver->ops.present(&s_active_gpu, backbuffer, width, height, pitch);
    }
    return 0;
}

int gpu_present_rect_active(const void* backbuffer, u32 x, u32 y, u32 w, u32 h, u32 pitch) {
    if (!s_active_gpu.initialized || !s_active_gpu.driver) return -1;

    if (!s_active_gpu.caps.has_partial_present || !s_active_gpu.driver->ops.present_rect) {
        serial_write("[gpu_runtime] partial present unsupported, falling back to full present\n");
        s_active_gpu.fallback_events++;
        return gpu_present_active(backbuffer, s_active_gpu.current_width, s_active_gpu.current_height, pitch);
    }

    if (!s_first_present_logged) {
        serial_write("[gpu_runtime] present via '");
        serial_write(s_active_gpu.driver->name);
        serial_write("' completed\n");
        s_first_present_logged = 1;
    }

    s_active_gpu.frames_presented++;
    s_active_gpu.partial_presents++;
    return s_active_gpu.driver->ops.present_rect(&s_active_gpu, backbuffer, x, y, w, h, pitch);
}

void gpu_notify_present(void) {
    if (!s_first_present_logged) {
        serial_write("[gpu_runtime] present via '");
        serial_write(s_active_gpu.driver ? s_active_gpu.driver->name : "generic-fb");
        serial_write("' completed\n");
        s_first_present_logged = 1;
    }
    gpu_notify_frame_presented();
}

active_gpu_device_t* gpu_get_active(void) {
    return &s_active_gpu;
}

const gpu_driver_t* gpu_get_active_driver(void) {
    return s_active_gpu.driver;
}

int gpu_is_accelerated(void) {
    return s_active_gpu.is_accelerated;
}

const char* gpu_active_name(void) {
    if (s_active_gpu.driver && s_active_gpu.driver->name) {
        return s_active_gpu.driver->name;
    }
    return "unknown";
}

u32 gpu_get_active_width(void) {
    return s_active_gpu.current_width;
}

u32 gpu_get_active_height(void) {
    return s_active_gpu.current_height;
}

const gpu_caps_t* gpu_get_caps(void) {
    return &s_active_gpu.caps;
}

int gpu_supports_partial_present(void) {
    return s_active_gpu.initialized ? s_active_gpu.caps.has_partial_present : 0;
}

int gpu_supports_hw_cursor(void) {
    return s_active_gpu.initialized ? s_active_gpu.caps.has_hardware_cursor : 0;
}

int gpu_supports_hw_blt(void) {
    return s_active_gpu.initialized ? s_active_gpu.caps.has_hardware_blt : 0;
}

int gpu_set_active_resolution(u32 width, u32 height, u32 bpp) {
    extern void compositor_init(int screen_w, int screen_h);
    extern int fb_set_resolution(u32 width, u32 height);

    serial_write("[gpu_runtime] resolution change requested ");
    serial_write_hex(width);
    serial_write("x");
    serial_write_hex(height);
    serial_write("x");
    serial_write_hex(bpp ? bpp : 32);
    serial_write("\n");

    if (!s_active_gpu.initialized || !s_active_gpu.driver) {
        serial_write("[gpu_runtime] resolution change failed: active GPU not initialized\n");
        return -1;
    }

    /* Phase 7: Disallow mode change while recovery/reset is in progress */
    if (s_active_gpu.device_state == GPU_DEVICE_LOST ||
        s_active_gpu.device_state == GPU_DEVICE_RESETTING ||
        s_active_gpu.device_state == GPU_DEVICE_REINITIALIZING) {
        serial_write("[gpu_runtime] resolution change rejected: device in recovery\n");
        return -3;
    }

    /* Phase 6: Mask IRQs during mode switch */
    gpu_irq_mask(1);

    /* Phase 5: Drain in-flight frame queue and destroy old buffers */
    gpu_buffers_drain(100);
    gpu_buffers_shutdown();

    int rc = -1;
    if (s_active_gpu.driver->ops.set_resolution) {
        rc = s_active_gpu.driver->ops.set_resolution(&s_active_gpu, width, height, bpp);
    } else {
        rc = fb_set_resolution(width, height);
    }

    if (rc >= 0) {
        s_active_gpu.current_width = width;
        s_active_gpu.current_height = height;
        s_active_gpu.current_bpp = bpp ? bpp : 32;
        s_active_gpu.current_pitch = width * (s_active_gpu.current_bpp / 8);
        compositor_init((int)width, (int)height);
        fb_set_resolution(width, height);

        /* Phase 5: Reallocate buffers matching new resolution */
        gpu_buffers_reallocate(width, height, s_active_gpu.current_bpp);

        /* Phase 6: Re-setup completion path and unmask IRQs */
        gpu_setup_completion_path();
        gpu_irq_mask(0);

        serial_write("[gpu_runtime] resolution change success active=");
        serial_write_hex(width);
        serial_write("x");
        serial_write_hex(height);
        serial_write("\n");
        return 0;
    }

    gpu_irq_mask(0);

    serial_write("[gpu_runtime] resolution change failed rc=");
    serial_write_hex((u32)rc);
    serial_write("\n");
    return rc;
}

u32 gpu_get_frames_presented(void) {
    return s_active_gpu.frames_presented;
}

u32 gpu_get_partial_presents(void) {
    return s_active_gpu.partial_presents;
}

u32 gpu_get_full_presents(void) {
    return s_active_gpu.full_presents;
}

u32 gpu_get_fallback_events(void) {
    return s_active_gpu.fallback_events;
}

int gpu_switch_to_fallback_driver(void) {
    const gpu_driver_t *fb_drv = gpu_registry_get_fallback();
    if (!fb_drv) return -1;

    s_active_gpu.driver = fb_drv;
    s_active_gpu.is_fallback = 1;
    s_active_gpu.is_accelerated = 0;
    s_active_gpu.caps.has_completion_irq = 0;
    s_active_gpu.caps.has_hardware_cursor = 0;
    s_active_gpu.caps.has_device_reset = 0;
    s_active_gpu.irq_enabled = 0;

    int rc = 0;
    if (fb_drv->ops.init) {
        rc = fb_drv->ops.init(&s_active_gpu, s_active_gpu.current_width, s_active_gpu.current_height);
    }
    if (rc == 0) {
        gpu_buffers_reallocate(s_active_gpu.current_width, s_active_gpu.current_height, s_active_gpu.current_bpp);
        gpu_cursor_init();
        gpu_setup_completion_path();
        s_active_gpu.device_state = GPU_DEVICE_FALLBACK;
        s_active_gpu.scheduler_mode = GPU_SCHED_POLLING;
        return 0;
    }
    return rc;
}

gpu_device_state_t gpu_get_device_state(void) {
    return s_active_gpu.device_state;
}

const char* gpu_device_state_name(gpu_device_state_t state) {
    switch (state) {
        case GPU_DEVICE_READY:          return "READY";
        case GPU_DEVICE_SUBMITTING:     return "SUBMITTING";
        case GPU_DEVICE_LOST:           return "LOST";
        case GPU_DEVICE_RESETTING:      return "RESETTING";
        case GPU_DEVICE_REINITIALIZING: return "REINITIALIZING";
        case GPU_DEVICE_DEGRADED:       return "DEGRADED";
        case GPU_DEVICE_FALLBACK:       return "FALLBACK";
        case GPU_DEVICE_FAILED:         return "FAILED";
        default:                        return "UNKNOWN";
    }
}

u32 gpu_get_device_generation(void) {
    return s_active_gpu.device_generation;
}

u32 gpu_get_reset_attempts(void) {
    return s_active_gpu.reset_attempts;
}

u32 gpu_get_reset_successes(void) {
    return s_active_gpu.reset_successes;
}

u32 gpu_get_reset_failures(void) {
    return s_active_gpu.reset_failures;
}

u32 gpu_get_device_loss_events(void) {
    return s_active_gpu.device_loss_events;
}

u32 gpu_get_recovery_fallbacks(void) {
    return s_active_gpu.recovery_fallbacks;
}

u32 gpu_get_frames_quarantined(void) {
    return s_active_gpu.frames_quarantined;
}

u32 gpu_get_resources_recreated(void) {
    return s_active_gpu.resources_recreated;
}

u32 gpu_get_recovery_storm_prevented(void) {
    return s_active_gpu.recovery_storm_prevented;
}

u32 gpu_get_safe_no_present_entries(void) {
    return s_active_gpu.safe_no_present_entries;
}

int gpu_device_can_present(void) {
    if (s_active_gpu.device_state == GPU_DEVICE_LOST ||
        s_active_gpu.device_state == GPU_DEVICE_RESETTING ||
        s_active_gpu.device_state == GPU_DEVICE_REINITIALIZING ||
        s_active_gpu.device_state == GPU_DEVICE_FAILED) {
        return 0;
    }
    return 1;
}

/* Phase 8: Resource Management & Memory Safety Getters */
u32 gpu_get_resource_allocations(void) {
    return s_active_gpu.resource_allocations;
}

u32 gpu_get_resource_frees(void) {
    return s_active_gpu.resource_frees;
}

u32 gpu_get_resource_destroy_deferred(void) {
    return s_active_gpu.resource_destroy_deferred;
}

u32 gpu_get_dma_map_failures(void) {
    return s_active_gpu.dma_map_failures;
}

u32 gpu_get_dma_unmap_failures(void) {
    return s_active_gpu.dma_unmap_failures;
}

u32 gpu_get_address_validation_failures(void) {
    return s_active_gpu.address_validation_failures;
}

u32 gpu_get_alignment_failures(void) {
    return s_active_gpu.alignment_failures;
}

u32 gpu_get_overflow_rejections(void) {
    return s_active_gpu.overflow_rejections;
}

u32 gpu_get_generation_mismatches(void) {
    return s_active_gpu.generation_mismatches;
}

u32 gpu_get_use_after_free_rejections(void) {
    return s_active_gpu.use_after_free_rejections;
}

u32 gpu_get_double_free_rejections(void) {
    return s_active_gpu.double_free_rejections;
}

u32 gpu_get_quarantine_resources(void) {
    return s_active_gpu.quarantine_resources;
}

u64 gpu_get_peak_gpu_memory(void) {
    return s_active_gpu.peak_gpu_memory;
}
