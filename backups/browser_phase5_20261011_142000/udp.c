#include "udp.h"
#include "ip.h"
#include "eth.h"
#include "serial.h"
#include "gui.h"
#include <string.h>

#define UDP_RX_SLOTS 8

typedef struct {
    u32 src_ip;
    u16 src_port, dst_port;
    u8  data[UDP_RX_DATA];
    u16 len;
    int used;
} udp_rx_slot_t;

static udp_rx_slot_t rx_ring[UDP_RX_SLOTS];
static int rx_write = 0;
static udp_binding_t g_bindings[MAX_UDP_BINDINGS];

static u8 udp_tx_buf[8 + 1400];

void udp_init(void) {
    memset(rx_ring, 0, sizeof(rx_ring));
    memset(g_bindings, 0, sizeof(g_bindings));
    rx_write = 0;
    ip_set_udp_handler(udp_handle);
}

udp_binding_t *udp_bind(u16 port, udp_rx_fn cb) {
    for (int i = 0; i < MAX_UDP_BINDINGS; i++) {
        if (!g_bindings[i].used) {
            memset(&g_bindings[i], 0, sizeof(udp_binding_t));
            g_bindings[i].used = 1;
            g_bindings[i].local_port = port;
            g_bindings[i].callback = cb;
            serial_write("[udp] bound port "); serial_write_hex((u32)port); serial_write("\n");
            return &g_bindings[i];
        }
    }
    return 0;
}

void udp_unbind(udp_binding_t *b) {
    if (!b) return;
    b->used = 0;
}

int udp_get_bindings(udp_binding_t *out_list, int max_count) {
    if (!out_list || max_count <= 0) return 0;
    int c = 0;
    for (int i = 0; i < MAX_UDP_BINDINGS && c < max_count; i++) {
        if (g_bindings[i].used) {
            memcpy(&out_list[c++], &g_bindings[i], sizeof(udp_binding_t));
        }
    }
    return c;
}

int udp_binding_send(udp_binding_t *b, u32 dst_ip, u16 dst_port, const void *data, u16 len) {
    if (!b || !b->used) return 0;
    int ret = udp_send(dst_ip, dst_port, b->local_port, data, len);
    if (ret) b->tx_packets++;
    return ret;
}

void udp_handle(u32 src_ip, u16 src_port, u16 dst_port,
                const void *data, u16 len) {
    if (len > UDP_RX_DATA) len = UDP_RX_DATA;

    /* 1. Dispatch to registered UDP bindings */
    for (int i = 0; i < MAX_UDP_BINDINGS; i++) {
        if (g_bindings[i].used && g_bindings[i].local_port == dst_port) {
            udp_binding_t *b = &g_bindings[i];
            memcpy(b->rx_buf, data, len);
            b->rx_len = len;
            b->rx_src_ip = src_ip;
            b->rx_src_port = src_port;
            b->has_data = 1;
            b->rx_packets++;
            if (b->callback) {
                b->callback(src_ip, src_port, data, len);
            }
        }
    }

    /* 2. Push into legacy circular ring buffer for polling API */
    udp_rx_slot_t *s = &rx_ring[rx_write % UDP_RX_SLOTS];
    s->src_ip   = src_ip;
    s->src_port = src_port;
    s->dst_port = dst_port;
    memcpy(s->data, data, len);
    s->len  = len;
    s->used = 1;
    rx_write++;
}

int udp_send(u32 dst_ip, u16 dst_port, u16 src_port,
             const void *data, u16 len) {
    if (len > 1400) return 0;
    u16 udp_len = (u16)(8 + len);
    udp_tx_buf[0] = (u8)(src_port >> 8);
    udp_tx_buf[1] = (u8)(src_port & 0xFF);
    udp_tx_buf[2] = (u8)(dst_port >> 8);
    udp_tx_buf[3] = (u8)(dst_port & 0xFF);
    udp_tx_buf[4] = (u8)(udp_len >> 8);
    udp_tx_buf[5] = (u8)(udp_len & 0xFF);
    udp_tx_buf[6] = 0; udp_tx_buf[7] = 0; /* checksum = 0 (optional for UDP) */
    memcpy(udp_tx_buf + 8, data, len);
    return ip_send(dst_ip, IP_PROTO_UDP, udp_tx_buf, udp_len);
}

int udp_recv(u16 local_port, u32 *out_src_ip, u16 *out_src_port,
             void *buf, u16 max_len, u32 max_polls) {
    static int rx_read = 0;
    u32 i;
    for (i = 0; i < max_polls; i++) {
        eth_poll();
        gui_pump_events();
        /* Scan ring for matching port */
        int j;
        for (j = 0; j < UDP_RX_SLOTS; j++) {
            udp_rx_slot_t *s = &rx_ring[(rx_read + j) % UDP_RX_SLOTS];
            if (s->used && s->dst_port == local_port) {
                u16 copy = s->len < max_len ? s->len : max_len;
                memcpy(buf, s->data, copy);
                if (out_src_ip)   *out_src_ip   = s->src_ip;
                if (out_src_port) *out_src_port = s->src_port;
                s->used = 0;
                rx_read = (rx_read + j + 1) % UDP_RX_SLOTS;
                return (int)copy;
            }
        }
    }
    return 0;
}
