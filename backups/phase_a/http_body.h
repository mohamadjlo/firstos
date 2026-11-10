#ifndef HTTP_BODY_H
#define HTTP_BODY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "io.h"

/* Configurable Resource Size Budgets */
#define HTTP_BODY_MAX_HTML (2u * 1024u * 1024u)  /* 2 MiB */
#define HTTP_BODY_MAX_CSS  (1u * 1024u * 1024u)  /* 1 MiB */
#define HTTP_BODY_MAX_JS   (4u * 1024u * 1024u)  /* 4 MiB */
#define HTTP_BODY_MAX_IMG  (8u * 1024u * 1024u)  /* 8 MiB */

typedef struct {
    unsigned char *data;
    unsigned int length;
    unsigned int capacity;
    unsigned int expected_length;
    int has_expected_length;
    int complete;
    int truncated;
    int binary;
    int http_status;
    char content_type[64];
    char content_encoding[32];
} http_body_t;

typedef http_body_t HttpBody;

/* Initialize body with zero length and starting allocation */
void http_body_init(http_body_t *body, unsigned int initial_capacity);

/* Append binary chunk with bounded dynamic exponential growth up to max_capacity */
int  http_body_append(http_body_t *body, const void *chunk, unsigned int chunk_len, unsigned int max_capacity);

/* Finalize body state, verifying Content-Length against received bytes and setting complete/truncated */
void http_body_finalize(http_body_t *body, int http_status, const char *content_type, const char *content_encoding, unsigned int expected_len, int has_expected_len);

/* Release memory safely */
void http_body_free(http_body_t *body);

/* Centralized Truncation Guard: returns 1 if complete and non-truncated, 0 otherwise */
int  http_body_check_guard(const http_body_t *body, const char *resource_name);

/* Standardized structured resource logger */
void http_body_log(const char *url, const http_body_t *body, const char *parse_result);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_BODY_H */
