#define _POSIX_C_SOURCE 200809L
#include "layout.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LayoutBox *layout_box_create(BoxType type, DOMNode *node) {
    LayoutBox *box = (LayoutBox *)calloc(1, sizeof(LayoutBox));
    if (!box) return NULL;
    box->box_type = type;
    box->node = node;
    if (node) {
        node->layout_box = box;
        if (node->style) {
            box->margin = node->style->margin;
            box->padding = node->style->padding;
            box->border.top = box->border.right = box->border.bottom = box->border.left = node->style->border_width;
        }
    }
    return box;
}

void layout_box_free(LayoutBox *box) {
    if (!box) return;
    LayoutBox *child = box->first_child;
    while (child) {
        LayoutBox *next = child->next_sibling;
        layout_box_free(child);
        child = next;
    }
    free(box->text);
    if (box->node && box->node->layout_box == box) {
        box->node->layout_box = NULL;
    }
    free(box);
}

void layout_box_append_child(LayoutBox *parent, LayoutBox *child) {
    if (!parent || !child) return;
    child->parent = parent;
    child->next_sibling = NULL;
    child->prev_sibling = parent->last_child;
    if (parent->last_child) {
        parent->last_child->next_sibling = child;
        parent->last_child = child;
    } else {
        parent->first_child = child;
        parent->last_child = child;
    }
}

static bool is_all_whitespace(const char *str) {
    if (!str) return true;
    while (*str) {
        if (!isspace((unsigned char)*str)) return false;
        str++;
    }
    return true;
}

LayoutBox *layout_build_tree(DOMNode *node) {
    if (!node) return NULL;

    /* Hidden elements */
    if (node->style && node->style->display == DISPLAY_NONE) {
        return NULL;
    }

    LayoutBox *box = NULL;
    if (node->type == NODE_DOCUMENT) {
        box = layout_box_create(BOX_BLOCK, node);
    } else if (node->type == NODE_ELEMENT) {
        BoxType btype = (node->style && node->style->display == DISPLAY_BLOCK) ? BOX_BLOCK : BOX_INLINE;
        box = layout_box_create(btype, node);
    } else if (node->type == NODE_TEXT) {
        if (is_all_whitespace(node->text_content)) {
            return NULL;
        }
        box = layout_box_create(BOX_TEXT, node);
        box->text = strdup(node->text_content);
    }

    if (!box) return NULL;

    for (DOMNode *child = node->first_child; child; child = child->next_sibling) {
        LayoutBox *child_box = layout_build_tree(child);
        if (child_box) {
            layout_box_append_child(box, child_box);
        }
    }

    return box;
}

static float estimate_text_width(const char *text, float font_size) {
    if (!text || !text[0]) return 0.0f;
    size_t len = strlen(text);
    return (float)len * (font_size * 0.55f);
}

