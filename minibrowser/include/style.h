#ifndef STYLE_H
#define STYLE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DISPLAY_BLOCK,
    DISPLAY_INLINE,
    DISPLAY_NONE
} DisplayType;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} RGBA;

typedef struct {
    float top;
    float right;
    float bottom;
    float left;
} Spacing;

typedef struct ComputedStyle {
    DisplayType display;
    float width;
    float height;
    bool width_auto;
    bool height_auto;

    Spacing margin;
    Spacing padding;

    RGBA color;
    RGBA background_color;

    float border_width;
    RGBA border_color;

    float font_size;
} ComputedStyle;

struct DOMNode;

ComputedStyle *computed_style_create(void);
void computed_style_free(ComputedStyle *style);
void computed_style_apply_defaults(ComputedStyle *style, const char *tag_name);
void computed_style_inherit_from_parent(ComputedStyle *child_style, const ComputedStyle *parent_style);

#endif /* STYLE_H */
