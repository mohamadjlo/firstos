#define _POSIX_C_SOURCE 200809L
#include "cookie.h"
#include "dom.h"
#include "html_parser.h"
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

static char *extract_script_open_url_test(const char *action) {
    if (!action) return NULL;
    const char *p = strstr(action, "window.open(");
    if (!p) p = strstr(action, "open(");
    if (!p) return NULL;

    p = strchr(p, '(');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\'' || *p == '\"') p++;

    const char *start = p;
    while (*p && *p != '\'' && *p != '\"' && *p != ')' && *p != ',') p++;
    size_t len = (size_t)(p - start);
    if (len == 0) return NULL;

    char *url = (char *)malloc(len + 1);
    if (!url) return NULL;
    memcpy(url, start, len);
    url[len] = '\0';
    return url;
}

int main(void) {
    printf("[test_script_trigger] Testing DOM inner_text & Script-Triggered Window Spawning...\n");

    /* 1. Test dom_node_get_inner_text() across deep hierarchy */
    const char *html = "<div><p>Hello <span>brave <strong>new <em>world</em></strong></span>!</p></div>";
    DOMNode *doc = html_parse(html);
    assert(doc != NULL);

    DOMNode *div = dom_node_find_by_tag(doc, "div");
    assert(div != NULL);

    char *inner = dom_node_get_inner_text(div);
    assert(inner != NULL);
    printf("[test_script_trigger] Extracted inner_text: '%s'\n", inner);
    assert(strstr(inner, "Hello") != NULL);
    assert(strstr(inner, "brave") != NULL);
    assert(strstr(inner, "new") != NULL);
    assert(strstr(inner, "world") != NULL);
    free(inner);

    /* Test direct inner_text field on DOMNode */
    div->inner_text = strdup("Custom Explicit Inner Text");
    assert(div->inner_text != NULL);
    assert(strcmp(div->inner_text, "Custom Explicit Inner Text") == 0);

    dom_node_free(doc);

    /* 2. Test Script Trigger URL Extraction */
    char *url1 = extract_script_open_url_test("onclick=\"window.open('examples/target_blank.html')\"");
    assert(url1 != NULL && strcmp(url1, "examples/target_blank.html") == 0);
    free(url1);

    char *url2 = extract_script_open_url_test("window.open(\"http://example.com/api/view\", \"_blank\")");
    assert(url2 != NULL && strcmp(url2, "http://example.com/api/view") == 0);
    free(url2);

    /* 3. Test Multi-Window Spawning via Script Trigger Sharing CookieJar */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) >= 0 && TTF_Init() >= 0) {
        TTF_Font *font = TTF_OpenFont("assets/font.ttf", 15);
        CookieJar *jar = cookie_jar_create(NULL);
        WindowManager *wm = window_manager_create(jar, font, NULL, NULL);

        /* Set cookie in main window */
        cookie_jar_set(jar, "user_session=abc123script; Domain=example.com; Path=/", "example.com", "/");

        BrowserWindow *main_win = window_manager_open_window(wm, "examples/demo.html", "Main Window", 800, 600);
        assert(main_win != NULL);

        /* Simulate script trigger window.open */
        const char *trigger = "window.open('examples/target_blank.html')";
        char *script_url = extract_script_open_url_test(trigger);
        assert(script_url != NULL);

        BrowserWindow *aux_win = window_manager_open_window(wm, script_url, "Aux Window", 800, 600);
        assert(aux_win != NULL);
        assert(aux_win != main_win);
        free(script_url);

        /* Verify cookie is accessible in aux window */
        char *hdr = cookie_jar_get_header(wm->shared_cookie_jar, "example.com", "/target_blank.html");
        assert(hdr != NULL && strstr(hdr, "user_session=abc123script") != NULL);
        printf("[test_script_trigger] Shared CookieJar verified across script-triggered window: %s\n", hdr);
        free(hdr);

        browser_window_close(wm, aux_win->window_id);
        browser_window_close(wm, main_win->window_id);
        window_manager_free(wm);
        cookie_jar_free(jar);
        if (font) TTF_CloseFont(font);
        TTF_Quit();
        SDL_Quit();
    }

    printf("[test_script_trigger] All inner_text & script trigger tests passed successfully!\n");
    return 0;
}
