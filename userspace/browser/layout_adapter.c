#include "include/firstos_renderer.h"


typedef struct {
    html_node_t *items[32];
    int count;
    int line_w;
    int line_h;
} line_box_t;

static int get_font_scale(uint8_t fs) {
    if (fs == 3) return 3; /* 24px */
    if (fs == 2) return 2; /* 20px */
    return 1;              /* 14px */
}

static int measure_text_width(const char *text, int len, int scale) {
    if (!text || len <= 0) return 0;
    return len * 8 * scale;
}

static void apply_line_alignment(line_box_t *line, int text_align, int avail_w) {
    if (!line || line->count == 0) return;
    int slack = avail_w - line->line_w;
    if (slack <= 0) return;

    int shift_x = 0;
    if (text_align == 1) shift_x = slack / 2;      /* Center */
    else if (text_align == 2) shift_x = slack;     /* Right */

    if (shift_x > 0) {
        for (int i = 0; i < line->count; i++) {
            line->items[i]->bounds.x += shift_x;
        }
    }
}

static int layout_node_rec(html_node_t *node, int cur_x, int cur_y, int avail_w);

static int layout_flex_children(html_node_t *node, int inner_x, int inner_y, int inner_w) {
    if (!node || !node->first_child) return inner_y;

    int cur_y = inner_y;
    int gap_r = (node->style.gap_row > 0) ? node->style.gap_row : 0;
    int gap_c = (node->style.gap_col > 0) ? node->style.gap_col : 0;

    if (node->style.flex_direction == CSS_FLEX_DIR_COLUMN) {
        for (html_node_t *c = node->first_child; c; c = c->next_sibling) {
            if (c->style.display_none) continue;
            int cw = (c->style.width > 0) ? c->style.width : inner_w;
            if (cw > inner_w) cw = inner_w;
            int next_y = layout_node_rec(c, inner_x, cur_y, cw);
            cur_y = next_y + gap_r;
        }
        return (cur_y > inner_y && gap_r > 0) ? (cur_y - gap_r) : cur_y;
    }

    /* Row flex container */
    html_node_t *line_nodes[64];
    int line_widths[64];
    int line_heights[64];
    int line_count = 0;
    int line_total_w = 0;
    int line_max_h = 0;

    for (html_node_t *c = node->first_child; c; c = c->next_sibling) {
        if (c->style.display_none) continue;

        int item_w = 0;
        int item_h = 0;
        if (c->style.width > 0) {
            item_w = c->style.width;
        } else if (c->tag == HTML_TAG_IMG) {
            item_w = (c->req_width > 0) ? c->req_width : 120;
            item_h = (c->req_height > 0) ? c->req_height : 80;
        } else if (c->tag == HTML_TAG_BUTTON) {
            const char *lbl = (c->text && c->text[0]) ? c->text : (c->value[0] ? c->value : "Button");
            int lw = (int)strlen(lbl) * 8 + 20;
            item_w = (c->req_width > 0) ? c->req_width : ((lw < 60) ? 60 : lw);
            item_h = (c->req_height > 0) ? c->req_height : 26;
        } else if (c->tag == HTML_TAG_INPUT) {
            item_w = (c->req_width > 0) ? c->req_width : 160;
            item_h = (c->req_height > 0) ? c->req_height : 24;
        } else if (c->tag == HTML_TAG_TEXT_NODE) {
            item_w = measure_text_width(c->text, c->text_len, get_font_scale(c->style.font_size));
            item_h = 16 * get_font_scale(c->style.font_size);
        } else {
            item_w = (inner_w > 0) ? (inner_w / 3) : 80;
            if (item_w < 60) item_w = 60;
        }

        if (c->style.height > 0) item_h = c->style.height;
        if (item_h <= 0) item_h = 24;

        int needed_w = (line_count > 0) ? (line_total_w + gap_c + item_w) : item_w;
        if (node->style.flex_wrap == CSS_FLEX_WRAP && line_count > 0 && needed_w > inner_w) {
            /* Wrap to next line: flush current line */
            int start_x = inner_x;
            int item_spacing = gap_c;
            int slack = inner_w - line_total_w;
            if (node->style.justify_content == CSS_JUSTIFY_CENTER && slack > 0) {
                start_x += slack / 2;
            } else if (node->style.justify_content == CSS_JUSTIFY_END && slack > 0) {
                start_x += slack;
            } else if (node->style.justify_content == CSS_JUSTIFY_SPACE_BETWEEN && line_count > 1 && slack > 0) {
                item_spacing += slack / (line_count - 1);
            }

            int cur_x = start_x;
            for (int i = 0; i < line_count; i++) {
                html_node_t *ln = line_nodes[i];
                int cy = cur_y;
                if (node->style.align_items == CSS_ALIGN_CENTER && line_max_h > line_heights[i]) {
                    cy += (line_max_h - line_heights[i]) / 2;
                }
                layout_node_rec(ln, cur_x, cy, line_widths[i]);
                ln->bounds.x = cur_x;
                ln->bounds.y = cy;
                ln->bounds.w = line_widths[i];
                ln->bounds.h = line_heights[i];
                cur_x += line_widths[i] + item_spacing;
            }

            cur_y += line_max_h + gap_r;
            line_count = 0;
            line_total_w = 0;
            line_max_h = 0;
        }

        if (line_count < 63) {
            line_nodes[line_count] = c;
            line_widths[line_count] = item_w;
            line_heights[line_count] = item_h;
            if (line_count > 0) line_total_w += gap_c;
            line_total_w += item_w;
            if (item_h > line_max_h) line_max_h = item_h;
            line_count++;
        }
    }

    /* Flush final line */
    if (line_count > 0) {
        int start_x = inner_x;
        int item_spacing = gap_c;
        int slack = inner_w - line_total_w;
        if (node->style.justify_content == CSS_JUSTIFY_CENTER && slack > 0) {
            start_x += slack / 2;
        } else if (node->style.justify_content == CSS_JUSTIFY_END && slack > 0) {
            start_x += slack;
        } else if (node->style.justify_content == CSS_JUSTIFY_SPACE_BETWEEN && line_count > 1 && slack > 0) {
            item_spacing += slack / (line_count - 1);
        }

        int cur_x = start_x;
        for (int i = 0; i < line_count; i++) {
            html_node_t *ln = line_nodes[i];
            int cy = cur_y;
            if (node->style.align_items == CSS_ALIGN_CENTER && line_max_h > line_heights[i]) {
                cy += (line_max_h - line_heights[i]) / 2;
            }
            layout_node_rec(ln, cur_x, cy, line_widths[i]);
            ln->bounds.x = cur_x;
            ln->bounds.y = cy;
            ln->bounds.w = line_widths[i];
            ln->bounds.h = line_heights[i];
            cur_x += line_widths[i] + item_spacing;
        }
        cur_y += line_max_h;
    }

    return cur_y;
}

