import sys

with open('/Users/mohamadjlo/Desktop/FirstOS/html_parser.c', 'r') as f:
    lines = f.readlines()

replacement = """/* ============================================================
 * Recursive Layout Engine — replaces the old flat loop
 * ============================================================ */

/* Measure width of an inline element */
static int measure_inline_w(dom_node_t *n) {
    if (!n) return 0;
    if (n->tag == TAG_BUTTON) {
        const char *lbl = n->text[0] ? n->text : (n->value[0] ? n->value : "Button");
        return (int)strlen(lbl)*8 + n->padding_left + n->padding_right + 14;
    }
    if (n->tag == TAG_INPUT) {
        if (n->input_type == 1 || n->input_type == 2) return 18;
        if (n->input_type == 4 || n->input_type == 5) {
            const char *lbl = n->value[0] ? n->value : "Button";
            return (int)strlen(lbl)*8 + 20;
        }
        return n->req_width > 0 ? n->req_width : 160;
    }
    if (n->tag == TAG_IMG)      return n->req_width  > 0 ? n->req_width  : 120;
    if (n->tag == TAG_SELECT)   return n->req_width  > 0 ? n->req_width  : 140;
    if (n->tag == TAG_PROGRESS) return n->req_width  > 0 ? n->req_width  : 160;
    if (n->tag == TAG_TEXTAREA) return n->req_width  > 0 ? n->req_width  : 220;
    if (n->tag == TAG_TH || n->tag == TAG_TD) return n->req_width > 0 ? n->req_width : 100;
    if (n->tag == TAG_AUDIO)    return n->req_width  > 0 ? n->req_width  : 320;
    if (n->tag == TAG_CANVAS)   return n->req_width  > 0 ? n->req_width  : 300;
    int fs = (n->font_scale > 0) ? n->font_scale : 1;
    return (int)strlen(n->text) * 8 * fs + n->padding_left + n->padding_right + 4;
}

/* Measure height of an inline element */
static int measure_inline_h(dom_node_t *n) {
    if (!n) return 16;
    if (n->tag == TAG_BUTTON) {
        int base = 14 + n->padding_top + n->padding_bottom;
        return n->req_height > 0 ? n->req_height : (base < 22 ? 22 : base);
    }
    if (n->tag == TAG_INPUT) {
        if (n->input_type == 1 || n->input_type == 2) return 18;
        return n->req_height > 0 ? n->req_height : 26;
    }
    if (n->tag == TAG_IMG)      return n->req_height > 0 ? n->req_height : 80;
    if (n->tag == TAG_SELECT)   return n->req_height > 0 ? n->req_height : 26;
    if (n->tag == TAG_PROGRESS) return n->req_height > 0 ? n->req_height : 18;
    if (n->tag == TAG_TEXTAREA) return n->req_height > 0 ? n->req_height : 60;
    if (n->tag == TAG_TH || n->tag == TAG_TD) return n->req_height > 0 ? n->req_height : 26;
    if (n->tag == TAG_AUDIO)    return n->req_height > 0 ? n->req_height : 36;
    if (n->tag == TAG_CANVAS)   return n->req_height > 0 ? n->req_height : 150;
    int fs = (n->font_scale > 0) ? n->font_scale : 1;
    return 16 * fs + n->padding_top + n->padding_bottom;
}

/* Layout flex container's direct children horizontally */
static void layout_flex_children(dom_node_t *flex_node) {
    int child_count = 0, total_child_w = 0, max_child_h = 0;
    for (dom_node_t *c = flex_node->first_child; c; c = c->next_sibling) {
        if (c->display_mode == CSS_DISPLAY_NONE) continue;
        if (c->is_block && c->w <= 0) c->w = flex_node->w / 3;
        if (c->w <= 0) c->w = measure_inline_w(c);
        if (c->h <= 0) c->h = measure_inline_h(c);
        total_child_w += c->w + flex_node->gap;
        if (c->h > max_child_h) max_child_h = c->h;
        child_count++;
    }
    if (child_count > 0 && flex_node->gap > 0) total_child_w -= flex_node->gap;

    int inner_w = flex_node->w - flex_node->padding_left - flex_node->padding_right;
    if (inner_w < 1) inner_w = 1;
    int start_x = flex_node->x + flex_node->padding_left;
    int spacing  = flex_node->gap;

    if (flex_node->justify_content == CSS_JUSTIFY_CENTER && total_child_w < inner_w)
        start_x += (inner_w - total_child_w) / 2;
    else if (flex_node->justify_content == CSS_JUSTIFY_END && total_child_w < inner_w)
        start_x += inner_w - total_child_w;
    else if (flex_node->justify_content == CSS_JUSTIFY_BETWEEN && child_count > 1) {
        int slack = inner_w - total_child_w;
        if (slack > 0) spacing = slack / (child_count - 1) + flex_node->gap;
    }

    int cx2    = start_x;
    int base_y = flex_node->y + flex_node->padding_top;
    for (dom_node_t *c = flex_node->first_child; c; c = c->next_sibling) {
        if (c->display_mode == CSS_DISPLAY_NONE) continue;
        c->x = cx2;
        c->y = (flex_node->align_items == CSS_ALIGN_CENTER)
               ? base_y + (max_child_h - c->h) / 2
               : base_y;
        c->in_flex = 1;
        cx2 += c->w + spacing;
    }
    if (flex_node->req_height <= 0)
        flex_node->h = max_child_h + flex_node->padding_top + flex_node->padding_bottom;
    if (flex_node->h <= 0) flex_node->h = 24;
}

/* Estimate text height for a block node */
static int estimate_text_h(dom_node_t *n, int avail_w) {
    if (!n->text[0]) return 0;
    int fs  = (n->font_scale > 0) ? n->font_scale : 1;
    int cw2 = avail_w - n->padding_left - n->padding_right;
    if (cw2 < 60) cw2 = 60;
    int char_w = 8 * fs;
    int cur_lw = 0, num_lines = 1;
    const char *tp = n->text;
    while (*tp) {
        while (*tp == ' ') { cur_lw += char_w; tp++; }
        if (!*tp) break;
        int wl = 0;
        while (tp[wl] && tp[wl] != ' ' && tp[wl] != '\n') wl++;
        int ww = wl * char_w;
        if (cur_lw + ww > cw2 && cur_lw > 0) { num_lines++; cur_lw = ww; }
        else cur_lw += ww;
        tp += wl;
        if (*tp == '\n') { num_lines++; cur_lw = 0; tp++; }
    }
    return num_lines * 16 * fs + n->padding_top + n->padding_bottom;
}

/* Lay out children of a block container.
 * cx,cy = top-left of content area. cw = content width.
 * Returns the absolute Y coordinate after all children. */
static int layout_block_children(dom_node_t *parent, int cx, int cy, int cw);

static int layout_block_children(dom_node_t *parent, int cx, int cy, int cw) {
    int cur_y  = cy;
    int cur_x  = cx;
    int line_h = 16;
    int had_inline = 0;

    for (dom_node_t *n = parent->first_child; n; n = n->next_sibling) {
        /* Skip non-visual nodes */
        if (n->tag == TAG_HEAD || n->tag == TAG_TITLE || n->tag == TAG_HTML ||
            n->tag == TAG_SCRIPT || n->tag == TAG_STYLE) continue;
        if (n->display_mode == CSS_DISPLAY_NONE) { n->w = 0; n->h = 0; continue; }
        /* Hidden under collapsed <details> */
        if (n->parent && n->parent->tag == TAG_DETAILS &&
            !n->parent->details_open && n->tag != TAG_SUMMARY) {
            n->w = 0; n->h = 0; continue;
        }

        if (n->is_block || n->display_mode == CSS_DISPLAY_FLEX) {
            /* Flush any pending inline line */
            if (had_inline) { cur_y += line_h + 4; cur_x = cx; line_h = 16; had_inline = 0; }

            cur_y += n->margin_top;
            n->x  = cx + n->margin_left;
            n->y  = cur_y;
            n->w  = (n->req_width > 0) ? n->req_width
                                        : (cw - n->margin_left - n->margin_right);
            if (n->w < 0) n->w = 0;

            /* --- Height calculation per element type --- */
            if (n->tag == TAG_HR) {
                n->h = 2;
            } else if (n->tag == TAG_VIDEO) {
                n->h = (n->req_height > 0) ? n->req_height : 240;
            } else if (n->tag == TAG_AUDIO) {
                n->h = (n->req_height > 0) ? n->req_height : 36;
            } else if (n->tag == TAG_CANVAS) {
                n->w = (n->req_width  > 0) ? n->req_width  : 300;
                n->h = (n->req_height > 0) ? n->req_height : 150;
                n->canvas_w = n->w; n->canvas_h = n->h;
                if (!n->canvas_pixels) {
                    n->canvas_pixels = (u32*)kmalloc(n->canvas_w * n->canvas_h * 4);
                    if (n->canvas_pixels)
                        memset(n->canvas_pixels, 0xFF, n->canvas_w * n->canvas_h * 4);
                }
            } else if (n->tag == TAG_IMG) {
                n->h = (n->req_height > 0) ? n->req_height : 80;
            } else if (n->tag == TAG_TEXTAREA) {
                n->h = (n->req_height > 0) ? n->req_height : 60;
            } else if (n->display_mode == CSS_DISPLAY_FLEX) {
                if (n->w <= 0) n->w = cw;
                layout_flex_children(n);
            } else {
                /* Generic block: recurse into children, measure height */
                int child_end_y = cur_y;
                if (n->first_child) {
                    int inner_x = n->x + n->padding_left;
                    int inner_y = n->y + n->padding_top;
                    int inner_w = n->w - n->padding_left - n->padding_right;
                    if (inner_w < 1) inner_w = 1;
                    child_end_y = layout_block_children(n, inner_x, inner_y, inner_w);
                }
                int child_h = (n->first_child) ? (child_end_y - (n->y + n->padding_top)) : 0;
                if (child_h < 0) child_h = 0;
                int text_h  = estimate_text_h(n, n->w);
                int content_h = (child_h > text_h) ? child_h : text_h;
                int min_h   = (n->tag == TAG_H1 || n->tag == TAG_H2) ? 28 :
                              (n->tag == TAG_H3 || n->tag == TAG_H4) ? 20 : 16;
                if (n->req_height > 0) {
                    n->h = n->req_height;
                } else if (content_h > 0) {
                    n->h = content_h + n->padding_top + n->padding_bottom;
                    if (n->h < min_h) n->h = min_h;
                } else {
                    n->h = min_h;
                }
            }

            cur_y += n->h + n->margin_bottom;

        } else {
            /* ---- Inline element ---- */
            if (n->tag == TAG_BR) {
                cur_y += line_h + 4; cur_x = cx; line_h = 16; had_inline = 0;
                n->w = 0; n->h = 0;
                continue;
            }
            int ew = measure_inline_w(n);
            int eh = measure_inline_h(n);
            /* Wrap to next line if needed */
            if (had_inline && cur_x + ew > cx + cw) {
                cur_y += line_h + 4; cur_x = cx; line_h = 16; had_inline = 0;
            }
            n->x = cur_x; n->y = cur_y;
            n->w = ew;    n->h = eh;
            cur_x += ew + 4;
            if (eh > line_h) line_h = eh;
            had_inline = 1;
        }
    }

    if (had_inline) cur_y += line_h;
    return cur_y;  /* absolute Y after last child */
}

void html_layout(dom_node_t *root, int vx, int vy, int vw, int *out_total_h) {
    if (!root) return;
    /* Root node covers the viewport */
    root->x = vx;
    root->y = vy;
    root->w = vw > 12 ? vw - 12 : vw;
    root->h = 0;
    /* Layout all children of root */
    int end_y = layout_block_children(root, vx + 6, vy + 6, vw - 12);
    root->h   = end_y - vy;
    if (out_total_h) *out_total_h = end_y - vy + 24;
}
"""

# Replace lines 725 to 929 (0-indexed) which correspond to lines 726-930 (1-indexed).
lines = lines[:725] + [replacement + "\n"] + lines[930:]

with open('/Users/mohamadjlo/Desktop/FirstOS/html_parser.c', 'w') as f:
    f.writelines(lines)
