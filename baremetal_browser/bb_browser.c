#include "bb_browser.h"

/* 12x19 Software Mouse Cursor Sprite (1 = border black, 2 = body white, 0 = transparent) */
static const uint8_t s_cursor_sprite[19][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,1,1,1,1,0,0},
    {1,2,2,1,2,2,1,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0}
};

static void draw_software_cursor(bb_browser_t *b) {
    if (!b || !b->backbuffer) return;

    int32_t mx = b->mouse_x;
    int32_t my = b->mouse_y;

    /* Save background pixels before drawing cursor */
    int row, col;
    for (row = 0; row < 19; row++) {
        for (col = 0; col < 12; col++) {
            int32_t px = mx + col;
            int32_t py = my + row;
            if (px >= 0 && px < (int32_t)b->width && py >= 0 && py < (int32_t)b->height) {
                b->cursor_saved_pixels[row * 12 + col] = b->backbuffer[py * b->width + px];
                uint8_t pixel_type = s_cursor_sprite[row][col];
                if (pixel_type == 1) {
                    b->backbuffer[py * b->width + px] = BB_COLOR_BLACK;
                } else if (pixel_type == 2) {
                    b->backbuffer[py * b->width + px] = BB_COLOR_WHITE;
                }
            }
        }
    }
    b->cursor_prev_rect.x = mx;
    b->cursor_prev_rect.y = my;
    b->cursor_prev_rect.w = 12;
    b->cursor_prev_rect.h = 19;
    b->cursor_drawn = true;
}

static void restore_software_cursor(bb_browser_t *b) {
    if (!b || !b->cursor_drawn || !b->backbuffer) return;
    int row, col;
    for (row = 0; row < 19; row++) {
        for (col = 0; col < 12; col++) {
            int32_t px = b->cursor_prev_rect.x + col;
            int32_t py = b->cursor_prev_rect.y + row;
            if (px >= 0 && px < (int32_t)b->width && py >= 0 && py < (int32_t)b->height) {
                b->backbuffer[py * b->width + px] = b->cursor_saved_pixels[row * 12 + col];
            }
        }
    }
    b->cursor_drawn = false;
}

bb_browser_t *bb_browser_create(uint32_t width, uint32_t height) {
    bb_browser_t *b = (bb_browser_t *)bb_malloc(sizeof(bb_browser_t));
    if (!b) return NULL;
    bb_memset(b, 0, sizeof(bb_browser_t));

    b->width = width;
    b->height = height;
    b->fb = bb_osal_get_fb();

    b->backbuffer = (uint32_t *)bb_malloc(width * height * sizeof(uint32_t));
    if (!b->backbuffer) {
        bb_free(b);
        return NULL;
    }

    bb_raster_init(&b->raster, b->backbuffer, width, height);

    /* Setup Chrome Geometry */
    b->title_bar_rect.x = 0;
    b->title_bar_rect.y = 0;
    b->title_bar_rect.w = width;
    b->title_bar_rect.h = 28;

    b->nav_bar_rect.x = 0;
    b->nav_bar_rect.y = 28;
    b->nav_bar_rect.w = width;
    b->nav_bar_rect.h = 38;

    b->btn_back_rect.x = 10;
    b->btn_back_rect.y = 33;
    b->btn_back_rect.w = 28;
    b->btn_back_rect.h = 28;

    b->btn_forward_rect.x = 44;
    b->btn_forward_rect.y = 33;
    b->btn_forward_rect.w = 28;
    b->btn_forward_rect.h = 28;

    b->btn_reload_rect.x = 78;
    b->btn_reload_rect.y = 33;
    b->btn_reload_rect.w = 28;
    b->btn_reload_rect.h = 28;

    b->url_bar_rect.x = 114;
    b->url_bar_rect.y = 33;
    b->url_bar_rect.w = width - 126;
    b->url_bar_rect.h = 28;

    b->status_bar_rect.x = 0;
    b->status_bar_rect.y = height - 22;
    b->status_bar_rect.w = width;
    b->status_bar_rect.h = 22;

    b->scrollbar_rect.x = width - 14;
    b->scrollbar_rect.y = 66;
    b->scrollbar_rect.w = 14;
    b->scrollbar_rect.h = height - 66 - 22;

    b->viewport_rect.x = 0;
    b->viewport_rect.y = 66;
    b->viewport_rect.w = width - 14;
    b->viewport_rect.h = height - 66 - 22;

    /* Initial URL & Status */
    bb_strcpy(b->url_buffer, "about:firstos");
    b->url_length = bb_strlen(b->url_buffer);
    b->cursor_pos = b->url_length;
    b->url_focused = false;
    bb_strcpy(b->status_text, "Ready");

    b->mouse_x = width / 2;
    b->mouse_y = height / 2;
    b->mouse_down = false;

    /* Navigate to default offline demo */
    bb_browser_navigate(b, b->url_buffer);

    return b;
}

