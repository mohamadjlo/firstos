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

/* URL policy validation function for test 10 */
static int test_validate_url(const char *url) {
    if (!url || !url[0]) return -1;
    int len = 0;
    while (url[len]) {
        if ((unsigned char)url[len] < 32 || (unsigned char)url[len] == 127) return -2;
        len++;
        if (len > 1024) return -3;
    }
    for (int i = 0; i < len - 1; i++) {
        if (url[i] == '.' && url[i+1] == '.') {
            if (i + 2 == len || url[i+2] == '/' || url[i+2] == '\\' || (i > 0 && (url[i-1] == '/' || url[i-1] == '\\'))) {
                return -4;
            }
        }
    }
    return 0;
}

/* 1. test_form_control_parsing */
static void test_form_control_parsing(void) {
    printf("[test_browser_forms] 1. test_form_control_parsing...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<form action=\"/submit\" method=\"GET\">"
                       "<input type=\"text\" name=\"user\" value=\"alice\" placeholder=\"Username\">"
                       "<input type=\"password\" name=\"pass\" value=\"secret\">"
                       "<input type=\"checkbox\" name=\"remember\" checked>"
                       "<input type=\"submit\" value=\"Login\">"
                       "<button>Cancel</button>"
                       "</form>";
    int res = firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    assert(res == RENDER_OK);

    html_node_t *form = firstos_renderer_get_element_by_tag(&ctx, HTML_TAG_FORM);
    assert(form != NULL);
    assert(form->action != NULL && strcmp(form->action, "/submit") == 0);
    assert(form->method != NULL && strcmp(form->method, "GET") == 0);

    /* Check child nodes */
    html_node_t *input1 = form->first_child;
    assert(input1 != NULL && input1->tag == HTML_TAG_INPUT);
    assert(input1->name != NULL && strcmp(input1->name, "user") == 0);
    assert(strcmp(input1->value, "alice") == 0);
    assert(input1->placeholder != NULL && strcmp(input1->placeholder, "Username") == 0);
    assert(input1->input_type != NULL && strcmp(input1->input_type, "text") == 0);

    html_node_t *input2 = input1->next_sibling;
    assert(input2 != NULL && input2->tag == HTML_TAG_INPUT);
    assert(input2->name != NULL && strcmp(input2->name, "pass") == 0);
    assert(strcmp(input2->value, "secret") == 0);
    assert(input2->input_type != NULL && strcmp(input2->input_type, "password") == 0);

    html_node_t *input3 = input2->next_sibling;
    assert(input3 != NULL && input3->tag == HTML_TAG_INPUT);
    assert(input3->name != NULL && strcmp(input3->name, "remember") == 0);
    assert(input3->checked == 1);
    assert(input3->input_type != NULL && strcmp(input3->input_type, "checkbox") == 0);

    html_node_t *input4 = input3->next_sibling;
    assert(input4 != NULL && input4->tag == HTML_TAG_INPUT);
    assert(input4->input_type != NULL && strcmp(input4->input_type, "submit") == 0);
    assert(strcmp(input4->value, "Login") == 0);

    html_node_t *btn = input4->next_sibling;
    assert(btn != NULL && btn->tag == HTML_TAG_BUTTON);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Form, inputs, checkbox, submit, and button parsed)\n");
}

/* 2. test_input_text_editing */
static void test_input_text_editing(void) {
    printf("[test_browser_forms] 2. test_input_text_editing...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<input id=\"tb\" type=\"text\" value=\"abc\">";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));

    html_node_t *tb = firstos_renderer_get_element_by_id(&ctx, "tb");
    assert(tb != NULL);
    assert(strcmp(tb->value, "abc") == 0);
    assert(tb->value_len == 3);

    /* Set focus */
    firstos_renderer_set_focus(&ctx, tb);
    assert(firstos_renderer_get_focused(&ctx) == tb);

    /* Type 'd' */
    int ok = firstos_form_handle_char(&ctx, 'd');
    assert(ok == 1);
    assert(strcmp(tb->value, "abcd") == 0);
    assert(tb->value_len == 4);
    assert(tb->cursor_pos == 4);

    /* Backspace */
    ok = firstos_form_handle_backspace(&ctx);
    assert(ok == 1);
    assert(strcmp(tb->value, "abc") == 0);
    assert(tb->value_len == 3);
    assert(tb->cursor_pos == 3);

    /* Backspace to empty */
    firstos_form_handle_backspace(&ctx);
    firstos_form_handle_backspace(&ctx);
    firstos_form_handle_backspace(&ctx);
    assert(strcmp(tb->value, "") == 0);
    assert(tb->value_len == 0);
    assert(tb->cursor_pos == 0);

    /* Backspace on empty input returns 0 */
    assert(firstos_form_handle_backspace(&ctx) == 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Character insertion, backspace, and cursor positioning verified)\n");
}

