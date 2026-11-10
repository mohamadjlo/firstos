#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wincompatible-library-redeclaration"
#endif

int printf(const char *format, ...);
int snprintf(char *str, unsigned int size, const char *format, ...);
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, unsigned int n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, unsigned int n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
void *memcpy(void *dest, const void *src, unsigned int n);
void *memset(void *s, int c, unsigned int n);
int atoi(const char *nptr);

#include "task.h"
#include "shm.h"
#include "process.h"
#include "include/firstos/uapi/browser_ipc.h"
#include "include/firstos/security/browser_security.h"
#include "browser_host_ipc.h"

/* Serial mock capture */
static char s_serial_log[16384];
static int  s_serial_log_pos = 0;

void serial_write(const char *str) {
    if (!str) return;
    int len = (int)strlen(str);
    if (s_serial_log_pos + len < (int)sizeof(s_serial_log) - 1) {
        memcpy(s_serial_log + s_serial_log_pos, str, (unsigned int)len);
        s_serial_log_pos += len;
        s_serial_log[s_serial_log_pos] = '\0';
    }
}

void serial_write_hex(unsigned int val) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%X", val);
    serial_write(buf);
}

/* Stubs for kernel subsystems used by browser_host_ipc.c */
int fs_read_at(const char *path, char *out, uint32_t capacity) {
    (void)path; (void)out; (void)capacity;
    return 0;
}

int fs_write_at(const char *path, const char *data, uint32_t size) {
    (void)path; (void)data; (void)size;
    return (int)size;
}

int fs_read(const char *path, char *buf, uint32_t max_len) {
    (void)path; (void)buf; (void)max_len;
    return 0;
}

void fb_rect(int x, int y, int w, int h, unsigned int col) {
    (void)x; (void)y; (void)w; (void)h; (void)col;
}

void fb_border(int x, int y, int w, int h, unsigned int col) {
    (void)x; (void)y; (void)w; (void)h; (void)col;
}

void fb_putpixel(int x, int y, unsigned int col) {
    (void)x; (void)y; (void)col;
}

unsigned int fb_read_pixel(int x, int y) {
    (void)x; (void)y;
    return 0;
}

void font_text(int x, int y, const char *text, unsigned int col, int scale) {
    (void)x; (void)y; (void)text; (void)col; (void)scale;
}

void image_free(uint8_t *data) {
    (void)data;
}

uint8_t *image_load_from_memory(const uint8_t *buf, int len, int *w, int *h) {
    (void)buf; (void)len; (void)w; (void)h;
    return NULL;
}

void *kmalloc(uint32_t size) {
    extern void *malloc(size_t);
    return malloc((size_t)size);
}

void kfree(void *ptr) {
    extern void free(void *);
    free(ptr);
}

/* Mock shared memory table */
static char s_mock_shm_buffer[4 * 1024 * 1024];
shm_region_t shm_table[FIRSTOS_MAX_SHM];

int shm_open(const char *name, int flags, int mode) {
    (void)flags; (void)mode;
    for (int i = 0; i < FIRSTOS_MAX_SHM; i++) {
        if (shm_table[i].used && strcmp(shm_table[i].name, name) == 0) {
            return i;
        }
    }
    for (int i = 0; i < FIRSTOS_MAX_SHM; i++) {
        if (!shm_table[i].used) {
            shm_table[i].used = 1;
            strncpy(shm_table[i].name, name, FIRSTOS_SHM_NAME_MAX - 1);
            shm_table[i].phys_mem = s_mock_shm_buffer;
            return i;
        }
    }
    return -1;
}

process_info_t process_table[FIRSTOS_MAX_PROCESSES];

int process_spawn(int parent_pid, const char *path, const char *const *argv) {
    (void)parent_pid; (void)path; (void)argv;
    process_table[2].state = PROC_STATE_READY;
    return 2;
}

void web_browser_navigate(const char *target) {
    (void)target;
}

/* Include security and host implementations */
#include "browser_security.c"
#include "cookies.c"
#include "browser_host_ipc.c"

