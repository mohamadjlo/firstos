#define _POSIX_C_SOURCE 200809L
#include "net.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

void http_response_free(HttpResponse *res) {
    if (!res) return;
    free(res->status_text);
    free(res->content_type);
    free(res->body);
    memset(res, 0, sizeof(HttpResponse));
}

static int fetch_local_file(const char *path, HttpResponse *out_res) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        out_res->status_code = 404;
        out_res->status_text = strdup("Not Found");
        out_res->body = strdup("<h1>404 Not Found</h1><p>The specified local file does not exist.</p>");
        out_res->body_len = strlen(out_res->body);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }

    size_t read_bytes = fread(buf, 1, size, f);
    buf[read_bytes] = 0;
    fclose(f);

    out_res->status_code = 200;
    out_res->status_text = strdup("OK");
    out_res->content_type = strdup("text/html");
    out_res->body = buf;
    out_res->body_len = read_bytes;
    return 0;
}

int net_fetch_url(const char *url, CookieJar *jar, HttpResponse *out_res) {
    if (!url || !out_res) return -1;
    memset(out_res, 0, sizeof(HttpResponse));

    /* 1. Handle file:// or local paths */
    if (strncmp(url, "file://", 7) == 0) {
        return fetch_local_file(url + 7, out_res);
    }
    if (strncmp(url, "http://", 7) != 0) {
        /* Treat as relative or direct local file */
        return fetch_local_file(url, out_res);
    }

    /* 2. Parse http URL: http://host[:port]/path */
    const char *p = url + 7;
    char host[256] = {0};
    char port_str[16] = "80";
    char path[1024] = "/";

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (slash) {
        strncpy(path, slash, sizeof(path) - 1);
        size_t host_len = (size_t)(slash - p);
        if (colon && colon < slash) {
            size_t hlen = (size_t)(colon - p);
            if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
            strncpy(host, p, hlen);
            size_t plen = (size_t)(slash - colon - 1);
            if (plen >= sizeof(port_str)) plen = sizeof(port_str) - 1;
            strncpy(port_str, colon + 1, plen);
        } else {
            if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
            strncpy(host, p, host_len);
        }
    } else {
        if (colon) {
            size_t hlen = (size_t)(colon - p);
            if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
            strncpy(host, p, hlen);
            strncpy(port_str, colon + 1, sizeof(port_str) - 1);
        } else {
            strncpy(host, p, sizeof(host) - 1);
        }
    }

    /* 3. Connect via POSIX sockets */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        out_res->status_code = 502;
        out_res->status_text = strdup("Bad Gateway");
        out_res->body = strdup("<h1>502 Bad Gateway</h1><p>DNS resolution failed.</p>");
        out_res->body_len = strlen(out_res->body);
        return -1;
    }

    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    /* Set 5-second timeout */
    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
        close(sockfd);
        freeaddrinfo(res);
        out_res->status_code = 504;
        out_res->status_text = strdup("Gateway Timeout");
        out_res->body = strdup("<h1>504 Gateway Timeout</h1><p>Failed to connect to host.</p>");
        out_res->body_len = strlen(out_res->body);
        return -1;
    }
    freeaddrinfo(res);

    /* 4. Format HTTP/1.1 Request with Cookies */
    char req[4096];
    char *cookie_header = jar ? cookie_jar_get_header(jar, host, path) : NULL;

    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: MiniBrowser/1.0 (SDL2; POSIX)\r\n"
             "Accept: text/html, text/css, */*\r\n"
             "Connection: close\r\n"
             "%s%s"
             "\r\n",
             path, host,
             cookie_header ? cookie_header : "",
             cookie_header ? "\r\n" : "");

    free(cookie_header);

    if (send(sockfd, req, strlen(req), 0) < 0) {
        close(sockfd);
        return -1;
    }

    /* 5. Receive HTTP Response */
    size_t cap = 65536;
    size_t total = 0;
    char *raw_data = (char *)malloc(cap);

    while (1) {
        if (total + 4096 >= cap) {
            cap *= 2;
            raw_data = (char *)realloc(raw_data, cap);
        }
        ssize_t n = recv(sockfd, raw_data + total, 4096, 0);
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(sockfd);
    raw_data[total] = 0;

    /* 6. Parse Status, Headers, Set-Cookie, and Body */
    char *header_end = strstr(raw_data, "\r\n\r\n");
    if (!header_end) {
        header_end = strstr(raw_data, "\n\n");
    }

    if (header_end) {
        size_t hdr_len = (size_t)(header_end - raw_data);
        char *body_start = (strncmp(header_end, "\r\n\r\n", 4) == 0) ? (header_end + 4) : (header_end + 2);

        /* Parse status line */
        int code = 200;
        char status_msg[64] = "OK";
        sscanf(raw_data, "HTTP/%*s %d %63[^\r\n]", &code, status_msg);
        out_res->status_code = code;
        out_res->status_text = strdup(status_msg);

        /* Extract Set-Cookie headers */
        char *hdr_copy = strndup(raw_data, hdr_len);
        char *line = strtok(hdr_copy, "\r\n");
        while (line) {
            if (strncasecmp(line, "Set-Cookie:", 11) == 0) {
                if (jar) {
                    cookie_jar_set(jar, line + 11, host, path);
                }
            } else if (strncasecmp(line, "Content-Type:", 13) == 0) {
                out_res->content_type = strdup(line + 13);
            }
            line = strtok(NULL, "\r\n");
        }
        free(hdr_copy);

        size_t blen = total - (size_t)(body_start - raw_data);
        out_res->body = (char *)malloc(blen + 1);
        memcpy(out_res->body, body_start, blen);
        out_res->body[blen] = 0;
        out_res->body_len = blen;
    } else {
        out_res->status_code = 200;
        out_res->status_text = strdup("OK");
        out_res->body = strdup(raw_data);
        out_res->body_len = total;
    }

    free(raw_data);
    return 0;
}
