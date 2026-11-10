#include "tcp.h"
#include "ip.h"
#include "eth.h"
#include "arp.h"
#include "ports.h"
#include "task.h"
#include "serial.h"
#include "gui.h"
#include <string.h>

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

/* Keep polling bounded.  A browser navigation runs on the GUI thread, so a
 * lost peer must never hold that thread in a multi-second busy loop for every
 * receive attempt.  HTTP applies its own total request deadline above this
 * short per-read wait.
 */
#define TCP_CONNECT_POLL_LIMIT 50000u
#define TCP_RECV_MIN_TICKS     10u   /* 100 ms at the 100 Hz timer rate */
#define TCP_RECV_MAX_TICKS     35u   /* 350 ms per receive attempt */

typedef struct {
    u16 src_port, dst_port;
    u32 seq_num, ack_num;
    u8  data_off; /* (header_len/4) << 4 */
    u8  flags;
    u16 window;
    u16 checksum;
    u16 urgent;
} __attribute__((packed)) tcp_hdr_t;

static tcp_socket_t g_sockets[MAX_TCP_SOCKETS];
static u32 initial_seq = 0x12345678u;

/* Pseudo-header for checksum */
typedef struct {
    u32 src_ip, dst_ip;
    u8  zero, proto;
    u16 tcp_len;
} __attribute__((packed)) tcp_pseudo_t;

static u16 tcp_checksum_compute(tcp_socket_t *s, const void *tcp_data, u16 tcp_len) {
    tcp_pseudo_t ph;
    ph.src_ip  = htonl(g_ip);
    ph.dst_ip  = htonl(s->dst_ip);
    ph.zero    = 0;
    ph.proto   = IP_PROTO_TCP;
    ph.tcp_len = htons(tcp_len);

    /* Compute over pseudo + tcp data */
    u32 sum = 0;
    const u8 *p;
    u16 i;

    p = (const u8 *)&ph;
    for (i = 0; i < sizeof(ph); i += 2)
        sum += (u32)((u16)p[i]<<8 | p[i+1]);

    p = (const u8 *)tcp_data;
    for (i = 0; i + 1 < tcp_len; i += 2)
        sum += (u32)((u16)p[i]<<8 | p[i+1]);
    if (tcp_len & 1) sum += (u32)p[tcp_len-1] << 8;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)(~sum & 0xFFFF);
}

static u8 tcp_tx_buf[20 + 1460];

static void tcp_send_seg(tcp_socket_t *s, u8 flags,
                         const void *data, u16 data_len) {
    u16 hdr_len = 20;
    u16 total   = (u16)(hdr_len + data_len);
    tcp_hdr_t *h = (tcp_hdr_t *)tcp_tx_buf;
    h->src_port = htons(s->src_port);
    h->dst_port = htons(s->dst_port);
    h->seq_num  = htonl(s->seq);
    h->ack_num  = (flags & TCP_FLAG_ACK) ? htonl(s->ack_num) : 0;
    h->data_off = (u8)(hdr_len / 4 << 4);
    h->flags    = flags;
    u16 win     = (s->rx_len < sizeof(s->rx_buf)) ? (u16)(sizeof(s->rx_buf) - s->rx_len) : 0;
    h->window   = htons(win);
    h->checksum = 0;
    h->urgent   = 0;
    if (data && data_len) memcpy(tcp_tx_buf + hdr_len, data, data_len);
    h->checksum = htons(tcp_checksum_compute(s, tcp_tx_buf, total));
    ip_send(s->dst_ip, IP_PROTO_TCP, tcp_tx_buf, total);
    s->tx_bytes += total;
}

static tcp_socket_t *tcp_alloc_socket(void) {
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (!g_sockets[i].used) {
            memset(&g_sockets[i], 0, sizeof(tcp_socket_t));
            g_sockets[i].used = 1;
            return &g_sockets[i];
        }
    }
    return 0;
}

void tcp_init(void) {
    memset(g_sockets, 0, sizeof(g_sockets));
    ip_set_tcp_handler(tcp_handle);
}

int tcp_get_sockets(tcp_socket_t *out_socks, int max_count) {
    if (!out_socks || max_count <= 0) return 0;
    int c = 0;
    for (int i = 0; i < MAX_TCP_SOCKETS && c < max_count; i++) {
        if (g_sockets[i].used) {
            memcpy(&out_socks[c++], &g_sockets[i], sizeof(tcp_socket_t));
        }
    }
    return c;
}

