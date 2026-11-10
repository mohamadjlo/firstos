#define _POSIX_C_SOURCE 200809L
#include "dom.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    printf("[test_dom] Running DOM tests...\n");

    DOMNode *doc = dom_node_create(NODE_DOCUMENT);
    assert(doc != NULL);
    assert(doc->type == NODE_DOCUMENT);

    DOMNode *html = dom_node_create(NODE_ELEMENT);
    html->tag_name = strdup("html");
    dom_node_append_child(doc, html);
    assert(doc->first_child == html);
    assert(doc->last_child == html);
    assert(html->parent == doc);

    DOMNode *body = dom_node_create(NODE_ELEMENT);
    body->tag_name = strdup("body");
    dom_node_append_child(html, body);

    DOMNode *h1 = dom_node_create(NODE_ELEMENT);
    h1->tag_name = strdup("h1");
    dom_node_set_attribute(h1, "id", "main-title");
    dom_node_set_attribute(h1, "class", "heading primary");
    dom_node_append_child(body, h1);

    DOMNode *h1_text = dom_node_create(NODE_TEXT);
    dom_node_set_text(h1_text, "Welcome to MiniBrowser");
    dom_node_append_child(h1, h1_text);

    DOMNode *p = dom_node_create(NODE_ELEMENT);
    p->tag_name = strdup("p");
    dom_node_append_child(body, p);

    DOMNode *p_text = dom_node_create(NODE_TEXT);
    dom_node_set_text(p_text, "A fast lightweight browser engine in C11.");
    dom_node_append_child(p, p_text);

    /* Test Attributes */
    const char *id_val = dom_node_get_attribute(h1, "id");
    assert(id_val != NULL && strcmp(id_val, "main-title") == 0);

    /* Test Classes */
    assert(dom_node_has_class(h1, "heading") == true);
    assert(dom_node_has_class(h1, "primary") == true);
    assert(dom_node_has_class(h1, "secondary") == false);

    /* Test Search */
    DOMNode *found_body = dom_node_find_by_tag(doc, "body");
    assert(found_body == body);
    DOMNode *found_h1 = dom_node_find_by_tag(doc, "h1");
    assert(found_h1 == h1);

    /* Test Siblings */
    assert(h1->next_sibling == p);
    assert(p->prev_sibling == h1);

    dom_node_print_tree(doc, 0);

    dom_node_free(doc);
    printf("[test_dom] All DOM tests passed successfully!\n");
    return 0;
}
