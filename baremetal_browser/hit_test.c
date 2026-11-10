#include "hit_test.h"
#include "bb_libc.h"

#define MAX_STACK_ITEMS 64

/* =========================================================================
 * Internal Geometry & Helper Functions
 * ========================================================================= */

static inline bool point_in_rect(int32_t x, int32_t y, bb_rect_t r) {
    return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
}

static inline bb_rect_t rect_intersect(bb_rect_t a, bb_rect_t b) {
    int32_t x1 = BB_MAX(a.x, b.x);
    int32_t y1 = BB_MAX(a.y, b.y);
    int32_t x2 = BB_MIN(a.x + a.w, b.x + b.w);
    int32_t y2 = BB_MIN(a.y + a.h, b.y + b.h);

    if (x2 <= x1 || y2 <= y1) {
        bb_rect_t empty = { 0, 0, 0, 0 };
        return empty;
    }
    bb_rect_t res = { x1, y1, x2 - x1, y2 - y1 };
    return res;
}

static inline bb_rect_t get_border_box(bb_layout_box_t *box, int32_t offset_x, int32_t offset_y) {
    bb_rect_t r;
    r.x = box->dimensions.x - box->padding_left - box->border_width + offset_x;
    r.y = box->dimensions.y - box->padding_top  - box->border_width + offset_y;
    r.w = box->dimensions.w + box->padding_left + box->padding_right + box->border_width * 2;
    r.h = box->dimensions.h + box->padding_top  + box->padding_bottom+ box->border_width * 2;
    return r;
}

static inline bb_rect_t get_padding_box(bb_layout_box_t *box, int32_t offset_x, int32_t offset_y) {
    bb_rect_t r;
    r.x = box->dimensions.x - box->padding_left + offset_x;
    r.y = box->dimensions.y - box->padding_top  + offset_y;
    r.w = box->dimensions.w + box->padding_left + box->padding_right;
    r.h = box->dimensions.h + box->padding_top  + box->padding_bottom;
    return r;
}

static inline int32_t get_z_index(bb_layout_box_t *box) {
    if (box && box->node && box->node->computed_style) {
        return box->node->computed_style->z_index;
    }
    return 0;
}

static inline bool is_stacking_context(bb_layout_box_t *box, bool is_root) {
    if (is_root) return true;
    if (!box || !box->node) return false;
    bb_style_t *s = box->node->computed_style;
    if (!s) return false;
    if (s->position != BB_POSITION_STATIC) return true;
    return false;
}

/* =========================================================================
 * Metadata Extraction
 * ========================================================================= */

