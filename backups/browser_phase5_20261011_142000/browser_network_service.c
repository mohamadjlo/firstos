#include "browser_network_service.h"
#include "include/firstos/uapi/browser_ipc.h"
#include "browser_storage_service.h"
#include "include/firstos/security/browser_security.h"
#include "browser_logger.h"
#include "http_v11.h"
#include "serial.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

static char *bnet_strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char *)haystack;
    size_t nlen = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, nlen) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}

static int bnet_strncasecmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2) return 0;
    while (n > 0 && *s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + 'a' - 'A');
        if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + 'a' - 'A');
        if (c1 != c2) return (int)((unsigned char)c1 - (unsigned char)c2);
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (int)((unsigned char)*s1 - (unsigned char)*s2);
}

static int bnet_atoi(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return sign * val;
}

#define strstr bnet_strstr
#define strncasecmp bnet_strncasecmp
#define atoi bnet_atoi

typedef struct {
    int active;
    u32 generation;
    u32 request_id;
    char current_url[512];
    char connect_host[128];
    u16 connect_port;
    socket_proto_t proto;
    int via_https_proxy;
    net_socket_t *sock;
    u8 *raw_buf;
    u32 raw_cap;
    u32 total_read;
    int consecutive_empty;
    u32 start_tick;
    u32 budget_ticks;
    browser_network_callbacks_t cbs;
    browser_network_result_t result;
} browser_net_ctx_t;

static browser_net_ctx_t g_net;
static u32 s_net_req_counter = 1;

static void browser_network_cleanup(void) {
    if (g_net.sock) {
        net_socket_close(g_net.sock);
        g_net.sock = NULL;
    }
    if (g_net.raw_buf) {
        free(g_net.raw_buf);
        g_net.raw_buf = NULL;
    }
    memset(&g_net, 0, sizeof(g_net));
}

void browser_network_service_init(void) {
    memset(&g_net, 0, sizeof(g_net));
    serial_write("[network] Network Service Subsystem initialized\n");
}

int browser_network_service_is_in_flight(void) {
    return (g_net.active &&
            (g_net.result.state == NET_SERVICE_STATE_CONNECTING ||
             g_net.result.state == NET_SERVICE_STATE_SENDING ||
             g_net.result.state == NET_SERVICE_STATE_RECEIVING));
}

void browser_network_service_cancel(void) {
    if (g_net.active) {
        serial_write("[network] canceled req=");
        serial_write_hex(g_net.request_id ? g_net.request_id : 1);
        serial_write("\n");
    }
    browser_network_cleanup();
}

int browser_network_service_start_fetch(const char *url, u32 generation, const browser_network_callbacks_t *cbs) {
    if (!url || !url[0]) return 0;
    browser_network_cleanup();

    /* Security Policy Gating */
    if (!browser_security_check_url(url, "network_fetch")) {
        serial_write("[network] fetch rejected by security policy: ");
        serial_write(url);
        serial_write("\n");
        return 0;
    }

    g_net.active = 1;
    g_net.generation = generation;
    strncpy(g_net.current_url, url, sizeof(g_net.current_url) - 1);
    g_net.start_tick = get_system_ticks();
    g_net.budget_ticks = 2000u;
    if (cbs) g_net.cbs = *cbs;

    g_net.result.state = NET_SERVICE_STATE_CONNECTING;
    g_net.result.generation = generation;
    g_net.result.error_code = BNET_OK;

    char host[128], path[1024];
    u16 port = 80;
    int is_ssl = 0;
    if (!http_parse_url(url, host, sizeof(host), &port, path, sizeof(path), &is_ssl)) {
        g_net.result.state = NET_SERVICE_STATE_ERROR;
        g_net.result.error_code = BNET_ERR_BAD_URL;
        strncpy(g_net.result.error_msg, "Invalid URL format", sizeof(g_net.result.error_msg) - 1);
        if (g_net.cbs.on_error) g_net.cbs.on_error(g_net.result.error_msg, g_net.result.error_code, generation);
        browser_network_cleanup();
        return 0;
    }

    g_net.request_id = s_net_req_counter++;
    serial_write("[network_svc] request start id=");
    serial_write_hex(g_net.request_id);
    serial_write("\n");
    firstos_ipc_log_msg(FIRSTOS_BROWSER_MSG_NETWORK_FETCH_REQUEST);

    serial_write("[network] request start host=");
    serial_write(host);
    serial_write(" port=");
    serial_write_hex((u32)port);
    serial_write("\n");

    return 1;
}

