#include "include/firstos_renderer.h"


static char css_to_lower_char(char c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int css_is_space_char(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static uint32_t parse_color(const char *v, int len) {
    if (!v || len <= 0) return 0;
    while (len > 0 && css_is_space_char(*v)) { v++; len--; }
    while (len > 0 && css_is_space_char(v[len-1])) len--;

    if (v[0] == '#') {
        v++; len--;
        uint32_t val = 0;
        for (int i = 0; i < len && i < 6; i++) {
            char c = v[i];
            int digit = 0;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else break;
            val = (val << 4) | (uint32_t)digit;
        }
        if (len == 3) {
            uint32_t r = (val >> 8) & 0xF;
            uint32_t g = (val >> 4) & 0xF;
            uint32_t b = val & 0xF;
            return ((r | (r << 4)) << 16) | ((g | (g << 4)) << 8) | (b | (b << 4));
        }
        return val;
    }

    if (len == 5 && strncmp(v, "white", 5) == 0) return 0xFFFFFF;
    if (len == 5 && strncmp(v, "black", 5) == 0) return 0x000000;
    if (len == 3 && strncmp(v, "red", 3) == 0) return 0xFF3B30;
    if (len == 5 && strncmp(v, "green", 5) == 0) return 0x30D158;
    if (len == 4 && strncmp(v, "blue", 4) == 0) return 0x0A84FF;
    if (len == 4 && strncmp(v, "gray", 4) == 0) return 0x8E8E93;
    if (len == 6 && strncmp(v, "yellow", 6) == 0) return 0xFFD60A;
    if (len == 4 && strncmp(v, "cyan", 4) == 0) return 0x64D2FF;
    if (len == 6 && strncmp(v, "orange", 6) == 0) return 0xFF9F0A;

    return 0;
}

static int parse_px(const char *v, int len) {
    if (!v || len <= 0) return 0;
    while (len > 0 && css_is_space_char(*v)) { v++; len--; }
    int sign = 1;
    if (*v == '-') { sign = -1; v++; len--; }
    int val = 0;
    while (len > 0 && *v >= '0' && *v <= '9') {
        val = val * 10 + (*v - '0');
        v++; len--;
    }
    return val * sign;
}

static const char *lookup_css_var(const html_node_t *node, const char *name, int nlen) {
    const html_node_t *curr = node;
    while (curr) {
        for (int i = 0; i < curr->var_table.count; i++) {
            if ((int)strlen(curr->var_table.entries[i].name) == nlen &&
                strncmp(curr->var_table.entries[i].name, name, nlen) == 0) {
                return curr->var_table.entries[i].value;
            }
        }
        curr = curr->parent;
    }
    return NULL;
}

static void resolve_var_in_val(const html_node_t *node, const char *val, int vlen, char *out, int max_out) {
    if (!val || vlen <= 0 || max_out <= 0) {
        if (max_out > 0) out[0] = '\0';
        return;
    }

    const char *vpos = NULL;
    for (int i = 0; i + 3 < vlen; i++) {
        if (val[i] == 'v' && val[i+1] == 'a' && val[i+2] == 'r' && val[i+3] == '(') {
            vpos = val + i;
            break;
        }
    }

    if (!vpos) {
        int l = (vlen < max_out - 1) ? vlen : max_out - 1;
        memcpy(out, val, l);
        out[l] = '\0';
        return;
    }

    const char *start = vpos + 4;
    while (start < val + vlen && css_is_space_char(*start)) start++;
    const char *end = start;
    const char *fallback = NULL;
    int fallback_len = 0;

    while (end < val + vlen && *end != ')' && *end != ',') end++;
    int var_name_len = (int)(end - start);
    while (var_name_len > 0 && css_is_space_char(start[var_name_len - 1])) var_name_len--;

    if (end < val + vlen && *end == ',') {
        const char *fb_start = end + 1;
        while (fb_start < val + vlen && css_is_space_char(*fb_start)) fb_start++;
        const char *fb_end = fb_start;
        while (fb_end < val + vlen && *fb_end != ')') fb_end++;
        fallback = fb_start;
        fallback_len = (int)(fb_end - fb_start);
        while (fallback_len > 0 && css_is_space_char(fallback[fallback_len - 1])) fallback_len--;
    }

    const char *resolved = lookup_css_var(node, start, var_name_len);
    if (resolved) {
        int rlen = (int)strlen(resolved);
        if (rlen >= max_out) rlen = max_out - 1;
        memcpy(out, resolved, rlen);
        out[rlen] = '\0';
    } else if (fallback && fallback_len > 0) {
        int flen = (fallback_len < max_out - 1) ? fallback_len : max_out - 1;
        memcpy(out, fallback, flen);
        out[flen] = '\0';
    } else {
        int l = (vlen < max_out - 1) ? vlen : max_out - 1;
        memcpy(out, val, l);
        out[l] = '\0';
    }
}

static void apply_declaration(html_node_t *node, const char *prop, int plen, const char *val, int vlen) {
    while (plen > 0 && css_is_space_char(*prop)) { prop++; plen--; }
    while (plen > 0 && css_is_space_char(prop[plen-1])) plen--;
    while (vlen > 0 && css_is_space_char(*val)) { val++; vlen--; }
    while (vlen > 0 && css_is_space_char(val[vlen-1])) vlen--;

    if (plen <= 0 || vlen <= 0) return;

    /* Phase 6C: Custom CSS Properties (--var-name) */
    if (plen > 2 && prop[0] == '-' && prop[1] == '-') {
        if (node->var_table.count < FIRSTOS_MAX_CSS_VARS_PER_NODE) {
            int nlen = (plen < 31) ? plen : 31;
            memcpy(node->var_table.entries[node->var_table.count].name, prop, nlen);
            node->var_table.entries[node->var_table.count].name[nlen] = '\0';

            int val_len = (vlen < 47) ? vlen : 47;
            memcpy(node->var_table.entries[node->var_table.count].value, val, val_len);
            node->var_table.entries[node->var_table.count].value[val_len] = '\0';
            node->var_table.count++;
        }
        return;
    }

    /* Phase 6C: Variable resolution (var(--...)) */
    char resolved_val[64];
    resolve_var_in_val(node, val, vlen, resolved_val, sizeof(resolved_val));
    val = resolved_val;
    vlen = (int)strlen(resolved_val);

    if (plen == 5 && strncmp(prop, "color", 5) == 0) {
        node->style.color = parse_color(val, vlen);
        node->style.has_explicit_color = 1;
    } else if ((plen == 16 && strncmp(prop, "background-color", 16) == 0) ||
               (plen == 10 && strncmp(prop, "background", 10) == 0)) {
        node->style.bg_color = parse_color(val, vlen);
        node->style.has_bg = 1;
    } else if (plen == 5 && strncmp(prop, "width", 5) == 0) {
        node->style.width = parse_px(val, vlen);
    } else if (plen == 6 && strncmp(prop, "height", 6) == 0) {
        node->style.height = parse_px(val, vlen);
    } else if (plen == 6 && strncmp(prop, "margin", 6) == 0) {
        if (vlen >= 4 && strncmp(val, "auto", 4) == 0) {
            node->style.margin_left_auto = 1;
            node->style.margin_right_auto = 1;
        } else {
            int px = parse_px(val, vlen);
            node->style.margin_top = px;
            node->style.margin_bottom = px;
            node->style.margin_left = px;
            node->style.margin_right = px;
        }
    } else if (plen == 10 && strncmp(prop, "margin-top", 10) == 0) {
        node->style.margin_top = parse_px(val, vlen);
    } else if (plen == 13 && strncmp(prop, "margin-bottom", 13) == 0) {
        node->style.margin_bottom = parse_px(val, vlen);
    } else if (plen == 11 && strncmp(prop, "margin-left", 11) == 0) {
        if (vlen >= 4 && strncmp(val, "auto", 4) == 0) node->style.margin_left_auto = 1;
        else node->style.margin_left = parse_px(val, vlen);
    } else if (plen == 12 && strncmp(prop, "margin-right", 12) == 0) {
        if (vlen >= 4 && strncmp(val, "auto", 4) == 0) node->style.margin_right_auto = 1;
        else node->style.margin_right = parse_px(val, vlen);
    } else if (plen == 7 && strncmp(prop, "padding", 7) == 0) {
        int px = parse_px(val, vlen);
        node->style.padding_top = px;
        node->style.padding_bottom = px;
        node->style.padding_left = px;
        node->style.padding_right = px;
    } else if (plen == 11 && strncmp(prop, "padding-top", 11) == 0) {
        node->style.padding_top = parse_px(val, vlen);
    } else if (plen == 14 && strncmp(prop, "padding-bottom", 14) == 0) {
        node->style.padding_bottom = parse_px(val, vlen);
    } else if (plen == 12 && strncmp(prop, "padding-left", 12) == 0) {
        node->style.padding_left = parse_px(val, vlen);
    } else if (plen == 13 && strncmp(prop, "padding-right", 13) == 0) {
        node->style.padding_right = parse_px(val, vlen);
    } else if (plen == 7 && strncmp(prop, "display", 7) == 0) {
        if (vlen == 4 && strncmp(val, "none", 4) == 0) {
            node->style.display_none = 1;
            node->style.display_mode = CSS_DISPLAY_NONE;
        } else if (vlen == 4 && strncmp(val, "flex", 4) == 0) {
            node->style.display_mode = CSS_DISPLAY_FLEX;
            node->style.is_block = 1;
            node->style.is_inline_block = 0;
            node->style.display_none = 0;
        } else if (vlen == 4 && strncmp(val, "grid", 4) == 0) {
            node->style.display_mode = CSS_DISPLAY_GRID;
            node->style.is_block = 1;
            node->style.is_inline_block = 0;
            node->style.display_none = 0;
        } else if (vlen == 5 && strncmp(val, "block", 5) == 0) {
            node->style.display_mode = CSS_DISPLAY_BLOCK;
            node->style.is_block = 1;
            node->style.is_inline_block = 0;
            node->style.display_none = 0;
        } else if (vlen == 6 && strncmp(val, "inline", 6) == 0) {
            node->style.display_mode = CSS_DISPLAY_INLINE;
            node->style.is_block = 0;
            node->style.is_inline_block = 0;
            node->style.display_none = 0;
        } else if (vlen == 12 && strncmp(val, "inline-block", 12) == 0) {
            node->style.display_mode = CSS_DISPLAY_INLINE_BLOCK;
            node->style.is_block = 0;
            node->style.is_inline_block = 1;
            node->style.display_none = 0;
        }
    } else if (plen == 14 && strncmp(prop, "flex-direction", 14) == 0) {
        if (vlen >= 6 && strncmp(val, "column", 6) == 0) {
            node->style.flex_direction = CSS_FLEX_DIR_COLUMN;
        } else if (vlen >= 3 && strncmp(val, "row", 3) == 0) {
            node->style.flex_direction = CSS_FLEX_DIR_ROW;
        }
    } else if (plen == 9 && strncmp(prop, "flex-wrap", 9) == 0) {
        if (vlen >= 4 && strncmp(val, "wrap", 4) == 0) {
            node->style.flex_wrap = CSS_FLEX_WRAP;
        } else if (vlen >= 6 && strncmp(val, "nowrap", 6) == 0) {
            node->style.flex_wrap = CSS_FLEX_NOWRAP;
        }
    } else if (plen == 15 && strncmp(prop, "justify-content", 15) == 0) {
        if (vlen >= 6 && strncmp(val, "center", 6) == 0) {
            node->style.justify_content = CSS_JUSTIFY_CENTER;
        } else if (vlen >= 8 && strncmp(val, "flex-end", 8) == 0) {
            node->style.justify_content = CSS_JUSTIFY_END;
        } else if (vlen >= 13 && strncmp(val, "space-between", 13) == 0) {
            node->style.justify_content = CSS_JUSTIFY_SPACE_BETWEEN;
        } else if (vlen >= 12 && strncmp(val, "space-around", 12) == 0) {
            node->style.justify_content = CSS_JUSTIFY_SPACE_AROUND;
        } else {
            node->style.justify_content = CSS_JUSTIFY_START;
        }
    } else if (plen == 11 && strncmp(prop, "align-items", 11) == 0) {
        if (vlen >= 6 && strncmp(val, "center", 6) == 0) {
            node->style.align_items = CSS_ALIGN_CENTER;
        } else if (vlen >= 8 && strncmp(val, "flex-end", 8) == 0) {
            node->style.align_items = CSS_ALIGN_END;
        } else if (vlen >= 7 && strncmp(val, "stretch", 7) == 0) {
            node->style.align_items = CSS_ALIGN_STRETCH;
        } else {
            node->style.align_items = CSS_ALIGN_START;
        }
    } else if (plen == 3 && strncmp(prop, "gap", 3) == 0) {
        int px = parse_px(val, vlen);
        node->style.gap_row = px;
        node->style.gap_col = px;
    } else if (plen == 7 && strncmp(prop, "row-gap", 7) == 0) {
        node->style.gap_row = parse_px(val, vlen);
    } else if ((plen == 10 && strncmp(prop, "column-gap", 10) == 0) ||
               (plen == 8 && strncmp(prop, "grid-gap", 8) == 0)) {
        node->style.gap_col = parse_px(val, vlen);
    } else if (plen == 21 && strncmp(prop, "grid-template-columns", 21) == 0) {
        /* Parse repeat(N, 1fr) or count tokens */
        if (vlen >= 7 && strncmp(val, "repeat(", 7) == 0) {
            int cnt = parse_px(val + 7, vlen - 7);
            if (cnt < 1) cnt = 1;
            if (cnt > 8) cnt = 8;
            node->style.grid_columns = (uint8_t)cnt;
        } else {
            /* Count column specifications separated by space */
            int cols = 0;
            int idx = 0;
            while (idx < vlen) {
                while (idx < vlen && css_is_space_char(val[idx])) idx++;
                if (idx >= vlen) break;
                cols++;
                while (idx < vlen && !css_is_space_char(val[idx])) idx++;
            }
            if (cols < 1) cols = 1;
            if (cols > 8) cols = 8;
            node->style.grid_columns = (uint8_t)cols;
        }
    } else if (plen == 9 && strncmp(prop, "flex-grow", 9) == 0) {
        node->style.flex_grow = (uint8_t)parse_px(val, vlen);
    } else if (plen == 9 && strncmp(prop, "font-size", 9) == 0) {
        int px = parse_px(val, vlen);
        if (px >= 22) node->style.font_size = 3;      /* 24px */
        else if (px >= 18) node->style.font_size = 2; /* 20px */
        else node->style.font_size = 1;               /* 14px */
    } else if (plen == 10 && strncmp(prop, "text-align", 10) == 0) {
        if (vlen == 6 && strncmp(val, "center", 6) == 0) {
            node->style.text_align = 1;
            node->style.has_explicit_align = 1;
        } else if (vlen == 5 && strncmp(val, "right", 5) == 0) {
            node->style.text_align = 2;
            node->style.has_explicit_align = 1;
        } else {
            node->style.text_align = 0;
            node->style.has_explicit_align = 1;
        }
    } else if (plen == 10 && strncmp(prop, "box-sizing", 10) == 0) {
        if (vlen == 10 && strncmp(val, "border-box", 10) == 0) {
            node->style.box_sizing = 1;
        }
    } else if (plen == 10 && strncmp(prop, "visibility", 10) == 0) {
        if (vlen == 6 && strncmp(val, "hidden", 6) == 0) {
            node->style.visibility_hidden = 1;
        }
    }
}

static void parse_css_declarations(html_node_t *node, const char *decls, int len) {
    if (!node || !decls || len <= 0) return;
    int p = 0;
    while (p < len) {
        while (p < len && (css_is_space_char(decls[p]) || decls[p] == ';')) p++;
        if (p >= len) break;

        int pstart = p;
        while (p < len && decls[p] != ':' && decls[p] != ';') p++;
        if (p >= len || decls[p] != ':') break;
        int plen = p - pstart;
        p++; /* skip ':' */

        int vstart = p;
        while (p < len && decls[p] != ';') p++;
        int vlen = p - vstart;
        if (p < len && decls[p] == ';') p++;

        apply_declaration(node, decls + pstart, plen, decls + vstart, vlen);
    }
}

static int node_matches_simple_selector(html_node_t *node, const char *sel, int len) {
    if (!node || !sel || len <= 0) return 0;
    while (len > 0 && css_is_space_char(*sel)) { sel++; len--; }
    while (len > 0 && css_is_space_char(sel[len-1])) len--;

    if (sel[0] == '.') {
        /* Class match */
        if (!node->class_name) return 0;
        const char *cls = sel + 1;
        int clen = len - 1;
        const char *ncls = node->class_name;
        int nlen = strlen(ncls);
        int p = 0;
        while (p < nlen) {
            while (p < nlen && css_is_space_char(ncls[p])) p++;
            int start = p;
            while (p < nlen && !css_is_space_char(ncls[p])) p++;
            if (p - start == clen && strncmp(ncls + start, cls, clen) == 0) return 1;
        }
        return 0;
    }

    if (sel[0] == '#') {
        /* ID match */
        if (!node->id) return 0;
        const char *nid = sel + 1;
        int idlen = len - 1;
        return ((int)strlen(node->id) == idlen && strncmp(node->id, nid, idlen) == 0);
    }

    /* Pseudo-class & Root match */
    if ((len == 5 && strncmp(sel, ":root", 5) == 0) ||
        (len == 4 && strncmp(sel, "html", 4) == 0)) {
        return (node->tag == HTML_TAG_HTML);
    }

    /* Tag match */
    char buf[16];
    int l = (len < 15) ? len : 15;
    for (int i = 0; i < l; i++) buf[i] = css_to_lower_char(sel[i]);
    buf[l] = '\0';

    if (l == 1 && buf[0] == 'p' && node->tag == HTML_TAG_P) return 1;
    if (l == 2 && buf[0] == 'h' && buf[1] == '1' && node->tag == HTML_TAG_H1) return 1;
    if (l == 2 && buf[0] == 'h' && buf[1] == '2' && node->tag == HTML_TAG_H2) return 1;
    if (l == 2 && buf[0] == 'h' && buf[1] == '3' && node->tag == HTML_TAG_H3) return 1;
    if (l == 3 && strncmp(buf, "div", 3) == 0 && node->tag == HTML_TAG_DIV) return 1;
    if (l == 4 && strncmp(buf, "body", 4) == 0 && node->tag == HTML_TAG_BODY) return 1;
    if (l == 1 && buf[0] == 'a' && node->tag == HTML_TAG_A) return 1;
    if (l == 4 && strncmp(buf, "span", 4) == 0 && node->tag == HTML_TAG_SPAN) return 1;
    if (l == 3 && strncmp(buf, "img", 3) == 0 && node->tag == HTML_TAG_IMG) return 1;
    if (l == 6 && strncmp(buf, "iframe", 6) == 0 && node->tag == HTML_TAG_IFRAME) return 1;
    if (l == 7 && strncmp(buf, "section", 7) == 0 && node->tag == HTML_TAG_SECTION) return 1;
    if (l == 7 && strncmp(buf, "article", 7) == 0 && node->tag == HTML_TAG_ARTICLE) return 1;
    if (l == 6 && strncmp(buf, "header", 6) == 0 && node->tag == HTML_TAG_HEADER) return 1;
    if (l == 6 && strncmp(buf, "footer", 6) == 0 && node->tag == HTML_TAG_FOOTER) return 1;
    if (l == 3 && strncmp(buf, "nav", 3) == 0 && node->tag == HTML_TAG_NAV) return 1;
    if (l == 6 && strncmp(buf, "button", 6) == 0 && node->tag == HTML_TAG_BUTTON) return 1;
    if (l == 4 && strncmp(buf, "form", 4) == 0 && node->tag == HTML_TAG_FORM) return 1;
    if (l == 5 && strncmp(buf, "input", 5) == 0 && node->tag == HTML_TAG_INPUT) return 1;
    if (l == 5 && strncmp(buf, "video", 5) == 0 && node->tag == HTML_TAG_VIDEO) return 1;
    if (l == 5 && strncmp(buf, "audio", 5) == 0 && node->tag == HTML_TAG_AUDIO) return 1;

    return 0;
}

static void apply_rule_to_tree_rec(html_node_t *node, const char *sel, int sel_len, const char *decls, int decl_len) {
    if (!node) return;
    if (node_matches_simple_selector(node, sel, sel_len)) {
        parse_css_declarations(node, decls, decl_len);
    }
    for (html_node_t *child = node->first_child; child; child = child->next_sibling) {
        apply_rule_to_tree_rec(child, sel, sel_len, decls, decl_len);
    }
}

static void parse_stylesheet_block(html_node_t *root, const char *css, int len) {
    if (!root || !css || len <= 0) return;
    int p = 0;
    while (p < len) {
        while (p < len && css_is_space_char(css[p])) p++;
        if (p >= len) break;

        /* Skip comment */
        if (p + 1 < len && css[p] == '/' && css[p+1] == '*') {
            p += 2;
            while (p + 1 < len && !(css[p] == '*' && css[p+1] == '/')) p++;
            if (p + 1 < len) p += 2;
            continue;
        }

        int sel_start = p;
        while (p < len && css[p] != '{') p++;
        if (p >= len) break;
        int sel_len = p - sel_start;
        p++; /* skip '{' */

        int decl_start = p;
        while (p < len && css[p] != '}') p++;
        int decl_len = p - decl_start;
        if (p < len) p++; /* skip '}' */

        apply_rule_to_tree_rec(root, css + sel_start, sel_len, css + decl_start, decl_len);
    }
}

static void propagate_inherited_styles_rec(html_node_t *node, uint32_t parent_color, uint8_t parent_align) {
    if (!node) return;
    if (!node->style.has_explicit_color) {
        node->style.color = parent_color;
    }
    if (!node->style.has_explicit_align) {
        node->style.text_align = parent_align;
    }
    for (html_node_t *child = node->first_child; child; child = child->next_sibling) {
        propagate_inherited_styles_rec(child, node->style.color, node->style.text_align);
    }
}

static void find_and_apply_styles_rec(html_node_t *root, html_node_t *curr) {
    if (!curr) return;
    if (curr->tag == HTML_TAG_STYLE && curr->text && curr->text_len > 0) {
        parse_stylesheet_block(root, curr->text, curr->text_len);
    }
    for (html_node_t *c = curr->first_child; c; c = c->next_sibling) {
        find_and_apply_styles_rec(root, c);
    }
}

void firstos_renderer_apply_styles(firstos_renderer_context_t *ctx) {
    if (!ctx || !ctx->root_dom) return;

    /* Scan for <style> tags anywhere in DOM and parse them */
    find_and_apply_styles_rec(ctx->root_dom, ctx->root_dom);

    /* Cascade inheritance from root downwards */
    propagate_inherited_styles_rec(ctx->root_dom, 0xDFDFE5, 0);
}

int firstos_renderer_add_stylesheet(firstos_renderer_context_t *ctx, const char *css_text, int len) {
    if (!ctx || !ctx->root_dom || !css_text || len <= 0) return -1;
    parse_stylesheet_block(ctx->root_dom, css_text, len);
    propagate_inherited_styles_rec(ctx->root_dom, 0xDFDFE5, 0);
    return 0;
}
