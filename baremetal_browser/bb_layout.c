#include "bb_layout.h"
#include "painter.h"
#include "hit_test.h"
#include "bb_image.h"

/* =========================================================================
 * Global viewport for POS_FIXED containing-block resolution
 * ========================================================================= */
int32_t g_layout_viewport_w = 800;
int32_t g_layout_viewport_h = 600;

/* =========================================================================
 * LineBox implementation
 * ========================================================================= */
#define LINEBOX_INITIAL_CAP 32

void linebox_init(bb_line_box_t *lb) {
    if (!lb) return;
    lb->items        = (bb_layout_box_t **)bb_malloc(LINEBOX_INITIAL_CAP * sizeof(bb_layout_box_t *));
    lb->item_count   = 0;
    lb->item_capacity= lb->items ? LINEBOX_INITIAL_CAP : 0;
    lb->width        = 0;
    lb->height       = 0;
    lb->baseline     = 0;
}

void linebox_add(bb_line_box_t *lb, bb_layout_box_t *item, int32_t item_w, int32_t item_h) {
    if (!lb || !item) return;

    /* Grow if needed */
    if (lb->item_count >= lb->item_capacity) {
        int new_cap = lb->item_capacity ? lb->item_capacity * 2 : LINEBOX_INITIAL_CAP;
        bb_layout_box_t **new_items =
            (bb_layout_box_t **)bb_realloc(lb->items, (size_t)new_cap * sizeof(bb_layout_box_t *));
        if (!new_items) return;
        lb->items        = new_items;
        lb->item_capacity= new_cap;
    }

    lb->items[lb->item_count++] = item;
    lb->width  += item_w;
    if (item_h > lb->height) lb->height = item_h;
}

/* Recursively shift an entire subtree by delta_x pixels. */
static void layout_shift_subtree_x(bb_layout_box_t *box, int32_t delta_x) {
    if (!box) return;
    box->dimensions.x += delta_x;
    bb_layout_box_t *child = box->first_child;
    while (child) {
        layout_shift_subtree_x(child, delta_x);
        child = child->next_sibling;
    }
}

void linebox_flush(bb_line_box_t *lb,
                   int32_t container_content_w,
                   int32_t container_padding_left,
                   int     text_align,
                   int32_t *cursor_x_out,
                   int32_t *cursor_y_in_out) {
    if (!lb) return;

    /* --- Pass 2: compute horizontal alignment offset -------------------- */
    int32_t offset_x = 0;
    if (lb->item_count > 0) {
        int32_t remaining = container_content_w - lb->width;
        if (remaining > 0) {
            if (text_align == BB_TEXT_ALIGN_CENTER) {
                offset_x = remaining / 2;
            } else if (text_align == BB_TEXT_ALIGN_RIGHT) {
                offset_x = remaining;
            }
        }
        if (offset_x > 0) {
            for (int i = 0; i < lb->item_count; i++) {
                layout_shift_subtree_x(lb->items[i], offset_x);
            }
        }
    }

    /* Advance cursor */
    if (cursor_y_in_out) *cursor_y_in_out += lb->height;
    if (cursor_x_out)    *cursor_x_out     = container_padding_left;

    /* Reset for next line */
    lb->item_count = 0;
    lb->width      = 0;
    lb->height     = 0;
    lb->baseline   = 0;
}

void linebox_free(bb_line_box_t *lb) {
    if (!lb) return;
    if (lb->items) {
        bb_free(lb->items);
        lb->items = NULL;
    }
    lb->item_count    = 0;
    lb->item_capacity = 0;
}

/* =========================================================================
 * Whitespace collapsing  (CSS white-space: normal)
 * ========================================================================= */
