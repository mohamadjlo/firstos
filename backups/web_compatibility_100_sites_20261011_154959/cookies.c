#include "cookies.h"
#include "include/firstos/security/browser_security.h"
#include "fs.h"
#include "serial.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static cookie_entry_t cookie_jar[MAX_COOKIES];
static int cookies_initialized = 0;

#define str_eq_ci browser_security_str_eq_ci
#define str_ends_with_ci browser_security_str_ends_with_ci
#define str_starts_with browser_security_str_starts_with

void cookies_init(void) {
    if (cookies_initialized) return;
    memset(cookie_jar, 0, sizeof(cookie_jar));
    cookies_initialized = 1;
    cookies_load();
}

int cookies_get_count(void) {
    int cnt = 0;
    for (int i = 0; i < MAX_COOKIES; i++) {
        if (cookie_jar[i].used) cnt++;
    }
    return cnt;
}

const cookie_entry_t *cookies_get_entry(int index) {
    int cur = 0;
    for (int i = 0; i < MAX_COOKIES; i++) {
        if (cookie_jar[i].used) {
            if (cur == index) return &cookie_jar[i];
            cur++;
        }
    }
    return NULL;
}

void cookies_clear_all(void) {
    memset(cookie_jar, 0, sizeof(cookie_jar));
    cookies_save();
}

void cookies_delete_entry(int index) {
    int cur = 0;
    for (int i = 0; i < MAX_COOKIES; i++) {
        if (cookie_jar[i].used) {
            if (cur == index) {
                memset(&cookie_jar[i], 0, sizeof(cookie_entry_t));
                cookies_save();
                return;
            }
            cur++;
        }
    }
}

void cookies_save(void) {
    static char buf[4096];
    int pos = 0;
    buf[0] = 0;

    for (int i = 0; i < MAX_COOKIES; i++) {
        if (cookie_jar[i].used) {
            int n = snprintf(buf + pos, sizeof(buf) - pos, "%s\t%s\t%s\t%s\t%d\t%d\n",
                             cookie_jar[i].domain,
                             cookie_jar[i].path,
                             cookie_jar[i].name,
                             cookie_jar[i].value,
                             cookie_jar[i].secure,
                             cookie_jar[i].http_only);
            if (n > 0 && pos + n < (int)sizeof(buf) - 1) {
                pos += n;
            } else {
                break;
            }
        }
    }
    if (pos > 0) {
        fs_write_at("/cookies.txt", buf, (u32)pos);
    }
}

void cookies_load(void) {
    static char buf[4096];
    int read_bytes = fs_read_at("/cookies.txt", buf, sizeof(buf) - 1);
    if (read_bytes <= 0) return;
    buf[read_bytes] = 0;

    char *line = buf;
    while (line && *line) {
        char *next_line = strchr(line, '\n');
        if (next_line) *next_line = 0;

        char dom[64] = "", path[64] = "", name[64] = "", val[192] = "";
        int sec = 0, h_only = 0;

        char *t1 = strchr(line, '\t');
        if (t1) {
            *t1 = 0; strncpy(dom, line, sizeof(dom) - 1);
            char *t2 = strchr(t1 + 1, '\t');
            if (t2) {
                *t2 = 0; strncpy(path, t1 + 1, sizeof(path) - 1);
                char *t3 = strchr(t2 + 1, '\t');
                if (t3) {
                    *t3 = 0; strncpy(name, t2 + 1, sizeof(name) - 1);
                    char *t4 = strchr(t3 + 1, '\t');
                    if (t4) {
                        *t4 = 0; strncpy(val, t3 + 1, sizeof(val) - 1);
                        char *t5 = strchr(t4 + 1, '\t');
                        if (t5) {
                            *t5 = 0; sec = atoi(t4 + 1);
                            h_only = atoi(t5 + 1);
                        } else {
                            sec = atoi(t4 + 1);
                        }
                    } else {
                        strncpy(val, t3 + 1, sizeof(val) - 1);
                    }
                }
            }
        }

        if (name[0] && dom[0]) {
            /* Insert into jar */
            int slot = -1;
            for (int i = 0; i < MAX_COOKIES; i++) {
                if (cookie_jar[i].used &&
                    strcmp(cookie_jar[i].name, name) == 0 &&
                    str_eq_ci(cookie_jar[i].domain, dom) &&
                    strcmp(cookie_jar[i].path, path) == 0) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                for (int i = 0; i < MAX_COOKIES; i++) {
                    if (!cookie_jar[i].used) { slot = i; break; }
                }
            }
            if (slot >= 0) {
                cookie_entry_t *c = &cookie_jar[slot];
                strncpy(c->domain, dom, sizeof(c->domain) - 1);
                strncpy(c->path, path[0] ? path : "/", sizeof(c->path) - 1);
                strncpy(c->name, name, sizeof(c->name) - 1);
                strncpy(c->value, val, sizeof(c->value) - 1);
                c->secure = sec;
                c->http_only = h_only;
                c->used = 1;
            }
        }

        if (next_line) line = next_line + 1;
        else break;
    }
}

