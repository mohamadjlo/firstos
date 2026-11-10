#include "http_v11.h"
#include "data_url.h"
#include "serial.h"
#include "task.h"
#include "gui.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* Host HTTPS Proxy Bridge configuration.
 * By default in QEMU user networking, 10.0.2.2 reaches the host.
 * The host proxy bridge is provided by tools/firstos_webproxy.py on port 18080. */
static char g_https_proxy_host[128] = "10.0.2.2";
static u16 g_https_proxy_port = 18080;
static int g_https_proxy_enabled = 1;

void http_set_proxy(const char *host, u16 port) {
    if (!host || !host[0] || port == 0) {
        g_https_proxy_enabled = 0;
        g_https_proxy_host[0] = 0;
        g_https_proxy_port = 0;
        return;
    }
    strncpy(g_https_proxy_host, host, sizeof(g_https_proxy_host) - 1);
    g_https_proxy_host[sizeof(g_https_proxy_host) - 1] = 0;
    g_https_proxy_port = port;
    g_https_proxy_enabled = 1;
}

const char *http_get_proxy_host(void) {
    return g_https_proxy_enabled ? g_https_proxy_host : NULL;
}

u16 http_get_proxy_port(void) {
    return g_https_proxy_enabled ? g_https_proxy_port : 0;
}

int http_is_proxy_configured(void) {
    return g_https_proxy_enabled && g_https_proxy_host[0] != 0 && g_https_proxy_port != 0;
}

/* Convert the public poll timeout into one total response budget.  The old
 * receive loop waited again and again until the peer closed the connection,
 * which made a web navigation capable of blocking the GUI forever. */
static u32 http_timeout_ticks(const http_request_t *req) {
    u32 ticks = (req && req->timeout_polls) ? (req->timeout_polls / 1000u) : 300u;
    if (ticks < 100u) ticks = 100u;  /* 1 second minimum */
    if (ticks > 800u) ticks = 800u;  /* 8 seconds maximum */
    return ticks;
}

static int http_deadline_reached(u32 start_tick, u32 budget_ticks) {
    return (get_system_ticks() - start_tick) >= budget_ticks;
}

static void str_trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    int len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = 0;
    }
}

/* Parse HTTP status line directly and robustly (e.g. "HTTP/1.0 200 OK" or "HTTP/1.1 200") */
static int http_parse_status_line(const char *line, char *ver, int ver_sz, int *code, char *msg, int msg_sz) {
    if (!line || !ver || ver_sz <= 0 || !code || !msg || msg_sz <= 0) return 0;
    ver[0] = 0;
    msg[0] = 0;
    *code = 0;

    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "HTTP/", 5) != 0) return 0;

    /* Extract protocol version */
    int vi = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        if (vi < ver_sz - 1) ver[vi++] = *p;
        p++;
    }
    ver[vi] = 0;

    while (*p == ' ' || *p == '\t') p++;
    if (!*p || *p < '0' || *p > '9') return 0;

    /* Extract status code */
    int val = 0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    if (val < 100 || val > 599) return 0;
    *code = val;

    /* Skip whitespace before reason phrase */
    while (*p == ' ' || *p == '\t') p++;

    /* Extract reason phrase */
    int mi = 0;
    while (*p && *p != '\r' && *p != '\n') {
        if (mi < msg_sz - 1) msg[mi++] = *p;
        p++;
    }
    msg[mi] = 0;
    str_trim(msg);

    return 1;
}

