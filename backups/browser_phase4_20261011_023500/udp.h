#ifndef UDP_H
#define UDP_H

#include "io.h"

#define MAX_UDP_BINDINGS 16
#define UDP_RX_DATA      1024

typedef void (*udp_rx_fn)(u32 src_ip, u16 src_port, const void *data, u16 len);

typedef struct {
    int       used;
    u16       local_port;
    udp_rx_fn callback;
    u8        rx_buf[UDP_RX_DATA];
    u16       rx_len;
    u32       rx_src_ip;
    u16       rx_src_port;
    int       has_data;
    u32       rx_packets;
    u32       tx_packets;
} udp_binding_t;

void           udp_init(void);
int            udp_send(u32 dst_ip, u16 dst_port, u16 src_port, const void *data, u16 len);
int            udp_recv(u16 local_port, u32 *out_src_ip, u16 *out_src_port,
                        void *buf, u16 max_len, u32 max_polls);
void           udp_handle(u32 src_ip, u16 src_port, u16 dst_port, const void *data, u16 len);

udp_binding_t *udp_bind(u16 port, udp_rx_fn cb);
void           udp_unbind(udp_binding_t *b);
int            udp_get_bindings(udp_binding_t *out_list, int max_count);
int            udp_binding_send(udp_binding_t *b, u32 dst_ip, u16 dst_port, const void *data, u16 len);

#endif /* UDP_H */