/* ============================================================
/* ============================================================
 * Phase 5C: Frame Tree & Secure postMessage Host Tests
 * ============================================================ */
static int s_pass = 0;
#define CHECK(c) do { assert(c); s_pass++; } while (0)

static firstos_tab_t *fresh_tab(const char *url) {
    browser_host_tab_manager_reset();
    firstos_tab_t *t = browser_host_get_active_tab();
    browser_host_tab_navigate(t->tab_id, url);
    return t;
}

static void test_root_frame_creation(void) {
    printf("[test_iframe] 1. root frame creation\n");
    firstos_tab_t *t = fresh_tab("http://example.com/index.html");
    firstos_frame_t *root = browser_host_frame_get(t->tab_id, t->frame_tree.root_frame_id);
    CHECK(root != NULL);
    CHECK(root->depth == 0 && root->parent_frame_id == 0);
    CHECK(t->frame_tree.frame_count == 1);
    CHECK(browser_security_origin_equals(&root->committed_origin, &t->committed_origin));
}

static void test_iframe_creation_and_capacity(void) {
    printf("[test_iframe] 2. creation and per-tab capacity\n");
    firstos_tab_t *t = fresh_tab("http://example.com/");
    int ok = 0;
    for (int i = 0; i < FIRSTOS_MAX_FRAMES_PER_TAB - 1; i++) {
        if (browser_host_frame_create(t->tab_id, 0, "http://example.com/a.html", 0, 0, 0, 0) > 0) ok++;
    }
    CHECK(ok == FIRSTOS_MAX_FRAMES_PER_TAB - 1);
    CHECK(t->frame_tree.frame_count == FIRSTOS_MAX_FRAMES_PER_TAB);
    CHECK(browser_host_frame_create(t->tab_id, 0, "http://example.com/x", 0, 0, 0, 0) == FIRSTOS_FRAME_ACCESS_DENY_LIMIT);
}

static void test_total_frame_limit(void) {
    printf("[test_iframe] 3. global frame limit\n");
    browser_host_tab_manager_reset();
    for (int i = 0; i < FIRSTOS_MAX_TABS - 1; i++) browser_host_tab_create("http://example.com/", 0);
    firstos_tab_manager_t *tm = browser_host_get_tab_manager();
    int created = 0, denied = 0;
    for (int ti = 0; ti < FIRSTOS_MAX_TABS; ti++) {
        firstos_tab_t *t = &tm->tabs[ti];
        if (t->state == FIRSTOS_TAB_FREE) continue;
        for (int k = 0; k < FIRSTOS_MAX_FRAMES_PER_TAB; k++) {
            int r = browser_host_frame_create(t->tab_id, 0, "about:blank", 0, 0, 0, 0);
            if (r > 0) created++; else denied++;
        }
    }
    CHECK(browser_host_frame_total_count() == FIRSTOS_MAX_TOTAL_FRAMES);
    CHECK(created == FIRSTOS_MAX_TOTAL_FRAMES - FIRSTOS_MAX_TABS);
    CHECK(denied > 0);
}

static void test_frame_nesting_depth_limit(void) {
    printf("[test_iframe] 4. nesting depth limit\n");
    firstos_tab_t *t = fresh_tab("http://example.com/");
    uint32_t parent = t->frame_tree.root_frame_id;
    for (uint32_t d = 1; d <= FIRSTOS_MAX_FRAME_DEPTH; d++) {
        int f = browser_host_frame_create(t->tab_id, parent, "http://example.com/n.html", 0, 0, 0, 0);
        CHECK(f > 0);
        CHECK(browser_host_frame_get(t->tab_id, (uint32_t)f)->depth == d);
        parent = (uint32_t)f;
    }
    CHECK(browser_host_frame_create(t->tab_id, parent, "http://example.com/deep.html", 0, 0, 0, 0) == FIRSTOS_FRAME_ACCESS_DENY_DEPTH);
}

