#ifndef NET_SOCKET_H
#define NET_SOCKET_H

#include "io.h"
#include "tcp.h"
#include "tls.h"
#include "bnet_error.h"

typedef enum {
    SOCKET_PROTO_PLAIN_TCP = 0,
    SOCKET_PROTO_TLS       = 1
} socket_proto_t;

typedef struct net_socket {
    socket_proto_t proto;
    tcp_socket_t *tcp_sock;
    tls_session_t *tls_sess;
    char hostname[128];
    u32 ip;
    u16 port;
    int is_connected;
    u32 timeout_polls;
} net_socket_t;

void net_socket_init(void);
net_socket_t *net_socket_open(const char *host, u16 port, socket_proto_t proto, u32 timeout_polls);
net_socket_t *net_socket_open_ex(const char *host, u16 port, socket_proto_t proto, u32 timeout_polls, bnet_error_t *out_err);
int net_socket_send(net_socket_t *sock, const void *buf, int len);
int net_socket_recv(net_socket_t *sock, void *buf, int max_len, u32 timeout_polls);
void net_socket_close(net_socket_t *sock);

/* DNS with LRU cache helper */
int net_socket_resolve_host(const char *hostname, u32 *out_ip);
int net_socket_resolve_host_ex(const char *hostname, u32 *out_ip, bnet_error_t *out_err);

#endif /* NET_SOCKET_H */
