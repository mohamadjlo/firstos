#ifndef GPU_VIRTIO_H
#define GPU_VIRTIO_H

#include "gpu_driver.h"

void virtio_gpu_driver_register(void);
const gpu_driver_t *virtio_gpu_driver(void);

#endif