void tcp_handle(u32 src_ip, const void *segment, u16 len) {
    if (len < 20) return;
    const tcp_hdr_t *h = (const tcp_hdr_t *)segment;
    u16 src_port = ntohs(h->src_port);
    u16 dst_port = ntohs(h->dst_port);
    u8  flags    = h->flags;
    u32 seg_seq  = ntohl(h->seq_num);
    u32 seg_ack  = ntohl(h->ack_num);
    u16 hdr_len  = (u16)((h->data_off >> 4) * 4);
    if (hdr_len > len) return;
    const u8 *data = (const u8 *)segment + hdr_len;
    u16 data_len   = (u16)(len - hdr_len);

    /* 1. Demultiplex to active connection */
    tcp_socket_t *sock = 0;
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (g_sockets[i].used && !g_sockets[i].is_listener) {
            if (g_sockets[i].src_port == dst_port &&
                g_sockets[i].dst_port == src_port &&
                g_sockets[i].dst_ip   == src_ip) {
                sock = &g_sockets[i];
                break;
            }
        }
    }

    /* 2. Check for listening socket if incoming SYN */
    if (!sock && (flags & TCP_FLAG_SYN) && !(flags & TCP_FLAG_ACK)) {
        for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
            if (g_sockets[i].used && g_sockets[i].is_listener &&
                g_sockets[i].src_port == dst_port) {
                /* Found listener! Allocate connection socket */
                tcp_socket_t *child = tcp_alloc_socket();
                if (child) {
                    child->src_port = dst_port;
                    child->dst_port = src_port;
                    child->src_ip   = g_ip;
                    child->dst_ip   = src_ip;
                    child->seq      = initial_seq;
                    initial_seq    += 0x10000u;
                    child->ack_num  = seg_seq + 1;
                    child->acked_seq= child->seq;
                    child->state    = TCP_STATE_ESTABLISHED;
                    arp_lookup(src_ip, child->dst_mac);
                    tcp_send_seg(child, TCP_FLAG_SYN | TCP_FLAG_ACK, 0, 0);
                    child->seq++;
                }
                return;
            }
        }
    }

    if (!sock) return;

    sock->rx_bytes += len;

    /* Update acknowledged sequence number on any incoming ACK */
    if (flags & TCP_FLAG_ACK) {
        sock->acked_seq = seg_ack;
    }

    if (sock->state == TCP_STATE_SYN_SENT) {
        if (flags & TCP_FLAG_RST) {
            serial_write("[tcp] RST received in SYN_SENT\n");
            sock->state = TCP_STATE_CLOSED;
            return;
        }
        if ((flags & TCP_FLAG_SYN) && (flags & TCP_FLAG_ACK)) {
            sock->ack_num = seg_seq + 1;
            sock->seq     = seg_ack;
            sock->acked_seq = seg_ack;
            sock->state   = TCP_STATE_ESTABLISHED;
            tcp_send_seg(sock, TCP_FLAG_ACK, 0, 0);
            return;
        }
    }

    if (sock->state == TCP_STATE_ESTABLISHED) {
        if (flags & TCP_FLAG_RST) { sock->state = TCP_STATE_CLOSED; return; }
        if (data_len > 0) {
            if (seg_seq == sock->ack_num) {
                u16 space = (u16)(sizeof(sock->rx_buf) - sock->rx_len);
                if (data_len > space) data_len = space;
                if (data_len > 0) {
                    memcpy(sock->rx_buf + sock->rx_len, data, data_len);
                    sock->rx_len += data_len;
                    sock->ack_num += data_len;
                    tcp_send_seg(sock, TCP_FLAG_ACK, 0, 0);
                }
            } else if (seg_seq < sock->ack_num) {
                u32 diff = sock->ack_num - seg_seq;
                if (data_len > diff) {
                    const u8 *new_data = data + diff;
                    u16 new_len = (u16)(data_len - diff);
                    u16 space = (u16)(sizeof(sock->rx_buf) - sock->rx_len);
                    if (new_len > space) new_len = space;
                    if (new_len > 0) {
                        memcpy(sock->rx_buf + sock->rx_len, new_data, new_len);
                        sock->rx_len += new_len;
                        sock->ack_num += new_len;
                    }
                }
                tcp_send_seg(sock, TCP_FLAG_ACK, 0, 0);
            } else {
                /* Out-of-order segment: re-ACK current expected sequence */
                tcp_send_seg(sock, TCP_FLAG_ACK, 0, 0);
            }
        }
        if (flags & TCP_FLAG_FIN) {
            sock->ack_num = seg_seq + data_len + 1;
            sock->rx_fin  = 1;
            sock->state   = TCP_STATE_CLOSE_WAIT;
            tcp_send_seg(sock, TCP_FLAG_ACK, 0, 0);
        }
        return;
    }

    if (sock->state == TCP_STATE_FIN_WAIT) {
        if ((flags & TCP_FLAG_FIN) || (flags & TCP_FLAG_ACK)) {
            if (flags & TCP_FLAG_FIN) {
                sock->ack_num = seg_seq + 1;
                tcp_send_seg(sock, TCP_FLAG_ACK, 0, 0);
            }
            sock->state = TCP_STATE_CLOSED;
        }
    }
}

