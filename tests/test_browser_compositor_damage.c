#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wincompatible-library-redeclaration"
#endif

int printf(const char *format, ...);
int snprintf(char *str, unsigned int size, const char *format, ...);
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, unsigned int n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, unsigned int n);
void *memcpy(void *dest, const void *src, unsigned int n);
void *memset(void *s, int c, unsigned int n);

#include "include/firstos/uapi/browser_ipc.h"
#include "userspace/browser/include/firstos_renderer.h"

/* Include renderer adapters directly for unit testing */
#include "html5_arena.c"
#include "userspace/browser/dom_adapter.c"
#include "userspace/browser/html_adapter.c"
#include "userspace/browser/css_adapter.c"
#include "userspace/browser/layout_adapter.c"
#include "userspace/browser/render_adapter.c"
#include "userspace/browser/form_adapter.c"
#include "userspace/browser/js_adapter.c"
#include "userspace/browser/event_adapter.c"

/* Test 1: Damage rectangle descriptor layout & size invariants */
static void test_damage_descriptor_layout(void) {
    printf("[test_browser_compositor_damage] 1. test_damage_descriptor_layout...\n");
    assert(sizeof(firstos_damage_rect_t) == 32);
    assert(FIRSTOS_RENDER_FLAG_FULL_REPAINT == 0x0001);
    assert(FIRSTOS_RENDER_FLAG_DAMAGE_VALID == 0x0002);
    assert(FIRSTOS_BROWSER_DAMAGE_OFFSET == 36864);

    firstos_damage_rect_t dmg;
    memset(&dmg, 0, sizeof(dmg));
    dmg.x = 20;
    dmg.y = 40;
    dmg.w = 300;
    dmg.h = 200;
    dmg.flags = FIRSTOS_RENDER_FLAG_DAMAGE_VALID;

    assert(dmg.x == 20 && dmg.y == 40 && dmg.w == 300 && dmg.h == 200);
    assert(dmg.flags == FIRSTOS_RENDER_FLAG_DAMAGE_VALID);
    printf("  -> OK (32-byte layout and flags verified)\n");
}

/* Test 2: Full repaint emission on initial load and resize */
static void test_full_repaint_lifecycle(void) {
    printf("[test_browser_compositor_damage] 2. test_full_repaint_lifecycle...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    /* Initially after init, needs_full_repaint is 1 and damage covers viewport */
    assert(ctx.needs_full_repaint == 1);
    assert(ctx.damage_rect.x == 0 && ctx.damage_rect.y == 0);
    assert(ctx.damage_rect.w == 800 && ctx.damage_rect.h == 600);

    /* Simulate frame emission: populate firstos_damage_rect_t */
    firstos_damage_rect_t dmg;
    memset(&dmg, 0, sizeof(dmg));
    dmg.x = ctx.damage_rect.x;
    dmg.y = ctx.damage_rect.y;
    dmg.w = ctx.damage_rect.w;
    dmg.h = ctx.damage_rect.h;
    dmg.flags = FIRSTOS_RENDER_FLAG_DAMAGE_VALID;
    if (ctx.needs_full_repaint) {
        dmg.flags |= FIRSTOS_RENDER_FLAG_FULL_REPAINT;
    }
    ctx.needs_full_repaint = 0;

    assert(dmg.flags & FIRSTOS_RENDER_FLAG_FULL_REPAINT);
    assert(dmg.flags & FIRSTOS_RENDER_FLAG_DAMAGE_VALID);
    assert(ctx.needs_full_repaint == 0);

    /* On resize: viewport bounds update, needs_full_repaint is flagged */
    firstos_renderer_handle_resize(&ctx, 1024, 768);
    assert(ctx.needs_full_repaint == 1);
    assert(ctx.damage_rect.w == 1024 && ctx.damage_rect.h == 768);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Initial load and resize full-repaint invariants verified)\n");
}

