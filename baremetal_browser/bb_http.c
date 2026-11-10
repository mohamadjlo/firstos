#include "bb_http.h"

static const char *s_offline_demo_page = 
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "<style>\n"
    "  body { font-size: 16px; color: #202124; background-color: #f8f9fa; margin: 16px; }\n"
    "  .card { background-color: #ffffff; border: 1px solid #dadce0; border-radius: 8px; padding: 20px; margin-bottom: 16px; }\n"
    "  .header-card { background-color: #1a73e8; color: #ffffff; border-radius: 8px; padding: 24px; margin-bottom: 20px; }\n"
    "  .header-card h1 { color: #ffffff; margin-top: 0; font-size: 28px; }\n"
    "  .badge { background-color: #e8f0fe; color: #1967d2; padding: 4px 10px; border-radius: 4px; font-size: 14px; font-weight: bold; }\n"
    "  .success-badge { background-color: #ceead6; color: #0d652d; padding: 4px 10px; border-radius: 4px; font-weight: bold; }\n"
    "  .btn-primary { background-color: #1a73e8; color: #ffffff; border: 1px solid #1557b0; border-radius: 4px; padding: 8px 16px; }\n"
    "  .btn-secondary { background-color: #ffffff; color: #3c4043; border: 1px solid #dadce0; border-radius: 4px; padding: 8px 16px; }\n"
    "  code { background-color: #f1f3f4; color: #d93025; padding: 2px 6px; border-radius: 4px; }\n"
    "  pre { background-color: #202124; color: #34a853; padding: 12px; border-radius: 6px; }\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "  <div class=\"header-card\">\n"
    "    <h1>FirstOS Bare-Metal Web Browser</h1>\n"
    "    <p>Zero Libc &bull; Zero POSIX &bull; Standalone 32-bpp Software Rasterizer &bull; Custom OSAL</p>\n"
    "  </div>\n"
    "\n"
    "  <div class=\"card\">\n"
    "    <h2>System Architecture Status</h2>\n"
    "    <p>\n"
    "      <span class=\"success-badge\">ACTIVE</span> <b>32-bpp ARGB Linear Rasterizer:</b> Porter-Duff alpha blending, scanline clipping stack.<br>\n"
    "      <span class=\"success-badge\">ACTIVE</span> <b>Bitmap Typography:</b> 8x16 VGA font with synthetic bold and shear italics.<br>\n"
    "      <span class=\"success-badge\">ACTIVE</span> <b>HTML5 DOM Engine:</b> State-machine lexer with open-element stack.<br>\n"
    "      <span class=\"success-badge\">ACTIVE</span> <b>CSS3 Cascade Resolver:</b> Specificity calculator with User-Agent stylesheet.<br>\n"
    "      <span class=\"success-badge\">ACTIVE</span> <b>Box-Model Layout:</b> Block vertical flow, inline formatting, word-wrapping.<br>\n"
    "      <span class=\"badge\">NETWORK</span> <b>HTTP/1.1 Socket Client:</b> FirstOS TCP/IP and chunked streaming support.\n"
    "    </p>\n"
    "  </div>\n"
    "\n"
    "  <div class=\"card\">\n"
    "    <h2>Interactive Action Tests</h2>\n"
    "    <p>Test clicking buttons and hyperlinks below:</p>\n"
    "    <p>\n"
    "      <button class=\"btn-primary\">Confirm Action</button>\n"
    "      <button class=\"btn-secondary\">Cancel</button>\n"
    "      <a href=\"http://firstos.local/test.html\">Visit Local Documentation</a>\n"
    "    </p>\n"
    "  </div>\n"
    "\n"
    "  <div class=\"card\">\n"
    "    <h2>Kernel Diagnostics Output</h2>\n"
    "    <pre>[FirstOS Kernel 0.1.0-baremetal]\nCPU: i686 Compatible | VESA 1024x768x32bpp | VirtIO-Net 0x1AF4:0x1000\nFreestanding Browser Engine online with 16MB Heap.</pre>\n"
    "  </div>\n"
    "</body>\n"
    "</html>";

const char *bb_http_get_offline_demo_page(void) {
    return s_offline_demo_page;
}

bool bb_url_parse(const char *raw_url, bb_url_t *out_url) {
    if (!raw_url || !out_url) return false;
    bb_memset(out_url, 0, sizeof(bb_url_t));

    const char *p = raw_url;
    while (*p == ' ' || *p == '\t') p++;

    /* Protocol */
    const char *proto_end = bb_strstr(p, "://");
    if (proto_end) {
        size_t proto_len = (size_t)(proto_end - p);
        if (proto_len >= sizeof(out_url->protocol)) proto_len = sizeof(out_url->protocol) - 1;
        bb_memcpy(out_url->protocol, p, proto_len);
        out_url->protocol[proto_len] = '\0';
        p = proto_end + 3;
    } else {
        bb_strcpy(out_url->protocol, "http");
    }

    /* Host and Port */
    const char *host_start = p;
    while (*p && *p != '/' && *p != ':') p++;
    size_t host_len = (size_t)(p - host_start);
    if (host_len >= sizeof(out_url->host)) host_len = sizeof(out_url->host) - 1;
    bb_memcpy(out_url->host, host_start, host_len);
    out_url->host[host_len] = '\0';

    if (*p == ':') {
        p++;
        out_url->port = (uint16_t)bb_atoi(p);
        while (*p && *p != '/') p++;
    } else {
        out_url->port = 80;
    }

    /* Path */
    if (*p == '/') {
        bb_strncpy(out_url->path, p, sizeof(out_url->path) - 1);
    } else {
        bb_strcpy(out_url->path, "/");
    }

    return true;
}

