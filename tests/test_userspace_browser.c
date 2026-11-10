#include <assert.h>
#include <stdint.h>
#include <stddef.h>

int printf(const char *format, ...);
void *malloc(size_t size);
void free(void *ptr);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
char *strcpy(char *dest, const char *src);

#include "userspace/browser/include/firstos_renderer.h"

/* Include adapter source files directly for host unit testing */
#include "html5_arena.c"
#include "userspace/browser/dom_adapter.c"
#include "userspace/browser/html_adapter.c"
#include "userspace/browser/css_adapter.c"
#include "userspace/browser/layout_adapter.c"
#include "userspace/browser/render_adapter.c"
#include "userspace/browser/form_adapter.c"
#include "userspace/browser/js_adapter.c"
#include "userspace/browser/ax_adapter.c"
#include "userspace/browser/event_adapter.c"

static void test_lifecycle_and_init(void) {
    printf("[test_userspace_browser] 1. test_lifecycle_and_init...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);
    assert(ctx.node_count == 0);
    assert(ctx.root_dom == NULL);
    assert(ctx.doc_height == 0);
    assert(ctx.viewport_w == 800);
    assert(ctx.viewport_h == 600);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Context init & destruction clean)\n");
}

static void test_html_parsing_and_dom(void) {
    printf("[test_userspace_browser] 2. test_html_parsing_and_dom...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = 
        "<html><head><title>Test Page</title></head>"
        "<body>"
        "  <h1 id=\"title\">FirstOS Browser</h1>"
        "  <p class=\"lead\">Paragraph &amp; text &lt;sample&gt;</p>"
        "  <a href=\"http://firstos.org\">Official Site</a>"
        "</body></html>";

    int res = firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    assert(res == RENDER_OK);
    assert(ctx.root_dom != NULL);
    assert(strcmp(ctx.doc_title, "Test Page") == 0);

    html_node_t *h1 = firstos_renderer_get_element_by_id(&ctx, "title");
    assert(h1 != NULL);
    assert(h1->tag == HTML_TAG_H1);

    html_node_t *p = firstos_renderer_get_element_by_class(&ctx, "lead");
    assert(p != NULL);
    assert(p->tag == HTML_TAG_P);

    html_node_t *a = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_A);
    assert(a != NULL);
    assert(a->href != NULL);
    assert(strcmp(a->href, "http://firstos.org") == 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (DOM tree structure, tags, and attributes verified)\n");
}

static void test_entity_decoding(void) {
    printf("[test_userspace_browser] 3. test_entity_decoding...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<div>Rock &amp; Roll &lt;FirstOS&gt; &quot;Native&quot;</div>";
    int res = firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    assert(res == RENDER_OK);

    html_node_t *div = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_DIV);
    assert(div != NULL);
    assert(div->first_child != NULL);
    assert(div->first_child->tag == HTML_TAG_TEXT_NODE);
    assert(strcmp(div->first_child->text, "Rock & Roll <FirstOS> \"Native\"") == 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Entity decoding &amp;, &lt;, &gt;, &quot; verified)\n");
}

static void test_css_parsing_and_cascade(void) {
    printf("[test_userspace_browser] 4. test_css_parsing_and_cascade...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = 
        "<style>"
        "  body { background-color: #121212; color: #FFFFFF; }"
        "  h1 { color: red; font-size: 24px; }"
        "  .card { background-color: blue; padding: 10px; width: 400px; }"
        "  #alert { color: green; }"
        "</style>"
        "<body>"
        "  <div class=\"card\"><h1 id=\"alert\">Title</h1><p>Body text</p></div>"
        "</body>";

    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    firstos_renderer_apply_styles(&ctx);

    html_node_t *body = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_BODY);
    assert(body != NULL);
    assert(body->style.has_bg == 1);
    assert(body->style.bg_color == 0x121212);

    html_node_t *card = firstos_renderer_get_element_by_class(&ctx, "card");
    assert(card != NULL);
    assert(card->style.bg_color == 0x0A84FF); /* blue */
    assert(card->style.padding_top == 10);
    assert(card->style.width == 400);

    html_node_t *h1 = firstos_renderer_get_element_by_id(&ctx, "alert");
    assert(h1 != NULL);
    assert(h1->style.color == 0x30D158); /* green (#alert overrides h1) */

    /* Check inheritance: p inside body should inherit #FFFFFF color */
    html_node_t *p = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_P);
    assert(p != NULL);
    assert(p->style.color == 0xFFFFFF);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (CSS selectors, specificity, and inheritance verified)\n");
}

static void test_layout_calculation(void) {
    printf("[test_userspace_browser] 5. test_layout_calculation...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = 
        "<style>"
        "  body { margin: 0; padding: 0; }"
        "  .box { width: 400px; height: 100px; margin: 20px; }"
        "</style>"
        "<body>"
        "  <div class=\"box\"><p>Inside box</p></div>"
        "</body>";

    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    firstos_renderer_apply_styles(&ctx);
    firstos_renderer_layout(&ctx, 800, 600);

    html_node_t *box = firstos_renderer_get_element_by_class(&ctx, "box");
    assert(box != NULL);
    assert(box->bounds.x == 20);
    assert(box->bounds.y == 20);
    assert(box->bounds.w == 400);
    assert(box->bounds.h == 100);

    assert(ctx.doc_height >= 140);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Box model margins, dimensions, and document height verified)\n");
}

