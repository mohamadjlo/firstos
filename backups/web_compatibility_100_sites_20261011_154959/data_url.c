#include "data_url.h"
#include <string.h>
#include <ctype.h>

url_scheme_t url_detect_scheme(const char *url) {
    if (!url || !url[0]) return URL_SCHEME_UNKNOWN;

    /* Skip leading whitespace */
    while (*url == ' ' || *url == '\t' || *url == '\r' || *url == '\n') url++;

    if (strncasecmp(url, "http://", 7) == 0) return URL_SCHEME_HTTP;
    if (strncasecmp(url, "https://", 8) == 0) return URL_SCHEME_HTTPS;
    if (strncasecmp(url, "data:", 5) == 0) return URL_SCHEME_DATA;
    if (strncasecmp(url, "blob:", 5) == 0) return URL_SCHEME_BLOB;
    if (strncasecmp(url, "file://", 7) == 0) return URL_SCHEME_FILE;
    if (strncasecmp(url, "about:", 6) == 0) return URL_SCHEME_ABOUT;
    if (strncasecmp(url, "javascript:", 11) == 0) return URL_SCHEME_JAVASCRIPT;

    return URL_SCHEME_UNKNOWN;
}

const char *url_scheme_name(url_scheme_t scheme) {
    switch (scheme) {
        case URL_SCHEME_HTTP:       return "http";
        case URL_SCHEME_HTTPS:      return "https";
        case URL_SCHEME_DATA:       return "data";
        case URL_SCHEME_BLOB:       return "blob";
        case URL_SCHEME_FILE:       return "file";
        case URL_SCHEME_ABOUT:      return "about";
        case URL_SCHEME_JAVASCRIPT: return "javascript";
        default:                    return "unknown";
    }
}

int url_is_network_scheme(url_scheme_t scheme) {
    return (scheme == URL_SCHEME_HTTP || scheme == URL_SCHEME_HTTPS);
}

static int b64_char_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

