#ifndef DOM_H
#define DOM_H

#include <stdbool.h>
#include <stddef.h>

struct ComputedStyle;
struct LayoutBox;

typedef enum {
    NODE_DOCUMENT,
    NODE_ELEMENT,
    NODE_TEXT
} NodeType;

typedef struct Attribute {
    char *key;
    char *value;
    struct Attribute *next;
} Attribute;

typedef struct DOMNode {
    NodeType type;
    char *tag_name;          /* Lowercase tag name for elements (e.g., "div", "p", "a") */
    char *text_content;      /* String content for text nodes or inner text */
    char *inner_text;        /* Computed or explicit inner text of the node */
    Attribute *attributes;   /* Singly-linked list of key/value pairs */

    struct DOMNode *parent;
    struct DOMNode *first_child;
    struct DOMNode *last_child;
    struct DOMNode *next_sibling;
    struct DOMNode *prev_sibling;

    struct ComputedStyle *style;
    struct LayoutBox *layout_box;
} DOMNode;

DOMNode *dom_node_create(NodeType type);
void dom_node_free(DOMNode *node);
void dom_node_append_child(DOMNode *parent, DOMNode *child);
void dom_node_set_text(DOMNode *node, const char *text);
void dom_node_set_attribute(DOMNode *node, const char *key, const char *value);
const char *dom_node_get_attribute(const DOMNode *node, const char *key);
const char *dom_node_get_id(const DOMNode *node);
bool dom_node_has_class(const DOMNode *node, const char *class_name);
DOMNode *dom_node_find_by_tag(const DOMNode *node, const char *tag_name);
char *dom_node_get_inner_text(const DOMNode *node);
void dom_node_print_tree(const DOMNode *node, int depth);

#endif /* DOM_H */
