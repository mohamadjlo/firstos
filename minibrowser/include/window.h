#ifndef WINDOW_H
#define WINDOW_H

#include "cookie.h"
#include "dom.h"
#include "layout.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>

typedef struct ModalDialog {
    bool active;
    char *title;
    char *message;
    Rect box;
    Rect ok_button;
    void (*on_confirm)(void *user_data);
    void *user_data;
} ModalDialog;

typedef struct BrowserWindow {
    Uint32 window_id;
    SDL_Window *sdl_window;
    SDL_Renderer *sdl_renderer;

    DOMNode *dom_root;
    LayoutBox *layout_root;

    int scroll_y;
    int max_scroll_y;
    int width;
    int height;
    char *current_url;

    ModalDialog modal;

    bool is_open;
    bool needs_redraw;

    struct BrowserWindow *next;
} BrowserWindow;

typedef struct WindowManager {
    BrowserWindow *windows;
    CookieJar *shared_cookie_jar;
    TTF_Font *font;
    TTF_Font *font_bold;
    TTF_Font *font_title;
} WindowManager;

WindowManager *window_manager_create(CookieJar *shared_jar, TTF_Font *font, TTF_Font *font_bold, TTF_Font *font_title);
void window_manager_free(WindowManager *wm);

BrowserWindow *window_manager_open_window(WindowManager *wm, const char *url, const char *title, int w, int h);
void browser_window_close(WindowManager *wm, Uint32 window_id);
BrowserWindow *window_manager_get_window(WindowManager *wm, Uint32 window_id);

void browser_window_navigate(BrowserWindow *win, WindowManager *wm, const char *url);
void browser_window_show_modal(BrowserWindow *win, const char *title, const char *message, void (*on_confirm)(void *), void *user_data);
void browser_window_dismiss_modal(BrowserWindow *win);
void browser_window_resize(BrowserWindow *win, int w, int h);

#endif /* WINDOW_H */
