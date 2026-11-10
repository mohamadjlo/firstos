#ifndef HTTP_V11_H
#define HTTP_V11_H

#include "io.h"
#include "net_socket.h"
#include "bnet_error.h"

typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
    HTTP_METHOD_HEAD
} http_method_t;

typedef struct {
    char name[64];
    char value[256];
} http_header_t;

#define HTTP_MAX_HEADERS 32

typedef struct {
    int status_code;
    char status_message[64];
    http_header_t headers[HTTP_MAX_HEADERS];
    int header_count;
    char content_type[64];
    char content_encoding[32];
    u32 content_length;
    int is_chunked;
    int is_keep_alive;
    int is_truncated;
    int is_complete;
    char location[256];
    u8 *body;
    u32 body_len;
} http_response_t;

typedef struct {
    const char *url;
    http_method_t method;
    const char *post_data;
    u32 post_data_len;
    const char *custom_headers;
    int follow_redirects;
    u32 timeout_polls;
    u32 max_response_size; /* 0 for default 8 MiB limit */
} http_request_t;

/* Callback signature for streaming downloads */
typedef int (*http_stream_cb)(const u8 *chunk, u32 chunk_len, u32 total_expected, void *user_data);

/* Callback signature for request status updates */
typedef void (*http_status_cb_t)(const char *status, u32 current_bytes, u32 total_bytes);
void http_set_status_callback(http_status_cb_t cb);

/* High-level API */
http_response_t *http_execute(const http_request_t *req);
http_response_t *http_execute_ex(const http_request_t *req, bnet_error_t *out_err);
void http_response_free(http_response_t *res);
const char *http_get_header(const http_response_t *res, const char *name);

/* Streaming API for large files / downloads */
int http_stream(const http_request_t *req, http_stream_cb cb, void *user_data);

/* URL Parsing Helper */
int http_parse_url(const char *url, char *out_host, int host_sz, u16 *out_port, char *out_path, int path_sz, int *out_is_ssl);

/* Chunked Transfer Decoding Helper */
int http_decode_chunked(const u8 *src, u32 src_len, u8 *dst, u32 max_dst_len);

/* Proxy Bridge Configuration */
void http_set_proxy(const char *host, u16 port);
const char *http_get_proxy_host(void);
u16 http_get_proxy_port(void);
int http_is_proxy_configured(void);

#endif /* HTTP_V11_H */
