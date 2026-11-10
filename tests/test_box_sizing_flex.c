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
    (void)base; (void)rel; (void)out; (void)max;
}
int needs_redraw = 0;
int point_in_rect(int px, int py, rect_t r) {
    return (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h);
}

/* --------------------------------------------------------------------------
 * Test 1: box-sizing: content-box vs border-box
 * -------------------------------------------------------------------------- */
static void test_box_sizing_modes(void) {
    printf("[TEST 1] Testing box-sizing: content-box vs border-box...\n");
    html5_engine_init();

    const char *html =
        "<html><body>"
        "<div id=\"cb\" style=\"width: 200px; padding: 20px; border: 5px solid #000; box-sizing: content-box;\">Content Box</div>"
        "<div id=\"bb\" style=\"width: 200px; padding: 20px; border: 5px solid #000; box-sizing: border-box;\">Border Box</div>"
        "</body></html>";

    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);

    html_node_t *cb = NULL;
    html_node_t *bb = NULL;
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && n->id && strcmp(n->id, "cb") == 0) cb = n;
        if (n && n->id && strcmp(n->id, "bb") == 0) bb = n;
    }

    assert(cb != NULL);
    assert(bb != NULL);

    /* content-box: outer width = 200 + 20*2 (padding) + 5*2 (border) = 250px */
    assert(cb->bounds.w == 250);

    /* border-box: outer width = 200px exactly */
    assert(bb->bounds.w == 200);

    printf("  -> content-box (w=%d) and border-box (w=%d) verified.\n", cb->bounds.w, bb->bounds.w);
}

/* --------------------------------------------------------------------------
 * Test 2: CSS Padding and Border Shorthand Parsing
 * -------------------------------------------------------------------------- */
static void test_padding_border_shorthand_parsing(void) {
    printf("[TEST 2] Testing padding and border shorthand parsing...\n");
    html5_engine_init();

    const char *html =
        "<html><body>"
        "<div id=\"p2\" style=\"padding: 10px 25px;\">Two values</div>"
        "<div id=\"p4\" style=\"padding: 5px 15px 20px 30px;\">Four values</div>"
        "<div id=\"b1\" style=\"border: 3px solid #ff0000;\">Border shorthand</div>"
        "</body></html>";

    html5_parse_document(html, strlen(html), "http://example.com");

    html_node_t *p2 = NULL, *p4 = NULL, *b1 = NULL;
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && n->id && strcmp(n->id, "p2") == 0) p2 = n;
        if (n && n->id && strcmp(n->id, "p4") == 0) p4 = n;
        if (n && n->id && strcmp(n->id, "b1") == 0) b1 = n;
    }

    assert(p2 != NULL);
    assert(p2->style.padding_top == 10);
    assert(p2->style.padding_bottom == 10);
    assert(p2->style.padding_left == 25);
    assert(p2->style.padding_right == 25);

    assert(p4 != NULL);
    assert(p4->style.padding_top == 5);
    assert(p4->style.padding_right == 15);
    assert(p4->style.padding_bottom == 20);
    assert(p4->style.padding_left == 30);

    assert(b1 != NULL);
    assert(b1->style.border_width == 3);
    assert(b1->style.border_color == 0xFF453A || b1->style.border_color == 0xFF0000);

    printf("  -> Multi-value padding and border shorthand parsed correctly.\n");
}

/* --------------------------------------------------------------------------
 * Test 3: min-width and max-width Clamping
 * -------------------------------------------------------------------------- */
static void test_min_max_dimensions_clamping(void) {
    printf("[TEST 3] Testing min-width and max-width clamping...\n");
    html5_engine_init();

    const char *html =
        "<html><body>"
        "<div id=\"max_clamp\" style=\"width: 600px; max-width: 320px; box-sizing: border-box;\">Clamped Max</div>"
        "<div id=\"min_clamp\" style=\"width: 40px; min-width: 140px; box-sizing: border-box;\">Clamped Min</div>"
        "<div id=\"pct_clamp\" style=\"width: 700px; max-width: 50%; box-sizing: border-box;\">Percentage Max</div>"
        "</body></html>";

    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);

    html_node_t *max_c = NULL, *min_c = NULL, *pct_c = NULL;
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && n->id && strcmp(n->id, "max_clamp") == 0) max_c = n;
        if (n && n->id && strcmp(n->id, "min_clamp") == 0) min_c = n;
        if (n && n->id && strcmp(n->id, "pct_clamp") == 0) pct_c = n;
    }

    assert(max_c != NULL);
    assert(max_c->bounds.w == 320);

    assert(min_c != NULL);
    assert(min_c->bounds.w == 140);

    assert(pct_c != NULL);
    /* 50% of 800 viewport = 400px */
    assert(pct_c->bounds.w == 400);

    printf("  -> min-width, max-width px, and max-width %% clamped accurately.\n");
}

