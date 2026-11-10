#ifndef GPU_RESET_H
#define GPU_RESET_H

#include "gpu_driver.h"

int         gpu_reset_primitive(active_gpu_device_t *gpu, gpu_reset_reason_t reason);
int         gpu_reset_transport(active_gpu_device_t *gpu);
int         gpu_reset_recreate_resources(active_gpu_device_t *gpu);
const char* gpu_reset_reason_name(gpu_reset_reason_t reason);

#endif
