#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../bb_types.h"
#include "../bb_dom.h"
#include "../bb_css.h"
#include "../bb_layout.h"
#include "../hit_test.h"

static void test_z_index_stacking(void) {
    printf("[TEST] Running Z-Index Stacking Order Hit Tests...\n");

    bb_node_t *root_node = bb_node_create_element("body");
    root_node->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    root_node->computed_style->display = BB_DISPLAY_BLOCK;
    root_node->computed_style->position = BB_POSITION_STATIC;

    bb_layout_box_t *root = (bb_layout_box_t *)calloc(1, sizeof(bb_layout_box_t));
    root->type = BB_BOX_BLOCK;
    root->dimensions = (bb_rect_t){0, 0, 800, 600};
    root->node = root_node;

    /* Child A: z-index 5 */
    bb_node_t *node_a = bb_node_create_element("div");
    node_a->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    node_a->computed_style->display = BB_DISPLAY_BLOCK;
    node_a->computed_style->position = BB_POSITION_RELATIVE;
    node_a->computed_style->z_index = 5;

    bb_layout_box_t *box_a = (bb_layout_box_t *)calloc(1, sizeof(bb_layout_box_t));
    box_a->type = BB_BOX_BLOCK;
    box_a->dimensions = (bb_rect_t){20, 20, 100, 100};
    box_a->node = node_a;
    box_a->parent = root;

    /* Child B: z-index 10 (defined earlier or later, shouldn't matter) */
    bb_node_t *node_b = bb_node_create_element("div");
    node_b->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    node_b->computed_style->display = BB_DISPLAY_BLOCK;
    node_b->computed_style->position = BB_POSITION_RELATIVE;
    node_b->computed_style->z_index = 10;

    bb_layout_box_t *box_b = (bb_layout_box_t *)calloc(1, sizeof(bb_layout_box_t));
    box_b->type = BB_BOX_BLOCK;
    box_b->dimensions = (bb_rect_t){50, 50, 100, 100};
    box_b->node = node_b;
    box_b->parent = root;

    /* Link children to root */
    root->first_child = box_a;
    box_a->next_sibling = box_b;
    box_b->prev_sibling = box_a;
    root->last_child = box_b;

    bb_hit_test_result_t res;

    /* Point (30, 30) is inside box A only */
    bool hit = hit_test_document(root, 30, 30, &res);
    assert(hit);
    assert(res.box == box_a);
    assert(res.node == node_a);

    /* Point (120, 120) is inside box B only */
    hit = hit_test_document(root, 120, 120, &res);
    assert(hit);
    assert(res.box == box_b);
    assert(res.node == node_b);

    /* Point (70, 70) is in the overlap region: box B (z-index 10) must win! */
    hit = hit_test_document(root, 70, 70, &res);
    assert(hit);
    assert(res.box == box_b);
    assert(res.node == node_b);

    printf("  -> Z-Index Stacking Order Hit Tests PASSED!\n");
}

