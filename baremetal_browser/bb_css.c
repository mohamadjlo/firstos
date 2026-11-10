#include "bb_css.h"
#include "css_parser.h"
#include "style_resolver.h"
#include "bb_libc.h"

/* Parse a single hex character */
static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}

bb_color_t bb_css_parse_color(const char *val) {
    if (!val) return BB_COLOR_BLACK;
    while (*val == ' ' || *val == '\t') val++;

    if (bb_strcasecmp(val, "transparent") == 0) return BB_COLOR_TRANSPARENT;
    if (bb_strcasecmp(val, "white") == 0) return BB_COLOR_WHITE;
    if (bb_strcasecmp(val, "black") == 0) return BB_COLOR_BLACK;
    if (bb_strcasecmp(val, "red") == 0) return BB_RGB(230, 40, 40);
    if (bb_strcasecmp(val, "green") == 0) return BB_RGB(30, 160, 40);
    if (bb_strcasecmp(val, "blue") == 0) return BB_RGB(30, 110, 230);
    if (bb_strcasecmp(val, "yellow") == 0) return BB_RGB(250, 210, 30);
    if (bb_strcasecmp(val, "cyan") == 0) return BB_RGB(0, 200, 220);
    if (bb_strcasecmp(val, "magenta") == 0) return BB_RGB(220, 30, 200);
    if (bb_strcasecmp(val, "gray") == 0 || bb_strcasecmp(val, "grey") == 0) return BB_COLOR_MID_GRAY;
    if (bb_strcasecmp(val, "lightgray") == 0 || bb_strcasecmp(val, "lightgrey") == 0) return BB_COLOR_LIGHT_GRAY;
    if (bb_strcasecmp(val, "darkgray") == 0 || bb_strcasecmp(val, "darkgrey") == 0) return BB_COLOR_DARK_GRAY;
    if (bb_strcasecmp(val, "orange") == 0) return BB_RGB(240, 140, 20);
    if (bb_strcasecmp(val, "purple") == 0) return BB_RGB(130, 40, 170);
    if (bb_strcasecmp(val, "navy") == 0) return BB_RGB(10, 25, 80);

    /* #RGB or #RRGGBB */
    if (*val == '#') {
        val++;
        size_t len = bb_strlen(val);
        if (len == 3) {
            uint8_t r = (uint8_t)(hex_val(val[0]) * 17);
            uint8_t g = (uint8_t)(hex_val(val[1]) * 17);
            uint8_t b = (uint8_t)(hex_val(val[2]) * 17);
            return BB_RGB(r, g, b);
        } else if (len >= 6) {
            uint8_t r = (uint8_t)((hex_val(val[0]) << 4) | hex_val(val[1]));
            uint8_t g = (uint8_t)((hex_val(val[2]) << 4) | hex_val(val[3]));
            uint8_t b = (uint8_t)((hex_val(val[4]) << 4) | hex_val(val[5]));
            return BB_RGB(r, g, b);
        }
    }

    /* rgb(r, g, b) */
    if (bb_strncasecmp(val, "rgb(", 4) == 0) {
        val += 4;
        int r = bb_atoi(val);
        const char *p = val;
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
        int g = bb_atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
        int b = bb_atoi(p);
        return BB_RGB(BB_CLAMP(r, 0, 255), BB_CLAMP(g, 0, 255), BB_CLAMP(b, 0, 255));
    }

    return BB_COLOR_BLACK;
}

bb_stylesheet_t *bb_css_create(void) {
    bb_stylesheet_t *sheet = (bb_stylesheet_t *)bb_malloc(sizeof(bb_stylesheet_t));
    if (sheet) {
        sheet->parsed = css_parsed_sheet_create();
        sheet->rules = NULL;
    }
    return sheet;
}

void bb_css_parse_rules(bb_stylesheet_t *sheet, const char *css_text) {
    if (!sheet || !sheet->parsed || !css_text) return;
    css_parse_stylesheet(sheet->parsed, css_text, bb_strlen(css_text));
    sheet->rules = (void *)sheet->parsed->rules;
}

void bb_css_free(bb_stylesheet_t *sheet) {
    if (!sheet) return;
    if (sheet->parsed) {
        css_parsed_sheet_free(sheet->parsed);
    }
    bb_free(sheet);
}

void bb_css_parse_inline_style(bb_style_t *style, const char *style_attr) {
    if (!style || !style_attr) return;
    css_parse_declarations_into_style(style, style_attr);
}

void bb_css_apply_styles(bb_node_t *root, const bb_stylesheet_t *sheet) {
    if (!root) return;
    style_resolve_tree(root, sheet ? sheet->parsed : NULL);
}