size_t bb_html_collapse_whitespace(char *text, bool strip_leading,
                                   bool *has_trailing_space) {
    if (!text) return 0;

    char *src = text;
    char *dst = text;
    bool in_ws      = false;
    bool line_empty = strip_leading; /* treat start-of-line as "line empty" */

    while (*src) {
        unsigned char c = (unsigned char)*src;
        bool is_ws = (c == ' ' || c == '\t' || c == '\r' || c == '\n');

        if (is_ws) {
            if (!in_ws) {
                /* Emit exactly one space, unless line is empty (strip leading) */
                if (!line_empty) {
                    *dst++ = ' ';
                }
            }
            in_ws = true;
        } else {
            *dst++     = (char)c;
            in_ws      = false;
            line_empty = false;
        }
        src++;
    }

    /* Optionally record whether the result ends in a space */
    if (has_trailing_space) {
        *has_trailing_space = (dst > text && *(dst - 1) == ' ');
    }

    /* Strip trailing space (only the single collapsed space we may have left) */
    if (dst > text && *(dst - 1) == ' ') {
        dst--;
    }

    *dst = '\0';
    return (size_t)(dst - text);
}

/* =========================================================================
 * Internal helpers
 * ========================================================================= */
static bb_layout_box_t *create_box(bb_box_type_t type, bb_node_t *node) {
    bb_layout_box_t *box = (bb_layout_box_t *)bb_malloc(sizeof(bb_layout_box_t));
    if (!box) return NULL;
    bb_memset(box, 0, sizeof(bb_layout_box_t));
    box->type       = type;
    box->node       = node;
    box->font_style = BB_FONT_STYLE_REGULAR;
    box->text_color = BB_RGB(32, 33, 36);
    box->background_color = BB_COLOR_TRANSPARENT;
    box->border_color     = BB_COLOR_BORDER_GRAY;

    if (node && node->computed_style) {
        bb_style_t *s = node->computed_style;
        box->margin_top    = s->margin_top;
        box->margin_right  = s->margin_right;
        box->margin_bottom = s->margin_bottom;
        box->margin_left   = s->margin_left;

        box->padding_top    = s->padding_top;
        box->padding_right  = s->padding_right;
        box->padding_bottom = s->padding_bottom;
        box->padding_left   = s->padding_left;

        box->border_width  = s->border_width;
        box->border_color  = s->border_color;
        box->border_radius = s->border_radius;

        box->background_color = s->background_color;
        box->text_color       = s->color;

        box->font_style.scale  = (s->font_size >= 28) ? 2 : 1;
        box->font_style.bold   = (s->font_weight >= 600);

        if (node->tag_name && bb_strcasecmp(node->tag_name, "a") == 0) {
            box->font_style.underline = true;
        }
    }

    box->image = node ? node->image : NULL;

    if (node) node->layout_box = box;
    return box;
}

static void resolve_replaced_dimensions(bb_layout_box_t *box, int32_t container_w, int32_t *out_w, int32_t *out_h) {
    (void)container_w;
    bb_style_t *s = box->node ? box->node->computed_style : NULL;
    bb_image_t *img = box->image;
    if (!img && box->node) img = box->node->image;

    int32_t specified_w = -1;
    int32_t specified_h = -1;

    /* 1. CSS width and height */
    if (s && s->width != -1) specified_w = s->width;
    if (s && s->height != -1) specified_h = s->height;

    /* 2. HTML attributes width and height (presentation hints) */
    if (specified_w == -1 && box->node) {
        const char *attr_w = bb_node_get_attribute(box->node, "width");
        if (attr_w && *attr_w) {
            int32_t val = (int32_t)bb_atoi(attr_w);
            if (val > 0) specified_w = val;
        }
    }
    if (specified_h == -1 && box->node) {
        const char *attr_h = bb_node_get_attribute(box->node, "height");
        if (attr_h && *attr_h) {
            int32_t val = (int32_t)bb_atoi(attr_h);
            if (val > 0) specified_h = val;
        }
    }

    /* 3. Intrinsic dimensions and aspect ratio */
    int32_t int_w = (img && img->state == BB_IMAGE_STATE_LOADED) ? img->intrinsic_width : 0;
    int32_t int_h = (img && img->state == BB_IMAGE_STATE_LOADED) ? img->intrinsic_height : 0;

    int32_t used_w = specified_w;
    int32_t used_h = specified_h;

    if (specified_w != -1 && specified_h != -1) {
        /* Both specified */
        used_w = specified_w;
        used_h = specified_h;
    } else if (specified_w != -1 && specified_h == -1) {
        /* Width specified, height auto */
        used_w = specified_w;
        if (int_w > 0 && int_h > 0) {
            used_h = (int32_t)((specified_w * int_h) / int_w);
        } else if (int_h > 0) {
            used_h = int_h;
        } else {
            used_h = BB_IMAGE_FALLBACK_HEIGHT;
        }
    } else if (specified_w == -1 && specified_h != -1) {
        /* Height specified, width auto */
        used_h = specified_h;
        if (int_w > 0 && int_h > 0) {
            used_w = (int32_t)((specified_h * int_w) / int_h);
        } else if (int_w > 0) {
            used_w = int_w;
        } else {
            used_w = BB_IMAGE_FALLBACK_WIDTH;
        }
    } else {
        /* Both auto */
        if (int_w > 0 && int_h > 0) {
            used_w = int_w;
            used_h = int_h;
        } else {
            used_w = BB_IMAGE_FALLBACK_WIDTH;
            used_h = BB_IMAGE_FALLBACK_HEIGHT;
        }
    }

    if (out_w) *out_w = (used_w >= 0) ? used_w : BB_IMAGE_FALLBACK_WIDTH;
    if (out_h) *out_h = (used_h >= 0) ? used_h : BB_IMAGE_FALLBACK_HEIGHT;
}

