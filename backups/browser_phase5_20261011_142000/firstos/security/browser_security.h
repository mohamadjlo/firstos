#ifndef _FIRSTOS_SECURITY_BROWSER_SECURITY_H
#define _FIRSTOS_SECURITY_BROWSER_SECURITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FIRSTOS_ORIGIN_OPAQUE = 0,
    FIRSTOS_ORIGIN_HTTP   = 1,
    FIRSTOS_ORIGIN_HTTPS  = 2,
    FIRSTOS_ORIGIN_FILE   = 3,
    FIRSTOS_ORIGIN_ABOUT  = 4
} firstos_origin_scheme_t;

typedef struct {
    firstos_origin_scheme_t scheme;
    char     host[128];     /* Canonical lowercase host or IPv4 address */
    uint16_t port;          /* Normalized port: 80 for HTTP, 443 for HTTPS if default */
    uint8_t  is_opaque;     /* 1 if origin is opaque (e.g. data:, about:, sandboxed) */
    uint32_t opaque_id;     /* Unique monotonic ID if opaque */
} firstos_origin_t;

typedef struct {
    int              renderer_pid;
    uint32_t         navigation_id;
    firstos_origin_t committed_origin;
    int              is_isolated;
    uint32_t         flags;
} firstos_renderer_security_context_t;

/* Origin parsing and canonicalization:
 * - Parses scheme, host, and port.
 * - Strips default ports (:80 for HTTP, :443 for HTTPS).
 * - Converts host to lowercase.
 * - Treats data: and about: as opaque origins.
 * Returns 0 on success, negative error code on failure. */
int browser_security_parse_origin(const char *url, firstos_origin_t *out_origin);

/* Equality test for two origins:
 * Returns 1 if same-origin, 0 if cross-origin.
 * Opaque origins are never equal unless they share the same non-zero opaque_id. */
int browser_security_origin_equals(const firstos_origin_t *a, const firstos_origin_t *b);

/* Serialize origin to string format (e.g. "http://example.com:8080" or "opaque:1") */
int browser_security_origin_to_string(const firstos_origin_t *origin, char *out_buf, int max_len);

/* Same-Origin Policy check for subresource loading:
 * doc_origin: Origin of the embedding document.
 * subresource_url: Target URL of the requested subresource.
 * resource_type: Resource type (e.g. stylesheet, image, font).
 * Returns 1 if permitted, 0 if blocked. */
int browser_security_can_load_subresource(const firstos_origin_t *doc_origin, const char *subresource_url, int resource_type);

/* Navigation permission check:
 * Returns 1 if navigation from current_origin to target_url is permitted, 0 if blocked. */
int browser_security_can_navigate(const firstos_origin_t *current_origin, const char *target_url);

/* Validate cookie domain scoping against request host:
 * Returns 1 if cookie_domain is valid for req_host, 0 otherwise. */
int browser_security_validate_cookie_domain(const char *cookie_domain, const char *req_host);

/* Validate cookie path scoping against request path:
 * Returns 1 if cookie_path matches req_path, 0 otherwise. */
int browser_security_validate_cookie_path(const char *cookie_path, const char *req_path);

/* High-Level Security Policy API */
int browser_security_is_scheme_allowed(const char *url);
int browser_security_check_url(const char *url, const char *context);
int browser_security_check_subresource_url(const char *doc_url, const char *subresource_url, int resource_type);

/* Security string matching helpers */
int browser_security_str_eq_ci(const char *a, const char *b);
int browser_security_str_ends_with_ci(const char *str, const char *suffix);
int browser_security_str_starts_with(const char *str, const char *prefix);

#ifdef __cplusplus
}
#endif

#endif /* _FIRSTOS_SECURITY_BROWSER_SECURITY_H */
