#include "html_parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("[test_html] Running HTML Tokenizer & DOM Builder tests...\n");

    const char *html = 
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>Test Page</title>\n"
        "    <meta charset=\"utf-8\" />\n"
        "</head>\n"
        "<body>\n"
        "    <!-- This is a comment -->\n"
        "    <div class=\"container\" id=\"main\">\n"
        "        <h1>Hello World</h1>\n"
        "        <p>Paragraph with <a href='https://example.com' target=\"_blank\">link</a> text.</p>\n"
        "        <img src=\"logo.png\" alt=\"Logo\" />\n"
        "        <br>\n"
        "    </div>\n"
        "</body>\n"
        "</html>";

    DOMNode *doc = html_parse(html);
    assert(doc != NULL);
    assert(doc->type == NODE_DOCUMENT);

    DOMNode *html_node = dom_node_find_by_tag(doc, "html");
    assert(html_node != NULL);

    DOMNode *head_node = dom_node_find_by_tag(doc, "head");
    assert(head_node != NULL);

    DOMNode *title_node = dom_node_find_by_tag(doc, "title");
    assert(title_node != NULL);
    assert(title_node->first_child != NULL);
    assert(title_node->first_child->type == NODE_TEXT);
    assert(strcmp(title_node->first_child->text_content, "Test Page") == 0);

    DOMNode *body_node = dom_node_find_by_tag(doc, "body");
    assert(body_node != NULL);

    DOMNode *div_node = dom_node_find_by_tag(doc, "div");
    assert(div_node != NULL);
    assert(dom_node_has_class(div_node, "container"));
    const char *div_id = dom_node_get_attribute(div_node, "id");
    assert(div_id != NULL && strcmp(div_id, "main") == 0);

    DOMNode *a_node = dom_node_find_by_tag(doc, "a");
    assert(a_node != NULL);
    const char *href = dom_node_get_attribute(a_node, "href");
    assert(href != NULL && strcmp(href, "https://example.com") == 0);
    const char *target = dom_node_get_attribute(a_node, "target");
    assert(target != NULL && strcmp(target, "_blank") == 0);

    DOMNode *img_node = dom_node_find_by_tag(doc, "img");
    assert(img_node != NULL);
    const char *src = dom_node_get_attribute(img_node, "src");
    assert(src != NULL && strcmp(src, "logo.png") == 0);

    dom_node_print_tree(doc, 0);

    dom_node_free(doc);
    printf("[test_html] All HTML parser tests passed successfully!\n");
    return 0;
}
