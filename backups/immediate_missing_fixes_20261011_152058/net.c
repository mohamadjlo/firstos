#include "net.h"
#include "virtio_net.h"
#include "eth.h"
#include "arp.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "dhcp.h"
#include "dns.h"
#include "tcp.h"
#include "ports.h"
#include "firewall.h"
#include "http_client.h"
#include "sound.h"
#include "serial.h"
#include "fs.h"
#include <string.h>
#include <stdio.h>

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (src) {
        while (i < max - 1 && src[i]) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static int str_equal(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int str_equal_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

/* Access point database seeded with real macOS scanned networks */
static wifi_ap_t wifi_access_points[MAX_WIFI_NETWORKS] = {
    {"Vodafone-6414",     1, 97, 1, "5C:13:AC:07:DE:70", ""},
    {"Vodafone Hotspot",  0, 92, 0, "5E:13:AC:07:DE:71", ""},
    {"Vodafone Homespot", 0, 91, 0, "5E:13:AC:07:DE:72", ""},
    {"Vodafone-E0BC",     1, 79, 0, "00:1A:2B:3C:4D:5E", ""},
    {"Vodafone-060C",     1, 64, 0, "44:4E:6D:11:22:33", ""},
    {"Vodafone-6044",     1, 62, 0, "88:E3:AB:99:11:22", ""},
    {"PandaPower",        1, 43, 0, "A0:C5:89:12:34:56", ""},
    {"VFCableMAX",        1, 38, 0, "70:54:D2:88:99:10", ""},
    {"Vodafone-2DE4",     1, 35, 0, "68:72:51:AA:BB:CC", ""},
    {"MagentaWLAN-3EYZ",  1, 32, 0, "DC:39:79:42:11:00", ""},
    {"Vodafone-F1D4",     1, 22, 0, "00:11:22:33:44:55", ""},
    {"FirstOS Public",    0, 95, 0, "12:34:56:78:9A:BC", ""},
    {"o2-WLAN08",         1, 85, 0, "34:27:92:44:81:AA", ""},
    {"FRITZ!Box 7530 MA", 1, 15, 0, "B0:4E:26:88:99:00", ""},
    {"KJHV Bornemann",    1, 16, 0, "CC:D2:81:44:55:66", ""},
    {"Guest Network",     0, 50, 0, "AA:BB:CC:DD:EE:FF", ""}
};

static int wifi_ap_count = MAX_WIFI_NETWORKS;
static int wifi_scanned_loaded = 0;
static net_state_t g_net_state;
static int g_net_initialized = 0;

static void net_load_saved_passwords(void) {
    if (!fs_ready()) return;
    static char buf[2048];
    int sz = fs_read_at("/wifi_saved.txt", buf, sizeof(buf) - 1);
    if (sz <= 0) return;
    buf[sz] = 0;

    const char *line = buf;
    while (*line) {
        const char *eol = line;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;
        if (eol > line) {
            const char *eq = line;
            while (eq < eol && *eq != '=') eq++;
            if (eq < eol && eq > line) {
                char s_ssid[WIFI_SSID_MAX];
                char s_pass[WIFI_PASS_MAX];
                int slen = (int)(eq - line);
                if (slen >= WIFI_SSID_MAX) slen = WIFI_SSID_MAX - 1;
                memcpy(s_ssid, line, slen);
                s_ssid[slen] = 0;

                int plen = (int)(eol - (eq + 1));
                if (plen >= WIFI_PASS_MAX) plen = WIFI_PASS_MAX - 1;
                memcpy(s_pass, eq + 1, plen);
                s_pass[plen] = 0;

                for (int i = 0; i < wifi_ap_count; i++) {
                    if (str_equal(wifi_access_points[i].ssid, s_ssid)) {
                        str_copy(wifi_access_points[i].expected_pass, s_pass, sizeof(wifi_access_points[i].expected_pass));
                        break;
                    }
                }
            }
        }
        while (*eol == '\n' || *eol == '\r') eol++;
        line = eol;
    }
}

static void net_save_saved_password(const char *ssid, const char *password) {
    if (!fs_ready() || !ssid || !ssid[0] || !password || !password[0]) return;
    static char buf[2048];
    static char new_buf[2048];
    int sz = fs_read_at("/wifi_saved.txt", buf, sizeof(buf) - 1);
    if (sz < 0) sz = 0;
    buf[sz] = 0;

    int new_pos = 0;
    int updated = 0;

    if (sz > 0) {
        const char *line = buf;
        while (*line) {
            const char *eol = line;
            while (*eol && *eol != '\n' && *eol != '\r') eol++;
            if (eol > line) {
                const char *eq = line;
                while (eq < eol && *eq != '=') eq++;
                if (eq < eol && eq > line) {
                    char cur_ssid[WIFI_SSID_MAX];
                    int slen = (int)(eq - line);
                    if (slen >= WIFI_SSID_MAX) slen = WIFI_SSID_MAX - 1;
                    memcpy(cur_ssid, line, slen);
                    cur_ssid[slen] = 0;

                    if (str_equal(cur_ssid, ssid)) {
                        int w = snprintf(new_buf + new_pos, sizeof(new_buf) - new_pos, "%s=%s\n", ssid, password);
                        if (w > 0) new_pos += w;
                        updated = 1;
                    } else {
                        int llen = (int)(eol - line);
                        if (new_pos + llen + 2 < (int)sizeof(new_buf)) {
                            memcpy(new_buf + new_pos, line, llen);
                            new_pos += llen;
                            new_buf[new_pos++] = '\n';
                        }
                    }
                }
            }
            while (*eol == '\n' || *eol == '\r') eol++;
            line = eol;
        }
    }

    if (!updated) {
        int w = snprintf(new_buf + new_pos, sizeof(new_buf) - new_pos, "%s=%s\n", ssid, password);
        if (w > 0) new_pos += w;
    }
    new_buf[new_pos] = 0;

    fs_write_at("/wifi_saved.txt", new_buf, (u32)new_pos);
    fs_sync();
}

void net_rescan_wifi(void) {
    if (!fs_ready()) return;

    static char buf[2048];
    int sz = fs_read_at("/wifi_networks.txt", buf, sizeof(buf) - 1);
    if (sz <= 0) return;
    buf[sz] = 0;

    int ap_idx = 0;
    const char *line = buf;
    while (*line && ap_idx < MAX_WIFI_NETWORKS) {
        const char *eol = line;
        while (*eol && *eol != '\n' && *eol != '\r') eol++;

        if (eol > line) {
            char ssid[32] = {0};
            int signal = 50;
            int encrypted = 1;
            int connected = 0;
            char bssid[20] = {0};

            const char *p = line;
            int fld = 0;
            while (p < eol) {
                const char *sep = p;
                while (sep < eol && *sep != '|') sep++;
                int flen = (int)(sep - p);

                if (fld == 0) {
                    int clen = flen < 31 ? flen : 31;
                    memcpy(ssid, p, clen);
                    ssid[clen] = 0;
                } else if (fld == 1) {
                    signal = 0;
                    for (int k = 0; k < flen; k++) {
                        if (p[k] >= '0' && p[k] <= '9') signal = signal * 10 + (p[k] - '0');
                    }
                    if (signal > 100) signal = 100;
                    if (signal < 0) signal = 0;
                } else if (fld == 2) {
                    encrypted = (flen > 0 && p[0] == '1') ? 1 : 0;
                } else if (fld == 3) {
                    connected = (flen > 0 && p[0] == '1') ? 1 : 0;
                } else if (fld == 4) {
                    int clen = flen < 19 ? flen : 19;
                    memcpy(bssid, p, clen);
                    bssid[clen] = 0;
                }

                fld++;
                p = (sep < eol) ? sep + 1 : eol;
            }

            if (ssid[0]) {
                str_copy(wifi_access_points[ap_idx].ssid, ssid, sizeof(wifi_access_points[ap_idx].ssid));
                wifi_access_points[ap_idx].signal_strength = signal;
                wifi_access_points[ap_idx].encrypted = encrypted;
                str_copy(wifi_access_points[ap_idx].bssid, bssid[0] ? bssid : "00:00:00:00:00:00", sizeof(wifi_access_points[ap_idx].bssid));
                wifi_access_points[ap_idx].expected_pass[0] = 0;

                int is_this_connected = 0;
                if (g_net_state.connected) {
                    if (g_net_state.ssid[0]) {
                        if (strcmp(ssid, g_net_state.ssid) == 0) {
                            is_this_connected = 1;
                            g_net_state.current_network_idx = ap_idx;
                        }
                    } else if (connected) {
                        is_this_connected = 1;
                        g_net_state.current_network_idx = ap_idx;
                        str_copy(g_net_state.ssid, ssid, sizeof(g_net_state.ssid));
                    }
                }
                wifi_access_points[ap_idx].connected = is_this_connected;
                ap_idx++;
            }
        }

        while (*eol == '\n' || *eol == '\r') eol++;
        line = eol;
    }

    if (ap_idx > 0) {
        wifi_ap_count = ap_idx;
        wifi_scanned_loaded = 1;
        net_load_saved_passwords();
        serial_write("[net] Loaded real host Wi-Fi networks from /wifi_networks.txt\n");
    }
}

void net_init(void) {
    if (g_net_initialized) return;

    /* Use QEMU virtual network (10.0.2.x) with Google DNS fallback */
    g_ip      = 0x0A00020Fu; /* 10.0.2.15      */
    g_gateway = 0x0A000202u; /* 10.0.2.2       */
    g_subnet  = 0xFFFFFF00u; /* 255.255.255.0  */
    g_dns     = 0x08080808u; /* 8.8.8.8        */

    /* Initialize hardware NIC */
    int nic_ok = virtio_net_init();
    if (nic_ok) {
        eth_init();
        eth_set_ip_handler(ip_handle);
        eth_set_arp_handler(arp_handle);
        arp_init();
        icmp_init();
        udp_init();
        tcp_init();
        dns_init();
        arp_announce();
    }

    ports_init();
    firewall_init();

    /* Try loading real Wi-Fi networks if FS is mounted */
    if (fs_ready()) {
        net_rescan_wifi();
    }

    /* Populate default network state (Connected on boot) */
    g_net_state.enabled   = 1;
    g_net_state.connected = 1;
    g_net_state.current_network_idx = 0;
    str_copy(g_net_state.ssid, wifi_access_points[0].ssid, sizeof(g_net_state.ssid));

    str_copy(g_net_state.ip_addr,     "10.0.2.15",        sizeof(g_net_state.ip_addr));
    str_copy(g_net_state.subnet_mask, "255.255.255.0",    sizeof(g_net_state.subnet_mask));
    str_copy(g_net_state.gateway,     "10.0.2.2",         sizeof(g_net_state.gateway));
    str_copy(g_net_state.dns_addr,    "8.8.8.8",          sizeof(g_net_state.dns_addr));
    str_copy(g_net_state.proxy_addr,  "None",             sizeof(g_net_state.proxy_addr));

    if (g_mac[0] || g_mac[1] || g_mac[2]) {
        snprintf(g_net_state.mac_addr, sizeof(g_net_state.mac_addr),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    } else {
        str_copy(g_net_state.mac_addr, "52:54:00:12:34:56", sizeof(g_net_state.mac_addr));
    }

    for (int i = 0; i < wifi_ap_count; i++) {
        wifi_access_points[i].connected = (i == 0);
    }

    g_net_initialized = 1;
}

int net_is_enabled(void) {
    if (!g_net_initialized) net_init();
    return g_net_state.enabled;
}

void net_set_enabled(int enabled) {
    if (!g_net_initialized) net_init();
    g_net_state.enabled = enabled ? 1 : 0;
    if (!g_net_state.enabled) {
        g_net_state.connected = 0;
        for (int i = 0; i < wifi_ap_count; i++) {
            wifi_access_points[i].connected = 0;
        }
    } else {
        /* Reconnect to current network */
        int target = g_net_state.current_network_idx >= 0 ? g_net_state.current_network_idx : 0;
        net_connect(target, "");
    }
}

int net_is_connected(void) {
    if (!g_net_initialized) net_init();
    return g_net_state.enabled && g_net_state.connected;
}

const net_state_t *net_get_state(void) {
    if (!g_net_initialized) net_init();
    return &g_net_state;
}

net_state_t *net_get_state_mut(void) {
    if (!g_net_initialized) net_init();
    return &g_net_state;
}

int net_get_ap_count(void) {
    if (!wifi_scanned_loaded && fs_ready()) {
        net_rescan_wifi();
    }
    return wifi_ap_count;
}

const wifi_ap_t *net_get_ap(int index) {
    if (!wifi_scanned_loaded && fs_ready()) {
        net_rescan_wifi();
    }
    if (index < 0 || index >= wifi_ap_count) return 0;
    return &wifi_access_points[index];
}

int net_connect(int index, const char *password) {
    if (!g_net_initialized) net_init();
    if (!g_net_state.enabled) return 0;
    if (index < 0 || index >= wifi_ap_count) return 0;

    wifi_ap_t *ap = &wifi_access_points[index];
    if (ap->encrypted) {
        const char *pass_to_use = (password && password[0]) ? password : ap->expected_pass;
        /* Standard WPA2 requirement: at least 8 characters */
        if (!pass_to_use || strlen(pass_to_use) < 8) {
            serial_write("[net] Connect rejected: Wi-Fi password must be at least 8 characters\n");
            return 0;
        }
        str_copy(ap->expected_pass, pass_to_use, sizeof(ap->expected_pass));
        net_save_saved_password(ap->ssid, pass_to_use);
    }

    /* Update connection markers */
    for (int i = 0; i < wifi_ap_count; i++) {
        wifi_access_points[i].connected = (i == index);
    }
    g_net_state.connected = 1;
    g_net_state.current_network_idx = index;
    str_copy(g_net_state.ssid, ap->ssid, sizeof(g_net_state.ssid));

    /* Active gateway & IP configuration — QEMU / hypervisor virtual network */
    g_ip      = 0x0A00020Fu; /* 10.0.2.15      */
    g_gateway = 0x0A000202u; /* 10.0.2.2       */
    g_subnet  = 0xFFFFFF00u; /* 255.255.255.0  */
    g_dns     = 0x08080808u; /* 8.8.8.8        */

    str_copy(g_net_state.ip_addr,     "10.0.2.15",      sizeof(g_net_state.ip_addr));
    str_copy(g_net_state.gateway,     "10.0.2.2",       sizeof(g_net_state.gateway));
    str_copy(g_net_state.subnet_mask, "255.255.255.0",  sizeof(g_net_state.subnet_mask));
    str_copy(g_net_state.dns_addr,    "8.8.8.8",        sizeof(g_net_state.dns_addr));

    if (g_mac[0] || g_mac[1] || g_mac[2]) {
        snprintf(g_net_state.mac_addr, sizeof(g_net_state.mac_addr),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
    }

    if (virtio_net_ready()) {
        arp_announce();
    }

    sound_play_event(SOUND_EVENT_NOTIFICATION);
    serial_write("[net] Connected to Wi-Fi: ");
    serial_write(ap->ssid);
    serial_write(" (IP: 10.0.2.15, Gateway: 10.0.2.2, DNS: 8.8.8.8)\n");
    return 1;
}

void net_disconnect(void) {
    if (!g_net_initialized) net_init();
    for (int i = 0; i < wifi_ap_count; i++) {
        wifi_access_points[i].connected = 0;
    }
    g_net_state.connected = 0;
    g_net_state.current_network_idx = -1;
    g_net_state.ssid[0] = 0;
    g_net_state.ip_addr[0] = 0;

    sound_play_event(SOUND_EVENT_CLICK);
    serial_write("[net] Disconnected from Wi-Fi\n");
}

void net_set_dns(const char *dns) {
    if (!g_net_initialized) net_init();
    if (dns) {
        str_copy(g_net_state.dns_addr, dns, sizeof(g_net_state.dns_addr));
    }
}

void net_set_proxy(const char *proxy) {
    if (!g_net_initialized) net_init();
    if (proxy) {
        str_copy(g_net_state.proxy_addr, proxy, sizeof(g_net_state.proxy_addr));
    }
}

int net_resolve_host(const char *host, char *out_ip, int max_len) {
    if (!g_net_initialized) net_init();
    if (!g_net_state.enabled || !g_net_state.connected) return 0;
    if (!host || !out_ip || max_len < 8) return 0;

    /* Try real DNS first (if NIC available and g_dns set) */
    if (g_dns && virtio_net_ready()) {
        int r = dns_resolve(host, out_ip, max_len);
        if (r) return 1;
    }

    /* Fallback: hardcoded IP literals or common table */
    if (host[0] >= '0' && host[0] <= '9') {
        str_copy(out_ip, host, max_len);
        return 1;
    }
    if (str_equal_ci(host, "localhost")) {
        str_copy(out_ip, "127.0.0.1", max_len);
        return 1;
    }
    if (str_equal_ci(host, "example.com") || str_equal_ci(host, "www.example.com")) {
        str_copy(out_ip, "93.184.215.14", max_len);
        return 1;
    }
    if (str_equal_ci(host, "neverssl.com") || str_equal_ci(host, "www.neverssl.com")) {
        str_copy(out_ip, "34.223.124.45", max_len);
        return 1;
    }
    if (str_equal_ci(host, "info.cern.ch")) {
        str_copy(out_ip, "188.184.67.127", max_len);
        return 1;
    }
    if (str_equal_ci(host, "google.com") || str_equal_ci(host, "www.google.com")) {
        str_copy(out_ip, "142.250.180.14", max_len);
        return 1;
    }
    if (str_equal_ci(host, "wikipedia.org") || str_equal_ci(host, "www.wikipedia.org") || str_equal_ci(host, "en.wikipedia.org")) {
        str_copy(out_ip, "185.15.59.224", max_len);
        return 1;
    }
    if (str_equal_ci(host, "youtube.com") || str_equal_ci(host, "www.youtube.com") || str_equal_ci(host, "m.youtube.com")) {
        str_copy(out_ip, "173.194.79.93", max_len);
        return 1;
    }
    if (str_equal_ci(host, "amazon.de") || str_equal_ci(host, "www.amazon.de")) {
        str_copy(out_ip, "3.253.177.16", max_len);
        return 1;
    }
    if (str_equal_ci(host, "amazon.com") || str_equal_ci(host, "www.amazon.com")) {
        str_copy(out_ip, "205.251.242.103", max_len);
        return 1;
    }
    if (str_equal_ci(host, "kernel.org") || str_equal_ci(host, "www.kernel.org")) {
        str_copy(out_ip, "139.178.84.217", max_len);
        return 1;
    }
    if (str_equal_ci(host, "gnu.org") || str_equal_ci(host, "www.gnu.org")) {
        str_copy(out_ip, "209.51.188.148", max_len);
        return 1;
    }
    if (str_equal_ci(host, "apple.com") || str_equal_ci(host, "www.apple.com")) {
        str_copy(out_ip, "17.253.144.10", max_len);
        return 1;
    }
    if (str_equal_ci(host, "github.com") || str_equal_ci(host, "www.github.com")) {
        str_copy(out_ip, "140.82.121.4", max_len);
        return 1;
    }
    if (str_equal_ci(host, "wttr.in") || str_equal_ci(host, "www.wttr.in")) {
        str_copy(out_ip, "5.9.243.187", max_len);
        return 1;
    }
    if (str_equal_ci(host, "news.ycombinator.com")) {
        str_copy(out_ip, "209.216.230.207", max_len);
        return 1;
    }

    /* Generic fallback — return the gateway */
    str_copy(out_ip, "10.0.2.2", max_len);
    return 1;
}

/* HTTP fetch via real TCP/IP stack */
int net_http_fetch(const char *url, char *out_buf, int max_len) {
    if (!g_net_initialized) net_init();
    if (!g_net_state.enabled || !g_net_state.connected) return 0;
    return http_get(url, out_buf, max_len);
}

/* ICMP ping */
int net_ping(const char *host) {
    if (!g_net_initialized) net_init();
    if (!g_net_state.enabled || !g_net_state.connected) return 0;
    char ip_str[20];
    if (!net_resolve_host(host, ip_str, sizeof(ip_str))) return 0;
    /* Parse ip_str -> u32 */
    u32 ip = 0;
    const char *p = ip_str;
    int i;
    for (i = 0; i < 4; i++) {
        u32 oct = 0;
        while (*p >= '0' && *p <= '9') oct = oct * 10 + (u32)(*p++ - '0');
        ip = (ip << 8) | (oct & 0xFF);
        if (*p == '.') p++;
    }
    return icmp_ping(ip);
}
