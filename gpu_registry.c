#include "gpu_registry.h"
#include "serial.h"

#define MAX_REGISTERED_DRIVERS 16

static gpu_driver_t *s_drivers[MAX_REGISTERED_DRIVERS];
static int s_driver_count = 0;
static const gpu_driver_t *s_fallback_driver = 0;

void gpu_registry_init(void) {
    s_driver_count = 0;
    s_fallback_driver = 0;
    for (int i = 0; i < MAX_REGISTERED_DRIVERS; i++) {
        s_drivers[i] = 0;
    }
    serial_write("[gpu_registry] Initialized GPU driver registry\n");
}

int gpu_register_driver(gpu_driver_t *drv) {
    if (!drv || !drv->name) return -1;
    if (s_driver_count >= MAX_REGISTERED_DRIVERS) {
        serial_write("[gpu_registry] Error: Registry full, cannot register ");
        serial_write(drv->name);
        serial_write("\n");
        return -2;
    }

    /* Check for duplicate registration */
    for (int i = 0; i < s_driver_count; i++) {
        if (s_drivers[i] == drv) return 0;
    }

    s_drivers[s_driver_count++] = drv;

    /* If driver priority is fallback or fallback not yet set, update fallback */
    if (drv->priority == GPU_PRIORITY_FALLBACK || !s_fallback_driver) {
        s_fallback_driver = drv;
    }

    serial_write("[gpu_registry] Registered driver: '");
    serial_write(drv->name);
    serial_write("' (priority=");
    serial_write_hex((u32)drv->priority);
    serial_write(", matches=");
    serial_write_hex(drv->match_count);
    serial_write(")\n");

    return 0;
}

int gpu_unregister_driver(gpu_driver_t *drv) {
    if (!drv) return -1;
    for (int i = 0; i < s_driver_count; i++) {
        if (s_drivers[i] == drv) {
            for (int j = i; j < s_driver_count - 1; j++) {
                s_drivers[j] = s_drivers[j + 1];
            }
            s_drivers[--s_driver_count] = 0;
            if (s_fallback_driver == drv) s_fallback_driver = 0;
            return 0;
        }
    }
    return -2;
}

static int pci_matches_entry(const pci_device_t *pdev, const gpu_match_entry_t *m) {
    if (!pdev || !m) return 0;

    if (m->match_flags & GPU_MATCH_FLAG_VENDOR) {
        if (m->vendor_id != GPU_MATCH_ANY && pdev->vendor_id != m->vendor_id) return 0;
    }
    if (m->match_flags & GPU_MATCH_FLAG_DEVICE) {
        if (m->device_id != GPU_MATCH_ANY && pdev->device_id != m->device_id) return 0;
    }
    if (m->match_flags & GPU_MATCH_FLAG_CLASS) {
        if (pdev->class_code != m->class_code) return 0;
    }
    if (m->match_flags & GPU_MATCH_FLAG_SUBCLASS) {
        if (pdev->subclass != m->subclass) return 0;
    }

    return 1;
}

const gpu_driver_t *gpu_registry_find_best(const pci_device_t *pdev, const gpu_match_entry_t **out_match) {
    if (!pdev) return 0;
    if (out_match) *out_match = 0;

    const gpu_driver_t *best_drv = 0;
    const gpu_match_entry_t *best_m = 0;
    int best_priority = -1;

    for (int i = 0; i < s_driver_count; i++) {
        gpu_driver_t *drv = s_drivers[i];
        if (!drv) continue;

        for (u32 m = 0; m < drv->match_count; m++) {
            const gpu_match_entry_t *entry = &drv->match_table[m];
            if (pci_matches_entry(pdev, entry)) {
                if (drv->priority > best_priority) {
                    best_drv = drv;
                    best_m = entry;
                    best_priority = drv->priority;
                }
                break;
            }
        }
    }

    if (out_match) *out_match = best_m;
    return best_drv;
}

const gpu_driver_t *gpu_registry_get_fallback(void) {
    return s_fallback_driver;
}

void gpu_registry_set_fallback(const gpu_driver_t *drv) {
    s_fallback_driver = drv;
}

int gpu_registry_count(void) {
    return s_driver_count;
}

void gpu_registry_dump(void) {
    serial_write("[gpu_registry] Registered drivers count: ");
    serial_write_hex((u32)s_driver_count);
    serial_write("\n");
    for (int i = 0; i < s_driver_count; i++) {
        serial_write("  [");
        serial_write_hex((u32)i);
        serial_write("] '");
        serial_write(s_drivers[i]->name);
        serial_write("' priority=");
        serial_write_hex((u32)s_drivers[i]->priority);
        serial_write("\n");
    }
}
