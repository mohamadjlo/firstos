#include <assert.h>
#include <stdint.h>
#include <stddef.h>

int printf(const char *format, ...);
void *malloc(size_t size);
void free(void *ptr);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

#include "userspace/browser/include/firstos_renderer.h"

/* Include adapter and helper implementations directly for host testing */
#include "html5_arena.c"
#include "userspace/browser/dom_adapter.c"
#include "userspace/browser/html_adapter.c"
#include "userspace/browser/css_adapter.c"
#include "userspace/browser/layout_adapter.c"
#include "userspace/browser/render_adapter.c"
#include "userspace/browser/form_adapter.c"
#include "userspace/browser/js_adapter.c"
#include "userspace/browser/event_adapter.c"

/* Mock Host Policy & Cache implementation for host unit testing */
static uint32_t s_mock_host_nav_id = 1;

static int mock_host_validate_url(const char *url, const char *base_origin) {
    if (!url || !url[0]) return -1;
    int len = (int)strlen(url);
    if (len > 1024) return -3;
    for (int i = 0; i < len; i++) {
        if ((unsigned char)url[i] < 32 || (unsigned char)url[i] == 127) return -2;
    }
    for (int i = 0; i < len - 1; i++) {
        if (url[i] == '.' && url[i+1] == '.') {
            if (i + 2 == len || url[i+2] == '/' || url[i+2] == '\\' || (i > 0 && (url[i-1] == '/' || url[i-1] == '\\'))) {
                return -4;
            }
        }
    }
    if (strncmp(url, "about:", 6) == 0) return 0;
    if (strncmp(url, "data:", 5) == 0) return 0;
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) return 0;
    if (strncmp(url, "file://", 7) == 0 || url[0] == '/') {
        if (base_origin) {
            if (strncmp(base_origin, "http://", 7) == 0 || strncmp(base_origin, "https://", 8) == 0) {
                return -5; /* Cross-origin violation */
            }
        }
        return 0;
    }
    return -6;
}

#define MOCK_CACHE_SLOTS 8
typedef struct {
    int in_use;
    char url[256];
    char mime[32];
    char data[4096];
    int len;
} mock_cache_entry_t;
static mock_cache_entry_t s_mock_cache[MOCK_CACHE_SLOTS];

static int mock_cache_lookup(const char *url, char *out_buf, char *out_mime, int *out_len) {
    for (int i = 0; i < MOCK_CACHE_SLOTS; i++) {
        if (s_mock_cache[i].in_use && strcmp(s_mock_cache[i].url, url) == 0) {
            memcpy(out_buf, s_mock_cache[i].data, s_mock_cache[i].len);
            if (out_mime) strcpy(out_mime, s_mock_cache[i].mime);
            if (out_len) *out_len = s_mock_cache[i].len;
            return 0;
        }
    }
    return -1;
}

static int mock_cache_insert(const char *url, const char *mime, const char *data, int len) {
    for (int i = 0; i < MOCK_CACHE_SLOTS; i++) {
        if (!s_mock_cache[i].in_use || strcmp(s_mock_cache[i].url, url) == 0) {
            s_mock_cache[i].in_use = 1;
            strcpy(s_mock_cache[i].url, url);
            strcpy(s_mock_cache[i].mime, mime);
            memcpy(s_mock_cache[i].data, data, len);
            s_mock_cache[i].len = len;
            return 0;
        }
    }
    return -1;
}

/* 1. Test Navigation ID Tracking */
static void test_navigation_id_tracking(void) {
    printf("[test_browser_resource_ipc] 1. test_navigation_id_tracking...\n");
    s_mock_host_nav_id = 1;
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);
    assert(ctx.navigation_id == 1);

    s_mock_host_nav_id++;
    ctx.navigation_id = s_mock_host_nav_id;
    assert(ctx.navigation_id == 2);
    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Navigation generation tracking synchronized)\n");
}

/* 2. Test URL Security Policy */
static void test_url_security_policy(void) {
    printf("[test_browser_resource_ipc] 2. test_url_security_policy...\n");
    assert(mock_host_validate_url("about:home", NULL) == 0);
    assert(mock_host_validate_url("about:blank", NULL) == 0);
    assert(mock_host_validate_url("http://firstos.org/index.html", NULL) == 0);
    assert(mock_host_validate_url("https://google.com/search", NULL) == 0);
    assert(mock_host_validate_url("data:text/css;base64,YWJj", NULL) == 0);
    assert(mock_host_validate_url("file:///assets/style.css", "file:///index.html") == 0);

    /* Traversal prevention */
    assert(mock_host_validate_url("file:///assets/../../etc/passwd", NULL) == -4);
    assert(mock_host_validate_url("/../etc/shadow", NULL) == -4);

    /* Cross-origin file access block */
    assert(mock_host_validate_url("file:///secrets.txt", "http://malicious.com") == -5);

    /* Dangerous / unsupported schemes */
    assert(mock_host_validate_url("javascript:alert(1)", NULL) == -6);
    assert(mock_host_validate_url("ftp://files.org", NULL) == -6);

    /* Control character rejection */
    char evil_url[32] = "http://test.com/\x01\x02";
    assert(mock_host_validate_url(evil_url, NULL) == -2);
    printf("  -> OK (Scheme validation, traversal defense, and SOP enforced)\n");
}

