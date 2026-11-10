#include "html5_resources.h"
#include "image.h"
#include "heap.h"
#include "serial.h"
#include "task.h"
#include "string.h"
#include "stdio.h"
#include "http_v11.h"
#include "bnet_error.h"
#include "http_body.h"

/* Bounded resource table */
static resource_entry_t g_resource_table[MAX_RESOURCE_ENTRIES];
static int g_resource_count = 0;

/* Prioritized request queue */
typedef struct {
    char url[MAX_RESOURCE_URL_LEN];
    res_type_t type;
    res_priority_t priority;
    html_node_t *target_node;
    int redirect_count;
    int active;
} queue_entry_t;

static queue_entry_t g_queue[MAX_RESOURCE_QUEUE];
static int g_queue_count = 0;

/* Subsystem state */
static browser_lifecycle_state_t g_lifecycle_state = LIFECYCLE_IDLE;
static res_lifecycle_cb g_lifecycle_cb = NULL;
static void *g_lifecycle_user_data = NULL;

static resource_transport_t g_active_transport;
static int g_has_custom_transport = 0;

/* Budgeting constants */
#define RESOURCE_BUDGET_TICKS 50u /* 500 ms at 100 Hz */
static u32 g_fetch_start_tick = 0;
static int g_fetch_active = 0;

static int resource_budget_expired(void) {
    return g_fetch_active && (get_system_ticks() - g_fetch_start_tick) >= RESOURCE_BUDGET_TICKS;
}

/* ========================================================================= */
/* URL Parsing & Dot-Segment Normalization (RFC 3986)                        */
/* ========================================================================= */

int res_url_parse(const char *raw_url, res_parsed_url_t *out) {
    if (!raw_url || !out) return 0;
    memset(out, 0, sizeof(*out));

    const char *p = raw_url;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    /* Extract Scheme */
    const char *colon = strstr(p, "://");
    if (colon) {
        int scheme_len = (int)(colon - p);
        if (scheme_len < (int)sizeof(out->scheme)) {
            memcpy(out->scheme, p, scheme_len);
            out->scheme[scheme_len] = 0;
        }
        p = colon + 3;
    } else if (strncmp(p, "about:", 6) == 0) {
        strcpy(out->scheme, "about");
        p += 6;
    } else {
        strcpy(out->scheme, "http");
    }

    /* For about: URLs */
    if (strcmp(out->scheme, "about") == 0) {
        strncpy(out->path, p, sizeof(out->path) - 1);
        return 1;
    }

    /* Extract Host & Port */
    int h = 0;
    while (*p && *p != '/' && *p != '?' && *p != '#' && *p != ':') {
        if (h < (int)sizeof(out->host) - 1) out->host[h++] = *p;
        p++;
    }
    out->host[h] = 0;

    out->port = (strcmp(out->scheme, "https") == 0) ? 443 : 80;
    if (*p == ':') {
        p++;
        u32 port = 0;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
        if (port > 0 && port <= 65535) out->port = (u16)port;
    }

    /* Extract Path */
    if (*p == '/') {
        int path_len = 0;
        while (*p && *p != '?' && *p != '#') {
            if (path_len < (int)sizeof(out->path) - 1) out->path[path_len++] = *p;
            p++;
        }
        out->path[path_len] = 0;
    } else {
        strcpy(out->path, "/");
    }

    /* Extract Query */
    if (*p == '?') {
        p++;
        int q_len = 0;
        while (*p && *p != '#') {
            if (q_len < (int)sizeof(out->query) - 1) out->query[q_len++] = *p;
            p++;
        }
        out->query[q_len] = 0;
    }

    /* Extract Fragment */
    if (*p == '#') {
        p++;
        int f_len = 0;
        while (*p) {
            if (f_len < (int)sizeof(out->fragment) - 1) out->fragment[f_len++] = *p;
            p++;
        }
        out->fragment[f_len] = 0;
    }

    return 1;
}

