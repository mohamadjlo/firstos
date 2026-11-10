#include "eth.h"
#include "virtio_net.h"
#include "serial.h"
#include <string.h>

u8  g_mac[6]    = {0};
u32 g_ip        = 0x0A00020Fu; /* 10.0.2.15 */
u32 g_gateway   = 0x0A000202u; /* 10.0.2.2  */
u32 g_subnet    = 0xFFFFFF00u; /* 255.255.255.0 */
u32 g_dns       = 0x0A000203u; /* 10.0.2.3  */

static void (*ip_handler)(const void *frame, u16 len)  = 0;
static void (*arp_handler)(const void *frame, u16 len) = 0;

void eth_set_ip_handler(void (*fn)(const void *frame, u16 len))  { ip_handler  = fn; }
void eth_set_arp_handler(void (*fn)(const void *frame, u16 len)) { arp_handler = fn; }

void eth_init(void) {
    virtio_net_get_mac(g_mac);
}

static u8 rx_frame[1536];

void eth_poll(void) {
    extern void ps2_poll(void);
    ps2_poll();
    int i;
    for (i = 0; i < 8; i++) {
        u16 len = 0;
        if (!virtio_net_rx_poll(rx_frame, &len)) break;
        if (len < ETH_HDR_LEN) continue;
        eth_hdr_t *hdr = (eth_hdr_t *)rx_frame;
        u16 etype = ntohs(hdr->ethertype);
        const void *payload = rx_frame + ETH_HDR_LEN;
        u16 plen  = (u16)(len - ETH_HDR_LEN);
        if (etype == ETH_TYPE_IP  && ip_handler)  ip_handler(payload, plen);
        if (etype == ETH_TYPE_ARP && arp_handler) arp_handler(payload, plen);
    }
}

static u8 tx_frame[1514];

int eth_send(const u8 dst_mac[6], u16 ethertype, const void *payload, u16 payload_len) {
    if (!virtio_net_ready()) return 0;
    if (ETH_HDR_LEN + payload_len > 1514) return 0;
    eth_hdr_t *hdr = (eth_hdr_t *)tx_frame;
    memcpy(hdr->dst, dst_mac, ETH_ALEN);
    memcpy(hdr->src, g_mac,   ETH_ALEN);
    hdr->ethertype = htons(ethertype);
    memcpy(tx_frame + ETH_HDR_LEN, payload, payload_len);
    return virtio_net_tx(tx_frame, (u16)(ETH_HDR_LEN + payload_len));
}
