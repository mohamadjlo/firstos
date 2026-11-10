#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include "io.h"

int  virtio_net_init(void);
int  virtio_net_ready(void);
int  virtio_net_tx(const void *data, u16 len);
int  virtio_net_rx_poll(void *buf, u16 *out_len);
void virtio_net_get_mac(u8 out_mac[6]);

#endif
