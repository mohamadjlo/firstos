#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

int printf(const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);

#define FB_W 800
#define FB_H 600
static unsigned int g_fb[FB_H][FB_W];

static int g_clip_x = 0, g_clip_y = 0, g_clip_w = FB_W, g_clip_h = FB_H;

void fb_set_clip(int x, int y, int w, int h) {
    g_clip_x = x; g_clip_y = y; g_clip_w = w; g_clip_h = h;
}
void fb_reset_clip(void) {
    g_clip_x = 0; g_clip_y = 0; g_clip_w = FB_W; g_clip_h = FB_H;
}

void fb_putpixel(int x, int y, unsigned int col) {
    if (x < g_clip_x || x >= g_clip_x + g_clip_w || y < g_clip_y || y >= g_clip_y + g_clip_h) return;
    if (x >= 0 && x < FB_W && y >= 0 && y < FB_H) {
        g_fb[y][x] = col | 0xFF000000;
    }
}

void fb_rect(int x, int y, int w, int h, unsigned int col) {
    for (int r = y; r < y + h; r++) {
        for (int c = x; c < x + w; c++) {
            fb_putpixel(c, r, col);
        }
    }
}

void fb_border(int x, int y, int w, int h, unsigned int col) {
    for (int c = x; c < x + w; c++) { fb_putpixel(c, y, col); fb_putpixel(c, y + h - 1, col); }
    for (int r = y; r < y + h; r++) { fb_putpixel(x, r, col); fb_putpixel(x + w - 1, r, col); }
}

void fb_round_rect(int x, int y, int w, int h, int rad, unsigned int col) {
    (void)rad;
    fb_rect(x, y, w, h, col);
}

void fb_round_rect_border(int x, int y, int w, int h, int rad, unsigned int col) {
    (void)rad;
    fb_border(x, y, w, h, col);
}

void font_text(int x, int y, const char *txt, unsigned int col, int size) {
    (void)x; (void)y; (void)txt; (void)col; (void)size;
}

void font_icon(int x, int y, int icon, unsigned int col) {
    (void)x; (void)y; (void)icon; (void)col;
}

void serial_write(const char *s) { (void)s; }
void serial_write_hex(unsigned int val) { (void)val; }
void gui_pump_events(void) {}

int http_get_binary(const char *url, unsigned char *buf, int max) {
    (void)url; (void)buf; (void)max; return 0;
}

/* Track all strings rendered by the text subsystem */
static char g_rendered_texts[128][256];
static int g_rendered_text_count = 0;

static void reset_rendered_texts(void) {
    g_rendered_text_count = 0;
    for (int i = 0; i < 128; i++) {
        g_rendered_texts[i][0] = '\0';
    }
}

#include "../text_engine.h"

void text_engine_init(void) {}

int text_engine_measure(const char *text, font_fallback_chain_t *chain, int scale, int max_w,
                        int *out_ends_with_space, int *out_has_nbsp) {
    (void)chain; (void)max_w;
    if (out_ends_with_space) *out_ends_with_space = 0;
    if (out_has_nbsp) *out_has_nbsp = 0;
    if (!text) return 0;
    int fs = (scale > 0) ? scale : 1;
    int len = 0;
    while (text[len]) len++;
    return len * 8 * fs;
}

void text_engine_render(int x, int y, const char *text, u32 color, int scale,
                        font_fallback_chain_t *chain, void *target_fb) {
    (void)x; (void)y; (void)color; (void)scale; (void)chain; (void)target_fb;
    if (text && g_rendered_text_count < 128) {
        int i = 0;
        while (text[i] && i < 255) {
            g_rendered_texts[g_rendered_text_count][i] = text[i];
            i++;
        }
        g_rendered_texts[g_rendered_text_count][i] = '\0';
        g_rendered_text_count++;
    }
}

int text_engine_hit_test(const char *text, font_fallback_chain_t *chain, int scale,
                         int click_x, int *out_byte_offset) {
    (void)text; (void)chain; (void)scale; (void)click_x;
    if (out_byte_offset) *out_byte_offset = 0;
    return 0;
}

font_fallback_chain_t g_browser_default_font_chain;

void *kmalloc(uint32_t sz) { return malloc(sz); }
void kfree(void *p) { free(p); }

#include "../html5_arena.c"

