#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wincompatible-library-redeclaration"
#endif

int printf(const char *format, ...);
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, unsigned int n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, unsigned int n);
void *memcpy(void *dest, const void *src, unsigned int n);
void *memset(void *s, int c, unsigned int n);

#include "../userspace/browser/include/firstos_renderer.h"

extern void firstos_renderer_apply_styles(firstos_renderer_context_t *ctx);
extern int  firstos_renderer_add_stylesheet(firstos_renderer_context_t *ctx, const char *css_text, int len);
extern void firstos_renderer_layout(firstos_renderer_context_t *ctx, uint32_t view_w, uint32_t view_h);

void test_css_variables_definition_and_resolution(void) {
    firstos_renderer_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    html_node_t root;
    memset(&root, 0, sizeof(root));
    root.id_num = 1;
    root.tag = HTML_TAG_HTML;
    ctx.root_dom = &root;

    html_node_t card;
    memset(&card, 0, sizeof(card));
    card.id_num = 2;
    card.tag = HTML_TAG_DIV;
    card.class_name = "card";
    card.parent = &root;
    root.first_child = &card;

    const char *css = 
        ":root {\n"
        "  --primary: #0A84FF;\n"
        "  --pad: 18px;\n"
        "  --text-c: red;\n"
        "}\n"
        ".card {\n"
        "  background-color: var(--primary);\n"
        "  padding: var(--pad);\n"
        "  color: var(--text-c);\n"
        "}\n";

    int rc = firstos_renderer_add_stylesheet(&ctx, css, strlen(css));
    assert(rc == 0);

    /* Assert root parsed CSS variables */
    assert(root.var_table.count >= 3);

    /* Assert .card resolved variables correctly */
    assert(card.style.has_bg == 1);
    assert(card.style.bg_color == 0x0A84FF);
    assert(card.style.padding_top == 18);
    assert(card.style.padding_bottom == 18);
    assert(card.style.padding_left == 18);
    assert(card.style.padding_right == 18);
    assert(card.style.has_explicit_color == 1);
    assert(card.style.color == 0xFF3B30); /* Red */

    printf("PASS: test_css_variables_definition_and_resolution\n");
}

void test_flexbox_wrapping_layout(void) {
    firstos_renderer_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    html_node_t root;
    memset(&root, 0, sizeof(root));
    root.id_num = 1;
    root.tag = HTML_TAG_BODY;
    ctx.root_dom = &root;

    html_node_t flex_container;
    memset(&flex_container, 0, sizeof(flex_container));
    flex_container.id_num = 2;
    flex_container.tag = HTML_TAG_DIV;
    flex_container.style.is_block = 1;
    flex_container.style.display_mode = CSS_DISPLAY_FLEX;
    flex_container.style.flex_direction = CSS_FLEX_DIR_ROW;
    flex_container.style.flex_wrap = CSS_FLEX_WRAP;
    flex_container.style.gap_row = 10;
    flex_container.style.gap_col = 10;
    flex_container.style.width = 300;
    flex_container.parent = &root;
    root.first_child = &flex_container;

    /* Create 4 child items of width 120px, height 40px */
    html_node_t items[4];
    for (int i = 0; i < 4; i++) {
        memset(&items[i], 0, sizeof(html_node_t));
        items[i].id_num = 10 + i;
        items[i].tag = HTML_TAG_DIV;
        items[i].style.width = 120;
        items[i].style.height = 40;
        items[i].parent = &flex_container;
        if (i == 0) flex_container.first_child = &items[0];
        else items[i - 1].next_sibling = &items[i];
    }

    firstos_renderer_layout(&ctx, 800, 600);

    /*
     * Row 0:
     *   Item 0: x=0, y=0, w=120, h=40
     *   Item 1: x=130, y=0, w=120, h=40
     * Row 1 (wrapped because 130 + 120 + 10 + 120 = 380 > 300):
     *   Item 2: x=0, y=50 (40 + 10 gap), w=120, h=40
     *   Item 3: x=130, y=50, w=120, h=40
     */
    assert(items[0].bounds.y == items[1].bounds.y);
    assert(items[2].bounds.y == items[3].bounds.y);
    assert(items[2].bounds.y == items[0].bounds.y + 40 + 10);
    assert(items[0].bounds.x == 0);
    assert(items[1].bounds.x == 130);
    assert(items[2].bounds.x == 0);
    assert(items[3].bounds.x == 130);

    printf("PASS: test_flexbox_wrapping_layout\n");
}

void test_css_grid_column_template_layout(void) {
    firstos_renderer_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    html_node_t root;
    memset(&root, 0, sizeof(root));
    root.id_num = 1;
    root.tag = HTML_TAG_BODY;
    ctx.root_dom = &root;

    html_node_t grid_container;
    memset(&grid_container, 0, sizeof(grid_container));
    grid_container.id_num = 2;
    grid_container.tag = HTML_TAG_DIV;
    grid_container.style.is_block = 1;
    grid_container.style.display_mode = CSS_DISPLAY_GRID;
    grid_container.style.grid_columns = 3;
    grid_container.style.gap_col = 15;
    grid_container.style.gap_row = 15;
    grid_container.style.width = 630; /* 630 - (2 * 15) = 600. 600 / 3 = 200 per col */
    grid_container.parent = &root;
    root.first_child = &grid_container;

    /* 6 children: 2 rows of 3 columns */
    html_node_t items[6];
    for (int i = 0; i < 6; i++) {
        memset(&items[i], 0, sizeof(html_node_t));
        items[i].id_num = 20 + i;
        items[i].tag = HTML_TAG_DIV;
        items[i].style.height = 50;
        items[i].parent = &grid_container;
        if (i == 0) grid_container.first_child = &items[0];
        else items[i - 1].next_sibling = &items[i];
    }

    firstos_renderer_layout(&ctx, 800, 600);

    for (int i = 0; i < 6; i++) {
        assert(items[i].bounds.w == 200);
    }

    /* Row 0 check */
    assert(items[0].bounds.x == 0);
    assert(items[1].bounds.x == 200 + 15);
    assert(items[2].bounds.x == (200 + 15) * 2);
    assert(items[0].bounds.y == items[1].bounds.y);
    assert(items[1].bounds.y == items[2].bounds.y);

    /* Row 1 check */
    assert(items[3].bounds.x == 0);
    assert(items[4].bounds.x == 200 + 15);
    assert(items[5].bounds.x == (200 + 15) * 2);
    assert(items[3].bounds.y == items[0].bounds.y + 50 + 15);
    assert(items[3].bounds.y == items[4].bounds.y);
    assert(items[4].bounds.y == items[5].bounds.y);

    printf("PASS: test_css_grid_column_template_layout\n");
}

int main(void) {
    printf("=== Starting Phase 6C Advanced CSS Engine Unit Tests ===\n");
    test_css_variables_definition_and_resolution();
    test_flexbox_wrapping_layout();
    test_css_grid_column_template_layout();
    printf("=== All 3 Advanced CSS Engine Unit Tests PASSED ===\n");
    return 0;
}