static void box_append_child(bb_layout_box_t *parent, bb_layout_box_t *child) {
    if (!parent || !child) return;
    child->parent       = parent;
    child->next_sibling = NULL;
    child->prev_sibling = parent->last_child;
    if (parent->last_child) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
}

/* =========================================================================
 * Containing-block resolution
 * ========================================================================= */
static bb_rect_t find_containing_block(bb_layout_box_t *box) {
    bb_style_t *s = (box && box->node) ? box->node->computed_style : NULL;
    bb_position_t pos = s ? s->position : BB_POSITION_STATIC;

    /* POS_FIXED always uses the root viewport */
    if (pos == BB_POSITION_FIXED) {
        bb_rect_t vp = {0, 0, g_layout_viewport_w, g_layout_viewport_h};
        return vp;
    }

    /* POS_ABSOLUTE: walk up to nearest ancestor with position != STATIC */
    bb_layout_box_t *cur = box ? box->parent : NULL;
    while (cur) {
        bb_style_t *cs = cur->node ? cur->node->computed_style : NULL;
        bb_position_t cp = cs ? cs->position : BB_POSITION_STATIC;
        if (cp != BB_POSITION_STATIC) {
            /* Border-box of the containing block */
            bb_rect_t cb = {
                cur->dimensions.x - cur->padding_left - cur->border_width,
                cur->dimensions.y - cur->padding_top  - cur->border_width,
                cur->dimensions.w + cur->padding_left + cur->padding_right + cur->border_width * 2,
                cur->dimensions.h + cur->padding_top  + cur->padding_bottom+ cur->border_width * 2
            };
            return cb;
        }
        cur = cur->parent;
    }

    /* No positioned ancestor → initial viewport */
    bb_rect_t vp = {0, 0, g_layout_viewport_w, g_layout_viewport_h};
    return vp;
}

/* =========================================================================
 * Layout tree builder
 * ========================================================================= */
bb_layout_box_t *bb_layout_build_tree(bb_node_t *node) {
    if (!node) return NULL;
    if (node->type == BB_NODE_COMMENT) return NULL;

    if (node->computed_style && node->computed_style->display == BB_DISPLAY_NONE) {
        return NULL;
    }

    bb_layout_box_t *box = NULL;

    if (node->type == BB_NODE_DOCUMENT) {
        box = create_box(BB_BOX_BLOCK, node);
    } else if (node->type == BB_NODE_ELEMENT) {
        if (node->tag_name && bb_strcasecmp(node->tag_name, "img") == 0) {
            if (!node->image) {
                node->image = bb_image_create(bb_node_get_attribute(node, "src"),
                                              bb_node_get_attribute(node, "alt"));
            }
            box = create_box(BB_BOX_REPLACED, node);
            box->image = node->image;
        } else if (node->computed_style && node->computed_style->display == BB_DISPLAY_BLOCK) {
            box = create_box(BB_BOX_BLOCK, node);
        } else {
            box = create_box(BB_BOX_INLINE, node);
        }
    } else if (node->type == BB_NODE_TEXT) {
        if (!node->text_content || node->text_content[0] == '\0') return NULL;

        /* Collapse whitespace before building the text box */
        char *collapsed = bb_strdup(node->text_content);
        if (!collapsed) return NULL;
        bool has_trailing;
        size_t new_len = bb_html_collapse_whitespace(collapsed, false, &has_trailing);
        if (new_len == 0) { bb_free(collapsed); return NULL; }

        box = create_box(BB_BOX_ANONYMOUS_TEXT, node);
        if (!box) { bb_free(collapsed); return NULL; }
        box->text = collapsed;

        /* Inherit style from parent layout box */
        if (node->parent && node->parent->layout_box) {
            box->font_style = node->parent->layout_box->font_style;
            box->text_color = node->parent->layout_box->text_color;
        }
    }

    if (!box) return NULL;

    /* Build children */
    bb_node_t *child_node = node->first_child;
    while (child_node) {
        bb_layout_box_t *child_box = bb_layout_build_tree(child_node);
        if (child_box) box_append_child(box, child_box);
        child_node = child_node->next_sibling;
    }

    return box;
}

