#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include "io.h"

typedef struct {
    u32 rx_packets;
    u32 tx_packets;
    u32 rx_bytes;
    u32 tx_bytes;
    u32 tx_timeouts;
    u32 rx_drops;
} virtio_net_stats_t;

int  virtio_net_init(void);
int  virtio_net_ready(void);
int  virtio_net_tx(const void *data, u16 len);
int  virtio_net_rx_poll(void *buf, u16 *out_len);
void virtio_net_get_mac(u8 out_mac[6]);
void virtio_net_get_stats(virtio_net_stats_t *out_stats);

#endif
