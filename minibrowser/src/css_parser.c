#define _POSIX_C_SOURCE 200809L
#include "css_parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char *trim_whitespace(char *str) {
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (!*str) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = 0;
    return str;
}

static RGBA parse_color(const char *val) {
    RGBA c = {0, 0, 0, 255};
    if (!val) return c;

    char v[128];
    strncpy(v, val, sizeof(v) - 1);
    v[sizeof(v) - 1] = 0;
    char *s = trim_whitespace(v);

    if (strcasecmp(s, "transparent") == 0) return (RGBA){0, 0, 0, 0};
    if (strcasecmp(s, "white") == 0) return (RGBA){255, 255, 255, 255};
    if (strcasecmp(s, "black") == 0) return (RGBA){0, 0, 0, 255};
    if (strcasecmp(s, "red") == 0) return (RGBA){230, 40, 40, 255};
    if (strcasecmp(s, "green") == 0) return (RGBA){40, 180, 50, 255};
    if (strcasecmp(s, "blue") == 0) return (RGBA){0, 110, 230, 255};
    if (strcasecmp(s, "gray") == 0 || strcasecmp(s, "grey") == 0) return (RGBA){128, 128, 135, 255};
    if (strcasecmp(s, "lightgray") == 0) return (RGBA){220, 220, 225, 255};
    if (strcasecmp(s, "darkgray") == 0) return (RGBA){50, 50, 55, 255};
    if (strcasecmp(s, "yellow") == 0) return (RGBA){255, 215, 0, 255};
    if (strcasecmp(s, "orange") == 0) return (RGBA){255, 140, 0, 255};
    if (strcasecmp(s, "cyan") == 0) return (RGBA){0, 200, 220, 255};
    if (strcasecmp(s, "purple") == 0) return (RGBA){130, 40, 180, 255};

    if (s[0] == '#') {
        size_t len = strlen(s + 1);
        if (len == 6) {
            unsigned int hex = 0;
            if (sscanf(s + 1, "%x", &hex) == 1) {
                c.r = (hex >> 16) & 0xFF;
                c.g = (hex >> 8) & 0xFF;
                c.b = hex & 0xFF;
                c.a = 255;
            }
        } else if (len == 3) {
            unsigned int hex = 0;
            if (sscanf(s + 1, "%x", &hex) == 1) {
                unsigned int r = (hex >> 8) & 0xF;
                unsigned int g = (hex >> 4) & 0xF;
                unsigned int b = hex & 0xF;
                c.r = (r << 4) | r;
                c.g = (g << 4) | g;
                c.b = (b << 4) | b;
                c.a = 255;
            }
        }
        return c;
    }

    if (strncasecmp(s, "rgb(", 4) == 0) {
        int r = 0, g = 0, b = 0;
        if (sscanf(s + 4, "%d,%d,%d", &r, &g, &b) >= 3 ||
            sscanf(s + 4, "%d %d %d", &r, &g, &b) >= 3) {
            c.r = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
            c.g = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
            c.b = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
            c.a = 255;
        }
        return c;
    }

    if (strncasecmp(s, "rgba(", 5) == 0) {
        int r = 0, g = 0, b = 0;
        float a = 1.0f;
        if (sscanf(s + 5, "%d,%d,%d,%f", &r, &g, &b, &a) >= 4) {
            c.r = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
            c.g = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
            c.b = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));
            c.a = (uint8_t)(a * 255.0f);
        }
        return c;
    }

    return c;
}

static float parse_px(const char *val) {
    if (!val) return 0.0f;
    float f = 0.0f;
    sscanf(val, "%f", &f);
    return f;
}

static Spacing parse_spacing(const char *val) {
    Spacing s = {0, 0, 0, 0};
    if (!val) return s;

    float vals[4];
    int count = 0;
    const char *p = val;
    while (*p && count < 4) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        vals[count++] = parse_px(p);
        while (*p && !isspace((unsigned char)*p)) p++;
    }

    if (count == 1) {
        s.top = s.right = s.bottom = s.left = vals[0];
    } else if (count == 2) {
        s.top = s.bottom = vals[0];
        s.right = s.left = vals[1];
    } else if (count == 3) {
        s.top = vals[0];
        s.right = s.left = vals[1];
        s.bottom = vals[2];
    } else if (count >= 4) {
        s.top = vals[0];
        s.right = vals[1];
        s.bottom = vals[2];
        s.left = vals[3];
    }
    return s;
}

