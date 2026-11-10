#ifndef TCP_H
#define TCP_H

#include "io.h"
#include "bnet_error.h"

#define MAX_TCP_SOCKETS 32

#define TCP_STATE_CLOSED      0
#define TCP_STATE_SYN_SENT    1
#define TCP_STATE_ESTABLISHED 2
#define TCP_STATE_FIN_WAIT    3
#define TCP_STATE_CLOSE_WAIT  4
#define TCP_STATE_LISTEN      5

typedef struct {
    int  used;
    int  is_listener;
    u32  dst_ip, src_ip;
    u16  dst_port, src_port;
    u32  seq, ack_num;
    u32  acked_seq;
    u8   dst_mac[6];
    int  state;
    u8   rx_buf[32768];
    u16  rx_len;
    int  rx_fin;
    u32  tx_bytes;
    u32  rx_bytes;
    bnet_error_t last_error;
    u32  connect_start_tick;
    u32  connect_time_ticks;
    u32  retransmits;
} tcp_socket_t;

void         tcp_init(void);
tcp_socket_t *tcp_connect(u32 dst_ip, u16 dst_port, u32 max_polls);
tcp_socket_t *tcp_connect_ex(u32 dst_ip, u16 dst_port, u32 max_polls, bnet_error_t *out_err);
tcp_socket_t *tcp_listen(u16 local_port);
tcp_socket_t *tcp_accept(tcp_socket_t *listener, u32 max_polls);
int          tcp_send(tcp_socket_t *sock, const void *data, u16 len);
int          tcp_recv(tcp_socket_t *sock, void *buf, u16 max_len, u32 max_polls);
void         tcp_close(tcp_socket_t *sock);
void         tcp_handle(u32 src_ip, const void *segment, u16 len);
int          tcp_get_sockets(tcp_socket_t *out_socks, int max_count);
const char   *tcp_state_name(int state);
void         tcp_get_stats(u32 *out_active, u32 *out_conn, u32 *out_closed);

#endif /* TCP_H */
