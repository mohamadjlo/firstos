#include "css_parser.h"
#include "css_tokenizer.h"
#include "bb_libc.h"

css_parsed_sheet_t *css_parsed_sheet_create(void) {
    css_parsed_sheet_t *s = (css_parsed_sheet_t *)bb_malloc(sizeof(css_parsed_sheet_t));
    if (s) {
        s->rules = NULL;
        s->rule_count = 0;
    }
    return s;
}

static void free_declaration_list(css_declaration_t *decl) {
    while (decl) {
        css_declaration_t *next = decl->next;
        if (decl->raw_val) bb_free(decl->raw_val);
        bb_free(decl);
        decl = next;
    }
}

static css_declaration_t *clone_declaration_list(const css_declaration_t *src) {
    css_declaration_t *head = NULL;
    css_declaration_t *tail = NULL;
    while (src) {
        css_declaration_t *d = (css_declaration_t *)bb_malloc(sizeof(css_declaration_t));
        if (!d) break;
        d->prop_id = src->prop_id;
        d->raw_val = bb_strdup(src->raw_val ? src->raw_val : "");
        d->next = NULL;
        if (!head) {
            head = d;
            tail = d;
        } else {
            tail->next = d;
            tail = d;
        }
        src = src->next;
    }
    return head;
}

void css_parsed_sheet_free(css_parsed_sheet_t *sheet) {
    if (!sheet) return;
    css_rule_t *rule = sheet->rules;
    while (rule) {
        css_rule_t *next = rule->next;
        if (rule->selector) css_selector_chain_free(rule->selector);
        free_declaration_list(rule->declarations);
        bb_free(rule);
        rule = next;
    }
    bb_free(sheet);
}

static css_property_id_t css_property_lookup(const char *name) {
    if (!name) return CSS_PROP_UNKNOWN;
    if (bb_strcasecmp(name, "display") == 0) return CSS_PROP_DISPLAY;
    if (bb_strcasecmp(name, "position") == 0) return CSS_PROP_POSITION;
    if (bb_strcasecmp(name, "top") == 0) return CSS_PROP_TOP;
    if (bb_strcasecmp(name, "right") == 0) return CSS_PROP_RIGHT;
    if (bb_strcasecmp(name, "bottom") == 0) return CSS_PROP_BOTTOM;
    if (bb_strcasecmp(name, "left") == 0) return CSS_PROP_LEFT;
    if (bb_strcasecmp(name, "width") == 0) return CSS_PROP_WIDTH;
    if (bb_strcasecmp(name, "height") == 0) return CSS_PROP_HEIGHT;
    if (bb_strcasecmp(name, "margin") == 0) return CSS_PROP_MARGIN;
    if (bb_strcasecmp(name, "margin-top") == 0) return CSS_PROP_MARGIN_TOP;
    if (bb_strcasecmp(name, "margin-right") == 0) return CSS_PROP_MARGIN_RIGHT;
    if (bb_strcasecmp(name, "margin-bottom") == 0) return CSS_PROP_MARGIN_BOTTOM;
    if (bb_strcasecmp(name, "margin-left") == 0) return CSS_PROP_MARGIN_LEFT;
    if (bb_strcasecmp(name, "padding") == 0) return CSS_PROP_PADDING;
    if (bb_strcasecmp(name, "padding-top") == 0) return CSS_PROP_PADDING_TOP;
    if (bb_strcasecmp(name, "padding-right") == 0) return CSS_PROP_PADDING_RIGHT;
    if (bb_strcasecmp(name, "padding-bottom") == 0) return CSS_PROP_PADDING_BOTTOM;
    if (bb_strcasecmp(name, "padding-left") == 0) return CSS_PROP_PADDING_LEFT;
    if (bb_strcasecmp(name, "border-width") == 0) return CSS_PROP_BORDER_WIDTH;
    if (bb_strcasecmp(name, "border-color") == 0) return CSS_PROP_BORDER_COLOR;
    if (bb_strcasecmp(name, "border-radius") == 0) return CSS_PROP_BORDER_RADIUS;
    if (bb_strcasecmp(name, "border") == 0) return CSS_PROP_BORDER;
    if (bb_strcasecmp(name, "color") == 0) return CSS_PROP_COLOR;
    if (bb_strcasecmp(name, "background-color") == 0 || bb_strcasecmp(name, "background") == 0) return CSS_PROP_BACKGROUND_COLOR;
    if (bb_strcasecmp(name, "font-size") == 0) return CSS_PROP_FONT_SIZE;
    if (bb_strcasecmp(name, "font-weight") == 0) return CSS_PROP_FONT_WEIGHT;
    if (bb_strcasecmp(name, "text-align") == 0) return CSS_PROP_TEXT_ALIGN;
    if (bb_strcasecmp(name, "line-height") == 0) return CSS_PROP_LINE_HEIGHT;
    if (bb_strcasecmp(name, "white-space") == 0) return CSS_PROP_WHITE_SPACE;
    if (bb_strcasecmp(name, "overflow") == 0) return CSS_PROP_OVERFLOW;
    if (bb_strcasecmp(name, "visibility") == 0) return CSS_PROP_VISIBILITY;
    if (bb_strcasecmp(name, "z-index") == 0) return CSS_PROP_Z_INDEX;
    return CSS_PROP_UNKNOWN;
}

