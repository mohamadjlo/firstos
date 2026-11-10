#ifndef BB_CSS_H
#define BB_CSS_H

#include "bb_types.h"
#include "bb_dom.h"

typedef enum {
    BB_DISPLAY_BLOCK,
    BB_DISPLAY_INLINE,
    BB_DISPLAY_INLINE_BLOCK,
    BB_DISPLAY_NONE
} bb_display_t;

/* CSS text-align values */
#define BB_TEXT_ALIGN_LEFT   0
#define BB_TEXT_ALIGN_CENTER 1
#define BB_TEXT_ALIGN_RIGHT  2

/* CSS white-space */
typedef enum {
    BB_WHITE_SPACE_NORMAL = 0,
    BB_WHITE_SPACE_NOWRAP,
    BB_WHITE_SPACE_PRE
} bb_white_space_t;

/* CSS overflow */
typedef enum {
    BB_OVERFLOW_VISIBLE = 0,
    BB_OVERFLOW_HIDDEN,
    BB_OVERFLOW_SCROLL,
    BB_OVERFLOW_AUTO
} bb_overflow_t;

/* CSS visibility */
typedef enum {
    BB_VISIBILITY_VISIBLE = 0,
    BB_VISIBILITY_HIDDEN
} bb_visibility_t;

/* CSS position model */
typedef enum {
    BB_POSITION_STATIC = 0, /* default: normal flow */
    BB_POSITION_RELATIVE,   /* flow + visual offset */
    BB_POSITION_ABSOLUTE,   /* out-of-flow, rel. to nearest positioned ancestor */
    BB_POSITION_FIXED       /* out-of-flow, rel. to viewport */
} bb_position_t;

/* Typedef aliases for prompt-specified caller names */
typedef bb_position_t PositionType;
#define POS_STATIC   BB_POSITION_STATIC
#define POS_RELATIVE BB_POSITION_RELATIVE
#define POS_ABSOLUTE BB_POSITION_ABSOLUTE
#define POS_FIXED    BB_POSITION_FIXED

typedef struct bb_style {
    bb_display_t     display;
    bb_color_t       color;
    bb_color_t       background_color;
    int32_t          font_size;
    int32_t          font_weight; /* 400=normal, 700=bold */
    int32_t          text_align;  /* BB_TEXT_ALIGN_* */
    int32_t          line_height; /* 0 = normal */
    bb_white_space_t white_space;
    bb_overflow_t    overflow;
    bb_visibility_t  visibility;
    int32_t          z_index;     /* 0 = auto / default */

    int32_t          margin_top;
    int32_t          margin_right;
    int32_t          margin_bottom;
    int32_t          margin_left;

    int32_t          padding_top;
    int32_t          padding_right;
    int32_t          padding_bottom;
    int32_t          padding_left;

    int32_t          border_width;
    bb_color_t       border_color;
    int32_t          border_radius;

    int32_t          width;       /* -1 = auto */
    int32_t          height;      /* -1 = auto */

    /* CSS Position Model */
    bb_position_t    position;   /* BB_POSITION_STATIC by default */
    int32_t          top,    right,    bottom,    left;
    bool             has_top, has_right, has_bottom, has_left;
} bb_style_t;

struct css_parsed_sheet;

typedef struct bb_stylesheet {
    struct css_parsed_sheet *parsed;
    void                    *rules; /* Legacy compatibility pointer */
} bb_stylesheet_t;

/* CSS Engine Public API */
bb_stylesheet_t *bb_css_create(void);
void             bb_css_parse_rules(bb_stylesheet_t *sheet, const char *css_text);
void             bb_css_free(bb_stylesheet_t *sheet);

bb_color_t       bb_css_parse_color(const char *val);
void             bb_css_apply_styles(bb_node_t *root, const bb_stylesheet_t *sheet);
void             bb_css_parse_inline_style(bb_style_t *style, const char *style_attr);

#endif /* BB_CSS_H */
