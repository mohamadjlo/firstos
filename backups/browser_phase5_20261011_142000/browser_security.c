#include "include/firstos/security/browser_security.h"
#include <stddef.h>
#include <string.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

static uint32_t s_next_opaque_id = 1;

int browser_security_str_eq_ci(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if (ca != cb) return 0;
        a++; b++;
    }
    return (*a == 0 && *b == 0);
}

int browser_security_str_ends_with_ci(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    int slen = (int)strlen(str);
    int suflen = (int)strlen(suffix);
    if (suflen > slen) return 0;
    return browser_security_str_eq_ci(str + slen - suflen, suffix);
}

int browser_security_str_starts_with(const char *str, const char *prefix) {
    if (!str || !prefix) return 0;
    while (*prefix) {
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

#define str_eq_ci browser_security_str_eq_ci
#define str_ends_with_ci browser_security_str_ends_with_ci
#define str_starts_with browser_security_str_starts_with

static void u32_to_str(uint32_t val, char *buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[16];
    int tp = 0;
    while (val > 0) {
        tmp[tp++] = (char)('0' + (val % 10));
        val /= 10;
    }
    int bp = 0;
    while (tp > 0) {
        buf[bp++] = tmp[--tp];
    }
    buf[bp] = '\0';
}

int browser_security_parse_origin(const char *url, firstos_origin_t *out_origin) {
    if (!url || !url[0] || !out_origin) return -1;

    memset(out_origin, 0, sizeof(firstos_origin_t));

    /* Check length & control characters */
    int len = 0;
    while (url[len]) {
        unsigned char c = (unsigned char)url[len];
        if (c < 32 || c == 127) return -2;
        len++;
        if (len > 1024) return -3;
    }

    /* 1. about: scheme */
    if (strncmp(url, "about:", 6) == 0) {
        out_origin->scheme = FIRSTOS_ORIGIN_ABOUT;
        out_origin->host[0] = '\0';
        out_origin->port = 0;
        out_origin->is_opaque = 1;
        out_origin->opaque_id = s_next_opaque_id++;
        return 0;
    }

    /* 2. data: scheme */
    if (strncmp(url, "data:", 5) == 0) {
        out_origin->scheme = FIRSTOS_ORIGIN_OPAQUE;
        out_origin->host[0] = '\0';
        out_origin->port = 0;
        out_origin->is_opaque = 1;
        out_origin->opaque_id = s_next_opaque_id++;
        return 0;
    }

    /* 3. file: scheme or local root path */
    if (strncmp(url, "file://", 7) == 0 || url[0] == '/') {
        out_origin->scheme = FIRSTOS_ORIGIN_FILE;
        out_origin->host[0] = '\0';
        out_origin->port = 0;
        out_origin->is_opaque = 0;
        out_origin->opaque_id = 0;
        return 0;
    }

    /* 4. http: or https: scheme */
    const char *p = url;
    firstos_origin_scheme_t scheme = FIRSTOS_ORIGIN_OPAQUE;
    uint16_t default_port = 80;

    if (strncmp(p, "http://", 7) == 0) {
        scheme = FIRSTOS_ORIGIN_HTTP;
        default_port = 80;
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        scheme = FIRSTOS_ORIGIN_HTTPS;
        default_port = 443;
        p += 8;
    } else {
        return -4; /* Unsupported scheme */
    }

    /* Extract host until ':', '/', '?', '#', or end of string */
    char host_buf[128];
    int hi = 0;
    while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#' && hi < (int)sizeof(host_buf) - 1) {
        char ch = *p;
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32); /* Normalize to lowercase */
        host_buf[hi++] = ch;
        p++;
    }
    host_buf[hi] = '\0';
    if (hi == 0) return -5; /* Missing host */

    uint16_t port = default_port;
    if (*p == ':') {
        p++;
        int port_val = 0;
        while (*p >= '0' && *p <= '9') {
            port_val = port_val * 10 + (*p - '0');
            if (port_val > 65535) return -6; /* Port out of range */
            p++;
        }
        if (port_val > 0) port = (uint16_t)port_val;
    }

    out_origin->scheme = scheme;
    strncpy(out_origin->host, host_buf, sizeof(out_origin->host) - 1);
    out_origin->host[sizeof(out_origin->host) - 1] = '\0';
    out_origin->port = port;
    out_origin->is_opaque = 0;
    out_origin->opaque_id = 0;

    return 0;
}

