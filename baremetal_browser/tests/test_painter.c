#include "../bb_types.h"
#include "../bb_libc.h"
#include "../bb_dom.h"
#include "../bb_html.h"
#include "../bb_css.h"
#include "../bb_layout.h"
#include "../bb_rasterizer.h"
#include "../painter.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

#define FB_W 200
#define FB_H 200

static uint32_t g_fb[FB_W * FB_H];

static bb_raster_t init_test_raster(void) {
    bb_raster_t r;
    bb_raster_init(&r, g_fb, FB_W, FB_H);
    bb_raster_clear(&r, BB_COLOR_WHITE);
    return r;
}

/* =========================================================================
 * 1. Three-Phase Box Paint (Background, Border, Text)
 * ========================================================================= */
static void test_three_phase_paint(void) {
    printf("[TEST] Running Three-Phase Box Paint Test...\n");

    const char *html = 
        "<div id=\"box\" style=\"width: 60px; height: 60px; background-color: #ff0000; border: 4px solid #0000ff;\">\n"
        "  Text\n"
        "</div>";

    bb_node_t *doc = bb_html_parse(html);
    assert(doc != NULL);

    bb_stylesheet_t *sheet = bb_css_create();
    bb_css_apply_styles(doc, sheet);

    bb_layout_box_t *root = bb_layout_build_tree(doc);
    assert(root != NULL);
    bb_layout_compute(root, 0, 0, FB_W);

    bb_raster_t r = init_test_raster();
    painter_paint_tree(root, &r, 10, 10);

    /* Background pixel inside the box should be red */
    bb_color_t center_pixel = g_fb[40 * FB_W + 40];
    assert(center_pixel == BB_RGB(255, 0, 0));

    /* Border pixel on the left boundary should be blue */
    /* Box starts at x=10+margin(0), padding(0), border(4). Left border edge is at x=10. */
    bb_color_t border_pixel = g_fb[20 * FB_W + 11];
    assert(border_pixel == BB_RGB(0, 0, 255));

    bb_layout_free_tree(root);
    bb_css_free(sheet);
    bb_node_free_tree(doc);
    printf("  -> Three-Phase Box Paint Test PASSED!\n");
}

/* =========================================================================
 * 2. Visibility Hidden Test
 * ========================================================================= */
static void test_visibility_hidden(void) {
    printf("[TEST] Running Visibility Hidden Test...\n");

    const char *html = 
        "<div style=\"width: 80px; height: 80px; background-color: #ff0000; visibility: hidden;\">\n"
        "  Hidden Text\n"
        "</div>";

    bb_node_t *doc = bb_html_parse(html);
    assert(doc != NULL);

    bb_stylesheet_t *sheet = bb_css_create();
    bb_css_apply_styles(doc, sheet);

    bb_layout_box_t *root = bb_layout_build_tree(doc);
    assert(root != NULL);
    bb_layout_compute(root, 0, 0, FB_W);

    bb_raster_t r = init_test_raster();
    painter_paint_tree(root, &r, 10, 10);

    /* Because visibility is hidden, pixel at (40, 40) must NOT be red; it must stay white */
    bb_color_t p = g_fb[40 * FB_W + 40];
    assert(p == BB_COLOR_WHITE);

    bb_layout_free_tree(root);
    bb_css_free(sheet);
    bb_node_free_tree(doc);
    printf("  -> Visibility Hidden Test PASSED!\n");
}

/* =========================================================================
 * 3. Overflow Clipping Test
 * ========================================================================= */
static void test_overflow_clipping(void) {
    printf("[TEST] Running Overflow Clipping Test...\n");

    /* Parent is 40x40 with overflow: hidden at (10, 10).
     * Child is 100x100 red block.
     * Pixels inside parent (e.g. x=30, y=30) must be RED.
     * Pixels outside parent (e.g. x=80, y=30) must be WHITE (clipped).
     */
    const char *html = 
        "<div id=\"parent\" style=\"width: 40px; height: 40px; overflow: hidden;\">\n"
        "  <div id=\"child\" style=\"width: 100px; height: 100px; background-color: #ff0000;\"></div>\n"
        "</div>";

    bb_node_t *doc = bb_html_parse(html);
    assert(doc != NULL);

    bb_stylesheet_t *sheet = bb_css_create();
    bb_css_apply_styles(doc, sheet);

    bb_layout_box_t *root = bb_layout_build_tree(doc);
    assert(root != NULL);
    bb_layout_compute(root, 0, 0, FB_W);

    bb_raster_t r = init_test_raster();
    painter_paint_tree(root, &r, 10, 10);

    /* Inside parent clipping area: should be red */
    bb_color_t inside_pixel = g_fb[25 * FB_W + 25];
    assert(inside_pixel == BB_RGB(255, 0, 0));

    /* Outside parent clipping area: child would have drawn at x=80, y=25 if not clipped, but must be WHITE */
    bb_color_t outside_pixel = g_fb[25 * FB_W + 80];
    assert(outside_pixel == BB_COLOR_WHITE);

    bb_layout_free_tree(root);
    bb_css_free(sheet);
    bb_node_free_tree(doc);
    printf("  -> Overflow Clipping Test PASSED!\n");
}

/* =========================================================================
 * 4. Z-Index and Stacking Order Test
 * ========================================================================= */
static void test_z_index_stacking(void) {
    printf("[TEST] Running Z-Index Stacking Order Test...\n");

    /* Box 1: earlier in DOM, but higher z-index (10), RED
     * Box 2: later in DOM, but lower z-index (5), BLUE
     * Overlapping area must be RED because Box 1 has higher z-index!
     */
    const char *html = 
        "<div id=\"container\" style=\"position: relative; width: 120px; height: 120px;\">\n"
        "  <div id=\"box1\" style=\"position: absolute; left: 10px; top: 10px; width: 60px; height: 60px; z-index: 10; background-color: #ff0000;\"></div>\n"
        "  <div id=\"box2\" style=\"position: absolute; left: 30px; top: 30px; width: 60px; height: 60px; z-index: 5; background-color: #0000ff;\"></div>\n"
        "</div>";

    bb_node_t *doc = bb_html_parse(html);
    assert(doc != NULL);

    bb_stylesheet_t *sheet = bb_css_create();
    bb_css_apply_styles(doc, sheet);

    bb_layout_box_t *root = bb_layout_build_tree(doc);
    assert(root != NULL);
    bb_layout_compute(root, 0, 0, FB_W);

    bb_raster_t r = init_test_raster();
    painter_paint_tree(root, &r, 0, 0);

    /* Overlap region: x=45, y=45.
     * Box 1 (red, z-index 10) must be painted on top of Box 2 (blue, z-index 5).
     */
    bb_color_t overlap_pixel = g_fb[45 * FB_W + 45];
    assert(overlap_pixel == BB_RGB(255, 0, 0));

    /* Non-overlapping part of Box 2: x=75, y=75 -> must be blue */
    bb_color_t box2_pixel = g_fb[75 * FB_W + 75];
    assert(box2_pixel == BB_RGB(0, 0, 255));

    bb_layout_free_tree(root);
    bb_css_free(sheet);
    bb_node_free_tree(doc);
    printf("  -> Z-Index Stacking Order Test PASSED!\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Browser Painter & Compositing Test Suite\n");
    printf("============================================================\n");

    test_three_phase_paint();
    test_visibility_hidden();
    test_overflow_clipping();
    test_z_index_stacking();

    printf("\nAll Browser Painter & Compositing Tests PASSED!\n");
    return 0;
}
