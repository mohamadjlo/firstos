#include "ip.h"
#include "eth.h"
#include "arp.h"
#include "firewall.h"
#include "serial.h"
#include <string.h>

static u16 ip_id_counter = 1;

static void (*icmp_handler)(u32 src_ip, const void *data, u16 len) = 0;
static void (*udp_handler)(u32 src_ip, u16 src_port, u16 dst_port, const void *data, u16 len) = 0;
static void (*tcp_handler)(u32 src_ip, const void *seg, u16 len) = 0;

void ip_set_icmp_handler(void (*fn)(u32 s, const void *d, u16 l)) { icmp_handler = fn; }
void ip_set_udp_handler(void (*fn)(u32 s, u16 sp, u16 dp, const void *d, u16 l)) { udp_handler = fn; }
void ip_set_tcp_handler(void (*fn)(u32 s, const void *seg, u16 l)) { tcp_handler = fn; }

u16 ip_checksum(const void *data, u16 len) {
    const u8 *p = (const u8 *)data;
    u32 sum = 0;
    u16 i;
    for (i = 0; i + 1 < len; i += 2)
        sum += (u32)((u16)p[i] << 8 | p[i+1]);
    if (len & 1) sum += (u32)p[len-1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)(~sum & 0xFFFF);
}

static u8 ip_tx_buf[20 + 1500];

int ip_send(u32 dst_ip, u8 proto, const void *payload, u16 len) {
    if (len > 1480) return 0;

    /* Extract ports if TCP/UDP */
    u16 sp = 0, dp = 0;
    if ((proto == IP_PROTO_TCP || proto == IP_PROTO_UDP) && len >= 4) {
        const u8 *p = (const u8 *)payload;
        sp = (u16)(p[0]<<8 | p[1]);
        dp = (u16)(p[2]<<8 | p[3]);
    }

    /* Firewall Egress Inspection */
    if (firewall_is_enabled()) {
        fw_action_t act = firewall_check_output(proto, g_ip, dst_ip, sp, dp, len);
        if (act == FW_ACTION_DROP || act == FW_ACTION_REJECT) {
            serial_write("[firewall] blocked outbound packet\n");
            return 0;
        }
    }

    u32 next_hop = dst_ip;
    /* Route: 10.0.2.x is directly on local hypervisor network; otherwise route via gateway */
    if ((dst_ip & 0xFFFFFF00u) == 0x0A000200u) {
        next_hop = dst_ip;
    } else if ((dst_ip & g_subnet) != (g_ip & g_subnet) && dst_ip != 0xFFFFFFFFu) {
        next_hop = g_gateway;
    }

    u8 dst_mac[6];
    if (!arp_lookup(next_hop, dst_mac)) {
        /* Fallback: if configured gateway ARP failed, try QEMU gateway (10.0.2.2) */
        if (next_hop != 0x0A000202u && arp_lookup(0x0A000202u, dst_mac)) {
            next_hop = 0x0A000202u;
        } else {
            serial_write("[ip] arp failed\n");
            return 0;
        }
    }

    ip_hdr_t *hdr = (ip_hdr_t *)ip_tx_buf;
    hdr->ver_ihl   = 0x45;
    hdr->dscp_ecn  = 0;
    hdr->total_len = htons((u16)(20 + len));
    hdr->id        = htons(ip_id_counter++);
    hdr->flags_frag= 0;
    hdr->ttl       = 64;
    hdr->proto     = proto;
    hdr->checksum  = 0;
    hdr->src       = htonl(g_ip);
    hdr->dst       = htonl(dst_ip);
    hdr->checksum  = htons(ip_checksum(hdr, 20));

    memcpy(ip_tx_buf + 20, payload, len);
    return eth_send(dst_mac, ETH_TYPE_IP, ip_tx_buf, (u16)(20 + len));
}

void ip_handle(const void *frame, u16 len) {
    if (len < 20) return;
    const ip_hdr_t *hdr = (const ip_hdr_t *)frame;
    if ((hdr->ver_ihl & 0xF0) != 0x40) return; /* not IPv4 */
    u16 ihl = (u16)((hdr->ver_ihl & 0x0F) * 4);
    if (ihl < 20 || ihl > len) return;

    u32 src_ip = ntohl(hdr->src);
    u32 dst_ip = ntohl(hdr->dst);
    const void *data = (const u8 *)frame + ihl;
    u16 data_len = (u16)(ntohs(hdr->total_len) - ihl);
    if (data_len > len - ihl) data_len = (u16)(len - ihl);

    /* Extract ports if TCP/UDP */
    u16 sp = 0, dp = 0;
    if ((hdr->proto == IP_PROTO_TCP || hdr->proto == IP_PROTO_UDP) && data_len >= 4) {
        const u8 *p = (const u8 *)data;
        sp = (u16)(p[0]<<8 | p[1]);
        dp = (u16)(p[2]<<8 | p[3]);
    }

    /* Firewall Ingress Inspection */
    if (firewall_is_enabled()) {
        fw_action_t act = firewall_check_input(hdr->proto, src_ip, dst_ip, sp, dp, data_len);
        if (act == FW_ACTION_DROP || act == FW_ACTION_REJECT) {
            return;
        }
    }

    if (hdr->proto == IP_PROTO_ICMP && icmp_handler) {
        icmp_handler(src_ip, data, data_len);
    } else if (hdr->proto == IP_PROTO_UDP && udp_handler) {
        if (data_len < 8) return;
        udp_handler(src_ip, sp, dp, (const u8 *)data + 8, (u16)(data_len - 8));
    } else if (hdr->proto == IP_PROTO_TCP && tcp_handler) {
        tcp_handler(src_ip, data, data_len);
    }
}
