#include "dns.h"
#include "udp.h"
#include "eth.h"
#include "serial.h"
#include <string.h>

#define DNS_PORT      53
#define DNS_SRC_PORT  1053
#define DNS_CACHE_SZ  8

static dns_cache_item_t dns_cache[DNS_CACHE_SZ];
static int dns_cache_next = 0;
static dns_stats_t g_dns_stats = {0};

static void u32_to_dot(u32 ip, char *out, int max) {
    /* ip in host byte order */
    int pos = 0;
    u8 b[4];
    b[0]=(u8)(ip>>24); b[1]=(u8)(ip>>16); b[2]=(u8)(ip>>8); b[3]=(u8)ip;
    int i;
    for (i = 0; i < 4; i++) {
        if (i) { if (pos < max-1) out[pos++]='.'; }
        u8 v = b[i];
        if (v >= 100) { if (pos<max-1) out[pos++]=(char)('0'+v/100); v%=100; if (pos<max-1) out[pos++]=(char)('0'+v/10); }
        else if (v >= 10) { if (pos<max-1) out[pos++]=(char)('0'+v/10); }
        v %= 10; if (pos<max-1) out[pos++]=(char)('0'+v);
    }
    if (pos < max) out[pos] = 0;
}

static int is_ip_literal(const char *s) {
    int dots=0;
    while (*s) {
        if (*s=='.') dots++;
        else if (*s<'0'||*s>'9') return 0;
        s++;
    }
    return dots==3;
}

static u8 dns_buf[512];
static u8 dns_rx[512];

void dns_init(void) {
    memset(dns_cache, 0, sizeof(dns_cache));
}