static void test_iframe_relative_url_resolution(void) {
    printf("[test_iframe] 5. relative URL resolution\n");
    firstos_tab_t *t = fresh_tab("http://example.com/dir/index.html");
    int a = browser_host_frame_create(t->tab_id, 0, "embed.html", 0, 0, 0, 0);
    int b = browser_host_frame_create(t->tab_id, 0, "/root.html", 0, 0, 0, 0);
    int c = browser_host_frame_create(t->tab_id, 0, "//cdn.example.com/w.html", 0, 0, 0, 0);
    CHECK(strcmp(browser_host_frame_get(t->tab_id, (uint32_t)a)->url, "http://example.com/dir/embed.html") == 0);
    CHECK(strcmp(browser_host_frame_get(t->tab_id, (uint32_t)b)->url, "http://example.com/root.html") == 0);
    CHECK(strcmp(browser_host_frame_get(t->tab_id, (uint32_t)c)->url, "http://cdn.example.com/w.html") == 0);
}

static void test_embed_scheme_policy(void) {
    printf("[test_iframe] 6. embed scheme policy\n");
    firstos_tab_t *t = fresh_tab("http://example.com/");
    CHECK(browser_host_frame_create(t->tab_id, 0, "javascript:alert(1)", 0, 0, 0, 0) == FIRSTOS_FRAME_ACCESS_DENY_SCHEME);
    CHECK(browser_host_frame_create(t->tab_id, 0, "file:///secret.html", 0, 0, 0, 0) == FIRSTOS_FRAME_ACCESS_DENY_SCHEME);
    CHECK(browser_host_frame_create(t->tab_id, 999999, "http://example.com/", 0, 0, 0, 0) == FIRSTOS_FRAME_ACCESS_DENY_PARENT);
    int d = browser_host_frame_create(t->tab_id, 0, "data:text/html,hi", 0, 0, 0, 0);
    CHECK(d > 0);
    CHECK(browser_host_frame_get(t->tab_id, (uint32_t)d)->committed_origin.is_opaque == 1);
}

static void test_iframe_sop_and_origin_binding(void) {
    printf("[test_iframe] 7. independent origins / cross-origin denial\n");
    firstos_tab_t *t = fresh_tab("http://example.com/");
    int x = browser_host_frame_create(t->tab_id, 0, "http://other.com/w.html", 0, 0, 0, 0);
    int p = browser_host_frame_create(t->tab_id, 0, "http://example.com:8080/w.html", 0, 0, 0, 0);
    firstos_frame_t *fx = browser_host_frame_get(t->tab_id, (uint32_t)x);
    CHECK(strcmp(fx->committed_origin.host, "other.com") == 0);
    CHECK(browser_host_frame_check_access(t->tab_id, t->frame_tree.root_frame_id, (uint32_t)x) == FIRSTOS_FRAME_ACCESS_DENY_ORIGIN);
    CHECK(browser_host_frame_check_access(t->tab_id, (uint32_t)x, t->frame_tree.root_frame_id) == FIRSTOS_FRAME_ACCESS_DENY_ORIGIN);
    CHECK(browser_host_frame_check_access(t->tab_id, t->frame_tree.root_frame_id, (uint32_t)p) == FIRSTOS_FRAME_ACCESS_DENY_ORIGIN);
}

static void test_same_origin_frame_access(void) {
    printf("[test_iframe] 8. same-origin access\n");
    firstos_tab_t *t = fresh_tab("http://example.com/");
    int s = browser_host_frame_create(t->tab_id, 0, "http://example.com:80/child.html", 0, 0, 0, 0);
    CHECK(browser_host_frame_check_access(t->tab_id, t->frame_tree.root_frame_id, (uint32_t)s) == FIRSTOS_FRAME_ACCESS_ALLOW);
    int d1 = browser_host_frame_create(t->tab_id, 0, "data:text/html,a", 0, 0, 0, 0);
    int d2 = browser_host_frame_create(t->tab_id, 0, "data:text/html,a", 0, 0, 0, 0);
    CHECK(browser_host_frame_check_access(t->tab_id, (uint32_t)d1, (uint32_t)d2) == FIRSTOS_FRAME_ACCESS_DENY_ORIGIN);
}

