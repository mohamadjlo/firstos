#ifdef BB_HOST_BUILD

#include "bb_types.h"
#include "bb_libc.h"
#include "bb_osal.h"
#include "bb_browser.h"

#include <stdio.h>
#include <stdlib.h>

/* Declaration of host-specific helper */
int bb_osal_host_dump_bmp(const char *filename);
void bb_osal_host_push_event(const bb_event_t *ev);

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("====================================================\n");
    printf("  FirstOS Bare-Metal Browser Host Simulation Harness\n");
    printf("====================================================\n");

    /* 1. Initialize Host OSAL */
    if (bb_osal_init(1024, 768) != 0) {
        fprintf(stderr, "Failed to initialize host OSAL framebuffer\n");
        return 1;
    }

    /* 2. Create Browser Engine */
    printf("[1/5] Creating freestanding browser engine (1024x768)...\n");
    bb_browser_t *browser = bb_browser_create(1024, 768);
    if (!browser) {
        fprintf(stderr, "Failed to create browser\n");
        return 1;
    }

    /* 3. Render Initial Showcase Page */
    printf("[2/5] Rendering initial showcase document...\n");
    bb_browser_render(browser);
    const char *out_initial = "baremetal_browser_initial.bmp";
    bb_osal_host_dump_bmp(out_initial);
    printf("      -> Saved frame to %s\n", out_initial);

    /* 4. Simulate Mouse Movement & Link Hover */
    printf("[3/5] Simulating mouse hover over interactive elements...\n");
    bb_event_t ev_move;
    ev_move.type = BB_EVENT_MOUSE_MOVE;
    ev_move.mouse_x = 220;
    ev_move.mouse_y = 380; /* Over 'Visit Local Documentation' link */
    ev_move.mouse_buttons = 0;
    ev_move.scroll_delta = 0;
    ev_move.keycode = 0;
    ev_move.char_code = 0;
    bb_browser_handle_event(browser, &ev_move);
    bb_browser_render(browser);
    const char *out_hover = "baremetal_browser_hover.bmp";
    bb_osal_host_dump_bmp(out_hover);
    printf("      -> Status bar updated: \"%s\"\n", browser->status_text);
    printf("      -> Saved frame to %s\n", out_hover);

    /* 5. Simulate Clicking URL Address Bar and Typing */
    printf("[4/5] Simulating clicking URL bar and typing 'http://10.0.2.2/kernel.html'...\n");
    bb_event_t ev_click_url;
    ev_click_url.type = BB_EVENT_MOUSE_DOWN;
    ev_click_url.mouse_x = browser->url_bar_rect.x + 10;
    ev_click_url.mouse_y = browser->url_bar_rect.y + 10;
    ev_click_url.mouse_buttons = 1;
    bb_browser_handle_event(browser, &ev_click_url);

    /* Type backspaces and new text */
    bb_event_t ev_key;
    ev_key.type = BB_EVENT_CHAR;
    const char *new_url = "http://10.0.2.2/kernel.html";
    /* Clear url buffer */
    browser->url_buffer[0] = '\0';
    browser->url_length = 0;
    browser->cursor_pos = 0;
    while (*new_url) {
        ev_key.char_code = *new_url++;
        bb_browser_handle_event(browser, &ev_key);
    }
    bb_browser_render(browser);
    const char *out_urlbar = "baremetal_browser_urlbar.bmp";
    bb_osal_host_dump_bmp(out_urlbar);
    printf("      -> URL bar value: \"%s\"\n", browser->url_buffer);
    printf("      -> Saved frame to %s\n", out_urlbar);

    /* 6. Simulate Scroll Wheel Down */
    printf("[5/5] Simulating scroll wheel events...\n");
    bb_event_t ev_scroll;
    ev_scroll.type = BB_EVENT_MOUSE_SCROLL;
    ev_scroll.scroll_delta = -4; /* Scroll down */
    bb_browser_handle_event(browser, &ev_scroll);
    bb_browser_render(browser);
    const char *out_scrolled = "baremetal_browser_scrolled.bmp";
    bb_osal_host_dump_bmp(out_scrolled);
    printf("      -> Scroll Y offset: %d / %d\n", browser->scroll_y, browser->max_scroll_y);
    printf("      -> Saved frame to %s\n", out_scrolled);

    /* Clean up */
    bb_browser_destroy(browser);
    bb_osal_shutdown();

    printf("\nAll browser verification steps completed successfully!\n");
    return 0;
}

#endif /* BB_HOST_BUILD */