static int layout_grid_children(html_node_t *node, int inner_x, int inner_y, int inner_w) {
    if (!node || !node->first_child) return inner_y;

    int cols = (node->style.grid_columns > 0) ? (int)node->style.grid_columns : 2;
    if (cols < 1) cols = 1;
    if (cols > 8) cols = 8;

    int gap_r = (node->style.gap_row > 0) ? node->style.gap_row : 0;
    int gap_c = (node->style.gap_col > 0) ? node->style.gap_col : 0;

    int total_gap_w = (cols - 1) * gap_c;
    int col_w = (inner_w > total_gap_w) ? ((inner_w - total_gap_w) / cols) : (inner_w / cols);
    if (col_w < 10) col_w = 10;

    int cur_row = 0;
    int cur_col = 0;
    int cur_y = inner_y;
    int row_max_h = 0;

    for (html_node_t *c = node->first_child; c; c = c->next_sibling) {
        if (c->style.display_none) continue;

        int cell_x = inner_x + cur_col * (col_w + gap_c);
        int cell_y = cur_y;

        int child_end_y = layout_node_rec(c, cell_x, cell_y, col_w);
        int ch = child_end_y - cell_y;
        if (ch < 24) ch = (c->style.height > 0) ? c->style.height : 24;
        c->bounds.x = cell_x;
        c->bounds.y = cell_y;
        c->bounds.w = col_w;
        c->bounds.h = ch;

        if (ch > row_max_h) row_max_h = ch;

        cur_col++;
        if (cur_col >= cols) {
            cur_y += row_max_h + gap_r;
            cur_col = 0;
            cur_row++;
            row_max_h = 0;
        }
    }

    if (cur_col > 0) {
        cur_y += row_max_h;
    } else if (cur_row > 0 && gap_r > 0) {
        cur_y -= gap_r;
    }

    return cur_y;
}