void cookies_parse_set_cookie(const char *header_val, const char *req_host, const char *req_path, int is_https) {
    if (!cookies_initialized) cookies_init();
    if (!header_val || !req_host) return;

    const char *p = header_val;
    while (*p == ' ') p++;

    /* Extract name=val */
    char name[64];
    char val[192];
    int ni = 0;
    while (*p && *p != '=' && *p != ';' && ni < 63) name[ni++] = *p++;
    name[ni] = 0;
    if (*p == '=') p++;

    int vi = 0;
    while (*p && *p != ';' && *p != '\r' && *p != '\n' && vi < 191) val[vi++] = *p++;
    val[vi] = 0;

    char domain[64];
    strncpy(domain, req_host, sizeof(domain) - 1);
    domain[sizeof(domain) - 1] = 0;

    char path[64] = "/";
    if (req_path && req_path[0]) {
        strncpy(path, req_path, sizeof(path) - 1);
        path[sizeof(path) - 1] = 0;
        char *slash = strrchr(path, '/');
        if (slash && slash != path) *slash = 0;
    }

    int secure = 0;
    int http_only = 0;

    /* Parse remaining attributes */
    while (*p && *p != '\r' && *p != '\n') {
        if (*p == ';') p++;
        while (*p == ' ') p++;
        if (!*p || *p == '\r' || *p == '\n') break;

        char attr[32];
        int ai = 0;
        while (*p && *p != '=' && *p != ';' && *p != '\r' && *p != '\n' && ai < 31) attr[ai++] = *p++;
        attr[ai] = 0;

        char attr_val[64] = "";
        if (*p == '=') {
            p++;
            int avi = 0;
            while (*p && *p != ';' && *p != '\r' && *p != '\n' && avi < 63) attr_val[avi++] = *p++;
            attr_val[avi] = 0;
        }

        if (str_eq_ci(attr, "domain")) {
            const char *d = attr_val;
            if (d[0] == '.') d++;
            strncpy(domain, d, sizeof(domain) - 1);
            domain[sizeof(domain) - 1] = 0;
        } else if (str_eq_ci(attr, "path")) {
            strncpy(path, attr_val[0] ? attr_val : "/", sizeof(path) - 1);
            path[sizeof(path) - 1] = 0;
        } else if (str_eq_ci(attr, "secure")) {
            secure = 1;
        } else if (str_eq_ci(attr, "httponly")) {
            http_only = 1;
        }
    }

    /* Reject secure cookies set over non-HTTPS */
    if (secure && !is_https) return;

    /* Validate domain scoping */
    if (!browser_security_validate_cookie_domain(domain, req_host)) {
        serial_write("[cookie] rejected invalid domain scoping\n");
        return;
    }

    /* Find matching slot or empty slot */
    int target_slot = -1;
    for (int i = 0; i < MAX_COOKIES; i++) {
        if (cookie_jar[i].used &&
            strcmp(cookie_jar[i].name, name) == 0 &&
            str_eq_ci(cookie_jar[i].domain, domain) &&
            strcmp(cookie_jar[i].path, path) == 0) {
            target_slot = i;
            break;
        }
    }
    if (target_slot < 0) {
        for (int i = 0; i < MAX_COOKIES; i++) {
            if (!cookie_jar[i].used) { target_slot = i; break; }
        }
    }

    if (target_slot >= 0) {
        cookie_entry_t *c = &cookie_jar[target_slot];
        strncpy(c->name, name, sizeof(c->name) - 1);
        strncpy(c->value, val, sizeof(c->value) - 1);
        strncpy(c->domain, domain, sizeof(c->domain) - 1);
        strncpy(c->path, path, sizeof(c->path) - 1);
        c->secure = secure;
        c->http_only = http_only;
        c->used = 1;

        serial_write("[cookie] stored: ");
        serial_write(name);
        serial_write(" (domain=");
        serial_write(domain);
        serial_write(" secure=");
        serial_write(secure ? "1" : "0");
        serial_write(" httponly=");
        serial_write(http_only ? "1" : "0");
        serial_write(")\n");

        cookies_save();
    }
}

