#include "http_client.h"
#include "data_url.h"
#include "http_v11.h"
#include "tcp.h"
#include "tls.h"
#include "cookies.h"
#include "dns.h"
#include "eth.h"
#include "serial.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static char http_host[128];
static char http_path[256];
static char http_req[1536];
static char http_resp[131072];

int http_get_body(const char *url, http_body_t *out_body, unsigned int max_capacity) {
    if (!out_body) return 0;
    http_body_init(out_body, 4096);
    out_body->max_capacity = max_capacity;
    if (!url || !url[0]) return 0;

    url_scheme_t scheme = url_detect_scheme(url);
    strncpy(out_body->scheme, url_scheme_name(scheme), sizeof(out_body->scheme) - 1);

    if (scheme == URL_SCHEME_DATA) {
        char mime[64] = {0};
        unsigned char *tmp = (unsigned char *)malloc(max_capacity);
        if (!tmp) {
            out_body->state = RESOURCE_STATE_OOM;
            out_body->complete = 0;
            out_body->truncated = 1;
            http_body_log(url, out_body, "DATA_URL_OOM");
            return 0;
        }
        unsigned int decoded_len = 0;
        int dres = data_url_decode(url, mime, sizeof(mime), tmp, max_capacity, &decoded_len);
        if (dres == DATA_URL_OK) {
            http_body_append(out_body, tmp, decoded_len, max_capacity);
            http_body_finalize(out_body, 200, mime, "identity", decoded_len, 1);
            out_body->state = RESOURCE_STATE_LOADED;
            http_body_log(url, out_body, "DATA_URL_LOADED");
            free(tmp);
            return (int)out_body->length;
        } else {
            free(tmp);
            out_body->state = (dres == DATA_URL_ERR_OVERFLOW) ? RESOURCE_STATE_TRUNCATED : RESOURCE_STATE_PARSE_ERROR;
            out_body->complete = 0;
            out_body->truncated = 1;
            http_body_log(url, out_body, "DATA_URL_DECODE_FAILED");
            return 0;
        }
    }

    if (scheme == URL_SCHEME_BLOB) {
        out_body->state = RESOURCE_STATE_HTTP_ERROR;
        out_body->complete = 0;
        out_body->truncated = 1;
        serial_write("[http_client] blob: scheme unsupported cleanly\n");
        http_body_log(url, out_body, "UNSUPPORTED_BLOB_SCHEME");
        return 0;
    }

    if (!url_is_network_scheme(scheme)) {
        out_body->state = RESOURCE_STATE_HTTP_ERROR;
        out_body->complete = 0;
        out_body->truncated = 1;
        serial_write("[http_client] unsupported scheme: ");
        serial_write(url);
        serial_write("\n");
        http_body_log(url, out_body, "UNSUPPORTED_SCHEME");
        return 0;
    }

    http_request_t req;
    memset(&req, 0, sizeof(req));
    req.url = url;
    req.method = HTTP_METHOD_GET;
    req.follow_redirects = 1;
    req.timeout_polls = 60000;
    req.max_response_size = max_capacity;

    serial_write("[http_client] GET: ");
    serial_write(url);
    serial_write("\n");

    bnet_error_t net_err = BNET_OK;
    http_response_t *res = http_execute_ex(&req, &net_err);
    if (!res || !res->body || res->status_code < 200 || res->status_code >= 400) {
        serial_write("[http_client] fetch failed: ");
        serial_write(bnet_error_name(net_err));
        serial_write("\n");
        if (res) {
            http_body_finalize(out_body, res->status_code, res->content_type, res->content_encoding, res->content_length, (res->content_length > 0));
            http_response_free(res);
        } else {
            http_body_finalize(out_body, 0, "", "", 0, 0);
        }

        if (net_err == BNET_ERR_PROXY_CONNECT_FAILED || net_err == BNET_ERR_PROXY_HTTP_ERROR ||
            net_err == BNET_ERR_HTTPS_BRIDGE_UNAVAILABLE || net_err == BNET_ERR_PROXY_TIMEOUT ||
            net_err == BNET_ERR_PROXY_NOT_CONFIGURED || net_err == BNET_ERR_PROXY_BAD_RESPONSE) {
            out_body->state = RESOURCE_STATE_PROXY_ERROR;
        } else if (net_err == BNET_ERR_UNSUPPORTED_ENCODING) {
            out_body->state = RESOURCE_STATE_UNSUPPORTED_ENCODING;
        } else if (out_body->http_status >= 400) {
            out_body->state = RESOURCE_STATE_HTTP_ERROR;
        } else {
            out_body->state = RESOURCE_STATE_INCOMPLETE;
        }

        out_body->complete = 0;
        out_body->truncated = 1;
        http_body_log(url, out_body, "FETCH_FAILED");
        return 0;
    }

    u32 hdr_bytes = 0;
    for (int i = 0; i < res->header_count; i++) {
        hdr_bytes += (u32)(strlen(res->headers[i].name) + strlen(res->headers[i].value) + 4);
    }
    serial_write("[http_client] response headers bytes=");
    serial_write_hex(hdr_bytes);
    serial_write("\n");

    /* Append response body with max_capacity enforcement */
    http_body_append(out_body, res->body, res->body_len, max_capacity);
    http_body_finalize(out_body, res->status_code, res->content_type, res->content_encoding, res->content_length, (res->content_length > 0));

    if (res->is_truncated) {
        out_body->truncated = 1;
        out_body->complete = 0;
        out_body->state = RESOURCE_STATE_TRUNCATED;
    }

    serial_write("[http_client] content_length=");
    serial_write_hex(res->content_length);
    serial_write("\n");

    serial_write("[http_client] body_received=");
    serial_write_hex(out_body->length);
    serial_write("\n");

    serial_write("[http_client] body_capacity=");
    serial_write_hex(max_capacity);
    serial_write("\n");

    serial_write("[http_client] body_complete=");
    serial_write((out_body->complete && !out_body->truncated) ? "1\n" : "0\n");

    serial_write("[http_client] body_truncated=");
    serial_write(out_body->truncated ? "1\n" : "0\n");

    http_response_free(res);
    http_body_log(url, out_body, (out_body->complete && !out_body->truncated) ? "LOADED" : "INCOMPLETE");
    return (int)out_body->length;
}

