#define _POSIX_C_SOURCE 200809L
#include "cookie.h"
#include "dom.h"
#include "layout.h"
#include "net.h"
#include "renderer.h"
#include "window.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static const char *candidate_fonts[] = {
    "assets/font.ttf",
    "minibrowser/assets/font.ttf",
    "/Users/mohamadjlo/Desktop/FirstOS/minibrowser/assets/font.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    "/Library/Fonts/Arial.ttf",
    NULL
};

static const char *find_font_path(void) {
    for (int i = 0; candidate_fonts[i]; i++) {
        if (access(candidate_fonts[i], R_OK) == 0) {
            return candidate_fonts[i];
        }
    }
    return "assets/font.ttf";
}

static char *resolve_url(const char *base_url, const char *href) {
    if (!href) return NULL;
    if (strncmp(href, "http://", 7) == 0 ||
        strncmp(href, "https://", 8) == 0 ||
        strncmp(href, "file://", 7) == 0 ||
        strncmp(href, "modal:", 6) == 0 ||
        strncmp(href, "alert:", 6) == 0 ||
        href[0] == '/') {
        return strdup(href);
    }
    if (!base_url) return strdup(href);

    const char *last_slash = strrchr(base_url, '/');
    if (!last_slash) {
        return strdup(href);
    }

    size_t dir_len = (size_t)(last_slash - base_url + 1);
    size_t href_len = strlen(href);
    char *res = (char *)malloc(dir_len + href_len + 1);
    if (!res) return strdup(href);

    memcpy(res, base_url, dir_len);
    memcpy(res + dir_len, href, href_len);
    res[dir_len + href_len] = '\0';
    return res;
}

static char *extract_script_open_url(const char *action) {
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

static char *extract_script_alert_msg(const char *action) {
    if (!action) return NULL;
    const char *p = strstr(action, "alert(");
    if (!p) return NULL;

    p = strchr(p, '(');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\'' || *p == '\"') p++;

    const char *start = p;
    while (*p && *p != '\'' && *p != '\"' && *p != ')') p++;
    size_t len = (size_t)(p - start);
    if (len == 0) return NULL;

    char *msg = (char *)malloc(len + 1);
    if (!msg) return NULL;
    memcpy(msg, start, len);
    msg[len] = '\0';
    return msg;
}

static const DOMNode *find_enclosing_link(const DOMNode *node, const char **out_href, const char **out_target) {
    const DOMNode *curr = node;
    while (curr) {
        if (curr->type == NODE_ELEMENT && curr->tag_name) {
            if (strcasecmp(curr->tag_name, "a") == 0) {
                if (out_href) *out_href = dom_node_get_attribute(curr, "href");
                if (out_target) *out_target = dom_node_get_attribute(curr, "target");
                return curr;
            }
            if (strcasecmp(curr->tag_name, "button") == 0) {
                const char *onclick = dom_node_get_attribute(curr, "onclick");
                const char *data_modal = dom_node_get_attribute(curr, "data-modal");
                if (data_modal) {
                    if (out_href) *out_href = data_modal;
                    return curr;
                }
                if (onclick) {
                    if (out_href) *out_href = onclick;
                    return curr;
                }
            }
        }
        curr = curr->parent;
    }
    return NULL;
}

