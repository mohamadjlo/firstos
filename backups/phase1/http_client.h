#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

/* Fetch URL via HTTP/1.0 or HTTPS GET.
   Supports both http:// and https:// protocols.
   Injects matching Cookie headers and parses Set-Cookie headers.
   Returns body byte count, 0 on error. */
int http_get(const char *url, char *out_buf, int max_len);

/* Fetch a binary resource body (images, media, stylesheets, scripts). */
int http_get_binary(const char *url, unsigned char *out_buf, int max_len);
int http_get_binary_ex(const char *url, unsigned char *out_buf, int max_len, int *out_is_truncated, unsigned int *out_actual_len);

#endif /* HTTP_CLIENT_H */