static void populate_hit_metadata(bb_layout_box_t *box, int32_t x, int32_t y,
                                  int32_t offset_x, int32_t offset_y,
                                  bb_hit_test_result_t *out) {
    if (!out) return;
    bb_memset(out, 0, sizeof(*out));

    out->box = box;
    out->node = box ? box->node : NULL;
    out->point_x = x;
    out->point_y = y;

    if (!box) {
        out->target_type = BB_HIT_TARGET_NONE;
        out->cursor = BB_CURSOR_DEFAULT;
        return;
    }

    /* Local relative coordinates */
    out->local_x = x - (box->dimensions.x + offset_x);
    out->local_y = y - (box->dimensions.y + offset_y);

    /* Default classifications based on box type */
    if (box->type == BB_BOX_ANONYMOUS_TEXT) {
        out->target_type = BB_HIT_TARGET_TEXT;
        out->is_text = true;
        out->cursor = BB_CURSOR_TEXT;

        /* Calculate character / glyph offset */
        int32_t char_w = 8;
        if (box->font_style.scale > 1) {
            char_w *= box->font_style.scale;
        }
        if (box->text && char_w > 0) {
            int32_t text_len = (int32_t)bb_strlen(box->text);
            int32_t offset = out->local_x / char_w;
            if (offset < 0) offset = 0;
            if (offset > text_len) offset = text_len;
            out->text_offset = offset;
        }
    } else if (box->type == BB_BOX_INLINE) {
        out->target_type = BB_HIT_TARGET_INLINE;
        out->cursor = BB_CURSOR_DEFAULT;
    } else {
        out->target_type = BB_HIT_TARGET_BLOCK;
        out->cursor = BB_CURSOR_DEFAULT;
    }

    out->is_hoverable = true;

    /* Walk up DOM tree to discover interactive tags (<a>, <button>, <input>, <img>) */
    bb_node_t *curr = out->node;
    while (curr) {
        if (curr->tag_name) {
            /* Link: <a> */
            if (bb_strcasecmp(curr->tag_name, "a") == 0) {
                out->is_link = true;
                out->href = bb_node_get_attribute(curr, "href");
                out->target = bb_node_get_attribute(curr, "target");
                out->is_interactive = true;
                out->is_activatable = true;
                out->cursor = BB_CURSOR_POINTER;
                out->target_type = BB_HIT_TARGET_LINK;
                if (!out->interactive_node) {
                    out->interactive_node = curr;
                }
            }
            /* Button: <button> */
            else if (bb_strcasecmp(curr->tag_name, "button") == 0) {
                out->is_interactive = true;
                out->is_activatable = true;
                out->cursor = BB_CURSOR_POINTER;
                out->target_type = BB_HIT_TARGET_INPUT;
                if (!out->interactive_node) {
                    out->interactive_node = curr;
                }
            }
            /* Input: <input> */
            else if (bb_strcasecmp(curr->tag_name, "input") == 0) {
                out->is_interactive = true;
                out->is_activatable = true;
                out->target_type = BB_HIT_TARGET_INPUT;
                const char *itype = bb_node_get_attribute(curr, "type");
                if (itype && (bb_strcasecmp(itype, "button") == 0 ||
                              bb_strcasecmp(itype, "submit") == 0 ||
                              bb_strcasecmp(itype, "reset") == 0)) {
                    out->cursor = BB_CURSOR_POINTER;
                } else {
                    out->cursor = BB_CURSOR_TEXT;
                }
                if (!out->interactive_node) {
                    out->interactive_node = curr;
                }
            }
            /* Replaced element: <img> */
            else if (bb_strcasecmp(curr->tag_name, "img") == 0) {
                if (out->target_type != BB_HIT_TARGET_LINK) {
                    out->target_type = BB_HIT_TARGET_REPLACED;
                }
            }
        }

        /* Tooltip discovery from title or alt attributes */
        if (!out->title) {
            const char *title_attr = bb_node_get_attribute(curr, "title");
            if (title_attr && title_attr[0] != '\0') {
                out->title = title_attr;
            } else if (curr->tag_name && bb_strcasecmp(curr->tag_name, "img") == 0) {
                const char *alt_attr = bb_node_get_attribute(curr, "alt");
                if (alt_attr && alt_attr[0] != '\0') {
                    out->title = alt_attr;
                }
            }
        }

        curr = curr->parent;
    }

    if (!out->interactive_node) {
        out->interactive_node = out->node;
    }
}

/* =========================================================================
 * Descending Stacking Context Collectors & Sorters
 * ========================================================================= */

static void collect_child_stacking_contexts(bb_layout_box_t *box,
                                           bb_layout_box_t **neg_contexts, int *neg_count,
                                           bb_layout_box_t **zero_contexts, int *zero_count,
                                           bb_layout_box_t **pos_contexts, int *pos_count) {
    if (!box) return;

    if (is_stacking_context(box, false)) {
        int32_t z = get_z_index(box);
        if (z < 0) {
            if (*neg_count < MAX_STACK_ITEMS) neg_contexts[(*neg_count)++] = box;
        } else if (z > 0) {
            if (*pos_count < MAX_STACK_ITEMS) pos_contexts[(*pos_count)++] = box;
        } else {
            if (*zero_count < MAX_STACK_ITEMS) zero_contexts[(*zero_count)++] = box;
        }
        return; /* Descendants belong to child's own stacking context */
    }

    bb_layout_box_t *child = box->first_child;
    while (child) {
        collect_child_stacking_contexts(child, neg_contexts, neg_count,
                                        zero_contexts, zero_count,
                                        pos_contexts, pos_count);
        child = child->next_sibling;
    }
}