bb_http_response_t *bb_http_get(const bb_url_t *url) {
    if (!url) return NULL;

    /* If offline or local test URL, return offline demo */
    if (bb_strcasecmp(url->host, "offline") == 0 ||
        bb_strcasecmp(url->host, "demo") == 0 ||
        bb_strcasecmp(url->host, "about:blank") == 0 ||
        url->host[0] == '\0') {
        bb_http_response_t *resp = (bb_http_response_t *)bb_malloc(sizeof(bb_http_response_t));
        if (!resp) return NULL;
        resp->status_code = 200;
        resp->content_type = bb_strdup("text/html");
        resp->body = bb_strdup(s_offline_demo_page);
        resp->body_len = bb_strlen(s_offline_demo_page);
        return resp;
    }

    /* Connect to remote host */
    bb_socket_t sock = bb_osal_net_connect(url->host, url->port);
    if (!sock) {
        bb_osal_log("[HTTP Client] Failed to connect to host, falling back to offline demo\n");
        bb_http_response_t *resp = (bb_http_response_t *)bb_malloc(sizeof(bb_http_response_t));
        if (!resp) return NULL;
        resp->status_code = 200;
        resp->content_type = bb_strdup("text/html");
        resp->body = bb_strdup(s_offline_demo_page);
        resp->body_len = bb_strlen(s_offline_demo_page);
        return resp;
    }

    /* Build HTTP/1.1 GET request */
    char req[512];
    bb_snprintf(req, sizeof(req),
                "GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                "User-Agent: BaremetalBrowser/1.0 (FirstOS)\r\n"
                "Accept: text/html, */*\r\n"
                "Connection: close\r\n"
                "\r\n",
                url->path, url->host);

    bb_osal_net_send(sock, req, bb_strlen(req));

    /* Receive full response */
    bb_str_t stream;
    bb_str_init_with_cap(&stream, 8192);

    char recv_buf[2048];
    while (1) {
        int bytes = bb_osal_net_recv(sock, recv_buf, sizeof(recv_buf), 3000);
        if (bytes <= 0) break;
        bb_str_append_mem(&stream, recv_buf, (size_t)bytes);
    }
    bb_osal_net_close(sock);

    if (stream.length == 0) {
        bb_str_free(&stream);
        return NULL;
    }

    /* Parse headers & body */
    bb_http_response_t *resp = (bb_http_response_t *)bb_malloc(sizeof(bb_http_response_t));
    if (!resp) {
        bb_str_free(&stream);
        return NULL;
    }
    bb_memset(resp, 0, sizeof(bb_http_response_t));

    /* Parse HTTP Status Line */
    const char *data = stream.data;
    if (bb_strncmp(data, "HTTP/1.", 7) == 0) {
        data += 8;
        while (*data == ' ') data++;
        resp->status_code = bb_atoi(data);
    }

    /* Locate boundary between headers and body */
    const char *body_sep = bb_strstr(stream.data, "\r\n\r\n");
    if (body_sep) {
        size_t head_len = (size_t)(body_sep - stream.data);
        const char *raw_body = body_sep + 4;
        size_t raw_body_len = stream.length - (head_len + 4);

        /* Check for Chunked Transfer Encoding */
        bool is_chunked = false;
        char *head_buf = (char *)bb_malloc(head_len + 1);
        if (head_buf) {
            bb_memcpy(head_buf, stream.data, head_len);
            head_buf[head_len] = '\0';
            if (bb_strcasestr(head_buf, "chunked")) {
                is_chunked = true;
            }
            bb_free(head_buf);
        }

        if (is_chunked) {
            /* Decode hex chunks */
            bb_str_t decoded;
            bb_str_init_with_cap(&decoded, raw_body_len);
            const char *cp = raw_body;
            while (*cp) {
                /* Read hex size */
                int chunk_size = 0;
                while (*cp && *cp != '\r' && *cp != '\n') {
                    int h = 0;
                    if (*cp >= '0' && *cp <= '9') h = *cp - '0';
                    else if (*cp >= 'a' && *cp <= 'f') h = 10 + (*cp - 'a');
                    else if (*cp >= 'A' && *cp <= 'F') h = 10 + (*cp - 'A');
                    chunk_size = (chunk_size << 4) | h;
                    cp++;
                }
                while (*cp == '\r' || *cp == '\n') cp++;
                if (chunk_size == 0) break;
                bb_str_append_mem(&decoded, cp, (size_t)chunk_size);
                cp += chunk_size;
                while (*cp == '\r' || *cp == '\n') cp++;
            }
            resp->body = decoded.data;
            resp->body_len = decoded.length;
        } else {
            resp->body = (char *)bb_malloc(raw_body_len + 1);
            if (resp->body) {
                bb_memcpy(resp->body, raw_body, raw_body_len);
                resp->body[raw_body_len] = '\0';
                resp->body_len = raw_body_len;
            }
        }
    } else {
        resp->body = bb_strdup(stream.data);
        resp->body_len = stream.length;
    }

    bb_str_free(&stream);
    return resp;
}

void bb_http_response_free(bb_http_response_t *resp) {
    if (!resp) return;
    if (resp->content_type) bb_free(resp->content_type);
    if (resp->body) bb_free(resp->body);
    bb_free(resp);
}