int cookie_set(const char *domain, const char *path, const char *name, const char *value, int secure, int http_only) {
    if (!domain || !name || !value) return 0;
    int target_slot = -1;
    for (int i = 0; i < MAX_COOKIES; i++) {
        if (cookie_jar[i].used &&
            strcmp(cookie_jar[i].name, name) == 0 &&
            str_eq_ci(cookie_jar[i].domain, domain) &&
            strcmp(cookie_jar[i].path, path ? path : "/") == 0) {
            target_slot = i;
            break;
        }
    }
    if (target_slot < 0) {
        for (int i = 0; i < MAX_COOKIES; i++) {
            if (!cookie_jar[i].used) { target_slot = i; break; }
        }
    }
    if (target_slot < 0) return 0;

    cookie_entry_t *c = &cookie_jar[target_slot];
    memset(c, 0, sizeof(cookie_entry_t));
    strncpy(c->name, name, sizeof(c->name) - 1);
    strncpy(c->value, value, sizeof(c->value) - 1);
    strncpy(c->domain, domain, sizeof(c->domain) - 1);
    strncpy(c->path, (path && path[0]) ? path : "/", sizeof(c->path) - 1);
    c->secure = secure;
    c->http_only = http_only;
    c->used = 1;
    return 1;
}

int cookies_build_header(const char *host, const char *path, int is_https, char *out_buf, int max_len) {
    if (!cookies_initialized) cookies_init();
    if (!host || !out_buf || max_len < 16) return 0;

    int pos = 0;
    int first = 1;

    for (int i = 0; i < MAX_COOKIES; i++) {
        if (!cookie_jar[i].used) continue;
        cookie_entry_t *c = &cookie_jar[i];

        /* Domain matching: host must equal or end with cookie domain */
        if (!str_ends_with_ci(host, c->domain)) continue;

        /* Path matching: request path must start with cookie path */
        if (path && !str_starts_with(path, c->path)) continue;

        /* Secure flag matching */
        if (c->secure && !is_https) continue;

        if (first) {
            int n = snprintf(out_buf + pos, max_len - pos, "Cookie: %s=%s", c->name, c->value);
            if (n > 0 && pos + n < max_len) { pos += n; first = 0; }
        } else {
            int n = snprintf(out_buf + pos, max_len - pos, "; %s=%s", c->name, c->value);
            if (n > 0 && pos + n < max_len) { pos += n; }
        }
    }

    if (!first && pos + 2 < max_len) {
        out_buf[pos++] = '\r';
        out_buf[pos++] = '\n';
        out_buf[pos] = 0;
        return pos;
    }

    out_buf[0] = 0;
    return 0;
}

int cookies_js_get(const char *host, const char *path, char *out_buf, int max_len) {
    if (!cookies_initialized) cookies_init();
    if (!out_buf || max_len <= 0) return 0;
    out_buf[0] = 0;
    if (!host) return 0;

    int pos = 0;
    int first = 1;

    for (int i = 0; i < MAX_COOKIES; i++) {
        if (!cookie_jar[i].used || cookie_jar[i].http_only) continue;
        cookie_entry_t *c = &cookie_jar[i];

        if (!str_ends_with_ci(host, c->domain)) continue;
        if (path && !str_starts_with(path, c->path)) continue;

        if (first) {
            int n = snprintf(out_buf + pos, max_len - pos, "%s=%s", c->name, c->value);
            if (n > 0 && pos + n < max_len) { pos += n; first = 0; }
        } else {
            int n = snprintf(out_buf + pos, max_len - pos, "; %s=%s", c->name, c->value);
            if (n > 0 && pos + n < max_len) { pos += n; }
        }
    }
    return pos;
}

void cookies_js_set(const char *host, const char *path, const char *cookie_str, int is_https) {
    cookies_parse_set_cookie(cookie_str, host ? host : "localhost", path ? path : "/", is_https);
}
