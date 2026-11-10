#ifndef LAYOUT_H
#define LAYOUT_H

#include "dom.h"
#include "style.h"

typedef enum {
    BOX_BLOCK,
    BOX_INLINE,
    BOX_TEXT,
    BOX_ANONYMOUS
} BoxType;

typedef struct Rect {
    float x;
    float y;
    float w;
    float h;
} Rect;

typedef struct LayoutBox {
    BoxType box_type;
    Rect dimensions;         /* Content rect (x, y, w, h) */
    Spacing margin;
    Spacing padding;
    Spacing border;

    DOMNode *node;           /* Pointer back to DOMNode */
    char *text;              /* Text content for text boxes */

    struct LayoutBox *parent;
    struct LayoutBox *first_child;
    struct LayoutBox *last_child;
    struct LayoutBox *next_sibling;
    struct LayoutBox *prev_sibling;
} LayoutBox;

LayoutBox *layout_box_create(BoxType type, DOMNode *node);
void layout_box_free(LayoutBox *box);
void layout_box_append_child(LayoutBox *parent, LayoutBox *child);

LayoutBox *layout_build_tree(DOMNode *node);
void layout_calculate(LayoutBox *root, float viewport_w, float viewport_h);
const LayoutBox *layout_hit_test(const LayoutBox *box, float x, float y);

#endif /* LAYOUT_H */