/* Remove dot segments according to RFC 3986 section 5.2.4 */
void res_url_normalize_path(char *path) {
    if (!path || !path[0]) return;

    char segments[32][64];
    int seg_count = 0;
    int is_absolute = (path[0] == '/');
    int has_trailing_slash = 0;
    int len = (int)strlen(path);
    if (len > 0 && path[len - 1] == '/') has_trailing_slash = 1;

    const char *p = path;
    if (is_absolute) p++;

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        const char *start = p;
        while (*p && *p != '/') p++;
        int seg_len = (int)(p - start);
        if (seg_len <= 0) continue;

        if (seg_len == 1 && start[0] == '.') {
            /* Skip '.' segment */
            has_trailing_slash = 1;
        } else if (seg_len == 2 && start[0] == '.' && start[1] == '.') {
            /* Pop '..' segment */
            if (seg_count > 0) seg_count--;
            has_trailing_slash = 1;
        } else {
            if (seg_count < 32) {
                int copy_len = seg_len < 63 ? seg_len : 63;
                memcpy(segments[seg_count], start, copy_len);
                segments[seg_count][copy_len] = 0;
                seg_count++;
                has_trailing_slash = (*p == '/');
            }
        }
    }

    char result[256];
    int r = 0;
    if (is_absolute) result[r++] = '/';

    for (int i = 0; i < seg_count; i++) {
        int slen = (int)strlen(segments[i]);
        if (r + slen + 2 >= (int)sizeof(result)) break;
        if (i > 0 || !is_absolute) {
            if (i > 0) result[r++] = '/';
        }
        memcpy(result + r, segments[i], slen);
        r += slen;
    }

    if (has_trailing_slash && (seg_count > 0 || !is_absolute)) {
        if (r < (int)sizeof(result) - 1) result[r++] = '/';
    }

    if (r == 0 && is_absolute) result[r++] = '/';
    result[r] = 0;

    strncpy(path, result, 255);
    path[255] = 0;
}

static void safe_concat(char *dst, int max_len,
                        const char *s1, const char *s2, const char *s3,
                        const char *s4, const char *s5, const char *s6) {
    if (!dst || max_len <= 0) return;
    dst[0] = 0;
    int cur = 0;
    const char *arr[6] = {s1, s2, s3, s4, s5, s6};
    for (int i = 0; i < 6; i++) {
        const char *s = arr[i];
        if (!s) continue;
        while (*s && cur < max_len - 1) {
            dst[cur++] = *s++;
        }
    }
    dst[cur] = 0;
}

