#define _POSIX_C_SOURCE 200809L
#include "window.h"
#include "css_parser.h"
#include "dom.h"
#include "html_parser.h"
#include "layout.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

WindowManager *window_manager_create(CookieJar *shared_jar, TTF_Font *font, TTF_Font *font_bold, TTF_Font *font_title) {
    WindowManager *wm = (WindowManager *)calloc(1, sizeof(WindowManager));
    if (!wm) return NULL;
    wm->shared_cookie_jar = shared_jar;
    wm->font = font;
    wm->font_bold = font_bold;
    wm->font_title = font_title;
    wm->windows = NULL;
    return wm;
}

void window_manager_free(WindowManager *wm) {
    if (!wm) return;
    BrowserWindow *curr = wm->windows;
    while (curr) {
        BrowserWindow *next = curr->next;
        if (curr->modal.title) free(curr->modal.title);
        if (curr->modal.message) free(curr->modal.message);
        if (curr->layout_root) layout_box_free(curr->layout_root);
        if (curr->dom_root) dom_node_free(curr->dom_root);
        if (curr->current_url) free(curr->current_url);
        if (curr->sdl_renderer) SDL_DestroyRenderer(curr->sdl_renderer);
        if (curr->sdl_window) SDL_DestroyWindow(curr->sdl_window);
        free(curr);
        curr = next;
    }
    free(wm);
}

BrowserWindow *window_manager_get_window(WindowManager *wm, Uint32 window_id) {
    if (!wm) return NULL;
    for (BrowserWindow *w = wm->windows; w; w = w->next) {
        if (w->window_id == window_id) {
            return w;
        }
    }
    return NULL;
}

static void collect_style_content(DOMNode *node, char **out_buf, size_t *out_len, size_t *out_cap) {
    if (!node) return;
    if (node->type == NODE_ELEMENT && node->tag_name && strcasecmp(node->tag_name, "style") == 0) {
        for (DOMNode *child = node->first_child; child; child = child->next_sibling) {
            if (child->type == NODE_TEXT && child->text_content) {
                size_t l = strlen(child->text_content);
                if (*out_len + l + 2 > *out_cap) {
                    *out_cap = (*out_len + l + 2) * 2;
                    *out_buf = (char *)realloc(*out_buf, *out_cap);
                }
                memcpy(*out_buf + *out_len, child->text_content, l);
                *out_len += l;
                (*out_buf)[*out_len] = '\n';
                (*out_len)++;
                (*out_buf)[*out_len] = '\0';
            }
        }
    }
    for (DOMNode *child = node->first_child; child; child = child->next_sibling) {
        collect_style_content(child, out_buf, out_len, out_cap);
    }
}

static DOMNode *find_title_node(DOMNode *node) {
    if (!node) return NULL;
    if (node->type == NODE_ELEMENT && node->tag_name && strcasecmp(node->tag_name, "title") == 0) {
        return node;
    }
    for (DOMNode *child = node->first_child; child; child = child->next_sibling) {
        DOMNode *found = find_title_node(child);
        if (found) return found;
    }
    return NULL;
}

void browser_window_navigate(BrowserWindow *win, WindowManager *wm, const char *url) {
    if (!win || !wm || !url) return;

    if (win->layout_root) {
        layout_box_free(win->layout_root);
        win->layout_root = NULL;
    }
    if (win->dom_root) {
        dom_node_free(win->dom_root);
        win->dom_root = NULL;
    }
    if (win->current_url) {
        free(win->current_url);
    }
    win->current_url = strdup(url);
    win->scroll_y = 0;

    /* Fetch URL */
    HttpResponse res;
    memset(&res, 0, sizeof(HttpResponse));
    int rc = net_fetch_url(url, wm->shared_cookie_jar, &res);

    const char *html_source = NULL;
    if (rc == 0 && res.body && res.body_len > 0) {
        html_source = res.body;
    } else {
        html_source = "<html><head><title>Error</title></head><body>"
                      "<h1>Error Loading Page</h1>"
                      "<p>Unable to retrieve requested URL.</p>"
                      "</body></html>";
    }

    /* Parse HTML into DOM */
    win->dom_root = html_parse(html_source);

    /* Update window title if <title> is specified */
    DOMNode *title_elem = find_title_node(win->dom_root);
    if (title_elem && title_elem->first_child && title_elem->first_child->text_content) {
        char full_title[256];
        snprintf(full_title, sizeof(full_title), "%s - MiniBrowser", title_elem->first_child->text_content);
        SDL_SetWindowTitle(win->sdl_window, full_title);
    } else {
        SDL_SetWindowTitle(win->sdl_window, "MiniBrowser");
    }

    /* Extract and apply embedded styles */
    size_t style_len = 0, style_cap = 1024;
    char *style_buf = (char *)malloc(style_cap);
    if (style_buf) {
        style_buf[0] = '\0';
        collect_style_content(win->dom_root, &style_buf, &style_len, &style_cap);
        if (style_len > 0) {
            Stylesheet *sheet = css_parse(style_buf);
            if (sheet) {
                css_apply_stylesheet(win->dom_root, sheet);
                stylesheet_free(sheet);
            }
        } else {
            /* Still apply user-agent defaults */
            css_apply_stylesheet(win->dom_root, NULL);
        }
        free(style_buf);
    } else {
        css_apply_stylesheet(win->dom_root, NULL);
    }

    /* Build layout tree */
    win->layout_root = layout_build_tree(win->dom_root);
    if (win->layout_root) {
        layout_calculate(win->layout_root, (float)win->width, (float)win->height);
        int content_h = (int)win->layout_root->dimensions.h;
        win->max_scroll_y = (content_h > win->height) ? (content_h - win->height + 60) : 0;
    } else {
        win->max_scroll_y = 0;
    }

    http_response_free(&res);
    win->needs_redraw = true;
}

