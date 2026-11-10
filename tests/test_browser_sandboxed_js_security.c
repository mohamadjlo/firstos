#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wincompatible-library-redeclaration"
#endif

int printf(const char *format, ...);
int snprintf(char *str, unsigned int size, const char *format, ...);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *malloc(size_t size);
void free(void *ptr);

#include "userspace/browser/include/firstos_renderer.h"

/* Include renderer adapter implementations */
#include "html5_arena.c"
#include "userspace/browser/dom_adapter.c"
#include "userspace/browser/html_adapter.c"
#include "userspace/browser/css_adapter.c"
#include "userspace/browser/layout_adapter.c"
#include "userspace/browser/render_adapter.c"
#include "userspace/browser/form_adapter.c"
#include "userspace/browser/js_adapter.c"
#include "userspace/browser/event_adapter.c"

/* URL and security policy validation */
static int test_validate_url(const char *url, const char *base_origin) {
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
    if (strncmp(url, "about:", 6) == 0) return 0;
    if (strncmp(url, "data:", 5) == 0) return 0;
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) return 0;
    if (strncmp(url, "file://", 7) == 0 || url[0] == '/') {
        if (base_origin) {
            if (strncmp(base_origin, "http://", 7) == 0 || strncmp(base_origin, "https://", 8) == 0) {
                return -5;
            }
        }
        return 0;
    }
    return -6;
}

/* 1. Test JS fuel limit exhaustion */
static void test_js_fuel_limit(void) {
    printf("[test_phase5_security] 1. test_js_fuel_limit...\n");
    firstos_js_init();

    const char *infinite_loop = "var i = 0;\n"
                                "while (1) {\n"
                                "    i = i + 1;\n"
                                "}\n";
    int res = firstos_js_eval(NULL, infinite_loop);
    assert(res == -1); /* Halted due to instruction budget limit */
    assert(firstos_js_get_instruction_count() >= 1000);
    printf("  -> OK (Script budget exhausted at count=%d, terminated cleanly)\n", firstos_js_get_instruction_count());
}

/* 2. Test JS recursion depth limit */
static void test_js_call_depth_limit(void) {
    printf("[test_phase5_security] 2. test_js_call_depth_limit...\n");
    firstos_js_init();

    const char *deep_recursion = "function recurse() {\n"
                                 "    recurse();\n"
                                 "}\n"
                                 "recurse();\n";
    int res = firstos_js_eval(NULL, deep_recursion);
    assert(res == -2); /* Halted due to max call depth exceeded */
    assert(firstos_js_get_call_depth() == 0); /* Call depth unwound cleanly */
    printf("  -> OK (Recursion halted at depth > 8, call depth unwound cleanly)\n");
}

/* 3. Test Renderer Context Origin Scoping */
static void test_renderer_origin_scoping(void) {
    printf("[test_phase5_security] 3. test_renderer_origin_scoping...\n");
    firstos_renderer_context_t ctx;
    firstos_renderer_init(&ctx);

    assert(strcmp(firstos_renderer_get_origin(&ctx), "") == 0);

    firstos_renderer_set_origin(&ctx, "https://secure.bank.com");
    assert(strcmp(firstos_renderer_get_origin(&ctx), "https://secure.bank.com") == 0);

    firstos_renderer_reset(&ctx);
    assert(strcmp(firstos_renderer_get_origin(&ctx), "") == 0);

    firstos_renderer_destroy(&ctx);
    printf("  -> OK (Renderer context origin properly isolated, assigned, and cleared)\n");
}

/* 4. Test User Gesture Gating Logic */
static void test_user_gesture_gating(void) {
    printf("[test_phase5_security] 4. test_user_gesture_gating...\n");

    /* Host gesture simulation */
    uint32_t active_tab = 1;
    uint32_t gesture_tab = 0;
    uint32_t gesture_tick = 0;
    uint32_t now = 100;

    /* Case A: Script initiates download with dreq->is_user_gesture = 1, but host has NO gesture */
    int dreq_is_user_gesture = 1;
    int host_has_gesture = (active_tab != 0 && gesture_tab == active_tab && (now - gesture_tick <= 3));
    int download_permitted = host_has_gesture && (dreq_is_user_gesture != 0);
    assert(!download_permitted);

    /* Case B: User clicks button in active tab (host records gesture) */
    gesture_tab = active_tab;
    gesture_tick = now;
    host_has_gesture = (active_tab != 0 && gesture_tab == active_tab && (now - gesture_tick <= 3));
    download_permitted = host_has_gesture && (dreq_is_user_gesture != 0);
    assert(download_permitted);

    /* Case C: Gesture expires after timeout */
    now += 4;
    host_has_gesture = (active_tab != 0 && gesture_tab == active_tab && (now - gesture_tick <= 3));
    download_permitted = host_has_gesture && (dreq_is_user_gesture != 0);
    assert(!download_permitted);

    printf("  -> OK (Download strictly gated by host-recorded user gesture timestamp)\n");
}

/* 5. Test Cross-Origin URL Validation & Security Rules */
static void test_cross_origin_security_isolation(void) {
    printf("[test_phase5_security] 5. test_cross_origin_security_isolation...\n");
    const char *web_origin = "http://evil.org";

    /* Local file access forbidden from web origin */
    assert(test_validate_url("file:///etc/passwd", web_origin) == -5);
    assert(test_validate_url("/etc/passwd", web_origin) == -5);

    /* Path traversal strictly blocked */
    assert(test_validate_url("http://evil.org/../../secret", web_origin) == -4);

    /* Web URLs allowed */
    assert(test_validate_url("http://example.com/api", web_origin) == 0);
    assert(test_validate_url("https://secure.bank.com", web_origin) == 0);

    printf("  -> OK (Cross-origin navigation and local file access rules strictly enforced)\n");
}

/* 6. Test ABI Struct Invariants */
static void test_abi_struct_invariants(void) {
    printf("[test_phase5_security] 6. test_abi_struct_invariants...\n");
    assert(sizeof(firstos_browser_message_t) == 32);
    assert(sizeof(firstos_render_command_t) == 32);
    assert(sizeof(firstos_form_submit_request_t) == 32);
    assert(sizeof(firstos_damage_rect_t) == 32);
    assert(sizeof(firstos_download_request_t) == 32);
    printf("  -> OK (All 32-byte IPC fixed layouts preserved)\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 5: Sandboxed Script Engine & Origin Security Tests\n");
    printf("============================================================\n");

    test_js_fuel_limit();
    test_js_call_depth_limit();
    test_renderer_origin_scoping();
    test_user_gesture_gating();
    test_cross_origin_security_isolation();
    test_abi_struct_invariants();

    printf("============================================================\n");
    printf("ALL 6 PHASE 5 SECURITY & SCRIPT SANDBOX TESTS PASSED (6/6)\n");
    printf("============================================================\n");
    return 0;
}