void res_url_resolve(const char *base_url, const char *rel_url, char *out_url, int max_len) {
    if (!out_url || max_len <= 0) return;
    out_url[0] = 0;

    if (!rel_url || !rel_url[0]) {
        if (base_url) {
            strncpy(out_url, base_url, max_len - 1);
            out_url[max_len - 1] = 0;
        }
        return;
    }

    /* Check if rel_url is data: or javascript: URI */
    if (strncmp(rel_url, "data:", 5) == 0 || strncmp(rel_url, "javascript:", 11) == 0) {
        strncpy(out_url, rel_url, max_len - 1);
        out_url[max_len - 1] = 0;
        return;
    }

    /* Check if rel_url has an explicit scheme */
    if (strncmp(rel_url, "http://", 7) == 0 ||
        strncmp(rel_url, "https://", 8) == 0 ||
        strncmp(rel_url, "about:", 6) == 0 ||
        strncmp(rel_url, "file://", 7) == 0) {
        res_parsed_url_t parsed;
        res_url_parse(rel_url, &parsed);
        res_url_normalize_path(parsed.path);
        if (strcmp(parsed.scheme, "about") == 0) {
            safe_concat(out_url, max_len, "about:", parsed.path, NULL, NULL, NULL, NULL);
        } else if (parsed.query[0]) {
            safe_concat(out_url, max_len, parsed.scheme, "://", parsed.host, parsed.path, "?", parsed.query);
        } else {
            safe_concat(out_url, max_len, parsed.scheme, "://", parsed.host, parsed.path, NULL, NULL);
        }
        out_url[max_len - 1] = 0;
        return;
    }

    /* Protocol-relative (//example.com/foo) */
    if (rel_url[0] == '/' && rel_url[1] == '/') {
        char scheme[16] = "http";
        if (base_url && strncmp(base_url, "https://", 8) == 0) strcpy(scheme, "https");
        safe_concat(out_url, max_len, scheme, ":", rel_url, NULL, NULL, NULL);
        out_url[max_len - 1] = 0;
        return;
    }

    if (!base_url || !base_url[0]) {
        strncpy(out_url, rel_url, max_len - 1);
        out_url[max_len - 1] = 0;
        return;
    }

    res_parsed_url_t base_parsed;
    res_url_parse(base_url, &base_parsed);

    /* Fragment-only (#hash) */
    if (rel_url[0] == '#') {
        if (base_parsed.query[0]) {
            safe_concat(out_url, max_len, base_parsed.scheme, "://", base_parsed.host, base_parsed.path, "?", base_parsed.query);
            int cur_len = strlen(out_url);
            safe_concat(out_url + cur_len, max_len - cur_len, rel_url, NULL, NULL, NULL, NULL, NULL);
        } else {
            safe_concat(out_url, max_len, base_parsed.scheme, "://", base_parsed.host, base_parsed.path, rel_url, NULL);
        }
        out_url[max_len - 1] = 0;
        return;
    }

    /* Query-only (?search=val) */
    if (rel_url[0] == '?') {
        safe_concat(out_url, max_len, base_parsed.scheme, "://", base_parsed.host, base_parsed.path, rel_url, NULL);
        out_url[max_len - 1] = 0;
        return;
    }

    /* Root-relative (/path/to/res) */
    if (rel_url[0] == '/') {
        char combined_path[256];
        strncpy(combined_path, rel_url, sizeof(combined_path) - 1);
        combined_path[sizeof(combined_path) - 1] = 0;
        char *q = strchr(combined_path, '?');
        char query_str[128] = {0};
        if (q) {
            strncpy(query_str, q + 1, sizeof(query_str) - 1);
            *q = 0;
        }
        char *f = strchr(combined_path, '#');
        if (f) *f = 0;

        res_url_normalize_path(combined_path);
        if (query_str[0]) {
            safe_concat(out_url, max_len, base_parsed.scheme, "://", base_parsed.host, combined_path, "?", query_str);
        } else {
            safe_concat(out_url, max_len, base_parsed.scheme, "://", base_parsed.host, combined_path, NULL, NULL);
        }
        out_url[max_len - 1] = 0;
        return;
    }

    /* Relative path (dir/file, ./file, ../file) */
    char dir[256] = "/";
    const char *last_slash = strrchr(base_parsed.path, '/');
    if (last_slash) {
        int dlen = (int)(last_slash - base_parsed.path) + 1;
        if (dlen < (int)sizeof(dir)) {
            memcpy(dir, base_parsed.path, dlen);
            dir[dlen] = 0;
        }
    }

    char full_rel[256];
    strncpy(full_rel, rel_url, sizeof(full_rel) - 1);
    full_rel[sizeof(full_rel) - 1] = 0;

    char *q = strchr(full_rel, '?');
    char query_str[128] = {0};
    if (q) {
        strncpy(query_str, q + 1, sizeof(query_str) - 1);
        *q = 0;
    }
    char *f = strchr(full_rel, '#');
    if (f) *f = 0;

    char combined[512];
    safe_concat(combined, sizeof(combined), dir, full_rel, NULL, NULL, NULL, NULL);
    res_url_normalize_path(combined);

    if (query_str[0]) {
        safe_concat(out_url, max_len, base_parsed.scheme, "://", base_parsed.host, combined, "?", query_str);
    } else {
        safe_concat(out_url, max_len, base_parsed.scheme, "://", base_parsed.host, combined, NULL, NULL);
    }
    out_url[max_len - 1] = 0;
}

void html5_url_resolve(const char *base_url, const char *rel_url, char *out_url, int max_len) {
    res_url_resolve(base_url, rel_url, out_url, max_len);
}

/* ========================================================================= */
/* Sample Demo Image Generator                                              */
/* ========================================================================= */

static u32 *create_sample_logo(int w, int h, u32 primary_col) {
    u32 *pix = (u32*)kmalloc(w * h * sizeof(u32));
    if (!pix) return NULL;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int dx = x - w / 2;
            int dy = y - h / 2;
            int r2 = dx * dx + dy * dy;
            int max_r = (w < h ? w : h) / 2 - 2;

            if (r2 <= max_r * max_r) {
                if (r2 >= (max_r - 2) * (max_r - 2)) {
                    pix[y * w + x] = 0xFFFFFFFF;
                } else if (abs(dx) <= 3 || abs(dy) <= 3) {
                    pix[y * w + x] = 0xFFFFFFFF;
                } else {
                    pix[y * w + x] = primary_col | 0xFF000000;
                }
            } else {
                pix[y * w + x] = 0x00000000;
            }
        }
    }
    return pix;
}

/* ========================================================================= */
/* Default FirstOS Network Transport                                         */
/* ========================================================================= */