static int str_casecmp(const char *a, const char *b) {
    while (*a && *b) {
        int diff = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (diff != 0) return diff;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int http_parse_url(const char *url, char *out_host, int host_sz, u16 *out_port, char *out_path, int path_sz, int *out_is_ssl) {
    if (!url || !out_host || !out_port || !out_path || !out_is_ssl) return 0;

    int is_ssl = 0;
    const char *p = url;
    if (strncasecmp(p, "http://", 7) == 0) {
        is_ssl = 0;
        p += 7;
    } else if (strncasecmp(p, "https://", 8) == 0) {
        is_ssl = 1;
        p += 8;
    } else {
        return 0;
    }
    *out_is_ssl = is_ssl;
    *out_port = is_ssl ? 443 : 80;

    /* Find start of path */
    const char *slash = strchr(p, '/');
    int host_len = slash ? (int)(slash - p) : (int)strlen(p);

    char host_buf[128];
    if (host_len >= (int)sizeof(host_buf)) host_len = sizeof(host_buf) - 1;
    memcpy(host_buf, p, host_len);
    host_buf[host_len] = 0;

    /* Check for explicit port :<port> */
    char *colon = strchr(host_buf, ':');
    if (colon) {
        *colon = 0;
        int pt = atoi(colon + 1);
        if (pt > 0 && pt <= 65535) *out_port = (u16)pt;
    }

    strncpy(out_host, host_buf, host_sz - 1);
    out_host[host_sz - 1] = 0;

    if (slash && slash[0]) {
        strncpy(out_path, slash, path_sz - 1);
        out_path[path_sz - 1] = 0;
    } else {
        strncpy(out_path, "/", path_sz - 1);
        out_path[path_sz - 1] = 0;
    }

    return 1;
}

const char *http_get_header(const http_response_t *res, const char *name) {
    if (!res || !name) return NULL;
    for (int i = 0; i < res->header_count; i++) {
        if (str_casecmp(res->headers[i].name, name) == 0) {
            return res->headers[i].value;
        }
    }
    return NULL;
}

int http_decode_chunked(const u8 *src, u32 src_len, u8 *dst, u32 max_dst_len) {
    if (!src || !dst || max_dst_len == 0) return 0;

    u32 src_pos = 0;
    u32 dst_pos = 0;

    while (src_pos < src_len && dst_pos < max_dst_len) {
        /* Parse hex chunk size until CRLF */
        const char *p = (const char *)src + src_pos;
        const char *crlf = strstr(p, "\r\n");
        if (!crlf) crlf = strstr(p, "\n");
        if (!crlf) break;

        char size_str[32];
        int size_len = (int)(crlf - p);
        if (size_len >= (int)sizeof(size_str)) size_len = sizeof(size_str) - 1;
        memcpy(size_str, p, size_len);
        size_str[size_len] = 0;

        /* Ignore extensions after ';' */
        char *semicolon = strchr(size_str, ';');
        if (semicolon) *semicolon = 0;
        str_trim(size_str);

        u32 chunk_sz = (u32)strtoul(size_str, NULL, 16);
        src_pos += (u32)((crlf - p) + ((crlf[0] == '\r') ? 2 : 1));

        if (chunk_sz == 0) {
            /* Final zero-length chunk reached */
            break;
        }

        if (src_pos + chunk_sz > src_len) {
            /* Partial chunk remaining */
            chunk_sz = src_len - src_pos;
        }

        u32 copy_sz = chunk_sz;
        if (dst_pos + copy_sz > max_dst_len) {
            copy_sz = max_dst_len - dst_pos;
        }

        memcpy(dst + dst_pos, src + src_pos, copy_sz);
        dst_pos += copy_sz;
        src_pos += chunk_sz;

        /* Skip trailing CRLF after chunk data */
        if (src_pos + 1 < src_len && src[src_pos] == '\r' && src[src_pos + 1] == '\n') {
            src_pos += 2;
        } else if (src_pos < src_len && (src[src_pos] == '\n' || src[src_pos] == '\r')) {
            src_pos++;
        }
    }

    return (int)dst_pos;
}

void http_response_free(http_response_t *res) {
    if (!res) return;
    if (res->body) {
        free(res->body);
        res->body = NULL;
    }
    free(res);
}

static http_status_cb_t g_http_status_cb = NULL;

void http_set_status_callback(http_status_cb_t cb) {
    g_http_status_cb = cb;
}

static void http_notify_status(const char *status, u32 cur_bytes, u32 tot_bytes) {
    if (g_http_status_cb) {
        g_http_status_cb(status, cur_bytes, tot_bytes);
    }
    gui_pump_events();
}

http_response_t *http_execute_ex(const http_request_t *req, bnet_error_t *out_err) {
    if (out_err) *out_err = BNET_OK;
    if (!req || !req->url || !req->url[0]) {
        if (out_err) *out_err = BNET_ERR_BAD_URL;
        return NULL;
    }

    char current_url[1024];
    strncpy(current_url, req->url, sizeof(current_url) - 1);
    current_url[sizeof(current_url) - 1] = 0;

    int hops = req->follow_redirects > 0 ? req->follow_redirects : 5;
    u32 request_start_tick = get_system_ticks();
    u32 request_budget_ticks = http_timeout_ticks(req);

    while (hops-- >= 0) {
        if (http_deadline_reached(request_start_tick, request_budget_ticks)) {
            serial_write("[http_v11] request deadline exceeded\n");
            http_notify_status("Request timed out", 0, 0);
            if (out_err) *out_err = BNET_ERR_HTTP_RECV_TIMEOUT;
            return NULL;
        }

        url_scheme_t scheme = url_detect_scheme(current_url);
        if (scheme == URL_SCHEME_DATA) {
            serial_write("[http_v11] data: URL detected, decoding locally without network/proxy\n");
            char mime[64] = {0};
            unsigned int max_sz = (req->max_response_size > 0) ? req->max_response_size : 65536;
            unsigned char *buf = (unsigned char *)malloc(max_sz);
            if (!buf) {
                if (out_err) *out_err = BNET_ERR_INTERNAL;
                return NULL;
            }
            unsigned int dec_len = 0;
            int ret = data_url_decode(current_url, mime, sizeof(mime), buf, max_sz, &dec_len);
            if (ret != DATA_URL_OK) {
                free(buf);
                serial_write("[http_v11] failed to decode data: URL\n");
                if (out_err) *out_err = (ret == DATA_URL_ERR_OVERFLOW) ? BNET_ERR_BODY_TRUNCATED : BNET_ERR_BAD_URL;
                return NULL;
            }
            http_response_t *res = (http_response_t *)malloc(sizeof(http_response_t));
            if (!res) {
                free(buf);
                if (out_err) *out_err = BNET_ERR_INTERNAL;
                return NULL;
            }
            memset(res, 0, sizeof(*res));
            res->status_code = 200;
            strcpy(res->status_message, "OK");
            strncpy(res->content_type, mime, sizeof(res->content_type) - 1);
            res->body = buf;
            res->body_len = dec_len;
            res->is_complete = 1;
            res->is_truncated = 0;
            if (out_err) *out_err = BNET_OK;
            return res;
        }

        if (scheme == URL_SCHEME_BLOB) {
            serial_write("[http_v11] blob scheme cleanly unsupported: ");
            serial_write(current_url);
            serial_write("\n");
            http_notify_status("Blob scheme unsupported", 0, 0);
            if (out_err) *out_err = BNET_ERR_UNSUPPORTED_SCHEME;
            return NULL;
        }

        if (!url_is_network_scheme(scheme)) {
            serial_write("[http_v11] non-network scheme in HTTP engine: ");
            serial_write(current_url);
            serial_write("\n");
            http_notify_status("Unsupported URL scheme", 0, 0);
            if (out_err) *out_err = (scheme == URL_SCHEME_UNKNOWN) ? BNET_ERR_BAD_URL : BNET_ERR_UNSUPPORTED_SCHEME;
            return NULL;
        }

        char host[128];
        char path[1024];
        u16 port = 80;
        int is_ssl = 0;

        if (!http_parse_url(current_url, host, sizeof(host), &port, path, sizeof(path), &is_ssl)) {
            serial_write("[http_v11] invalid URL: ");
            serial_write(current_url);
            serial_write("\n");
            http_notify_status("Invalid URL", 0, 0);
            if (out_err) *out_err = BNET_ERR_BAD_URL;
            return NULL;
        }

        serial_write("[http_v11] parsed scheme=");
        serial_write(is_ssl ? "https" : "http");
        serial_write(" host=");
        serial_write(host);
        serial_write(" path=");
        serial_write(path);
        serial_write("\n");

        char status_msg[128];
        snprintf(status_msg, sizeof(status_msg), "Looking up %s...", host);
        http_notify_status(status_msg, 0, 0);

        int via_https_proxy = is_ssl;
        if (via_https_proxy && !http_is_proxy_configured()) {
            serial_write("[proxy_bridge] proxy bridge not configured for HTTPS URL: ");
            serial_write(current_url);
            serial_write("\n");
            if (out_err) *out_err = BNET_ERR_PROXY_NOT_CONFIGURED;
            http_notify_status("Proxy not configured", 0, 0);
            return NULL;
        }

        const char *connect_host = via_https_proxy ? g_https_proxy_host : host;
        u16 connect_port = via_https_proxy ? g_https_proxy_port : port;
        socket_proto_t proto = SOCKET_PROTO_PLAIN_TCP;
        u32 connect_timeout = via_https_proxy ? 50000u : req->timeout_polls;

        if (via_https_proxy) {
            serial_write("[proxy_bridge] HTTPS target detected: ");
            serial_write(current_url);
            serial_write("\n");
            serial_write("[proxy_bridge] routing through host bridge at ");
            serial_write(connect_host);
            serial_write(":\n");
            snprintf(status_msg, sizeof(status_msg), "Connecting to proxy at %s:%u...", connect_host, (unsigned int)connect_port);
        } else {
            snprintf(status_msg, sizeof(status_msg), "Connecting to %s...", host);
        }
        http_notify_status(status_msg, 0, 0);

        bnet_error_t sock_err = BNET_OK;
        net_socket_t *sock = net_socket_open_ex(connect_host, connect_port, proto, connect_timeout, &sock_err);
        if (!sock && via_https_proxy) {
            bnet_error_t proxy_err = BNET_ERR_PROXY_CONNECT_FAILED;
            if (sock_err == BNET_ERR_TCP_TIMEOUT) {
                proxy_err = BNET_ERR_PROXY_TIMEOUT;
                serial_write("[proxy_bridge] connection timed out to host bridge ");
                serial_write(connect_host);
                serial_write(":\n");
            } else if (sock_err == BNET_ERR_TCP_RESET) {
                proxy_err = BNET_ERR_PROXY_CONNECT_FAILED;
                serial_write("[proxy_bridge] connection refused by host bridge (is tools/firstos_webproxy.py running on ");
                serial_write(connect_host);
                serial_write("?)\n");
            } else if (sock_err == BNET_ERR_TCP_CONNECT_FAILED) {
                proxy_err = BNET_ERR_PROXY_CONNECT_FAILED;
                serial_write("[proxy_bridge] ARP lookup failed for host bridge ");
                serial_write(connect_host);
                serial_write("\n");
            } else {
                serial_write("[proxy_bridge] failed to open socket to host bridge: ");
                serial_write(bnet_error_name(sock_err));
                serial_write("\n");
            }

            /* Attempt native TLS fallback only if direct connection is possible */
            serial_write("[http_v11] attempting native TLS fallback for ");
            serial_write(host);
            serial_write("\n");
            via_https_proxy = 0;
            connect_host = host;
            connect_port = port;
            proto = SOCKET_PROTO_TLS;
            bnet_error_t tls_sock_err = BNET_OK;
            sock = net_socket_open_ex(connect_host, connect_port, proto, req->timeout_polls, &tls_sock_err);
            if (!sock) {
                snprintf(status_msg, sizeof(status_msg), "Proxy bridge connect failed");
                http_notify_status(status_msg, 0, 0);
                if (out_err) *out_err = proxy_err;
                return NULL;
            }
        }
        if (!sock) {
            serial_write("[http_v11] failed to open socket to ");
            serial_write(connect_host);
            serial_write(" error=");
            serial_write(bnet_error_name(sock_err));
            serial_write("\n");
            snprintf(status_msg, sizeof(status_msg), "Failed to connect to %s", host);
            http_notify_status(status_msg, 0, 0);

            if (out_err) {
                *out_err = (sock_err != BNET_OK) ? sock_err : BNET_ERR_TCP_CONNECT_FAILED;
            }
            return NULL;
        }

        if (via_https_proxy) {
            serial_write("[proxy_bridge] connected to host bridge\n");
        }

        /* Build Request Buffer */
        char req_buf[2560];
        const char *method_str = (req->method == HTTP_METHOD_POST) ? "POST" :
                                 ((req->method == HTTP_METHOD_HEAD) ? "HEAD" : "GET");

        const char *wire_host = via_https_proxy ? g_https_proxy_host : host;
        const char *wire_path = via_https_proxy ? "/" : path;
        int req_len = snprintf(req_buf, sizeof(req_buf),
            "%s %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Mozilla/5.0 (X11; FirstOS x86_64; rv:109.0) FirstOS-WebBrowser/1.0\r\n"
            "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/*,*/*;q=0.8\r\n"
            "Accept-Encoding: identity\r\n"
            "Connection: close\r\n",
            method_str, wire_path, wire_host);

        if (via_https_proxy) {
            req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len,
                "X-FirstOS-Target: %s\r\n", current_url);
        }

        if (req->method == HTTP_METHOD_POST && req->post_data && req->post_data_len > 0) {
            req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len,
                "Content-Type: application/x-www-form-urlencoded\r\n"
                "Content-Length: %u\r\n", req->post_data_len);
        }

        if (req->custom_headers && req->custom_headers[0]) {
            req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len, "%s", req->custom_headers);
        }

        req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len, "\r\n");

        /* Send Request */
        snprintf(status_msg, sizeof(status_msg), "Sending request to %s...", host);
        http_notify_status(status_msg, 0, 0);

        if (via_https_proxy) {
            serial_write("[proxy_bridge] sending request with X-FirstOS-Target: ");
            serial_write(current_url);
            serial_write("\n");
        } else {
            serial_write("[http_v11] sending request (len=");
            serial_write_hex((u32)req_len);
            serial_write(")\n");
        }

        int s_hdr = net_socket_send(sock, req_buf, req_len);
        if (s_hdr <= 0) {
            if (via_https_proxy) {
                serial_write("[proxy_bridge] failed to send request headers to host bridge\n");
            } else {
                serial_write("[http_v11] failed to send HTTP request headers\n");
            }
            net_socket_close(sock);
            if (out_err) *out_err = BNET_ERR_HTTP_SEND_FAILED;
            return NULL;
        }

        if (req->method == HTTP_METHOD_POST && req->post_data && req->post_data_len > 0) {
            int s_body = net_socket_send(sock, req->post_data, (int)req->post_data_len);
            if (s_body <= 0) {
                if (via_https_proxy) {
                    serial_write("[proxy_bridge] failed to send post data to host bridge\n");
                } else {
                    serial_write("[http_v11] failed to send HTTP post data\n");
                }
                net_socket_close(sock);
                if (out_err) *out_err = BNET_ERR_HTTP_SEND_FAILED;
                return NULL;
            }
        }

        snprintf(status_msg, sizeof(status_msg), "Waiting for %s...", host);
        http_notify_status(status_msg, 0, 0);

        /* Read Response */
        u32 max_allowed = (req && req->max_response_size > 0) ? req->max_response_size : (8u * 1024u * 1024u);
        int body_truncated = 0;
        u32 initial_alloc = 131072;
        if (initial_alloc > max_allowed) initial_alloc = max_allowed;
        u8 *raw_buf = (u8 *)malloc(initial_alloc);
        if (!raw_buf) {
            net_socket_close(sock);
            http_notify_status("Memory allocation error", 0, 0);
            if (out_err) *out_err = BNET_ERR_INTERNAL;
            return NULL;
        }

        u32 total_read = 0;
        u32 expected_len = 0;
        int headers_parsed = 0;
        u32 hdr_len = 0;
        int consecutive_empty = 0;
        int first_chunk_logged = 0;

        while (!http_deadline_reached(request_start_tick, request_budget_ticks)) {
            if (total_read + 8192 > initial_alloc) {
                if (initial_alloc < max_allowed) {
                    u32 next_alloc = initial_alloc * 2;
                    if (next_alloc > max_allowed) next_alloc = max_allowed;
                    u8 *new_buf = (u8 *)realloc(raw_buf, next_alloc);
                    if (new_buf) {
                        raw_buf = new_buf;
                        initial_alloc = next_alloc;
                    } else {
                        body_truncated = 1;
                        break;
                    }
                }
            }

            u32 chunk_to_read = 8192;
            if (total_read + chunk_to_read > initial_alloc) {
                chunk_to_read = initial_alloc > total_read ? (initial_alloc - total_read) : 0;
            }
            if (chunk_to_read == 0) {
                if (headers_parsed && expected_len > 0 && (total_read >= hdr_len) && ((total_read - hdr_len) < expected_len)) {
                    body_truncated = 1;
                }
                break;
            }

            /* Use short receive slices so the total request deadline remains
             * effective even when the peer sends a response in small bursts. */
            int got = net_socket_recv(sock, raw_buf + total_read, (int)chunk_to_read, 10000u);
            if (got <= 0) {
                /* Check if peer closed socket */
                if (sock->proto == SOCKET_PROTO_PLAIN_TCP && sock->tcp_sock) {
                    if (sock->tcp_sock->rx_fin || sock->tcp_sock->state == TCP_STATE_CLOSED) {
                        break;
                    }
                }
                /* If we have Content-Length and received the full body, we're done */
                if (headers_parsed && expected_len > 0) {
                    u32 body_received = (total_read >= hdr_len) ? (total_read - hdr_len) : 0;
                    if (body_received >= expected_len) {
                        break;
                    }
                }
                if (total_read > 0) {
                    consecutive_empty++;
                    /* Allow brief network pauses between bursts (up to ~600ms = 6 empty recv slices) */
                    if (consecutive_empty >= 6) {
                        break;
                    }
                }
                continue;
            }
            consecutive_empty = 0;
            if (!first_chunk_logged && got > 0) {
                if (via_https_proxy) {
                    serial_write("[proxy_bridge] first response chunk received from host bridge\n");
                } else {
                    serial_write("[http_v11] first response chunk received\n");
                }
                first_chunk_logged = 1;
            }
            total_read += (u32)got;
            raw_buf[total_read] = 0;

            /* Check if we have received all headers */
            if (!headers_parsed) {
                const char *hdr_end = strstr((const char *)raw_buf, "\r\n\r\n");
                int delim_len = 4;
                if (!hdr_end) {
                    hdr_end = strstr((const char *)raw_buf, "\n\n");
                    delim_len = 2;
                }
                if (hdr_end) {
                    headers_parsed = 1;
                    hdr_len = (u32)(hdr_end - (const char *)raw_buf) + delim_len;
                    const char *cl = strstr((const char *)raw_buf, "Content-Length:");
                    if (!cl) cl = strstr((const char *)raw_buf, "content-length:");
                    if (cl && cl < hdr_end) {
                        expected_len = (u32)atoi(cl + 15);
                    }
                }
            }

            /* Check if entire body is received */
            if (headers_parsed) {
                u32 body_received = (total_read >= hdr_len) ? (total_read - hdr_len) : 0;
                if (expected_len > 0 && body_received >= expected_len) {
                    /* Entire response received! Break immediately without waiting for timeout */
                    break;
                }
                /* Check chunked EOF */
                if (strstr((const char *)raw_buf, "\r\n0\r\n\r\n") != NULL ||
                    (body_received >= 5 && strcmp((const char *)raw_buf + total_read - 5, "0\r\n\r\n") == 0)) {
                    break;
                }
            }

            if (expected_len > 0) {
                u32 pct = (total_read * 100) / (expected_len > 0 ? expected_len : 1);
                if (pct > 100) pct = 100;
                snprintf(status_msg, sizeof(status_msg), "Downloading: %u KB / %u KB (%u%%)...",
                         (unsigned int)(total_read / 1024), (unsigned int)(expected_len / 1024), (unsigned int)pct);
            } else {
                snprintf(status_msg, sizeof(status_msg), "Downloading: %u KB...", (unsigned int)(total_read / 1024));
            }
            http_notify_status(status_msg, total_read, expected_len);
        }
        int request_timed_out = http_deadline_reached(request_start_tick, request_budget_ticks);
        net_socket_close(sock);

        if (total_read == 0) {
            free(raw_buf);
            if (via_https_proxy) {
                serial_write(request_timed_out ?
                             "[proxy_bridge] response timed out waiting for host bridge\n" :
                             "[proxy_bridge] zero bytes received from host bridge\n");
                if (out_err) {
                    *out_err = request_timed_out ? BNET_ERR_PROXY_TIMEOUT : BNET_ERR_PROXY_BAD_RESPONSE;
                }
            } else {
                serial_write(request_timed_out ?
                             "[http_v11] response deadline exceeded\n" :
                             "[http_v11] zero bytes received from server\n");
                if (out_err) {
                    *out_err = request_timed_out ? BNET_ERR_HTTP_RECV_TIMEOUT : BNET_ERR_HTTP_BAD_RESPONSE;
                }
            }
            http_notify_status(request_timed_out ? "Request timed out" : "No data received", 0, 0);
            return NULL;
        }
        raw_buf[total_read] = 0;

        snprintf(status_msg, sizeof(status_msg), "Download complete (%u KB)", (unsigned int)(total_read / 1024));
        http_notify_status(status_msg, total_read, total_read);

        /* Split Headers and Body */
        const char *hdr_end = strstr((const char *)raw_buf, "\r\n\r\n");
        int hdr_delim_len = 4;
        if (!hdr_end) {
            hdr_end = strstr((const char *)raw_buf, "\n\n");
            hdr_delim_len = 2;
        }

        if (!hdr_end) {
            if (via_https_proxy) {
                serial_write("[proxy_bridge] rejected response without HTTP headers from host bridge\n");
                if (out_err) *out_err = BNET_ERR_PROXY_BAD_RESPONSE;
            } else {
                serial_write("[http_v11] rejected response without HTTP headers\n");
                if (out_err) *out_err = BNET_ERR_HTTP_BAD_RESPONSE;
            }
            free(raw_buf);
            return NULL;
        }

        u32 header_bytes = (u32)(hdr_end - (const char *)raw_buf);
        const u8 *body_start = (const u8 *)hdr_end + hdr_delim_len;
        u32 raw_body_len = total_read - header_bytes - hdr_delim_len;

        http_response_t *res = (http_response_t *)calloc(1, sizeof(http_response_t));
        if (!res) {
            free(raw_buf);
            if (out_err) *out_err = BNET_ERR_INTERNAL;
            return NULL;
        }

        /* Parse Status Line */
        char status_line[256];
        const char *first_crlf = strchr((const char *)raw_buf, '\n');
        int status_len = first_crlf ? (int)(first_crlf - (const char *)raw_buf) : 0;
        if (status_len >= (int)sizeof(status_line)) status_len = sizeof(status_line) - 1;
        memcpy(status_line, raw_buf, status_len);
        status_line[status_len] = 0;
        str_trim(status_line);

        char http_ver[32];
        int code = 0;
        char msg[64];
        msg[0] = 0;
        if (!http_parse_status_line(status_line, http_ver, sizeof(http_ver), &code, msg, sizeof(msg))) {
            if (via_https_proxy) {
                serial_write("[proxy_bridge] invalid status line from host bridge: ");
                serial_write(status_line);
                serial_write("\n");
                if (out_err) *out_err = BNET_ERR_PROXY_BAD_RESPONSE;
            } else {
                serial_write("[http_v11] rejected invalid HTTP status line: ");
                serial_write(status_line);
                serial_write("\n");
                if (out_err) *out_err = BNET_ERR_HTTP_BAD_RESPONSE;
            }
            free(res);
            free(raw_buf);
            return NULL;
        }
        res->status_code = code;
        strncpy(res->status_message, msg[0] ? msg : "OK", sizeof(res->status_message) - 1);

        if (via_https_proxy) {
            serial_write("[proxy_bridge] host bridge status: ");
            serial_write(status_line);
            serial_write("\n");
            if (code >= 400) {
                if (out_err) *out_err = BNET_ERR_PROXY_HTTP_ERROR;
            }
        } else {
            serial_write("[http_v11] status: ");
            serial_write(status_line);
            serial_write("\n");
        }

        /* Parse Headers */
        const char *hptr = first_crlf ? first_crlf + 1 : (const char *)raw_buf;
        while (hptr < hdr_end && res->header_count < HTTP_MAX_HEADERS) {
            const char *next_line = strchr(hptr, '\n');
            if (!next_line || next_line > hdr_end) next_line = hdr_end;

            char line[320];
            int llen = (int)(next_line - hptr);
            if (llen >= (int)sizeof(line)) llen = sizeof(line) - 1;
            memcpy(line, hptr, llen);
            line[llen] = 0;
            str_trim(line);

            char *colon = strchr(line, ':');
            if (colon) {
                *colon = 0;
                char *hval = colon + 1;
                str_trim(line);
                str_trim(hval);

                http_header_t *hdr = &res->headers[res->header_count++];
                strncpy(hdr->name, line, sizeof(hdr->name) - 1);
                strncpy(hdr->value, hval, sizeof(hdr->value) - 1);

                if (str_casecmp(hdr->name, "Content-Length") == 0) {
                    res->content_length = (u32)atoi(hdr->value);
                } else if (str_casecmp(hdr->name, "Transfer-Encoding") == 0 && strstr(hdr->value, "chunked")) {
                    res->is_chunked = 1;
                } else if (str_casecmp(hdr->name, "Content-Type") == 0) {
                    strncpy(res->content_type, hdr->value, sizeof(res->content_type) - 1);
                } else if (str_casecmp(hdr->name, "Location") == 0) {
                    strncpy(res->location, hdr->value, sizeof(res->location) - 1);
                } else if (str_casecmp(hdr->name, "Content-Encoding") == 0) {
                    strncpy(res->content_encoding, hdr->value, sizeof(res->content_encoding) - 1);
                }
            }

            if (next_line == hdr_end) break;
            hptr = next_line + 1;
        }

        /* Verify Content-Encoding: reject unsupported compression (gzip, br, etc.) */
        if (res->content_encoding[0] && str_casecmp(res->content_encoding, "identity") != 0) {
            serial_write("[http_v11] unsupported Content-Encoding: ");
            serial_write(res->content_encoding);
            serial_write(" (compression not supported, rejecting)\n");
            if (out_err) *out_err = BNET_ERR_UNSUPPORTED_ENCODING;
            http_response_free(res);
            free(raw_buf);
            return NULL;
        }

        /* Handle Redirection (301, 302, 303, 307, 308) */
        if ((res->status_code == 301 || res->status_code == 302 || res->status_code == 303 ||
             res->status_code == 307 || res->status_code == 308) && res->location[0]) {
            serial_write("[http_v11] redirect ");
            serial_write_hex((u32)res->status_code);
            serial_write(" -> ");
            serial_write(res->location);
            serial_write("\n");

            if (strncmp(res->location, "http://", 7) == 0 || strncmp(res->location, "https://", 8) == 0) {
                strncpy(current_url, res->location, sizeof(current_url) - 1);
            } else {
                /* Relative redirection */
                if (res->location[0] == '/') {
                    snprintf(current_url, sizeof(current_url), "%s://%s%s", is_ssl ? "https" : "http", host, res->location);
                } else {
                    snprintf(current_url, sizeof(current_url), "%s://%s/%s", is_ssl ? "https" : "http", host, res->location);
                }
            }
            http_response_free(res);
            free(raw_buf);
            continue; /* Loop to follow redirect */
        }

        /* Handle Body Extraction */
        if (res->is_chunked) {
            u8 *decoded_body = (u8 *)malloc(raw_body_len + 1024);
            if (decoded_body) {
                int dlen = http_decode_chunked(body_start, raw_body_len, decoded_body, raw_body_len + 1024);
                decoded_body[dlen] = 0;
                res->body = decoded_body;
                res->body_len = (u32)dlen;
            } else {
                res->body = (u8 *)malloc(raw_body_len + 1);
                if (res->body) {
                    memcpy(res->body, body_start, raw_body_len);
                    res->body[raw_body_len] = 0;
                    res->body_len = raw_body_len;
                }
            }
        } else {
            res->body = (u8 *)malloc(raw_body_len + 1);
            if (res->body) {
                memcpy(res->body, body_start, raw_body_len);
                res->body[raw_body_len] = 0;
                res->body_len = raw_body_len;
            }
        }

        /* Determine body completeness and truncation */
        if (body_truncated) {
            res->is_truncated = 1;
            res->is_complete = 0;
        } else if (res->content_length > 0 && res->body_len < res->content_length) {
            res->is_truncated = 1;
            res->is_complete = 0;
        } else {
            res->is_truncated = 0;
            res->is_complete = (res->status_code >= 200 && res->status_code < 400);
        }

        free(raw_buf);
        if (via_https_proxy) {
            serial_write("[proxy_bridge] request completed status=");
            serial_write_hex((u32)res->status_code);
            serial_write(" bytes=");
            serial_write_hex((u32)res->body_len);
            serial_write("\n");
        }
        if (out_err && (!via_https_proxy || res->status_code < 400)) *out_err = BNET_OK;
        return res;
    }

    if (out_err) *out_err = BNET_ERR_INTERNAL;
    return NULL;
}

