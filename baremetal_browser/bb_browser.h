#ifndef BB_BROWSER_H
#define BB_BROWSER_H

#include "bb_types.h"
#include "bb_osal.h"
#include "bb_rasterizer.h"
#include "bb_font.h"
#include "bb_dom.h"
#include "bb_html.h"
#include "bb_css.h"
#include "bb_layout.h"
#include "hit_test.h"
#include "bb_http.h"

typedef struct {
    uint32_t         width;
    uint32_t         height;
    bb_fb_info_t    *fb;
    bb_raster_t      raster;
    uint32_t        *backbuffer;

    /* Chrome Geometry */
    bb_rect_t        title_bar_rect;
    bb_rect_t        nav_bar_rect;
    bb_rect_t        btn_back_rect;
    bb_rect_t        btn_forward_rect;
    bb_rect_t        btn_reload_rect;
    bb_rect_t        url_bar_rect;
    bb_rect_t        viewport_rect;
    bb_rect_t        scrollbar_rect;
    bb_rect_t        status_bar_rect;

    /* Interactive URL Bar State */
    char             url_buffer[256];
    size_t           url_length;
    size_t           cursor_pos;
    bool             url_focused;
    uint32_t         cursor_blink_tick;

    /* Status Bar State */
    char             status_text[128];

    /* Scrolling State */
    int32_t          scroll_y;
    int32_t          max_scroll_y;
    int32_t          document_height;
    bool             is_dragging_scroll;
    int32_t          drag_start_mouse_y;
    int32_t          drag_start_scroll_y;

    /* Software Cursor State */
    int32_t          mouse_x;
    int32_t          mouse_y;
    bool             mouse_down;
    uint32_t         cursor_saved_pixels[16 * 24];
    bb_rect_t        cursor_prev_rect;
    bool             cursor_drawn;

    /* Document Engine State */
    bb_node_t       *dom_root;
    bb_stylesheet_t *stylesheet;
    bb_layout_box_t *layout_root;
    bool             needs_layout;
    bool             needs_repaint;
} bb_browser_t;

/* Browser Lifecycle & Control */
bb_browser_t *bb_browser_create(uint32_t width, uint32_t height);
void          bb_browser_navigate(bb_browser_t *b, const char *url);
void          bb_browser_handle_event(bb_browser_t *b, const bb_event_t *ev);
void          bb_browser_render(bb_browser_t *b);
void          bb_browser_destroy(bb_browser_t *b);

#endif /* BB_BROWSER_H */
