#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../bb_types.h"
#include "../bb_dom.h"
#include "../bb_css.h"
#include "../bb_layout.h"
#include "../bb_rasterizer.h"
#include "../bb_image.h"
#include "../painter.h"

static void test_intrinsic_sizing(void) {
    printf("[TEST] Running Image Intrinsic Sizing Tests...\n");

    bb_node_t *node = bb_node_create_element("img");
    node->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    node->computed_style->display = BB_DISPLAY_INLINE;
    node->computed_style->width = -1;
    node->computed_style->height = -1;

    bb_image_t *img = bb_image_create("logo.png", "Logo");
    uint32_t *pixels = (uint32_t *)bb_calloc(100 * 80, sizeof(uint32_t));
    bb_image_set_loaded(img, 100, 80, pixels);
    node->image = img;

    bb_layout_box_t *box = bb_layout_build_tree(node);
    assert(box != NULL);
    assert(box->type == BB_BOX_REPLACED);
    assert(box->image == img);

    bb_layout_compute(box, 0, 0, 500);

    /* Intrinsic dimensions should be used: 100x80 */
    assert(box->dimensions.w == 100);
    assert(box->dimensions.h == 80);

    bb_layout_free_tree(box);
    bb_node_free_tree(node);
    printf("  -> Image Intrinsic Sizing Tests PASSED!\n");
}

static void test_explicit_sizing_and_precedence(void) {
    printf("[TEST] Running Explicit Sizing & Precedence Tests...\n");

    /* 1. HTML attributes width="300" height="200" */
    bb_node_t *node = bb_node_create_element("img");
    bb_node_set_attribute(node, "width", "300");
    bb_node_set_attribute(node, "height", "200");
    node->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    node->computed_style->display = BB_DISPLAY_INLINE;
    node->computed_style->width = -1;
    node->computed_style->height = -1;

    bb_layout_box_t *box = bb_layout_build_tree(node);
    bb_layout_compute(box, 0, 0, 800);

    assert(box->dimensions.w == 300);
    assert(box->dimensions.h == 200);
    bb_layout_free_tree(box);

    /* 2. CSS overrides HTML attributes */
    node->computed_style->width = 250;
    node->computed_style->height = 180;

    box = bb_layout_build_tree(node);
    bb_layout_compute(box, 0, 0, 800);

    assert(box->dimensions.w == 250);
    assert(box->dimensions.h == 180);

    bb_layout_free_tree(box);
    bb_node_free_tree(node);
    printf("  -> Explicit Sizing & Precedence Tests PASSED!\n");
}

static void test_aspect_ratio_preservation(void) {
    printf("[TEST] Running Aspect Ratio Preservation Tests...\n");

    /* Intrinsic image: 400x200 (aspect ratio 2:1) */
    bb_node_t *node = bb_node_create_element("img");
    node->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    node->computed_style->display = BB_DISPLAY_INLINE;
    node->computed_style->width = -1;
    node->computed_style->height = -1;

    bb_image_t *img = bb_image_create("banner.png", "Banner");
    uint32_t *pixels = (uint32_t *)bb_calloc(400 * 200, sizeof(uint32_t));
    bb_image_set_loaded(img, 400, 200, pixels);
    node->image = img;

    /* Case A: Specified width = 200, height = auto (-1) -> H should be 100 */
    node->computed_style->width = 200;
    node->computed_style->height = -1;

    bb_layout_box_t *box = bb_layout_build_tree(node);
    bb_layout_compute(box, 0, 0, 800);
    assert(box->dimensions.w == 200);
    assert(box->dimensions.h == 100);
    bb_layout_free_tree(box);

    /* Case B: Specified height = 150, width = auto (-1) -> W should be 300 */
    node->computed_style->width = -1;
    node->computed_style->height = 150;

    box = bb_layout_build_tree(node);
    bb_layout_compute(box, 0, 0, 800);
    assert(box->dimensions.w == 300);
    assert(box->dimensions.h == 150);
    bb_layout_free_tree(box);

    bb_node_free_tree(node);
    printf("  -> Aspect Ratio Preservation Tests PASSED!\n");
}

