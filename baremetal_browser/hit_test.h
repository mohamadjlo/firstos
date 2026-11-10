#ifndef BB_HIT_TEST_H
#define BB_HIT_TEST_H

#include "bb_types.h"
#include "bb_dom.h"
#include "bb_layout.h"

/* =========================================================================
 * Cursor types for UI feedback
 * ========================================================================= */
typedef enum {
    BB_CURSOR_DEFAULT = 0,
    BB_CURSOR_POINTER,    /* Links (<a>), clickable buttons (<button>), etc. */
    BB_CURSOR_TEXT,       /* Selectable text runs, text inputs */
    BB_CURSOR_NOT_ALLOWED /* Disabled interactive controls */
} bb_cursor_type_t;

/* =========================================================================
 * Hit target classification
 * ========================================================================= */
typedef enum {
    BB_HIT_TARGET_NONE = 0,
    BB_HIT_TARGET_BLOCK,
    BB_HIT_TARGET_INLINE,
    BB_HIT_TARGET_TEXT,
    BB_HIT_TARGET_LINK,
    BB_HIT_TARGET_REPLACED, /* Replaced elements (e.g., <img>, <canvas>) */
    BB_HIT_TARGET_INPUT     /* Interactive controls (<input>, <button>) */
} bb_hit_target_type_t;

/* =========================================================================
 * Hit test result metadata
 * ========================================================================= */
typedef struct bb_hit_test_result {
    bb_hit_target_type_t target_type;
    bb_layout_box_t     *box;              /* Topmost layout box hit */
    bb_node_t           *node;             /* Direct DOM node associated with box */
    bb_node_t           *interactive_node; /* Enclosing interactive element (<a>, <button>, etc.) */

    /* Link metadata */
    bool                 is_link;
    const char          *href;             /* href attribute if is_link is true */
    const char          *target;           /* target attribute (e.g. _blank) */

    /* Interactivity flags */
    bool                 is_interactive;   /* true for links, buttons, inputs */
    bool                 is_hoverable;     /* true if element can receive hover states */
    bool                 is_activatable;   /* true if click can trigger navigation/action */
    bb_cursor_type_t     cursor;

    /* Tooltip metadata */
    const char          *title;            /* title="..." or alt="..." attribute */

    /* Text selection / glyph info */
    bool                 is_text;
    int32_t              text_offset;      /* Character/glyph index within box->text */
    int32_t              local_x;          /* Hit point relative to box content area */
    int32_t              local_y;

    /* Coordinate metadata */
    int32_t              point_x;          /* Input test point X */
    int32_t              point_y;          /* Input test point Y */
} bb_hit_test_result_t;

/* Prompt-compatible aliases */
typedef bb_hit_test_result_t HitTestResult;
typedef bb_cursor_type_t     CursorType;
typedef bb_layout_box_t      LayoutBox;
typedef bb_node_t            DOMNode;

/* =========================================================================
 * Core Hit Testing API
 * ========================================================================= */

/* Hit test against document coordinates (doc_x, doc_y) */
bool hit_test_document(bb_layout_box_t *root,
                       int32_t doc_x, int32_t doc_y,
                       bb_hit_test_result_t *out_result);

/* Hit test against viewport coordinates (vp_x, vp_y) given scroll and viewport bounds */
bool hit_test_viewport(bb_layout_box_t *root,
                       int32_t vp_x, int32_t vp_y,
                       int32_t scroll_x, int32_t scroll_y,
                       const bb_rect_t *viewport_clip,
                       bb_hit_test_result_t *out_result);

/* Returns topmost DOM node at document coordinates (x, y) */
bb_node_t *node_at_point(bb_layout_box_t *root, int32_t x, int32_t y);

/* General hit test with arbitrary translation offsets and clipping boundaries */
bool hit_test_at_point(bb_layout_box_t *root,
                       int32_t x, int32_t y,
                       int32_t offset_x, int32_t offset_y,
                       bb_rect_t clip_rect,
                       bb_hit_test_result_t *out_result);

#endif /* BB_HIT_TEST_H */
