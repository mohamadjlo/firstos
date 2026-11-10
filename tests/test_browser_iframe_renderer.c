#include <assert.h>
#include <stdint.h>
#include <stddef.h>

int printf(const char *format, ...);
void *malloc(size_t size);
void free(void *ptr);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

#include "userspace/browser/include/firstos_renderer.h"

/* Include adapter implementations directly for unit testing */
#include "html5_arena.c"
#include "userspace/browser/dom_adapter.c"
#include "userspace/browser/html_adapter.c"
#include "userspace/browser/css_adapter.c"
#include "userspace/browser/layout_adapter.c"
#include "userspace/browser/render_adapter.c"
#include "userspace/browser/form_adapter.c"
#include "userspace/browser/js_adapter.c"
#include "userspace/browser/event_adapter.c"

static int s_renderer_pass = 0;
#define CHECK_R(c) do { assert(c); s_renderer_pass++; } while (0)

static void test_iframe_html_parsing(void) {
    printf("[test_iframe_renderer] 1. test_iframe_html_parsing...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<div>"
                       "<iframe src=\"http://example.com/embed\" width=\"400\" height=\"200\" "
                       "title=\"Widget Frame\" name=\"widget1\">"
                       "<p>Fallback text</p>"
                       "</iframe>"
                       "</div>";

    int res = firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    CHECK_R(res == 0);

    html_node_t *iframe = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_IFRAME);
    CHECK_R(iframe != NULL);
    CHECK_R(iframe->tag == HTML_TAG_IFRAME);
    CHECK_R(iframe->src != NULL);
    CHECK_R(strcmp(iframe->src, "http://example.com/embed") == 0);
    CHECK_R(iframe->req_width == 400);
    CHECK_R(iframe->req_height == 200);
    CHECK_R(iframe->frame_title != NULL);
    CHECK_R(strcmp(iframe->frame_title, "Widget Frame") == 0);
    CHECK_R(iframe->name != NULL);
    CHECK_R(strcmp(iframe->name, "widget1") == 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (iframe parsed with src, dimensions, title, and name)\n");
}

static void test_iframe_default_dimensions_and_layout(void) {
    printf("[test_iframe_renderer] 2. test_iframe_default_dimensions_and_layout...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<div><iframe src=\"about:blank\"></iframe></div>";
    int res = firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    CHECK_R(res == 0);

    firstos_renderer_apply_styles(&ctx);
    firstos_renderer_layout(&ctx, 800, 600);

    html_node_t *iframe = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_IFRAME);
    CHECK_R(iframe != NULL);
    /* Default replaced dimension for iframe: 300 x 150 */
    CHECK_R(iframe->bounds.w == 300);
    CHECK_R(iframe->bounds.h == 150);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Default replaced-element 300x150 layout computed)\n");
}

static void test_iframe_css_styling(void) {
    printf("[test_iframe_renderer] 3. test_iframe_css_styling...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<style>iframe { width: 450px; height: 250px; }</style>"
                       "<div><iframe src=\"about:blank\"></iframe></div>";
    int res = firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    CHECK_R(res == 0);

    firstos_renderer_apply_styles(&ctx);
    firstos_renderer_layout(&ctx, 800, 600);

    html_node_t *iframe = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_IFRAME);
    CHECK_R(iframe != NULL);
    CHECK_R(iframe->bounds.w == 450);
    CHECK_R(iframe->bounds.h == 250);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (CSS stylesheet overrides iframe dimensions)\n");
}