static void test_missing_and_failed_images(void) {
    printf("[TEST] Running Missing & Failed Image Tests...\n");

    bb_node_t *node = bb_node_create_element("img");
    bb_node_set_attribute(node, "src", "missing.png");
    bb_node_set_attribute(node, "alt", "Missing Photo");
    node->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    node->computed_style->display = BB_DISPLAY_INLINE;
    node->computed_style->width = -1;
    node->computed_style->height = -1;

    bb_layout_box_t *box = bb_layout_build_tree(node);
    assert(box != NULL);
    assert(box->image != NULL);
    assert(box->image->state == BB_IMAGE_STATE_UNLOADED);

    bb_layout_compute(box, 0, 0, 800);
    /* Should fall back to standard placeholder dimensions (200x150) */
    assert(box->dimensions.w == BB_IMAGE_FALLBACK_WIDTH);
    assert(box->dimensions.h == BB_IMAGE_FALLBACK_HEIGHT);

    /* Test placeholder paint */
    uint32_t fb_pixels[300 * 200];
    memset(fb_pixels, 0xFF, sizeof(fb_pixels));
    bb_raster_t r;
    bb_raster_init(&r, fb_pixels, 300, 200);

    /* Paint into rasterizer without crash */
    painter_paint_replaced(box, &r, 10, 10);

    /* Set failed and paint again */
    bb_image_set_failed(box->image);
    assert(box->image->state == BB_IMAGE_STATE_FAILED);
    painter_paint_replaced(box, &r, 10, 10);

    bb_layout_free_tree(box);
    bb_node_free_tree(node);
    printf("  -> Missing & Failed Image Tests PASSED!\n");
}

static void test_inline_image_formatting_context(void) {
    printf("[TEST] Running Inline Image Formatting Context Tests...\n");

    /* Container div with width 250px */
    bb_node_t *container = bb_node_create_element("div");
    container->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    container->computed_style->display = BB_DISPLAY_BLOCK;
    container->computed_style->width = 250;

    /* Add three 100px-wide inline images:
     * Image 1 (100px) and Image 2 (100px) fit on line 1 (100 + 100 + 4 = 204 <= 250).
     * Image 3 (100px) must wrap to line 2.                                    */
    for (int i = 0; i < 3; i++) {
        bb_node_t *img_node = bb_node_create_element("img");
        img_node->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
        img_node->computed_style->display = BB_DISPLAY_INLINE;
        img_node->computed_style->width = 100;
        img_node->computed_style->height = 50;
        bb_node_append_child(container, img_node);
    }

    bb_layout_box_t *root = bb_layout_build_tree(container);
    bb_layout_compute(root, 0, 0, 250);

    bb_layout_box_t *b1 = root->first_child;
    bb_layout_box_t *b2 = b1 ? b1->next_sibling : NULL;
    bb_layout_box_t *b3 = b2 ? b2->next_sibling : NULL;

    assert(b1 && b2 && b3);
    /* b1 and b2 on same line (same y) */
    assert(b1->dimensions.y == b2->dimensions.y);
    assert(b2->dimensions.x > b1->dimensions.x);

    /* b3 wrapped onto second line (y > b1->dimensions.y) */
    assert(b3->dimensions.y > b1->dimensions.y);

    bb_layout_free_tree(root);
    bb_node_free_tree(container);
    printf("  -> Inline Image Formatting Context Tests PASSED!\n");
}

static void test_block_image_formatting_context(void) {
    printf("[TEST] Running Block Image Formatting Context Tests...\n");

    bb_node_t *container = bb_node_create_element("div");
    container->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    container->computed_style->display = BB_DISPLAY_BLOCK;

    /* Image 1: display: block; height: 80px; margin-bottom: 10px; */
    bb_node_t *img1 = bb_node_create_element("img");
    img1->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    img1->computed_style->display = BB_DISPLAY_BLOCK;
    img1->computed_style->width = 120;
    img1->computed_style->height = 80;
    img1->computed_style->margin_bottom = 10;
    bb_node_append_child(container, img1);

    /* Image 2: display: block; height: 60px; */
    bb_node_t *img2 = bb_node_create_element("img");
    img2->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    img2->computed_style->display = BB_DISPLAY_BLOCK;
    img2->computed_style->width = 140;
    img2->computed_style->height = 60;
    bb_node_append_child(container, img2);

    bb_layout_box_t *root = bb_layout_build_tree(container);
    bb_layout_compute(root, 0, 0, 500);

    bb_layout_box_t *b1 = root->first_child;
    bb_layout_box_t *b2 = b1 ? b1->next_sibling : NULL;

    assert(b1 && b2);
    assert(b1->dimensions.w == 120);
    assert(b1->dimensions.h == 80);
    assert(b2->dimensions.w == 140);
    assert(b2->dimensions.h == 60);

    /* b2 placed vertically below b1, taking margin-bottom into account */
    assert(b2->dimensions.y >= b1->dimensions.y + 80 + 10);

    bb_layout_free_tree(root);
    bb_node_free_tree(container);
    printf("  -> Block Image Formatting Context Tests PASSED!\n");
}