static int default_transport_fetch(const char *url, void *buf, int max_len,
                                  int *out_status, char *out_content_type, int max_ct_len,
                                  char *out_redirect_url, int max_redir_len,
                                  void *user_data) {
    (void)user_data;
    if (!url || !buf || max_len <= 0) return RES_TRANSPORT_ERR_GENERIC;

    if (out_status) *out_status = 0;
    if (out_content_type && max_ct_len > 0) out_content_type[0] = 0;
    if (out_redirect_url && max_redir_len > 0) out_redirect_url[0] = 0;

    /* Handle file:// scheme locally */
    if (strncmp(url, "file://", 7) == 0) {
        int iw = 0, ih = 0;
        u8 *pix = image_load(url + 7, &iw, &ih);
        if (pix) {
            /* Pack pixel data into buffer if it fits */
            int byte_size = iw * ih * 4;
            if (byte_size <= max_len) {
                memcpy(buf, pix, byte_size);
                if (out_status) *out_status = 200;
                if (out_content_type) strncpy(out_content_type, "image/raw-rgba", max_ct_len - 1);
                image_free(pix);
                return byte_size;
            }
            image_free(pix);
            return RES_TRANSPORT_ERR_OVERSIZE;
        }
        return RES_TRANSPORT_ERR_NOT_FOUND;
    }

    /* Handle http:// and https:// via FirstOS http_v11 */
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        http_request_t req;
        memset(&req, 0, sizeof(req));
        req.url = url;
        req.method = HTTP_METHOD_GET;
        req.follow_redirects = 0; /* Let loader inspect redirect location */
        req.timeout_polls = 100000;
        req.max_response_size = (u32)max_len;

        serial_write("[html5_resources] fetch: ");
        serial_write(url);
        serial_write("\n");

        bnet_error_t net_err = BNET_OK;
        http_response_t *res = http_execute_ex(&req, &net_err);
        if (!res) {
            serial_write("[html5_resources] fetch failed: ");
            serial_write(bnet_error_name(net_err));
            serial_write("\n");

            if (net_err == BNET_ERR_DNS_TIMEOUT || net_err == BNET_ERR_TCP_TIMEOUT || net_err == BNET_ERR_HTTP_RECV_TIMEOUT) {
                return RES_TRANSPORT_ERR_TIMEOUT;
            }
            return RES_TRANSPORT_ERR_GENERIC;
        }

        if (out_status) *out_status = res->status_code;
        if (out_content_type && res->content_type[0]) {
            strncpy(out_content_type, res->content_type, max_ct_len - 1);
            out_content_type[max_ct_len - 1] = 0;
        }
        if (out_redirect_url && res->location[0]) {
            strncpy(out_redirect_url, res->location, max_redir_len - 1);
            out_redirect_url[max_redir_len - 1] = 0;
        }

        if (res->is_truncated || !res->is_complete) {
            serial_write("[html5_resources] fetch incomplete or truncated url=");
            serial_write(url);
            serial_write("\n");
            http_response_free(res);
            return RES_TRANSPORT_ERR_INCOMPLETE;
        }

        int copied = 0;
        if (res->body && res->body_len > 0) {
            copied = (int)res->body_len;
            if (copied > max_len) {
                http_response_free(res);
                return RES_TRANSPORT_ERR_OVERSIZE;
            }
            memcpy(buf, res->body, copied);
        }

        serial_write("[html5_resources] fetched status=");
        serial_write_hex((u32)res->status_code);
        serial_write(" bytes=");
        serial_write_hex((u32)copied);
        serial_write("\n");

        http_response_free(res);
        return copied;
    }

    return RES_TRANSPORT_ERR_GENERIC;
}

/* ========================================================================= */
/* Subsystem Lifecycle & Transport API                                       */
/* ========================================================================= */

void html5_resources_init(void) {
    memset(g_resource_table, 0, sizeof(g_resource_table));
    memset(g_queue, 0, sizeof(g_queue));
    g_resource_count = 0;
    g_queue_count = 0;
    g_lifecycle_state = LIFECYCLE_IDLE;
    g_fetch_active = 0;

    /* Initialize default transport */
    g_active_transport.fetch = default_transport_fetch;
    g_active_transport.user_data = NULL;
    g_has_custom_transport = 0;

    /* Pre-populate standard demo icons as permanent static assets */
    resource_entry_t *res0 = &g_resource_table[0];
    strncpy(res0->url, "logo.png", sizeof(res0->url) - 1);
    res0->type = RES_TYPE_IMAGE;
    res0->state = RES_STATE_LOADED;
    res0->status_code = 200;
    res0->width = 48;
    res0->height = 48;
    res0->pixels = create_sample_logo(48, 48, 0x007AFF);
    res0->is_static = 1;

    resource_entry_t *res1 = &g_resource_table[1];
    strncpy(res1->url, "icon.png", sizeof(res1->url) - 1);
    res1->type = RES_TYPE_IMAGE;
    res1->state = RES_STATE_LOADED;
    res1->status_code = 200;
    res1->width = 64;
    res1->height = 64;
    res1->pixels = create_sample_logo(64, 64, 0xFF7139);
    res1->is_static = 1;

    g_resource_count = 2;
    serial_write("[html5_resources] Subsystem initialized with bounded cache\n");
}

