#include "browser_core.h"
#include "browser_logger.h"
#include "http_v11.h"
#include "serial.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static browser_document_t g_doc;
static char g_status_msg[128] = "Ready";
static void (*g_core_status_cb)(const char *status) = NULL;

void browser_core_set_status_callback(void (*cb)(const char *status)) {
    g_core_status_cb = cb;
}

const char *browser_core_get_status_message(void) {
    return g_status_msg;
}

static void browser_core_notify_status(const char *msg) {
    if (!msg) return;
    strncpy(g_status_msg, msg, sizeof(g_status_msg) - 1);
    g_status_msg[sizeof(g_status_msg) - 1] = 0;
    if (g_core_status_cb) g_core_status_cb(g_status_msg);
}

static void http_status_forward(const char *status, u32 current_bytes, u32 total_bytes) {
    (void)current_bytes;
    (void)total_bytes;
    browser_core_notify_status(status);
}

void browser_core_init(void) {
    memset(&g_doc, 0, sizeof(g_doc));
    g_doc.state = BROWSER_CORE_IDLE;
    browser_core_notify_status("Ready");
    http_set_status_callback(http_status_forward);
}

int browser_core_begin_navigation_gen(const char *url, u32 generation) {
    if (!url || !url[0]) return 0;
    browser_log_clear();
    browser_log_info(BLOG_CAT_PAGE, "Navigating to: %s", url);
    memset(&g_doc, 0, sizeof(g_doc));
    strncpy(g_doc.url, url, sizeof(g_doc.url) - 1);
    g_doc.state = BROWSER_CORE_LOADING;
    g_doc.generation = generation;
    g_doc.error_code = BNET_OK;

    serial_write("[browser_core] navigation start: ");
    serial_write(url);
    serial_write("\n");

    char buf[128];
    sprintf(buf, "Requesting %.100s...", url);
    browser_core_notify_status(buf);
    return 1;
}

int browser_core_begin_navigation(const char *url) {
    return browser_core_begin_navigation_gen(url, 0);
}

typedef struct {
    int active;
    u32 generation;
    char current_url[512];
    char connect_host[128];
    u16 connect_port;
    socket_proto_t proto;
    int via_https_proxy;
    net_socket_t *sock;
    u8 *raw_buf;
    u32 raw_cap;
    u32 total_read;
    u32 expected_len;
    int headers_parsed;
    u32 hdr_len;
    int consecutive_empty;
    u32 start_tick;
    u32 budget_ticks;
} browser_async_ctx_t;

static browser_async_ctx_t g_async;

static void browser_core_cleanup_async(void) {
    if (g_async.sock) {
        net_socket_close(g_async.sock);
        g_async.sock = NULL;
    }
    if (g_async.raw_buf) {
        free(g_async.raw_buf);
        g_async.raw_buf = NULL;
    }
    memset(&g_async, 0, sizeof(g_async));
}

int browser_core_is_in_flight(void) {
    return (g_doc.state == BROWSER_CORE_LOADING ||
            g_doc.state == BROWSER_CORE_NAV_CONNECTING ||
            g_doc.state == BROWSER_CORE_NAV_SENDING ||
            g_doc.state == BROWSER_CORE_NAV_RECEIVING);
}

void browser_core_cancel(void) {
    browser_core_cleanup_async();
    g_doc.state = BROWSER_CORE_IDLE;
    browser_core_notify_status("Ready");
}

const browser_document_t *browser_core_document(void) {
    return &g_doc;
}

int browser_core_start_async(const char *url, u32 generation) {
    if (!url || !url[0]) return 0;
    browser_core_cleanup_async();
    browser_log_clear();
    browser_log_info(BLOG_CAT_PAGE, "Async navigating to: %s", url);

    memset(&g_doc, 0, sizeof(g_doc));
    strncpy(g_doc.url, url, sizeof(g_doc.url) - 1);
    g_doc.state = BROWSER_CORE_NAV_CONNECTING;
    g_doc.generation = generation;
    g_doc.error_code = BNET_OK;

    g_async.active = 1;
    g_async.generation = generation;
    strncpy(g_async.current_url, url, sizeof(g_async.current_url) - 1);
    g_async.start_tick = get_system_ticks();
    g_async.budget_ticks = 2000u; /* 20 seconds deadline for modern large web pages */

    serial_write("[browser_core] navigation start: ");
    serial_write(url);
    serial_write("\n");

    char buf[128];
    snprintf(buf, sizeof(buf), "Connecting to %.80s...", url);
    browser_core_notify_status(buf);
    return 1;
}