static int layout_node_rec(html_node_t *node, int cur_x, int cur_y, int avail_w) {
    if (!node || node->style.display_none) {
        if (node) { node->bounds.w = 0; node->bounds.h = 0; }
        return cur_y;
    }

    int scale = get_font_scale(node->style.font_size);

    if (node->tag == HTML_TAG_TEXT_NODE) {
        int tw = measure_text_width(node->text, node->text_len, scale);
        int th = 16 * scale;
        node->bounds.x = cur_x;
        node->bounds.y = cur_y;
        node->bounds.w = tw;
        node->bounds.h = th;
        return cur_y + th;
    }

    /* Compute effective widths */
    int pad_horiz = node->style.padding_left + node->style.padding_right;
    int pad_vert = node->style.padding_top + node->style.padding_bottom;

    int box_w = avail_w;
    if (node->style.width > 0) {
        box_w = node->style.width;
        if (node->style.box_sizing == 0) box_w += pad_horiz;
    } else if (node->tag == HTML_TAG_IMG && node->req_width > 0) {
        box_w = node->req_width + pad_horiz;
    } else if (node->tag == HTML_TAG_IFRAME) {
        box_w = (node->req_width > 0) ? (node->req_width + pad_horiz) : (node->style.width > 0 ? node->style.width : 300);
    } else if (node->tag == HTML_TAG_VIDEO) {
        box_w = (node->req_width > 0) ? (node->req_width + pad_horiz) : (node->style.width > 0 ? node->style.width : 480);
    } else if (node->tag == HTML_TAG_AUDIO) {
        box_w = (node->req_width > 0) ? (node->req_width + pad_horiz) : (node->style.width > 0 ? node->style.width : 320);
    }

    int left_m = node->style.margin_left;
    if (node->style.margin_left_auto && node->style.margin_right_auto) {
        int slack = avail_w - box_w;
        if (slack > 0) left_m = slack / 2;
    }

    int my_x = cur_x + left_m;
    int my_y = cur_y + node->style.margin_top;

    node->bounds.x = my_x;
    node->bounds.y = my_y;
    node->bounds.w = (box_w > 0) ? box_w : 0;

    int inner_x = my_x + node->style.padding_left;
    int inner_y = my_y + node->style.padding_top;
    int inner_w = box_w - pad_horiz;
    if (inner_w < 0) inner_w = 0;

    /* Lay out children */
    int child_y = inner_y;
    if (node->style.display_mode == CSS_DISPLAY_FLEX) {
        child_y = layout_flex_children(node, inner_x, inner_y, inner_w);
    } else if (node->style.display_mode == CSS_DISPLAY_GRID) {
        child_y = layout_grid_children(node, inner_x, inner_y, inner_w);
    } else {
        int child_x = inner_x;

        line_box_t current_line;
        current_line.count = 0;
        current_line.line_w = 0;
        current_line.line_h = 16 * scale;

    for (html_node_t *child = node->first_child; child; child = child->next_sibling) {
        if (child->style.display_none) continue;

        if (child->style.is_block) {
            /* Flush current line */
            if (current_line.count > 0) {
                apply_line_alignment(&current_line, node->style.text_align, inner_w);
                child_y += current_line.line_h;
                current_line.count = 0;
                current_line.line_w = 0;
                current_line.line_h = 16 * scale;
            }
            child_x = inner_x;
            child_y = layout_node_rec(child, inner_x, child_y, inner_w);
        } else {
            /* Inline element */
            int item_w = 0;
            int item_h = 16 * get_font_scale(child->style.font_size);
            if (child->tag == HTML_TAG_TEXT_NODE) {
                item_w = measure_text_width(child->text, child->text_len, get_font_scale(child->style.font_size));
            } else if (child->tag == HTML_TAG_IMG) {
                item_w = (child->req_width > 0) ? child->req_width : ((child->style.width > 0) ? child->style.width : 120);
                item_h = (child->req_height > 0) ? child->req_height : ((child->style.height > 0) ? child->style.height : 80);
            } else if (child->tag == HTML_TAG_INPUT) {
                if (child->input_type && strcmp(child->input_type, "checkbox") == 0) {
                    item_w = 16;
                    item_h = 16;
                } else if (child->input_type && strcmp(child->input_type, "submit") == 0) {
                    const char *lbl = child->value[0] ? child->value : "Submit";
                    int lw = (int)strlen(lbl) * 8 + 20;
                    item_w = (child->req_width > 0) ? child->req_width : ((lw < 60) ? 60 : lw);
                    item_h = (child->req_height > 0) ? child->req_height : 26;
                } else {
                    item_w = (child->req_width > 0) ? child->req_width : ((child->style.width > 0) ? child->style.width : 160);
                    item_h = (child->req_height > 0) ? child->req_height : ((child->style.height > 0) ? child->style.height : 24);
                }
            } else if (child->tag == HTML_TAG_BUTTON) {
                const char *lbl = (child->text && child->text[0]) ? child->text : (child->value[0] ? child->value : "Button");
                int lw = (int)strlen(lbl) * 8 + 20;
                item_w = (child->req_width > 0) ? child->req_width : ((lw < 60) ? 60 : lw);
                item_h = (child->req_height > 0) ? child->req_height : 26;
            } else if (child->tag == HTML_TAG_IFRAME) {
                item_w = (child->req_width > 0) ? child->req_width : ((child->style.width > 0) ? child->style.width : 300);
                item_h = (child->req_height > 0) ? child->req_height : ((child->style.height > 0) ? child->style.height : 150);
                if (item_w > 4096) item_w = 4096;
                if (item_h > 4096) item_h = 4096;
            } else if (child->tag == HTML_TAG_VIDEO) {
                item_w = (child->req_width > 0) ? child->req_width : ((child->style.width > 0) ? child->style.width : 480);
                item_h = (child->req_height > 0) ? child->req_height : ((child->style.height > 0) ? child->style.height : 270);
            } else if (child->tag == HTML_TAG_AUDIO) {
                item_w = (child->req_width > 0) ? child->req_width : ((child->style.width > 0) ? child->style.width : 320);
                item_h = (child->req_height > 0) ? child->req_height : ((child->style.height > 0) ? child->style.height : 36);
            } else {
                if (child->style.width > 0) {
                    item_w = child->style.width;
                } else if (child->text && child->text_len > 0) {
                    item_w = measure_text_width(child->text, child->text_len, get_font_scale(child->style.font_size)) + child->style.padding_left + child->style.padding_right;
                } else if (child->first_child && child->first_child->text && child->first_child->text_len > 0) {
                    item_w = measure_text_width(child->first_child->text, child->first_child->text_len, get_font_scale(child->first_child->style.font_size)) + child->style.padding_left + child->style.padding_right;
                } else {
                    item_w = 20;
                }
            }

            /* Wrap if needed */
            if (current_line.count > 0 && child_x + item_w > inner_x + inner_w) {
                apply_line_alignment(&current_line, node->style.text_align, inner_w);
                child_y += current_line.line_h;
                child_x = inner_x;
                current_line.count = 0;
                current_line.line_w = 0;
                current_line.line_h = 16 * scale;
            }

            child->bounds.x = child_x;
            child->bounds.y = child_y;
            child->bounds.w = item_w;
            child->bounds.h = item_h;

            if (current_line.count < 31) {
                current_line.items[current_line.count++] = child;
                current_line.line_w += item_w;
                if (item_h > current_line.line_h) current_line.line_h = item_h;
            }

            child_x += item_w;

            /* Recursively layout inline children if any */
            if (child->first_child) {
                layout_node_rec(child, child->bounds.x, child->bounds.y, item_w);
            }
        }
    }

    /* Flush final line */
    if (current_line.count > 0) {
        apply_line_alignment(&current_line, node->style.text_align, inner_w);
        child_y += current_line.line_h;
    }
    } /* end else (standard flow) */

    int content_h = child_y - inner_y;
    int box_h = content_h + pad_vert;
    if (node->style.height > 0) {
        box_h = node->style.height;
        if (node->style.box_sizing == 0) box_h += pad_vert;
    } else if (node->tag == HTML_TAG_IMG && node->req_height > 0) {
        box_h = node->req_height + pad_vert;
    } else if (node->tag == HTML_TAG_IFRAME) {
        box_h = (node->req_height > 0) ? (node->req_height + pad_vert) : 150;
    } else if (node->tag == HTML_TAG_VIDEO) {
        box_h = (node->req_height > 0) ? (node->req_height + pad_vert) : (node->style.height > 0 ? node->style.height : 270);
    } else if (node->tag == HTML_TAG_AUDIO) {
        box_h = (node->req_height > 0) ? (node->req_height + pad_vert) : (node->style.height > 0 ? node->style.height : 36);
    }
    if (box_h < 16 && (node->tag == HTML_TAG_H1 || node->tag == HTML_TAG_H2 || node->tag == HTML_TAG_H3 || node->tag == HTML_TAG_P)) {
        box_h = 16 * scale;
    }
    node->bounds.h = box_h;

    return my_y + box_h + node->style.margin_bottom;
}

void firstos_renderer_layout(firstos_renderer_context_t *ctx, uint32_t view_w, uint32_t view_h) {
    if (!ctx || !ctx->root_dom) return;
    ctx->viewport_w = view_w;
    ctx->viewport_h = view_h;

    int total_h = layout_node_rec(ctx->root_dom, 0, 0, (int)view_w);
    ctx->doc_height = (total_h > (int)view_h) ? total_h : (int)view_h;
}
