#include "style_resolver.h"
#include "css_cascade.h"
#include "bb_libc.h"

void style_resolver_init_default(bb_style_t *s) {
    if (!s) return;
    s->display = BB_DISPLAY_INLINE;
    s->color = BB_RGB(32, 33, 36);
    s->background_color = BB_COLOR_TRANSPARENT;
    s->font_size = 16;
    s->font_weight = 400;
    s->text_align = BB_TEXT_ALIGN_LEFT;
    s->line_height = 0; /* Normal */
    s->white_space = BB_WHITE_SPACE_NORMAL;
    s->overflow = BB_OVERFLOW_VISIBLE;
    s->visibility = BB_VISIBILITY_VISIBLE;
    s->z_index = 0;

    s->margin_top = 0;
    s->margin_right = 0;
    s->margin_bottom = 0;
    s->margin_left = 0;

    s->padding_top = 0;
    s->padding_right = 0;
    s->padding_bottom = 0;
    s->padding_left = 0;

    s->border_width = 0;
    s->border_color = BB_COLOR_BORDER_GRAY;
    s->border_radius = 0;

    s->width = -1;  /* auto */
    s->height = -1; /* auto */

    s->position = BB_POSITION_STATIC;
    s->top = s->right = s->bottom = s->left = 0;
    s->has_top = s->has_right = s->has_bottom = s->has_left = false;
}

void style_resolver_apply_ua_defaults(bb_node_t *node, bb_style_t *style) {
    if (!node || node->type != BB_NODE_ELEMENT || !node->tag_name || !style) return;
    const char *tag = node->tag_name;

    if (bb_strcasecmp(tag, "html") == 0 || bb_strcasecmp(tag, "body") == 0) {
        style->display = BB_DISPLAY_BLOCK;
        style->background_color = BB_COLOR_WHITE;
        style->color = BB_RGB(32, 33, 36);
        style->margin_top = 8;
        style->margin_right = 8;
        style->margin_bottom = 8;
        style->margin_left = 8;
    } else if (bb_strcasecmp(tag, "div") == 0 || bb_strcasecmp(tag, "section") == 0 || 
               bb_strcasecmp(tag, "article") == 0 || bb_strcasecmp(tag, "header") == 0 || 
               bb_strcasecmp(tag, "footer") == 0 || bb_strcasecmp(tag, "nav") == 0 ||
               bb_strcasecmp(tag, "main") == 0 || bb_strcasecmp(tag, "aside") == 0) {
        style->display = BB_DISPLAY_BLOCK;
    } else if (bb_strcasecmp(tag, "p") == 0) {
        style->display = BB_DISPLAY_BLOCK;
        style->margin_top = 10;
        style->margin_bottom = 10;
    } else if (bb_strcasecmp(tag, "h1") == 0) {
        style->display = BB_DISPLAY_BLOCK;
        style->font_size = 32;
        style->font_weight = 700;
        style->margin_top = 16;
        style->margin_bottom = 12;
    } else if (bb_strcasecmp(tag, "h2") == 0) {
        style->display = BB_DISPLAY_BLOCK;
        style->font_size = 24;
        style->font_weight = 700;
        style->margin_top = 14;
        style->margin_bottom = 10;
    } else if (bb_strcasecmp(tag, "h3") == 0) {
        style->display = BB_DISPLAY_BLOCK;
        style->font_size = 18;
        style->font_weight = 700;
        style->margin_top = 12;
        style->margin_bottom = 8;
    } else if (bb_strcasecmp(tag, "h4") == 0) {
        style->display = BB_DISPLAY_BLOCK;
        style->font_size = 16;
        style->font_weight = 700;
        style->margin_top = 10;
        style->margin_bottom = 8;
    } else if (bb_strcasecmp(tag, "h5") == 0) {
        style->display = BB_DISPLAY_BLOCK;
        style->font_size = 14;
        style->font_weight = 700;
        style->margin_top = 8;
        style->margin_bottom = 6;
    } else if (bb_strcasecmp(tag, "h6") == 0) {
        style->display = BB_DISPLAY_BLOCK;
        style->font_size = 12;
        style->font_weight = 700;
        style->margin_top = 6;
        style->margin_bottom = 4;
    } else if (bb_strcasecmp(tag, "ul") == 0 || bb_strcasecmp(tag, "ol") == 0) {
        style->display = BB_DISPLAY_BLOCK;
        style->margin_top = 10;
        style->margin_bottom = 10;
        style->padding_left = 20;
    } else if (bb_strcasecmp(tag, "li") == 0) {
        style->display = BB_DISPLAY_BLOCK;
    } else if (bb_strcasecmp(tag, "a") == 0) {
        style->display = BB_DISPLAY_INLINE;
        style->color = BB_COLOR_LINK;
    } else if (bb_strcasecmp(tag, "button") == 0) {
        style->display = BB_DISPLAY_INLINE_BLOCK;
        style->padding_top = 6;
        style->padding_bottom = 6;
        style->padding_left = 12;
        style->padding_right = 12;
        style->background_color = BB_COLOR_LIGHT_GRAY;
        style->border_width = 1;
        style->border_color = BB_COLOR_BORDER_GRAY;
        style->border_radius = 4;
        style->margin_right = 6;
    } else if (bb_strcasecmp(tag, "input") == 0) {
        style->display = BB_DISPLAY_INLINE_BLOCK;
        style->padding_top = 4;
        style->padding_bottom = 4;
        style->padding_left = 8;
        style->padding_right = 8;
        style->background_color = BB_COLOR_WHITE;
        style->border_width = 1;
        style->border_color = BB_COLOR_BORDER_GRAY;
        style->border_radius = 3;
    } else if (bb_strcasecmp(tag, "hr") == 0) {
        style->display = BB_DISPLAY_BLOCK;
        style->height = 1;
        style->margin_top = 8;
        style->margin_bottom = 8;
        style->background_color = BB_COLOR_BORDER_GRAY;
    } else if (bb_strcasecmp(tag, "b") == 0 || bb_strcasecmp(tag, "strong") == 0) {
        style->display = BB_DISPLAY_INLINE;
        style->font_weight = 700;
    } else if (bb_strcasecmp(tag, "code") == 0) {
        style->display = BB_DISPLAY_INLINE;
        style->background_color = BB_COLOR_LIGHT_GRAY;
        style->padding_top = 2;
        style->padding_bottom = 2;
        style->padding_left = 4;
        style->padding_right = 4;
        style->border_radius = 3;
    } else if (bb_strcasecmp(tag, "pre") == 0) {
        style->display = BB_DISPLAY_BLOCK;
        style->background_color = BB_COLOR_LIGHT_GRAY;
        style->padding_top = 8;
        style->padding_bottom = 8;
        style->padding_left = 10;
        style->padding_right = 10;
        style->margin_top = 8;
        style->margin_bottom = 8;
        style->border_radius = 4;
        style->white_space = BB_WHITE_SPACE_PRE;
    } else if (bb_strcasecmp(tag, "head") == 0 || bb_strcasecmp(tag, "style") == 0 ||
               bb_strcasecmp(tag, "script") == 0 || bb_strcasecmp(tag, "title") == 0) {
        style->display = BB_DISPLAY_NONE;
    }
}

