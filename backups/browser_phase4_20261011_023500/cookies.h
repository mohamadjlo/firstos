#ifndef COOKIES_H
#define COOKIES_H

#include "io.h"

#define MAX_COOKIES 64

typedef struct {
    char name[64];
    char value[192];
    char domain[64];
    char path[64];
    int secure;
    int http_only;
    u32 expires;
    int used;
} cookie_entry_t;

void cookies_init(void);
void cookies_parse_set_cookie(const char *header_val, const char *req_host, const char *req_path, int is_https);
int  cookies_build_header(const char *host, const char *path, int is_https, char *out_buf, int max_len);
int  cookie_set(const char *domain, const char *path, const char *name, const char *value, int secure, int http_only);
void cookies_save(void);
void cookies_load(void);
void cookies_clear_all(void);
void cookies_delete_entry(int index);

/* JavaScript document.cookie interface */
int  cookies_js_get(const char *host, const char *path, char *out_buf, int max_len);
void cookies_js_set(const char *host, const char *path, const char *cookie_str, int is_https);

int  cookies_get_count(void);
const cookie_entry_t *cookies_get_entry(int index);

#endif /* COOKIES_H */
