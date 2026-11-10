#ifndef CSS_PARSER_H
#define CSS_PARSER_H

#include "bb_types.h"
#include "css_selectors.h"
#include "bb_css.h"

typedef enum {
    CSS_PROP_UNKNOWN = 0,
    CSS_PROP_DISPLAY,
    CSS_PROP_POSITION,
    CSS_PROP_TOP, CSS_PROP_RIGHT, CSS_PROP_BOTTOM, CSS_PROP_LEFT,
    CSS_PROP_WIDTH, CSS_PROP_HEIGHT,
    CSS_PROP_MARGIN_TOP, CSS_PROP_MARGIN_RIGHT, CSS_PROP_MARGIN_BOTTOM, CSS_PROP_MARGIN_LEFT,
    CSS_PROP_MARGIN,
    CSS_PROP_PADDING_TOP, CSS_PROP_PADDING_RIGHT, CSS_PROP_PADDING_BOTTOM, CSS_PROP_PADDING_LEFT,
    CSS_PROP_PADDING,
    CSS_PROP_BORDER_WIDTH, CSS_PROP_BORDER_COLOR, CSS_PROP_BORDER_RADIUS, CSS_PROP_BORDER,
    CSS_PROP_COLOR,
    CSS_PROP_BACKGROUND_COLOR,
    CSS_PROP_FONT_SIZE, CSS_PROP_FONT_WEIGHT,
    CSS_PROP_TEXT_ALIGN,
    CSS_PROP_LINE_HEIGHT,
    CSS_PROP_WHITE_SPACE,
    CSS_PROP_OVERFLOW,
    CSS_PROP_VISIBILITY,
    CSS_PROP_Z_INDEX
} css_property_id_t;

typedef struct css_declaration {
    css_property_id_t       prop_id;
    char                   *raw_val;
    struct css_declaration *next;
} css_declaration_t;

typedef struct css_rule {
    css_selector_chain_t *selector;
    css_declaration_t    *declarations;
    uint32_t              source_order;
    struct css_rule      *next;
} css_rule_t;

typedef struct css_parsed_sheet {
    css_rule_t *rules;
    uint32_t    rule_count;
} css_parsed_sheet_t;

css_parsed_sheet_t *css_parsed_sheet_create(void);
void                css_parsed_sheet_free(css_parsed_sheet_t *sheet);
void                css_parse_stylesheet(css_parsed_sheet_t *sheet, const char *css_text, size_t len);

/* Apply declarations to a bb_style_t */
void                css_apply_declaration(bb_style_t *style, css_property_id_t prop_id, const char *val);
void                css_parse_declarations_into_style(bb_style_t *style, const char *decl_text);

#endif /* CSS_PARSER_H */
