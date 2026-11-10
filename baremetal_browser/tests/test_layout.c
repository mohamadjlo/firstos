#include "../bb_types.h"
#include "../bb_libc.h"
#include "../bb_dom.h"
#include "../bb_html.h"
#include "../bb_css.h"
#include "../bb_layout.h"
#include <stdio.h>
#include <assert.h>

void run_layout_tests(void) {
    printf("[TEST] Running Box-Model Layout Tests...\n");

    const char *html = 
        "<div style=\"padding: 10px; margin: 5px;\">\n"
        "  <p style=\"height: 30px;\">Paragraph Content</p>\n"
        "  <button id=\"testbtn\">Click Me</button>\n"
        "</div>";

    bb_node_t *doc = bb_html_parse(html);
    bb_stylesheet_t *sheet = bb_css_create();
    bb_css_apply_styles(doc, sheet);

    bb_layout_box_t *root = bb_layout_build_tree(doc);
    assert(root != NULL);

    int32_t container_w = 800;
    bb_layout_compute(root, 0, 0, container_w);

    assert(root->dimensions.w == container_w);
    assert(root->first_child != NULL);

    bb_layout_box_t *div_box = root->first_child;
    /* padding: 10px, margin: 5px on 800px width -> content width = 800 - 10 - 20 = 770 */
    assert(div_box->dimensions.w == 770);
    assert(div_box->dimensions.x == 15); /* 5 margin + 10 padding */

    /* Hit-testing */
    bb_node_t *hit = bb_layout_hit_test(root, 20, 20, 0, 0);
    assert(hit != NULL);

    bb_layout_free_tree(root);
    bb_css_free(sheet);
    bb_node_free_tree(doc);

    printf("       -> ALL Layout tests PASSED!\n");
}

int main(void) {
    run_layout_tests();
    return 0;
}
