#include "ports.h"
#include "tcp.h"
#include "udp.h"
#include "eth.h"
#include <string.h>

typedef struct {
    u16 port;
    u8  proto; /* 1=TCP, 2=UDP, 3=Both */
    const char *name;
} service_mapping_t;

static const service_mapping_t SERVICE_TABLE[] = {
    {7,    3, "echo"},
    {13,   3, "daytime"},
    {21,   1, "ftp"},
    {22,   1, "ssh"},
    {23,   1, "telnet"},
    {25,   1, "smtp"},
    {53,   3, "domain"},
    {67,   2, "bootps"},
    {68,   2, "bootpc"},
    {80,   1, "http"},
    {110,  1, "pop3"},
    {123,  2, "ntp"},
    {143,  1, "imap"},
    {443,  1, "https"},
    {587,  1, "submission"},
    {993,  1, "imaps"},
    {995,  1, "pop3s"},
    {3000, 1, "dev-web"},
    {5000, 1, "flask"},
    {8000, 1, "http-alt"},
    {8080, 1, "http-proxy"},
    {8443, 1, "https-alt"},
    {0,    0, 0}
};

static u16 next_tcp_ephemeral = 49152;
static u16 next_udp_ephemeral = 49152;

void ports_init(void) {
    next_tcp_ephemeral = 49152;
    next_udp_ephemeral = 49152;
}

const char *ports_get_service_name(u16 port, u8 proto) {
    for (int i = 0; SERVICE_TABLE[i].port != 0; i++) {
        if (SERVICE_TABLE[i].port == port) {
            if (SERVICE_TABLE[i].proto == 3 || SERVICE_TABLE[i].proto == proto) {
                return SERVICE_TABLE[i].name;
            }
        }
    }
    return "-";
}

int ports_is_port_in_use(u8 proto, u16 port) {
    if (proto == PORT_TYPE_TCP) {
        tcp_socket_t socks[MAX_TCP_SOCKETS];
        int n = tcp_get_sockets(socks, MAX_TCP_SOCKETS);
        for (int i = 0; i < n; i++) {
            if (socks[i].used && socks[i].src_port == port) {
                return 1;
            }
        }
    } else if (proto == PORT_TYPE_UDP) {
        udp_binding_t binds[MAX_UDP_BINDINGS];
        int n = udp_get_bindings(binds, MAX_UDP_BINDINGS);
        for (int i = 0; i < n; i++) {
            if (binds[i].used && binds[i].local_port == port) {
                return 1;
            }
        }
    }
    return 0;
}

u16 ports_alloc_ephemeral(u8 proto) {
    u16 *ptr = (proto == PORT_TYPE_TCP) ? &next_tcp_ephemeral : &next_udp_ephemeral;
    for (int attempt = 0; attempt < 16384; attempt++) {
        u16 candidate = *ptr;
        (*ptr)++;
        if (*ptr < 49152 || *ptr > 65530) {
            *ptr = 49152;
        }
        if (!ports_is_port_in_use(proto, candidate)) {
            return candidate;
        }
    }
    return 49152;
}

int ports_get_active_list(port_entry_t *out_list, int max_entries) {
    if (!out_list || max_entries <= 0) return 0;
    int count = 0;

    /* 1. Gather active TCP sockets */
    tcp_socket_t socks[MAX_TCP_SOCKETS];
    int tcp_n = tcp_get_sockets(socks, MAX_TCP_SOCKETS);
    for (int i = 0; i < tcp_n && count < max_entries; i++) {
        if (!socks[i].used) continue;
        port_entry_t *e = &out_list[count++];
        memset(e, 0, sizeof(*e));
        e->proto = PORT_TYPE_TCP;
        e->local_port = socks[i].src_port;
        e->remote_port = socks[i].dst_port;
        e->local_ip = socks[i].src_ip ? socks[i].src_ip : g_ip;
        e->remote_ip = socks[i].dst_ip;
        e->rx_bytes = socks[i].rx_bytes;
        e->tx_bytes = socks[i].tx_bytes;
        e->rx_packets = 0;
        e->tx_packets = 0;

        if (socks[i].is_listener) {
            e->state = PORT_STATE_LISTEN;
        } else if (socks[i].state == TCP_STATE_ESTABLISHED) {
            e->state = PORT_STATE_ESTABLISHED;
        } else if (socks[i].state == TCP_STATE_SYN_SENT) {
            e->state = PORT_STATE_SYN_SENT;
        } else if (socks[i].state == TCP_STATE_CLOSE_WAIT) {
            e->state = PORT_STATE_CLOSE_WAIT;
        } else {
            e->state = PORT_STATE_BOUND;
        }

        const char *svc = ports_get_service_name(e->local_port, PORT_TYPE_TCP);
        if (strcmp(svc, "-") == 0 && e->remote_port != 0) {
            svc = ports_get_service_name(e->remote_port, PORT_TYPE_TCP);
        }
        strncpy(e->service, svc, sizeof(e->service) - 1);
    }

    /* 2. Gather active UDP bindings */
    udp_binding_t binds[MAX_UDP_BINDINGS];
    int udp_n = udp_get_bindings(binds, MAX_UDP_BINDINGS);
    for (int i = 0; i < udp_n && count < max_entries; i++) {
        if (!binds[i].used) continue;
        port_entry_t *e = &out_list[count++];
        memset(e, 0, sizeof(*e));
        e->proto = PORT_TYPE_UDP;
        e->local_port = binds[i].local_port;
        e->remote_port = binds[i].rx_src_port;
        e->local_ip = g_ip;
        e->remote_ip = binds[i].rx_src_ip;
        e->state = PORT_STATE_BOUND;
        e->rx_packets = binds[i].rx_packets;
        e->tx_packets = binds[i].tx_packets;

        const char *svc = ports_get_service_name(e->local_port, PORT_TYPE_UDP);
        strncpy(e->service, svc, sizeof(e->service) - 1);
    }

    return count;
}

int ports_get_active_count(void) {
    port_entry_t tmp[MAX_TCP_SOCKETS + MAX_UDP_BINDINGS];
    return ports_get_active_list(tmp, MAX_TCP_SOCKETS + MAX_UDP_BINDINGS);
}