void style_resolver_inherit(bb_style_t *child_style, const bb_style_t *parent_style) {
    if (!child_style || !parent_style) return;

    /* Text and typographical properties inherit down the tree */
    child_style->color       = parent_style->color;
    child_style->font_size   = parent_style->font_size;
    child_style->font_weight = parent_style->font_weight;
    child_style->text_align  = parent_style->text_align;
    child_style->line_height = parent_style->line_height;
    child_style->white_space = parent_style->white_space;
    child_style->visibility  = parent_style->visibility;
}

static void resolve_node_internal(bb_node_t *node, const css_parsed_sheet_t *sheet) {
    if (!node) return;

    if (!node->computed_style) {
        node->computed_style = (bb_style_t *)bb_malloc(sizeof(bb_style_t));
        style_resolver_init_default(node->computed_style);
    } else {
        style_resolver_init_default(node->computed_style);
    }

    /* 1. Inherit from parent */
    if (node->parent && node->parent->computed_style) {
        style_resolver_inherit(node->computed_style, node->parent->computed_style);
    }

    /* 2. Apply User Agent defaults */
    style_resolver_apply_ua_defaults(node, node->computed_style);

    /* 3. Apply author rules and inline style via cascade */
    if (node->type == BB_NODE_ELEMENT) {
        css_cascade_apply_to_node(node, sheet);
    }

    /* 4. Recurse down children */
    bb_node_t *child = node->first_child;
    while (child) {
        resolve_node_internal(child, sheet);
        child = child->next_sibling;
    }
}

void style_resolve_tree(bb_node_t *root, const css_parsed_sheet_t *sheet) {
    if (!root) return;
    resolve_node_internal(root, sheet);
}
