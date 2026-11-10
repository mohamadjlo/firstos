#ifndef BB_HTTP_H
#define BB_HTTP_H

#include "bb_types.h"
#include "bb_osal.h"
#include "bb_libc.h"

typedef struct {
    char     protocol[16];
    char     host[128];
    uint16_t port;
    char     path[256];
} bb_url_t;

typedef struct {
    int     status_code;
    char   *content_type;
    char   *body;
    size_t  body_len;
} bb_http_response_t;

bool                bb_url_parse(const char *raw_url, bb_url_t *out_url);
bb_http_response_t *bb_http_get(const bb_url_t *url);
void                bb_http_response_free(bb_http_response_t *resp);

/* Offline fallback demonstration page */
const char         *bb_http_get_offline_demo_page(void);

#endif /* BB_HTTP_H */
