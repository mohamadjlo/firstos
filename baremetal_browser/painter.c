#include "painter.h"
#include "bb_font.h"
#include "bb_libc.h"

#define MAX_STACK_ITEMS 64

static bool is_hidden(const bb_layout_box_t *box) {
    if (!box || !box->node || !box->node->computed_style) return false;
    return (box->node->computed_style->visibility == BB_VISIBILITY_HIDDEN);
}

void painter_paint_background(const bb_layout_box_t *box, bb_raster_t *r, int32_t offset_x, int32_t offset_y) {
    if (!box || !r || is_hidden(box)) return;

    if (BB_GET_A(box->background_color) == 0) return;

    int32_t px = box->dimensions.x - box->padding_left - box->border_width + offset_x;
    int32_t py = box->dimensions.y - box->padding_top  - box->border_width + offset_y;
    int32_t pw = box->dimensions.w + box->padding_left + box->padding_right + box->border_width * 2;
    int32_t ph = box->dimensions.h + box->padding_top  + box->padding_bottom+ box->border_width * 2;

    bb_rect_t border_rect = {px, py, pw > 0 ? pw : 0, ph > 0 ? ph : 0};

    if (box->border_radius > 0) {
        bb_raster_fill_round_rect(r, border_rect, box->border_radius, box->background_color);
    } else {
        bb_raster_fill_rect(r, border_rect, box->background_color);
    }
}

void painter_paint_border(const bb_layout_box_t *box, bb_raster_t *r, int32_t offset_x, int32_t offset_y) {
    if (!box || !r || is_hidden(box)) return;

    if (box->border_width <= 0 || BB_GET_A(box->border_color) == 0) return;

    int32_t px = box->dimensions.x - box->padding_left - box->border_width + offset_x;
    int32_t py = box->dimensions.y - box->padding_top  - box->border_width + offset_y;
    int32_t pw = box->dimensions.w + box->padding_left + box->padding_right + box->border_width * 2;
    int32_t ph = box->dimensions.h + box->padding_top  + box->padding_bottom+ box->border_width * 2;

    bb_rect_t border_rect = {px, py, pw > 0 ? pw : 0, ph > 0 ? ph : 0};

    if (box->border_radius > 0) {
        bb_raster_draw_round_rect_outline(r, border_rect, box->border_radius, box->border_width, box->border_color);
    } else {
        bb_raster_draw_rect_outline(r, border_rect, box->border_width, box->border_color);
    }
}

void painter_paint_text(const bb_layout_box_t *box, bb_raster_t *r, int32_t offset_x, int32_t offset_y) {
    if (!box || !r || is_hidden(box)) return;

    if (box->type != BB_BOX_ANONYMOUS_TEXT || !box->text) return;

    int scale  = box->font_style.scale > 0 ? box->font_style.scale : 1;
    int char_w = 8 * scale;
    int l_h    = 16 * scale + 2;

    int32_t cur_x     = box->dimensions.x + offset_x;
    int32_t cur_y     = box->dimensions.y + offset_y;
    int32_t left_edge = (box->parent) ? (box->parent->dimensions.x + offset_x) : cur_x;
    int32_t max_w     = (box->parent) ? box->parent->dimensions.w : box->dimensions.w;

    const char *p = box->text;

    while (*p) {
        while (*p == ' ') {
            cur_x += char_w;
            p++;
        }
        if (!*p) break;

        const char *word_start = p;
        const char *scan       = p;
        int32_t     codepoints = 0;
        while (*scan && *scan != ' ' && *scan != '\n') {
            bb_utf8_decode_next(&scan, NULL);
            codepoints++;
        }

        int32_t word_w = codepoints * char_w;

        if (cur_x + word_w > left_edge + max_w && cur_x > left_edge) {
            cur_x  = left_edge;
            cur_y += l_h;
        }

        char word_buf[512];
        int  wlen = 0;
        const char *rdp = word_start;
        while (rdp < scan && wlen < (int)(sizeof(word_buf) - 1)) {
            uint32_t cp = bb_utf8_decode_next(&rdp, scan);
            if (cp == 0) break;
            if (cp == BB_UNICODE_NBSP) {
                cur_x += char_w;
            } else {
                word_buf[wlen++] = (char)bb_unicode_to_glyph(cp);
            }
        }
        word_buf[wlen] = '\0';

        if (wlen > 0) {
            bb_font_draw_string(r, cur_x, cur_y, word_buf,
                                box->text_color, &box->font_style);
            cur_x += (int32_t)wlen * char_w;
        }

        p = scan;
        if (*p == '\n') {
            cur_x  = left_edge;
            cur_y += l_h;
            p++;
        }
    }
}