int http_get_binary_ex(const char *url, unsigned char *out_buf, int max_len, int *out_is_truncated, unsigned int *out_actual_len) {
    if (out_is_truncated) *out_is_truncated = 0;
    if (out_actual_len) *out_actual_len = 0;
    if (!url || !out_buf || max_len <= 0) return 0;

    http_body_t body;
    int len = http_get_body(url, &body, (unsigned int)max_len);
    (void)len;

    if (out_is_truncated) *out_is_truncated = body.truncated;
    if (out_actual_len) *out_actual_len = body.has_expected_length ? body.expected_length : body.length;

    int copy_len = 0;
    /* Strictly refuse to return truncated, incomplete, or HTTP error payloads */
    if (body.data && body.length > 0 && body.complete && !body.truncated && body.http_status >= 200 && body.http_status < 300) {
        copy_len = (int)body.length;
        if (copy_len > max_len) copy_len = max_len;
        memcpy(out_buf, body.data, (size_t)copy_len);
    }
    http_body_free(&body);

    serial_write("[http_client] binary loaded bytes=");
    serial_write_hex((u32)copy_len);
    serial_write("\n");
    return copy_len;
}

int http_get_binary(const char *url, unsigned char *out_buf, int max_len) {
    return http_get_binary_ex(url, out_buf, max_len, NULL, NULL);
}