static int parse_px(const char *val) {
    if (!val) return 0;
    while (*val == ' ' || *val == '\t') val++;
    return bb_atoi(val);
}

static void parse_quad_values(const char *val, int32_t *top, int32_t *right, int32_t *bottom, int32_t *left) {
    int32_t v[4];
    int count = 0;
    const char *p = val;

    while (*p && count < 4) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        v[count++] = bb_atoi(p);
        while (*p && *p != ' ' && *p != '\t') p++;
    }

    if (count == 1) {
        *top = *right = *bottom = *left = v[0];
    } else if (count == 2) {
        *top = *bottom = v[0];
        *right = *left = v[1];
    } else if (count == 3) {
        *top = v[0];
        *right = *left = v[1];
        *bottom = v[2];
    } else if (count == 4) {
        *top = v[0];
        *right = v[1];
        *bottom = v[2];
        *left = v[3];
    }
}

void css_apply_declaration(bb_style_t *style, css_property_id_t prop_id, const char *val) {
    if (!style || !val) return;
    while (*val == ' ' || *val == '\t') val++;

    switch (prop_id) {
        case CSS_PROP_DISPLAY:
            if (bb_strcasecmp(val, "block") == 0) style->display = BB_DISPLAY_BLOCK;
            else if (bb_strcasecmp(val, "inline") == 0) style->display = BB_DISPLAY_INLINE;
            else if (bb_strcasecmp(val, "inline-block") == 0) style->display = BB_DISPLAY_INLINE_BLOCK;
            else if (bb_strcasecmp(val, "none") == 0) style->display = BB_DISPLAY_NONE;
            break;

        case CSS_PROP_POSITION:
            if (bb_strcasecmp(val, "static") == 0) style->position = BB_POSITION_STATIC;
            else if (bb_strcasecmp(val, "relative") == 0) style->position = BB_POSITION_RELATIVE;
            else if (bb_strcasecmp(val, "absolute") == 0) style->position = BB_POSITION_ABSOLUTE;
            else if (bb_strcasecmp(val, "fixed") == 0) style->position = BB_POSITION_FIXED;
            break;

        case CSS_PROP_TOP:
            style->top = parse_px(val);
            style->has_top = true;
            break;
        case CSS_PROP_RIGHT:
            style->right = parse_px(val);
            style->has_right = true;
            break;
        case CSS_PROP_BOTTOM:
            style->bottom = parse_px(val);
            style->has_bottom = true;
            break;
        case CSS_PROP_LEFT:
            style->left = parse_px(val);
            style->has_left = true;
            break;

        case CSS_PROP_WIDTH:
            if (bb_strcasecmp(val, "auto") == 0) style->width = -1;
            else style->width = parse_px(val);
            break;
        case CSS_PROP_HEIGHT:
            if (bb_strcasecmp(val, "auto") == 0) style->height = -1;
            else style->height = parse_px(val);
            break;

        case CSS_PROP_MARGIN:
            parse_quad_values(val, &style->margin_top, &style->margin_right, &style->margin_bottom, &style->margin_left);
            break;
        case CSS_PROP_MARGIN_TOP:
            style->margin_top = parse_px(val);
            break;
        case CSS_PROP_MARGIN_RIGHT:
            style->margin_right = parse_px(val);
            break;
        case CSS_PROP_MARGIN_BOTTOM:
            style->margin_bottom = parse_px(val);
            break;
        case CSS_PROP_MARGIN_LEFT:
            style->margin_left = parse_px(val);
            break;

        case CSS_PROP_PADDING:
            parse_quad_values(val, &style->padding_top, &style->padding_right, &style->padding_bottom, &style->padding_left);
            break;
        case CSS_PROP_PADDING_TOP:
            style->padding_top = parse_px(val);
            break;
        case CSS_PROP_PADDING_RIGHT:
            style->padding_right = parse_px(val);
            break;
        case CSS_PROP_PADDING_BOTTOM:
            style->padding_bottom = parse_px(val);
            break;
        case CSS_PROP_PADDING_LEFT:
            style->padding_left = parse_px(val);
            break;

        case CSS_PROP_BORDER_WIDTH:
            style->border_width = parse_px(val);
            break;
        case CSS_PROP_BORDER_COLOR:
            style->border_color = bb_css_parse_color(val);
            break;
        case CSS_PROP_BORDER_RADIUS:
            style->border_radius = parse_px(val);
            break;
        case CSS_PROP_BORDER: {
            style->border_width = bb_atoi(val);
            const char *p = val;
            while (*p && *p != ' ' && *p != '\t') p++;
            while (*p == ' ' || *p == '\t') p++;
            while (*p && *p != ' ' && *p != '\t') p++;
            while (*p == ' ' || *p == '\t') p++;
            if (*p) style->border_color = bb_css_parse_color(p);
            break;
        }

        case CSS_PROP_COLOR:
            style->color = bb_css_parse_color(val);
            break;
        case CSS_PROP_BACKGROUND_COLOR:
            style->background_color = bb_css_parse_color(val);
            break;

        case CSS_PROP_FONT_SIZE:
            style->font_size = parse_px(val);
            break;
        case CSS_PROP_FONT_WEIGHT:
            if (bb_strcasecmp(val, "bold") == 0) style->font_weight = 700;
            else if (bb_strcasecmp(val, "normal") == 0) style->font_weight = 400;
            else style->font_weight = bb_atoi(val);
            break;

        case CSS_PROP_TEXT_ALIGN:
            if (bb_strcasecmp(val, "center") == 0) style->text_align = BB_TEXT_ALIGN_CENTER;
            else if (bb_strcasecmp(val, "right") == 0) style->text_align = BB_TEXT_ALIGN_RIGHT;
            else if (bb_strcasecmp(val, "left") == 0) style->text_align = BB_TEXT_ALIGN_LEFT;
            break;

        case CSS_PROP_LINE_HEIGHT:
            if (bb_strcasecmp(val, "normal") == 0) style->line_height = 0;
            else style->line_height = parse_px(val);
            break;

        case CSS_PROP_WHITE_SPACE:
            if (bb_strcasecmp(val, "nowrap") == 0) style->white_space = BB_WHITE_SPACE_NOWRAP;
            else if (bb_strcasecmp(val, "pre") == 0) style->white_space = BB_WHITE_SPACE_PRE;
            else style->white_space = BB_WHITE_SPACE_NORMAL;
            break;

        case CSS_PROP_OVERFLOW:
            if (bb_strcasecmp(val, "hidden") == 0) style->overflow = BB_OVERFLOW_HIDDEN;
            else if (bb_strcasecmp(val, "scroll") == 0) style->overflow = BB_OVERFLOW_SCROLL;
            else if (bb_strcasecmp(val, "auto") == 0) style->overflow = BB_OVERFLOW_AUTO;
            else style->overflow = BB_OVERFLOW_VISIBLE;
            break;

        case CSS_PROP_VISIBILITY:
            if (bb_strcasecmp(val, "hidden") == 0) style->visibility = BB_VISIBILITY_HIDDEN;
            else style->visibility = BB_VISIBILITY_VISIBLE;
            break;

        case CSS_PROP_Z_INDEX:
            if (bb_strcasecmp(val, "auto") == 0) style->z_index = 0;
            else style->z_index = bb_atoi(val);
            break;

        default:
            /* Unknown property safely ignored */
            break;
    }
}