int main(int argc, char **argv) {
    const char *initial_url = "examples/demo.html";
    if (argc > 1) {
        initial_url = argv[1];
    }

    printf("=====================================================\n");
    printf("   MiniBrowser - C11 Minimal Web Browser Engine      \n");
    printf("=====================================================\n");
    printf("[main] Initializing SDL2 & SDL2_ttf...\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "[main] SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    if (TTF_Init() < 0) {
        fprintf(stderr, "[main] TTF_Init Error: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    const char *font_path = find_font_path();
    printf("[main] Loading TrueType font: %s\n", font_path);

    TTF_Font *font = TTF_OpenFont(font_path, 15);
    TTF_Font *font_bold = TTF_OpenFont(font_path, 16);
    TTF_Font *font_title = TTF_OpenFont(font_path, 24);

    if (font_bold) TTF_SetFontStyle(font_bold, TTF_STYLE_BOLD);
    if (font_title) TTF_SetFontStyle(font_title, TTF_STYLE_BOLD);

    if (!font) {
        fprintf(stderr, "[main] Warning: Failed to load font '%s': %s\n", font_path, TTF_GetError());
    }

    /* Initialize Global Shared Cookie Jar */
    printf("[main] Initializing Global Shared CookieJar...\n");
    CookieJar *shared_jar = cookie_jar_create(NULL);

    /* Pre-seed a session cookie to demonstrate shared cookie jar across windows */
    cookie_jar_set(shared_jar, "session_token=xyz987_authenticated; Path=/; Domain=localhost; HttpOnly", "localhost", "/");

    /* Initialize Window Manager */
    WindowManager *wm = window_manager_create(shared_jar, font, font_bold, font_title);
    if (!wm) {
        fprintf(stderr, "[main] Failed to create WindowManager\n");
        return 1;
    }

    /* Open Initial Browser Window */
    printf("[main] Opening primary window with URL: %s\n", initial_url);
    BrowserWindow *main_win = window_manager_open_window(wm, initial_url, "MiniBrowser", 960, 680);
    if (!main_win) {
        fprintf(stderr, "[main] Failed to open main window\n");
        return 1;
    }

    SDL_Cursor *arrow_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    SDL_Cursor *hand_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT: {
                    running = false;
                    break;
                }

                case SDL_WINDOWEVENT: {
                    Uint32 wid = event.window.windowID;
                    BrowserWindow *win = window_manager_get_window(wm, wid);
                    if (!win) break;

                    if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                        printf("[main] Window %u closed\n", wid);
                        browser_window_close(wm, wid);
                        if (!wm->windows) {
                            printf("[main] All windows closed, exiting event loop.\n");
                            running = false;
                        }
                    } else if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                               event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        int w = event.window.data1;
                        int h = event.window.data2;
                        browser_window_resize(win, w, h);
                    } else if (event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                        win->needs_redraw = true;
                    }
                    break;
                }

                case SDL_MOUSEWHEEL: {
                    Uint32 wid = event.wheel.windowID;
                    BrowserWindow *win = window_manager_get_window(wm, wid);
                    if (!win) break;

                    /* If modal dialog is active, trap focus and ignore scroll */
                    if (win->modal.active) {
                        break;
                    }

                    win->scroll_y -= event.wheel.y * 36;
                    if (win->scroll_y < 0) win->scroll_y = 0;
                    if (win->scroll_y > win->max_scroll_y) win->scroll_y = win->max_scroll_y;
                    win->needs_redraw = true;
                    break;
                }

                case SDL_MOUSEMOTION: {
                    Uint32 wid = event.motion.windowID;
                    BrowserWindow *win = window_manager_get_window(wm, wid);
                    if (!win) break;

                    if (win->modal.active) {
                        /* Check if hover over OK button */
                        float mx = (float)event.motion.x;
                        float my = (float)event.motion.y;
                        if (mx >= win->modal.ok_button.x && mx <= win->modal.ok_button.x + win->modal.ok_button.w &&
                            my >= win->modal.ok_button.y && my <= win->modal.ok_button.y + win->modal.ok_button.h) {
                            SDL_SetCursor(hand_cursor);
                        } else {
                            SDL_SetCursor(arrow_cursor);
                        }
                        break;
                    }

                    float doc_x = (float)event.motion.x;
                    float doc_y = (float)event.motion.y + (float)win->scroll_y;
                    const LayoutBox *hit = layout_hit_test(win->layout_root, doc_x, doc_y);

                    const char *href = NULL;
                    if (hit && find_enclosing_link(hit->node, &href, NULL)) {
                        SDL_SetCursor(hand_cursor);
                    } else {
                        SDL_SetCursor(arrow_cursor);
                    }
                    break;
                }

                case SDL_MOUSEBUTTONDOWN: {
                    if (event.button.button != SDL_BUTTON_LEFT) break;

                    Uint32 wid = event.button.windowID;
                    BrowserWindow *win = window_manager_get_window(wm, wid);
                    if (!win) break;

                    float mx = (float)event.button.x;
                    float my = (float)event.button.y;

                    /* 1. Modal Dialog Active: Trap all focus inside modal dialog */
                    if (win->modal.active) {
                        if (mx >= win->modal.ok_button.x && mx <= win->modal.ok_button.x + win->modal.ok_button.w &&
                            my >= win->modal.ok_button.y && my <= win->modal.ok_button.y + win->modal.ok_button.h) {
                            printf("[main] Modal OK button clicked -> Dismissing modal\n");
                            browser_window_dismiss_modal(win);
                        }
                        /* Modal dialog traps all clicks - do not process page clicks */
                        break;
                    }

                    /* 2. Page Hit-Testing */
                    float doc_x = mx;
                    float doc_y = my + (float)win->scroll_y;
                    const LayoutBox *hit = layout_hit_test(win->layout_root, doc_x, doc_y);

                    if (hit) {
                        const char *href = NULL;
                        const char *target = NULL;
                        const DOMNode *link_node = find_enclosing_link(hit->node, &href, &target);

                        if (link_node && href) {
                            printf("[main] Clicked element with action: '%s', target: '%s'\n",
                                   href, target ? target : "(self)");

                            char *script_url = extract_script_open_url(href);
                            char *script_alert = extract_script_alert_msg(href);

                            if (script_url) {
                                char *resolved = resolve_url(win->current_url, script_url);
                                printf("[main] Script trigger window.open() -> Spawning auxiliary window: %s\n", resolved);
                                window_manager_open_window(wm, resolved, "Script Auxiliary Window", 850, 600);
                                free(resolved);
                                free(script_url);
                            } else if (script_alert) {
                                browser_window_show_modal(win, "Alert", script_alert, NULL, NULL);
                                free(script_alert);
                            } else if (strncmp(href, "modal:", 6) == 0) {
                                browser_window_show_modal(win, "Notification", href + 6, NULL, NULL);
                            } else if (strncmp(href, "alert:", 6) == 0) {
                                browser_window_show_modal(win, "Alert Dialog", href + 6, NULL, NULL);
                            } else {
                                char *resolved = resolve_url(win->current_url, href);
                                if (target && strcmp(target, "_blank") == 0) {
                                    printf("[main] Spawning auxiliary window (_blank) sharing global CookieJar: %s\n", resolved);
                                    window_manager_open_window(wm, resolved, "Auxiliary Window", 850, 600);
                                } else {
                                    printf("[main] Navigating current window to: %s\n", resolved);
                                    browser_window_navigate(win, wm, resolved);
                                }
                                free(resolved);
                            }
                        }
                    }
                    break;
                }

                case SDL_KEYDOWN: {
                    if (event.key.keysym.sym == SDLK_ESCAPE) {
                        Uint32 wid = event.key.windowID;
                        BrowserWindow *win = window_manager_get_window(wm, wid);
                        if (win && win->modal.active) {
                            browser_window_dismiss_modal(win);
                        }
                    }
                    break;
                }

                default:
                    break;
            }
        }

        /* Render all windows that need redrawing */
        for (BrowserWindow *w = wm->windows; w; w = w->next) {
            if (w->needs_redraw) {
                renderer_paint_window(w, wm);
            }
        }

        SDL_Delay(10);
    }

    printf("[main] Cleaning up resources...\n");
    if (arrow_cursor) SDL_FreeCursor(arrow_cursor);
    if (hand_cursor) SDL_FreeCursor(hand_cursor);

    window_manager_free(wm);
    cookie_jar_free(shared_jar);

    if (font) TTF_CloseFont(font);
    if (font_bold) TTF_CloseFont(font_bold);
    if (font_title) TTF_CloseFont(font_title);

    TTF_Quit();
    SDL_Quit();

    printf("[main] MiniBrowser terminated successfully.\n");
    return 0;
}