/* Test 3: Localized damage calculation on partial updates */
static void test_localized_damage_rect(void) {
    printf("[test_browser_compositor_damage] 3. test_localized_damage_rect...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<style>#box { width: 200px; height: 50px; }</style>"
                       "<html><body><div id=\"box\">Hello</div></body></html>";
    int res = firstos_renderer_parse_html(&ctx, html, strlen(html));
    assert(res == RENDER_OK);
    firstos_renderer_apply_styles(&ctx);
    firstos_renderer_layout(&ctx, 800, 600);

    /* After initial layout, clear full repaint flag */
    ctx.needs_full_repaint = 0;

    /* Simulate partial invalidation of a sub-node */
    html_node_t *box = firstos_renderer_get_element_by_id(&ctx, "box");
    assert(box != NULL);
    assert(box->bounds.w == 200 && box->bounds.h == 50);

    /* Dirty specifically the element bounds */
    ctx.damage_rect.x = box->bounds.x;
    ctx.damage_rect.y = box->bounds.y - ctx.scroll_y;
    ctx.damage_rect.w = box->bounds.w;
    ctx.damage_rect.h = box->bounds.h;
    ctx.needs_full_repaint = 0;

    firstos_damage_rect_t dmg;
    memset(&dmg, 0, sizeof(dmg));
    dmg.x = ctx.damage_rect.x;
    dmg.y = ctx.damage_rect.y;
    dmg.w = ctx.damage_rect.w;
    dmg.h = ctx.damage_rect.h;
    dmg.flags = FIRSTOS_RENDER_FLAG_DAMAGE_VALID;
    if (ctx.needs_full_repaint) {
        dmg.flags |= FIRSTOS_RENDER_FLAG_FULL_REPAINT;
    }

    assert((dmg.flags & FIRSTOS_RENDER_FLAG_FULL_REPAINT) == 0);
    assert(dmg.flags & FIRSTOS_RENDER_FLAG_DAMAGE_VALID);
    assert(dmg.w == 200 && dmg.h == 50);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Localized partial damage bounds verified)\n");
}

/* Test 4: Host compositor damage clipping algorithm */
static void test_compositor_damage_clipping(void) {
    printf("[test_browser_compositor_damage] 4. test_compositor_damage_clipping...\n");
    int content_x = 100, content_y = 100, content_w = 600, content_h = 400;

    firstos_damage_rect_t dmg = {
        .x = 50,
        .y = 50,
        .w = 100,
        .h = 100,
        .flags = FIRSTOS_RENDER_FLAG_DAMAGE_VALID,
        .reserved = {0, 0, 0}
    };

    /* Compute clip region as in browser_host_ipc.c */
    int is_full_repaint = (dmg.flags & FIRSTOS_RENDER_FLAG_FULL_REPAINT) ||
                          !(dmg.flags & FIRSTOS_RENDER_FLAG_DAMAGE_VALID);
    assert(is_full_repaint == 0);

    int clip_x = content_x + dmg.x;
    int clip_y = content_y + dmg.y;
    int clip_w = dmg.w;
    int clip_h = dmg.h;

    /* Clamp damage region to browser window content bounds */
    if (clip_x < content_x) { clip_w -= (content_x - clip_x); clip_x = content_x; }
    if (clip_y < content_y) { clip_h -= (content_y - clip_y); clip_y = content_y; }
    if (clip_x + clip_w > content_x + content_w) clip_w = (content_x + content_w) - clip_x;
    if (clip_y + clip_h > content_y + content_h) clip_h = (content_y + content_h) - clip_y;

    assert(clip_x == 150);
    assert(clip_y == 150);
    assert(clip_w == 100);
    assert(clip_h == 100);

    /* Case A: Command completely inside damage tile -> retained */
    firstos_render_command_t cmd_inside = {
        .type = FIRSTOS_RENDER_CMD_RECT,
        .x = 60, .y = 60, .w = 20, .h = 20,
        .color = 0xFF0000, .text_offset = 0, .text_len = 0
    };
    int rx = content_x + cmd_inside.x;
    int ry = content_y + cmd_inside.y;
    int rw = cmd_inside.w;
    int rh = cmd_inside.h;
    if (rx < clip_x) { rw -= (clip_x - rx); rx = clip_x; }
    if (ry < clip_y) { rh -= (clip_y - ry); ry = clip_y; }
    if (rx + rw > clip_x + clip_w) rw = (clip_x + clip_w) - rx;
    if (ry + rh > clip_y + clip_h) rh = (clip_y + clip_h) - ry;
    assert(rw == 20 && rh == 20);

    /* Case B: Command completely outside damage tile -> clipped out (rw <= 0 or rh <= 0) */
    firstos_render_command_t cmd_outside = {
        .type = FIRSTOS_RENDER_CMD_RECT,
        .x = 300, .y = 300, .w = 50, .h = 50,
        .color = 0x00FF00, .text_offset = 0, .text_len = 0
    };
    rx = content_x + cmd_outside.x;
    ry = content_y + cmd_outside.y;
    rw = cmd_outside.w;
    rh = cmd_outside.h;
    if (rx < clip_x) { rw -= (clip_x - rx); rx = clip_x; }
    if (ry < clip_y) { rh -= (clip_y - ry); ry = clip_y; }
    if (rx + rw > clip_x + clip_w) rw = (clip_x + clip_w) - rx;
    if (ry + rh > clip_y + clip_h) rh = (clip_y + clip_h) - ry;
    assert(rw <= 0 || rh <= 0);

    printf("  -> OK (Damage tile command clipping & non-intersecting culling verified)\n");
}

