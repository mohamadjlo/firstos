#ifndef VIRTIO_GPU_2D_H
#define VIRTIO_GPU_2D_H
#include "virtio_gpu_protocol.h"
typedef struct { u32 width,height,stride,resource_id,format,bytes; u8 *pixels; int ready; } virtio_gpu_2d_t;
int virtio_gpu_2d_init(virtio_gpu_2d_t *g, u32 width, u32 height);
int virtio_gpu_2d_present(virtio_gpu_2d_t *g, u32 x, u32 y, u32 width, u32 height);
int virtio_gpu_2d_set_resolution(virtio_gpu_2d_t *g, u32 width, u32 height);
void virtio_gpu_2d_shutdown(virtio_gpu_2d_t *g);
#endif
