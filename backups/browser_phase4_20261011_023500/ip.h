#ifndef IP_H
#define IP_H
#include "io.h"
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

typedef struct {
    u8  ver_ihl, dscp_ecn;
    u16 total_len, id, flags_frag;
    u8  ttl, proto;
    u16 checksum;
    u32 src, dst;
} __attribute__((packed)) ip_hdr_t;

int  ip_send(u32 dst_ip, u8 proto, const void *payload, u16 len);
void ip_handle(const void *frame, u16 len);
u16  ip_checksum(const void *data, u16 len);
void ip_set_icmp_handler(void (*fn)(u32 src_ip, const void *data, u16 len));
void ip_set_udp_handler(void (*fn)(u32 src_ip, u16 src_port, u16 dst_port, const void *data, u16 len));
void ip_set_tcp_handler(void (*fn)(u32 src_ip, const void *seg, u16 len));
#endif
