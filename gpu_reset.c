#include "gpu_reset.h"
#include "gpu_detect.h"
#include "serial.h"

const char* gpu_reset_reason_name(gpu_reset_reason_t reason) {
    switch (reason) {
        case GPU_RESET_TIMEOUT:     return "completion-timeout";
        case GPU_RESET_QUEUE_ERROR: return "queue-error";
        case GPU_RESET_IRQ_FAILURE: return "irq-failure";
        case GPU_RESET_DEVICE_LOSS: return "device-loss";
        case GPU_RESET_MANUAL_TEST: return "manual-test";
        default:                    return "unknown";
    }
}

int gpu_reset_primitive(active_gpu_device_t *gpu, gpu_reset_reason_t reason) {
    if (!gpu) return -1;

    /* 1. Quiesce hardware if driver supports it */
    if (gpu->driver && gpu->driver->ops.quiesce) {
        gpu->driver->ops.quiesce(gpu);
    }

    /* 2. Execute hardware or driver reset */
    if (gpu->driver && gpu->driver->ops.reset_device) {
        return gpu->driver->ops.reset_device(gpu, reason);
    }

    /* Fallback driver or driver without explicit reset */
    return 0;
}

int gpu_reset_transport(active_gpu_device_t *gpu) {
    if (!gpu) return -1;
    if (gpu->driver && gpu->driver->ops.reinit_device) {
        return gpu->driver->ops.reinit_device(gpu);
    }
    return 0;
}

int gpu_reset_recreate_resources(active_gpu_device_t *gpu) {
    if (!gpu) return -1;
    if (gpu->driver && gpu->driver->ops.recreate_resources) {
        return gpu->driver->ops.recreate_resources(gpu);
    }
    return 0;
}