static Selector *parse_selector_chain(const char *raw_sel) {
    Selector *head = NULL;
    Selector *tail = NULL;

    char buf[512];
    strncpy(buf, raw_sel, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    char *token = strtok(buf, ",");
    while (token) {
        char *s = trim_whitespace(token);
        if (*s) {
            Selector *sel = (Selector *)calloc(1, sizeof(Selector));
            if (s[0] == '.') {
                sel->type = SELECTOR_CLASS;
                sel->value = strdup(s + 1);
            } else if (s[0] == '#') {
                sel->type = SELECTOR_ID;
                sel->value = strdup(s + 1);
            } else if (s[0] == '*' && strlen(s) == 1) {
                sel->type = SELECTOR_UNIVERSAL;
                sel->value = strdup("*");
            } else {
                sel->type = SELECTOR_TAG;
                sel->value = strdup(s);
            }

            if (!head) head = sel;
            else tail->next = sel;
            tail = sel;
        }
        token = strtok(NULL, ",");
    }
    return head;
}

static int calculate_specificity(const Selector *selectors) {
    int spec = 0;
    for (const Selector *s = selectors; s; s = s->next) {
        if (s->type == SELECTOR_ID) spec += 100;
        else if (s->type == SELECTOR_CLASS) spec += 10;
        else if (s->type == SELECTOR_TAG) spec += 1;
    }
    return spec;
}

Stylesheet *css_parse(const char *css_source) {
    if (!css_source) return NULL;

    Stylesheet *sheet = (Stylesheet *)calloc(1, sizeof(Stylesheet));
    if (!sheet) return NULL;

    CSSRule *tail_rule = NULL;

    const char *p = css_source;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Skip comments */
        if (*p == '/' && *(p + 1) == '*') {
            p += 2;
            while (*p && !(*p == '*' && *(p + 1) == '/')) p++;
            if (*p) p += 2;
            continue;
        }

        /* Read selector portion up to '{' */
        const char *sel_start = p;
        while (*p && *p != '{') p++;
        if (!*p) break;

        size_t sel_len = (size_t)(p - sel_start);
        char sel_buf[512];
        if (sel_len >= sizeof(sel_buf)) sel_len = sizeof(sel_buf) - 1;
        strncpy(sel_buf, sel_start, sel_len);
        sel_buf[sel_len] = 0;
        p++; /* Skip '{' */

        Selector *selectors = parse_selector_chain(sel_buf);
        if (!selectors) {
            /* Skip to '}' */
            while (*p && *p != '}') p++;
            if (*p) p++;
            continue;
        }

        /* Read declarations up to '}' */
        Declaration *head_decl = NULL;
        Declaration *tail_decl = NULL;

        while (*p && *p != '}') {
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == '}' || !*p) break;

            const char *prop_start = p;
            while (*p && *p != ':' && *p != '}') p++;
            if (*p != ':') {
                if (*p) p++;
                continue;
            }
            size_t prop_len = (size_t)(p - prop_start);
            char prop_buf[128];
            if (prop_len >= sizeof(prop_buf)) prop_len = sizeof(prop_buf) - 1;
            strncpy(prop_buf, prop_start, prop_len);
            prop_buf[prop_len] = 0;
            p++; /* Skip ':' */

            const char *val_start = p;
            while (*p && *p != ';' && *p != '}') p++;
            size_t val_len = (size_t)(p - val_start);
            char val_buf[256];
            if (val_len >= sizeof(val_buf)) val_len = sizeof(val_buf) - 1;
            strncpy(val_buf, val_start, val_len);
            val_buf[val_len] = 0;
            if (*p == ';') p++;

            char *clean_prop = trim_whitespace(prop_buf);
            char *clean_val = trim_whitespace(val_buf);

            if (*clean_prop && *clean_val) {
                Declaration *d = (Declaration *)calloc(1, sizeof(Declaration));
                d->property = strdup(clean_prop);
                d->value = strdup(clean_val);
                if (!head_decl) head_decl = d;
                else tail_decl->next = d;
                tail_decl = d;
            }
        }
        if (*p == '}') p++;

        CSSRule *rule = (CSSRule *)calloc(1, sizeof(CSSRule));
        rule->selectors = selectors;
        rule->declarations = head_decl;
        rule->specificity = calculate_specificity(selectors);

        if (!sheet->rules) sheet->rules = rule;
        else tail_rule->next = rule;
        tail_rule = rule;
    }

    return sheet;
}

void stylesheet_free(Stylesheet *sheet) {
    if (!sheet) return;
    CSSRule *rule = sheet->rules;
    while (rule) {
        CSSRule *next_rule = rule->next;

        Selector *sel = rule->selectors;
        while (sel) {
            Selector *next_sel = sel->next;
            free(sel->value);
            free(sel);
            sel = next_sel;
        }

        Declaration *d = rule->declarations;
        while (d) {
            Declaration *next_d = d->next;
            free(d->property);
            free(d->value);
            free(d);
            d = next_d;
        }

        free(rule);
        rule = next_rule;
    }
    free(sheet);
}

static bool selector_matches_node(const Selector *sel, const DOMNode *node) {
    if (!sel || !node || node->type != NODE_ELEMENT) return false;

    if (sel->type == SELECTOR_UNIVERSAL) return true;

    if (sel->type == SELECTOR_TAG) {
        return node->tag_name && (strcasecmp(node->tag_name, sel->value) == 0);
    }
    if (sel->type == SELECTOR_CLASS) {
        return dom_node_has_class(node, sel->value);
    }
    if (sel->type == SELECTOR_ID) {
        const char *id = dom_node_get_id(node);
        return id && (strcmp(id, sel->value) == 0);
    }
    return false;
}

