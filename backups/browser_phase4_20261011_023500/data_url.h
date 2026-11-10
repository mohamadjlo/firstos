#ifndef DATA_URL_H
#define DATA_URL_H

#include "io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    URL_SCHEME_HTTP = 0,
    URL_SCHEME_HTTPS,
    URL_SCHEME_DATA,
    URL_SCHEME_BLOB,
    URL_SCHEME_FILE,
    URL_SCHEME_ABOUT,
    URL_SCHEME_JAVASCRIPT,
    URL_SCHEME_UNKNOWN
} url_scheme_t;

/* Return URL scheme enum from url string */
url_scheme_t url_detect_scheme(const char *url);

/* Return scheme name as string: "http", "https", "data", "blob", "file", "about", "javascript", "unknown" */
const char *url_scheme_name(url_scheme_t scheme);

/* Returns 1 if scheme requires network transmission (http, https), 0 otherwise */
int url_is_network_scheme(url_scheme_t scheme);

/* Base64 decoding helper: decodes base64 src into dst.
 * Handles standard and URL-safe base64, skips whitespace, stops at padding '=' or invalid chars.
 * Returns number of decoded bytes, or -1 on invalid input. */
int base64_decode(const char *src, int src_len, unsigned char *dst, int max_dst_len);

/* URL percent-decoding helper: decodes %XX into raw bytes.
 * Returns number of decoded bytes. */
int url_percent_decode(const char *src, int src_len, unsigned char *dst, int max_dst_len);

/* Return codes for data_url_decode */
#define DATA_URL_OK               0
#define DATA_URL_ERR_INVALID     -1
#define DATA_URL_ERR_OVERFLOW    -2
#define DATA_URL_ERR_BAD_BASE64  -3

/* Decode RFC 2397 Data URL:
 * data:[<mediatype>][;base64],<data>
 *
 * Parameters:
 *   data_url: The full data URL string
 *   out_mime: Buffer to receive the MIME type (e.g. "text/css", "image/png"). Defaults to "text/plain" if omitted.
 *   max_mime_len: Size of out_mime buffer
 *   out_buf: Buffer to receive decoded data payload
 *   max_out_len: Size of out_buf
 *   out_len: Pointer to unsigned int receiving the number of decoded bytes
 *
 * Returns DATA_URL_OK (0) on success, or a negative error code.
 */
int data_url_decode(const char *data_url,
                    char *out_mime, int max_mime_len,
                    unsigned char *out_buf, unsigned int max_out_len,
                    unsigned int *out_len);

#ifdef __cplusplus
}
#endif

#endif /* DATA_URL_H */
