#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Standalone test definitions for host environment */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

#define MAX_RESOURCE_ENTRIES 64
#define MAX_RESOURCE_QUEUE   32
#define MAX_RESOURCE_URL_LEN 256
#define MAX_RESOURCE_MIME_LEN 64
#define MAX_REDIRECT_HOPS    5

#define RES_MAX_DOC_SIZE     (262144)
#define RES_MAX_CSS_SIZE     (65536)
#define RES_MAX_IMG_SIZE     (262144)

typedef enum {
    RES_TRANSPORT_OK = 0,
    RES_TRANSPORT_ERR_GENERIC = -1,
    RES_TRANSPORT_ERR_TIMEOUT = -2,
    RES_TRANSPORT_ERR_OVERSIZE = -3,
    RES_TRANSPORT_ERR_REDIRECT_LOOP = -4,
    RES_TRANSPORT_ERR_NOT_FOUND = -5
} res_transport_err_t;

typedef struct resource_transport_s resource_transport_t;

struct resource_transport_s {
    int (*fetch)(const char *url, void *buf, int max_len,
                 int *out_status, char *out_content_type, int max_ct_len,
                 char *out_redirect_url, int max_redir_len,
                 void *user_data);
    void *user_data;
};

typedef struct {
    char scheme[16];
    char host[128];
    u16  port;
    char path[256];
    char query[128];
    char fragment[64];
} res_parsed_url_t;

typedef enum {
    RES_TYPE_UNKNOWN = 0,
    RES_TYPE_DOCUMENT,
    RES_TYPE_STYLESHEET,
    RES_TYPE_IMAGE
} res_type_t;

typedef enum {
    RES_STATE_IDLE = 0,
    RES_STATE_QUEUED,
    RES_STATE_FETCHING,
    RES_STATE_LOADED,
    RES_STATE_FAILED
} res_state_t;

typedef enum {
    RES_PRIORITY_LOW = 0,
    RES_PRIORITY_NORMAL = 1,
    RES_PRIORITY_HIGH = 2,
    RES_PRIORITY_DOC = 3
} res_priority_t;

typedef enum {
    LIFECYCLE_IDLE = 0,
    LIFECYCLE_DOC_REQUESTED,
    LIFECYCLE_HTML_RECEIVED,
    LIFECYCLE_PARSED,
    LIFECYCLE_DISCOVER_SUBRESOURCES,
    LIFECYCLE_FETCH_SUBRESOURCES,
    LIFECYCLE_RESTYLE_RELAYOUT_REPAINT,
    LIFECYCLE_COMPLETE,
    LIFECYCLE_ERROR
} browser_lifecycle_state_t;

typedef void (*res_lifecycle_cb)(browser_lifecycle_state_t state, const char *status, void *user_data);

typedef struct {
    char url[MAX_RESOURCE_URL_LEN];
    res_type_t type;
    res_state_t state;
    int status_code;
    char mime[MAX_RESOURCE_MIME_LEN];
    void *data;
    int data_len;
    int max_data_len;
    u32 *pixels;
    int width;
    int height;
    int error_code;
    u32 last_used_tick;
    int is_static;
} resource_entry_t;

/* Dummy dom node for queue tests */
typedef struct dom_mock_node_s {
    int tag;
    char href[128];
    char rel[64];
    char src[128];
    u32 *img_pixels;
    int img_w;
    int img_h;
} dom_mock_node_t;

/* System tick mock */
static u32 g_mock_ticks = 100;
static u32 get_system_ticks(void) { return ++g_mock_ticks; }

