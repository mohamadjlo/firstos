#ifndef BB_LAYOUT_H
#define BB_LAYOUT_H

#include "bb_types.h"
#include "bb_dom.h"
#include "bb_css.h"
#include "bb_rasterizer.h"
#include "bb_font.h"

/* =========================================================================
 * Box types
 * ========================================================================= */
typedef enum {
    BB_BOX_BLOCK,
    BB_BOX_INLINE,
    BB_BOX_ANONYMOUS_TEXT,
    BB_BOX_REPLACED
} bb_box_type_t;

struct bb_image;

/* =========================================================================
 * Layout Box  —  the fundamental render unit
 * ========================================================================= */
typedef struct bb_layout_box {
    bb_box_type_t          type;
    bb_rect_t              dimensions; /* Content coordinates and size (x, y, w, h) */

    int32_t                margin_top, margin_right, margin_bottom, margin_left;
    int32_t                padding_top, padding_right, padding_bottom, padding_left;
    int32_t                border_width;
    bb_color_t             border_color;
    int32_t                border_radius;
    bb_color_t             background_color;
    bb_color_t             text_color;
    bb_font_style_t        font_style;

    char                  *text;       /* For anonymous text boxes */
    bb_node_t             *node;       /* Reference to corresponding DOM node */
    struct bb_image       *image;      /* Attached image resource for replaced boxes */

    struct bb_layout_box  *parent;
    struct bb_layout_box  *first_child;
    struct bb_layout_box  *last_child;
    struct bb_layout_box  *next_sibling;
    struct bb_layout_box  *prev_sibling;
} bb_layout_box_t;

/* Prompt-compatible alias */
typedef bb_layout_box_t LayoutBox;

/* =========================================================================
 * Line Box  —  accumulates inline items for two-pass alignment
 *
 * Usage:
 *   1. linebox_init()
 *   2. linebox_add() for each inline item
 *   3. linebox_flush() when line is full or block is done
 *   4. linebox_free() to release item pointer array
 * ========================================================================= */
typedef struct {
    bb_layout_box_t **items;
    int               item_count;
    int               item_capacity;
    int32_t           width;    /* sum of item widths on this line */
    int32_t           height;   /* max item height on this line */
    int32_t           baseline;
} bb_line_box_t;

/* Prompt-compatible alias */
#ifndef HTML5_ENGINE_H
typedef bb_line_box_t LineBox;
#endif

void linebox_init   (bb_line_box_t *lb);
void linebox_add    (bb_line_box_t *lb, bb_layout_box_t *item, int32_t item_w, int32_t item_h);
/* Flush: apply text-align offset to all items and reset the linebox.
 * container_content_w : usable width of the enclosing block
 * text_align          : BB_TEXT_ALIGN_{LEFT|CENTER|RIGHT}
 * cursor_x_out        : reset to container_padding_left after flush
 * cursor_y_in_out     : advanced by line height after flush             */
void linebox_flush  (bb_line_box_t *lb,
                     int32_t container_content_w,
                     int32_t container_padding_left,
                     int     text_align,
                     int32_t *cursor_x_out,
                     int32_t *cursor_y_in_out);
void linebox_free   (bb_line_box_t *lb);

/* =========================================================================
 * Whitespace collapsing
 * ========================================================================= */
/* Collapse whitespace in-place per CSS 'white-space: normal':
 *   - runs of [ \t\r\n] → single space 0x20
 *   - leading whitespace stripped when strip_leading is true
 *   - sets *has_trailing_space if the result ends in a space
 * Returns the new (shorter or equal) length.                             */
size_t bb_html_collapse_whitespace(char *text, bool strip_leading,
                                   bool *has_trailing_space);

/* =========================================================================
 * Layout Pipeline
 * ========================================================================= */
bb_layout_box_t *bb_layout_build_tree  (bb_node_t *node);
void             bb_layout_compute     (bb_layout_box_t *box,
                                        int32_t x, int32_t y,
                                        int32_t container_w);
void             bb_layout_paint       (bb_layout_box_t *box,
                                        bb_raster_t *r,
                                        int32_t offset_x, int32_t offset_y);
bb_node_t       *bb_layout_hit_test   (bb_layout_box_t *box,
                                        int32_t x, int32_t y,
                                        int32_t offset_x, int32_t offset_y);
void             bb_layout_free_tree  (bb_layout_box_t *box);

/* Viewport dimensions used for POS_FIXED containing-block resolution.
 * Set once at browser init / window resize.                             */
extern int32_t g_layout_viewport_w;
extern int32_t g_layout_viewport_h;

#endif /* BB_LAYOUT_H */
