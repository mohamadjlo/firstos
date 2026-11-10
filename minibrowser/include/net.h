#ifndef NET_H
#define NET_H

#include "cookie.h"
#include <stddef.h>

typedef struct HttpResponse {
    int status_code;
    char *status_text;
    char *content_type;
    char *body;
    size_t body_len;
} HttpResponse;

void http_response_free(HttpResponse *res);

/* Fetches a URL (http:// or file:// or local path), handles cookies, and returns response */
int net_fetch_url(const char *url, CookieJar *jar, HttpResponse *out_res);

#endif /* NET_H */
