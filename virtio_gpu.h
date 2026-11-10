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
typedef struct {
    u32 submit2_count;
    u32 submit3_count;
    u32 submit_batch2_count;
    u32 timeout_count;
    u32 device_fail_count;
    u32 max_spin_iterations;
} virtio_gpu_transport_stats_t;

void virtio_gpu_probe(void);
const virtio_gpu_info_t* virtio_gpu_info(void);
const virtio_gpu_transport_stats_t* virtio_gpu_get_stats(void);
int virtio_gpu_available(void);
int virtio_gpu_init_safe(u32 width, u32 height);
int virtio_gpu_submit2(void *req, u32 req_len, void *resp, u32 resp_len);
int virtio_gpu_submit3(void *a, u32 al, void *b, u32 bl, void *r, u32 rl);
int virtio_gpu_submit_batch2(void *req1, u32 len1, void *resp1, u32 rlen1,
                             void *req2, u32 len2, void *resp2, u32 rlen2);
typedef struct {
    const void *req;
    u32 req_len;
    void *resp;
    u32 resp_len;
} virtio_gpu_cmd_pair_t;
int virtio_gpu_submit_batch_multi(const virtio_gpu_cmd_pair_t *cmds, u32 cmd_count);
u32 virtio_gpu_get_detected_width(void);
u32 virtio_gpu_get_detected_height(void);
int virtio_gpu_cursor_init(void);
int virtio_gpu_cursor_update(u32 x, u32 y);
int virtio_gpu_cursor_move(u32 x, u32 y);
int virtio_gpu_has_hardware_cursor(void);
#endif