BrowserWindow *window_manager_open_window(WindowManager *wm, const char *url, const char *title, int w, int h) {
    if (!wm) return NULL;
    if (w <= 0) w = 900;
    if (h <= 0) h = 650;

    SDL_Window *sdl_win = SDL_CreateWindow(
        title ? title : "MiniBrowser",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        w, h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!sdl_win) {
        fprintf(stderr, "[window] Failed to create SDL_Window: %s\n", SDL_GetError());
        return NULL;
    }

    SDL_Renderer *sdl_rend = SDL_CreateRenderer(
        sdl_win,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!sdl_rend) {
        /* Fallback to software renderer */
        sdl_rend = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_SOFTWARE);
        if (!sdl_rend) {
            fprintf(stderr, "[window] Failed to create SDL_Renderer: %s\n", SDL_GetError());
            SDL_DestroyWindow(sdl_win);
            return NULL;
        }
    }

    SDL_SetRenderDrawBlendMode(sdl_rend, SDL_BLENDMODE_BLEND);

    BrowserWindow *win = (BrowserWindow *)calloc(1, sizeof(BrowserWindow));
    if (!win) {
        SDL_DestroyRenderer(sdl_rend);
        SDL_DestroyWindow(sdl_win);
        return NULL;
    }

    win->sdl_window = sdl_win;
    win->sdl_renderer = sdl_rend;
    win->window_id = SDL_GetWindowID(sdl_win);
    win->width = w;
    win->height = h;
    win->is_open = true;
    win->needs_redraw = true;

    /* Add to WM list */
    win->next = wm->windows;
    wm->windows = win;

    /* Navigate to initial URL */
    if (url) {
        browser_window_navigate(win, wm, url);
    }

    return win;
}

void browser_window_close(WindowManager *wm, Uint32 window_id) {
    if (!wm) return;
    BrowserWindow **curr = &wm->windows;
    while (*curr) {
        BrowserWindow *entry = *curr;
        if (entry->window_id == window_id) {
            *curr = entry->next;
            if (entry->modal.title) free(entry->modal.title);
            if (entry->modal.message) free(entry->modal.message);
            if (entry->layout_root) layout_box_free(entry->layout_root);
            if (entry->dom_root) dom_node_free(entry->dom_root);
            if (entry->current_url) free(entry->current_url);
            if (entry->sdl_renderer) SDL_DestroyRenderer(entry->sdl_renderer);
            if (entry->sdl_window) SDL_DestroyWindow(entry->sdl_window);
            free(entry);
            return;
        }
        curr = &(*curr)->next;
    }
}

void browser_window_show_modal(BrowserWindow *win, const char *title, const char *message, void (*on_confirm)(void *), void *user_data) {
    if (!win) return;
    if (win->modal.active) {
        browser_window_dismiss_modal(win);
    }

    win->modal.active = true;
    win->modal.title = title ? strdup(title) : strdup("Alert");
    win->modal.message = message ? strdup(message) : strdup("");
    win->modal.on_confirm = on_confirm;
    win->modal.user_data = user_data;

    float mw = (win->width > 440) ? 420.0f : (win->width - 40.0f);
    float mh = 200.0f;
    win->modal.box = (Rect){
        (win->width - mw) / 2.0f,
        (win->height - mh) / 2.0f,
        mw,
        mh
    };

    float bw = 90.0f;
    float bh = 34.0f;
    win->modal.ok_button = (Rect){
        win->modal.box.x + (mw - bw) / 2.0f,
        win->modal.box.y + mh - bh - 20.0f,
        bw,
        bh
    };

    win->needs_redraw = true;
}

void browser_window_dismiss_modal(BrowserWindow *win) {
    if (!win || !win->modal.active) return;
    void (*callback)(void *) = win->modal.on_confirm;
    void *ud = win->modal.user_data;

    if (win->modal.title) {
        free(win->modal.title);
        win->modal.title = NULL;
    }
    if (win->modal.message) {
        free(win->modal.message);
        win->modal.message = NULL;
    }
    win->modal.active = false;
    win->modal.on_confirm = NULL;
    win->modal.user_data = NULL;
    win->needs_redraw = true;

    if (callback) {
        callback(ud);
    }
}

void browser_window_resize(BrowserWindow *win, int w, int h) {
    if (!win) return;
    win->width = w;
    win->height = h;

    if (win->layout_root) {
        layout_calculate(win->layout_root, (float)w, (float)h);
        int content_h = (int)win->layout_root->dimensions.h;
        win->max_scroll_y = (content_h > h) ? (content_h - h + 60) : 0;
        if (win->scroll_y > win->max_scroll_y) {
            win->scroll_y = win->max_scroll_y;
        }
    }

    if (win->modal.active) {
        float mw = (win->width > 440) ? 420.0f : (win->width - 40.0f);
        float mh = 200.0f;
        win->modal.box = (Rect){
            (win->width - mw) / 2.0f,
            (win->height - mh) / 2.0f,
            mw,
            mh
        };
        float bw = 90.0f;
        float bh = 34.0f;
        win->modal.ok_button = (Rect){
            win->modal.box.x + (mw - bw) / 2.0f,
            win->modal.box.y + mh - bh - 20.0f,
            bw,
            bh
        };
    }

    win->needs_redraw = true;
}