/* --------------------------------------------------------------------------
 * Test 4: Image Geometry & Aspect Ratio Preservation
 * -------------------------------------------------------------------------- */
static void test_image_geometry_aspect_ratio(void) {
    printf("[TEST 4] Testing image aspect ratio preservation and size clamping...\n");
    html5_engine_init();

    const char *html =
        "<html><body>"
        "<img id=\"img_w\" src=\"test1.png\" style=\"width: 200px;\" />"
        "<img id=\"img_h\" src=\"test2.png\" style=\"height: 50px;\" />"
        "<img id=\"img_max\" src=\"test3.png\" style=\"width: 800px; max-width: 300px;\" />"
        "</body></html>";

    html5_parse_document(html, strlen(html), "http://example.com");

    /* Simulate natural dimensions: 400x200 (aspect ratio 2:1) */
    html_node_t *img1 = NULL, *img2 = NULL, *img3 = NULL;
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && n->id && strcmp(n->id, "img_w") == 0) {
            n->img_w = 400; n->img_h = 200; img1 = n;
        }
        if (n && n->id && strcmp(n->id, "img_h") == 0) {
            n->img_w = 400; n->img_h = 200; img2 = n;
        }
        if (n && n->id && strcmp(n->id, "img_max") == 0) {
            n->img_w = 400; n->img_h = 200; img3 = n;
        }
    }

    assert(img1 != NULL && img2 != NULL && img3 != NULL);

    html5_layout_document(800, 600);

    /* img1: width=200px -> height should be scaled to 100px (400x200 2:1 ratio) */
    assert(img1->bounds.w == 200);
    assert(img1->bounds.h == 100);

    /* img2: height=50px -> width should be scaled to 100px (2:1 ratio) */
    assert(img2->bounds.h == 50);
    assert(img2->bounds.w == 100);

    /* img3: width=800, max-width=300 -> width clamped to 300, height scaled to 150 (2:1 ratio) */
    assert(img3->bounds.w == 300);
    assert(img3->bounds.h == 150);

    printf("  -> Image aspect ratio preserved across width, height, and max-width scaling.\n");
}

/* --------------------------------------------------------------------------
 * Test 5: Flexbox Row Layout & Gap
 * -------------------------------------------------------------------------- */
static void test_flexbox_row_layout(void) {
    printf("[TEST 5] Testing flexbox row layout and gap spacing...\n");
    html5_engine_init();

    const char *html =
        "<html><body>"
        "<div id=\"flex_row\" style=\"display: flex; flex-direction: row; gap: 15px; width: 400px; box-sizing: border-box;\">"
        "<div id=\"it1\" style=\"width: 60px; height: 30px;\">1</div>"
        "<div id=\"it2\" style=\"width: 70px; height: 40px;\">2</div>"
        "<div id=\"it3\" style=\"width: 80px; height: 25px;\">3</div>"
        "</div>"
        "</body></html>";

    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);

    html_node_t *row = NULL, *it1 = NULL, *it2 = NULL, *it3 = NULL;
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && n->id && strcmp(n->id, "flex_row") == 0) row = n;
        if (n && n->id && strcmp(n->id, "it1") == 0) it1 = n;
        if (n && n->id && strcmp(n->id, "it2") == 0) it2 = n;
        if (n && n->id && strcmp(n->id, "it3") == 0) it3 = n;
    }

    assert(row != NULL && it1 != NULL && it2 != NULL && it3 != NULL);

    /* Item 1 starts at container left */
    assert(it1->bounds.x == row->bounds.x);
    assert(it1->bounds.w == 60);

    /* Item 2 placed after it1 (60px) + gap (15px) */
    assert(it2->bounds.x == it1->bounds.x + 60 + 15);
    assert(it2->bounds.w == 70);

    /* Item 3 placed after it2 (70px) + gap (15px) */
    assert(it3->bounds.x == it2->bounds.x + 70 + 15);
    assert(it3->bounds.w == 80);

    /* Container height should adapt to tallest child (it2 height 40px) */
    assert(row->bounds.h >= 40);

    printf("  -> Flexbox row items placed side-by-side with exact 15px gap.\n");
}