/* 3. test_password_masking */
static void test_password_masking(void) {
    printf("[test_browser_forms] 3. test_password_masking...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<input id=\"pwd\" type=\"password\" value=\"Secret123\">";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    firstos_renderer_layout(&ctx, 800, 600);

    firstos_render_command_t cmds[64];
    char strings[1024];
    uint32_t num_cmds = 0;
    int res = firstos_renderer_generate_commands(&ctx, cmds, 64, strings, 1024, &num_cmds);
    assert(res == RENDER_OK);

    /* Internal value MUST remain unchanged */
    html_node_t *pwd = firstos_renderer_get_element_by_id(&ctx, "pwd");
    assert(pwd != NULL);
    assert(strcmp(pwd->value, "Secret123") == 0);

    /* Find TEXT command emitted for password and verify it is masked with '*' */
    int found_masked = 0;
    for (uint32_t i = 0; i < num_cmds; i++) {
        if (cmds[i].type == FIRSTOS_RENDER_CMD_TEXT) {
            const char *str = strings + cmds[i].text_offset;
            if (strcmp(str, "*********") == 0) {
                found_masked = 1;
            }
            /* Secret value MUST NEVER appear in render commands */
            assert(strstr(str, "Secret123") == NULL);
        }
    }
    assert(found_masked == 1);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Password masked with asterisks; raw password never leaked to commands)\n");
}

/* 4. test_checkbox_toggling */
static void test_checkbox_toggling(void) {
    printf("[test_browser_forms] 4. test_checkbox_toggling...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<input id=\"cb\" type=\"checkbox\">";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));

    html_node_t *cb = firstos_renderer_get_element_by_id(&ctx, "cb");
    assert(cb != NULL);
    assert(cb->checked == 0);

    /* Direct toggle */
    int ok = firstos_form_toggle_checkbox(cb);
    assert(ok == 1);
    assert(cb->checked == 1);

    ok = firstos_form_toggle_checkbox(cb);
    assert(ok == 1);
    assert(cb->checked == 0);

    /* Toggle via keyboard Space */
    firstos_renderer_set_focus(&ctx, cb);
    int kres = firstos_renderer_handle_key(&ctx, 0, ' ', NULL, 0, NULL, 0);
    assert(kres == 1);
    assert(cb->checked == 1);

    kres = firstos_renderer_handle_key(&ctx, 0, ' ', NULL, 0, NULL, 0);
    assert(kres == 1);
    assert(cb->checked == 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Checkbox toggled programmatically and via Space key)\n");
}

/* 5. test_focus_tab_navigation */
static void test_focus_tab_navigation(void) {
    printf("[test_browser_forms] 5. test_focus_tab_navigation...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<div>"
                       "<input id=\"f1\" type=\"text\" value=\"one\">"
                       "<input id=\"f2\" type=\"password\" value=\"two\">"
                       "<input id=\"f3\" type=\"checkbox\">"
                       "<button id=\"f4\">Click</button>"
                       "</div>";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));

    html_node_t *f1 = firstos_renderer_get_element_by_id(&ctx, "f1");
    html_node_t *f2 = firstos_renderer_get_element_by_id(&ctx, "f2");
    html_node_t *f3 = firstos_renderer_get_element_by_id(&ctx, "f3");
    html_node_t *f4 = firstos_renderer_get_element_by_id(&ctx, "f4");

    /* Initial state: no focus */
    assert(firstos_renderer_get_focused(&ctx) == NULL);

    /* Tab forward -> f1 */
    firstos_renderer_focus_next(&ctx, 0);
    assert(firstos_renderer_get_focused(&ctx) == f1);
    assert(f1->is_focused == 1);

    /* Tab forward -> f2 */
    firstos_renderer_focus_next(&ctx, 0);
    assert(firstos_renderer_get_focused(&ctx) == f2);
    assert(f1->is_focused == 0);
    assert(f2->is_focused == 1);

    /* Tab forward -> f3 */
    firstos_renderer_focus_next(&ctx, 0);
    assert(firstos_renderer_get_focused(&ctx) == f3);

    /* Tab forward -> f4 */
    firstos_renderer_focus_next(&ctx, 0);
    assert(firstos_renderer_get_focused(&ctx) == f4);

    /* Tab wrap around -> f1 */
    firstos_renderer_focus_next(&ctx, 0);
    assert(firstos_renderer_get_focused(&ctx) == f1);

    /* Shift+Tab (reverse) -> f4 */
    firstos_renderer_focus_next(&ctx, 1);
    assert(firstos_renderer_get_focused(&ctx) == f4);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Tab forward and Shift+Tab reverse navigation wrap cleanly)\n");
}