static void test_negative_z_index(void) {
    printf("[TEST] Running Negative Z-Index Hit Tests...\n");

    bb_node_t *root_node = bb_node_create_element("body");
    root_node->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    root_node->computed_style->display = BB_DISPLAY_BLOCK;
    root_node->computed_style->position = BB_POSITION_STATIC;

    bb_layout_box_t *root = (bb_layout_box_t *)calloc(1, sizeof(bb_layout_box_t));
    root->type = BB_BOX_BLOCK;
    root->dimensions = (bb_rect_t){0, 0, 800, 600};
    root->node = root_node;

    /* Box C: z-index -1 (underneath in-flow content) */
    bb_node_t *node_c = bb_node_create_element("div");
    node_c->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    node_c->computed_style->display = BB_DISPLAY_BLOCK;
    node_c->computed_style->position = BB_POSITION_RELATIVE;
    node_c->computed_style->z_index = -1;

    bb_layout_box_t *box_c = (bb_layout_box_t *)calloc(1, sizeof(bb_layout_box_t));
    box_c->type = BB_BOX_BLOCK;
    box_c->dimensions = (bb_rect_t){20, 20, 100, 100};
    box_c->node = node_c;
    box_c->parent = root;

    /* Box D: in-flow block (position static) */
    bb_node_t *node_d = bb_node_create_element("div");
    node_d->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    node_d->computed_style->display = BB_DISPLAY_BLOCK;
    node_d->computed_style->position = BB_POSITION_STATIC;

    bb_layout_box_t *box_d = (bb_layout_box_t *)calloc(1, sizeof(bb_layout_box_t));
    box_d->type = BB_BOX_BLOCK;
    box_d->dimensions = (bb_rect_t){20, 20, 100, 100};
    box_d->node = node_d;
    box_d->parent = root;

    root->first_child = box_c;
    box_c->next_sibling = box_d;
    box_d->prev_sibling = box_c;
    root->last_child = box_d;

    bb_hit_test_result_t res;
    /* Point (30, 30): in-flow Box D is on top of negative z-index Box C */
    bool hit = hit_test_document(root, 30, 30, &res);
    assert(hit);
    assert(res.box == box_d);
    assert(res.node == node_d);

    printf("  -> Negative Z-Index Hit Tests PASSED!\n");
}

static void test_overflow_clipping(void) {
    printf("[TEST] Running Overflow Clipping Hit Tests...\n");

    bb_node_t *parent_node = bb_node_create_element("div");
    parent_node->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    parent_node->computed_style->display = BB_DISPLAY_BLOCK;
    parent_node->computed_style->overflow = BB_OVERFLOW_HIDDEN;

    bb_layout_box_t *parent_box = (bb_layout_box_t *)calloc(1, sizeof(bb_layout_box_t));
    parent_box->type = BB_BOX_BLOCK;
    parent_box->dimensions = (bb_rect_t){10, 10, 50, 50};
    parent_box->node = parent_node;

    /* Child box overflows parent: 100x100 */
    bb_node_t *child_node = bb_node_create_element("div");
    child_node->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    child_node->computed_style->display = BB_DISPLAY_BLOCK;

    bb_layout_box_t *child_box = (bb_layout_box_t *)calloc(1, sizeof(bb_layout_box_t));
    child_box->type = BB_BOX_BLOCK;
    child_box->dimensions = (bb_rect_t){10, 10, 100, 100};
    child_box->node = child_node;
    child_box->parent = parent_box;

    parent_box->first_child = child_box;
    parent_box->last_child = child_box;

    bb_hit_test_result_t res;

    /* Inside parent bounds: child is hit */
    bool hit = hit_test_document(parent_box, 30, 30, &res);
    assert(hit);
    assert(res.box == child_box);

    /* Outside parent bounds (75, 75): clipped, must NOT hit child */
    hit = hit_test_document(parent_box, 75, 75, &res);
    assert(!hit);

    printf("  -> Overflow Clipping Hit Tests PASSED!\n");
}