/* --------------------------------------------------------------------------
 * Test 6: Flexbox Justify Content & Align Items
 * -------------------------------------------------------------------------- */
static void test_flexbox_alignment_distribution(void) {
    printf("[TEST 6] Testing flexbox justify-content and align-items...\n");
    html5_engine_init();

    const char *html =
        "<html><body>"
        "<div id=\"flex_centered\" style=\"display: flex; justify-content: center; align-items: center; width: 400px; height: 100px; box-sizing: border-box;\">"
        "<div id=\"cit1\" style=\"width: 100px; height: 40px;\">Center</div>"
        "</div>"
        "</body></html>";

    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);

    html_node_t *cont = NULL, *cit = NULL;
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && n->id && strcmp(n->id, "flex_centered") == 0) cont = n;
        if (n && n->id && strcmp(n->id, "cit1") == 0) cit = n;
    }

    assert(cont != NULL && cit != NULL);

    /* Container is 400px wide. Child is 100px wide. Centered child X = cont->x + (400 - 100) / 2 = cont->x + 150 */
    assert(cit->bounds.x == cont->bounds.x + 150);

    /* Container is 100px high. Child is 40px high. Centered child Y = cont->y + (100 - 40) / 2 = cont->y + 30 */
    assert(cit->bounds.y == cont->bounds.y + 30);

    printf("  -> justify-content: center and align-items: center aligned items precisely.\n");
}

/* --------------------------------------------------------------------------
 * Test 7: Flexbox Column Layout
 * -------------------------------------------------------------------------- */
static void test_flexbox_column_layout(void) {
    printf("[TEST 7] Testing flexbox column layout...\n");
    html5_engine_init();

    const char *html =
        "<html><body>"
        "<div id=\"flex_col\" style=\"display: flex; flex-direction: column; gap: 10px; width: 200px; box-sizing: border-box;\">"
        "<div id=\"c1\" style=\"width: 180px; height: 30px;\">Col 1</div>"
        "<div id=\"c2\" style=\"width: 180px; height: 50px;\">Col 2</div>"
        "</div>"
        "</body></html>";

    html5_parse_document(html, strlen(html), "http://example.com");
    html5_layout_document(800, 600);

    html_node_t *col = NULL, *c1 = NULL, *c2 = NULL;
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && n->id && strcmp(n->id, "flex_col") == 0) col = n;
        if (n && n->id && strcmp(n->id, "c1") == 0) c1 = n;
        if (n && n->id && strcmp(n->id, "c2") == 0) c2 = n;
    }

    assert(col != NULL && c1 != NULL && c2 != NULL);

    assert(c1->bounds.y == col->bounds.y);
    /* c2 should be at c1->y + 30 + 10 (gap) */
    assert(c2->bounds.y == c1->bounds.y + 30 + 10);

    /* Total column container height should wrap both items + gap: 30 + 10 + 50 = 90 */
    assert(col->bounds.h >= 90);

    printf("  -> Flexbox column items stacked vertically with gap.\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS CSS Box Sizing & Flexbox Layout Test Suite (Phase D)\n");
    printf("============================================================\n");

    test_box_sizing_modes();
    test_padding_border_shorthand_parsing();
    test_min_max_dimensions_clamping();
    test_image_geometry_aspect_ratio();
    test_flexbox_row_layout();
    test_flexbox_alignment_distribution();
    test_flexbox_column_layout();

    printf("\nAll 7 CSS Box Sizing & Flexbox Layout tests PASSED successfully!\n");
    return 0;
}