static void test_render_command_generation(void) {
    printf("[test_userspace_browser] 6. test_render_command_generation...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = 
        "<style>"
        "  body { background-color: #202028; }"
        "  .banner { background-color: #303040; width: 600px; height: 40px; }"
        "</style>"
        "<body>"
        "  <div class=\"banner\"><h1>Heading Text</h1></div>"
        "</body>";

    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    firstos_renderer_apply_styles(&ctx);
    firstos_renderer_layout(&ctx, 800, 600);

    firstos_render_command_t cmds[64];
    char strings[1024];
    uint32_t num_cmds = 0;

    int res = firstos_renderer_generate_commands(&ctx, cmds, 64, strings, 1024, &num_cmds);
    assert(res == RENDER_OK);
    assert(num_cmds >= 3); /* CLEAR, banner RECT, TEXT, END */

    assert(cmds[0].type == FIRSTOS_RENDER_CMD_CLEAR);
    assert(cmds[0].color == 0x202028);

    assert(cmds[num_cmds - 1].type == FIRSTOS_RENDER_CMD_END);

    /* Verify TEXT command exists and string content matches */
    int found_text = 0;
    for (uint32_t i = 0; i < num_cmds; i++) {
        if (cmds[i].type == FIRSTOS_RENDER_CMD_TEXT) {
            found_text = 1;
            const char *str = strings + cmds[i].text_offset;
            assert(strcmp(str, "Heading Text") == 0);
            break;
        }
    }
    assert(found_text == 1);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Render command buffer & string payload generated cleanly)\n");
}

static void test_scrolling_and_event_handling(void) {
    printf("[test_userspace_browser] 7. test_scrolling_and_event_handling...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    /* Construct a long document that requires scrolling */
    const char *html = 
        "<style>"
        "  .long { height: 1200px; width: 600px; }"
        "</style>"
        "<body>"
        "  <div class=\"long\"><a href=\"http://target.com\">Bottom Link</a></div>"
        "</body>";

    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    firstos_renderer_apply_styles(&ctx);
    firstos_renderer_layout(&ctx, 800, 600);

    assert(ctx.doc_height >= 1200);

    /* Scroll down 100px */
    int changed = firstos_renderer_handle_scroll(&ctx, 100);
    assert(changed == 1);
    assert(ctx.scroll_y == 100);

    /* Scroll cannot exceed doc_height - viewport_h */
    firstos_renderer_handle_scroll(&ctx, 2000);
    assert(ctx.scroll_y == ctx.doc_height - 600);

    /* Scroll cannot be negative */
    firstos_renderer_handle_scroll(&ctx, -5000);
    assert(ctx.scroll_y == 0);

    /* Click hit testing */
    html_node_t *a = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_A);
    assert(a != NULL);
    char nav_url[128];
    int hit = firstos_renderer_handle_click(&ctx, a->bounds.x + 2, a->bounds.y + 2, nav_url, sizeof(nav_url), NULL, 0, NULL, 0);
    assert(hit == 1);
    assert(strcmp(nav_url, "http://target.com") == 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Scroll clamping & link hit-testing verified)\n");
}

static void test_malformed_html_recovery(void) {
    printf("[test_userspace_browser] 8. test_malformed_html_recovery...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    /* Unclosed tags, mismatched tags, and extra brackets */
    const char *html = "<div><p>Unclosed paragraph<h1>Mismatched</div><br>Trailing";
    int res = firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    assert(res == RENDER_OK);
    assert(ctx.root_dom != NULL);
    assert(ctx.node_count > 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Malformed HTML handled safely without crashing)\n");
}

static void test_damage_and_frame_tracking(void) {
    printf("[test_userspace_browser] 9. test_damage_and_frame_tracking...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    /* Initially needs full repaint */
    assert(ctx.needs_full_repaint == 1);
    assert(ctx.damage_rect.w == 800);
    assert(ctx.damage_rect.h == 600);
    assert(ctx.frame_count == 0);

    /* Populate document */
    const char *html = "<html><body><div style=\"height:2000px;\">Tall content</div></body></html>";
    int res = firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    assert(res == RENDER_OK);

    /* After reset, full repaint is flagged again */
    firstos_renderer_reset(&ctx);
    assert(ctx.needs_full_repaint == 1);
    assert(ctx.frame_count == 0);

    /* Test scrolling updates damage and needs_full_repaint */
    ctx.needs_full_repaint = 0;
    ctx.doc_height = 2000;
    int scrolled = firstos_renderer_handle_scroll(&ctx, 50);
    assert(scrolled == 1);
    assert(ctx.needs_full_repaint == 1);
    assert(ctx.damage_rect.w == 800);
    assert(ctx.damage_rect.h == 600);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Damage tracking and frame state invariants verified)\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 4B: Userspace Browser Engine Unit Tests\n");
    printf("============================================================\n");

    test_lifecycle_and_init();
    test_html_parsing_and_dom();
    test_entity_decoding();
    test_css_parsing_and_cascade();
    test_layout_calculation();
    test_render_command_generation();
    test_scrolling_and_event_handling();
    test_malformed_html_recovery();
    test_damage_and_frame_tracking();

    printf("============================================================\n");
    printf("ALL 9 USERSPACE BROWSER UNIT TESTS PASSED (9/9)\n");
    printf("============================================================\n");
    return 0;
}