int browser_security_origin_equals(const firstos_origin_t *a, const firstos_origin_t *b) {
    if (!a || !b) return 0;

    /* Opaque origins never match unless they share the same non-zero opaque ID */
    if (a->is_opaque || b->is_opaque) {
        return (a->is_opaque && b->is_opaque && a->opaque_id != 0 && a->opaque_id == b->opaque_id);
    }

    if (a->scheme != b->scheme) return 0;
    if (a->port != b->port) return 0;

    return str_eq_ci(a->host, b->host);
}

int browser_security_origin_to_string(const firstos_origin_t *origin, char *out_buf, int max_len) {
    if (!origin || !out_buf || max_len <= 0) return -1;
    out_buf[0] = '\0';

    if (origin->is_opaque) {
        char id_str[16];
        u32_to_str(origin->opaque_id, id_str);
        int pos = 0;
        const char *pref = "opaque:";
        while (*pref && pos < max_len - 1) out_buf[pos++] = *pref++;
        const char *idp = id_str;
        while (*idp && pos < max_len - 1) out_buf[pos++] = *idp++;
        out_buf[pos] = '\0';
        return pos;
    }

    if (origin->scheme == FIRSTOS_ORIGIN_FILE) {
        const char *f = "file://";
        int pos = 0;
        while (*f && pos < max_len - 1) out_buf[pos++] = *f++;
        out_buf[pos] = '\0';
        return pos;
    }

    const char *scheme_str = (origin->scheme == FIRSTOS_ORIGIN_HTTPS) ? "https://" : "http://";
    uint16_t default_port = (origin->scheme == FIRSTOS_ORIGIN_HTTPS) ? 443 : 80;

    int pos = 0;
    while (*scheme_str && pos < max_len - 1) out_buf[pos++] = *scheme_str++;
    const char *hp = origin->host;
    while (*hp && pos < max_len - 1) out_buf[pos++] = *hp++;

    if (origin->port != default_port && origin->port != 0) {
        if (pos < max_len - 1) out_buf[pos++] = ':';
        char port_str[16];
        u32_to_str((uint32_t)origin->port, port_str);
        const char *pp = port_str;
        while (*pp && pos < max_len - 1) out_buf[pos++] = *pp++;
    }

    out_buf[pos] = '\0';
    return pos;
}

int browser_security_can_load_subresource(const firstos_origin_t *doc_origin, const char *subresource_url, int resource_type) {
    (void)resource_type;
    if (!subresource_url || !subresource_url[0]) return 0;

    /* data: URLs are always allowed as subresources */
    if (strncmp(subresource_url, "data:", 5) == 0) return 1;

    /* Traversal defense in URL */
    int len = 0;
    while (subresource_url[len]) {
        if ((unsigned char)subresource_url[len] < 32 || (unsigned char)subresource_url[len] == 127) return 0;
        len++;
    }
    for (int i = 0; i < len - 1; i++) {
        if (subresource_url[i] == '.' && subresource_url[i+1] == '.') {
            if (i + 2 == len || subresource_url[i+2] == '/' || subresource_url[i+2] == '\\' ||
                (i > 0 && (subresource_url[i-1] == '/' || subresource_url[i-1] == '\\'))) {
                return 0; /* Path traversal prohibited */
            }
        }
    }

    /* If embedding document is on a web origin (HTTP / HTTPS) */
    if (doc_origin && (doc_origin->scheme == FIRSTOS_ORIGIN_HTTP || doc_origin->scheme == FIRSTOS_ORIGIN_HTTPS)) {
        /* Strictly block web documents from loading local file:// resources */
        if (strncmp(subresource_url, "file://", 7) == 0) {
            return 0;
        }
        /* Relative paths on same origin, data URLs, and remote web subresources (e.g. images, css) are permitted */
        if (subresource_url[0] == '/' ||
            strncmp(subresource_url, "data:", 5) == 0 ||
            strncmp(subresource_url, "http://", 7) == 0 ||
            strncmp(subresource_url, "https://", 8) == 0) {
            return 1;
        }
        return 0;
    }

    /* Local documents (file:// or about:) can load local resources, data URLs, and remote URLs */
    return 1;
}