void painter_paint_replaced(const bb_layout_box_t *box, bb_raster_t *r, int32_t offset_x, int32_t offset_y) {
    if (!box || !r) return;
    bb_style_t *s = box->node ? box->node->computed_style : NULL;
    if (s && s->visibility == BB_VISIBILITY_HIDDEN) return;

    bb_image_t *img = box->image;
    if (!img && box->node) img = box->node->image;

    bb_rect_t content_rect = {
        box->dimensions.x + offset_x,
        box->dimensions.y + offset_y,
        box->dimensions.w,
        box->dimensions.h
    };
    if (content_rect.w <= 0 || content_rect.h <= 0) return;

    bb_raster_push_clip(r, content_rect);

    if (img && img->state == BB_IMAGE_STATE_LOADED && img->pixels &&
        img->intrinsic_width > 0 && img->intrinsic_height > 0) {
        bb_raster_t src_raster;
        bb_raster_init(&src_raster, img->pixels, (uint32_t)img->intrinsic_width, (uint32_t)img->intrinsic_height);

        bb_rect_t src_rect = { 0, 0, img->intrinsic_width, img->intrinsic_height };
        if (content_rect.w == img->intrinsic_width && content_rect.h == img->intrinsic_height) {
            bb_raster_blit(r, content_rect.x, content_rect.y, &src_raster, src_rect);
        } else {
            bb_raster_blit_scaled(r, content_rect, &src_raster, src_rect);
        }
    } else {
        /* Render placeholder / fallback */
        bb_color_t bg_col = (img && img->state == BB_IMAGE_STATE_FAILED) ? BB_RGB(248, 242, 242) : BB_RGB(242, 243, 245);
        bb_color_t border_col = (img && img->state == BB_IMAGE_STATE_FAILED) ? BB_RGB(218, 180, 180) : BB_RGB(200, 204, 210);

        bb_raster_fill_rect(r, content_rect, bg_col);
        bb_raster_draw_rect_outline(r, content_rect, 1, border_col);

        /* Icon / text display */
        const char *alt_text = (img && img->alt) ? img->alt : (box->node ? bb_node_get_attribute(box->node, "alt") : NULL);
        if (!alt_text && img && img->src) alt_text = img->src;

        if (alt_text && *alt_text) {
            bb_font_draw_string(r, content_rect.x + 6, content_rect.y + 6, alt_text, BB_RGB(100, 102, 108), &BB_FONT_STYLE_REGULAR);
        } else {
            const char *label = (img && img->state == BB_IMAGE_STATE_FAILED) ? "[Broken Image]" : "[Image]";
            bb_font_draw_string(r, content_rect.x + 6, content_rect.y + 6, label, BB_RGB(120, 122, 128), &BB_FONT_STYLE_REGULAR);
        }
    }

    bb_raster_pop_clip(r);
}

bool painter_push_overflow_clip(const bb_layout_box_t *box, bb_raster_t *r, int32_t offset_x, int32_t offset_y) {
    if (!box || !r || !box->node || !box->node->computed_style) return false;

    bb_overflow_t ov = box->node->computed_style->overflow;
    if (ov == BB_OVERFLOW_HIDDEN || ov == BB_OVERFLOW_SCROLL || ov == BB_OVERFLOW_AUTO) {
        int32_t cx = box->dimensions.x - box->padding_left + offset_x;
        int32_t cy = box->dimensions.y - box->padding_top  + offset_y;
        int32_t cw = box->dimensions.w + box->padding_left + box->padding_right;
        int32_t ch = box->dimensions.h + box->padding_top  + box->padding_bottom;

        bb_rect_t clip_box = {cx, cy, cw > 0 ? cw : 0, ch > 0 ? ch : 0};
        bb_raster_push_clip(r, clip_box);
        return true;
    }
    return false;
}

void painter_pop_overflow_clip(bb_raster_t *r) {
    if (r) {
        bb_raster_pop_clip(r);
    }
}

/* Stacking context detection */
static bool is_stacking_context(const bb_layout_box_t *box, bool is_root) {
    if (is_root) return true;
    if (!box || !box->node || !box->node->computed_style) return false;
    const bb_style_t *s = box->node->computed_style;
    return (s->position != BB_POSITION_STATIC && s->z_index != 0);
}

static int get_z_index(const bb_layout_box_t *box) {
    if (box && box->node && box->node->computed_style) {
        return box->node->computed_style->z_index;
    }
    return 0;
}

/* Forward declaration for recursive stacking context rendering */
static void paint_stacking_context(bb_layout_box_t *root, bb_raster_t *r, int32_t offset_x, int32_t offset_y, bool is_root);