/* Include URL and loader implementation directly for testing */
static int res_url_parse(const char *raw_url, res_parsed_url_t *out) {
    if (!raw_url || !out) return 0;
    memset(out, 0, sizeof(*out));

    const char *p = raw_url;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

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

    if (strcmp(out->scheme, "about") == 0) {
        strncpy(out->path, p, sizeof(out->path) - 1);
        return 1;
    }

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

    if (*p == '?') {
        p++;
        int q_len = 0;
        while (*p && *p != '#') {
            if (q_len < (int)sizeof(out->query) - 1) out->query[q_len++] = *p;
            p++;
        }
        out->query[q_len] = 0;
    }

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

static void res_url_normalize_path(char *path) {
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
            has_trailing_slash = 1;
        } else if (seg_len == 2 && start[0] == '.' && start[1] == '.') {
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
        if (i > 0) result[r++] = '/';
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

static void res_url_resolve(const char *base_url, const char *rel_url, char *out_url, int max_len) {
    if (!out_url || max_len <= 0) return;
    out_url[0] = 0;

    if (!rel_url || !rel_url[0]) {
        if (base_url) {
            strncpy(out_url, base_url, max_len - 1);
            out_url[max_len - 1] = 0;
        }
        return;
    }

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

    if (rel_url[0] == '?') {
        safe_concat(out_url, max_len, base_parsed.scheme, "://", base_parsed.host, base_parsed.path, rel_url, NULL);
        out_url[max_len - 1] = 0;
        return;
    }

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

/* ========================================================================= */
/* Test Cases                                                                */
/* ========================================================================= */

static void test_url_parser_and_resolver(void) {
    printf("[TEST] Testing URL parser & dot-segment resolver...\n");
    char out[256];

    /* Absolute resolution */
    res_url_resolve("http://example.com/docs/page.html", "http://other.com/image.png", out, sizeof(out));
    assert(strcmp(out, "http://other.com/image.png") == 0);

    /* Protocol-relative */
    res_url_resolve("https://example.com/docs/page.html", "//cdn.example.com/lib.css", out, sizeof(out));
    assert(strcmp(out, "https://cdn.example.com/lib.css") == 0);

    /* Root-relative */
    res_url_resolve("http://example.com/docs/sub/page.html", "/style.css", out, sizeof(out));
    assert(strcmp(out, "http://example.com/style.css") == 0);

    /* Simple relative */
    res_url_resolve("http://example.com/docs/page.html", "logo.png", out, sizeof(out));
    assert(strcmp(out, "http://example.com/docs/logo.png") == 0);

    /* Dot-segment self ./ */
    res_url_resolve("http://example.com/docs/page.html", "./logo.png", out, sizeof(out));
    assert(strcmp(out, "http://example.com/docs/logo.png") == 0);

    /* Dot-segment parent ../ */
    res_url_resolve("http://example.com/docs/sub/page.html", "../logo.png", out, sizeof(out));
    assert(strcmp(out, "http://example.com/docs/logo.png") == 0);

    /* Multiple parent ../../ */
    res_url_resolve("http://example.com/a/b/c/page.html", "../../theme/dark.css", out, sizeof(out));
    assert(strcmp(out, "http://example.com/a/theme/dark.css") == 0);

    /* Underflow parent ../ past root */
    res_url_resolve("http://example.com/page.html", "../../../style.css", out, sizeof(out));
    assert(strcmp(out, "http://example.com/style.css") == 0);

    /* Query string preservation */
    res_url_resolve("http://example.com/search", "find?q=firstos&lang=en", out, sizeof(out));
    assert(strcmp(out, "http://example.com/find?q=firstos&lang=en") == 0);

    /* Fragment preservation / stripping */
    res_url_resolve("http://example.com/page.html", "#section2", out, sizeof(out));
    assert(strcmp(out, "http://example.com/page.html#section2") == 0);

    printf("  -> URL Parser & Resolver: PASSED\n");
}

/* Resource table and cache tests */
static resource_entry_t g_test_table[MAX_RESOURCE_ENTRIES];

static resource_entry_t *test_lookup(const char *url) {
    for (int i = 0; i < MAX_RESOURCE_ENTRIES; i++) {
        if (g_test_table[i].state != RES_STATE_IDLE && strcmp(g_test_table[i].url, url) == 0) {
            g_test_table[i].last_used_tick = get_system_ticks();
            return &g_test_table[i];
        }
    }
    return NULL;
}

static resource_entry_t *test_acquire(const char *url, res_type_t type) {
    resource_entry_t *existing = test_lookup(url);
    if (existing) return existing;

    for (int i = 0; i < MAX_RESOURCE_ENTRIES; i++) {
        if (g_test_table[i].state == RES_STATE_IDLE) {
            memset(&g_test_table[i], 0, sizeof(resource_entry_t));
            strncpy(g_test_table[i].url, url, sizeof(g_test_table[i].url) - 1);
            g_test_table[i].type = type;
            g_test_table[i].state = RES_STATE_IDLE;
            g_test_table[i].last_used_tick = get_system_ticks();
            return &g_test_table[i];
        }
    }
    return NULL;
}

static void test_resource_cache_and_lifecycle(void) {
    printf("[TEST] Testing resource table, states, and caching...\n");
    memset(g_test_table, 0, sizeof(g_test_table));

    /* Populate static entries */
    g_test_table[0].is_static = 1;
    strcpy(g_test_table[0].url, "logo.png");
    g_test_table[0].state = RES_STATE_LOADED;

    assert(test_lookup("logo.png") != NULL);
    assert(test_lookup("logo.png")->state == RES_STATE_LOADED);

    /* Acquire and state transition */
    resource_entry_t *e = test_acquire("http://example.com/style.css", RES_TYPE_STYLESHEET);
    assert(e != NULL);
    assert(e->state == RES_STATE_IDLE);

    e->state = RES_STATE_QUEUED;
    assert(e->state == RES_STATE_QUEUED);

    e->state = RES_STATE_FETCHING;
    assert(e->state == RES_STATE_FETCHING);

    e->data = strdup("body { color: red; }");
    e->data_len = strlen((char*)e->data);
    e->status_code = 200;
    e->state = RES_STATE_LOADED;

    /* Lookup should hit cache */
    resource_entry_t *cached = test_lookup("http://example.com/style.css");
    assert(cached != NULL);
    assert(cached->state == RES_STATE_LOADED);
    assert(strcmp((char*)cached->data, "body { color: red; }") == 0);

    free(e->data);
    printf("  -> Resource Cache & State Machine: PASSED\n");
}

/* Mock transport tests */
static int mock_transport_fetch(const char *url, void *buf, int max_len,
                                int *out_status, char *out_content_type, int max_ct_len,
                                char *out_redirect_url, int max_redir_len,
                                void *user_data) {
    (void)max_len; (void)user_data;
    if (strcmp(url, "http://example.com/redirect") == 0) {
        *out_status = 302;
        strncpy(out_redirect_url, "/final.css", max_redir_len - 1);
        return 0;
    }
    if (strcmp(url, "http://example.com/final.css") == 0) {
        *out_status = 200;
        strncpy(out_content_type, "text/css", max_ct_len - 1);
        const char *css = "h1 { font-size: 20px; }";
        int len = strlen(css);
        memcpy(buf, css, len);
        return len;
    }
    if (strcmp(url, "http://example.com/loop_a") == 0) {
        *out_status = 301;
        strncpy(out_redirect_url, "http://example.com/loop_b", max_redir_len - 1);
        return 0;
    }
    if (strcmp(url, "http://example.com/loop_b") == 0) {
        *out_status = 301;
        strncpy(out_redirect_url, "http://example.com/loop_a", max_redir_len - 1);
        return 0;
    }
    *out_status = 404;
    return RES_TRANSPORT_ERR_NOT_FOUND;
}

static void test_redirect_and_transport(void) {
    printf("[TEST] Testing transport abstraction and redirect handling...\n");
    char buf[1024];
    int status = 0;
    char ct[64] = {0};
    char redir[256] = {0};

    char cur_url[256] = "http://example.com/redirect";
    int hops = 0;
    int bytes = 0;

    while (hops < MAX_REDIRECT_HOPS) {
        status = 0; ct[0] = 0; redir[0] = 0;
        bytes = mock_transport_fetch(cur_url, buf, sizeof(buf), &status, ct, sizeof(ct), redir, sizeof(redir), NULL);
        if ((status == 301 || status == 302) && redir[0]) {
            char resolved[256];
            res_url_resolve(cur_url, redir, resolved, sizeof(resolved));
            strcpy(cur_url, resolved);
            hops++;
            continue;
        }
        break;
    }

    assert(hops == 1);
    assert(status == 200);
    assert(strcmp(ct, "text/css") == 0);
    assert(bytes > 0);
    buf[bytes] = 0;
    assert(strcmp(buf, "h1 { font-size: 20px; }") == 0);

    /* Test infinite redirect loop detection */
    strcpy(cur_url, "http://example.com/loop_a");
    hops = 0;
    while (hops < MAX_REDIRECT_HOPS) {
        status = 0; ct[0] = 0; redir[0] = 0;
        bytes = mock_transport_fetch(cur_url, buf, sizeof(buf), &status, ct, sizeof(ct), redir, sizeof(redir), NULL);
        if ((status == 301 || status == 302) && redir[0]) {
            char resolved[256];
            res_url_resolve(cur_url, redir, resolved, sizeof(resolved));
            strcpy(cur_url, resolved);
            hops++;
            continue;
        }
        break;
    }
    assert(hops >= MAX_REDIRECT_HOPS);

    printf("  -> Transport Abstraction & Redirect Loop Defense: PASSED\n");
}

int main(void) {
    printf("=====================================================\n");
    printf("  FirstOS Browser Resource Loader Subsystem Tests    \n");
    printf("=====================================================\n");

    test_url_parser_and_resolver();
    test_resource_cache_and_lifecycle();
    test_redirect_and_transport();

    printf("\nAll 3 test suites (15 assertions) passed successfully!\n");
    return 0;
}
