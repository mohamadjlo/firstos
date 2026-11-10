#ifndef HTTP_BODY_H
#define HTTP_BODY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "io.h"

/* Configurable Resource Size Budgets */
#define HTTP_BODY_MAX_HTML (4u * 1024u * 1024u)  /* 4 MiB */
#define HTTP_BODY_MAX_CSS  (2u * 1024u * 1024u)  /* 2 MiB */
#define HTTP_BODY_MAX_JS   (4u * 1024u * 1024u)  /* 4 MiB */
#define HTTP_BODY_MAX_IMG  (8u * 1024u * 1024u)  /* 8 MiB */

/* Unified Resource State Machine */
typedef enum {
    RESOURCE_STATE_IDLE = 0,
    RESOURCE_STATE_LOADED,
    RESOURCE_STATE_INCOMPLETE,
    RESOURCE_STATE_TRUNCATED,
    RESOURCE_STATE_HTTP_ERROR,
    RESOURCE_STATE_PROXY_ERROR,
    RESOURCE_STATE_UNSUPPORTED_ENCODING,
    RESOURCE_STATE_OOM,
    RESOURCE_STATE_PARSE_ERROR
} resource_state_t;

typedef struct {
    unsigned char *data;
    unsigned int length;
    unsigned int capacity;
    unsigned int max_capacity;
    unsigned int expected_length;
    int has_expected_length;
    int complete;
    int truncated;
    int binary;
    int http_status;
    resource_state_t state;
    char scheme[16];
    char content_type[64];
    char content_encoding[32];
} http_body_t;

typedef http_body_t HttpBody;

/* Initialize body with zero length and starting allocation */
void http_body_init(http_body_t *body, unsigned int initial_capacity);

/* Append binary chunk with bounded dynamic exponential growth up to max_capacity */
int  http_body_append(http_body_t *body, const void *chunk, unsigned int chunk_len, unsigned int max_capacity);

/* Finalize body state, verifying Content-Length against received bytes and setting state/complete/truncated */
void http_body_finalize(http_body_t *body, int http_status, const char *content_type, const char *content_encoding, unsigned int expected_len, int has_expected_len);

/* Release memory safely */
void http_body_free(http_body_t *body);

/* State query & name helper */
const char *resource_state_name(resource_state_t state);

/* Centralized Executable Guard: returns 1 ONLY if state==LOADED, 2xx, complete, non-truncated, len>0, and JS */
int  resource_is_executable(const http_body_t *body);

/* Centralized CSS Guard: returns 1 ONLY if state==LOADED, 2xx, complete, non-truncated, len>0, and CSS */
int  resource_is_css(const http_body_t *body);

/* Centralized Truncation Guard: returns 1 if complete and non-truncated, 0 otherwise */
int  http_body_check_guard(const http_body_t *body, const char *resource_name);

/* Standardized structured resource logger with all 12 diagnostic fields */
void http_body_log(const char *url, const http_body_t *body, const char *parse_result);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_BODY_H */