static void test_bmp_decoder_and_scaled_blit(void) {
    printf("[TEST] Running BMP Decoder & Scaled Blit Tests...\n");

    /* Create a 2x2 24-bpp uncompressed BMP in memory:
     * Row 0 (bottom): Red, Green
     * Row 1 (top):    Blue, White
     * Row stride: 2 pixels * 3 bytes = 6 bytes -> aligned to 4 bytes = 8 bytes per row.
     * Header size: 14 (file) + 40 (info) = 54 bytes.
     * Total file size: 54 + 2 * 8 = 70 bytes.                                         */
    uint8_t bmp[70];
    memset(bmp, 0, sizeof(bmp));

    /* File Header */
    bmp[0] = 'B'; bmp[1] = 'M';
    bmp[2] = 70;  /* File size */
    bmp[10] = 54; /* Data offset */

    /* Info Header (BITMAPINFOHEADER) */
    bmp[14] = 40; /* Header size */
    bmp[18] = 2;  /* Width */
    bmp[22] = 2;  /* Height */
    bmp[26] = 1;  /* Planes */
    bmp[28] = 24; /* Bits per pixel */

    /* Row 0 (Bottom): Red (BGR: 0, 0, 255), Green (BGR: 0, 255, 0), padding: 0, 0 */
    uint8_t *row0 = &bmp[54];
    row0[0] = 0;   row0[1] = 0;   row0[2] = 255;
    row0[3] = 0;   row0[4] = 255; row0[5] = 0;

    /* Row 1 (Top): Blue (BGR: 255, 0, 0), White (BGR: 255, 255, 255), padding: 0, 0 */
    uint8_t *row1 = &bmp[54 + 8];
    row1[0] = 255; row1[1] = 0;   row1[2] = 0;
    row1[3] = 255; row1[4] = 255; row1[5] = 255;

    bb_image_t *img = bb_image_create("test.bmp", "Test BMP");
    bool ok = bb_image_decode_buffer(img, bmp, sizeof(bmp));
    assert(ok);
    assert(img->state == BB_IMAGE_STATE_LOADED);
    assert(img->intrinsic_width == 2);
    assert(img->intrinsic_height == 2);
    assert(img->pixels != NULL);

    /* Verify decoded pixel values (stored top-down):
     * (0, 0) should be Blue: BB_ARGB(255, 0, 0, 255)
     * (1, 0) should be White: BB_ARGB(255, 255, 255, 255)
     * (0, 1) should be Red: BB_ARGB(255, 255, 0, 0)
     * (1, 1) should be Green: BB_ARGB(255, 0, 255, 0) */
    assert(img->pixels[0] == BB_ARGB(255, 0, 0, 255));
    assert(img->pixels[1] == BB_ARGB(255, 255, 255, 255));
    assert(img->pixels[2] == BB_ARGB(255, 255, 0, 0));
    assert(img->pixels[3] == BB_ARGB(255, 0, 255, 0));

    /* Test scaled blitting onto 20x20 destination framebuffer */
    uint32_t fb[20 * 20];
    memset(fb, 0, sizeof(fb));
    bb_raster_t r;
    bb_raster_init(&r, fb, 20, 20);

    bb_raster_t src;
    bb_raster_init(&src, img->pixels, 2, 2);

    bb_rect_t dst_rect = { 0, 0, 20, 20 };
    bb_rect_t src_rect = { 0, 0, 2, 2 };
    bb_raster_blit_scaled(&r, dst_rect, &src, src_rect);

    /* Top-left quadrant should be Blue */
    assert(fb[0] == BB_ARGB(255, 0, 0, 255));
    assert(fb[5 * 20 + 5] == BB_ARGB(255, 0, 0, 255));

    /* Bottom-left quadrant should be Red */
    assert(fb[15 * 20 + 5] == BB_ARGB(255, 255, 0, 0));

    bb_image_free(img);
    printf("  -> BMP Decoder & Scaled Blit Tests PASSED!\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Browser Replaced Elements & Image Test Suite\n");
    printf("============================================================\n");

    test_intrinsic_sizing();
    test_explicit_sizing_and_precedence();
    test_aspect_ratio_preservation();
    test_missing_and_failed_images();
    test_inline_image_formatting_context();
    test_block_image_formatting_context();
    test_bmp_decoder_and_scaled_blit();

    printf("\nAll Browser Replaced Elements & Image Tests PASSED!\n");
    return 0;
}
