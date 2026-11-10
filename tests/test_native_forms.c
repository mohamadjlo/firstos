#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#define FB_W 800
#define FB_H 600
static unsigned int g_fb[FB_H][FB_W];

static int g_clip_x = 0, g_clip_y = 0, g_clip_w = FB_W, g_clip_h = FB_H;

void fb_set_clip(int x, int y, int w, int h) {
    g_clip_x = x; g_clip_y = y; g_clip_w = w; g_clip_h = h;
}
void fb_reset_clip(void) {
    g_clip_x = 0; g_clip_y = 0; g_clip_w = FB_W; g_clip_h = FB_H;
}

void fb_putpixel(int x, int y, unsigned int col) {
    if (x < g_clip_x || x >= g_clip_x + g_clip_w || y < g_clip_y || y >= g_clip_y + g_clip_h) return;
    if (x >= 0 && x < FB_W && y >= 0 && y < FB_H) {
        g_fb[y][x] = col | 0xFF000000;
    }
}

void fb_rect(int x, int y, int w, int h, unsigned int col) {
    for (int r = y; r < y + h; r++) {
        for (int c = x; c < x + w; c++) {
            fb_putpixel(c, r, col);
        }
    }
}

void fb_border(int x, int y, int w, int h, unsigned int col) {
    for (int c = x; c < x + w; c++) { fb_putpixel(c, y, col); fb_putpixel(c, y + h - 1, col); }
    for (int r = y; r < y + h; r++) { fb_putpixel(x, r, col); fb_putpixel(x + w - 1, r, col); }
}

void fb_round_rect(int x, int y, int w, int h, int rad, unsigned int col) {
    (void)rad;
    fb_rect(x, y, w, h, col);
}

void fb_round_rect_border(int x, int y, int w, int h, int rad, unsigned int col) {
    (void)rad;
    fb_border(x, y, w, h, col);
}

int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
char *strcpy(char *dest, const char *src);
char *strstr(const char *haystack, const char *needle);

void font_text(int x, int y, const char *txt, unsigned int col, int size) {
    if (!txt) return;
    int len = strlen(txt);
    int cw = (size == 2) ? 10 : 8;
    int ch = (size == 2) ? 20 : 14;
    for (int i = 0; i < len; i++) {
        for (int py = 0; py < ch; py += 2) {
            for (int px = 0; px < cw - 2; px += 2) {
                fb_putpixel(x + i * cw + px, y + py, col);
            }
        }
    }
}

void font_icon(int x, int y, int icon, unsigned int col) {
    (void)icon;
    fb_rect(x, y, 16, 16, col);
}

void serial_write(const char *s) { (void)s; }
void serial_write_hex(unsigned int val) { (void)val; }
void gui_pump_events(void) {}

int http_get_binary(const char *url, unsigned char *buf, int max) {
    (void)url; (void)buf; (void)max; return 0;
}

#include "../html5_engine.c"

void html5_resource_fetch_for_dom(html_node_t *root, const char *base_url) {
    (void)root; (void)base_url;
}
void html5_script_run_dom_scripts(html_node_t *root, const char *base_url) {
    (void)root; (void)base_url;
}
html5_video_ctx_t *html5_media_create_video_ctx(const char *src, int loop, int w, int h) {
    (void)src; (void)loop; (void)w; (void)h; return NULL;
}
void html5_media_destroy_video_ctx(html5_video_ctx_t *ctx) { (void)ctx; }
void html5_media_render_video(html5_video_ctx_t *ctx, int x, int y, int w, int h) {
    (void)ctx; (void)x; (void)y; (void)w; (void)h;
}

int gui_window_dirty_only = 0;
int needs_redraw = 0;
int point_in_rect(int px, int py, rect_t r) {
    return (px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h);
}
int html5_media_handle_click(html5_video_ctx_t *ctx, int mx, int my) { (void)ctx; (void)mx; (void)my; return 0; }
void html5_media_init(void) {}
void html5_media_tick(void) {}
void html5_resources_init(void) {}
void html5_script_eval(const char *code, html_node_t *node) { (void)code; (void)node; }
void html5_script_init(void) {}
void html5_url_resolve(const char *base, const char *rel, char *out, int max) {
    if (!out || max <= 0) return;
    strncpy(out, rel, max - 1);
    out[max - 1] = 0;
}