u32 text_utf8_decode_next(const char **ptr, const char *end) {
    if (!ptr || !*ptr || *ptr >= end) return 0;
    const unsigned char *p = (const unsigned char *)(*ptr);
    unsigned char b0 = *p++;
    *ptr = (const char *)p;
    return (u32)b0;
}

#include "../html5_resources.h"

void html5_resource_discover_and_fetch(html_node_t *root, const char *base_url, char *css_buf, int *css_len, int max_css) {
    (void)root; (void)base_url; (void)css_buf; (void)max_css;
    if (css_len) *css_len = 0;
}

void html5_resources_set_lifecycle_callback(res_lifecycle_cb cb, void *user_data) {
    (void)cb; (void)user_data;
}

#include "../html5_engine.c"

void html5_resource_fetch_for_dom(html_node_t *root, const char *base_url) {
    (void)root; (void)base_url;
}
void html5_script_run_dom_scripts(html_node_t *root, const char *base_url) {
    (void)root; (void)base_url;
}
html5_video_ctx_t *html5_media_create_video_ctx(const char *src, int loop, int w, int h) {
    (void)src; (void)loop; (void)w; (void)h; return NULL;
}
void html5_media_destroy_video_ctx(html5_video_ctx_t *ctx) { (void)ctx; }
void html5_media_render_video(html5_video_ctx_t *ctx, int x, int y, int w, int h) {
    (void)ctx; (void)x; (void)y; (void)w; (void)h;
}

int gui_window_dirty_only = 0;
int html5_media_handle_click(html5_video_ctx_t *ctx, int mx, int my) { (void)ctx; (void)mx; (void)my; return 0; }
void html5_media_init(void) {}
void html5_media_tick(void) {}
void html5_resources_init(void) {}
void html5_script_eval(const char *code, html_node_t *node) { (void)code; (void)node; }
void html5_script_init(void) {}
void html5_url_resolve(const char *base, const char *rel, char *out, int max) {
    if (!out || max <= 0) return;
    if (!rel) { out[0] = 0; return; }
    if (strncmp(rel, "http://", 7) == 0 || strncmp(rel, "https://", 8) == 0) {
        strncpy(out, rel, max - 1);
        out[max - 1] = 0;
    } else {
        snprintf(out, max, "%s/%s", base ? base : "", rel);
    }
}
int needs_redraw = 0;
int point_in_rect(int px, int py, rect_t r) {
    return (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h);
}

/* --------------------------------------------------------------------------
 * Test 1: Non-visual Tag Classification & Defaults
 * -------------------------------------------------------------------------- */
static void test_non_visual_tags_defaults(void) {
    printf("[TEST 1] Testing non-visual tag classification & default styles...\n");
    html5_engine_init();

    html_tag_t non_visual_tags[] = {
        HTML_TAG_HEAD,
        HTML_TAG_STYLE,
        HTML_TAG_SCRIPT,
        HTML_TAG_META,
        HTML_TAG_LINK,
        HTML_TAG_TITLE
    };

    for (size_t i = 0; i < sizeof(non_visual_tags)/sizeof(non_visual_tags[0]); i++) {
        html_node_t *n = alloc_node(non_visual_tags[i]);
        assert(n != NULL);
        assert(html5_node_is_non_visual(n) == 1);
        assert(n->style.display_none == 1);
        assert(n->style.is_block == 0);
    }

    /* Verify standard tags are not non-visual */
    html_tag_t visual_tags[] = {
        HTML_TAG_DIV, HTML_TAG_P, HTML_TAG_H1, HTML_TAG_SPAN, HTML_TAG_A, HTML_TAG_IMG
    };
    for (size_t i = 0; i < sizeof(visual_tags)/sizeof(visual_tags[0]); i++) {
        html_node_t *n = alloc_node(visual_tags[i]);
        assert(n != NULL);
        assert(html5_node_is_non_visual(n) == 0);
        assert(n->style.display_none == 0);
    }

    printf("  -> Default display_none=1 and non-visual classification verified.\n");
}

/* --------------------------------------------------------------------------
 * Test 2: CSS Override Resistance for Non-visual Elements
 * -------------------------------------------------------------------------- */