static int browser_core_step_internal(char *body, int body_max, browser_document_t *out_doc) {
    if (!g_async.active || !browser_core_is_in_flight()) {
        if (out_doc) *out_doc = g_doc;
        return (g_doc.state == BROWSER_CORE_READY) ? g_doc.body_len : 0;
    }

    if (g_doc.generation != g_async.generation) {
        /* Generation changed; abort stale navigation cleanly */
        browser_core_cleanup_async();
        return 0;
    }

    /* Check global deadline */
    if ((get_system_ticks() - g_async.start_tick) >= g_async.budget_ticks) {
        serial_write("[browser_core] async request timed out\n");
        browser_log_error(BLOG_CAT_PAGE, "Navigation timeout: deadline exceeded (%u ticks) for %s", g_async.budget_ticks, g_async.current_url);
        g_doc.state = BROWSER_CORE_ERROR;
        g_doc.error_code = BNET_ERR_HTTP_RECV_TIMEOUT;
        strncpy(g_doc.error, "Request timed out", sizeof(g_doc.error) - 1);
        browser_core_notify_status(g_doc.error);
        browser_core_cleanup_async();
        if (out_doc) *out_doc = g_doc;
        return 0;
    }

    /* --- State: CONNECTING --- */
    if (g_doc.state == BROWSER_CORE_NAV_CONNECTING) {
        char host[128], path[1024];
        u16 port = 80;
        int is_ssl = 0;
        if (!http_parse_url(g_async.current_url, host, sizeof(host), &port, path, sizeof(path), &is_ssl)) {
            browser_log_error(BLOG_CAT_PAGE, "Invalid URL scheme or format: %s", g_async.current_url);
            g_doc.state = BROWSER_CORE_ERROR;
            g_doc.error_code = BNET_ERR_BAD_URL;
            strncpy(g_doc.error, "Invalid URL", sizeof(g_doc.error) - 1);
            browser_core_notify_status(g_doc.error);
            browser_core_cleanup_async();
            if (out_doc) *out_doc = g_doc;
            return 0;
        }

        g_async.via_https_proxy = is_ssl;
        if (g_async.via_https_proxy) {
            if (!http_is_proxy_configured()) {
                browser_log_error(BLOG_CAT_PAGE, "HTTPS proxy bridge not configured for %s", g_async.current_url);
                g_doc.state = BROWSER_CORE_ERROR;
                g_doc.error_code = BNET_ERR_PROXY_NOT_CONFIGURED;
                strncpy(g_doc.error, "Proxy not configured", sizeof(g_doc.error) - 1);
                browser_core_notify_status(g_doc.error);
                browser_core_cleanup_async();
                if (out_doc) *out_doc = g_doc;
                return 0;
            }
            strncpy(g_async.connect_host, http_get_proxy_host(), sizeof(g_async.connect_host) - 1);
            g_async.connect_port = http_get_proxy_port();
            g_async.proto = SOCKET_PROTO_PLAIN_TCP;
        } else {
            strncpy(g_async.connect_host, host, sizeof(g_async.connect_host) - 1);
            g_async.connect_port = port;
            g_async.proto = SOCKET_PROTO_PLAIN_TCP;
        }

        /* Non-blocking connect attempt slice */
        bnet_error_t sock_err = BNET_OK;
        g_async.sock = net_socket_open_ex(g_async.connect_host, g_async.connect_port, g_async.proto, 80000u, &sock_err);
        if (!g_async.sock) {
            g_doc.state = BROWSER_CORE_ERROR;
            g_doc.error_code = (sock_err != BNET_OK) ? sock_err : BNET_ERR_TCP_CONNECT_FAILED;
            const char *desc = bnet_error_description(g_doc.error_code);
            strncpy(g_doc.error, desc ? desc : "Connection failed", sizeof(g_doc.error) - 1);
            browser_log_error(BLOG_CAT_PAGE, "TCP connect failed to %s:%u (%s)", g_async.connect_host, g_async.connect_port, g_doc.error);
            browser_core_notify_status(g_doc.error);
            browser_core_cleanup_async();
            if (out_doc) *out_doc = g_doc;
            return 0;
        }

        g_doc.state = BROWSER_CORE_NAV_SENDING;
        browser_core_notify_status("Sending request...");
        if (out_doc) *out_doc = g_doc;
        return 0;
    }

    /* --- State: SENDING --- */
    if (g_doc.state == BROWSER_CORE_NAV_SENDING) {
        char host[128], path[1024];
        u16 port = 80;
        int is_ssl = 0;
        http_parse_url(g_async.current_url, host, sizeof(host), &port, path, sizeof(path), &is_ssl);

        char req_buf[2048];
        const char *wire_host = g_async.via_https_proxy ? g_async.connect_host : host;
        const char *wire_path = g_async.via_https_proxy ? "/" : path;
        int req_len = snprintf(req_buf, sizeof(req_buf),
            "GET %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: Mozilla/5.0 (X11; FirstOS x86_64; rv:109.0) FirstOS-WebBrowser/1.0\r\n"
            "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/*,*/*;q=0.8\r\n"
            "Accept-Encoding: identity\r\n"
            "Connection: close\r\n",
            wire_path, wire_host);

        if (g_async.via_https_proxy) {
            req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len,
                "X-FirstOS-Target: %s\r\n", g_async.current_url);
        }
        req_len += snprintf(req_buf + req_len, sizeof(req_buf) - req_len, "\r\n");

        int sent = net_socket_send(g_async.sock, req_buf, req_len);
        if (sent <= 0) {
            browser_log_error(BLOG_CAT_PAGE, "Failed to send HTTP GET request headers");
            g_doc.state = BROWSER_CORE_ERROR;
            g_doc.error_code = BNET_ERR_HTTP_SEND_FAILED;
            strncpy(g_doc.error, "Failed to send request", sizeof(g_doc.error) - 1);
            browser_core_notify_status(g_doc.error);
            browser_core_cleanup_async();
            if (out_doc) *out_doc = g_doc;
            return 0;
        }

        /* Prepare receive buffer */
        g_async.raw_cap = 65536;
        g_async.raw_buf = (u8 *)malloc(g_async.raw_cap);
        if (!g_async.raw_buf) {
            browser_log_error(BLOG_CAT_PAGE, "OOM allocating 64KB receive buffer");
            g_doc.state = BROWSER_CORE_ERROR;
            g_doc.error_code = BNET_ERR_INTERNAL;
            strncpy(g_doc.error, "Out of memory", sizeof(g_doc.error) - 1);
            browser_core_notify_status(g_doc.error);
            browser_core_cleanup_async();
            if (out_doc) *out_doc = g_doc;
            return 0;
        }
        g_async.total_read = 0;
        g_async.consecutive_empty = 0;
        g_doc.state = BROWSER_CORE_NAV_RECEIVING;
        browser_core_notify_status("Waiting for response...");
        if (out_doc) *out_doc = g_doc;
        return 0;
    }

    /* --- State: RECEIVING --- */
    if (g_doc.state == BROWSER_CORE_NAV_RECEIVING) {
        int batch_received = 0;
        int got = 0;
        while (batch_received < 65536) {
            if (g_async.total_read + 4096 > g_async.raw_cap) {
                u32 max_cap = body_max > 0 ? (u32)body_max : 2097152u;
                if (g_async.raw_cap >= max_cap) {
                    /* Exceeded buffer */
                    browser_log_error(BLOG_CAT_PAGE, "Response exceeded maximum limit of %u bytes", max_cap);
                    g_doc.state = BROWSER_CORE_ERROR;
                    g_doc.error_code = BNET_ERR_BODY_TOO_LARGE;
                    strncpy(g_doc.error, "Response exceeded buffer limit", sizeof(g_doc.error) - 1);
                    browser_core_notify_status(g_doc.error);
                    browser_core_cleanup_async();
                    if (out_doc) *out_doc = g_doc;
                    return 0;
                }
                u32 next_cap = g_async.raw_cap * 2;
                if (next_cap > max_cap) next_cap = max_cap;
                u8 *new_buf = (u8 *)malloc(next_cap);
                if (!new_buf) {
                    browser_log_error(BLOG_CAT_PAGE, "OOM reallocating receive buffer to %u bytes", next_cap);
                    g_doc.state = BROWSER_CORE_ERROR;
                    g_doc.error_code = BNET_ERR_INTERNAL;
                    strncpy(g_doc.error, "Out of memory", sizeof(g_doc.error) - 1);
                    browser_core_notify_status(g_doc.error);
                    browser_core_cleanup_async();
                    if (out_doc) *out_doc = g_doc;
                    return 0;
                }
                if (g_async.total_read > 0 && g_async.raw_buf) {
                    memcpy(new_buf, g_async.raw_buf, g_async.total_read);
                }
                if (g_async.raw_buf) {
                    free(g_async.raw_buf);
                }
                g_async.raw_buf = new_buf;
                g_async.raw_cap = next_cap;
            }

            got = net_socket_recv(g_async.sock, g_async.raw_buf + g_async.total_read, 4096, (batch_received == 0) ? 1000u : 50u);
            if (got <= 0) break;
            g_async.total_read += (u32)got;
            batch_received += got;
            g_async.consecutive_empty = 0;
        }

        if (batch_received > 0) {
            char pbuf[64];
            snprintf(pbuf, sizeof(pbuf), "Downloading (%u KB)...", (unsigned int)(g_async.total_read / 1024));
            browser_core_notify_status(pbuf);
        } else {
            g_async.consecutive_empty++;
        }

        /* Check if connection closed or full response arrived */
        int done = 0;
        if (g_async.total_read > 0 && g_async.raw_buf) {
            g_async.raw_buf[g_async.total_read] = 0;
            const char *hdr_end = strstr((const char *)g_async.raw_buf, "\r\n\r\n");
            int delim_len = 4;
            if (!hdr_end) {
                hdr_end = strstr((const char *)g_async.raw_buf, "\n\n");
                delim_len = 2;
            }
            if (hdr_end) {
                u32 hdr_len = (u32)(hdr_end - (const char *)g_async.raw_buf) + (u32)delim_len;
                const char *cl = strstr((const char *)g_async.raw_buf, "Content-Length:");
                if (!cl) cl = strstr((const char *)g_async.raw_buf, "content-length:");
                if (cl && cl < hdr_end) {
                    u32 expected_len = (u32)atoi(cl + 15);
                    if (expected_len > 0 && (g_async.total_read >= hdr_len + expected_len)) {
                        done = 1;
                    }
                }
            }
        }
        if (!done && got <= 0) {
            if (g_async.sock->proto == SOCKET_PROTO_PLAIN_TCP && g_async.sock->tcp_sock) {
                if (g_async.sock->tcp_sock->rx_fin || g_async.sock->tcp_sock->state == TCP_STATE_CLOSED) {
                    done = 1;
                }
            }
            if (g_async.total_read > 0 && g_async.consecutive_empty >= 80) {
                done = 1;
            }
        }

        if (!done) {
            return 0; /* Keep receiving on next tick */
        }

        /* Response completed! Process headers and body */
        if (g_async.total_read == 0) {
            browser_log_error(BLOG_CAT_PAGE, "Server closed connection with zero bytes received");
            g_doc.state = BROWSER_CORE_ERROR;
            g_doc.error_code = BNET_ERR_HTTP_BAD_RESPONSE;
            strncpy(g_doc.error, "Zero bytes received", sizeof(g_doc.error) - 1);
            browser_core_notify_status(g_doc.error);
            browser_core_cleanup_async();
            if (out_doc) *out_doc = g_doc;
            return 0;
        }

        g_async.raw_buf[g_async.total_read] = 0;
        const char *hdr_end = strstr((const char *)g_async.raw_buf, "\r\n\r\n");
        int hdr_delim = 4;
        if (!hdr_end) {
            hdr_end = strstr((const char *)g_async.raw_buf, "\n\n");
            hdr_delim = 2;
        }

        if (!hdr_end) {
            serial_write("[browser_core] malformed response total_read=");
            serial_write_hex(g_async.total_read);
            serial_write(" prefix: ");
            char prev[128];
            int pl = g_async.total_read < 120 ? (int)g_async.total_read : 120;
            memcpy(prev, g_async.raw_buf, pl);
            prev[pl] = 0;
            serial_write(prev);
            serial_write("\n");
            browser_log_error(BLOG_CAT_PAGE, "Malformed HTTP response (no header terminator \\r\\n\\r\\n found)");
            g_doc.state = BROWSER_CORE_ERROR;
            g_doc.error_code = BNET_ERR_HTTP_BAD_RESPONSE;
            strncpy(g_doc.error, "Malformed HTTP response", sizeof(g_doc.error) - 1);
            browser_core_notify_status(g_doc.error);
            browser_core_cleanup_async();
            if (out_doc) *out_doc = g_doc;
            return 0;
        }

        u32 hdr_len = (u32)(hdr_end - (const char *)g_async.raw_buf);
        const u8 *body_start = (const u8 *)hdr_end + hdr_delim;
        u32 raw_body_len = g_async.total_read - hdr_len - (u32)hdr_delim;

        /* Parse status line */
        int code = 200;
        if (strncmp((const char *)g_async.raw_buf, "HTTP/", 5) == 0) {
            const char *sp = strchr((const char *)g_async.raw_buf, ' ');
            if (sp) code = atoi(sp + 1);
        }
        g_doc.status_code = code;

        serial_write("[browser_core] headers parsed\n");

        if (code < 200 || code >= 400) {
            browser_log_error(BLOG_CAT_PAGE, "HTTP response status %d", code);
            g_doc.state = BROWSER_CORE_ERROR;
            g_doc.error_code = BNET_ERR_HTTP_BAD_RESPONSE;
            snprintf(g_doc.error, sizeof(g_doc.error), "HTTP Error %d", code);
            browser_core_notify_status(g_doc.error);
            browser_core_cleanup_async();
            if (out_doc) *out_doc = g_doc;
            return 0;
        }

        /* Check for Transfer-Encoding: chunked */
        int is_chunked = 0;
        const char *hdr_ptr = (const char *)g_async.raw_buf;
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
        if (body && body_max > 0) {
            if (is_chunked) {
                int decoded = http_decode_chunked(body_start, raw_body_len, (u8 *)body, (u32)body_max - 1);
                if (decoded < 0) decoded = 0;
                copy_len = (u32)decoded;
                body[copy_len] = 0;
            } else {
                copy_len = raw_body_len;
                if (copy_len >= (u32)body_max) copy_len = (u32)body_max - 1;
                memcpy(body, body_start, copy_len);
                body[copy_len] = 0;
            }
        }

        if (copy_len == 0) {
            browser_log_error(BLOG_CAT_PAGE, "Empty response body received from server");
            g_doc.state = BROWSER_CORE_ERROR;
            g_doc.error_code = BNET_ERR_HTTP_BAD_RESPONSE;
            strncpy(g_doc.error, "Empty response body", sizeof(g_doc.error) - 1);
            browser_core_notify_status(g_doc.error);
            browser_core_cleanup_async();
            if (out_doc) *out_doc = g_doc;
            return 0;
        }

        g_doc.body_len = (int)copy_len;
        g_doc.error_code = BNET_OK;
        g_doc.state = BROWSER_CORE_READY;
        browser_core_notify_status("Download complete");

        serial_write("[browser_core] async navigation complete: ");
        serial_write(g_doc.url);
        serial_write("\n");
        browser_log_info(BLOG_CAT_PAGE, "Page load complete (%u bytes, HTTP %d)", copy_len, g_doc.status_code);

        browser_core_cleanup_async();
        if (out_doc) *out_doc = g_doc;
        return (int)copy_len;
    }

    return 0;
}