void html5_resources_clear(void) {
    for (int i = 0; i < MAX_RESOURCE_ENTRIES; i++) {
        resource_entry_t *e = &g_resource_table[i];
        if (e->state != RES_STATE_IDLE && !e->is_static) {
            if (e->pixels) {
                image_free((u8*)e->pixels);
                e->pixels = NULL;
            }
            if (e->data) {
                kfree(e->data);
                e->data = NULL;
            }
            memset(e, 0, sizeof(*e));
            e->state = RES_STATE_IDLE;
        }
    }
    g_resource_count = 2;
    html5_resource_queue_clear();
    g_lifecycle_state = LIFECYCLE_IDLE;
}

void html5_resources_set_transport(const resource_transport_t *transport) {
    if (transport && transport->fetch) {
        g_active_transport = *transport;
        g_has_custom_transport = 1;
    } else {
        g_active_transport.fetch = default_transport_fetch;
        g_active_transport.user_data = NULL;
        g_has_custom_transport = 0;
    }
}

void html5_resources_set_lifecycle_callback(res_lifecycle_cb cb, void *user_data) {
    g_lifecycle_cb = cb;
    g_lifecycle_user_data = user_data;
}

void html5_resources_notify_lifecycle(browser_lifecycle_state_t state, const char *status) {
    g_lifecycle_state = state;
    if (g_lifecycle_cb) {
        g_lifecycle_cb(state, status, g_lifecycle_user_data);
    }
}

browser_lifecycle_state_t html5_resources_get_lifecycle_state(void) {
    return g_lifecycle_state;
}

/* ========================================================================= */
/* Resource Table / Cache Management                                         */
/* ========================================================================= */

resource_entry_t *html5_resource_lookup(const char *url) {
    if (!url || !url[0]) return NULL;

    for (int i = 0; i < MAX_RESOURCE_ENTRIES; i++) {
        resource_entry_t *e = &g_resource_table[i];
        if (e->state != RES_STATE_IDLE) {
            if (strcmp(e->url, url) == 0 || strstr(url, e->url) != NULL) {
                e->last_used_tick = get_system_ticks();
                return e;
            }
        }
    }
    return NULL;
}

resource_entry_t *html5_resource_acquire(const char *url, res_type_t type) {
    if (!url || !url[0]) return NULL;

    resource_entry_t *existing = html5_resource_lookup(url);
    if (existing) return existing;

    /* Find an empty slot */
    for (int i = 0; i < MAX_RESOURCE_ENTRIES; i++) {
        if (g_resource_table[i].state == RES_STATE_IDLE) {
            resource_entry_t *e = &g_resource_table[i];
            memset(e, 0, sizeof(*e));
            strncpy(e->url, url, sizeof(e->url) - 1);
            e->type = type;
            e->state = RES_STATE_IDLE;
            e->last_used_tick = get_system_ticks();
            if (i >= g_resource_count) g_resource_count = i + 1;
            return e;
        }
    }

    /* Cache full: evict LRU non-static entry */
    int oldest_idx = -1;
    u32 oldest_tick = 0xFFFFFFFF;
    for (int i = 0; i < MAX_RESOURCE_ENTRIES; i++) {
        resource_entry_t *e = &g_resource_table[i];
        if (!e->is_static && e->state != RES_STATE_FETCHING) {
            if (e->last_used_tick < oldest_tick) {
                oldest_tick = e->last_used_tick;
                oldest_idx = i;
            }
        }
    }

    if (oldest_idx >= 0) {
        resource_entry_t *e = &g_resource_table[oldest_idx];
        if (e->pixels) image_free((u8*)e->pixels);
        if (e->data) kfree(e->data);
        memset(e, 0, sizeof(*e));
        strncpy(e->url, url, sizeof(e->url) - 1);
        e->type = type;
        e->state = RES_STATE_IDLE;
        e->last_used_tick = get_system_ticks();
        return e;
    }

    return NULL;
}

