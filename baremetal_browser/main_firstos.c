#ifndef BB_HOST_BUILD

#include "bb_types.h"
#include "bb_libc.h"
#include "bb_osal.h"
#include "bb_browser.h"

void _start(void) {
    bb_osal_log("[FirstOS Browser] Starting bare-metal userspace browser...\n");

    /* Initialize freestanding OSAL with standard 1024x768 resolution */
    if (bb_osal_init(1024, 768) != 0) {
        bb_osal_log("[FirstOS Browser] Fatal: failed to initialize OSAL\n");
        while (1) {
            __asm__ volatile ("int $0x81");
        }
    }

    /* Instantiate Browser Engine */
    bb_browser_t *browser = bb_browser_create(1024, 768);
    if (!browser) {
        bb_osal_log("[FirstOS Browser] Fatal: failed to instantiate browser\n");
        while (1) {
            __asm__ volatile ("int $0x81");
        }
    }

    bb_osal_log("[FirstOS Browser] Rendering initial view...\n");
    bb_browser_render(browser);

    /* Main Event & Render Loop */
    bb_event_t ev;
    while (1) {
        bool had_event = false;
        while (bb_osal_poll_event(&ev)) {
            had_event = true;
            bb_browser_handle_event(browser, &ev);
        }

        if (had_event || browser->needs_repaint) {
            browser->needs_repaint = false;
            bb_browser_render(browser);
        }

        /* Yield time slice to FirstOS kernel scheduler */
        __asm__ volatile ("int $0x81");
    }
}

#endif /* !BB_HOST_BUILD */