int browser_core_step(char *body, int body_max, browser_document_t *out_doc) {
    static int s_in_step = 0;
    if (s_in_step) return 0;
    s_in_step = 1;
    int ret = browser_core_step_internal(body, body_max, out_doc);
    s_in_step = 0;
    return ret;
}

int browser_core_fetch_gen(char *body, int body_max, browser_document_t *out_doc, u32 expected_generation) {
    if (!body || body_max <= 1 || g_doc.state != BROWSER_CORE_LOADING) return 0;

    http_request_t req;
    memset(&req, 0, sizeof(req));
    req.url = g_doc.url;
    req.method = HTTP_METHOD_GET;
    req.follow_redirects = 1;
    /* Keep navigation bounded because this function is called from the GUI
     * tick.  HTTP enforces the same value as a total response deadline. */
    req.timeout_polls = 100000;

    serial_write("[browser_core] GET ");
    serial_write(g_doc.url);
    serial_write("\n");

    req.max_response_size = (u32)body_max;

    bnet_error_t net_err = BNET_OK;
    http_response_t *res = http_execute_ex(&req, &net_err);
    if (g_doc.state != BROWSER_CORE_LOADING || (expected_generation != 0 && g_doc.generation != expected_generation)) {
        if (res) http_response_free(res);
        return 0;
    }
    if (!res) {
        if (expected_generation != 0 && g_doc.generation != expected_generation) return 0;
        g_doc.state = BROWSER_CORE_ERROR;
        g_doc.error_code = net_err;
        const char *desc = bnet_error_description(net_err);
        strncpy(g_doc.error, desc ? desc : "Network or TLS connection failed", sizeof(g_doc.error) - 1);
        g_doc.error[sizeof(g_doc.error) - 1] = 0;
        browser_core_notify_status(g_doc.error);

        browser_log_error(BLOG_CAT_PAGE, "Fetch failed: %s - %s", bnet_error_name(net_err), g_doc.error);
        serial_write("[browser_core] fetch failed: ");
        serial_write(bnet_error_name(net_err));
        serial_write(" - ");
        serial_write(g_doc.error);
        serial_write("\n");

        if (out_doc) *out_doc = g_doc;
        return 0;
    }

    if (expected_generation != 0 && g_doc.generation != expected_generation) {
        http_response_free(res);
        return 0;
    }

    g_doc.status_code = res->status_code;
    strncpy(g_doc.mime, res->content_type, sizeof(g_doc.mime) - 1);
    if (res->status_code < 200 || res->status_code >= 400 || !res->body) {
        if (expected_generation != 0 && g_doc.generation != expected_generation) {
            http_response_free(res);
            return 0;
        }
        g_doc.state = BROWSER_CORE_ERROR;
        g_doc.error_code = (net_err != BNET_OK) ? net_err : ((res->status_code == 0) ? BNET_ERR_HTTP_BAD_RESPONSE : BNET_OK);
        if (net_err == BNET_ERR_PROXY_HTTP_ERROR) {
            if (res->body && res->body_len > 0) {
                char err_detail[80];
                int dlen = (int)res->body_len;
                if (dlen >= (int)sizeof(err_detail)) dlen = sizeof(err_detail) - 1;
                memcpy(err_detail, res->body, dlen);
                err_detail[dlen] = 0;
                char *nl = strchr(err_detail, '\n');
                if (nl) *nl = 0;
                char *cr = strchr(err_detail, '\r');
                if (cr) *cr = 0;
                snprintf(g_doc.error, sizeof(g_doc.error), "Proxy bridge (%d): %.60s", res->status_code, err_detail);
            } else {
                snprintf(g_doc.error, sizeof(g_doc.error), "Proxy bridge upstream fetch failed (HTTP %d %s)",
                         res->status_code, res->status_message[0] ? res->status_message : "Error");
            }
        } else {
            snprintf(g_doc.error, sizeof(g_doc.error), "HTTP %d %s", res->status_code, res->status_message[0] ? res->status_message : "Error");
        }
        browser_core_notify_status(g_doc.error);

        browser_log_error(BLOG_CAT_PAGE, "HTTP fetch error: %s", g_doc.error);
        serial_write("[browser_core] HTTP error: ");
        serial_write(g_doc.error);
        serial_write("\n");

        http_response_free(res);
        if (out_doc) *out_doc = g_doc;
        return 0;
    }

    if (res->is_truncated || !res->is_complete) {
        if (expected_generation != 0 && g_doc.generation != expected_generation) {
            http_response_free(res);
            return 0;
        }
        g_doc.state = BROWSER_CORE_ERROR;
        g_doc.error_code = BNET_ERR_BODY_TRUNCATED;
        strncpy(g_doc.error, "Page incomplete or exceeded buffer limit", sizeof(g_doc.error) - 1);
        g_doc.error[sizeof(g_doc.error) - 1] = 0;
        browser_core_notify_status(g_doc.error);

        browser_log_error(BLOG_CAT_PAGE, "Body truncated or incomplete (%u bytes)", res->body_len);
        serial_write("[browser_core] fetch failed: ");
        serial_write(bnet_error_name(BNET_ERR_BODY_TRUNCATED));
        serial_write(" - ");
        serial_write(g_doc.error);
        serial_write("\n");

        http_response_free(res);
        if (out_doc) *out_doc = g_doc;
        return 0;
    }

    if (expected_generation != 0 && g_doc.generation != expected_generation) {
        http_response_free(res);
        return 0;
    }

    u32 copy_len = res->body_len;
    if (copy_len >= (u32)body_max) copy_len = (u32)body_max - 1;
    memcpy(body, res->body, copy_len);
    body[copy_len] = 0;
    g_doc.body_len = (int)copy_len;
    g_doc.error_code = BNET_OK;
    g_doc.state = BROWSER_CORE_READY;
    browser_core_notify_status("Download complete");

    serial_write("[browser_core] navigation complete: ");
    serial_write(g_doc.url);
    serial_write("\n");
    browser_log_info(BLOG_CAT_PAGE, "Page load complete (%u bytes, HTTP %d)", copy_len, g_doc.status_code);

    http_response_free(res);
    if (out_doc) *out_doc = g_doc;
    return (int)copy_len;
}

int browser_core_fetch(char *body, int body_max, browser_document_t *out_doc) {
    return browser_core_fetch_gen(body, body_max, out_doc, 0);
}