void bb_browser_navigate(bb_browser_t *b, const char *url) {
    if (!b || !url) return;

    /* Update URL Bar */
    bb_strncpy(b->url_buffer, url, sizeof(b->url_buffer) - 1);
    b->url_length = bb_strlen(b->url_buffer);
    b->cursor_pos = b->url_length;

    bb_snprintf(b->status_text, sizeof(b->status_text), "Loading %s...", url);
    bb_osal_log("[Browser] Navigating to: ");
    bb_osal_log(url);
    bb_osal_log("\n");

    /* Free previous document trees */
    if (b->layout_root) {
        bb_layout_free_tree(b->layout_root);
        b->layout_root = NULL;
    }
    if (b->stylesheet) {
        bb_css_free(b->stylesheet);
        b->stylesheet = NULL;
    }
    if (b->dom_root) {
        bb_node_free_tree(b->dom_root);
        b->dom_root = NULL;
    }

    /* Parse URL & Fetch */
    bb_url_t parsed_url;
    bb_url_parse(url, &parsed_url);
    bb_http_response_t *resp = bb_http_get(&parsed_url);

    const char *html_source = NULL;
    if (resp && resp->body) {
        html_source = resp->body;
    } else {
        html_source = bb_http_get_offline_demo_page();
    }

    /* Parse HTML to DOM */
    b->dom_root = bb_html_parse(html_source);

    /* Create and apply CSS stylesheet */
    b->stylesheet = bb_css_create();
    
    /* Extract any <style> tags from DOM */
    /* For simplicity, CSS stylesheet is parsed directly from document rules */
    const char *style_tag_start = bb_strstr(html_source, "<style>");
    if (style_tag_start) {
        style_tag_start += 7;
        const char *style_tag_end = bb_strstr(style_tag_start, "</style>");
        if (style_tag_end) {
            size_t slen = (size_t)(style_tag_end - style_tag_start);
            char *css_buf = (char *)bb_malloc(slen + 1);
            if (css_buf) {
                bb_memcpy(css_buf, style_tag_start, slen);
                css_buf[slen] = '\0';
                bb_css_parse_rules(b->stylesheet, css_buf);
                bb_free(css_buf);
            }
        }
    }

    bb_css_apply_styles(b->dom_root, b->stylesheet);

    /* Build Layout Tree */
    b->layout_root = bb_layout_build_tree(b->dom_root);
    if (b->layout_root) {
        bb_layout_compute(b->layout_root, 0, 0, b->viewport_rect.w);
        b->document_height = b->layout_root->dimensions.h;
    } else {
        b->document_height = 0;
    }

    /* Reset Scroll */
    b->scroll_y = 0;
    b->max_scroll_y = b->document_height - b->viewport_rect.h;
    if (b->max_scroll_y < 0) b->max_scroll_y = 0;

    if (resp) {
        bb_snprintf(b->status_text, sizeof(b->status_text), "HTTP %d OK | Content-Length: %u bytes", 
                    resp->status_code, (unsigned int)resp->body_len);
        bb_http_response_free(resp);
    } else {
        bb_strcpy(b->status_text, "Offline Mode | Loaded Local Showcase");
    }

    b->needs_repaint = true;
}

static bool point_in_rect(int32_t x, int32_t y, bb_rect_t r) {
    return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
}