static void test_postmessage_exact_origin_success(void) {
    printf("[test_iframe] 9. postMessage exact origin\n");
    firstos_tab_t *t = fresh_tab("http://example.com/");
    uint32_t root = t->frame_tree.root_frame_id;
    int c = browser_host_frame_create(t->tab_id, 0, "http://other.com/w.html", 0, 0, 0, 0);
    CHECK(browser_host_post_message_between(t->tab_id, root, (uint32_t)c, "hello", "http://other.com") == 0);
    static firstos_post_message_t rx;
    CHECK(browser_host_post_message_receive(t->tab_id, (uint32_t)c, &rx) == 1);
    CHECK(strcmp(rx.payload, "hello") == 0);
    CHECK(strcmp(rx.sender_origin, "http://example.com") == 0);
    CHECK(browser_host_post_message_receive(t->tab_id, (uint32_t)c, &rx) == 0);
    /* Child -> parent reply */
    CHECK(browser_host_post_message_between(t->tab_id, (uint32_t)c, root, "pong", "http://example.com") == 0);
    CHECK(browser_host_post_message_receive(t->tab_id, root, &rx) == 1);
    CHECK(strcmp(rx.sender_origin, "http://other.com") == 0);
}

static void test_postmessage_target_origin_mismatch(void) {
    printf("[test_iframe] 10. postMessage targetOrigin mismatch / wildcard\n");
    firstos_tab_t *t = fresh_tab("http://example.com/");
    uint32_t root = t->frame_tree.root_frame_id;
    int c = browser_host_frame_create(t->tab_id, 0, "http://other.com/w.html", 0, 0, 0, 0);
    s_serial_log_pos = 0; s_serial_log[0] = '\0';
    CHECK(browser_host_post_message_between(t->tab_id, root, (uint32_t)c, "s", "http://evil.com") == -6);
    CHECK(strstr(s_serial_log, "targetOrigin mismatch rejected") != NULL);
    CHECK(browser_host_post_message_between(t->tab_id, root, (uint32_t)c, "s", "https://other.com") == -6);
    CHECK(browser_host_post_message_between(t->tab_id, root, (uint32_t)c, "s", "*") == -6);
    CHECK(browser_host_post_message_between(t->tab_id, root, (uint32_t)c, "s", "") == -6);
    CHECK(browser_host_post_message_receive(t->tab_id, (uint32_t)c, NULL) == 0);
}

static void test_postmessage_sender_spoofing(void) {
    printf("[test_iframe] 11. sender origin cannot be spoofed\n");
    firstos_tab_t *t = fresh_tab("http://example.com/");
    int c = browser_host_frame_create(t->tab_id, 0, "http://other.com/w.html", 0, 0, 0, 0);
    firstos_frame_t *fc = browser_host_frame_get(t->tab_id, (uint32_t)c);
    firstos_frame_t *fr = browser_host_frame_get(t->tab_id, t->frame_tree.root_frame_id);
    static firstos_post_message_t m;
    memset(&m, 0, sizeof(m));
    m.version = FIRSTOS_BROWSER_IPC_VERSION; m.size = sizeof(m);
    m.sender_tab_id = t->tab_id; m.sender_tab_generation = t->generation;
    m.sender_frame_id = fc->frame_id; m.sender_frame_generation = fc->generation;
    m.target_tab_id = t->tab_id; m.target_frame_id = fr->frame_id; m.target_frame_generation = fr->generation;
    strcpy(m.sender_origin, "http://example.com"); /* lie */
    strcpy(m.target_origin, "http://example.com");
    strcpy(m.payload, "x"); m.payload_length = 1;
    CHECK(browser_host_post_message_send(&m) == 0);
    static firstos_post_message_t rx;
    CHECK(browser_host_post_message_receive(t->tab_id, fr->frame_id, &rx) == 1);
    CHECK(strcmp(rx.sender_origin, "http://other.com") == 0);
    /* Forged sender generation */
    m.sender_frame_generation += 7;
    CHECK(browser_host_post_message_send(&m) == -3);
    /* Cross-tab target */
    m.sender_frame_generation = fc->generation;
    m.target_tab_id = t->tab_id + 50;
    CHECK(browser_host_post_message_send(&m) == -4);
}