/* ========================================================================= */
/* Content-Type Dispatch & Fetch Engine                                      */
/* ========================================================================= */

static res_type_t detect_content_type(const char *mime, const char *url) {
    if (mime && mime[0]) {
        if (strstr(mime, "text/css")) return RES_TYPE_STYLESHEET;
        if (strstr(mime, "image/")) return RES_TYPE_IMAGE;
        if (strstr(mime, "text/html") || strstr(mime, "application/xhtml")) return RES_TYPE_DOCUMENT;
    }

    if (url) {
        if (strstr(url, ".css")) return RES_TYPE_STYLESHEET;
        if (strstr(url, ".png") || strstr(url, ".jpg") || strstr(url, ".jpeg") ||
            strstr(url, ".bmp") || strstr(url, ".ico")) return RES_TYPE_IMAGE;
        if (strstr(url, ".html") || strstr(url, ".htm")) return RES_TYPE_DOCUMENT;
    }

    return RES_TYPE_UNKNOWN;
}

int html5_resource_load(const char *url, res_type_t type, resource_entry_t **out_entry) {
    if (!url || !url[0]) return RES_TRANSPORT_ERR_GENERIC;

    resource_entry_t *entry = html5_resource_acquire(url, type);
    if (!entry) return RES_TRANSPORT_ERR_GENERIC;
    if (out_entry) *out_entry = entry;

    if (entry->state == RES_STATE_LOADED) return RES_TRANSPORT_OK;

    entry->state = RES_STATE_FETCHING;
    int max_sz = (type == RES_TYPE_STYLESHEET) ? RES_MAX_CSS_SIZE :
                 (type == RES_TYPE_DOCUMENT)   ? RES_MAX_DOC_SIZE : RES_MAX_IMG_SIZE;

    char *buf = (char*)kmalloc(max_sz);
    if (!buf) {
        entry->state = RES_STATE_FAILED;
        entry->error_code = RES_TRANSPORT_ERR_GENERIC;
        return RES_TRANSPORT_ERR_GENERIC;
    }

    char current_url[MAX_RESOURCE_URL_LEN];
    strncpy(current_url, url, sizeof(current_url) - 1);
    current_url[sizeof(current_url) - 1] = 0;

    int hops = 0;
    int status = 0;
    char mime[MAX_RESOURCE_MIME_LEN] = {0};
    char redir[MAX_RESOURCE_URL_LEN] = {0};
    int bytes = 0;

    while (hops < MAX_REDIRECT_HOPS) {
        status = 0;
        mime[0] = 0;
        redir[0] = 0;

        bytes = g_active_transport.fetch(current_url, buf, max_sz,
                                         &status, mime, sizeof(mime),
                                         redir, sizeof(redir),
                                         g_active_transport.user_data);

        /* Handle basic HTTP Redirects (301, 302, 303, 307, 308) */
        if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && redir[0]) {
            char resolved_redir[MAX_RESOURCE_URL_LEN];
            res_url_resolve(current_url, redir, resolved_redir, sizeof(resolved_redir));
            strncpy(current_url, resolved_redir, sizeof(current_url) - 1);
            current_url[sizeof(current_url) - 1] = 0;
            hops++;
            continue;
        }
        break;
    }

    if (hops >= MAX_REDIRECT_HOPS) {
        kfree(buf);
        entry->state = RES_STATE_FAILED;
        entry->status_code = status;
        entry->error_code = RES_TRANSPORT_ERR_REDIRECT_LOOP;
        return RES_TRANSPORT_ERR_REDIRECT_LOOP;
    }

    entry->status_code = status;
    strncpy(entry->mime, mime, sizeof(entry->mime) - 1);

    if (bytes <= 0 || (status >= 400)) {
        http_body_t mock_body;
        memset(&mock_body, 0, sizeof(mock_body));
        mock_body.http_status = status;
        mock_body.capacity = max_sz;
        mock_body.length = (bytes > 0) ? (unsigned int)bytes : 0;
        mock_body.truncated = (bytes == RES_TRANSPORT_ERR_INCOMPLETE || bytes == RES_TRANSPORT_ERR_OVERSIZE);
        mock_body.complete = 0;
        strncpy(mock_body.content_type, mime, sizeof(mock_body.content_type) - 1);
        http_body_log(url, &mock_body, (bytes == RES_TRANSPORT_ERR_INCOMPLETE) ? "REJECTED_INCOMPLETE" : "FETCH_FAILED");

        kfree(buf);
        entry->state = RES_STATE_FAILED;
        if (bytes == RES_TRANSPORT_ERR_INCOMPLETE) entry->error_code = RES_TRANSPORT_ERR_INCOMPLETE;
        else if (bytes == RES_TRANSPORT_ERR_OVERSIZE) entry->error_code = RES_TRANSPORT_ERR_OVERSIZE;
        else entry->error_code = (status == 404) ? RES_TRANSPORT_ERR_NOT_FOUND : RES_TRANSPORT_ERR_GENERIC;

        /* For images, fall back to synthesized demo badge so UI stays intact */
        if (type == RES_TYPE_IMAGE) {
            u32 col = 0x30D158;
            if (strstr(url, "blue") || strstr(url, "kernel")) col = 0x007AFF;
            else if (strstr(url, "red") || strstr(url, "video")) col = 0xFF453A;
            else if (strstr(url, "yellow") || strstr(url, "warn")) col = 0xFFD60A;

            entry->width = 48;
            entry->height = 48;
            entry->pixels = create_sample_logo(48, 48, col);
            entry->state = RES_STATE_LOADED;
            return RES_TRANSPORT_OK;
        }
        return entry->error_code;
    }

    /* Content-type verification / dispatch */
    res_type_t detected = detect_content_type(mime, current_url);
    if (detected != RES_TYPE_UNKNOWN) entry->type = detected;

    entry->data = buf;
    entry->data_len = bytes;
    entry->max_data_len = max_sz;

    /* Handle Image Decoding */
    if (entry->type == RES_TYPE_IMAGE) {
        if (strcmp(mime, "image/raw-rgba") == 0) {
            /* Already raw RGBA */
            entry->pixels = (u32*)buf;
            entry->width = 48;
            entry->height = 48;
            entry->state = RES_STATE_LOADED;
            return RES_TRANSPORT_OK;
        }

        int iw = 0, ih = 0;
        u8 *pix = image_load_from_memory((const u8*)buf, bytes, &iw, &ih);
        if (pix) {
            entry->pixels = (u32*)pix;
            entry->width = iw;
            entry->height = ih;
            entry->state = RES_STATE_LOADED;
            serial_write("[html5_resources] Decoded network image: ");
            serial_write(url);
            serial_write("\n");
            return RES_TRANSPORT_OK;
        }

        /* Fallback badge on decode failure */
        entry->width = 48;
        entry->height = 48;
        entry->pixels = create_sample_logo(48, 48, 0x007AFF);
        entry->state = RES_STATE_LOADED;
        return RES_TRANSPORT_OK;
    }

    entry->state = RES_STATE_LOADED;
    http_body_t loaded_body;
    memset(&loaded_body, 0, sizeof(loaded_body));
    loaded_body.http_status = status;
    loaded_body.capacity = max_sz;
    loaded_body.length = (unsigned int)bytes;
    loaded_body.complete = 1;
    loaded_body.truncated = 0;
    strncpy(loaded_body.content_type, mime, sizeof(loaded_body.content_type) - 1);
    http_body_log(url, &loaded_body, "LOADED");

    serial_write("[html5_resources] resource complete url=");
    serial_write(url);
    serial_write(" bytes=");
    serial_write_hex((u32)bytes);
    serial_write("\n");
    return RES_TRANSPORT_OK;
}

