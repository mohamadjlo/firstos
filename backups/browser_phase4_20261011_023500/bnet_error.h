#ifndef BNET_ERROR_H
#define BNET_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BNET_OK = 0,
    BNET_ERR_BAD_URL,
    BNET_ERR_UNSUPPORTED_SCHEME,
    BNET_ERR_DNS_START_FAILED,
    BNET_ERR_DNS_TIMEOUT,
    BNET_ERR_DNS_NO_RECORD,
    BNET_ERR_TCP_CONNECT_FAILED,
    BNET_ERR_TCP_TIMEOUT,
    BNET_ERR_TCP_RESET,
    BNET_ERR_TLS_NOT_AVAILABLE,
    BNET_ERR_TLS_HANDSHAKE_FAILED,
    BNET_ERR_TLS_TIMEOUT,
    BNET_ERR_HTTP_SEND_FAILED,
    BNET_ERR_HTTP_RECV_TIMEOUT,
    BNET_ERR_HTTP_BAD_RESPONSE,
    BNET_ERR_BODY_TOO_LARGE,
    BNET_ERR_ABORTED,
    BNET_ERR_INTERNAL,
    /* Host Proxy Bridge Specific Errors */
    BNET_ERR_PROXY_REQUIRED,
    BNET_ERR_PROXY_NOT_CONFIGURED,
    BNET_ERR_PROXY_CONNECT_FAILED,
    BNET_ERR_PROXY_TIMEOUT,
    BNET_ERR_PROXY_BAD_RESPONSE,
    BNET_ERR_PROXY_HTTP_ERROR,
    BNET_ERR_HTTPS_BRIDGE_UNAVAILABLE,
    BNET_ERR_BODY_TRUNCATED,
    BNET_ERR_UNSUPPORTED_ENCODING
} bnet_error_t;

/* Returns compact symbolic enum string (e.g. "BNET_ERR_DNS_TIMEOUT") */
const char *bnet_error_name(bnet_error_t err);

/* Returns user-facing, stage-specific diagnostic description */
const char *bnet_error_description(bnet_error_t err);

#ifdef __cplusplus
}
#endif

#endif /* BNET_ERROR_H */
