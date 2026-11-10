#include "../bb_types.h"
#include "../bb_libc.h"
#include "../bb_rasterizer.h"
#include <stdio.h>
#include <assert.h>

void run_rasterizer_tests(void) {
    printf("[TEST] Running 2D Software Rasterizer Tests...\n");

    /* Alpha blend test */
    bb_color_t white = BB_RGB(255, 255, 255);
    bb_color_t semi_black = BB_ARGB(128, 0, 0, 0); /* ~50% black over white */
    bb_color_t blended = bb_blend_color(white, semi_black);

    /* Result should be mid-gray ~126-128 */
    uint32_t r = BB_GET_R(blended);
    uint32_t g = BB_GET_G(blended);
    uint32_t b = BB_GET_B(blended);
    assert(r >= 120 && r <= 135);
    assert(g >= 120 && g <= 135);
    assert(b >= 120 && b <= 135);

    /* Clipping stack test */
    uint32_t pixels[100 * 100];
    bb_raster_t rast;
    bb_raster_init(&rast, pixels, 100, 100);
    bb_raster_clear(&rast, BB_COLOR_WHITE);

    bb_rect_t c1 = {10, 10, 50, 50};
    bb_raster_push_clip(&rast, c1);
    bb_rect_t active = bb_raster_get_clip(&rast);
    assert(active.x == 10 && active.y == 10 && active.w == 50 && active.h == 50);

    /* Draw rect outside clip - should not affect pixels outside (10,10,50,50) */
    bb_rect_t out_rect = {0, 0, 5, 5};
    bb_raster_fill_rect(&rast, out_rect, BB_COLOR_BLACK);
    assert(pixels[0] == BB_COLOR_WHITE); /* untouched */

    /* Draw inside clip */
    bb_rect_t in_rect = {15, 15, 10, 10};
    bb_raster_fill_rect(&rast, in_rect, BB_COLOR_BLACK);
    assert(pixels[15 * 100 + 15] == BB_COLOR_BLACK);

    bb_raster_pop_clip(&rast);
    active = bb_raster_get_clip(&rast);
    assert(active.w == 100 && active.h == 100);

    printf("       -> ALL Rasterizer tests PASSED!\n");
}

int main(void) {
    run_rasterizer_tests();
    return 0;
}
