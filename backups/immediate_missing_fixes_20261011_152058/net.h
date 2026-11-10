#ifndef NET_H
#define NET_H

#define MAX_WIFI_NETWORKS   16
#define WIFI_SSID_MAX       32
#define WIFI_PASS_MAX       64

typedef struct {
    char ssid[WIFI_SSID_MAX];
    int  encrypted;          /* 1 = WPA2-Personal, 0 = Open */
    int  signal_strength;    /* Percentage: 0 to 100% */
    int  connected;          /* 1 if currently connected */
    char bssid[18];         /* Access Point MAC address */
    char expected_pass[WIFI_PASS_MAX]; /* Passphrase for verification */
} wifi_ap_t;

typedef struct {
    int  enabled;               /* 1 = Wi-Fi ON, 0 = Wi-Fi OFF */
    int  connected;             /* 1 = Connected, 0 = Disconnected */
    int  current_network_idx;   /* Index of connected AP or -1 */
    char ssid[WIFI_SSID_MAX];   /* Connected SSID */
    char ip_addr[16];          /* Local IP e.g. "192.168.1.105" */
    char subnet_mask[16];      /* e.g. "255.255.255.0" */
    char gateway[16];          /* e.g. "192.168.1.1" */
    char mac_addr[18];         /* Adapter MAC e.g. "52:54:00:12:34:56" */
    char dns_addr[32];         /* DNS server e.g. "8.8.8.8" (editable) */
    char proxy_addr[32];       /* Proxy e.g. "None" or "192.168.1.1:8080" */
} net_state_t;

/* Initialization and state queries */
void net_init(void);
int  net_is_enabled(void);
void net_set_enabled(int enabled);
int  net_is_connected(void);
const net_state_t *net_get_state(void);
net_state_t *net_get_state_mut(void);

/* Access Point Management */
int  net_get_ap_count(void);
const wifi_ap_t *net_get_ap(int index);
int  net_connect(int index, const char *password);
void net_disconnect(void);
void net_rescan_wifi(void);

/* Configuration */
void net_set_dns(const char *dns);
void net_set_proxy(const char *proxy);

/* DNS & Ping Utilities */
int  net_resolve_host(const char *host, char *out_ip, int max_len);

/* HTTP fetch via real TCP/IP stack */
int  net_http_fetch(const char *url, char *out_buf, int max_len);

/* ICMP ping — returns 1 if host replied, 0 on timeout */
int  net_ping(const char *host);

#endif /* NET_H */
