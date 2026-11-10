#ifndef ETH_H
#define ETH_H

#include "io.h"

#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806
#define ETH_ALEN      6
#define ETH_HDR_LEN   14
#define ETH_MTU       1500

static inline u16 bswap16(u16 x) { return (u16)((x >> 8) | (x << 8)); }
static inline u32 bswap32(u32 x) {
    return ((x & 0xFF000000u) >> 24) | ((x & 0x00FF0000u) >> 8)
         | ((x & 0x0000FF00u) <<  8) | ((x & 0x000000FFu) << 24);
}
#define htons(x)  bswap16((u16)(x))
#define ntohs(x)  bswap16((u16)(x))
#define htonl(x)  bswap32((u32)(x))
#define ntohl(x)  bswap32((u32)(x))

typedef struct {
    u8  dst[ETH_ALEN];
    u8  src[ETH_ALEN];
    u16 ethertype; /* big-endian */
} __attribute__((packed)) eth_hdr_t;

extern u8  g_mac[6];
extern u32 g_ip;       /* host byte order */
extern u32 g_gateway;
extern u32 g_subnet;
extern u32 g_dns;

void eth_init(void);
void eth_poll(void);
int  eth_send(const u8 dst_mac[6], u16 ethertype,
              const void *payload, u16 payload_len);
void eth_set_ip_handler(void (*fn)(const void *frame, u16 len));
void eth_set_arp_handler(void (*fn)(const void *frame, u16 len));

#endif
