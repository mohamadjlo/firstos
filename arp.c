#include "arp.h"
#include "eth.h"
#include "serial.h"
#include <string.h>

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2
#define ARP_CACHE_SIZE 16

/* Use byte arrays to avoid alignment issues with packed u32 */
typedef struct {
    u16 htype, ptype;
    u8  hlen, plen;
    u16 op;
    u8  sha[6];
    u8  spa[4];
    u8  tha[6];
    u8  tpa[4];
} __attribute__((packed)) arp_pkt_t;

static arp_entry_t arp_cache[ARP_CACHE_SIZE];

static const u8 bcast_mac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static u32 bytes_to_ip(const u8 b[4]) {
    return ((u32)b[0]<<24)|((u32)b[1]<<16)|((u32)b[2]<<8)|(u32)b[3];
}
static void ip_to_bytes(u32 ip, u8 b[4]) {
    b[0]=(u8)(ip>>24); b[1]=(u8)(ip>>16); b[2]=(u8)(ip>>8); b[3]=(u8)ip;
}

static void arp_cache_store(u32 ip, const u8 mac[6]) {
    int i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid || arp_cache[i].ip == ip) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = 1;
            return;
        }
    }
    /* evict first slot */
    arp_cache[0].ip = ip;
    memcpy(arp_cache[0].mac, mac, 6);
    arp_cache[0].valid = 1;
}

void arp_init(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
    /* QEMU SLIRP virtual router MACs: 10.0.2.2 -> ...02, 10.0.2.3 -> ...03 */
    u8 qemu_gw_mac[6]  = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
    u8 qemu_dns_mac[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x03};
    arp_cache_store(0x0A000202u, qemu_gw_mac);  /* 10.0.2.2 Gateway */
    arp_cache_store(0x0A000203u, qemu_dns_mac); /* 10.0.2.3 DNS Server */
}

static int arp_cache_get(u32 ip, u8 out_mac[6]) {
    int i;
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(out_mac, arp_cache[i].mac, 6);
            return 1;
        }
    }
    return 0;
}

static void arp_send_request(u32 target_ip) {
    arp_pkt_t p;
    memset(&p, 0, sizeof(p));
    p.htype = htons(1);
    p.ptype = htons(0x0800);
    p.hlen  = 6;
    p.plen  = 4;
    p.op    = htons(ARP_OP_REQUEST);
    memcpy(p.sha, g_mac, 6);
    ip_to_bytes(g_ip, p.spa);
    memset(p.tha, 0, 6);
    ip_to_bytes(target_ip, p.tpa);
    eth_send(bcast_mac, ETH_TYPE_ARP, &p, sizeof(p));
}

void arp_handle(const void *frame, u16 len) {
    if (len < (u16)sizeof(arp_pkt_t)) return;
    const arp_pkt_t *p = (const arp_pkt_t *)frame;
    if (ntohs(p->htype) != 1 || ntohs(p->ptype) != 0x0800) return;
    u16 op = ntohs(p->op);
    u32 spa = bytes_to_ip(p->spa);
    u32 tpa = bytes_to_ip(p->tpa);

    if (op == ARP_OP_REPLY) {
        arp_cache_store(spa, p->sha);
    } else if (op == ARP_OP_REQUEST && tpa == g_ip) {
        /* Send reply */
        arp_pkt_t r;
        memset(&r, 0, sizeof(r));
        r.htype = htons(1);
        r.ptype = htons(0x0800);
        r.hlen  = 6;
        r.plen  = 4;
        r.op    = htons(ARP_OP_REPLY);
        memcpy(r.sha, g_mac, 6);
        ip_to_bytes(g_ip, r.spa);
        memcpy(r.tha, p->sha, 6);
        ip_to_bytes(spa, r.tpa);
        eth_send(p->sha, ETH_TYPE_ARP, &r, sizeof(r));
        arp_cache_store(spa, p->sha);
    }
}

int arp_lookup(u32 ip, u8 out_mac[6]) {
    /* Broadcast/self shortcuts */
    if (ip == 0xFFFFFFFFu) { memset(out_mac, 0xFF, 6); return 1; }
    if (ip == g_ip)        { memcpy(out_mac, g_mac, 6); return 1; }
    if (arp_cache_get(ip, out_mac)) return 1;

    /* Send request and poll */
    arp_send_request(ip);
    int spin;
    for (spin = 0; spin < 50000; spin++) {
        eth_poll();
        if (arp_cache_get(ip, out_mac)) return 1;
        if ((spin % 10000) == 0 && spin > 0) {
            arp_send_request(ip);
        }
    }
    return 0;
}

void arp_announce(void) {
    arp_pkt_t p;
    memset(&p, 0, sizeof(p));
    p.htype = htons(1);
    p.ptype = htons(0x0800);
    p.hlen  = 6;
    p.plen  = 4;
    p.op    = htons(ARP_OP_REQUEST);
    memcpy(p.sha, g_mac, 6);
    ip_to_bytes(g_ip, p.spa);
    memset(p.tha, 0, 6);
    ip_to_bytes(g_ip, p.tpa);
    eth_send(bcast_mac, ETH_TYPE_ARP, &p, sizeof(p));
}

int arp_get_table(arp_entry_t *out_entries, int max_entries) {
    if (!out_entries || max_entries <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ARP_CACHE_SIZE && count < max_entries; i++) {
        if (arp_cache[i].valid) {
            memcpy(&out_entries[count++], &arp_cache[i], sizeof(arp_entry_t));
        }
    }
    return count;
}