static void test_postmessage_stale_generation_rejection(void) {
    printf("[test_iframe] 12. stale target generation\n");
    firstos_tab_t *t = fresh_tab("http://example.com/");
    uint32_t root = t->frame_tree.root_frame_id;
    int c = browser_host_frame_create(t->tab_id, 0, "http://example.com/a.html", 0, 0, 0, 0);
    firstos_frame_t *fc = browser_host_frame_get(t->tab_id, (uint32_t)c);
    firstos_frame_t *fr = browser_host_frame_get(t->tab_id, root);
    static firstos_post_message_t m;
    memset(&m, 0, sizeof(m));
    m.version = FIRSTOS_BROWSER_IPC_VERSION; m.size = sizeof(m);
    m.sender_tab_id = t->tab_id; m.sender_tab_generation = t->generation;
    m.sender_frame_id = root; m.sender_frame_generation = fr->generation;
    m.target_tab_id = t->tab_id; m.target_frame_id = (uint32_t)c; m.target_frame_generation = fc->generation;
    strcpy(m.target_origin, "http://example.com");
    uint32_t old_gen = fc->generation;
    browser_host_frame_destroy(t->tab_id, (uint32_t)c);
    int c2 = browser_host_frame_create(t->tab_id, 0, "http://example.com/a.html", 0, 0, 0, 0);
    CHECK((uint32_t)c2 != (uint32_t)c);
    CHECK(browser_host_post_message_send(&m) == -5);
    m.target_frame_id = (uint32_t)c2; m.target_frame_generation = old_gen;
    CHECK(browser_host_post_message_send(&m) == -5);
}

static void test_postmessage_payload_bounds(void) {
    printf("[test_iframe] 13. payload bounds and queue bound\n");
    firstos_tab_t *t = fresh_tab("http://example.com/");
    uint32_t root = t->frame_tree.root_frame_id;
    int c = browser_host_frame_create(t->tab_id, 0, "http://example.com/a.html", 0, 0, 0, 0);
    static char big[FIRSTOS_MAX_MESSAGE_BYTES + 2];
    memset(big, 'A', sizeof(big) - 1); big[sizeof(big) - 1] = '\0';
    CHECK(browser_host_post_message_between(t->tab_id, root, (uint32_t)c, big, "http://example.com") == -2);
    big[FIRSTOS_MAX_MESSAGE_BYTES] = '\0';
    CHECK(browser_host_post_message_between(t->tab_id, root, (uint32_t)c, big, "http://example.com") == 0);
    int ok = 1;
    for (int i = 1; i < FIRSTOS_MAX_MESSAGE_QUEUE; i++)
        if (browser_host_post_message_between(t->tab_id, root, (uint32_t)c, "q", "/") != 0) ok = 0;
    CHECK(ok);
    CHECK(browser_host_post_message_between(t->tab_id, root, (uint32_t)c, "q", "/") == -7);
}

static void test_frame_crash_isolation(void) {
    printf("[test_iframe] 14. child crash isolation\n");
    firstos_tab_t *t = fresh_tab("http://example.com/");
    uint32_t root = t->frame_tree.root_frame_id;
    int a = browser_host_frame_create(t->tab_id, 0, "http://other.com/", 0, 0, 0, 0);
    int b = browser_host_frame_create(t->tab_id, 0, "http://example.com/b", 0, 0, 0, 0);
    int a1 = browser_host_frame_create(t->tab_id, (uint32_t)a, "http://other.com/n", 0, 0, 0, 0);
    browser_host_post_message_between(t->tab_id, root, (uint32_t)a, "pending", "http://other.com");
    CHECK(browser_host_frame_handle_crash(t->tab_id, (uint32_t)a) == 0);
    CHECK(browser_host_frame_get(t->tab_id, (uint32_t)a)->state == FIRSTOS_FRAME_CRASHED);
    CHECK(browser_host_frame_get(t->tab_id, (uint32_t)a1) == NULL);
    CHECK(browser_host_frame_get(t->tab_id, root)->state == FIRSTOS_FRAME_READY);
    CHECK(browser_host_frame_get(t->tab_id, (uint32_t)b)->state == FIRSTOS_FRAME_READY);
    CHECK(t->state == FIRSTOS_TAB_READY);
    CHECK(browser_host_post_message_receive(t->tab_id, (uint32_t)a, NULL) == -1);
    CHECK(browser_host_post_message_between(t->tab_id, root, (uint32_t)a, "x", "http://other.com") == -5);
    CHECK(browser_host_post_message_between(t->tab_id, (uint32_t)a, root, "x", "http://example.com") == -3);
}