http_response_t *http_execute(const http_request_t *req) {
    return http_execute_ex(req, NULL);
}

int http_stream(const http_request_t *req, http_stream_cb cb, void *user_data) {
    if (!req || !req->url || !cb) return 0;

    char host[128];
    char path[1024];
    u16 port = 80;
    int is_ssl = 0;

    if (!http_parse_url(req->url, host, sizeof(host), &port, path, sizeof(path), &is_ssl)) {
        return 0;
    }

    socket_proto_t proto = is_ssl ? SOCKET_PROTO_TLS : SOCKET_PROTO_PLAIN_TCP;
    net_socket_t *sock = net_socket_open(host, port, proto, req->timeout_polls);
    if (!sock) return 0;

    char req_buf[2048];
    int req_len = snprintf(req_buf, sizeof(req_buf),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: FirstOS-StreamDownloader/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n\r\n",
        path, host);

    net_socket_send(sock, req_buf, req_len);

    /* Read header preamble */
    u8 chunk[8192];
    int got = net_socket_recv(sock, chunk, sizeof(chunk) - 1, req->timeout_polls);
    if (got <= 0) {
        net_socket_close(sock);
        return 0;
    }
    chunk[got] = 0;

    const char *hdr_end = strstr((const char *)chunk, "\r\n\r\n");
    int delim_len = 4;
    if (!hdr_end) {
        hdr_end = strstr((const char *)chunk, "\n\n");
        delim_len = 2;
    }

    u32 total_expected = 0;
    const char *cl = strstr((const char *)chunk, "Content-Length:");
    if (!cl) cl = strstr((const char *)chunk, "content-length:");
    if (cl) total_expected = (u32)atoi(cl + 15);

    if (hdr_end) {
        const u8 *initial_data = (const u8 *)hdr_end + delim_len;
        u32 initial_len = (u32)(got - (initial_data - chunk));
        if (initial_len > 0) {
            if (!cb(initial_data, initial_len, total_expected, user_data)) {
                net_socket_close(sock);
                return 0;
            }
        }
    }

    /* Stream remaining data */
    u32 request_start_tick = get_system_ticks();
    u32 request_budget_ticks = http_timeout_ticks(req);
    while (!http_deadline_reached(request_start_tick, request_budget_ticks)) {
        got = net_socket_recv(sock, chunk, sizeof(chunk), 100000u);
        if (got <= 0) break;
        if (!cb(chunk, (u32)got, total_expected, user_data)) {
            net_socket_close(sock);
            return 0; /* Cancelled */
        }
    }

    net_socket_close(sock);
    return 1;
}