int dns_resolve_ex(const char *hostname, char *out_ip, int max_len, bnet_error_t *out_err) {
    if (out_err) *out_err = BNET_ERR_INTERNAL;
    if (!hostname || !hostname[0]) {
        if (out_err) *out_err = BNET_ERR_BAD_URL;
        return 0;
    }

    /* Literal IP? */
    if (is_ip_literal(hostname)) {
        int n = (int)strlen(hostname);
        if (n >= max_len) n = max_len-1;
        memcpy(out_ip, hostname, (u32)n);
        out_ip[n] = 0;
        if (out_err) *out_err = BNET_OK;
        return 1;
    }

    /* Cache check */
    int i;
    for (i = 0; i < DNS_CACHE_SZ; i++) {
        if (dns_cache[i].valid && strcmp(dns_cache[i].host, hostname)==0) {
            int n=(int)strlen(dns_cache[i].ip);
            if (n>=max_len) n=max_len-1;
            memcpy(out_ip, dns_cache[i].ip, (u32)n);
            out_ip[n]=0;
            g_dns_stats.cache_hits++;
            serial_write("[dns] cache hit for "); serial_write(hostname);
            serial_write(" -> "); serial_write(out_ip); serial_write("\n");
            if (out_err) *out_err = BNET_OK;
            return 1;
        }
    }

    /* Check network readiness */
    if (!g_ip || !g_gateway) {
        serial_write("[dns] network unconfigured (g_ip or g_gateway is 0)\n");
        if (out_err) *out_err = BNET_ERR_DNS_START_FAILED;
        return 0;
    }

    /* Always try 8.8.8.8 first; skip any stale QEMU 10.0.2.3 address.
     * Order: configured DNS (if not QEMU placeholder) → 8.8.8.8 → 1.1.1.1 */
    u32 cfg_dns = (g_dns && g_dns != 0x0A000203u) ? g_dns : 0x08080808u;
    u32 dns_servers[3] = { cfg_dns, 0x08080808u, 0x01010101u };

    /* Build DNS query */
    static u16 dns_id = 1;
    memset(dns_buf, 0, sizeof(dns_buf));
    u16 id = dns_id++;
    dns_buf[0]=(u8)(id>>8); dns_buf[1]=(u8)(id&0xFF);
    dns_buf[2]=0x01; dns_buf[3]=0x00; /* standard query, recursion desired */
    dns_buf[4]=0x00; dns_buf[5]=0x01; /* qdcount=1 */

    /* Encode hostname as DNS labels */
    int pos = 12;
    const char *p = hostname;
    while (*p && pos < 490) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        int llen = (int)(dot - p);
        if (llen > 63 || pos + llen + 1 >= 490) break;
        dns_buf[pos++] = (u8)llen;
        int k; for (k=0;k<llen;k++) dns_buf[pos++]=(u8)p[k];
        p = (*dot == '.') ? dot+1 : dot;
    }
    dns_buf[pos++] = 0;       /* root label */
    dns_buf[pos++] = 0; dns_buf[pos++] = 1; /* qtype A */
    dns_buf[pos++] = 0; dns_buf[pos++] = 1; /* qclass IN */

    int rx_len = 0;
    int sidx;
    for (sidx = 0; sidx < 3; sidx++) {
        if (!dns_servers[sidx]) continue;
        if (sidx > 0 && dns_servers[sidx] == dns_servers[0]) continue;
        g_dns_stats.cache_misses++;
        g_dns_stats.queries_sent++;
        serial_write("[dns] query host="); serial_write(hostname);
        serial_write(" server="); serial_write_hex(dns_servers[sidx]); serial_write("\n");
        udp_send(dns_servers[sidx], DNS_PORT, DNS_SRC_PORT, dns_buf, (u16)pos);

        /* Drain and inspect incoming packets until matching transaction id is received */
        while ((rx_len = udp_recv(DNS_SRC_PORT, 0, 0, dns_rx, sizeof(dns_rx), 80000)) >= 12) {
            u16 rid = (u16)((u16)dns_rx[0]<<8|dns_rx[1]);
            if (rid == id) {
                break;
            }
            serial_write("[dns] ignoring stale/mismatched id packet\n");
            rx_len = 0;
        }

        if (rx_len >= 12) break;
    }
    if (rx_len < 12) {
        g_dns_stats.timeouts++;
        serial_write("[dns] timeout: no response for "); serial_write(hostname); serial_write("\n");
        if (out_err) *out_err = BNET_ERR_DNS_TIMEOUT;
        return 0;
    }

    u16 ancount = (u16)((u16)dns_rx[6]<<8|dns_rx[7]);
    if (ancount == 0) {
        g_dns_stats.failures++;
        serial_write("[dns] no answers for "); serial_write(hostname); serial_write("\n");
        if (out_err) *out_err = BNET_ERR_DNS_NO_RECORD;
        return 0;
    }

    /* Skip question section */
    int rpos = 12;
    while (rpos < rx_len && dns_rx[rpos]) {
        if ((dns_rx[rpos] & 0xC0) == 0xC0) { rpos += 2; goto skip_q; }
        rpos += dns_rx[rpos] + 1;
    }
    rpos++; /* null label */
    skip_q:
    rpos += 4; /* qtype + qclass */

    /* Parse answer records */
    u32 found_ip = 0;
    for (i = 0; i < (int)ancount && rpos < rx_len; i++) {
        while (rpos < rx_len && dns_rx[rpos]) {
            if ((dns_rx[rpos] & 0xC0) == 0xC0) { rpos += 2; goto name_done; }
            rpos += dns_rx[rpos] + 1;
        }
        rpos++;
        name_done:;
        if (rpos + 10 > rx_len) break;
        u16 rtype  = (u16)((u16)dns_rx[rpos]<<8|dns_rx[rpos+1]); rpos+=2;
        rpos += 6; /* class(2) + ttl(4) */
        u16 rdlen  = (u16)((u16)dns_rx[rpos]<<8|dns_rx[rpos+1]); rpos+=2;
        if (rtype == 1 && rdlen == 4 && rpos + 4 <= rx_len) {
            found_ip = ((u32)dns_rx[rpos]<<24)|((u32)dns_rx[rpos+1]<<16)
                      |((u32)dns_rx[rpos+2]<<8)|(u32)dns_rx[rpos+3];
            break;
        }
        rpos += rdlen;
    }

    if (!found_ip) {
        g_dns_stats.failures++;
        serial_write("[dns] no A record for "); serial_write(hostname); serial_write("\n");
        if (out_err) *out_err = BNET_ERR_DNS_NO_RECORD;
        return 0;
    }

    u32_to_dot(found_ip, out_ip, max_len);
    g_dns_stats.responses_received++;
    serial_write("[dns] resolved "); serial_write(hostname);
    serial_write(" -> "); serial_write(out_ip); serial_write("\n");

    /* Cache */
    dns_cache_item_t *ce = &dns_cache[dns_cache_next % DNS_CACHE_SZ];
    dns_cache_next++;
    strncpy(ce->host, hostname, 63); ce->host[63]=0;
    strncpy(ce->ip,   out_ip,   15); ce->ip[15]=0;
    ce->valid = 1;
    if (out_err) *out_err = BNET_OK;
    return 1;
}

int dns_resolve(const char *hostname, char *out_ip, int max_len) {
    return dns_resolve_ex(hostname, out_ip, max_len, NULL);
}

void dns_get_stats(dns_stats_t *out_stats) {
    if (out_stats) {
        memcpy(out_stats, &g_dns_stats, sizeof(dns_stats_t));
    }
}

int dns_get_cache_entries(dns_cache_item_t *out_entries, int max_entries) {
    if (!out_entries || max_entries <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DNS_CACHE_SZ && count < max_entries; i++) {
        if (dns_cache[i].valid) {
            memcpy(&out_entries[count++], &dns_cache[i], sizeof(dns_cache_item_t));
        }
    }
    return count;
}
