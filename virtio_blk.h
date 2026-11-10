#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "io.h"

typedef struct {
    u32 type;
    u32 reserved;
    u64 sector;
} __attribute__((packed)) virtio_blk_req_hdr_t;

int virtio_blk_init(void);
int virtio_blk_ready(void);
int virtio_blk_read(u32 sector, void *buffer, u32 sector_count);
int virtio_blk_write(u32 sector, const void *buffer, u32 sector_count);
int virtio_blk_probe_rw(void);

#endif