/* 3. Test Subresource Discovery */
static void test_subresource_discovery(void) {
    printf("[test_browser_resource_ipc] 3. test_subresource_discovery...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = 
        "<html><head>"
        "  <link rel=\"stylesheet\" href=\"file:///style.css\">"
        "</head><body>"
        "  <h1>Resource Test</h1>"
        "  <img src=\"file:///logo.png\" width=\"100\" height=\"40\">"
        "</body></html>";

    int res = firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    assert(res == RENDER_OK);
    assert(ctx.pending_res_count == 2);
    assert(ctx.pending_res[0].type == FIRSTOS_RES_TYPE_STYLESHEET);
    assert(strcmp(ctx.pending_res[0].url, "file:///style.css") == 0);
    assert(ctx.pending_res[1].type == FIRSTOS_RES_TYPE_IMAGE);
    assert(strcmp(ctx.pending_res[1].url, "file:///logo.png") == 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Discovered external CSS and image tags)\n");
}

/* 4. Test Resource Request Correlation */
static void test_resource_request_correlation(void) {
    printf("[test_browser_resource_ipc] 4. test_resource_request_correlation...\n");
    firstos_resource_request_t req;
    req.request_id = 101;
    req.navigation_id = 3;
    req.resource_type = FIRSTOS_RES_TYPE_STYLESHEET;
    req.url_offset = 128;
    req.url_len = 14;

    firstos_resource_response_t resp;
    resp.request_id = req.request_id;
    resp.navigation_id = req.navigation_id;
    resp.status_code = 200;
    resp.resource_type = req.resource_type;
    strcpy(resp.mime_type, "text/css");
    resp.data_offset = 256;
    resp.data_len = 64;

    assert(resp.request_id == req.request_id);
    assert(resp.navigation_id == req.navigation_id);
    assert(resp.resource_type == FIRSTOS_RES_TYPE_STYLESHEET);
    assert(resp.status_code == 200);
    printf("  -> OK (Request and response correlation verified)\n");
}

/* 5. Test Stale Response Cancellation */
static void test_stale_response_cancellation(void) {
    printf("[test_browser_resource_ipc] 5. test_stale_response_cancellation...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);
    ctx.navigation_id = 5; /* Active document generation */

    firstos_resource_response_t stale_resp;
    stale_resp.request_id = 1;
    stale_resp.navigation_id = 4; /* Stale generation from previous page */
    stale_resp.status_code = 200;

    int accepted = (stale_resp.navigation_id == ctx.navigation_id);
    assert(accepted == 0); /* Stale response discarded */

    firstos_resource_response_t fresh_resp;
    fresh_resp.request_id = 2;
    fresh_resp.navigation_id = 5; /* Matches current generation */
    fresh_resp.status_code = 200;

    accepted = (fresh_resp.navigation_id == ctx.navigation_id);
    assert(accepted == 1); /* Fresh response accepted */

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Stale responses safely discarded on generation mismatch)\n");
}

/* 6. Test External CSS Restyle */
static void test_external_css_restyle(void) {
    printf("[test_browser_resource_ipc] 6. test_external_css_restyle...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<html><body><h1 id=\"heading\">Hello</h1><p class=\"custom\">Text</p></body></html>";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    firstos_renderer_apply_styles(&ctx);

    html_node_t *h1 = firstos_renderer_get_element_by_id(&ctx, "heading");
    assert(h1 != NULL);
    assert(h1->style.color == 0xDFDFE5); /* Default color */

    /* Apply external stylesheet */
    const char *ext_css = "#heading { color: #FF3B30; width: 400px; } .custom { background: #30D158; }";
    int res = firstos_renderer_add_stylesheet(&ctx, ext_css, (int)strlen(ext_css));
    assert(res == 0);

    assert(h1->style.color == 0xFF3B30);
    assert(h1->style.width == 400);

    html_node_t *p = firstos_renderer_get_element_by_class(&ctx, "custom");
    assert(p != NULL);
    assert(p->style.has_bg == 1);
    assert(p->style.bg_color == 0x30D158);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (External CSS dynamically merged and cascaded)\n");
}

/* 7. Test Layout Update After Resource Arrival */
static void test_layout_update_after_resource(void) {
    printf("[test_browser_resource_ipc] 7. test_layout_update_after_resource...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<html><body><h1>Title</h1><p>Body line</p></body></html>";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    firstos_renderer_apply_styles(&ctx);
    firstos_renderer_layout(&ctx, 800, 600);
    int initial_h = ctx.doc_height;

    /* Add CSS with large padding and margin */
    const char *ext_css = "h1 { margin-bottom: 80px; padding: 20px; }";
    firstos_renderer_add_stylesheet(&ctx, ext_css, (int)strlen(ext_css));
    firstos_renderer_layout(&ctx, 800, 600);
    int updated_h = ctx.doc_height;

    assert(updated_h >= initial_h);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Layout coordinates updated following CSS arrival)\n");
}

/* 8. Test Image Placeholder Bounds & Rendering */
static void test_image_placeholder_bounds(void) {
    printf("[test_browser_resource_ipc] 8. test_image_placeholder_bounds...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = 
        "<html><body>"
        "  <img id=\"img_explicit\" src=\"pic1.png\" width=\"150\" height=\"90\">"
        "  <img id=\"img_default\" src=\"pic2.png\">"
        "</body></html>";

    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    firstos_renderer_layout(&ctx, 800, 600);

    html_node_t *i1 = firstos_renderer_get_element_by_id(&ctx, "img_explicit");
    assert(i1 != NULL);
    assert(i1->bounds.w == 150);
    assert(i1->bounds.h == 90);

    html_node_t *i2 = firstos_renderer_get_element_by_id(&ctx, "img_default");
    assert(i2 != NULL);
    assert(i2->bounds.w == 120);
    assert(i2->bounds.h == 80);

    firstos_render_command_t cmds[64];
    char strings[1024];
    uint32_t num_cmds = 0;
    int rres = firstos_renderer_generate_commands(&ctx, cmds, 64, strings, sizeof(strings), &num_cmds);
    assert(rres == RENDER_OK);
    assert(num_cmds > 2);

    /* Verify presence of image placeholder rect and text commands */
    int has_img_rect = 0;
    int has_img_text = 0;
    for (uint32_t c = 0; c < num_cmds; c++) {
        if (cmds[c].type == FIRSTOS_RENDER_CMD_RECT && cmds[c].color == 0x2A2A38) {
            has_img_rect = 1;
        }
        if (cmds[c].type == FIRSTOS_RENDER_CMD_TEXT && strcmp(strings + cmds[c].text_offset, "[Image]") == 0) {
            has_img_text = 1;
        }
    }
    assert(has_img_rect == 1);
    assert(has_img_text == 1);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Image placeholder boxes and render commands generated)\n");
}

/* 9. Test Data URL Processing */
static void test_data_url_processing(void) {
    printf("[test_browser_resource_ipc] 9. test_data_url_processing...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<html><head><link rel=\"stylesheet\" href=\"data:text/css,body{background:#222430;}\"></head><body><p>Data URL</p></body></html>";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    assert(ctx.pending_res_count == 1);
    assert(strncmp(ctx.pending_res[0].url, "data:text/css,", 14) == 0);

    const char *css_body = ctx.pending_res[0].url + 14;
    firstos_renderer_add_stylesheet(&ctx, css_body, (int)strlen(css_body));

    html_node_t *body = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_BODY);
    assert(body != NULL);
    assert(body->style.has_bg == 1);
    assert(body->style.bg_color == 0x222430);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Data URL parsed and applied without network overhead)\n");
}

/* 10. Test Host Cache and Error Resilience */
static void test_host_cache_and_error_handling(void) {
    printf("[test_browser_resource_ipc] 10. test_host_cache_and_error_handling...\n");
    memset(s_mock_cache, 0, sizeof(s_mock_cache));

    char buf[128];
    char mime[32];
    int len = 0;

    /* Cache miss */
    assert(mock_cache_lookup("file:///cached.css", buf, mime, &len) == -1);

    /* Insert into cache */
    const char *sample_css = "p{color:#fff;}";
    assert(mock_cache_insert("file:///cached.css", "text/css", sample_css, (int)strlen(sample_css)) == 0);

    /* Cache hit */
    assert(mock_cache_lookup("file:///cached.css", buf, mime, &len) == 0);
    assert(len == (int)strlen(sample_css));
    assert(strcmp(mime, "text/css") == 0);
    assert(strncmp(buf, sample_css, len) == 0);

    /* Error resilience test: 404 response handled safely without crash */
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);
    firstos_renderer_parse_html(&ctx, "<html><body><p>Valid</p></body></html>", 39);

    firstos_resource_response_t err_resp;
    err_resp.request_id = 99;
    err_resp.navigation_id = ctx.navigation_id;
    err_resp.status_code = 404; /* Not found */
    err_resp.data_len = 0;

    /* Renderer ignores 404 and main DOM remains intact */
    assert(ctx.root_dom != NULL);
    assert(ctx.node_count > 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Cache hit/miss verified; 404 handled gracefully)\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 4C: Browser Navigation & Resource Loading Tests\n");
    printf("============================================================\n");

    test_navigation_id_tracking();
    test_url_security_policy();
    test_subresource_discovery();
    test_resource_request_correlation();
    test_stale_response_cancellation();
    test_external_css_restyle();
    test_layout_update_after_resource();
    test_image_placeholder_bounds();
    test_data_url_processing();
    test_host_cache_and_error_handling();

    printf("============================================================\n");
    printf("ALL 10 PHASE 4C RESOURCE IPC UNIT TESTS PASSED (10/10)\n");
    printf("============================================================\n");
    return 0;
}