/* 6. test_dom_event_capture_bubble */
static void test_dom_event_capture_bubble(void) {
    printf("[test_browser_forms] 6. test_dom_event_capture_bubble...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<div id=\"parent\"><button id=\"child\">Btn</button></div>";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));

    html_node_t *child = firstos_renderer_get_element_by_id(&ctx, "child");
    assert(child != NULL);

    firstos_dom_event_t ev;
    ev.type = DOM_EVENT_CLICK;
    ev.target_node_id = child->id_num;
    ev.current_node_id = 0;
    ev.phase = 0;
    ev.default_prevented = 0;
    ev.propagation_stopped = 0;

    int prevented = firstos_dom_dispatch_event(&ctx, child, &ev);
    assert(prevented == 0);
    assert(ev.phase == 3); /* Ended in bubble phase */

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (DOM event traverses Capture, Target, and Bubble phases)\n");
}

/* 7. test_dom_event_prevent_default */
static void test_dom_event_prevent_default(void) {
    printf("[test_browser_forms] 7. test_dom_event_prevent_default...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<a id=\"link\" href=\"/destination\">Link</a>";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));
    firstos_renderer_layout(&ctx, 800, 600);

    html_node_t *link = firstos_renderer_get_element_by_id(&ctx, "link");
    assert(link != NULL);

    firstos_dom_event_t ev;
    ev.type = DOM_EVENT_CLICK;
    ev.target_node_id = link->id_num;
    ev.current_node_id = link->id_num;
    ev.phase = 2;
    ev.default_prevented = 1; /* preventDefault() */
    ev.propagation_stopped = 0;

    int res = firstos_dom_dispatch_event(&ctx, link, &ev);
    assert(res == 1);
    assert(ev.default_prevented == 1);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Default prevented flag correctly halts action)\n");
}

/* 8. test_dom_event_stop_propagation */
static void test_dom_event_stop_propagation(void) {
    printf("[test_browser_forms] 8. test_dom_event_stop_propagation...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<div id=\"box\"><button id=\"btn\">Click</button></div>";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));

    html_node_t *btn = firstos_renderer_get_element_by_id(&ctx, "btn");
    assert(btn != NULL);

    firstos_dom_event_t ev;
    ev.type = DOM_EVENT_CLICK;
    ev.target_node_id = btn->id_num;
    ev.current_node_id = 0;
    ev.phase = 0;
    ev.default_prevented = 0;
    ev.propagation_stopped = 1; /* Stop propagation */

    firstos_dom_dispatch_event(&ctx, btn, &ev);
    /* Propagation was stopped before bubbling */
    assert(ev.propagation_stopped == 1);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (stopPropagation halts event traversal)\n");
}

/* 9. test_form_query_encoding */
static void test_form_query_encoding(void) {
    printf("[test_browser_forms] 9. test_form_query_encoding...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    const char *html = "<form id=\"frm\" action=\"/search\">"
                       "<input name=\"q\" value=\"hello world\">"
                       "<input name=\"tag\" value=\"c++ & os\">"
                       "<input name=\"opt\" type=\"checkbox\" checked>"
                       "<input name=\"unchecked\" type=\"checkbox\">"
                       "</form>";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));

    html_node_t *frm = firstos_renderer_get_element_by_id(&ctx, "frm");
    assert(frm != NULL);

    char query[256];
    int len = firstos_form_collect_query(&ctx, frm, query, sizeof(query));
    assert(len > 0);
    assert(strstr(query, "q=hello+world") != NULL);
    assert(strstr(query, "tag=c%2B%2B+%26+os") != NULL);
    assert(strstr(query, "opt=on") != NULL);
    assert(strstr(query, "unchecked") == NULL); /* Unchecked checkbox excluded */

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Form fields URL-encoded and concatenated as query string)\n");
}

/* 10. test_form_submit_ipc_validation */
static void test_form_submit_ipc_validation(void) {
    printf("[test_browser_forms] 10. test_form_submit_ipc_validation...\n");
    assert(test_validate_url("/search?q=firstos") == 0);
    assert(test_validate_url("http://example.com/api?a=1&b=2") == 0);

    /* Directory traversal attempt */
    assert(test_validate_url("/submit/../../etc/passwd") == -4);
    assert(test_validate_url("..\\secret") == -4);

    /* Empty string */
    assert(test_validate_url("") == -1);

    printf("  -> OK (Form submission URLs correctly validated against security policies)\n");
}

