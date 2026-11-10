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
    (void)rad; fb_rect(x, y, w, h, col);
}
void fb_round_rect_border(int x, int y, int w, int h, int rad, unsigned int col) {
    (void)rad; fb_border(x, y, w, h, col);
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

#include "../text_engine.h"

void text_engine_init(void) {}
int text_engine_measure(const char *text, font_fallback_chain_t *chain, int scale, int max_w,
                        int *out_ends_with_space, int *out_has_nbsp) {
    (void)chain; (void)max_w;
    if (out_ends_with_space) *out_ends_with_space = 0;
    if (out_has_nbsp) *out_has_nbsp = 0;
    if (!text) return 0;
    int fs = (scale > 0) ? scale : 1;
    int len = 0; while (text[len]) len++;
    return len * 8 * fs;
}
void text_engine_render(int x, int y, const char *text, u32 color, int scale,
                        font_fallback_chain_t *chain, void *target_fb) {
    (void)x; (void)y; (void)text; (void)color; (void)scale; (void)chain; (void)target_fb;
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
    (void)root; (void)base_url; (void)css_buf; (void)css_len; (void)max_css;
}
void html5_resources_set_lifecycle_callback(res_lifecycle_cb cb, void *user_data) {
    (void)cb; (void)user_data;
}

#include "../http_body.h"

const char *resource_state_name(resource_state_t state) {
    (void)state;
    return "LOADED";
}

int resource_is_executable(const http_body_t *b) {
    if (!b) return 0;
    return (b->state == RESOURCE_STATE_LOADED && !b->truncated && b->complete && b->length > 0);
}

void http_body_free(http_body_t *body) {
    if (body && body->data) {
        free(body->data);
        body->data = NULL;
    }
}

int http_get_body(const char *url, http_body_t *out_body, unsigned int max_capacity) {
    (void)url; (void)max_capacity;
    if (out_body) {
        memset(out_body, 0, sizeof(*out_body));
        out_body->state = RESOURCE_STATE_LOADED;
    }
    return 0;
}

int http_get(const char *url, char *buf, int max) {
    (void)url; (void)buf; (void)max; return 0;
}

#include "../html5_engine.c"
#include "../html5_script.c"

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
void html5_url_resolve(const char *base, const char *rel, char *out, int max) {
    (void)base; (void)rel; (void)out; (void)max;
}
int needs_redraw = 0;
int point_in_rect(int px, int py, rect_t r) {
    return (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h);
}

void test_html_has_class(void) {
    printf("[TEST 1] Testing html_has_class exact word boundary matching...\n");
    assert(html_has_class("card", "card", 4) == 1);
    assert(html_has_class("btn card active", "card", 4) == 1);
    assert(html_has_class("card active", "card", 4) == 1);
    assert(html_has_class("btn card", "card", 4) == 1);
    assert(html_has_class("cardholder", "card", 4) == 0);
    assert(html_has_class("wildcard", "card", 4) == 0);
    assert(html_has_class("postcard active", "card", 4) == 0);
    printf("  -> Word boundary checks passed (exact, start, middle, end, substrings rejected).\n");
}

void test_css_comments_and_media(void) {
    printf("[TEST 2] Testing CSS comments stripping and @media unpacking...\n");
    html5_engine_init();

    const char *html = "<html><head><style>"
                       "/* Comment 1 */\n"
                       "@media screen {\n"
                       "  .box { color: #112233; background-color: #445566; }\n"
                       "}\n"
                       "@media print {\n"
                       "  .box { color: #999999; }\n"
                       "}\n"
                       "@keyframes spin { 0% { opacity: 0; } 100% { opacity: 1; } }\n"
                       "/* Final comment */\n"
                       "</style></head><body><div class=\"box\">Hello Box</div></body></html>";

    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);

    html_node_t *box = html5_get_element_by_class("box");
    assert(box != NULL);
    assert((box->style.color & 0x00FFFFFF) == 0x00112233);
    assert((box->style.bg_color & 0x00FFFFFF) == 0x00445566);
    printf("  -> Screen media rules applied, print rules ignored, comments & keyframes safely parsed.\n");
}

