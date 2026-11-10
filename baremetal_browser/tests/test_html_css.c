#include "../bb_types.h"
#include "../bb_libc.h"
#include "../bb_dom.h"
#include "../bb_html.h"
#include "../bb_css.h"
#include <stdio.h>
#include <assert.h>

void run_html_css_tests(void) {
    printf("[TEST] Running HTML5 Lexer & CSS Cascade Tests...\n");

    const char *html = 
        "<div id=\"main\" class=\"container\">\n"
        "  <h1 class=\"title\">FirstOS Browser</h1>\n"
        "  <p style=\"color: #ff0000;\">Welcome &amp; enjoy!</p>\n"
        "  <a href=\"http://test.local\">Link</a>\n"
        "</div>";

    bb_node_t *doc = bb_html_parse(html);
    assert(doc != NULL);
    assert(doc->type == BB_NODE_DOCUMENT);
    assert(doc->first_child != NULL);

    /* Check <div> element */
    bb_node_t *div = doc->first_child;
    assert(div->type == BB_NODE_ELEMENT);
    assert(bb_strcmp(div->tag_name, "div") == 0);
    assert(bb_strcmp(bb_node_get_attribute(div, "id"), "main") == 0);
    assert(bb_node_has_class(div, "container") == true);

    /* Check children */
    bb_node_t *h1 = div->first_child;
    assert(h1 != NULL);
    assert(bb_strcmp(h1->tag_name, "h1") == 0);

    /* CSS Parsing & Cascade */
    const char *css = 
        "#main { background-color: #f0f0f0; padding: 10px; }\n"
        ".title { color: #0000ff; }\n"
        "p { margin: 15px; }";

    bb_stylesheet_t *sheet = bb_css_create();
    bb_css_parse_rules(sheet, css);
    assert(sheet->rules != NULL);

    bb_css_apply_styles(doc, sheet);

    /* Verify resolved styles */
    assert(div->computed_style != NULL);
    assert(div->computed_style->padding_top == 10);
    assert(div->computed_style->background_color == BB_RGB(0xf0, 0xf0, 0xf0));

    assert(h1->computed_style != NULL);
    assert(h1->computed_style->color == BB_RGB(0x00, 0x00, 0xff));

    bb_node_t *p = h1->next_sibling;
    assert(p != NULL);
    assert(p->computed_style != NULL);
    assert(p->computed_style->margin_top == 15);
    /* Inline style override test (color: #ff0000) */
    assert(p->computed_style->color == BB_RGB(0xff, 0x00, 0x00));

    bb_css_free(sheet);
    bb_node_free_tree(doc);

    printf("       -> ALL HTML & CSS tests PASSED!\n");
}

int main(void) {
    run_html_css_tests();
    return 0;
}