static void test_css_override_resistance(void) {
    printf("[TEST 2] Testing CSS override resistance on non-visual nodes...\n");
    html5_engine_init();

    const char *html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<style>head, style, script, meta, title { display: block !important; width: 500px; height: 100px; }</style>"
        "<script>var x = 42;</script>"
        "</head>"
        "<body>"
        "<p>Visible Paragraph</p>"
        "</body>"
        "</html>";

    html5_parse_document(html, strlen(html), "http://example.com");

    /* Scan DOM nodes to verify non-visual nodes kept display_none=1 despite CSS */
    int non_visual_count = 0;
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && html5_node_is_non_visual(n)) {
            non_visual_count++;
            assert(n->style.display_none == 1);
        }
    }
    assert(non_visual_count >= 3); /* head, style, script */

    printf("  -> CSS rules prevented from overriding display_none on non-visual elements.\n");
}

/* --------------------------------------------------------------------------
 * Test 3: Script & Style Content Isolation from Page Painting
 * -------------------------------------------------------------------------- */
static void test_script_style_painting_isolation(void) {
    printf("[TEST 3] Testing script & style content isolation from render pass...\n");
    html5_engine_init();
    reset_rendered_texts();

    const char *html =
        "<html>"
        "<head>"
        "<title>Test Page Title</title>"
        "<style>"
        ".content-box { background-color: #f0f0f0; margin: 10px; font-family: sans-serif; }"
        "body { color: #333333; }"
        "</style>"
        "<script>"
        "function computeMetrics() { console.log('metrics running'); return 42; }"
        "var apiKey = 'secret_12345';"
        "</script>"
        "</head>"
        "<body>"
        "<h1>Article Title</h1>"
        "<p>This is paragraph text that must be visible.</p>"
        "</body>"
        "</html>";

    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);
    html5_render_document(0, 0, 800, 600, 0);

    /* Verify title was extracted */
    assert(strcmp(html5_get_document_title(), "Test Page Title") == 0);

    /* Check that only legitimate body text was rendered */
    int found_h1 = 0;
    int found_p = 0;

    for (int i = 0; i < g_rendered_text_count; i++) {
        const char *t = g_rendered_texts[i];
        /* Must not contain script content */
        assert(strstr(t, "function") == NULL);
        assert(strstr(t, "computeMetrics") == NULL);
        assert(strstr(t, "secret_12345") == NULL);
        /* Must not contain style content */
        assert(strstr(t, "content-box") == NULL);
        assert(strstr(t, "sans-serif") == NULL);
        assert(strstr(t, "background-color") == NULL);

        if (strstr(t, "Article Title") != NULL) found_h1 = 1;
        if (strstr(t, "This is paragraph text") != NULL) found_p = 1;
    }

    assert(found_h1 == 1);
    assert(found_p == 1);
    printf("  -> Script and style contents completely excluded from visual rendering.\n");
}

/* --------------------------------------------------------------------------
 * Test 4: Text Outside <body> Culling
 * -------------------------------------------------------------------------- */
static void test_text_outside_body_culling(void) {
    printf("[TEST 4] Testing that text outside <body> is never rendered...\n");
    html5_engine_init();
    reset_rendered_texts();

    const char *html =
        "Prefix text before HTML\n"
        "<html>\n"
        "Text before head\n"
        "<head>\n"
        "Text inside head\n"
        "<title>Document Title</title>\n"
        "</head>\n"
        "Text between head and body\n"
        "<body>\n"
        "<p>Only this should be visible!</p>\n"
        "</body>\n"
        "Suffix text after body\n"
        "</html>\n"
        "Trailing text outside html";

    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);
    html5_render_document(0, 0, 800, 600, 0);

    int found_valid_p = 0;
    for (int i = 0; i < g_rendered_text_count; i++) {
        const char *t = g_rendered_texts[i];
        assert(strstr(t, "Prefix text") == NULL);
        assert(strstr(t, "Text before head") == NULL);
        assert(strstr(t, "Text inside head") == NULL);
        assert(strstr(t, "Text between head and body") == NULL);
        assert(strstr(t, "Suffix text") == NULL);
        assert(strstr(t, "Trailing text") == NULL);

        if (strstr(t, "Only this should be visible") != NULL) found_valid_p = 1;
    }

    assert(found_valid_p == 1);
    printf("  -> Text outside <body> successfully culled from layout and paint.\n");
}

/* --------------------------------------------------------------------------
 * Test 5: Comments, DOCTYPE, and XML Processing Instructions
 * -------------------------------------------------------------------------- */