/* ========================================================================= */
/* Request Queue & Subresource Discovery                                     */
/* ========================================================================= */

int html5_resource_queue_subresource(const char *url, res_type_t type, res_priority_t priority, void *target_node) {
    if (!url || !url[0]) return 0;
    if (g_queue_count >= MAX_RESOURCE_QUEUE) return 0;

    /* Check if already in queue */
    for (int i = 0; i < g_queue_count; i++) {
        if (g_queue[i].active && strcmp(g_queue[i].url, url) == 0) {
            return 1;
        }
    }

    queue_entry_t *q = &g_queue[g_queue_count++];
    strncpy(q->url, url, sizeof(q->url) - 1);
    q->url[sizeof(q->url) - 1] = 0;
    q->type = type;
    q->priority = priority;
    q->target_node = (html_node_t*)target_node;
    q->redirect_count = 0;
    q->active = 1;
    return 1;
}

void html5_resource_queue_clear(void) {
    memset(g_queue, 0, sizeof(g_queue));
    g_queue_count = 0;
}

int html5_resource_queue_pump(int max_fetches) {
    int processed = 0;

    /* Process highest priority items first */
    for (int prio = RES_PRIORITY_DOC; prio >= RES_PRIORITY_LOW; prio--) {
        for (int i = 0; i < g_queue_count; i++) {
            if (processed >= max_fetches || resource_budget_expired()) return processed;

            queue_entry_t *q = &g_queue[i];
            if (q->active && (int)q->priority == prio) {
                resource_entry_t *entry = NULL;
                html5_resource_load(q->url, q->type, &entry);
                if (entry && entry->state == RES_STATE_LOADED && q->target_node) {
                    if (entry->type == RES_TYPE_IMAGE && entry->pixels) {
                        q->target_node->img_pixels = entry->pixels;
                        q->target_node->img_w = entry->width;
                        q->target_node->img_h = entry->height;
                    }
                }
                q->active = 0;
                processed++;
            }
        }
    }

    return processed;
}