/* =========================================================================
 * Layout computation  (positioning model + two-pass line boxes)
 * ========================================================================= */
void bb_layout_compute(bb_layout_box_t *box, int32_t x, int32_t y, int32_t container_w) {
    if (!box) return;

    bb_style_t *s = box->node ? box->node->computed_style : NULL;

    /* ---------- Determine content width ---------------------------------- */
    int32_t total_margin_x  = box->margin_left + box->margin_right;
    int32_t total_padding_x = box->padding_left + box->padding_right;
    int32_t total_border_x  = box->border_width * 2;

    int32_t content_w;
    if (box->type == BB_BOX_REPLACED) {
        int32_t rw, rh;
        resolve_replaced_dimensions(box, container_w, &rw, &rh);
        content_w = rw;
    } else if (s && s->width != -1) {
        content_w = s->width;
    } else if (box->type == BB_BOX_BLOCK) {
        content_w = container_w - total_margin_x - total_padding_x - total_border_x;
        if (content_w < 0) content_w = 0;
    } else {
        content_w = container_w;
    }

    box->dimensions.x = x + box->margin_left + box->border_width + box->padding_left;
    box->dimensions.y = y + box->margin_top  + box->border_width + box->padding_top;
    box->dimensions.w = content_w;

    /* ---------- Get text-align for this block ----------------------------- */
    int text_align = s ? s->text_align : BB_TEXT_ALIGN_LEFT;

    /* ---------- Lay out children ----------------------------------------- */
    int32_t cur_y     = 0;     /* relative to box->dimensions.y */
    int32_t inline_x  = 0;     /* relative to box->dimensions.x */
    int32_t line_h    = bb_font_get_line_height(&box->font_style);

    /* Two-pass line box collector for inline children */
    bb_line_box_t line;
    linebox_init(&line);

    /* We do two sweeps:
     *   (a) block children are laid out immediately, flushing any pending line first
     *   (b) inline/text children are accumulated into a LineBox; the line is flushed
     *       on overflow, on <br>, or when a block child follows inline content.
     *
     * Out-of-flow children (abs/fixed) are deferred to a second pass so their
     * parents' content dimensions are already resolved.                      */

    /* Storage for deferred out-of-flow children (max 64 per container) */
#define MAX_OOF 64
    bb_layout_box_t *oof_children[MAX_OOF];
    int oof_count = 0;

    bb_layout_box_t *child = box->first_child;
    while (child) {
        bb_style_t *cs = child->node ? child->node->computed_style : NULL;
        bb_position_t child_pos = cs ? cs->position : BB_POSITION_STATIC;

        /* --- Defer out-of-flow children ---------------------------------- */
        if (child_pos == BB_POSITION_ABSOLUTE || child_pos == BB_POSITION_FIXED) {
            if (oof_count < MAX_OOF) oof_children[oof_count++] = child;
            child = child->next_sibling;
            continue;
        }

        if (child->type == BB_BOX_BLOCK) {
            /* Flush any pending inline line before starting a block */
            if (line.item_count > 0) {
                int32_t cx_reset = box->dimensions.x;
                linebox_flush(&line, content_w, box->padding_left,
                              text_align, &cx_reset, &cur_y);
                inline_x = 0;
            } else if (inline_x > 0) {
                cur_y   += line_h;
                inline_x = 0;
            }

            int32_t child_start_y = box->dimensions.y + cur_y;
            bb_layout_compute(child, box->dimensions.x, child_start_y, content_w);

            int32_t child_total_h = child->margin_top + child->border_width * 2
                                  + child->padding_top + child->padding_bottom
                                  + child->dimensions.h + child->margin_bottom;

            /* POS_RELATIVE: apply visual offset after flow placement */
            if (child_pos == BB_POSITION_RELATIVE && cs) {
                if (cs->has_left) child->dimensions.x += cs->left;
                if (cs->has_top)  child->dimensions.y += cs->top;
                layout_shift_subtree_x(child->first_child, cs->has_left ? cs->left : 0);
            }

            cur_y += child_total_h;

        } else if (child->type == BB_BOX_INLINE) {
            /* Check for <br> */
            if (child->node && child->node->tag_name &&
                bb_strcasecmp(child->node->tag_name, "br") == 0)
            {
                if (line.item_count > 0) {
                    int32_t cx_reset = box->dimensions.x;
                    linebox_flush(&line, content_w, box->padding_left,
                                  text_align, &cx_reset, &cur_y);
                } else {
                    cur_y += line_h;
                }
                inline_x = 0;
                child = child->next_sibling;
                continue;
            }

            /* Measure inline child */
            int32_t child_pad_x = child->padding_left + child->padding_right + child->border_width * 2;
            int32_t child_pad_y = child->padding_top + child->padding_bottom + child->border_width * 2;
            (void)child_pad_y;

            int32_t child_content_w = 0;
            int32_t child_content_h = line_h;

            if (child->first_child && child->first_child->type == BB_BOX_ANONYMOUS_TEXT
                && child->first_child->text) {
                int32_t tw, th;
                bb_font_measure_string(child->first_child->text, &child->font_style, &tw, &th);
                child_content_w = tw;
                child_content_h = th;
                child->first_child->dimensions.w = tw;
                child->first_child->dimensions.h = th;
            }

            int32_t box_w = child_content_w + child_pad_x;

            /* Wrap if overflow */
            if (inline_x + box_w > content_w && line.item_count > 0) {
                /* Line break — flush and reset */
                int32_t cx_reset = box->dimensions.x;
                linebox_flush(&line, content_w, box->padding_left,
                              text_align, &cx_reset, &cur_y);
                inline_x = 0;
            }

            /* Place the inline child */
            child->dimensions.x = box->dimensions.x + inline_x
                                 + child->margin_left + child->border_width + child->padding_left;
            child->dimensions.y = box->dimensions.y + cur_y
                                 + child->margin_top + child->border_width + child->padding_top;
            child->dimensions.w = child_content_w;
            child->dimensions.h = child_content_h;

            if (child->first_child) {
                child->first_child->dimensions.x = child->dimensions.x;
                child->first_child->dimensions.y = child->dimensions.y;
            }

            linebox_add(&line, child, box_w, child_content_h + child->border_width * 2
                                + child->padding_top + child->padding_bottom);
            inline_x += box_w + child->margin_left + child->margin_right + 4;

            /* Apply relative offset without affecting siblings */
            if (child_pos == BB_POSITION_RELATIVE && cs) {
                if (cs->has_left) child->dimensions.x += cs->left;
                if (cs->has_top)  child->dimensions.y += cs->top;
            }

        } else if (child->type == BB_BOX_REPLACED) {
            int32_t rw, rh;
            resolve_replaced_dimensions(child, content_w, &rw, &rh);

            if (cs && cs->display == BB_DISPLAY_BLOCK) {
                /* Block replaced box */
                if (line.item_count > 0) {
                    int32_t cx_reset = box->dimensions.x;
                    linebox_flush(&line, content_w, box->padding_left,
                                  text_align, &cx_reset, &cur_y);
                    inline_x = 0;
                } else if (inline_x > 0) {
                    cur_y += line_h;
                    inline_x = 0;
                }

                child->dimensions.x = box->dimensions.x + child->margin_left + child->border_width + child->padding_left;
                child->dimensions.y = box->dimensions.y + cur_y + child->margin_top + child->border_width + child->padding_top;
                child->dimensions.w = rw;
                child->dimensions.h = rh;

                int32_t child_total_h = child->margin_top + child->border_width * 2
                                      + child->padding_top + child->padding_bottom
                                      + child->dimensions.h + child->margin_bottom;

                if (child_pos == BB_POSITION_RELATIVE && cs) {
                    if (cs->has_left) child->dimensions.x += cs->left;
                    if (cs->has_top)  child->dimensions.y += cs->top;
                }

                cur_y += child_total_h;
            } else {
                /* Inline replaced box */
                int32_t box_w = rw + child->padding_left + child->padding_right + child->border_width * 2;
                int32_t box_h = rh + child->padding_top + child->padding_bottom + child->border_width * 2;

                /* Wrap if line overflow */
                if (inline_x + box_w > content_w && line.item_count > 0) {
                    int32_t cx_reset = box->dimensions.x;
                    linebox_flush(&line, content_w, box->padding_left,
                                  text_align, &cx_reset, &cur_y);
                    inline_x = 0;
                }

                child->dimensions.x = box->dimensions.x + inline_x
                                     + child->margin_left + child->border_width + child->padding_left;
                child->dimensions.y = box->dimensions.y + cur_y
                                     + child->margin_top + child->border_width + child->padding_top;
                child->dimensions.w = rw;
                child->dimensions.h = rh;

                linebox_add(&line, child, box_w, box_h);
                inline_x += box_w + child->margin_left + child->margin_right + 4;

                if (child_pos == BB_POSITION_RELATIVE && cs) {
                    if (cs->has_left) child->dimensions.x += cs->left;
                    if (cs->has_top)  child->dimensions.y += cs->top;
                }
            }

        } else if (child->type == BB_BOX_ANONYMOUS_TEXT) {
            /* Word-wrap text run with UTF-8 awareness */
            if (child->text) {
                int scale  = child->font_style.scale > 0 ? child->font_style.scale : 1;
                int char_w = 8 * scale;
                int l_h    = 16 * scale + 2;

                child->dimensions.x = box->dimensions.x + inline_x;
                child->dimensions.y = box->dimensions.y + cur_y;

                /* Collapse whitespace — strip leading if inline_x == 0 */
                bool strip_leading = (inline_x == 0);
                bool has_trailing;
                bb_html_collapse_whitespace(child->text, strip_leading, &has_trailing);

                const char *p   = child->text;
                const char *end = NULL; /* NUL-terminated */

                while (true) {
                    /* Skip (collapsed) spaces */
                    while (*p == ' ') {
                        /* NBSP would have been decoded separately; regular collapsed
                         * spaces allow a line-break opportunity if needed.         */
                        if (inline_x + char_w > content_w && inline_x > 0) {
                            /* Flush current line */
                            int32_t cx_reset = box->dimensions.x;
                            linebox_flush(&line, content_w, box->padding_left,
                                          text_align, &cx_reset, &cur_y);
                            inline_x = 0;
                        } else {
                            inline_x += char_w;
                        }
                        p++;
                    }
                    if (!*p) break;

                    /* Measure next word boundary in codepoints */
                    const char *word_start = p;
                    const char *scan       = p;
                    while (*scan && *scan != ' ' && *scan != '\n') {
                        uint32_t cp = bb_utf8_decode_next(&scan, end);
                        (void)cp;
                    }
                    int32_t word_bytes  = (int32_t)(scan - word_start);
                    /* Count codepoints for width */
                    const char *cntptr  = word_start;
                    int32_t codepoints  = 0;
                    while (cntptr < scan) {
                        bb_utf8_decode_next(&cntptr, scan);
                        codepoints++;
                    }
                    int32_t word_w = codepoints * char_w;

                    if (inline_x + word_w > content_w && inline_x > 0) {
                        int32_t cx_reset = box->dimensions.x;
                        linebox_flush(&line, content_w, box->padding_left,
                                      text_align, &cx_reset, &cur_y);
                        inline_x = 0;
                        /* Reset text box position to current cursor */
                        child->dimensions.x = box->dimensions.x;
                        child->dimensions.y = box->dimensions.y + cur_y;
                    }

                    inline_x += word_w;
                    p = scan;

                    if (*p == '\n') {
                        int32_t cx_reset = box->dimensions.x;
                        linebox_flush(&line, content_w, box->padding_left,
                                      text_align, &cx_reset, &cur_y);
                        inline_x = 0;
                        p++;
                    }
                    (void)word_bytes; /* used implicitly via scan pointer */
                }

                /* Add text run to current line box */
                linebox_add(&line, child, inline_x, l_h);
                child->dimensions.w = content_w;
                child->dimensions.h = l_h;
            }
        }

        child = child->next_sibling;
    }

    /* Flush any remaining inline line */
    if (line.item_count > 0) {
        int32_t cx_reset = box->dimensions.x;
        linebox_flush(&line, content_w, box->padding_left,
                      text_align, &cx_reset, &cur_y);
        inline_x = 0;
    } else if (inline_x > 0) {
        cur_y += line_h;
    }

    linebox_free(&line);

    /* ---------- Resolve content height ----------------------------------- */
    if (box->type == BB_BOX_REPLACED) {
        int32_t rw, rh;
        resolve_replaced_dimensions(box, container_w, &rw, &rh);
        box->dimensions.h = rh;
    } else if (s && s->height != -1) {
        box->dimensions.h = s->height;
    } else {
        box->dimensions.h = cur_y;
    }

    /* ---------- Second pass: out-of-flow children (abs / fixed) ---------- */
    for (int i = 0; i < oof_count; i++) {
        bb_layout_box_t *oof = oof_children[i];
        bb_style_t      *cs  = oof->node ? oof->node->computed_style : NULL;
        if (!cs) continue;

        bb_rect_t cb = find_containing_block(oof);

        /* Determine box dimensions (use explicit values or estimate) */
        int32_t oof_w = (cs->width  != -1) ? cs->width  : 100;
        int32_t oof_h = (cs->height != -1) ? cs->height :  20;

        /* Horizontal */
        if (cs->has_left) {
            oof->dimensions.x = cb.x + cs->left + oof->margin_left;
        } else if (cs->has_right) {
            oof->dimensions.x = cb.x + cb.w - cs->right - oof_w - oof->margin_right;
        } else {
            oof->dimensions.x = cb.x + oof->margin_left;
        }

        /* Vertical */
        if (cs->has_top) {
            oof->dimensions.y = cb.y + cs->top + oof->margin_top;
        } else if (cs->has_bottom) {
            oof->dimensions.y = cb.y + cb.h - cs->bottom - oof_h - oof->margin_bottom;
        } else {
            oof->dimensions.y = cb.y + oof->margin_top;
        }

        oof->dimensions.w = oof_w;
        oof->dimensions.h = oof_h;

        /* Recursively compute layout for children of the OOF subtree */
        bb_layout_compute(oof, oof->dimensions.x, oof->dimensions.y, oof_w);
    }
}

/* =========================================================================
 * Paint pass
 * ========================================================================= */
void bb_layout_paint(bb_layout_box_t *box, bb_raster_t *r, int32_t offset_x, int32_t offset_y) {
    if (!box || !r) return;
    painter_paint_tree(box, r, offset_x, offset_y);
}

/* =========================================================================
 * Hit test
 * ========================================================================= */
bb_node_t *bb_layout_hit_test(bb_layout_box_t *box, int32_t x, int32_t y,
                               int32_t offset_x, int32_t offset_y) {
    if (!box) return NULL;
    bb_hit_test_result_t res;
    bb_rect_t infinite_clip = { -1000000, -1000000, 2000000, 2000000 };
    if (hit_test_at_point(box, x, y, offset_x, offset_y, infinite_clip, &res)) {
        return res.node;
    }
    return NULL;
}

/* =========================================================================
 * Free tree
 * ========================================================================= */
void bb_layout_free_tree(bb_layout_box_t *box) {
    if (!box) return;
    bb_layout_box_t *child = box->first_child;
    while (child) {
        bb_layout_box_t *next = child->next_sibling;
        bb_layout_free_tree(child);
        child = next;
    }
    if (box->text) bb_free(box->text);
    bb_free(box);
}