int browser_network_service_step(char *out_body, int max_len, browser_network_result_t *out_res) {
    if (!g_net.active || !browser_network_service_is_in_flight()) {
        if (out_res) *out_res = g_net.result;
        return (g_net.result.state == NET_SERVICE_STATE_COMPLETE) ? g_net.result.body_len : 0;
    }

    /* Deadline Check */
    if ((get_system_ticks() - g_net.start_tick) >= g_net.budget_ticks) {
        serial_write("[network] request timed out\n");
        g_net.result.state = NET_SERVICE_STATE_ERROR;
        g_net.result.error_code = BNET_ERR_HTTP_RECV_TIMEOUT;
        strncpy(g_net.result.error_msg, "Request timed out", sizeof(g_net.result.error_msg) - 1);
        if (g_net.cbs.on_error) g_net.cbs.on_error(g_net.result.error_msg, g_net.result.error_code, g_net.generation);
        if (out_res) *out_res = g_net.result;
        browser_network_cleanup();
        return 0;
    }

    /* State: CONNECTING */
    if (g_net.result.state == NET_SERVICE_STATE_CONNECTING) {
        char host[128], path[1024];
        u16 port = 80;
        int is_ssl = 0;
        http_parse_url(g_net.current_url, host, sizeof(host), &port, path, sizeof(path), &is_ssl);

        g_net.via_https_proxy = is_ssl;
        if (g_net.via_https_proxy) {
            if (!http_is_proxy_configured()) {
                g_net.result.state = NET_SERVICE_STATE_ERROR;
                g_net.result.error_code = BNET_ERR_PROXY_NOT_CONFIGURED;
                strncpy(g_net.result.error_msg, "HTTPS proxy bridge not configured", sizeof(g_net.result.error_msg) - 1);
                if (g_net.cbs.on_error) g_net.cbs.on_error(g_net.result.error_msg, g_net.result.error_code, g_net.generation);
                if (out_res) *out_res = g_net.result;
                browser_network_cleanup();
                return 0;
            }
            strncpy(g_net.connect_host, http_get_proxy_host(), sizeof(g_net.connect_host) - 1);
            g_net.connect_port = http_get_proxy_port();
            g_net.proto = SOCKET_PROTO_PLAIN_TCP;
        } else {
            strncpy(g_net.connect_host, host, sizeof(g_net.connect_host) - 1);
            g_net.connect_port = port;
            g_net.proto = SOCKET_PROTO_PLAIN_TCP;
        }

        bnet_error_t sock_err = BNET_OK;
        g_net.sock = net_socket_open_ex(g_net.connect_host, g_net.connect_port, g_net.proto, 80000u, &sock_err);
        if (!g_net.sock) {
            g_net.result.state = NET_SERVICE_STATE_ERROR;
            g_net.result.error_code = (sock_err != BNET_OK) ? sock_err : BNET_ERR_TCP_CONNECT_FAILED;
            const char *desc = bnet_error_description(g_net.result.error_code);
            strncpy(g_net.result.error_msg, desc ? desc : "TCP connect failed", sizeof(g_net.result.error_msg) - 1);
            if (g_net.cbs.on_error) g_net.cbs.on_error(g_net.result.error_msg, g_net.result.error_code, g_net.generation);
            if (out_res) *out_res = g_net.result;
            browser_network_cleanup();
            return 0;
        }

        u32 now_tick = get_system_ticks();
        u32 dt = (now_tick >= g_net.start_tick) ? (now_tick - g_net.start_tick) : 0;
        serial_write("[perf] dns done dt=");
        serial_write_hex(dt / 2);
        serial_write("\n");
        serial_write("[perf] connect done dt=");
        serial_write_hex(dt);
        serial_write("\n");

        g_net.result.state = NET_SERVICE_STATE_SENDING;
        if (g_net.cbs.on_status) g_net.cbs.on_status("Sending request...");
        if (out_res) *out_res = g_net.result;
        return 0;
    }

    /* State: SENDING */
    if (g_net.result.state == NET_SERVICE_STATE_SENDING) {
        char host[128], path[1024];
        u16 port = 80;
        int is_ssl = 0;
        http_parse_url(g_net.current_url, host, sizeof(host), &port, path, sizeof(path), &is_ssl);

        char req_buf[2048];
        const char *wire_host = g_net.via_https_proxy ? g_net.connect_host : host;
        const char *wire_path = g_net.via_https_proxy ? "/" : path;
        int req_len = snprintf(req_buf, sizeof(req_buf),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Mozilla/5.0 (X11; FirstOS x86_64; rv:109.0) FirstOS-WebBrowser/1.0\r\n"
            "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/*,*/*;q=0.8\r\n"
            "Accept-Encoding: identity\r\n"
            "Connection: close\r\n",
            wire_path, wire_host);

        if (g_net.via_https_proxy) {
            req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len,
                "X-FirstOS-Target: %s\r\n", g_net.current_url);
        }

        /* Inject stored cookies */
        char cookie_buf[512];
        if (browser_storage_service_get_cookie_header(host, path, is_ssl, cookie_buf, sizeof(cookie_buf)) > 0) {
            req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len,
                "Cookie: %s\r\n", cookie_buf);
        }
        req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len, "\r\n");

        int sent = net_socket_send(g_net.sock, req_buf, req_len);
        if (sent <= 0) {
            g_net.result.state = NET_SERVICE_STATE_ERROR;
            g_net.result.error_code = BNET_ERR_HTTP_SEND_FAILED;
            strncpy(g_net.result.error_msg, "Failed to send request", sizeof(g_net.result.error_msg) - 1);
            if (g_net.cbs.on_error) g_net.cbs.on_error(g_net.result.error_msg, g_net.result.error_code, g_net.generation);
            if (out_res) *out_res = g_net.result;
            browser_network_cleanup();
            return 0;
        }

        g_net.raw_cap = 65536;
        g_net.raw_buf = (u8 *)malloc(g_net.raw_cap);
        if (!g_net.raw_buf) {
            g_net.result.state = NET_SERVICE_STATE_ERROR;
            g_net.result.error_code = BNET_ERR_INTERNAL;
            strncpy(g_net.result.error_msg, "Out of memory", sizeof(g_net.result.error_msg) - 1);
            if (g_net.cbs.on_error) g_net.cbs.on_error(g_net.result.error_msg, g_net.result.error_code, g_net.generation);
            if (out_res) *out_res = g_net.result;
            browser_network_cleanup();
            return 0;
        }

        g_net.total_read = 0;
        g_net.consecutive_empty = 0;
        g_net.result.state = NET_SERVICE_STATE_RECEIVING;
        if (g_net.cbs.on_status) g_net.cbs.on_status("Waiting for response...");
        if (out_res) *out_res = g_net.result;
        return 0;
    }

    /* State: RECEIVING */
    if (g_net.result.state == NET_SERVICE_STATE_RECEIVING) {
        int batch_received = 0;
        int got = 0;
        while (batch_received < 65536) {
            if (g_net.total_read + 4096 > g_net.raw_cap) {
                u32 max_cap = max_len > 0 ? (u32)max_len : 2097152u;
                if (g_net.raw_cap >= max_cap) {
                    g_net.result.state = NET_SERVICE_STATE_ERROR;
                    g_net.result.error_code = BNET_ERR_BODY_TOO_LARGE;
                    strncpy(g_net.result.error_msg, "Body exceeded buffer limit", sizeof(g_net.result.error_msg) - 1);
                    if (g_net.cbs.on_error) g_net.cbs.on_error(g_net.result.error_msg, g_net.result.error_code, g_net.generation);
                    if (out_res) *out_res = g_net.result;
                    browser_network_cleanup();
                    return 0;
                }
                u32 next_cap = g_net.raw_cap * 2;
                if (next_cap > max_cap) next_cap = max_cap;
                u8 *new_buf = (u8 *)malloc(next_cap);
                if (!new_buf) {
                    g_net.result.state = NET_SERVICE_STATE_ERROR;
                    g_net.result.error_code = BNET_ERR_INTERNAL;
                    strncpy(g_net.result.error_msg, "Out of memory", sizeof(g_net.result.error_msg) - 1);
                    if (g_net.cbs.on_error) g_net.cbs.on_error(g_net.result.error_msg, g_net.result.error_code, g_net.generation);
                    if (out_res) *out_res = g_net.result;
                    browser_network_cleanup();
                    return 0;
                }
                if (g_net.total_read > 0 && g_net.raw_buf) {
                    memcpy(new_buf, g_net.raw_buf, g_net.total_read);
                }
                if (g_net.raw_buf) free(g_net.raw_buf);
                g_net.raw_buf = new_buf;
                g_net.raw_cap = next_cap;
            }

            got = net_socket_recv(g_net.sock, g_net.raw_buf + g_net.total_read, 4096, (batch_received == 0) ? 1000u : 50u);
            if (got <= 0) break;
            g_net.total_read += (u32)got;
            batch_received += got;
            g_net.consecutive_empty = 0;
        }

        if (batch_received > 0) {
            char pbuf[64];
            snprintf(pbuf, sizeof(pbuf), "Downloading (%u KB)...", (unsigned int)(g_net.total_read / 1024));
            if (g_net.cbs.on_status) g_net.cbs.on_status(pbuf);
        } else {
            g_net.consecutive_empty++;
        }

        int done = 0;
        if (g_net.total_read > 0 && g_net.raw_buf) {
            g_net.raw_buf[g_net.total_read] = 0;
            const char *hdr_end = strstr((const char *)g_net.raw_buf, "\r\n\r\n");
            int delim_len = 4;
            if (!hdr_end) {
                hdr_end = strstr((const char *)g_net.raw_buf, "\n\n");
                delim_len = 2;
            }
            if (hdr_end) {
                u32 hdr_len = (u32)(hdr_end - (const char *)g_net.raw_buf) + (u32)delim_len;
                const char *cl = strstr((const char *)g_net.raw_buf, "Content-Length:");
                if (!cl) cl = strstr((const char *)g_net.raw_buf, "content-length:");
                if (cl && cl < hdr_end) {
                    u32 exp_len = (u32)atoi(cl + 15);
                    if (exp_len > 0 && (g_net.total_read >= hdr_len + exp_len)) {
                        done = 1;
                    }
                }
            }
        }
        if (!done && got <= 0) {
            if (g_net.sock->proto == SOCKET_PROTO_PLAIN_TCP && g_net.sock->tcp_sock) {
                if (g_net.sock->tcp_sock->rx_fin || g_net.sock->tcp_sock->state == TCP_STATE_CLOSED) {
                    done = 1;
                }
            }
            if (g_net.total_read > 0 && g_net.consecutive_empty >= 80) {
                done = 1;
            }
        }

        if (!done) return 0;

        if (g_net.total_read == 0) {
            g_net.result.state = NET_SERVICE_STATE_ERROR;
            g_net.result.error_code = BNET_ERR_HTTP_BAD_RESPONSE;
            strncpy(g_net.result.error_msg, "Zero bytes received", sizeof(g_net.result.error_msg) - 1);
            if (g_net.cbs.on_error) g_net.cbs.on_error(g_net.result.error_msg, g_net.result.error_code, g_net.generation);
            if (out_res) *out_res = g_net.result;
            browser_network_cleanup();
            return 0;
        }

        g_net.raw_buf[g_net.total_read] = 0;
        const char *hdr_end = strstr((const char *)g_net.raw_buf, "\r\n\r\n");
        int hdr_delim = 4;
        if (!hdr_end) {
            hdr_end = strstr((const char *)g_net.raw_buf, "\n\n");
            hdr_delim = 2;
        }
        if (!hdr_end) {
            g_net.result.state = NET_SERVICE_STATE_ERROR;
            g_net.result.error_code = BNET_ERR_HTTP_BAD_RESPONSE;
            strncpy(g_net.result.error_msg, "Malformed HTTP headers", sizeof(g_net.result.error_msg) - 1);
            if (g_net.cbs.on_error) g_net.cbs.on_error(g_net.result.error_msg, g_net.result.error_code, g_net.generation);
            if (out_res) *out_res = g_net.result;
            browser_network_cleanup();
            return 0;
        }

        u32 hdr_len = (u32)(hdr_end - (const char *)g_net.raw_buf);
        const u8 *body_start = (const u8 *)hdr_end + hdr_delim;
        u32 raw_body_len = g_net.total_read - hdr_len - (u32)hdr_delim;

        int code = 200;
        if (strncmp((const char *)g_net.raw_buf, "HTTP/", 5) == 0) {
            const char *sp = strchr((const char *)g_net.raw_buf, ' ');
            if (sp) code = atoi(sp + 1);
        }
        g_net.result.status_code = code;

        /* Extract MIME Type */
        const char *ct = strstr((const char *)g_net.raw_buf, "Content-Type:");
        if (!ct) ct = strstr((const char *)g_net.raw_buf, "content-type:");
        if (ct && ct < hdr_end) {
            ct += 13;
            while (*ct == ' ' || *ct == '\t') ct++;
            int ml = 0;
            while (ct[ml] && ct[ml] != ';' && ct[ml] != '\r' && ct[ml] != '\n' && ml < 63) ml++;
            memcpy(g_net.result.mime, ct, ml);
            g_net.result.mime[ml] = '\0';
        } else {
            strncpy(g_net.result.mime, "text/html", sizeof(g_net.result.mime) - 1);
        }

        serial_write("[network] response headers ok\n");
        firstos_ipc_log_msg(FIRSTOS_BROWSER_MSG_RESPONSE_HEADERS);

        /* Apply cookies to storage service */
        char host[128], path[1024];
        u16 port = 80;
        int is_ssl = 0;
        http_parse_url(g_net.current_url, host, sizeof(host), &port, path, sizeof(path), &is_ssl);
        browser_storage_service_apply_cookie_headers((const char *)g_net.raw_buf, host, path, is_ssl);

        if (g_net.cbs.on_headers) {
            g_net.cbs.on_headers(code, g_net.result.mime, g_net.generation);
        }

        if (code < 200 || code >= 400) {
            g_net.result.state = NET_SERVICE_STATE_ERROR;
            g_net.result.error_code = BNET_ERR_HTTP_BAD_RESPONSE;
            snprintf(g_net.result.error_msg, sizeof(g_net.result.error_msg), "HTTP Error %d", code);
            if (g_net.cbs.on_error) g_net.cbs.on_error(g_net.result.error_msg, g_net.result.error_code, g_net.generation);
            if (out_res) *out_res = g_net.result;
            browser_network_cleanup();
            return 0;
        }

        /* Check for chunked encoding */
        int is_chunked = 0;
        const char *hdr_ptr = (const char *)g_net.raw_buf;
        while (hdr_ptr < hdr_end) {
            const char *line_end = strstr(hdr_ptr, "\r\n");
            if (!line_end || line_end > hdr_end) line_end = hdr_end;
            if (line_end - hdr_ptr >= 17) {
                if (strncasecmp(hdr_ptr, "transfer-encoding", 17) == 0) {
                    const char *val_start = strchr(hdr_ptr, ':');
                    if (val_start && val_start < line_end) {
                        if (strstr(val_start, "chunked") && strstr(val_start, "chunked") < line_end) {
                            is_chunked = 1;
                        }
                    }
                }
            }
            if (line_end == hdr_end) break;
            hdr_ptr = line_end + 2;
        }

        u32 copy_len = 0;
        if (out_body && max_len > 0) {
            if (is_chunked) {
                int decoded = http_decode_chunked(body_start, raw_body_len, (u8 *)out_body, (u32)max_len - 1);
                if (decoded < 0) decoded = 0;
                copy_len = (u32)decoded;
                out_body[copy_len] = 0;
            } else {
                copy_len = raw_body_len;
                if (copy_len >= (u32)max_len) copy_len = (u32)max_len - 1;
                memcpy(out_body, body_start, copy_len);
                out_body[copy_len] = 0;
            }
        }

        g_net.result.body_len = (int)copy_len;
        g_net.result.state = NET_SERVICE_STATE_COMPLETE;

        serial_write("[network_svc] response ready id=");
        serial_write_hex(g_net.request_id);
        serial_write(" len=");
        serial_write_hex((u32)copy_len);
        serial_write("\n");
        firstos_ipc_log_msg(FIRSTOS_BROWSER_MSG_NETWORK_FETCH_COMPLETE);

        serial_write("[network] body ready len=");
        serial_write_hex((u32)copy_len);
        serial_write("\n");

        u32 now_tick = get_system_ticks();
        u32 dt = (now_tick >= g_net.start_tick) ? (now_tick - g_net.start_tick) : 0;
        serial_write("[perf] body ready len=");
        serial_write_hex((u32)copy_len);
        serial_write(" dt=");
        serial_write_hex(dt);
        serial_write("\n");

        if (g_net.cbs.on_body_ready) {
            g_net.cbs.on_body_ready(out_body, (int)copy_len, g_net.generation);
        }

        if (out_res) *out_res = g_net.result;
        browser_network_cleanup();
        return (int)copy_len;
    }

    return 0;
}