/* Insertion sort descending (highest z-index first) */
static void sort_by_z_index_descending(bb_layout_box_t **items, int count) {
    for (int i = 1; i < count; i++) {
        bb_layout_box_t *key = items[i];
        int32_t key_z = get_z_index(key);
        int j = i - 1;
        while (j >= 0 && get_z_index(items[j]) < key_z) {
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = key;
    }
}

/* Forward declaration for recursive stacking context hit testing */
static bool hit_test_stacking_context(bb_layout_box_t *root,
                                      int32_t x, int32_t y,
                                      int32_t offset_x, int32_t offset_y,
                                      bb_rect_t clip_rect,
                                      bool is_root,
                                      bb_hit_test_result_t *out_result);

/* =========================================================================
 * In-Flow Subtree Hit Testing (Reverse Document Order)
 * ========================================================================= */

static bool hit_test_inflow_subtree(bb_layout_box_t *box,
                                    int32_t x, int32_t y,
                                    int32_t offset_x, int32_t offset_y,
                                    bb_rect_t clip_rect,
                                    bb_hit_test_result_t *out_result) {
    if (!box) return false;

    /* Skip if descendant stacking context (handled in z-index layers) */
    if (is_stacking_context(box, false)) {
        return false;
    }

    /* Check visibility & display */
    bb_style_t *s = box->node ? box->node->computed_style : NULL;
    if (s) {
        if (s->visibility == BB_VISIBILITY_HIDDEN) return false;
        if (s->display == BB_DISPLAY_NONE) return false;
    }

    /* Compute effective clipping rectangle if box has overflow clipping */
    bb_rect_t effective_clip = clip_rect;
    if (s && (s->overflow == BB_OVERFLOW_HIDDEN ||
              s->overflow == BB_OVERFLOW_SCROLL ||
              s->overflow == BB_OVERFLOW_AUTO)) {
        bb_rect_t pbox = get_padding_box(box, offset_x, offset_y);
        effective_clip = rect_intersect(clip_rect, pbox);
    }

    /* 1. First, search children in reverse document order (last child to first) */
    bb_layout_box_t *child = box->last_child;
    while (child) {
        if (hit_test_inflow_subtree(child, x, y, offset_x, offset_y, effective_clip, out_result)) {
            return true;
        }
        child = child->prev_sibling;
    }

    /* 2. Check if point hits this box itself within the clipping region */
    if (point_in_rect(x, y, effective_clip)) {
        bb_rect_t bbox = get_border_box(box, offset_x, offset_y);
        if (point_in_rect(x, y, bbox)) {
            populate_hit_metadata(box, x, y, offset_x, offset_y, out_result);
            return true;
        }
    }

    return false;
}

/* =========================================================================
 * Stacking Context Hit Testing (Exact Reverse of Painter Order)
 * ========================================================================= */

static bool hit_test_stacking_context(bb_layout_box_t *root,
                                      int32_t x, int32_t y,
                                      int32_t offset_x, int32_t offset_y,
                                      bb_rect_t clip_rect,
                                      bool is_root,
                                      bb_hit_test_result_t *out_result) {
    if (!root) return false;

    bb_style_t *s = root->node ? root->node->computed_style : NULL;
    if (s) {
        if (s->visibility == BB_VISIBILITY_HIDDEN) return false;
        if (s->display == BB_DISPLAY_NONE) return false;
    }

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

    /* Sort positive and negative contexts descending (highest z-index tested first) */
    sort_by_z_index_descending(pos_contexts, pos_count);
    sort_by_z_index_descending(neg_contexts, neg_count);

    /* 1. Positive z-index child stacking contexts (descending order) */
    for (int i = 0; i < pos_count; i++) {
        if (hit_test_stacking_context(pos_contexts[i], x, y, offset_x, offset_y, clip_rect, false, out_result)) {
            return true;
        }
    }

    /* 2. Positioned zero/auto contexts (reverse document order) */
    for (int i = zero_count - 1; i >= 0; i--) {
        if (hit_test_stacking_context(zero_contexts[i], x, y, offset_x, offset_y, clip_rect, false, out_result)) {
            return true;
        }
    }

    /* 3. In-flow document subtree */
    bb_rect_t effective_clip = clip_rect;
    if (s && (s->overflow == BB_OVERFLOW_HIDDEN ||
              s->overflow == BB_OVERFLOW_SCROLL ||
              s->overflow == BB_OVERFLOW_AUTO)) {
        bb_rect_t pbox = get_padding_box(root, offset_x, offset_y);
        effective_clip = rect_intersect(clip_rect, pbox);
    }

    bb_layout_box_t *child = root->last_child;
    while (child) {
        if (hit_test_inflow_subtree(child, x, y, offset_x, offset_y, effective_clip, out_result)) {
            return true;
        }
        child = child->prev_sibling;
    }

    /* 4. Negative z-index child stacking contexts (descending: -1 down to -N) */
    for (int i = 0; i < neg_count; i++) {
        if (hit_test_stacking_context(neg_contexts[i], x, y, offset_x, offset_y, clip_rect, false, out_result)) {
            return true;
        }
    }

    /* 5. Root box's own border box */
    if (point_in_rect(x, y, clip_rect)) {
        bb_rect_t root_bbox = get_border_box(root, offset_x, offset_y);
        if (point_in_rect(x, y, root_bbox)) {
            populate_hit_metadata(root, x, y, offset_x, offset_y, out_result);
            return true;
        }
    }

    (void)is_root;
    return false;
}

/* =========================================================================
 * Public API Implementations
 * ========================================================================= */

bool hit_test_at_point(bb_layout_box_t *root,
                       int32_t x, int32_t y,
                       int32_t offset_x, int32_t offset_y,
                       bb_rect_t clip_rect,
                       bb_hit_test_result_t *out_result) {
    if (!root) {
        if (out_result) bb_memset(out_result, 0, sizeof(*out_result));
        return false;
    }

    bb_hit_test_result_t temp_res;
    bb_memset(&temp_res, 0, sizeof(temp_res));

    bool hit = hit_test_stacking_context(root, x, y, offset_x, offset_y, clip_rect, true, &temp_res);
    if (hit && out_result) {
        *out_result = temp_res;
    } else if (!hit && out_result) {
        bb_memset(out_result, 0, sizeof(*out_result));
        out_result->point_x = x;
        out_result->point_y = y;
        out_result->cursor = BB_CURSOR_DEFAULT;
    }
    return hit;
}

bool hit_test_document(bb_layout_box_t *root,
                       int32_t doc_x, int32_t doc_y,
                       bb_hit_test_result_t *out_result) {
    /* Document coordinates have zero offset and vast initial clipping */
    bb_rect_t infinite_clip = { -1000000, -1000000, 2000000, 2000000 };
    return hit_test_at_point(root, doc_x, doc_y, 0, 0, infinite_clip, out_result);
}

bool hit_test_viewport(bb_layout_box_t *root,
                       int32_t vp_x, int32_t vp_y,
                       int32_t scroll_x, int32_t scroll_y,
                       const bb_rect_t *viewport_clip,
                       bb_hit_test_result_t *out_result) {
    bb_rect_t clip;
    if (viewport_clip) {
        clip = *viewport_clip;
    } else {
        clip.x = 0;
        clip.y = 0;
        clip.w = 1000000;
        clip.h = 1000000;
    }

    if (!point_in_rect(vp_x, vp_y, clip)) {
        if (out_result) {
            bb_memset(out_result, 0, sizeof(*out_result));
            out_result->point_x = vp_x;
            out_result->point_y = vp_y;
            out_result->cursor = BB_CURSOR_DEFAULT;
        }
        return false;
    }

    /* Convert viewport coordinates to document space with scroll offsets */
    int32_t offset_x = (viewport_clip ? viewport_clip->x : 0) - scroll_x;
    int32_t offset_y = (viewport_clip ? viewport_clip->y : 0) - scroll_y;
    return hit_test_at_point(root, vp_x, vp_y, offset_x, offset_y, clip, out_result);
}

bb_node_t *node_at_point(bb_layout_box_t *root, int32_t x, int32_t y) {
    bb_hit_test_result_t res;
    if (hit_test_document(root, x, y, &res)) {
        return res.node;
    }
    return NULL;
}