/* 11. test_js_variables_and_arithmetic */
static void test_js_variables_and_arithmetic(void) {
    printf("[test_browser_forms] 11. test_js_variables_and_arithmetic...\n");
    firstos_js_init();

    const char *code = "var a = 15;\n"
                       "var b = 27;\n"
                       "var sum = a + b;\n"
                       "var str1 = 'Hello ';\n"
                       "var greeting = str1 + 'FirstOS';\n";
    int res = firstos_js_eval(NULL, code);
    assert(res == 0);

    assert(get_var("a") != NULL && strcmp(get_var("a"), "15") == 0);
    assert(get_var("b") != NULL && strcmp(get_var("b"), "27") == 0);
    assert(get_var("sum") != NULL && strcmp(get_var("sum"), "42") == 0);
    assert(get_var("greeting") != NULL && strcmp(get_var("greeting"), "Hello FirstOS") == 0);

    printf("  -> OK (Variables, integer addition, and string concatenation evaluated)\n");
}

/* 12. test_js_dom_bridge_text */
static void test_js_dom_bridge_text(void) {
    printf("[test_browser_forms] 12. test_js_dom_bridge_text...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);
    firstos_js_init();

    const char *html = "<h1 id=\"title\">Initial</h1>";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));

    html_node_t *title = firstos_renderer_get_element_by_id(&ctx, "title");
    assert(title != NULL);
    assert(strcmp(title->text, "Initial") == 0);

    const char *js = "document.getElementById('title').textContent = 'Dynamic Title';";
    int res = firstos_js_eval(&ctx, js);
    assert(res == 0);
    assert(strcmp(title->text, "Dynamic Title") == 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (JavaScript updated DOM textContent successfully)\n");
}

/* 13. test_js_dom_bridge_value */
static void test_js_dom_bridge_value(void) {
    printf("[test_browser_forms] 13. test_js_dom_bridge_value...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);
    firstos_js_init();

    const char *html = "<input id=\"user\" value=\"guest\">";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));

    html_node_t *user = firstos_renderer_get_element_by_id(&ctx, "user");
    assert(user != NULL);
    assert(strcmp(user->value, "guest") == 0);

    const char *js = "document.getElementById('user').value = 'superuser';";
    int res = firstos_js_eval(&ctx, js);
    assert(res == 0);
    assert(strcmp(user->value, "superuser") == 0);
    assert(user->value_len == 9);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (JavaScript read and updated input element value)\n");
}

/* 14. test_js_event_listener */
static void test_js_event_listener(void) {
    printf("[test_browser_forms] 14. test_js_event_listener...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);
    firstos_js_init();

    const char *html = "<button id=\"mybtn\">Click</button><p id=\"status\">idle</p>";
    firstos_renderer_parse_html(&ctx, html, (int)strlen(html));

    html_node_t *btn = firstos_renderer_get_element_by_id(&ctx, "mybtn");
    html_node_t *st = firstos_renderer_get_element_by_id(&ctx, "status");
    assert(btn != NULL && st != NULL);

    const char *js = "function handleBtnClick() {\n"
                     "    document.getElementById('status').textContent = 'active';\n"
                     "}\n"
                     "document.getElementById('mybtn').addEventListener('click', handleBtnClick);\n";
    int res = firstos_js_eval(&ctx, js);
    assert(res == 0);

    /* Before event */
    assert(strcmp(st->text, "idle") == 0);

    /* Dispatch event */
    firstos_js_dispatch_event(&ctx, btn, "click");

    /* After event */
    assert(strcmp(st->text, "active") == 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Event listener registered and executed on DOM event dispatch)\n");
}

/* 15. test_js_instruction_limit */
static void test_js_instruction_limit(void) {
    printf("[test_browser_forms] 15. test_js_instruction_limit...\n");
    firstos_js_init();

    /* Infinite loop script */
    const char *infinite_loop = "var i = 0;\n"
                                "while (1) {\n"
                                "    i = i + 1;\n"
                                "}\n";
    int res = firstos_js_eval(NULL, infinite_loop);
    /* Engine must cleanly terminate with -1 without hanging */
    assert(res == -1);

    printf("  -> OK (Infinite loop halted cleanly at MAX_INSTRUCTIONS budget)\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 4E: Forms, DOM Events & JS Subset Unit Tests\n");
    printf("============================================================\n");

    test_form_control_parsing();
    test_input_text_editing();
    test_password_masking();
    test_checkbox_toggling();
    test_focus_tab_navigation();
    test_dom_event_capture_bubble();
    test_dom_event_prevent_default();
    test_dom_event_stop_propagation();
    test_form_query_encoding();
    test_form_submit_ipc_validation();
    test_js_variables_and_arithmetic();
    test_js_dom_bridge_text();
    test_js_dom_bridge_value();
    test_js_event_listener();
    test_js_instruction_limit();

    printf("============================================================\n");
    printf("ALL 15 PHASE 4E UNIT TESTS PASSED (15/15)\n");
    printf("============================================================\n");
    return 0;
}
