#ifndef GPU_REGISTRY_H
#define GPU_REGISTRY_H

#include "gpu_driver.h"

void gpu_registry_init(void);
int gpu_register_driver(gpu_driver_t *drv);
int gpu_unregister_driver(gpu_driver_t *drv);
const gpu_driver_t *gpu_registry_find_best(const pci_device_t *pdev, const gpu_match_entry_t **out_match);
const gpu_driver_t *gpu_registry_get_fallback(void);
void gpu_registry_set_fallback(const gpu_driver_t *drv);
int gpu_registry_count(void);
void gpu_registry_dump(void);

#endif