static void apply_declaration(ComputedStyle *style, const char *prop, const char *val) {
    if (!style || !prop || !val) return;

    if (strcasecmp(prop, "color") == 0) {
        style->color = parse_color(val);
    } else if (strcasecmp(prop, "background-color") == 0 || strcasecmp(prop, "background") == 0) {
        style->background_color = parse_color(val);
    } else if (strcasecmp(prop, "display") == 0) {
        if (strcasecmp(val, "block") == 0) style->display = DISPLAY_BLOCK;
        else if (strcasecmp(val, "inline") == 0) style->display = DISPLAY_INLINE;
        else if (strcasecmp(val, "none") == 0) style->display = DISPLAY_NONE;
    } else if (strcasecmp(prop, "width") == 0) {
        if (strcasecmp(val, "auto") == 0) {
            style->width_auto = true;
        } else {
            style->width = parse_px(val);
            style->width_auto = false;
        }
    } else if (strcasecmp(prop, "height") == 0) {
        if (strcasecmp(val, "auto") == 0) {
            style->height_auto = true;
        } else {
            style->height = parse_px(val);
            style->height_auto = false;
        }
    } else if (strcasecmp(prop, "margin") == 0) {
        style->margin = parse_spacing(val);
    } else if (strcasecmp(prop, "margin-top") == 0) {
        style->margin.top = parse_px(val);
    } else if (strcasecmp(prop, "margin-right") == 0) {
        style->margin.right = parse_px(val);
    } else if (strcasecmp(prop, "margin-bottom") == 0) {
        style->margin.bottom = parse_px(val);
    } else if (strcasecmp(prop, "margin-left") == 0) {
        style->margin.left = parse_px(val);
    } else if (strcasecmp(prop, "padding") == 0) {
        style->padding = parse_spacing(val);
    } else if (strcasecmp(prop, "padding-top") == 0) {
        style->padding.top = parse_px(val);
    } else if (strcasecmp(prop, "padding-right") == 0) {
        style->padding.right = parse_px(val);
    } else if (strcasecmp(prop, "padding-bottom") == 0) {
        style->padding.bottom = parse_px(val);
    } else if (strcasecmp(prop, "padding-left") == 0) {
        style->padding.left = parse_px(val);
    } else if (strcasecmp(prop, "border-width") == 0 || strcasecmp(prop, "border") == 0) {
        style->border_width = parse_px(val);
        style->border_color = (RGBA){180, 180, 190, 255};
    } else if (strcasecmp(prop, "border-color") == 0) {
        style->border_color = parse_color(val);
    } else if (strcasecmp(prop, "font-size") == 0) {
        style->font_size = parse_px(val);
    }
}

static void apply_inline_style(ComputedStyle *style, const char *inline_css) {
    if (!style || !inline_css) return;

    char buf[512];
    strncpy(buf, inline_css, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    char *stmt = strtok(buf, ";");
    while (stmt) {
        char *colon = strchr(stmt, ':');
        if (colon) {
            *colon = 0;
            char *prop = trim_whitespace(stmt);
            char *val = trim_whitespace(colon + 1);
            apply_declaration(style, prop, val);
        }
        stmt = strtok(NULL, ";");
    }
}

static void apply_styles_to_node(DOMNode *node, const Stylesheet *sheet) {
    if (!node) return;

    if (!node->style) {
        node->style = computed_style_create();
    }

    if (node->type == NODE_ELEMENT) {
        computed_style_apply_defaults(node->style, node->tag_name);

        if (node->parent && node->parent->style) {
            computed_style_inherit_from_parent(node->style, node->parent->style);
        }

        /* Match rules from stylesheet */
        if (sheet) {
            for (const CSSRule *rule = sheet->rules; rule; rule = rule->next) {
                bool matched = false;
                for (const Selector *sel = rule->selectors; sel; sel = sel->next) {
                    if (selector_matches_node(sel, node)) {
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    for (const Declaration *d = rule->declarations; d; d = d->next) {
                        apply_declaration(node->style, d->property, d->value);
                    }
                }
            }
        }

        /* Inline style attribute overrides */
        const char *inline_css = dom_node_get_attribute(node, "style");
        if (inline_css) {
            apply_inline_style(node->style, inline_css);
        }
    } else if (node->type == NODE_TEXT) {
        if (node->parent && node->parent->style) {
            node->style->color = node->parent->style->color;
            node->style->font_size = node->parent->style->font_size;
            node->style->display = DISPLAY_INLINE;
        }
    }

    /* Recurse to children */
    for (DOMNode *child = node->first_child; child; child = child->next_sibling) {
        apply_styles_to_node(child, sheet);
    }
}

void css_apply_stylesheet(DOMNode *root, const Stylesheet *sheet) {
    apply_styles_to_node(root, sheet);
}