static void test_comments_and_doctype(void) {
    printf("[TEST 5] Testing comment, doctype, and processing instruction filtering...\n");
    html5_engine_init();
    reset_rendered_texts();

    const char *html =
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\">\n"
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!-- Leading comment before document -->\n"
        "<html>\n"
        "<head><!-- Comment inside head --></head>\n"
        "<body>\n"
        "<!-- Comment inside body -->\n"
        "<h2>Body Heading</h2>\n"
        "<!-- Multiline comment\n"
        "     that spans multiple lines\n"
        "     and contains <div> tags -->\n"
        "<p>Paragraph after multiline block.</p>\n"
        "</body>\n"
        "</html>";

    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);
    html5_render_document(0, 0, 800, 600, 0);

    for (int i = 0; i < g_rendered_text_count; i++) {
        const char *t = g_rendered_texts[i];
        assert(strstr(t, "Leading comment") == NULL);
        assert(strstr(t, "Comment inside") == NULL);
        assert(strstr(t, "Multiline comment") == NULL);
        assert(strstr(t, "DOCTYPE") == NULL);
        assert(strstr(t, "xml") == NULL);
    }

    assert(g_rendered_text_count >= 2);
    printf("  -> Comments, DOCTYPE, and processing instructions cleanly skipped.\n");
}

/* --------------------------------------------------------------------------
 * Test 6: Hit Testing Isolation
 * -------------------------------------------------------------------------- */
static void test_hit_testing_isolation(void) {
    printf("[TEST 6] Testing hit testing ignores non-visual nodes...\n");
    html5_engine_init();

    const char *html =
        "<html>"
        "<head><title>Test Hit</title><style>body { color: red; }</style></head>"
        "<body>"
        "<a href=\"http://example.com/link\">Clickable Link</a>"
        "</body>"
        "</html>";

    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);

    /* Click on link bounds */
    html_node_t *link = NULL;
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && n->tag == HTML_TAG_A) {
            link = n;
            break;
        }
    }
    assert(link != NULL);
    assert(link->bounds.w > 0 && link->bounds.h > 0);

    html_node_t *hit = find_clicked_node(g_root_dom, link->bounds.x + 2, link->bounds.y + 2);
    assert(hit != NULL);
    /* Hit could be the link or its text child inside the link */
    assert(hit == link || hit->parent == link);

    /* Test that head or style are NEVER returned by find_clicked_node */
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && html5_node_is_non_visual(n)) {
            html_node_t *bad_hit = find_clicked_node(n, 0, 0);
            assert(bad_hit == NULL);
        }
    }

    printf("  -> Hit testing strictly isolates non-visual nodes.\n");
}

/* --------------------------------------------------------------------------
 * Test 7: Unclosed Script Tag Structural Breakout
 * -------------------------------------------------------------------------- */
static void test_unclosed_script_recovery(void) {
    printf("[TEST 7] Testing unclosed <script> structural tag recovery...\n");
    html5_engine_init();
    reset_rendered_texts();

    /* Notice missing </script> closing tag before <div> */
    const char *html =
        "<html>"
        "<head>"
        "<script>"
        "function brokenScript() { var z = 1;"
        "<div class=\"content\">"
        "<h1>Recovered Heading</h1>"
        "<p>Recovered Paragraph</p>"
        "</div>"
        "</body>"
        "</html>";

    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);
    html5_render_document(0, 0, 800, 600, 0);

    int found_h1 = 0;
    int found_p = 0;
    for (int i = 0; i < g_rendered_text_count; i++) {
        const char *t = g_rendered_texts[i];
        if (strstr(t, "Recovered Heading")) found_h1 = 1;
        if (strstr(t, "Recovered Paragraph")) found_p = 1;
        assert(strstr(t, "brokenScript") == NULL);
    }

    assert(found_h1 == 1);
    assert(found_p == 1);
    printf("  -> Structural recovery on unclosed script passed.\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS DOM Visibility Isolation Test Suite (Phase C)\n");
    printf("============================================================\n");

    test_non_visual_tags_defaults();
    test_css_override_resistance();
    test_script_style_painting_isolation();
    test_text_outside_body_culling();
    test_comments_and_doctype();
    test_hit_testing_isolation();
    test_unclosed_script_recovery();

    printf("\nAll 7 DOM Visibility Isolation tests PASSED successfully!\n");
    return 0;
}
