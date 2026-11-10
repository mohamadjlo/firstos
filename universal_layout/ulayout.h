/*
 * ulayout.h - Universal layout engine shared types
 *
 * Shared data structures for the four upgraded subsystems:
 *   UTF-8 decoding & glyph mapping  (utf8.c)
 *   CSS positioning model           (position.c)
 *   Two-pass line box layout        (linebox.c)
 *   Whitespace collapsing           (whitespace.c)
 *
 * Freestanding-friendly: the only libc headers used are <stdint.h> and
 * <stddef.h>. All allocation goes through UL_MALLOC / UL_REALLOC / UL_FREE,
 * which default to malloc/realloc/free on a hosted build and can be
 * remapped to the kernel heap (kmalloc/kfree) on bare metal.
 */
#ifndef ULAYOUT_H
#define ULAYOUT_H

#include <stdint.h>
#include <stddef.h>

#ifndef UL_MALLOC
#include <stdlib.h>
#define UL_MALLOC  malloc
#define UL_REALLOC realloc
#define UL_FREE    free
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Sentinels for "auto" CSS values. Chosen so real pixel values (>= 0)
 * never collide with the auto state. */
#define UL_AUTO INT32_MIN

/* ------------------------------------------------------------------ */
/* Framebuffer drawing context                                        */
/* ------------------------------------------------------------------ */

/*
 * FBContext abstracts the raster target so the layout modules stay free
 * of any GUI-toolkit or driver dependency. On FirstOS this maps onto the
 * existing framebuffer.c blit primitives; the glyph_width/glyph_height
 * fields describe the 8x16 (or scaled) console font.
 */
typedef struct FBContext {
    void *user;                 /* opaque driver state */
    int glyph_width;            /* horizontal advance per glyph, px (e.g. 8) */
    int glyph_height;           /* font cell height, px (e.g. 16) */
    void (*fill_rect)(void *user, int x, int y, int w, int h, uint32_t color);
    void (*draw_glyph)(void *user, int glyph_index, int x, int y, uint32_t color);
} FBContext;

/* ------------------------------------------------------------------ */
/* DOM                                                                  */
/* ------------------------------------------------------------------ */

typedef struct DOMNode DOMNode;
struct DOMNode {
    const char *tag;            /* element name, NULL for text nodes */
    const char *text;           /* text content for text nodes */
    DOMNode *parent;
    DOMNode *first_child;
    DOMNode *next_sibling;
};

/* ------------------------------------------------------------------ */
/* Computed style                                                      */
/* ------------------------------------------------------------------ */

typedef enum {
    POS_STATIC = 0,
    POS_RELATIVE,
    POS_ABSOLUTE,
    POS_FIXED
} PositionType;

typedef enum {
    TEXT_ALIGN_LEFT = 0,
    TEXT_ALIGN_CENTER,
    TEXT_ALIGN_RIGHT,
    TEXT_ALIGN_JUSTIFY
} TextAlign;

typedef struct ComputedStyle {
    PositionType position;
    /* Box offsets. UL_AUTO means "auto" per CSS 2.1 section 10.3/10.6. */
    int top;
    int right;
    int bottom;
    int left;
    /* Box dimensions. UL_AUTO means shrink/expand per flow rules. */
    int width;
    int height;
    int margin_top;
    int margin_right;
    int margin_bottom;
    int margin_left;
    int padding_top;
    int padding_right;
    int padding_bottom;
    int padding_left;
    TextAlign text_align;
    int line_height;            /* px; <= 0 means "use glyph_height" */
    uint32_t color;
    uint32_t bg_color;
} ComputedStyle;

/* ------------------------------------------------------------------ */
/* Layout box                                                          */
/* ------------------------------------------------------------------ */

typedef struct LayoutBox LayoutBox;
struct LayoutBox {
    DOMNode *node;              /* originating DOM node, may be NULL */
    ComputedStyle style;        /* embedded copy of the computed style */
    int x, y;                   /* border-box origin, absolute coords */
    int width, height;          /* border-box size */
    int baseline;               /* baseline offset from box top */

    /* Text-run payloads: for boxes produced from a text node. */
    const char *text;
    int text_len;

    /* Tree links */
    LayoutBox *parent;
    LayoutBox *first_child;
    LayoutBox *next_sibling;
};

#ifdef __cplusplus
}
#endif

#endif /* ULAYOUT_H */
