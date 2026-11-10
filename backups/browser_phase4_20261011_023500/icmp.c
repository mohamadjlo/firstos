#include "icmp.h"
#include "ip.h"
#include "eth.h"
#include "firewall.h"
#include <string.h>

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0

static volatile int ping_reply_received = 0;

void icmp_init(void) {
    ping_reply_received = 0;
    ip_set_icmp_handler(icmp_handle);
}

void icmp_handle(u32 src_ip, const void *data, u16 len) {
    if (len < 8) return;
    const u8 *p = (const u8 *)data;
    u8 type = p[0];

    if (type == ICMP_ECHO_REQUEST) {
        /* If stealth mode is active in firewall, ignore ping silently */
        if (firewall_is_enabled() && firewall_is_stealth()) {
            return;
        }

        /* Build reply: flip type to 0, recompute checksum */
        static u8 reply[1024];
        if (len > sizeof(reply)) len = (u16)sizeof(reply);
        memcpy(reply, data, len);
        reply[0] = ICMP_ECHO_REPLY;
        reply[1] = 0;
        reply[2] = 0;
        reply[3] = 0;
        /* Compute checksum */
        u32 sum = 0;
        u16 i;
        for (i = 0; i + 1 < len; i += 2) sum += (u32)((u16)reply[i]<<8|reply[i+1]);
        if (len & 1) sum += (u32)reply[len-1]<<8;
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        u16 csum = (u16)(~sum & 0xFFFF);
        reply[2] = (u8)(csum >> 8);
        reply[3] = (u8)(csum & 0xFF);
        ip_send(src_ip, IP_PROTO_ICMP, reply, len);
    } else if (type == ICMP_ECHO_REPLY) {
        ping_reply_received = 1;
    }
}

int icmp_ping(u32 dst_ip) {
    u8 pkt[16];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = ICMP_ECHO_REQUEST;
    pkt[1] = 0;
    pkt[4] = 0; pkt[5] = 1; /* id=1 */
    pkt[6] = 0; pkt[7] = 1; /* seq=1 */
    /* checksum */
    u32 sum = 0;
    u16 i;
    for (i = 0; i + 1 < 16; i += 2) sum += (u32)((u16)pkt[i]<<8|pkt[i+1]);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    u16 csum = (u16)(~sum & 0xFFFF);
    pkt[2] = (u8)(csum >> 8); pkt[3] = (u8)(csum & 0xFF);

    ping_reply_received = 0;
    ip_send(dst_ip, IP_PROTO_ICMP, pkt, 16);

    int spin;
    for (spin = 0; spin < 5000; spin++) {
        eth_poll();
        if (ping_reply_received) return 1;
    }
    return 0;
}