int browser_security_can_navigate(const firstos_origin_t *current_origin, const char *target_url) {
    (void)current_origin;
    if (!target_url || !target_url[0]) return 0;

    int len = 0;
    while (target_url[len]) {
        if ((unsigned char)target_url[len] < 32 || (unsigned char)target_url[len] == 127) return 0;
        len++;
        if (len > 1024) return 0;
    }

    for (int i = 0; i < len - 1; i++) {
        if (target_url[i] == '.' && target_url[i+1] == '.') {
            if (i + 2 == len || target_url[i+2] == '/' || target_url[i+2] == '\\' ||
                (i > 0 && (target_url[i-1] == '/' || target_url[i-1] == '\\'))) {
                return 0;
            }
        }
    }

    if (strncmp(target_url, "about:", 6) == 0) return 1;
    if (strncmp(target_url, "data:", 5) == 0) return 1;
    if (strncmp(target_url, "http://", 7) == 0 || strncmp(target_url, "https://", 8) == 0) return 1;
    if (strncmp(target_url, "file://", 7) == 0 || target_url[0] == '/') return 1;

    return 0;
}

int browser_security_validate_cookie_domain(const char *cookie_domain, const char *req_host) {
    if (!cookie_domain || !cookie_domain[0] || !req_host || !req_host[0]) return 0;

    const char *cd = cookie_domain;
    if (cd[0] == '.') cd++; /* Strip leading dot */
    if (!cd[0]) return 0;

    /* If exact match */
    if (str_eq_ci(req_host, cd)) return 1;

    /* Prevent setting cookies on top-level domains (TLDs) like .com, .org, or domain without dot */
    if (!strchr(cd, '.')) return 0;

    /* req_host must end with .cd */
    int cd_len = (int)strlen(cd);
    int rh_len = (int)strlen(req_host);
    if (rh_len <= cd_len) return 0;

    /* Verify separator before cd in req_host is a dot */
    if (req_host[rh_len - cd_len - 1] != '.') return 0;

    return str_ends_with_ci(req_host, cd);
}

int browser_security_validate_cookie_path(const char *cookie_path, const char *req_path) {
    if (!cookie_path || !cookie_path[0]) return 1;
    if (!req_path || !req_path[0]) req_path = "/";

    int cplen = (int)strlen(cookie_path);
    if (strncmp(req_path, cookie_path, (size_t)cplen) != 0) return 0;

    /* Either exact match, cookie_path ends with '/', or next char in req_path is '/' */
    if (cookie_path[cplen - 1] == '/' || req_path[cplen] == '\0' || req_path[cplen] == '/') {
        return 1;
    }
    return 0;
}

int browser_security_is_scheme_allowed(const char *url) {
    if (!url || !url[0]) return 0;
    if (strncmp(url, "http://", 7) == 0 ||
        strncmp(url, "https://", 8) == 0 ||
        strncmp(url, "about:", 6) == 0 ||
        strncmp(url, "data:", 5) == 0 ||
        strncmp(url, "file://", 7) == 0 ||
        url[0] == '/') {
        return 1;
    }
    return 0;
}

int browser_security_check_url(const char *url, const char *context) {
    if (!browser_security_is_scheme_allowed(url)) {
        extern void serial_write(const char *str);
        serial_write("[security] blocked scheme=");
        if (url) {
            char scheme_buf[32];
            int p = 0;
            while (url[p] && url[p] != ':' && p < 31) {
                scheme_buf[p] = url[p];
                p++;
            }
            scheme_buf[p] = '\0';
            serial_write(scheme_buf);
        } else {
            serial_write("null");
        }
        serial_write("\n");

        serial_write("[security] blocked resource url=");
        serial_write(url ? url : "(null)");
        serial_write(" reason=disallowed_scheme\n");
        return 0;
    }
    (void)context;
    extern void serial_write(const char *str);
    serial_write("[security] checked url=");
    serial_write(url);
    serial_write(" allowed=1\n");
    return 1;
}

int browser_security_check_subresource_url(const char *doc_url, const char *subresource_url, int resource_type) {
    extern void serial_write(const char *str);
    if (!subresource_url || !subresource_url[0]) return 0;
    if (!browser_security_is_scheme_allowed(subresource_url)) {
        serial_write("[security] blocked resource url=");
        serial_write(subresource_url);
        serial_write(" reason=disallowed_scheme\n");
        return 0;
    }
    firstos_origin_t doc_orig;
    if (doc_url && doc_url[0] && browser_security_parse_origin(doc_url, &doc_orig) == 0) {
        if (!browser_security_can_load_subresource(&doc_orig, subresource_url, resource_type)) {
            serial_write("[security] blocked resource url=");
            serial_write(subresource_url);
            serial_write(" reason=cross_origin_sop\n");
            return 0;
        }
    }
    return 1;
}