static css_declaration_t *parse_declarations_block(const char *text, size_t len) {
    if (!text || len == 0) return NULL;
    css_declaration_t *head = NULL;
    css_declaration_t *tail = NULL;

    const char *p = text;
    const char *end = text + len;

    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (p >= end) break;

        const char *prop_start = p;
        while (p < end && *p != ':' && *p != ';' && *p != '}') p++;
        if (p >= end || *p != ':') {
            /* Malformed declaration: skip to next ';' or end */
            while (p < end && *p != ';') p++;
            if (p < end && *p == ';') p++;
            continue;
        }

        size_t prop_len = (size_t)(p - prop_start);
        while (prop_len > 0 && (prop_start[prop_len - 1] == ' ' || prop_start[prop_len - 1] == '\t')) {
            prop_len--;
        }

        char prop_buf[64];
        if (prop_len >= sizeof(prop_buf)) prop_len = sizeof(prop_buf) - 1;
        bb_memcpy(prop_buf, prop_start, prop_len);
        prop_buf[prop_len] = '\0';

        p++; /* skip ':' */
        while (p < end && (*p == ' ' || *p == '\t')) p++;

        const char *val_start = p;
        while (p < end && *p != ';' && *p != '}') p++;
        size_t val_len = (size_t)(p - val_start);
        while (val_len > 0 && (val_start[val_len - 1] == ' ' || val_start[val_len - 1] == '\t' ||
                               val_start[val_len - 1] == '\r' || val_start[val_len - 1] == '\n')) {
            val_len--;
        }

        css_property_id_t pid = css_property_lookup(prop_buf);
        if (pid != CSS_PROP_UNKNOWN) {
            css_declaration_t *d = (css_declaration_t *)bb_malloc(sizeof(css_declaration_t));
            if (d) {
                d->prop_id = pid;
                d->raw_val = bb_strndup(val_start, val_len);
                d->next = NULL;
                if (!head) {
                    head = d;
                    tail = d;
                } else {
                    tail->next = d;
                    tail = d;
                }
            }
        }

        if (p < end && *p == ';') p++;
    }

    return head;
}

