#include "net_socket.h"
#include "dns.h"
#include "serial.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_NET_SOCKETS 8
#define DNS_CACHE_SIZE  16

typedef struct {
    char hostname[128];
    u32 ip;
    u32 hits;
    int valid;
} dns_cache_entry_t;

static dns_cache_entry_t g_dns_cache[DNS_CACHE_SIZE];
static net_socket_t g_net_sockets[MAX_NET_SOCKETS];
static int g_socket_pool_inited = 0;

static u32 parse_dotted_ip(const char *s) {
    u32 ip = 0;
    for (int i = 0; i < 4; i++) {
        u32 oct = 0;
        while (*s >= '0' && *s <= '9') {
            oct = oct * 10 + (u32)(*s - '0');
            s++;
        }
        ip = (ip << 8) | (oct & 0xFF);
        if (*s == '.') s++;
    }
    return ip;
}

static int is_ip_string(const char *s) {
    int dots = 0;
    int digits = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '.') dots++;
        else if (s[i] >= '0' && s[i] <= '9') digits++;
        else return 0;
    }
    return (dots == 3 && digits >= 4);
}

void net_socket_init(void) {
    if (g_socket_pool_inited) return;
    memset(g_net_sockets, 0, sizeof(g_net_sockets));
    memset(g_dns_cache, 0, sizeof(g_dns_cache));
    g_socket_pool_inited = 1;
}

int net_socket_resolve_host_ex(const char *hostname, u32 *out_ip, bnet_error_t *out_err) {
    if (!hostname || !hostname[0] || !out_ip) {
        if (out_err) *out_err = BNET_ERR_INTERNAL;
        return 0;
    }
    net_socket_init();

    /* Check if already dotted-quad decimal */
    if (is_ip_string(hostname)) {
        *out_ip = parse_dotted_ip(hostname);
        if (out_err) *out_err = BNET_OK;
        return 1;
    }

    /* Check local DNS LRU cache */
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (g_dns_cache[i].valid && strcmp(g_dns_cache[i].hostname, hostname) == 0) {
            g_dns_cache[i].hits++;
            *out_ip = g_dns_cache[i].ip;
            if (out_err) *out_err = BNET_OK;
            return 1;
        }
    }

    /* Fallback to DNS subsystem */
    serial_write("[net_socket] resolving host: ");
    serial_write(hostname);
    serial_write("\n");

    char ip_str[32];
    ip_str[0] = 0;
    if (dns_resolve_ex(hostname, ip_str, sizeof(ip_str), out_err)) {
        u32 ip = parse_dotted_ip(ip_str);
        if (ip != 0) {
            *out_ip = ip;

            /* Store in DNS cache (replace lowest hits or invalid) */
            int replace_idx = 0;
            u32 min_hits = 0xFFFFFFFF;
            for (int i = 0; i < DNS_CACHE_SIZE; i++) {
                if (!g_dns_cache[i].valid) {
                    replace_idx = i;
                    break;
                }
                if (g_dns_cache[i].hits < min_hits) {
                    min_hits = g_dns_cache[i].hits;
                    replace_idx = i;
                }
            }
            strncpy(g_dns_cache[replace_idx].hostname, hostname, sizeof(g_dns_cache[replace_idx].hostname) - 1);
            g_dns_cache[replace_idx].ip = ip;
            g_dns_cache[replace_idx].hits = 1;
            g_dns_cache[replace_idx].valid = 1;

            serial_write("[net_socket] host ");
            serial_write(hostname);
            serial_write(" resolved to ");
            serial_write(ip_str);
            serial_write("\n");

            if (out_err) *out_err = BNET_OK;
            return 1;
        }
    }

    serial_write("[net_socket] failed to resolve host: ");
    serial_write(hostname);
    serial_write("\n");
    return 0;
}

int net_socket_resolve_host(const char *hostname, u32 *out_ip) {
    return net_socket_resolve_host_ex(hostname, out_ip, NULL);
}

