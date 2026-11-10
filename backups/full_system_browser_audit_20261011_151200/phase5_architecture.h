#ifndef PHASE5_ARCHITECTURE_H
#define PHASE5_ARCHITECTURE_H

#include "framebuffer.h"

typedef enum {
    GUI_EVENT_NONE = 0,
    GUI_EVENT_MOUSE_DOWN,
    GUI_EVENT_MOUSE_UP,
    GUI_EVENT_MOUSE_MOVE,
    GUI_EVENT_KEY_DOWN,
    GUI_EVENT_KEY_UP,
    GUI_EVENT_FOCUS
} gui_event_type_t;

typedef struct {
    gui_event_type_t type;
    int x;
    int y;
    unsigned char key;
    int button;
} gui_event_t;

typedef struct {
    rect_t bounds;
    int direction;
    int gap;
    int cursor_x;
    int cursor_y;
} gui_layout_t;

#define GUI_LAYOUT_VERTICAL 0
#define GUI_LAYOUT_HORIZONTAL 1

static inline void gui_layout_begin(gui_layout_t *layout, rect_t bounds, int direction, int gap) {
    if (!layout) return;
    layout->bounds = bounds;
    layout->direction = direction;
    layout->gap = gap;
    layout->cursor_x = bounds.x;
    layout->cursor_y = bounds.y;
}

static inline rect_t gui_layout_next(gui_layout_t *layout, int w, int h) {
    rect_t result = {0, 0, w, h, 0};
    if (!layout) return result;
    result.x = layout->cursor_x;
    result.y = layout->cursor_y;
    if (layout->direction == GUI_LAYOUT_HORIZONTAL) layout->cursor_x += w + layout->gap;
    else layout->cursor_y += h + layout->gap;
    return result;
}

#endif