tcp_socket_t *tcp_connect_ex(u32 dst_ip, u16 dst_port, u32 max_polls, bnet_error_t *out_err) {
    if (out_err) *out_err = BNET_ERR_INTERNAL;
    tcp_socket_t *s = tcp_alloc_socket();
    if (!s) return 0;

    if (max_polls == 0 || max_polls > TCP_CONNECT_POLL_LIMIT)
        max_polls = TCP_CONNECT_POLL_LIMIT;

    s->dst_ip   = dst_ip;
    s->dst_port = dst_port;
    s->src_port = ports_alloc_ephemeral(PORT_TYPE_TCP);
    s->src_ip   = g_ip;
    s->seq      = initial_seq;
    initial_seq += 0x10000u;
    s->ack_num  = 0;
    s->acked_seq= s->seq;
    s->state    = TCP_STATE_SYN_SENT;
    s->rx_len   = 0;
    s->rx_fin   = 0;
    s->tx_bytes = 0;
    s->rx_bytes = 0;

    /* Resolve MAC: 10.0.2.x is directly on local hypervisor virtual network */
    u32 next_hop = dst_ip;
    if ((dst_ip & 0xFFFFFF00u) == 0x0A000200u) {
        next_hop = dst_ip;
    } else if ((dst_ip & g_subnet) == (g_ip & g_subnet)) {
        next_hop = dst_ip;
    } else {
        next_hop = g_gateway;
    }

    if (!arp_lookup(next_hop, s->dst_mac)) {
        /* Fallback: if configured gateway ARP failed, try QEMU gateway (10.0.2.2) */
        if (next_hop != 0x0A000202u && arp_lookup(0x0A000202u, s->dst_mac)) {
            next_hop = 0x0A000202u;
        } else {
            serial_write("[tcp] ARP failed for next hop "); serial_write_hex(next_hop); serial_write("\n");
            s->state = TCP_STATE_CLOSED;
            s->used = 0;
            if (out_err) *out_err = BNET_ERR_TCP_CONNECT_FAILED;
            return 0;
        }
    }

    serial_write("[tcp] connect start dst="); serial_write_hex(dst_ip);
    serial_write(" port="); serial_write_hex((u32)dst_port);
    serial_write(" (SYN sent)\n");
    tcp_send_seg(s, TCP_FLAG_SYN, 0, 0);
    s->seq++;

    u32 i;
    for (i = 0; i < max_polls; i++) {
        eth_poll();
        gui_pump_events();
        if (s->state == TCP_STATE_ESTABLISHED) {
            serial_write("[tcp] connection established\n");
            if (out_err) *out_err = BNET_OK;
            return s;
        }
        if (s->state == TCP_STATE_CLOSED) {
            serial_write("[tcp] connection refused / reset by peer\n");
            if (out_err) *out_err = BNET_ERR_TCP_RESET;
            s->used = 0;
            return 0;
        }
    }
    serial_write("[tcp] connect timeout (no SYN-ACK received)\n");
    s->state = TCP_STATE_CLOSED;
    s->used = 0;
    if (out_err) *out_err = BNET_ERR_TCP_TIMEOUT;
    return 0;
}

tcp_socket_t *tcp_connect(u32 dst_ip, u16 dst_port, u32 max_polls) {
    return tcp_connect_ex(dst_ip, dst_port, max_polls, NULL);
}

tcp_socket_t *tcp_listen(u16 local_port) {
    tcp_socket_t *s = tcp_alloc_socket();
    if (!s) return 0;
    s->is_listener = 1;
    s->src_port = local_port;
    s->src_ip = g_ip;
    s->state = TCP_STATE_LISTEN;
    serial_write("[tcp] listening on port "); serial_write_hex((u32)local_port); serial_write("\n");
    return s;
}

