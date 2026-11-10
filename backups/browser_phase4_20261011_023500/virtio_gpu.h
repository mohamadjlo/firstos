#ifndef VIRTIO_GPU_H
#define VIRTIO_GPU_H
#include "pci.h"
#include "virtio_gpu_protocol.h"
typedef unsigned int u32;
typedef struct {
    int present;
    int initialized;
    u8 bus;
    u8 slot;
    u8 function;
    u16 vendor_id;
    u16 device_id;
    u32 bar0;
    u32 bar1;
    u32 resource_width;
    u32 resource_height;
    u32 scanout_id;
} virtio_gpu_info_t;
void virtio_gpu_probe(void);
const virtio_gpu_info_t* virtio_gpu_info(void);
int virtio_gpu_available(void);
int virtio_gpu_init_safe(u32 width, u32 height);
int virtio_gpu_submit2(void *req, u32 req_len, void *resp, u32 resp_len);
int virtio_gpu_submit3(void *a, u32 al, void *b, u32 bl, void *r, u32 rl);
u32 virtio_gpu_get_detected_width(void);
u32 virtio_gpu_get_detected_height(void);
#endif