/* Parse dotted-decimal IP string into u32 (host byte order) */
static u32 parse_ip(const char *s) {
    u32 ip = 0;
    int i;
    for (i = 0; i < 4; i++) {
        u32 oct = 0;
        while (*s >= '0' && *s <= '9') oct = oct*10 + (u32)(*s++ - '0');
        ip = (ip << 8) | (oct & 0xFF);
        if (*s == '.') s++;
    }
    return ip;
}

/* Find substring, returns pointer or NULL */
static const char *find_str(const char *hay, int hlen, const char *needle) {
    int nlen = (int)strlen(needle);
    int i;
    for (i = 0; i <= hlen - nlen; i++) {
        if (memcmp(hay + i, needle, (u32)nlen) == 0) return hay + i;
    }
    return 0;
}

/* Case-insensitive substring search */
static const char *find_str_ci(const char *hay, int hlen, const char *needle) {
    int nlen = (int)strlen(needle);
    int i, j;
    for (i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (j = 0; j < nlen; j++) {
            char a = hay[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = 0; break; }
        }
        if (match) return hay + i;
    }
    return 0;
}

int http_get(const char *url, char *out_buf, int max_len) {
    if (!url || !out_buf || max_len < 32) return 0;

    char current_url[256];
    strncpy(current_url, url, sizeof(current_url) - 1);
    current_url[sizeof(current_url) - 1] = 0;

    int hops = 0;
    int fallback_attempted = 0;
    while (hops < 5) {
        hops++;

        int is_https = 0;
        const char *host_start = NULL;

        if (strncmp(current_url, "https://", 8) == 0) {
            is_https = 1;
            host_start = current_url + 8;
        } else if (strncmp(current_url, "http://", 7) == 0) {
            is_https = 0;
            host_start = current_url + 7;
        } else {
            return 0;
        }

        const char *slash = strchr(host_start, '/');
        u16 port = is_https ? 443 : 80;

        if (slash) {
            int hlen = (int)(slash - host_start);
            if (hlen >= 127) hlen = 127;
            memcpy(http_host, host_start, (u32)hlen); http_host[hlen] = 0;
            strncpy(http_path, slash, 255); http_path[255] = 0;
        } else {
            strncpy(http_host, host_start, 127); http_host[127] = 0;
            http_path[0] = '/'; http_path[1] = 0;
        }

        /* Check for custom port in host e.g. example.com:8080 */
        char *colon = strchr(http_host, ':');
        if (colon) {
            port = (u16)atoi(colon + 1);
            if (!port) port = is_https ? 443 : 80;
            *colon = 0;
        }

        /* Resolve hostname */
        extern int net_resolve_host(const char *host, char *out_ip, int max_len);
        char ip_str[20];
        if (!net_resolve_host(http_host, ip_str, sizeof(ip_str))) {
            serial_write("[http] dns fail for "); serial_write(http_host); serial_write("\n");
            return 0;
        }
        u32 dst_ip = parse_ip(ip_str);
        if (!dst_ip) return 0;

        serial_write(is_https ? "[https] connecting to " : "[http] connecting to ");
        serial_write(http_host);
        serial_write(" ("); serial_write(ip_str); serial_write(")\n");

        tcp_socket_t *sock = NULL;
        tls_session_t *tls_sess = NULL;

        if (is_https) {
            tls_sess = tls_connect(dst_ip, port, http_host, 800000);
            if (!tls_sess) {
                serial_write("[https] TLS connection failed\n");
                /* Some legacy endpoints still serve HTTP.  Retry once over
                   port 80 so a TLS failure does not make every page fail
                   when the site has a usable plaintext endpoint. */
                if (!fallback_attempted) {
                    fallback_attempted = 1;
                    snprintf(current_url, sizeof(current_url), "http://%s%s", http_host, http_path);
                    serial_write("[http] retrying failed HTTPS request over HTTP\n");
                    continue;
                }
                return 0;
            }
        } else {
            sock = tcp_connect(dst_ip, port, 600000);
            if (!sock) {
                serial_write("[http] TCP connect failed\n");
                return 0;
            }
        }

        /* Build HTTP/1.0 request with Host, Engine-specific User-Agent, Accept, and Cookie headers */
        char cookie_hdr[256];
        cookies_build_header(http_host, http_path, is_https, cookie_hdr, sizeof(cookie_hdr));

        const char *ua = "FirstOS/2.0 (i686-pc-none; rv:1.0)";
        const char *sec_hdrs = "";

        int reqlen = snprintf(http_req, sizeof(http_req),
            "GET %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "User-Agent: %s\r\n"
            "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
            "%s%s"
            "Connection: close\r\n\r\n",
            http_path, http_host, ua, sec_hdrs, cookie_hdr);

        if (reqlen >= (int)sizeof(http_req)) reqlen = sizeof(http_req) - 1;

        /* Send Request */
        if (is_https) {
            tls_send(tls_sess, http_req, (u16)reqlen);
        } else {
            tcp_send(sock, http_req, (u16)reqlen);
        }

        /* Receive Response */
        int total = 0;
        while (total < (int)sizeof(http_resp) - 1) {
            int got = 0;
            int rem = (int)sizeof(http_resp) - 1 - total;
            u16 chunk_len = (rem > 16384) ? 16384 : (u16)rem;
            if (is_https) {
                got = tls_recv(tls_sess, http_resp + total, chunk_len, 500000);
                if (got <= 0) break;
            } else {
                got = tcp_recv(sock, http_resp + total, chunk_len, 500000);
                if (got <= 0) break;
                if (sock->rx_fin || sock->state != TCP_STATE_ESTABLISHED) {
                    total += got;
                    break;
                }
            }
            total += got;
        }

        if (is_https) {
            tls_close(tls_sess);
        } else {
            tcp_close(sock);
        }
        http_resp[total] = 0;

        serial_write("[http] received "); serial_write_hex((u32)total); serial_write(" bytes\n");

        /* If HTTPS failed or received TLS alert (non-HTTP), attempt automatic HTTP port 80 fallback once */
        if (is_https && !fallback_attempted && (total < 12 || memcmp(http_resp, "HTTP/", 5) != 0)) {
            fallback_attempted = 1;
            serial_write("[http] HTTPS TLS failed, falling back to HTTP port 80 for ");
            serial_write(http_host);
            serial_write("\n");

            snprintf(current_url, sizeof(current_url), "http://%s%s", http_host, http_path);
            continue;
        }

        if (total < 12 || memcmp(http_resp, "HTTP/", 5) != 0) {
            serial_write("[http] Invalid non-HTTP response received\n");
            return 0;
        }

        /* Check HTTP status code */
        int status_code = 0;
        const char *sp1 = strchr(http_resp, ' ');
        if (sp1 && sp1 - http_resp < 16) {
            status_code = atoi(sp1 + 1);
        }

        /* Parse any Set-Cookie headers in the response */
        const char *hdr_end = find_str(http_resp, total, "\r\n\r\n");
        if (!hdr_end) {
            serial_write("[http] Missing HTTP header terminator\n");
            return 0;
        }
        int hdr_len = (int)(hdr_end - http_resp);

        const char *ck_scan = http_resp;
        while (ck_scan < http_resp + hdr_len) {
            const char *set_ck = find_str_ci(ck_scan, (int)(http_resp + hdr_len - ck_scan), "set-cookie:");
            if (!set_ck) break;
            set_ck += 11;
            while (*set_ck == ' ' || *set_ck == '\t') set_ck++;

            char cookie_val[256];
            int ci = 0;
            while (*set_ck && *set_ck != '\r' && *set_ck != '\n' && ci < 255) {
                cookie_val[ci++] = *set_ck++;
            }
            cookie_val[ci] = 0;
            if (cookie_val[0]) {
                cookies_parse_set_cookie(cookie_val, http_host, http_path, is_https);
            }
            ck_scan = set_ck;
        }

        /* Check for redirect 301, 302, 303, 307, 308 */
        if (status_code >= 301 && status_code <= 308) {
            const char *loc = find_str_ci(http_resp, hdr_len, "location:");
            if (loc) {
                loc += 9;
                while (*loc == ' ' || *loc == '\t') loc++;
                char target[256];
                int ti = 0;
                while (*loc && *loc != '\r' && *loc != '\n' && ti < 255) {
                    target[ti++] = *loc++;
                }
                target[ti] = 0;
                serial_write("[http] redirect to: "); serial_write(target); serial_write("\n");

                if (strncmp(target, "http://", 7) == 0 || strncmp(target, "https://", 8) == 0) {
                    strncpy(current_url, target, sizeof(current_url) - 1);
                    current_url[sizeof(current_url) - 1] = 0;
                    continue;
                } else if (target[0] == '/') {
                    snprintf(current_url, sizeof(current_url), "%s://%s%s",
                             is_https ? "https" : "http", http_host, target);
                    continue;
                }
            }
        }

        /* Strip HTTP headers — find \r\n\r\n */
        const char *body = hdr_end + 4;
        int body_len = total - (int)(body - http_resp);

        if (body_len <= 0) {
            if (status_code >= 200 && status_code < 300) {
                int l = snprintf(out_buf, max_len,
                    "<html><body style=\"background:#0F172A;color:#F8FAFC;font-family:sans-serif;padding:32px;\">"
                    "<div style=\"max-width:540px;margin:0 auto;background:#1E293B;padding:24px;border:1px solid #334155;\">"
                    "<h2 style=\"color:#10B981;margin-top:0;\">HTTP %d OK</h2>"
                    "<p style=\"color:#94A3B8;\">Connected to %s, received 0-byte payload.</p>"
                    "<p><a href=\"firstos://home\" style=\"color:#38BDF8;\">&larr; Back to Home</a></p>"
                    "</div></body></html>", status_code, http_host);
                if (l >= max_len) l = max_len - 1;
                return l;
            }
            return 0;
        }

        /* Check if Transfer-Encoding: chunked */
        const char *te = find_str_ci(http_resp, hdr_len, "transfer-encoding: chunked");
        if (te) {
            int out_idx = 0;
            const char *p = body;
            const char *body_end = http_resp + total;
            while (p < body_end && out_idx < max_len - 1) {
                int chunk_size = 0;
                while (p < body_end && (*p == ' ' || *p == '\r' || *p == '\n')) p++;
                if (p >= body_end) break;
                while (p < body_end) {
                    char ch = *p;
                    if (ch >= '0' && ch <= '9') chunk_size = (chunk_size << 4) | (ch - '0');
                    else if (ch >= 'a' && ch <= 'f') chunk_size = (chunk_size << 4) | (ch - 'a' + 10);
                    else if (ch >= 'A' && ch <= 'F') chunk_size = (chunk_size << 4) | (ch - 'A' + 10);
                    else break;
                    p++;
                }
                while (p < body_end && *p != '\n') p++;
                if (p < body_end && *p == '\n') p++;

                if (chunk_size <= 0) break;

                int copy_len = chunk_size;
                if (p + copy_len > body_end) copy_len = (int)(body_end - p);
                if (out_idx + copy_len >= max_len) copy_len = max_len - 1 - out_idx;
                if (copy_len > 0) {
                    memcpy(out_buf + out_idx, p, (u32)copy_len);
                    out_idx += copy_len;
                    p += copy_len;
                }
                while (p < body_end && (*p == '\r' || *p == '\n')) p++;
            }
            out_buf[out_idx] = 0;
            return out_idx;
        }

        if (body_len >= max_len) body_len = max_len - 1;
        memcpy(out_buf, body, (u32)body_len);
        out_buf[body_len] = 0;
        return body_len;
    }
    return 0;
}
