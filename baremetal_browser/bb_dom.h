#ifndef BB_DOM_H
#define BB_DOM_H

#include "bb_types.h"
#include "bb_libc.h"

/* Forward declarations */
struct bb_style;
struct bb_layout_box;
struct bb_image;

typedef enum {
    BB_NODE_DOCUMENT,
    BB_NODE_ELEMENT,
    BB_NODE_TEXT,
    BB_NODE_COMMENT
} bb_node_type_t;

typedef struct bb_attr {
    char           *key;
    char           *val;
    struct bb_attr *next;
} bb_attr_t;

typedef struct bb_node {
    bb_node_type_t         type;
    char                  *tag_name;
    char                  *text_content;
    bb_attr_t             *attributes;

    struct bb_node        *parent;
    struct bb_node        *first_child;
    struct bb_node        *last_child;
    struct bb_node        *next_sibling;
    struct bb_node        *prev_sibling;

    struct bb_style       *computed_style;
    struct bb_layout_box  *layout_box;
    struct bb_image       *image;
} bb_node_t;

/* Node Construction & Tree Manipulation */
bb_node_t   *bb_node_create(bb_node_type_t type);
bb_node_t   *bb_node_create_element(const char *tag_name);
bb_node_t   *bb_node_create_text(const char *text);
void         bb_node_append_child(bb_node_t *parent, bb_node_t *child);
void         bb_node_set_attribute(bb_node_t *node, const char *key, const char *val);
const char  *bb_node_get_attribute(const bb_node_t *node, const char *key);
bool         bb_node_has_class(const bb_node_t *node, const char *class_name);
void         bb_node_free_tree(bb_node_t *root);

#endif /* BB_DOM_H */
