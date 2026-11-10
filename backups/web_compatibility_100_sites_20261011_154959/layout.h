#ifndef LAYOUT_H
#define LAYOUT_H

#include "framebuffer.h"

typedef struct {
    int x;
    int y;
    int w;
    int h;
} layout_rect_t;

typedef enum {
    LAYOUT_DIRECTION_ROW = 0,
    LAYOUT_DIRECTION_COLUMN = 1
} layout_direction_t;

typedef enum {
    LAYOUT_ALIGN_START = 0,
    LAYOUT_ALIGN_CENTER = 1,
    LAYOUT_ALIGN_END = 2,
    LAYOUT_ALIGN_STRETCH = 3
} layout_align_t;

typedef enum {
    LAYOUT_JUSTIFY_START = 0,
    LAYOUT_JUSTIFY_CENTER = 1,
    LAYOUT_JUSTIFY_END = 2,
    LAYOUT_JUSTIFY_SPACE_BETWEEN = 3
} layout_justify_t;

typedef struct {
    int basis;
    int grow;
    int shrink;
    int min_w;
    int min_h;
    int max_w;
    int max_h;
    int pad_left;
    int pad_top;
    int pad_right;
    int pad_bottom;
    int margin_left;
    int margin_top;
    int margin_right;
    int margin_bottom;
    int align_self;
    int visible;
    layout_rect_t frame;
    layout_rect_t content;
} layout_item_t;

typedef struct {
    layout_rect_t frame;
    int pad_left;
    int pad_top;
    int pad_right;
    int pad_bottom;
    int gap;
    int wrap;
    layout_direction_t direction;
    layout_align_t align_items;
    layout_justify_t justify_content;
} layout_container_t;

void layout_init_container(layout_container_t *container, int x, int y, int w, int h);
void layout_init_item(layout_item_t *item, int basis_w, int basis_h);
void layout_set_padding(layout_item_t *item, int left, int top, int right, int bottom);
void layout_set_margin(layout_item_t *item, int left, int top, int right, int bottom);
void layout_set_container_padding(layout_container_t *container, int left, int top, int right, int bottom);
void layout_apply(layout_container_t *container, layout_item_t *items, int count);

#endif