void css_parse_declarations_into_style(bb_style_t *style, const char *decl_text) {
    if (!style || !decl_text) return;
    css_declaration_t *decls = parse_declarations_block(decl_text, bb_strlen(decl_text));
    css_declaration_t *curr = decls;
    while (curr) {
        css_apply_declaration(style, curr->prop_id, curr->raw_val);
        curr = curr->next;
    }
    free_declaration_list(decls);
}

/* Parse a single selector string (e.g. "div.card #header a") into a css_selector_chain_t */
static css_selector_chain_t *parse_single_selector(const char *src, size_t len) {
    if (!src || len == 0) return NULL;

    css_tokenizer_t tok;
    css_tok_init(&tok, src, len);
    css_token_t t;

    css_selector_step_t *curr_step = NULL;
    bool in_descendant = false;

    while (css_tok_next(&tok, &t)) {
        if (t.type == CSS_TOK_WHITESPACE) {
            if (curr_step != NULL) {
                in_descendant = true;
            }
            continue;
        }

        if (t.type == CSS_TOK_IDENT || t.type == CSS_TOK_ASTERISK ||
            t.type == CSS_TOK_DOT   || t.type == CSS_TOK_HASH) {
            
            if (in_descendant || curr_step == NULL) {
                /* Start a new step */
                css_selector_step_t *step = css_selector_step_create();
                if (!step) break;

                if (curr_step) {
                    step->ancestor = curr_step;
                }
                curr_step = step;
                in_descendant = false;
            }

            if (t.type == CSS_TOK_IDENT || t.type == CSS_TOK_ASTERISK) {
                if (!curr_step->tag) {
                    curr_step->tag = bb_strndup(t.text, t.len);
                }
            } else if (t.type == CSS_TOK_DOT) {
                /* Class name follows */
                if (css_tok_next(&tok, &t) && (t.type == CSS_TOK_IDENT)) {
                    if (!curr_step->class_name) {
                        curr_step->class_name = bb_strndup(t.text, t.len);
                    }
                }
            } else if (t.type == CSS_TOK_HASH) {
                if (!curr_step->id) {
                    curr_step->id = bb_strndup(t.text, t.len);
                }
            }
        }
    }

    if (!curr_step) return NULL;

    css_selector_chain_t *chain = css_selector_chain_create();
    if (!chain) {
        css_selector_step_free(curr_step);
        return NULL;
    }

    chain->key_selector = curr_step;
    chain->specificity = css_selector_calc_specificity(chain);
    return chain;
}