/* Paint in-flow elements (document order traversal with overflow clipping) */
static void paint_inflow_subtree(bb_layout_box_t *box, bb_raster_t *r, int32_t offset_x, int32_t offset_y) {
    if (!box) return;

    /* If this box is a descendant stacking context, it is painted during its z-index layer instead */
    if (is_stacking_context(box, false)) {
        return;
    }

    bool pushed_clip = painter_push_overflow_clip(box, r, offset_x, offset_y);

    /* 1. Background */
    painter_paint_background(box, r, offset_x, offset_y);

    /* 2. Border */
    painter_paint_border(box, r, offset_x, offset_y);

    /* 3. Text or Replaced Content */
    if (box->type == BB_BOX_REPLACED || box->image != NULL) {
        painter_paint_replaced(box, r, offset_x, offset_y);
    } else {
        painter_paint_text(box, r, offset_x, offset_y);
    }

    /* 4. Children */
    bb_layout_box_t *child = box->first_child;
    while (child) {
        paint_inflow_subtree(child, r, offset_x, offset_y);
        child = child->next_sibling;
    }

    if (pushed_clip) {
        painter_pop_overflow_clip(r);
    }
}

/* Collect descendant stacking contexts belonging to this stacking context */
static void collect_child_stacking_contexts(bb_layout_box_t *box,
                                           bb_layout_box_t **neg_contexts, int *neg_count,
                                           bb_layout_box_t **zero_contexts, int *zero_count,
                                           bb_layout_box_t **pos_contexts, int *pos_count) {
    if (!box) return;

    if (is_stacking_context(box, false)) {
        int z = get_z_index(box);
        if (z < 0) {
            if (*neg_count < MAX_STACK_ITEMS) neg_contexts[(*neg_count)++] = box;
        } else if (z > 0) {
            if (*pos_count < MAX_STACK_ITEMS) pos_contexts[(*pos_count)++] = box;
        } else {
            if (*zero_count < MAX_STACK_ITEMS) zero_contexts[(*zero_count)++] = box;
        }
        return; /* Do not search descendants of child stacking context */
    }

    bb_layout_box_t *child = box->first_child;
    while (child) {
        collect_child_stacking_contexts(child, neg_contexts, neg_count,
                                        zero_contexts, zero_count,
                                        pos_contexts, pos_count);
        child = child->next_sibling;
    }
}

/* Simple insertion sort for z-index ascending */
static void sort_by_z_index(bb_layout_box_t **items, int count) {
    for (int i = 1; i < count; i++) {
        bb_layout_box_t *key = items[i];
        int key_z = get_z_index(key);
        int j = i - 1;
        while (j >= 0 && get_z_index(items[j]) > key_z) {
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = key;
    }
}

static void paint_stacking_context(bb_layout_box_t *root, bb_raster_t *r, int32_t offset_x, int32_t offset_y, bool is_root) {
    (void)is_root;
    if (!root) return;

    /* 1. Paint root's own background & border */
    painter_paint_background(root, r, offset_x, offset_y);
    painter_paint_border(root, r, offset_x, offset_y);

    /* Collect child stacking contexts */
    bb_layout_box_t *neg_contexts[MAX_STACK_ITEMS];
    int neg_count = 0;
    bb_layout_box_t *zero_contexts[MAX_STACK_ITEMS];
    int zero_count = 0;
    bb_layout_box_t *pos_contexts[MAX_STACK_ITEMS];
    int pos_count = 0;

    bb_layout_box_t *c = root->first_child;
    while (c) {
        collect_child_stacking_contexts(c, neg_contexts, &neg_count,
                                        zero_contexts, &zero_count,
                                        pos_contexts, &pos_count);
        c = c->next_sibling;
    }

    sort_by_z_index(neg_contexts, neg_count);
    sort_by_z_index(pos_contexts, pos_count);

    /* 2. Paint negative z-index child stacking contexts */
    for (int i = 0; i < neg_count; i++) {
        paint_stacking_context(neg_contexts[i], r, offset_x, offset_y, false);
    }

    /* 3. Paint in-flow document subtree (backgrounds, borders, text, overflow clips) */
    bool pushed_clip = painter_push_overflow_clip(root, r, offset_x, offset_y);
    if (root->type == BB_BOX_REPLACED || root->image != NULL) {
        painter_paint_replaced(root, r, offset_x, offset_y);
    } else {
        painter_paint_text(root, r, offset_x, offset_y);
    }

    bb_layout_box_t *child = root->first_child;
    while (child) {
        paint_inflow_subtree(child, r, offset_x, offset_y);
        child = child->next_sibling;
    }

    if (pushed_clip) {
        painter_pop_overflow_clip(r);
    }

    /* 4. Paint positioned zero/auto contexts */
    for (int i = 0; i < zero_count; i++) {
        paint_stacking_context(zero_contexts[i], r, offset_x, offset_y, false);
    }

    /* 5. Paint positive z-index child stacking contexts (ascending order) */
    for (int i = 0; i < pos_count; i++) {
        paint_stacking_context(pos_contexts[i], r, offset_x, offset_y, false);
    }
}

void painter_paint_tree(bb_layout_box_t *root, bb_raster_t *r, int32_t offset_x, int32_t offset_y) {
    if (!root || !r) return;
    paint_stacking_context(root, r, offset_x, offset_y, true);
}