void test_css_selectors(void) {
    printf("[TEST 3] Testing CSS child combinator (>), pseudo-classes (:hover), compound classes...\n");
    html5_engine_init();

    const char *html = "<html><head><style>"
                       "header > nav { background-color: #123456; }\n"
                       "a:hover { color: #aabbcc; }\n"
                       "ul li:first-child { list-style: none; }\n"
                       ".btn.primary { background-color: #00ff00; }\n"
                       "</style></head><body>"
                       "<header><nav><a href=\"#\">Home</a></nav></header>"
                       "<ul><li>First Item</li><li>Second Item</li></ul>"
                       "<button class=\"btn primary\">Go</button>"
                       "<button class=\"btn secondary\">Wait</button>"
                       "</body></html>";

    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);

    html_node_t *nav = html5_get_element_by_tag(HTML_TAG_NAV);
    assert(nav != NULL);
    assert((nav->style.bg_color & 0x00FFFFFF) == 0x00123456);

    html_node_t *a = html5_get_element_by_tag(HTML_TAG_A);
    assert(a != NULL);
    assert((a->style.color & 0x00FFFFFF) == 0x00AABBCC);

    html_node_t *first_li = html5_get_element_by_tag(HTML_TAG_LI);
    assert(first_li != NULL);
    assert(first_li->style.list_style_none == 1);

    html_node_t *btn_primary = html5_get_element_by_class("primary");
    assert(btn_primary != NULL);
    assert((btn_primary->style.bg_color & 0x00FFFFFF) == 0x0000FF00);

    html_node_t *btn_secondary = html5_get_element_by_class("secondary");
    assert(btn_secondary != NULL);
    assert((btn_secondary->style.bg_color & 0x00FFFFFF) != 0x0000FF00);
    printf("  -> Selectors matched correctly (nav bg, a:hover color, list_style_none, compound class).\n");
}

void test_html_attribute_quotes_and_whitespace(void) {
    printf("[TEST 4] Testing HTML attribute quote preservation and whitespace collapsing...\n");
    html5_engine_init();

    const char *html = "<html><body><div><a href=\"https://site.com/search?q=1>2\">Link Text</a>\n\n   Lots   of  \t  spaces \n and   lines  </div></body></html>";
    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);

    html_node_t *a = html5_get_element_by_tag(HTML_TAG_A);
    assert(a != NULL);
    assert(strcmp(a->href, "https://site.com/search?q=1>2") == 0);
    assert(a->first_child != NULL && strcmp(a->first_child->text, "Link Text") == 0);

    /* Check sibling text */
    html_node_t *txt = a->next_sibling;
    assert(txt != NULL);
    assert(strstr(txt->text, "Lots of spaces and lines") != NULL);
    assert(strstr(txt->text, "  ") == NULL);
    printf("  -> Attributes with '>' parsed safely without tag truncation; multiple newlines/spaces collapsed.\n");
}

void test_js_engine_enhancements(void) {
    printf("[TEST 5] Testing JS comments, functions, document.body.style, querySelector, classList...\n");
    html5_engine_init();

    const char *html = "<html><head></head><body><div id=\"main-card\" class=\"card\">Content</div></body></html>";
    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);

    html5_script_init();

    const char *script = 
        "// Single line comment\n"
        "/* Multi line\n"
        "   comment */\n"
        "function setup() {\n"
        "  document.body.style.backgroundColor = '#112233';\n"
        "  document.querySelector('#main-card').style.color = '#445566';\n"
        "  document.querySelector('.card').classList.add('selected');\n"
        "}\n"
        "setup();\n";

    html_node_t *root = html5_get_root_dom();
    html5_script_eval(script, root);

    /* Verify body style */
    html_node_t *body = html5_get_element_by_tag(HTML_TAG_BODY);
    assert(body != NULL);
    assert((body->style.bg_color & 0x00FFFFFF) == 0x00112233);

    /* Verify card color */
    html_node_t *card = html5_get_element_by_id("main-card");
    assert(card != NULL);
    assert((card->style.color & 0x00FFFFFF) == 0x00445566);
    assert(strstr(card->class_name, "selected") != NULL);
    printf("  -> JS comments stripped, functions declared & invoked, styles and classList manipulated.\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS CSS & JS Engine Upgrade Test Suite\n");
    printf("============================================================\n");
    test_html_has_class();
    test_css_comments_and_media();
    test_css_selectors();
    test_html_attribute_quotes_and_whitespace();
    test_js_engine_enhancements();
    printf("============================================================\n");
    printf("All 5 CSS & JS Upgrade tests PASSED successfully!\n");
    printf("============================================================\n");
    return 0;
}