void css_parse_stylesheet(css_parsed_sheet_t *sheet, const char *css_text, size_t len) {
    if (!sheet || !css_text || len == 0) return;

    const char *p = css_text;
    const char *end = css_text + len;

    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (p >= end) break;

        /* Skip comments */
        if (p + 1 < end && p[0] == '/' && p[1] == '*') {
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++;
            if (p + 1 < end) p += 2;
            else p = end;
            continue;
        }

        /* Parse selector until '{' */
        const char *sel_start = p;
        while (p < end && *p != '{') p++;
        if (p >= end || *p != '{') break;

        size_t sel_group_len = (size_t)(p - sel_start);
        p++; /* skip '{' */

        /* Parse declaration block until '}' */
        const char *block_start = p;
        while (p < end && *p != '}') p++;
        size_t block_len = (size_t)(p - block_start);
        if (p < end && *p == '}') p++;

        css_declaration_t *decls = parse_declarations_block(block_start, block_len);
        if (!decls) continue;

        /* Parse comma-separated selector group */
        const char *sp = sel_start;
        const char *send = sel_start + sel_group_len;

        while (sp < send) {
            while (sp < send && (*sp == ' ' || *sp == '\t' || *sp == '\r' || *sp == '\n')) sp++;
            if (sp >= send) break;

            const char *item_start = sp;
            while (sp < send && *sp != ',') sp++;
            size_t item_len = (size_t)(sp - item_start);
            while (item_len > 0 && (item_start[item_len - 1] == ' ' || item_start[item_len - 1] == '\t' ||
                                    item_start[item_len - 1] == '\r' || item_start[item_len - 1] == '\n')) {
                item_len--;
            }

            if (item_len > 0) {
                css_selector_chain_t *chain = parse_single_selector(item_start, item_len);
                if (chain) {
                    css_rule_t *rule = (css_rule_t *)bb_malloc(sizeof(css_rule_t));
                    if (rule) {
                        rule->selector = chain;
                        rule->declarations = clone_declaration_list(decls);
                        rule->source_order = sheet->rule_count++;
                        rule->next = NULL;

                        /* Append to sheet rules */
                        if (!sheet->rules) {
                            sheet->rules = rule;
                        } else {
                            css_rule_t *tail = sheet->rules;
                            while (tail->next) tail = tail->next;
                            tail->next = rule;
                        }
                    } else {
                        css_selector_chain_free(chain);
                    }
                }
            }

            if (sp < send && *sp == ',') sp++;
        }

        free_declaration_list(decls);
    }
}
