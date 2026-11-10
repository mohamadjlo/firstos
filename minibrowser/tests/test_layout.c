#include "css_parser.h"
#include "dom.h"
#include "html_parser.h"
#include "layout.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    printf("[test_layout] Running Layout Engine tests...\n");

    const char *html =
        "<html><head><style>"
        "body { margin: 0; padding: 0; }"
        "#box1 { width: 400px; height: 100px; margin: 10px; padding: 5px; }"
        "#box2 { width: 300px; height: 50px; margin: 10px; padding: 0; }"
        "</style></head><body>"
        "<div id=\"box1\"></div>"
        "<div id=\"box2\"></div>"
        "</body></html>";

    DOMNode *doc = html_parse(html);
    assert(doc != NULL);

    /* Parse and apply inline style */
    const char *css =
        "body { margin: 0; padding: 0; }\n"
        "#box1 { width: 400px; height: 100px; margin: 10px; padding: 5px; }\n"
        "#box2 { width: 300px; height: 50px; margin: 10px; padding: 0; }\n";
    Stylesheet *sheet = css_parse(css);
    css_apply_stylesheet(doc, sheet);
    stylesheet_free(sheet);

    LayoutBox *root = layout_build_tree(doc);
    assert(root != NULL);

    /* Run layout calculation for an 800x600 viewport */
    layout_calculate(root, 800.0f, 600.0f);

    /* Verify root layout */
    assert(root->dimensions.w == 800.0f);

    DOMNode *node_box1 = dom_node_find_by_tag(doc, "div");
    assert(node_box1 != NULL);
    LayoutBox *box1 = node_box1->layout_box;
    assert(box1 != NULL);
    assert(box1->dimensions.w == 400.0f);
    assert(box1->dimensions.h == 100.0f);
    assert(box1->padding.top == 5.0f);
    assert(box1->margin.top == 10.0f);

    DOMNode *node_box2 = node_box1->next_sibling;
    assert(node_box2 != NULL);
    LayoutBox *box2 = node_box2->layout_box;
    assert(box2 != NULL);
    assert(box2->dimensions.w == 300.0f);
    assert(box2->dimensions.h == 50.0f);

    /* Verify vertical stacking: box2 y must be below box1 */
    float box1_bottom = box1->dimensions.y + box1->dimensions.h + box1->padding.bottom + box1->margin.bottom;
    assert(box2->dimensions.y >= box1_bottom);

    /* Test Hit Testing */
    const LayoutBox *hit1 = layout_hit_test(root, box1->dimensions.x + 10, box1->dimensions.y + 10);
    assert(hit1 == box1);

    const LayoutBox *hit2 = layout_hit_test(root, box2->dimensions.x + 10, box2->dimensions.y + 10);
    assert(hit2 == box2);

    layout_box_free(root);
    dom_node_free(doc);

    printf("[test_layout] All Layout Engine tests passed successfully!\n");
    return 0;
}