static void test_iframe_render_clip_command(void) {
    printf("[test_iframe_renderer] 4. test_iframe_render_clip_command...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<div><iframe src=\"http://target.com/page.html\" width=\"320\" height=\"180\">"
                       "<p>Fallback Content Not Drawn</p></iframe></div>";
    int res = firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    CHECK_R(res == 0);

    firstos_renderer_apply_styles(&ctx);
    firstos_renderer_layout(&ctx, 800, 600);

    firstos_render_command_t cmds[64];
    char strings[1024];
    uint32_t num_cmds = 0;
    int gres = firstos_renderer_generate_commands(&ctx, cmds, 64, strings, sizeof(strings), &num_cmds);
    CHECK_R(gres == 0);
    CHECK_R(num_cmds > 0);

    /* Verify CLIP command is present and contains the iframe src in strings */
    int found_clip = 0;
    for (uint32_t i = 0; i < num_cmds; i++) {
        if (cmds[i].type == FIRSTOS_RENDER_CMD_CLIP) {
            found_clip = 1;
            CHECK_R(cmds[i].w == 320 - 2); /* 320 minus 2px border */
            CHECK_R(cmds[i].h == 180 - 2);
            CHECK_R(cmds[i].text_len > 0);
            const char *src_str = strings + cmds[i].text_offset;
            CHECK_R(strcmp(src_str, "http://target.com/page.html") == 0);
        }
    }
    CHECK_R(found_clip == 1);

    /* Verify fallback content "Fallback Content Not Drawn" is NOT emitted */
    CHECK_R(strstr(strings, "Fallback Content Not Drawn") == NULL);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (CLIP command emitted with src string and fallback content suppressed)\n");
}

static void test_js_postmessage_outbox(void) {
    printf("[test_iframe_renderer] 5. test_js_postmessage_outbox...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);
    firstos_js_init();

    const char *script1 = "window.parent.postMessage('token123', 'http://parent.com');";
    firstos_js_eval(&ctx, script1);

    int kind = -1;
    char data[256];
    char target_origin[128];
    int taken = firstos_js_take_outbound_message(&kind, data, sizeof(data), target_origin, sizeof(target_origin));
    CHECK_R(taken == 1);
    CHECK_R(kind == 1); /* parent */
    CHECK_R(strcmp(data, "token123") == 0);
    CHECK_R(strcmp(target_origin, "http://parent.com") == 0);

    /* Queue should now be empty */
    CHECK_R(firstos_js_take_outbound_message(NULL, NULL, 0, NULL, 0) == 0);

    /* Test window.top.postMessage */
    const char *script2 = "window.top.postMessage('top_alert', 'http://top.com');";
    firstos_js_eval(&ctx, script2);
    taken = firstos_js_take_outbound_message(&kind, data, sizeof(data), target_origin, sizeof(target_origin));
    CHECK_R(taken == 1);
    CHECK_R(kind == 2); /* top */
    CHECK_R(strcmp(data, "top_alert") == 0);
    CHECK_R(strcmp(target_origin, "http://top.com") == 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (window.parent and window.top postMessage properly queued in outbox)\n");
}

static void test_js_outbox_queue_capacity(void) {
    printf("[test_iframe_renderer] 6. test_js_outbox_queue_capacity...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);
    firstos_js_init();

    /* Fill outbox capacity (JS_OUTBOX_MAX = 8) */
    for (int i = 0; i < 8; i++) {
        firstos_js_eval(&ctx, "parent.postMessage('msg', 'http://parent.com');");
    }
    CHECK_R(firstos_js_outbound_dropped() == 0);

    /* 9th message must be dropped */
    firstos_js_eval(&ctx, "parent.postMessage('overflow', 'http://parent.com');");
    CHECK_R(firstos_js_outbound_dropped() > 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Outbox capacity bounded and overflows safely dropped)\n");
}

static void test_js_message_listener_and_delivery(void) {
    printf("[test_iframe_renderer] 7. test_js_message_listener_and_delivery...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);
    firstos_js_init();

    const char *setup_script =
        "var received_data = 'none';\n"
        "var received_origin = 'none';\n"
        "function onMessage() {\n"
        "    received_data = event.data;\n"
        "    received_origin = event.origin;\n"
        "}\n"
        "window.addEventListener('message', onMessage);\n";

    firstos_js_eval(&ctx, setup_script);

    /* Deliver inbound message */
    int delivered = firstos_js_deliver_message(&ctx, "payload_hello", "http://sender.com");
    CHECK_R(delivered == 1);

    /* Verify variables set by listener callback */
    firstos_js_eval(&ctx, "document.title = received_data;");
    CHECK_R(strcmp(ctx.doc_title, "payload_hello") == 0);

    firstos_js_eval(&ctx, "document.title = received_origin;");
    CHECK_R(strcmp(ctx.doc_title, "http://sender.com") == 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Inbound message delivered to window message listener with event.data and event.origin)\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 5C: Iframe & postMessage (Renderer Adapter)\n");
    printf("============================================================\n");

    test_iframe_html_parsing();
    test_iframe_default_dimensions_and_layout();
    test_iframe_css_styling();
    test_iframe_render_clip_command();
    test_js_postmessage_outbox();
    test_js_outbox_queue_capacity();
    test_js_message_listener_and_delivery();

    printf("============================================================\n");
    printf("ALL 7 PHASE 5C RENDERER TESTS PASSED (%d assertions)\n", s_renderer_pass);
    printf("============================================================\n");
    return 0;
}