static void test_parent_frame_close_cascade(void) {
    printf("[test_iframe] 15. bottom-up cascade on destroy / navigate / tab close\n");
    firstos_tab_t *t = fresh_tab("http://example.com/");
    uint32_t total0 = browser_host_frame_total_count();
    int a = browser_host_frame_create(t->tab_id, 0, "http://example.com/a", 0, 0, 0, 0);
    int a1 = browser_host_frame_create(t->tab_id, (uint32_t)a, "http://example.com/a1", 0, 0, 0, 0);
    int a2 = browser_host_frame_create(t->tab_id, (uint32_t)a1, "http://example.com/a2", 0, 0, 0, 0);
    int b = browser_host_frame_create(t->tab_id, 0, "http://example.com/b", 0, 0, 0, 0);
    CHECK(browser_host_frame_total_count() == total0 + 4);
    CHECK(browser_host_frame_destroy(t->tab_id, (uint32_t)a) == 0);
    CHECK(browser_host_frame_get(t->tab_id, (uint32_t)a1) == NULL);
    CHECK(browser_host_frame_get(t->tab_id, (uint32_t)a2) == NULL);
    CHECK(browser_host_frame_get(t->tab_id, (uint32_t)b) != NULL);
    CHECK(browser_host_frame_total_count() == total0 + 1);
    CHECK(browser_host_frame_destroy(t->tab_id, t->frame_tree.root_frame_id) == FIRSTOS_FRAME_ACCESS_DENY_PARENT);
    /* Top-level navigation kills child documents */
    browser_host_tab_navigate(t->tab_id, "http://example.com/next");
    CHECK(browser_host_frame_get(t->tab_id, (uint32_t)b) == NULL);
    CHECK(t->frame_tree.frame_count == 1);
    /* Tab close frees whole tree */
    int t2 = browser_host_tab_create("http://example.org/", 0);
    browser_host_frame_create((uint32_t)t2, 0, "http://example.org/c", 0, 0, 0, 0);
    uint32_t before = browser_host_frame_total_count();
    browser_host_tab_close((uint32_t)t2);
    CHECK(browser_host_frame_total_count() == before - 2);
}

static void test_abi_struct_invariants(void) {
    printf("[test_iframe] 16. ABI invariants\n");
    CHECK(sizeof(task_t) == 52);
    CHECK(sizeof(firstos_render_command_t) == 32);
    CHECK(sizeof(firstos_form_submit_request_t) == 32);
    CHECK(sizeof(firstos_post_message_t) == 1332);
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 5C: Frame Tree & Secure postMessage (Host)\n");
    printf("============================================================\n");
    test_root_frame_creation();
    test_iframe_creation_and_capacity();
    test_total_frame_limit();
    test_frame_nesting_depth_limit();
    test_iframe_relative_url_resolution();
    test_embed_scheme_policy();
    test_iframe_sop_and_origin_binding();
    test_same_origin_frame_access();
    test_postmessage_exact_origin_success();
    test_postmessage_target_origin_mismatch();
    test_postmessage_sender_spoofing();
    test_postmessage_stale_generation_rejection();
    test_postmessage_payload_bounds();
    test_frame_crash_isolation();
    test_parent_frame_close_cascade();
    test_abi_struct_invariants();
    printf("ALL PHASE 5C HOST TESTS PASSED (16 tests, %d assertions)\n", s_pass);
    return 0;
}