void bb_browser_handle_event(bb_browser_t *b, const bb_event_t *ev) {
    if (!b || !ev) return;

    if (ev->type == BB_EVENT_MOUSE_MOVE) {
        restore_software_cursor(b);
        b->mouse_x = ev->mouse_x;
        b->mouse_y = ev->mouse_y;

        if (b->is_dragging_scroll && b->max_scroll_y > 0) {
            int32_t dy = ev->mouse_y - b->drag_start_mouse_y;
            int32_t track_h = b->scrollbar_rect.h;
            b->scroll_y = b->drag_start_scroll_y + (dy * b->max_scroll_y) / track_h;
            b->scroll_y = BB_CLAMP(b->scroll_y, 0, b->max_scroll_y);
            b->needs_repaint = true;
        }

        /* Hover detection for links in document viewport */
        if (point_in_rect(ev->mouse_x, ev->mouse_y, b->viewport_rect) && b->layout_root) {
            bb_hit_test_result_t hit;
            if (hit_test_viewport(b->layout_root, ev->mouse_x, ev->mouse_y,
                                  0, b->scroll_y, &b->viewport_rect, &hit)) {
                if (hit.is_link && hit.href) {
                    bb_snprintf(b->status_text, sizeof(b->status_text), "Link: %s", hit.href);
                } else if (hit.title) {
                    bb_snprintf(b->status_text, sizeof(b->status_text), "%s", hit.title);
                } else {
                    bb_strcpy(b->status_text, "Ready");
                }
            } else {
                bb_strcpy(b->status_text, "Ready");
            }
        }
    } else if (ev->type == BB_EVENT_MOUSE_DOWN) {
        b->mouse_down = true;

        /* Click inside Address Bar */
        if (point_in_rect(ev->mouse_x, ev->mouse_y, b->url_bar_rect)) {
            b->url_focused = true;
            b->cursor_pos = b->url_length;
            b->needs_repaint = true;
        } else {
            if (b->url_focused) {
                b->url_focused = false;
                b->needs_repaint = true;
            }
        }

        /* Reload Button */
        if (point_in_rect(ev->mouse_x, ev->mouse_y, b->btn_reload_rect)) {
            bb_browser_navigate(b, b->url_buffer);
        }

        /* Scrollbar Track click */
        if (point_in_rect(ev->mouse_x, ev->mouse_y, b->scrollbar_rect)) {
            b->is_dragging_scroll = true;
            b->drag_start_mouse_y = ev->mouse_y;
            b->drag_start_scroll_y = b->scroll_y;
        }

        /* Click in Viewport (Hyperlinks & Buttons) */
        if (point_in_rect(ev->mouse_x, ev->mouse_y, b->viewport_rect) && b->layout_root) {
            bb_hit_test_result_t hit;
            if (hit_test_viewport(b->layout_root, ev->mouse_x, ev->mouse_y,
                                  0, b->scroll_y, &b->viewport_rect, &hit)) {
                if (hit.is_link && hit.href) {
                    bb_browser_navigate(b, hit.href);
                } else if (hit.target_type == BB_HIT_TARGET_INPUT && hit.interactive_node) {
                    bb_snprintf(b->status_text, sizeof(b->status_text), "Clicked Button: %s", 
                                hit.interactive_node->first_child && hit.interactive_node->first_child->text_content ? 
                                hit.interactive_node->first_child->text_content : "Button");
                    b->needs_repaint = true;
                }
            }
        }
    } else if (ev->type == BB_EVENT_MOUSE_UP) {
        b->mouse_down = false;
        b->is_dragging_scroll = false;
    } else if (ev->type == BB_EVENT_MOUSE_SCROLL) {
        if (b->max_scroll_y > 0) {
            b->scroll_y -= ev->scroll_delta * 32;
            b->scroll_y = BB_CLAMP(b->scroll_y, 0, b->max_scroll_y);
            b->needs_repaint = true;
        }
    } else if (ev->type == BB_EVENT_KEY_DOWN) {
        if (b->url_focused) {
            if (ev->keycode == BB_KEY_ENTER) {
                b->url_focused = false;
                bb_browser_navigate(b, b->url_buffer);
            } else if (ev->keycode == BB_KEY_BACKSPACE) {
                if (b->cursor_pos > 0 && b->url_length > 0) {
                    size_t i;
                    for (i = b->cursor_pos - 1; i < b->url_length; i++) {
                        b->url_buffer[i] = b->url_buffer[i + 1];
                    }
                    b->cursor_pos--;
                    b->url_length--;
                    b->needs_repaint = true;
                }
            } else if (ev->keycode == BB_KEY_LEFT) {
                if (b->cursor_pos > 0) {
                    b->cursor_pos--;
                    b->needs_repaint = true;
                }
            } else if (ev->keycode == BB_KEY_RIGHT) {
                if (b->cursor_pos < b->url_length) {
                    b->cursor_pos++;
                    b->needs_repaint = true;
                }
            }
        } else {
            /* Page scrolling keys */
            if (ev->keycode == BB_KEY_DOWN || ev->keycode == BB_KEY_PAGE_DOWN) {
                b->scroll_y = BB_MIN(b->scroll_y + 40, b->max_scroll_y);
                b->needs_repaint = true;
            } else if (ev->keycode == BB_KEY_UP || ev->keycode == BB_KEY_PAGE_UP) {
                b->scroll_y = BB_MAX(b->scroll_y - 40, 0);
                b->needs_repaint = true;
            }
        }
    } else if (ev->type == BB_EVENT_CHAR) {
        if (b->url_focused && ev->char_code >= 32 && ev->char_code <= 126) {
            if (b->url_length + 1 < sizeof(b->url_buffer)) {
                size_t i;
                for (i = b->url_length + 1; i > b->cursor_pos; i--) {
                    b->url_buffer[i] = b->url_buffer[i - 1];
                }
                b->url_buffer[b->cursor_pos++] = ev->char_code;
                b->url_length++;
                b->needs_repaint = true;
            }
        }
    }
}

