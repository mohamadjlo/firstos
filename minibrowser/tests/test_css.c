#include "css_parser.h"
#include "dom.h"
#include "html_parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("[test_css] Running CSS Parser & Cascade tests...\n");

    const char *css =
        "* { box-sizing: border-box; }\n"
        "body { background-color: #ffffff; color: #111111; margin: 0; }\n"
        "h1 { font-size: 32px; color: #ff0000; margin: 16px 0; }\n"
        ".highlight { background-color: #ffff00; color: #0000ff; padding: 10px 20px; }\n"
        "#main-header { border: 2px solid #333333; }\n"
        "p { font-size: 14px; margin: 8px 0; }\n";

    Stylesheet *sheet = css_parse(css);
    assert(sheet != NULL);
    assert(sheet->rules != NULL);

    /* Verify rules specificity */
    int rule_count = 0;
    for (CSSRule *r = sheet->rules; r; r = r->next) {
        rule_count++;
        assert(r->selectors != NULL);
        assert(r->declarations != NULL);
    }
    assert(rule_count >= 6);

    const char *html =
        "<html><body>\n"
        "<h1 id=\"main-header\" class=\"highlight\">Hello World</h1>\n"
        "<p>This is a paragraph.</p>\n"
        "</body></html>";

    DOMNode *doc = html_parse(html);
    assert(doc != NULL);

    /* Apply stylesheet to DOM tree */
    css_apply_stylesheet(doc, sheet);

    DOMNode *h1 = dom_node_find_by_tag(doc, "h1");
    assert(h1 != NULL);
    assert(h1->style != NULL);

    /* h1 matched tag 'h1', class '.highlight', and id '#main-header' */
    /* Due to cascade order and specificity, class sets background to #ffff00, id sets border_width to 2 */
    assert(h1->style->font_size == 32.0f);
    assert(h1->style->background_color.r == 255);
    assert(h1->style->background_color.g == 255);
    assert(h1->style->background_color.b == 0);
    assert(h1->style->border_width == 2.0f);
    assert(h1->style->padding.top == 10.0f);
    assert(h1->style->padding.right == 20.0f);
    assert(h1->style->padding.bottom == 10.0f);
    assert(h1->style->padding.left == 20.0f);

    DOMNode *p = dom_node_find_by_tag(doc, "p");
    assert(p != NULL);
    assert(p->style != NULL);
    assert(p->style->font_size == 14.0f);
    assert(p->style->margin.top == 8.0f);
    assert(p->style->margin.bottom == 8.0f);

    stylesheet_free(sheet);
    dom_node_free(doc);

    printf("[test_css] All CSS tests passed successfully!\n");
    return 0;
}