static void layout_box_tree(LayoutBox *box, float containing_x, float containing_y, float containing_w) {
    if (!box) return;

    /* 1. Width Calculation (Top-down) */
    if (box->box_type == BOX_BLOCK) {
        float horizontal_spacing = box->margin.left + box->margin.right +
                                   box->padding.left + box->padding.right +
                                   box->border.left + box->border.right;

        if (box->node && box->node->style && !box->node->style->width_auto && box->node->style->width > 0) {
            box->dimensions.w = box->node->style->width;
        } else {
            box->dimensions.w = containing_w - horizontal_spacing;
            if (box->dimensions.w < 0) box->dimensions.w = 0;
        }

        box->dimensions.x = containing_x + box->margin.left + box->border.left + box->padding.left;
        box->dimensions.y = containing_y + box->margin.top + box->border.top + box->padding.top;
    } else if (box->box_type == BOX_TEXT) {
        float fs = (box->node && box->node->style) ? box->node->style->font_size : 16.0f;
        box->dimensions.w = estimate_text_width(box->text, fs);
        box->dimensions.h = fs * 1.25f;
        box->dimensions.x = containing_x;
        box->dimensions.y = containing_y;
        return;
    } else {
        /* Inline box */
        box->dimensions.x = containing_x;
        box->dimensions.y = containing_y;
        box->dimensions.w = 0;
        box->dimensions.h = 0;
    }

    /* 2. Children Layout (Vertical Stacking for Blocks, Horizontal Flow for Inlines/Text) */
    float cursor_x = box->dimensions.x;
    float cursor_y = box->dimensions.y;
    float line_height = 20.0f;
    float max_child_bottom = cursor_y;

    for (LayoutBox *child = box->first_child; child; child = child->next_sibling) {
        if (child->box_type == BOX_BLOCK) {
            /* If we had inline content on the line, flush down */
            if (cursor_x > box->dimensions.x) {
                cursor_y += line_height;
                cursor_x = box->dimensions.x;
            }

            layout_box_tree(child, box->dimensions.x, cursor_y, box->dimensions.w);

            float child_total_h = child->dimensions.h + child->margin.top + child->margin.bottom +
                                  child->padding.top + child->padding.bottom +
                                  child->border.top + child->border.bottom;

            cursor_y += child_total_h;
            if (cursor_y > max_child_bottom) max_child_bottom = cursor_y;
        } else if (child->box_type == BOX_TEXT) {
            float fs = (child->node && child->node->style) ? child->node->style->font_size : 16.0f;
            float text_w = estimate_text_width(child->text, fs);
            float text_h = fs * 1.25f;
            if (text_h > line_height) line_height = text_h;

            /* Line wrap check */
            if (cursor_x + text_w > box->dimensions.x + box->dimensions.w && cursor_x > box->dimensions.x) {
                cursor_x = box->dimensions.x;
                cursor_y += line_height;
            }

            child->dimensions.x = cursor_x;
            child->dimensions.y = cursor_y;
            child->dimensions.w = text_w;
            child->dimensions.h = text_h;

            cursor_x += text_w;
            float bottom = cursor_y + text_h;
            if (bottom > max_child_bottom) max_child_bottom = bottom;
        } else {
            /* Inline element box (e.g. <a>, <span>, <button>) */
            float child_left_space = child->margin.left + child->border.left + child->padding.left;
            child->dimensions.x = cursor_x + child_left_space;
            child->dimensions.y = cursor_y + child->margin.top + child->border.top + child->padding.top;

            /* Recursively layout children of inline box */
            layout_box_tree(child, child->dimensions.x, child->dimensions.y, box->dimensions.w);

            float inline_w = child->dimensions.w + child->padding.left + child->padding.right +
                             child->border.left + child->border.right;
            float inline_h = child->dimensions.h + child->padding.top + child->padding.bottom +
                             child->border.top + child->border.bottom;

            /* Line wrap check for inline elements if content exceeds container */
            if (cursor_x + inline_w > box->dimensions.x + box->dimensions.w && cursor_x > box->dimensions.x) {
                cursor_x = box->dimensions.x;
                cursor_y += line_height;
                child->dimensions.x = cursor_x + child_left_space;
                child->dimensions.y = cursor_y + child->margin.top + child->border.top + child->padding.top;
                layout_box_tree(child, child->dimensions.x, child->dimensions.y, box->dimensions.w);
            }

            if (inline_h > line_height) line_height = inline_h;

            cursor_x += inline_w + child->margin.left + child->margin.right;
            float bottom = cursor_y + inline_h;
            if (bottom > max_child_bottom) max_child_bottom = bottom;
        }
    }

    /* 3. Width & Height for Inline Container Boxes */
    if (box->box_type == BOX_INLINE) {
        box->dimensions.w = cursor_x - box->dimensions.x;
        box->dimensions.h = (max_child_bottom > box->dimensions.y) ? (max_child_bottom - box->dimensions.y) : line_height;
        return;
    }

    if (cursor_x > box->dimensions.x) {
        cursor_y += line_height;
        if (cursor_y > max_child_bottom) max_child_bottom = cursor_y;
    }

    /* 4. Height Calculation (Bottom-up for Block boxes) */
    if (box->node && box->node->style && !box->node->style->height_auto && box->node->style->height > 0) {
        box->dimensions.h = box->node->style->height;
    } else {
        box->dimensions.h = max_child_bottom - box->dimensions.y;
        if (box->dimensions.h < 0) box->dimensions.h = 0;
    }
}

void layout_calculate(LayoutBox *root, float viewport_w, float viewport_h) {
    if (!root) return;
    (void)viewport_h;
    layout_box_tree(root, 0, 0, viewport_w);
}

const LayoutBox *layout_hit_test(const LayoutBox *box, float x, float y) {
    if (!box) return NULL;

    /* Check children in reverse order (topmost first) */
    for (const LayoutBox *child = box->last_child; child; child = child->prev_sibling) {
        const LayoutBox *hit = layout_hit_test(child, x, y);
        if (hit) return hit;
    }

    /* Check this box's outer bounds */
    float left = box->dimensions.x - box->padding.left - box->border.left;
    float top = box->dimensions.y - box->padding.top - box->border.top;
    float right = box->dimensions.x + box->dimensions.w + box->padding.right + box->border.right;
    float bottom = box->dimensions.y + box->dimensions.h + box->padding.bottom + box->border.bottom;

    if (x >= left && x <= right && y >= top && y <= bottom) {
        return box;
    }

    return NULL;
}