void bb_browser_render(bb_browser_t *b) {
    if (!b || !b->backbuffer) return;

    restore_software_cursor(b);

    /* 1. Clear background */
    bb_raster_clear(&b->raster, BB_COLOR_WHITE);

    /* 2. Title Bar */
    bb_raster_fill_rect(&b->raster, b->title_bar_rect, BB_RGB(43, 45, 48));
    bb_font_draw_string(&b->raster, 12, 6, "FirstOS Baremetal Web Browser (Freestanding)", BB_COLOR_WHITE, &BB_FONT_STYLE_BOLD);

    /* Title bar window control dots (close, minimize, maximize) */
    bb_rect_t dot_close = {(int32_t)b->width - 24, 8, 12, 12};
    bb_rect_t dot_max   = {(int32_t)b->width - 44, 8, 12, 12};
    bb_rect_t dot_min   = {(int32_t)b->width - 64, 8, 12, 12};
    bb_raster_fill_round_rect(&b->raster, dot_close, 6, BB_RGB(235, 87, 87));
    bb_raster_fill_round_rect(&b->raster, dot_max, 6, BB_RGB(39, 174, 96));
    bb_raster_fill_round_rect(&b->raster, dot_min, 6, BB_RGB(242, 201, 76));

    /* 3. Navigation Bar */
    bb_raster_fill_rect(&b->raster, b->nav_bar_rect, BB_COLOR_CHROME_BG);
    bb_rect_t nav_border = {0, b->nav_bar_rect.y + b->nav_bar_rect.h - 1, (int32_t)b->width, 1};
    bb_raster_fill_rect(&b->raster, nav_border, BB_COLOR_BORDER_GRAY);

    /* Back, Forward, Reload Buttons */
    bb_raster_fill_round_rect(&b->raster, b->btn_back_rect, 4, BB_COLOR_WHITE);
    bb_raster_draw_round_rect_outline(&b->raster, b->btn_back_rect, 4, 1, BB_COLOR_BORDER_GRAY);
    bb_font_draw_string(&b->raster, b->btn_back_rect.x + 10, b->btn_back_rect.y + 6, "<", BB_COLOR_DARK_GRAY, &BB_FONT_STYLE_BOLD);

    bb_raster_fill_round_rect(&b->raster, b->btn_forward_rect, 4, BB_COLOR_WHITE);
    bb_raster_draw_round_rect_outline(&b->raster, b->btn_forward_rect, 4, 1, BB_COLOR_BORDER_GRAY);
    bb_font_draw_string(&b->raster, b->btn_forward_rect.x + 10, b->btn_forward_rect.y + 6, ">", BB_COLOR_DARK_GRAY, &BB_FONT_STYLE_BOLD);

    bb_raster_fill_round_rect(&b->raster, b->btn_reload_rect, 4, BB_COLOR_WHITE);
    bb_raster_draw_round_rect_outline(&b->raster, b->btn_reload_rect, 4, 1, BB_COLOR_BORDER_GRAY);
    bb_font_draw_string(&b->raster, b->btn_reload_rect.x + 10, b->btn_reload_rect.y + 6, "R", BB_COLOR_DARK_GRAY, &BB_FONT_STYLE_BOLD);

    /* Address Bar */
    bb_raster_fill_round_rect(&b->raster, b->url_bar_rect, 4, BB_COLOR_WHITE);
    bb_color_t url_border = b->url_focused ? BB_COLOR_URLBAR_FOCUS : BB_COLOR_BORDER_GRAY;
    int32_t url_bw = b->url_focused ? 2 : 1;
    bb_raster_draw_round_rect_outline(&b->raster, b->url_bar_rect, 4, url_bw, url_border);

    /* Address Bar Text */
    bb_font_draw_string(&b->raster, b->url_bar_rect.x + 8, b->url_bar_rect.y + 6, b->url_buffer, BB_COLOR_DARK_GRAY, &BB_FONT_STYLE_REGULAR);

    /* Blinking URL Cursor */
    if (b->url_focused) {
        int32_t cursor_x = b->url_bar_rect.x + 8 + (int32_t)b->cursor_pos * 8;
        bb_rect_t cbar = {cursor_x, b->url_bar_rect.y + 5, 2, 18};
        bb_raster_fill_rect(&b->raster, cbar, BB_COLOR_URLBAR_FOCUS);
    }

    /* 4. Document Viewport with Clipping */
    bb_raster_push_clip(&b->raster, b->viewport_rect);
    if (b->layout_root) {
        bb_layout_paint(b->layout_root, &b->raster, b->viewport_rect.x, b->viewport_rect.y - b->scroll_y);
    }
    bb_raster_pop_clip(&b->raster);

    /* 5. Vertical Scrollbar */
    bb_raster_fill_rect(&b->raster, b->scrollbar_rect, BB_RGB(241, 243, 244));
    bb_rect_t scroll_div = {b->scrollbar_rect.x, b->scrollbar_rect.y, 1, b->scrollbar_rect.h};
    bb_raster_fill_rect(&b->raster, scroll_div, BB_COLOR_BORDER_GRAY);

    if (b->document_height > b->viewport_rect.h) {
        int32_t track_h = b->scrollbar_rect.h;
        int32_t thumb_h = (b->viewport_rect.h * track_h) / b->document_height;
        if (thumb_h < 24) thumb_h = 24;

        int32_t thumb_y = b->scrollbar_rect.y;
        if (b->max_scroll_y > 0) {
            thumb_y += (b->scroll_y * (track_h - thumb_h)) / b->max_scroll_y;
        }

        bb_rect_t thumb_rect = {b->scrollbar_rect.x + 2, thumb_y, b->scrollbar_rect.w - 4, thumb_h};
        bb_color_t thumb_color = b->is_dragging_scroll ? BB_RGB(130, 134, 140) : BB_RGB(180, 184, 190);
        bb_raster_fill_round_rect(&b->raster, thumb_rect, 4, thumb_color);
    }

    /* 6. Status Bar */
    bb_raster_fill_rect(&b->raster, b->status_bar_rect, BB_RGB(248, 249, 250));
    bb_rect_t status_border = {0, b->status_bar_rect.y, (int32_t)b->width, 1};
    bb_raster_fill_rect(&b->raster, status_border, BB_COLOR_BORDER_GRAY);
    bb_font_draw_string(&b->raster, 10, b->status_bar_rect.y + 3, b->status_text, BB_COLOR_DARK_GRAY, &BB_FONT_STYLE_REGULAR);

    /* 7. Draw Software Mouse Cursor */
    draw_software_cursor(b);

    /* 8. Present Framebuffer */
    if (b->fb && b->fb->pixels) {
        bb_memcpy(b->fb->pixels, b->backbuffer, b->width * b->height * sizeof(uint32_t));
        bb_osal_present_fb();
    }
}

void bb_browser_destroy(bb_browser_t *b) {
    if (!b) return;
    if (b->layout_root) bb_layout_free_tree(b->layout_root);
    if (b->stylesheet) bb_css_free(b->stylesheet);
    if (b->dom_root) bb_node_free_tree(b->dom_root);
    if (b->backbuffer) bb_free(b->backbuffer);
    bb_free(b);
}