static void discover_subresources(html_node_t *node, const char *base_url) {
    if (!node) return;

    /* Discover <link rel="stylesheet" href="..."> */
    if (node->tag == HTML_TAG_LINK && node->href[0] &&
        (strstr(node->rel, "stylesheet") || strstr(node->href, ".css"))) {
        char resolved[MAX_RESOURCE_URL_LEN];
        res_url_resolve(base_url, node->href, resolved, sizeof(resolved));
        html5_resource_queue_subresource(resolved, RES_TYPE_STYLESHEET, RES_PRIORITY_HIGH, node);
    }

    /* Discover <img src="..."> */
    if (node->tag == HTML_TAG_IMG && node->src[0]) {
        char resolved[MAX_RESOURCE_URL_LEN];
        res_url_resolve(base_url, node->src, resolved, sizeof(resolved));
        html5_resource_queue_subresource(resolved, RES_TYPE_IMAGE, RES_PRIORITY_NORMAL, node);
    }

    discover_subresources(node->first_child, base_url);
    discover_subresources(node->next_sibling, base_url);
}

void html5_resource_discover_and_fetch(html_node_t *root, const char *base_url,
                                       char *css_out, int *css_len, int max_css) {
    if (!root) return;

    html5_resources_notify_lifecycle(LIFECYCLE_DISCOVER_SUBRESOURCES, "Discovering linked CSS & images...");
    html5_resource_queue_clear();
    discover_subresources(root, base_url);

    html5_resources_notify_lifecycle(LIFECYCLE_FETCH_SUBRESOURCES, "Fetching queued subresources...");
    g_fetch_start_tick = get_system_ticks();
    g_fetch_active = 1;

    html5_resource_queue_pump(MAX_RESOURCE_QUEUE);
    g_fetch_active = 0;

    /* Aggregate loaded stylesheet data */
    if (css_out && css_len && max_css > 0) {
        for (int i = 0; i < MAX_RESOURCE_ENTRIES; i++) {
            resource_entry_t *e = &g_resource_table[i];
            if (e->state == RES_STATE_LOADED && e->type == RES_TYPE_STYLESHEET && e->data && e->data_len > 0) {
                int avail = max_css - 1 - *css_len;
                if (avail > 0) {
                    int to_copy = e->data_len < avail ? e->data_len : avail;
                    memcpy(css_out + *css_len, e->data, to_copy);
                    *css_len += to_copy;
                    css_out[*css_len] = 0;
                }
            }
        }
    }

    html5_resources_notify_lifecycle(LIFECYCLE_RESTYLE_RELAYOUT_REPAINT, "Restyling and relayout...");
}

/* ========================================================================= */
/* Compatibility Functions                                                   */
/* ========================================================================= */

u32 *html5_resource_get_image(const char *url, int *out_w, int *out_h) {
    if (!url || !url[0]) return NULL;

    resource_entry_t *entry = html5_resource_lookup(url);
    if (!entry) {
        html5_resource_load(url, RES_TYPE_IMAGE, &entry);
    }

    if (entry && entry->state == RES_STATE_LOADED && entry->pixels) {
        if (out_w) *out_w = entry->width;
        if (out_h) *out_h = entry->height;
        return entry->pixels;
    }
    return NULL;
}

void html5_resource_fetch_for_dom(html_node_t *root, const char *base_url) {
    html5_resource_discover_and_fetch(root, base_url, NULL, NULL, 0);
}