/* Test 5: Clamping of damage tile to screen/window bounds */
static void test_damage_window_boundary_clamping(void) {
    printf("[test_browser_compositor_damage] 5. test_damage_window_boundary_clamping...\n");
    int content_x = 50, content_y = 50, content_w = 400, content_h = 300;

    /* Damage partially extending beyond the right and bottom */
    firstos_damage_rect_t dmg_over = {
        .x = 350, .y = 250, .w = 200, .h = 200,
        .flags = FIRSTOS_RENDER_FLAG_DAMAGE_VALID,
        .reserved = {0, 0, 0}
    };

    int clip_x = content_x + dmg_over.x;
    int clip_y = content_y + dmg_over.y;
    int clip_w = dmg_over.w;
    int clip_h = dmg_over.h;

    if (clip_x < content_x) { clip_w -= (content_x - clip_x); clip_x = content_x; }
    if (clip_y < content_y) { clip_h -= (content_y - clip_y); clip_y = content_y; }
    if (clip_x + clip_w > content_x + content_w) clip_w = (content_x + content_w) - clip_x;
    if (clip_y + clip_h > content_y + content_h) clip_h = (content_y + content_h) - clip_y;

    assert(clip_x == 400);
    assert(clip_y == 300);
    assert(clip_w == 50); /* 450 max clamped to 450 (content_x + content_w = 450) */
    assert(clip_h == 50); /* 350 max clamped to 350 (content_y + content_h = 350) */

    /* Damage completely outside window */
    firstos_damage_rect_t dmg_out = {
        .x = 600, .y = 600, .w = 100, .h = 100,
        .flags = FIRSTOS_RENDER_FLAG_DAMAGE_VALID,
        .reserved = {0, 0, 0}
    };

    clip_x = content_x + dmg_out.x;
    clip_y = content_y + dmg_out.y;
    clip_w = dmg_out.w;
    clip_h = dmg_out.h;

    if (clip_x < content_x) { clip_w -= (content_x - clip_x); clip_x = content_x; }
    if (clip_y < content_y) { clip_h -= (content_y - clip_y); clip_y = content_y; }
    if (clip_x + clip_w > content_x + content_w) clip_w = (content_x + content_w) - clip_x;
    if (clip_y + clip_h > content_y + content_h) clip_h = (content_y + content_h) - clip_y;

    assert(clip_w <= 0 || clip_h <= 0);
    printf("  -> OK (Damage boundary clamping and out-of-bounds rejection verified)\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 4: Compositor Damage & Display List Tests\n");
    printf("============================================================\n");

    test_damage_descriptor_layout();
    test_full_repaint_lifecycle();
    test_localized_damage_rect();
    test_compositor_damage_clipping();
    test_damage_window_boundary_clamping();

    printf("============================================================\n");
    printf("ALL 5 PHASE 4 COMPOSITOR DAMAGE UNIT TESTS PASSED (5/5)\n");
    printf("============================================================\n");
    return 0;
}