static void test_text_runs_and_links(void) {
    printf("[TEST] Running Text Runs & Link Hit Tests...\n");

    /* Create <a> link node */
    bb_node_t *link_node = bb_node_create_element("a");
    bb_node_set_attribute(link_node, "href", "https://firstos.org/docs");
    bb_node_set_attribute(link_node, "title", "FirstOS Documentation");
    link_node->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    link_node->computed_style->display = BB_DISPLAY_INLINE;

    bb_layout_box_t *link_box = (bb_layout_box_t *)calloc(1, sizeof(bb_layout_box_t));
    link_box->type = BB_BOX_INLINE;
    link_box->dimensions = (bb_rect_t){100, 100, 88, 16};
    link_box->node = link_node;

    /* Anonymous text child "Hello World" */
    bb_layout_box_t *text_box = (bb_layout_box_t *)calloc(1, sizeof(bb_layout_box_t));
    text_box->type = BB_BOX_ANONYMOUS_TEXT;
    text_box->dimensions = (bb_rect_t){100, 100, 88, 16};
    text_box->text = strdup("Hello World");
    text_box->node = link_node;
    text_box->parent = link_box;
    text_box->font_style.scale = 1;

    link_box->first_child = text_box;
    link_box->last_child = text_box;

    bb_hit_test_result_t res;

    /* Hit character 0 ('H') at x=102 */
    bool hit = hit_test_document(link_box, 102, 105, &res);
    assert(hit);
    assert(res.box == text_box);
    assert(res.is_text);
    assert(res.text_offset == 0);
    assert(res.is_link);
    assert(res.cursor == BB_CURSOR_POINTER);
    assert(res.href != NULL && strcmp(res.href, "https://firstos.org/docs") == 0);
    assert(res.title != NULL && strcmp(res.title, "FirstOS Documentation") == 0);
    assert(res.is_interactive);
    assert(res.is_activatable);

    /* Hit character 6 ('W') at x=149 (149 - 100 = 49px / 8 = 6) */
    hit = hit_test_document(link_box, 149, 105, &res);
    assert(hit);
    assert(res.box == text_box);
    assert(res.text_offset == 6);

    printf("  -> Text Runs & Link Hit Tests PASSED!\n");
}

static void test_visibility_hidden(void) {
    printf("[TEST] Running Visibility Hidden Hit Tests...\n");

    bb_node_t *node = bb_node_create_element("div");
    node->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    node->computed_style->display = BB_DISPLAY_BLOCK;
    node->computed_style->visibility = BB_VISIBILITY_HIDDEN;

    bb_layout_box_t *box = (bb_layout_box_t *)calloc(1, sizeof(bb_layout_box_t));
    box->type = BB_BOX_BLOCK;
    box->dimensions = (bb_rect_t){50, 50, 100, 100};
    box->node = node;

    bb_hit_test_result_t res;
    bool hit = hit_test_document(box, 75, 75, &res);
    assert(!hit);

    printf("  -> Visibility Hidden Hit Tests PASSED!\n");
}

static void test_viewport_scroll(void) {
    printf("[TEST] Running Viewport & Scroll Hit Tests...\n");

    bb_node_t *node = bb_node_create_element("div");
    node->computed_style = (bb_style_t *)calloc(1, sizeof(bb_style_t));
    node->computed_style->display = BB_DISPLAY_BLOCK;

    bb_layout_box_t *box = (bb_layout_box_t *)calloc(1, sizeof(bb_layout_box_t));
    box->type = BB_BOX_BLOCK;
    /* Box is at document y = 500 */
    box->dimensions = (bb_rect_t){50, 500, 200, 100};
    box->node = node;

    bb_rect_t vp_clip = { 0, 70, 800, 500 };
    bb_hit_test_result_t res;

    /* When scroll_y = 0: box is at screen y = 70 + 500 = 570, which is outside viewport clip (70..570) */
    bool hit = hit_test_viewport(box, 100, 570, 0, 0, &vp_clip, &res);
    assert(!hit);

    /* When scroll_y = 400: box is at screen y = 70 + (500 - 400) = 170 (inside viewport clip) */
    hit = hit_test_viewport(box, 100, 170, 0, 400, &vp_clip, &res);
    assert(hit);
    assert(res.box == box);

    /* Test node_at_point helper */
    bb_node_t *found = node_at_point(box, 100, 550);
    assert(found == node);

    printf("  -> Viewport & Scroll Hit Tests PASSED!\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Browser Hit Testing & Interaction Test Suite\n");
    printf("============================================================\n");

    test_z_index_stacking();
    test_negative_z_index();
    test_overflow_clipping();
    test_text_runs_and_links();
    test_visibility_hidden();
    test_viewport_scroll();

    printf("\nAll Browser Hit Testing & Interaction Tests PASSED!\n");
    return 0;
}