tcp_socket_t *tcp_accept(tcp_socket_t *listener, u32 max_polls) {
    if (!listener || !listener->is_listener) return 0;
    for (u32 p = 0; p < max_polls; p++) {
        eth_poll();
        for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
            if (g_sockets[i].used && !g_sockets[i].is_listener &&
                g_sockets[i].src_port == listener->src_port &&
                g_sockets[i].state == TCP_STATE_ESTABLISHED) {
                return &g_sockets[i];
            }
        }
    }
    return 0;
}

int tcp_send(tcp_socket_t *sock, const void *data, u16 len) {
    if (!sock || sock->state != TCP_STATE_ESTABLISHED) return 0;
    const u8 *ptr = (const u8 *)data;
    u16 sent = 0;
    while (sent < len) {
        u16 chunk = (u16)(len - sent);
        if (chunk > 1460) chunk = 1460;
        u32 target_seq = sock->seq + chunk;
        tcp_send_seg(sock, TCP_FLAG_PSH | TCP_FLAG_ACK, ptr + sent, chunk);
        sock->seq = target_seq;
        sent = (u16)(sent + chunk);

        /* Wait for ACK or break immediately once acknowledged */
        u32 i;
        for (i = 0; i < 50000; i++) {
            eth_poll();
            gui_pump_events();
            if (sock->state != TCP_STATE_ESTABLISHED) return sent;
            if (sock->acked_seq >= target_seq) break;
        }
    }
    return sent;
}

int tcp_recv(tcp_socket_t *sock, void *buf, u16 max_len, u32 max_polls) {
    if (!sock) return 0;
    if (sock->rx_len > 0) {
        u16 copy = sock->rx_len < max_len ? sock->rx_len : max_len;
        memcpy(buf, sock->rx_buf, copy);
        if (copy < sock->rx_len) {
            u16 rem = sock->rx_len - copy;
            for (u16 i = 0; i < rem; i++) {
                sock->rx_buf[i] = sock->rx_buf[copy + i];
            }
            sock->rx_len = rem;
        } else {
            sock->rx_len = 0;
        }
        if (sock->state == TCP_STATE_ESTABLISHED && copy > 0) {
            tcp_send_seg(sock, TCP_FLAG_ACK, 0, 0);
        }
        return (int)copy;
    }

    u32 start_tick = get_system_ticks();
    u32 timeout_ticks = (max_polls / 100000) * 100;
    if (timeout_ticks < TCP_RECV_MIN_TICKS) timeout_ticks = TCP_RECV_MIN_TICKS;
    if (timeout_ticks > TCP_RECV_MAX_TICKS) timeout_ticks = TCP_RECV_MAX_TICKS;

    while (get_system_ticks() - start_tick < timeout_ticks) {
        eth_poll();
        gui_pump_events();
        if (sock->rx_len > 0 || sock->rx_fin || sock->state == TCP_STATE_CLOSED)
            break;
    }
    u16 copy = sock->rx_len < max_len ? sock->rx_len : max_len;
    if (copy > 0) {
        memcpy(buf, sock->rx_buf, copy);
        if (copy < sock->rx_len) {
            u16 rem = sock->rx_len - copy;
            for (u16 i = 0; i < rem; i++) {
                sock->rx_buf[i] = sock->rx_buf[copy + i];
            }
            sock->rx_len = rem;
        } else {
            sock->rx_len = 0;
        }
        if (sock->state == TCP_STATE_ESTABLISHED) {
            tcp_send_seg(sock, TCP_FLAG_ACK, 0, 0);
        }
    }
    return (int)copy;
}

void tcp_close(tcp_socket_t *sock) {
    if (!sock || sock->state == TCP_STATE_CLOSED) {
        if (sock) {
            sock->used = 0;
            sock->rx_len = 0;
        }
        return;
    }
    if (!sock->is_listener) {
        tcp_send_seg(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
        sock->seq++;
        sock->state = TCP_STATE_FIN_WAIT;
        u32 i;
        for (i = 0; i < 10000; i++) {
            eth_poll();
            gui_pump_events();
            if (sock->state == TCP_STATE_CLOSED) break;
        }
    }
    sock->state = TCP_STATE_CLOSED;
    sock->used = 0;
    sock->rx_len = 0;
}
