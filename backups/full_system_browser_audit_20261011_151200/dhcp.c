#include "dhcp.h"
#include "udp.h"
#include "eth.h"
#include "serial.h"
#include <string.h>

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_MAGIC       0x63825363u

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

#define BCAST_IP 0xFFFFFFFFu

typedef struct {
    u8  op, htype, hlen, hops;
    u32 xid;
    u16 secs, flags;
    u32 ciaddr, yiaddr, siaddr, giaddr;
    u8  chaddr[16];
    u8  sname[64];
    u8  file[128];
    u32 magic;
    u8  options[312];
} __attribute__((packed)) dhcp_pkt_t;

static u8 dhcp_buf[sizeof(dhcp_pkt_t)];

static int dhcp_opt_get(const u8 *opts, u16 opts_len, u8 type, u8 *out, int max) {
    u16 i = 0;
    while (i < opts_len && opts[i] != 255) {
        u8 t = opts[i++];
        if (i >= opts_len) break;
        u8 l = opts[i++];
        if (t == type) {
            int copy = l < max ? l : max;
            int k; for (k=0;k<copy;k++) out[k] = opts[i+k];
            return copy;
        }
        i += l;
    }
    return 0;
}

static u32 b4_to_u32(const u8 *b) {
    return ((u32)b[0]<<24)|((u32)b[1]<<16)|((u32)b[2]<<8)|(u32)b[3];
}
static void u32_to_b4(u32 v, u8 *b) {
    b[0]=(u8)(v>>24); b[1]=(u8)(v>>16); b[2]=(u8)(v>>8); b[3]=(u8)v;
}

int dhcp_discover(void) {
    static u8 rx_buf[sizeof(dhcp_pkt_t) + 16];
    u32 xid = 0x12345678u;

    /* ---- DISCOVER ---- */
    dhcp_pkt_t *pkt = (dhcp_pkt_t *)dhcp_buf;
    memset(pkt, 0, sizeof(dhcp_pkt_t));
    pkt->op    = 1; pkt->htype = 1; pkt->hlen = 6; pkt->hops = 0;
    pkt->xid   = htonl(xid);
    pkt->flags = htons(0x8000); /* broadcast */
    memcpy(pkt->chaddr, g_mac, 6);
    pkt->magic = htonl(DHCP_MAGIC);
    /* options: msg type, param request list, end */
    u8 *o = pkt->options;
    *o++ = 53; *o++ = 1; *o++ = DHCP_DISCOVER;
    *o++ = 55; *o++ = 4; *o++ = 1; *o++ = 3; *o++ = 6; *o++ = 15;
    *o++ = 255;

    serial_write("[dhcp] sending discover\n");
    udp_send(BCAST_IP, DHCP_SERVER_PORT, DHCP_CLIENT_PORT,
             dhcp_buf, (u16)sizeof(dhcp_pkt_t));

    /* ---- Wait for OFFER ---- */
    int rx_len = udp_recv(DHCP_CLIENT_PORT, 0, 0,
                          rx_buf, (u16)sizeof(rx_buf), 800000);
    if (rx_len < (int)(sizeof(dhcp_pkt_t) - 4)) {
        serial_write("[dhcp] no offer, using fallback\n");
        goto fallback;
    }
    {
        dhcp_pkt_t *offer = (dhcp_pkt_t *)rx_buf;
        if (ntohl(offer->xid) != xid) goto fallback;
        u8 mtype = 0;
        dhcp_opt_get(offer->options, 308, 53, &mtype, 1);
        if (mtype != DHCP_OFFER) goto fallback;

        u32 offered_ip = ntohl(offer->yiaddr);
        u8 srv_ip_b[4] = {0};
        dhcp_opt_get(offer->options, 308, 54, srv_ip_b, 4);
        u32 srv_ip = b4_to_u32(srv_ip_b);
        serial_write("[dhcp] offer received, offered ip=");
        serial_write_hex(offered_ip); serial_write("\n");

        /* ---- REQUEST ---- */
        dhcp_pkt_t *req = (dhcp_pkt_t *)dhcp_buf;
        memset(req, 0, sizeof(dhcp_pkt_t));
        req->op    = 1; req->htype = 1; req->hlen = 6;
        req->xid   = htonl(xid);
        req->flags = htons(0x8000);
        memcpy(req->chaddr, g_mac, 6);
        req->magic = htonl(DHCP_MAGIC);
        o = req->options;
        *o++ = 53; *o++ = 1; *o++ = DHCP_REQUEST;
        *o++ = 50; *o++ = 4;
        u32_to_b4(offered_ip, o); o += 4;
        *o++ = 54; *o++ = 4;
        u32_to_b4(srv_ip, o); o += 4;
        *o++ = 255;

        udp_send(BCAST_IP, DHCP_SERVER_PORT, DHCP_CLIENT_PORT,
                 dhcp_buf, (u16)sizeof(dhcp_pkt_t));

        /* ---- Wait for ACK ---- */
        rx_len = udp_recv(DHCP_CLIENT_PORT, 0, 0,
                          rx_buf, (u16)sizeof(rx_buf), 800000);
        if (rx_len < (int)(sizeof(dhcp_pkt_t) - 4)) goto fallback;

        dhcp_pkt_t *ack = (dhcp_pkt_t *)rx_buf;
        u8 mtype2 = 0;
        dhcp_opt_get(ack->options, 308, 53, &mtype2, 1);
        if (mtype2 != DHCP_ACK) goto fallback;

        g_ip = ntohl(ack->yiaddr);

        u8 mask_b[4]={0}; dhcp_opt_get(ack->options, 308, 1, mask_b, 4);
        u8 gw_b[4]={0};   dhcp_opt_get(ack->options, 308, 3, gw_b, 4);
        u8 dns_b[4]={0};  dhcp_opt_get(ack->options, 308, 6, dns_b, 4);
        if (mask_b[0]) g_subnet  = b4_to_u32(mask_b);
        if (gw_b[0])   g_gateway = b4_to_u32(gw_b);
        if (dns_b[0])  g_dns     = b4_to_u32(dns_b);

        serial_write("[dhcp] ack: ip="); serial_write_hex(g_ip);
        serial_write(" gw="); serial_write_hex(g_gateway);
        serial_write(" dns="); serial_write_hex(g_dns); serial_write("\n");
        return 1;
    }
fallback:
    g_ip      = 0x0A00020Fu; /* 10.0.2.15  */
    g_gateway = 0x0A000202u; /* 10.0.2.2   */
    g_subnet  = 0xFFFFFF00u;
    g_dns     = 0x0A000203u; /* 10.0.2.3   */
    serial_write("[dhcp] using fallback ip="); serial_write_hex(g_ip); serial_write("\n");
    return 0;
}