static void print_msg(const char *s) {
    int len = 0;
    while (s[len]) len++;
    write(1, s, len);
}

int main(void) {
    print_msg("=== FirstOS Forms Subsystem Test Suite ===\n");

    html5_engine_init();

    const char *html =
        "<html><body>"
        "<form id=\"searchForm\" action=\"/search\" method=\"GET\">"
        "  <label id=\"q_label\" for=\"q_input\">Search Query:</label>"
        "  <input id=\"q_input\" name=\"q\" type=\"text\" value=\"FirstOS Browser\" size=\"25\">"
        "  <label id=\"notes_label\">Description: "
        "    <textarea id=\"notes_ta\" name=\"notes\" rows=\"4\" cols=\"30\">Line One\nLine Two</textarea>"
        "  </label>"
        "  <input id=\"disabled_inp\" name=\"ignored\" type=\"text\" value=\"secret\" disabled>"
        "  <button id=\"submit_btn\" name=\"action_btn\" value=\"search_now\" type=\"submit\">Search</button>"
        "</form>"
        "</body></html>";

    int count = html5_parse_document(html, strlen(html), "http://localhost");
    print_msg("[1] Document parsed.\n");
    assert(count > 0);

    /* Verify DOM structure and attributes */
    html_node_t *q_label = html5_get_element_by_id("q_label");
    assert(q_label != NULL);
    assert(q_label->tag == HTML_TAG_LABEL);
    assert(strcmp(q_label->for_id, "q_input") == 0);
    print_msg("  [PASS] <label for=\"q_input\"> parsed correctly.\n");

    html_node_t *q_input = html5_get_element_by_id("q_input");
    assert(q_input != NULL);
    assert(q_input->tag == HTML_TAG_INPUT);
    assert(strcmp(q_input->name, "q") == 0);
    assert(strcmp(q_input->value, "FirstOS Browser") == 0);
    assert(q_input->size == 25);
    print_msg("  [PASS] <input size=\"25\"> parsed correctly.\n");

    html_node_t *notes_ta = html5_get_element_by_id("notes_ta");
    assert(notes_ta != NULL);
    assert(notes_ta->tag == HTML_TAG_TEXTAREA);
    assert(strcmp(notes_ta->name, "notes") == 0);
    assert(notes_ta->rows == 4);
    assert(notes_ta->cols == 30);
    assert(strcmp(notes_ta->value, "Line One\nLine Two") == 0);
    print_msg("  [PASS] <textarea rows=\"4\" cols=\"30\"> body content parsed correctly.\n");

    html_node_t *dis_inp = html5_get_element_by_id("disabled_inp");
    assert(dis_inp != NULL);
    assert(dis_inp->disabled == 1);
    print_msg("  [PASS] disabled attribute parsed correctly.\n");

    html_node_t *btn = html5_get_element_by_id("submit_btn");
    assert(btn != NULL);
    assert(btn->tag == HTML_TAG_BUTTON);
    print_msg("  [PASS] <button> parsed correctly.\n");

    /* Layout the document */
    html5_layout_document(800, 600);
    print_msg("[2] Layout completed.\n");
    assert(q_input->bounds.w == 25 * 8 + 16); /* 216 */
    assert(q_input->bounds.h == 28);
    assert(notes_ta->bounds.w == 30 * 8 + 16); /* 256 */
    assert(notes_ta->bounds.h == 4 * 16 + 14); /* 78 */
    assert(btn->bounds.w >= 64);
    assert(btn->bounds.h == 28);
    print_msg("  [PASS] Intrinsic layout sizing matches specification.\n");

    /* Test Label Click Association (for="q_input") */
    print_msg("[3] Testing Label Click Association...\n");
    char nav_buf[256];
    int clicked = html5_handle_click(0, 0, 0, q_label->bounds.x + 2, q_label->bounds.y + 2, nav_buf, sizeof(nav_buf));
    assert(clicked == 1);
    assert(q_input->is_focused == 1);
    assert(notes_ta->is_focused == 0);
    print_msg("  [PASS] Clicking <label for=\"q_input\"> focused q_input!\n");

    /* Test Tab Navigation */
    print_msg("[4] Testing Tab-navigation Focus Cycling...\n");
    html5_focus_next_control(1);
    assert(notes_ta->is_focused == 1);
    assert(q_input->is_focused == 0);
    print_msg("  [PASS] Tab moved focus to notes_ta.\n");

    html5_focus_next_control(1);
    assert(btn->is_focused == 1);
    assert(notes_ta->is_focused == 0);
    print_msg("  [PASS] Tab moved focus to submit_btn (skipped disabled_inp).\n");

    html5_focus_next_control(1);
    assert(q_input->is_focused == 1);
    assert(btn->is_focused == 0);
    print_msg("  [PASS] Tab wrapped focus back to q_input.\n");

    /* Test Keyboard Typing and Editing */
    print_msg("[5] Testing Keyboard Input & In-place Text Editing...\n");
    assert(q_input->cursor_pos == 15);
    html5_handle_key(0, '!');
    assert(strcmp(q_input->value, "FirstOS Browser!") == 0);
    assert(q_input->cursor_pos == 16);

    /* Backspace */
    html5_handle_key(0x0E, 8);
    assert(strcmp(q_input->value, "FirstOS Browser") == 0);
    assert(q_input->cursor_pos == 15);

    /* Left Arrow twice */
    html5_handle_key(0x4B, 0);
    html5_handle_key(0x4B, 0);
    assert(q_input->cursor_pos == 13);

    /* Type 'X' in middle */
    html5_handle_key(0, 'X');
    assert(strcmp(q_input->value, "FirstOS BrowsXer") == 0);
    assert(q_input->cursor_pos == 14);

    /* Left arrow and Delete */
    html5_handle_key(0x4B, 0);
    assert(q_input->cursor_pos == 13);
    html5_handle_key(0x53, 0); /* Delete */
    assert(strcmp(q_input->value, "FirstOS Browser") == 0);
    print_msg("  [PASS] Insert, Backspace, Arrow keys, and Delete operate accurately.\n");

    /* Test Textarea Newline */
    html5_focus_next_control(1); /* notes_ta */
    assert(notes_ta->is_focused == 1);
    int ta_len_before = strlen(notes_ta->value);
    html5_handle_key(0x1C, '\n'); /* Enter in textarea inserts \n */
    assert((int)strlen(notes_ta->value) == ta_len_before + 1);
    assert(notes_ta->value[strlen(notes_ta->value) - 1] == '\n');
    print_msg("  [PASS] Enter in <textarea> inserts newline character.\n");

    /* Test Form Submission via Submit Button Click */
    print_msg("[6] Testing GET Form Submission & URL Parameter Serialization...\n");
    strcpy(q_input->value, "FirstOS & C11");
    strcpy(notes_ta->value, "A+B\nC");

    int submit_click = html5_handle_click(0, 0, 0, btn->bounds.x + 4, btn->bounds.y + 4, nav_buf, sizeof(nav_buf));
    assert(submit_click == 1);

    const char *pending_req = html5_take_navigation_request();
    assert(pending_req != NULL);
    print_msg("  Serialized URL: ");
    print_msg(pending_req);
    print_msg("\n");

    /* Verify URL serialization */
    assert(strstr(pending_req, "/search?") != NULL);
    assert(strstr(pending_req, "q=FirstOS+%26+C11") != NULL);
    assert(strstr(pending_req, "notes=A%2BB%0D%0AC") != NULL);
    assert(strstr(pending_req, "action_btn=search_now") != NULL);
    assert(strstr(pending_req, "ignored=") == NULL); /* disabled input omitted */
    print_msg("  [PASS] GET form serialization matches standard URL percent-encoding!\n");

    /* Render without crashes */
    html5_render_document(0, 0, 800, 600, 0);
    print_msg("[7] Rendering pipeline completed with focus rings and caret.\n");

    print_msg("\n>>> ALL 7 FORMS SUBSYSTEM TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
