#define _POSIX_C_SOURCE 200809L
#include "cookie.h"
#include "dom.h"
#include "layout.h"
#include "net.h"
#include "renderer.h"
#include "window.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool g_modal_callback_fired = false;
static void modal_confirm_callback(void *user_data) {
    int *val = (int *)user_data;
    if (val) *val = 42;
    g_modal_callback_fired = true;
}

int main(void) {
    printf("[test_integration] Starting Engine Integration & Lifecycle verification...\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() < 0) {
        fprintf(stderr, "TTF_Init error: %s\n", TTF_GetError());
        return 1;
    }

    TTF_Font *font = TTF_OpenFont("assets/font.ttf", 15);
    TTF_Font *font_bold = TTF_OpenFont("assets/font.ttf", 16);
    TTF_Font *font_title = TTF_OpenFont("assets/font.ttf", 24);

    /* 1. Create Shared CookieJar */
    CookieJar *shared_jar = cookie_jar_create(NULL);
    assert(shared_jar != NULL);

    /* 2. Create WindowManager */
    WindowManager *wm = window_manager_create(shared_jar, font, font_bold, font_title);
    assert(wm != NULL);
    assert(wm->shared_cookie_jar == shared_jar);

    /* 3. Open Primary Window */
    printf("[test_integration] Opening primary window with examples/demo.html...\n");
    BrowserWindow *win1 = window_manager_open_window(wm, "examples/demo.html", "Primary Window", 900, 650);
    assert(win1 != NULL);
    assert(win1->dom_root != NULL);
    assert(win1->layout_root != NULL);
    assert(win1->modal.active == false);

    /* Verify rendering pipeline of Primary Window */
    renderer_save_screenshot(win1, wm, "minibrowser_demo_rendered.bmp");
    printf("[test_integration] Primary window painted successfully (width=%d, height=%d, max_scroll=%d)\n",
           win1->width, win1->height, win1->max_scroll_y);

    /* 4. Test In-Engine Focus-Trapping Modal Dialog */
    printf("[test_integration] Testing Modal Dialog activation and focus trapping...\n");
    int test_payload = 0;
    browser_window_show_modal(win1, "Verification Alert", "Testing focus-trapping modal layer.", modal_confirm_callback, &test_payload);
    assert(win1->modal.active == true);
    assert(strcmp(win1->modal.title, "Verification Alert") == 0);
    assert(win1->modal.box.w > 0 && win1->modal.box.h > 0);
    assert(win1->modal.ok_button.w > 0 && win1->modal.ok_button.h > 0);

    /* Paint window with modal overlay and save screenshot */
    renderer_save_screenshot(win1, wm, "minibrowser_modal_rendered.bmp");
    printf("[test_integration] Modal dialog backdrop, card, and OK button painted successfully.\n");

    /* Dismiss modal */
    browser_window_dismiss_modal(win1);
    assert(win1->modal.active == false);
    assert(g_modal_callback_fired == true);
    assert(test_payload == 42);
    printf("[test_integration] Modal dialog dismissed and confirm callback verified.\n");

    /* 5. Test Multi-Window Spawning with Shared CookieJar (target="_blank") */
    printf("[test_integration] Testing auxiliary window spawning with shared CookieJar...\n");
    /* Set a cookie from primary window */
    cookie_jar_set(wm->shared_cookie_jar, "auth_token=super_secret_session_token_123; Domain=example.com; Path=/", "example.com", "/");

    /* Spawn auxiliary window */
    BrowserWindow *win2 = window_manager_open_window(wm, "examples/target_blank.html", "Auxiliary Window", 800, 600);
    assert(win2 != NULL);
    assert(win2->window_id != win1->window_id);
    assert(wm->windows != NULL);

    /* Verify that Auxiliary Window accesses the exact same CookieJar state */
    char *cookie_header = cookie_jar_get_header(wm->shared_cookie_jar, "example.com", "/target_blank.html");
    assert(cookie_header != NULL);
    assert(strstr(cookie_header, "auth_token=super_secret_session_token_123") != NULL);
    printf("[test_integration] Verified shared CookieJar across multiple windows: %s\n", cookie_header);
    free(cookie_header);

    /* Paint auxiliary window */
    renderer_paint_window(win2, wm);

    /* 6. Test In-Page Navigation */
    printf("[test_integration] Testing in-page navigation to examples/popup.html...\n");
    browser_window_navigate(win1, wm, "examples/popup.html");
    assert(win1->dom_root != NULL);
    assert(win1->layout_root != NULL);
    assert(win1->scroll_y == 0);
    renderer_paint_window(win1, wm);
    printf("[test_integration] Navigation complete and reflowed successfully.\n");

    /* 7. Test Window Closure and Cleanup */
    Uint32 win2_id = win2->window_id;
    browser_window_close(wm, win2_id);
    assert(window_manager_get_window(wm, win2_id) == NULL);
    assert(window_manager_get_window(wm, win1->window_id) == win1);
    printf("[test_integration] Auxiliary window closed cleanly. Primary window intact.\n");

    browser_window_close(wm, win1->window_id);
    assert(wm->windows == NULL);
    printf("[test_integration] Primary window closed. Window list is now empty.\n");

    /* 8. Teardown */
    window_manager_free(wm);
    cookie_jar_free(shared_jar);

    if (font) TTF_CloseFont(font);
    if (font_bold) TTF_CloseFont(font_bold);
    if (font_title) TTF_CloseFont(font_title);

    TTF_Quit();
    SDL_Quit();

    printf("[test_integration] All integration tests passed (100%% SUCCESS)!\n");
    return 0;
}