net_socket_t *net_socket_open_ex(const char *host, u16 port, socket_proto_t proto, u32 timeout_polls, bnet_error_t *out_err) {
    net_socket_init();
    if (out_err) *out_err = BNET_OK;

    if (!host || !host[0]) {
        if (out_err) *out_err = BNET_ERR_BAD_URL;
        return NULL;
    }

    u32 ip = 0;
    if (!net_socket_resolve_host_ex(host, &ip, out_err)) {
        return NULL;
    }

    /* Find free slot in socket pool */
    net_socket_t *sock = NULL;
    for (int i = 0; i < MAX_NET_SOCKETS; i++) {
        if (!g_net_sockets[i].is_connected) {
            sock = &g_net_sockets[i];
            break;
        }
    }
    if (!sock) {
        serial_write("[net_socket] pool exhausted\n");
        if (out_err) *out_err = BNET_ERR_INTERNAL;
        return NULL;
    }

    memset(sock, 0, sizeof(net_socket_t));
    sock->proto = proto;
    sock->ip = ip;
    sock->port = port;
    sock->timeout_polls = timeout_polls ? timeout_polls : 400000;
    strncpy(sock->hostname, host, sizeof(sock->hostname) - 1);

    if (proto == SOCKET_PROTO_PLAIN_TCP) {
        serial_write("[net_socket] connecting TCP to ");
        serial_write(host);
        serial_write(":\n");

        tcp_socket_t *tsock = tcp_connect_ex(ip, port, sock->timeout_polls, out_err);
        if (!tsock) {
            serial_write("[net_socket] TCP connect failed\n");
            return NULL;
        }
        sock->tcp_sock = tsock;
        sock->is_connected = 1;
        serial_write("[net_socket] TCP connected\n");
    } else {
        serial_write("[net_socket] connecting TLS to ");
        serial_write(host);
        serial_write(":\n");

        tls_session_t *tls_sess = tls_connect(ip, port, host, sock->timeout_polls);
        if (!tls_sess) {
            serial_write("[net_socket] TLS connect failed\n");
            if (out_err) *out_err = BNET_ERR_TLS_HANDSHAKE_FAILED;
            return NULL;
        }
        sock->tls_sess = tls_sess;
        sock->is_connected = 1;
        serial_write("[net_socket] TLS connected\n");
    }

    if (out_err) *out_err = BNET_OK;
    return sock;
}

net_socket_t *net_socket_open(const char *host, u16 port, socket_proto_t proto, u32 timeout_polls) {
    return net_socket_open_ex(host, port, proto, timeout_polls, NULL);
}

int net_socket_send(net_socket_t *sock, const void *buf, int len) {
    if (!sock || !sock->is_connected || !buf || len <= 0) return -1;

    if (sock->proto == SOCKET_PROTO_PLAIN_TCP) {
        if (!sock->tcp_sock) return -1;
        return tcp_send(sock->tcp_sock, buf, (u16)len);
    } else {
        if (!sock->tls_sess) return -1;
        return tls_send(sock->tls_sess, buf, (u16)len);
    }
}

int net_socket_recv(net_socket_t *sock, void *buf, int max_len, u32 timeout_polls) {
    if (!sock || !sock->is_connected || !buf || max_len <= 0) return -1;

    u32 tout = timeout_polls ? timeout_polls : sock->timeout_polls;
    if (sock->proto == SOCKET_PROTO_PLAIN_TCP) {
        if (!sock->tcp_sock) return -1;
        return tcp_recv(sock->tcp_sock, buf, (u16)max_len, tout);
    } else {
        if (!sock->tls_sess) return -1;
        return tls_recv(sock->tls_sess, buf, (u16)max_len, tout);
    }
}

void net_socket_close(net_socket_t *sock) {
    if (!sock || !sock->is_connected) return;

    if (sock->proto == SOCKET_PROTO_PLAIN_TCP) {
        if (sock->tcp_sock) {
            tcp_close(sock->tcp_sock);
            sock->tcp_sock = NULL;
        }
    } else {
        if (sock->tls_sess) {
            tls_close(sock->tls_sess);
            sock->tls_sess = NULL;
        }
    }

    sock->is_connected = 0;
}