int base64_decode(const char *src, int src_len, unsigned char *dst, int max_dst_len) {
    if (!src || !dst || max_dst_len <= 0) return -1;
    if (src_len < 0) src_len = (int)strlen(src);

    int out_idx = 0;
    int quad[4];
    int quad_len = 0;

    for (int i = 0; i < src_len; i++) {
        char c = src[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '=') {
            /* Padding character encountered */
            quad[quad_len++] = -2;
            if (quad_len == 4) {
                int v0 = quad[0];
                int v1 = quad[1];
                int v2 = quad[2];
                int v3 = quad[3];

                if (v0 < 0 || v1 < 0) return -1; /* Invalid padding */

                if (out_idx >= max_dst_len) return -1;
                dst[out_idx++] = (unsigned char)((v0 << 2) | ((v1 >> 4) & 0x03));

                if (v2 >= 0) {
                    if (out_idx >= max_dst_len) return -1;
                    dst[out_idx++] = (unsigned char)(((v1 & 0x0F) << 4) | ((v2 >> 2) & 0x0F));
                }
                if (v3 >= 0) {
                    if (out_idx >= max_dst_len) return -1;
                    dst[out_idx++] = (unsigned char)(((v2 & 0x03) << 6) | (v3 & 0x3F));
                }
                quad_len = 0;
            }
            continue;
        }

        int val = b64_char_val(c);
        if (val < 0) return -1; /* Invalid Base64 character */

        quad[quad_len++] = val;
        if (quad_len == 4) {
            int v0 = quad[0];
            int v1 = quad[1];
            int v2 = quad[2];
            int v3 = quad[3];

            if (out_idx >= max_dst_len) return -1;
            dst[out_idx++] = (unsigned char)((v0 << 2) | ((v1 >> 4) & 0x03));

            if (out_idx >= max_dst_len) return -1;
            dst[out_idx++] = (unsigned char)(((v1 & 0x0F) << 4) | ((v2 >> 2) & 0x0F));

            if (out_idx >= max_dst_len) return -1;
            dst[out_idx++] = (unsigned char)(((v2 & 0x03) << 6) | (v3 & 0x3F));

            quad_len = 0;
        }
    }

    /* Process any remaining non-padded trailing characters */
    if (quad_len == 2) {
        int v0 = quad[0];
        int v1 = quad[1];
        if (v0 >= 0 && v1 >= 0) {
            if (out_idx >= max_dst_len) return -1;
            dst[out_idx++] = (unsigned char)((v0 << 2) | ((v1 >> 4) & 0x03));
        }
    } else if (quad_len == 3) {
        int v0 = quad[0];
        int v1 = quad[1];
        int v2 = quad[2];
        if (v0 >= 0 && v1 >= 0 && v2 >= 0) {
            if (out_idx >= max_dst_len) return -1;
            dst[out_idx++] = (unsigned char)((v0 << 2) | ((v1 >> 4) & 0x03));
            if (out_idx >= max_dst_len) return -1;
            dst[out_idx++] = (unsigned char)(((v1 & 0x0F) << 4) | ((v2 >> 2) & 0x0F));
        }
    } else if (quad_len != 0) {
        return -1; /* Malformed base64 block */
    }

    return out_idx;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int url_percent_decode(const char *src, int src_len, unsigned char *dst, int max_dst_len) {
    if (!src || !dst || max_dst_len <= 0) return 0;
    if (src_len < 0) src_len = (int)strlen(src);

    int out_idx = 0;
    for (int i = 0; i < src_len && out_idx < max_dst_len; i++) {
        if (src[i] == '%' && i + 2 < src_len) {
            int h1 = hex_val(src[i + 1]);
            int h2 = hex_val(src[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                dst[out_idx++] = (unsigned char)((h1 << 4) | h2);
                i += 2;
                continue;
            }
        }
        dst[out_idx++] = (unsigned char)src[i];
    }
    return out_idx;
}

int data_url_decode(const char *data_url,
                    char *out_mime, int max_mime_len,
                    unsigned char *out_buf, unsigned int max_out_len,
                    unsigned int *out_len) {
    if (out_len) *out_len = 0;
    if (!data_url || !out_buf || max_out_len == 0) return DATA_URL_ERR_INVALID;

    /* Skip leading whitespace */
    while (*data_url == ' ' || *data_url == '\t' || *data_url == '\r' || *data_url == '\n') data_url++;

    if (strncasecmp(data_url, "data:", 5) != 0) return DATA_URL_ERR_INVALID;

    const char *comma = strchr(data_url, ',');
    if (!comma) return DATA_URL_ERR_INVALID;

    const char *meta_start = data_url + 5;
    int meta_len = (int)(comma - meta_start);

    char meta[128] = {0};
    if (meta_len >= (int)sizeof(meta)) meta_len = sizeof(meta) - 1;
    memcpy(meta, meta_start, meta_len);
    meta[meta_len] = 0;

    int is_base64 = 0;
    char *b64_pos = strstr(meta, ";base64");
    if (b64_pos) {
        is_base64 = 1;
        *b64_pos = 0; /* truncate metadata before ;base64 */
    }

    /* Extract MIME type */
    char mime[64] = "text/plain";
    if (meta[0]) {
        /* If there are parameters like charset=..., isolate the MIME */
        char *semi = strchr(meta, ';');
        if (semi) *semi = 0;

        /* Trim whitespace */
        char *m = meta;
        while (*m == ' ') m++;
        if (m[0]) {
            strncpy(mime, m, sizeof(mime) - 1);
            mime[sizeof(mime) - 1] = 0;
        }
    }

    if (out_mime && max_mime_len > 0) {
        strncpy(out_mime, mime, max_mime_len - 1);
        out_mime[max_mime_len - 1] = 0;
    }

    const char *payload = comma + 1;
    int payload_len = (int)strlen(payload);

    int decoded = 0;
    if (is_base64) {
        decoded = base64_decode(payload, payload_len, out_buf, (int)max_out_len);
        if (decoded < 0) return DATA_URL_ERR_BAD_BASE64;
    } else {
        decoded = url_percent_decode(payload, payload_len, out_buf, (int)max_out_len);
    }

    if (decoded < 0) return DATA_URL_ERR_INVALID;
    if ((unsigned int)decoded >= max_out_len && is_base64 && payload_len > (int)max_out_len) {
        return DATA_URL_ERR_OVERFLOW;
    }

    /* Null terminate if space permits (safe for text callers) */
    if ((unsigned int)decoded < max_out_len) {
        out_buf[decoded] = 0;
    }

    if (out_len) *out_len = (unsigned int)decoded;
    return DATA_URL_OK;
}
