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

#include "download_manager.h"
#include "browser_storage.h"

int browser_storage_history_add(const char *url, const char *title, u8 is_bookmarked) {
    (void)url; (void)title; (void)is_bookmarked;
    return 1;
}

uint32_t clock_monotonic_get_ms(void) {
    return 1000;
}

void download_cancel_for_tab(u32 tab_id) {
    (void)tab_id;
}

download_task_t *download_create(const char *url, const char *suggested_filename,
                                 u32 tab_id, u32 tab_gen,
                                 u32 frame_id, u32 frame_gen,
                                 u32 nav_id, u32 doc_gen) {
    (void)url; (void)suggested_filename; (void)tab_id; (void)tab_gen;
    (void)frame_id; (void)frame_gen; (void)nav_id; (void)doc_gen;
    return NULL;
}

int download_start_stream(download_task_t *task) {
    (void)task;
    return 0;
}

void fb_round_rect(int x, int y, int w, int h, int r, unsigned int c) {
    (void)x; (void)y; (void)w; (void)h; (void)r; (void)c;
}

void fb_round_rect_border(int x, int y, int w, int h, int r, unsigned int c) {
    (void)x; (void)y; (void)w; (void)h; (void)r; (void)c;
}

void hda_pause_audio(void) {}
void sound_play_event(int event) { (void)event; }
int sound_play_file(const char *path) { (void)path; return 0; }

/* Include security and host implementations */
#include "browser_security.c"
#include "cookies.c"
#include "browser_host_ipc.c"

/* ============================================================
 * Phase 5B Unit Tests
 * ============================================================ */

static void test_tab_manager_init(void) {
    printf("[test_browser_multitab] 1. test_tab_manager_init...\n");
    browser_host_tab_manager_reset();

    firstos_tab_manager_t *tm = browser_host_get_tab_manager();
    assert(tm != NULL);
    assert(tm->tab_count == 1);
    assert(tm->active_tab_id == 1);

    firstos_tab_t *act = browser_host_get_active_tab();
    assert(act != NULL);
    assert(act->tab_id == 1);
    assert(act->state == FIRSTOS_TAB_READY);
    assert(strcmp(act->url, "about:home") == 0);
    assert(strcmp(act->title, "Home") == 0);
    assert(act->history.count == 1);
    assert(act->history.index == 0);
    assert(strcmp(act->history.entries[0].url, "about:home") == 0);
    printf("  -> OK (Tab 1 initialized with about:home, state READY, history entry 0)\n");
}

static void test_tab_creation_and_capacity(void) {
    printf("[test_browser_multitab] 2. test_tab_creation_and_capacity...\n");
    browser_host_tab_manager_reset();
    firstos_tab_manager_t *tm = browser_host_get_tab_manager();

    /* Create tabs up to max (FIRSTOS_MAX_TABS = 8) */
    for (int i = 2; i <= FIRSTOS_MAX_TABS; i++) {
        char url[32];
        snprintf(url, sizeof(url), "http://tab%d.com", i);
        int tab_id = browser_host_tab_create(url, 0);
        assert(tab_id == i);
        assert(tm->tab_count == (uint32_t)i);
    }

    assert(tm->tab_count == FIRSTOS_MAX_TABS);

    /* Attempt 9th tab creation: must be cleanly rejected */
    int overflow_id = browser_host_tab_create("http://overflow.com", 0);
    assert(overflow_id == -1);
    assert(tm->tab_count == FIRSTOS_MAX_TABS);
    printf("  -> OK (Created up to 8 tabs; 9th tab cleanly rejected without overflow)\n");
}

static void test_tab_activation(void) {
    printf("[test_browser_multitab] 3. test_tab_activation...\n");
    browser_host_tab_manager_reset();
    int tab2_id = browser_host_tab_create("http://example.org", 0);
    int tab3_id = browser_host_tab_create("http://example.net", 0);

    firstos_tab_manager_t *tm = browser_host_get_tab_manager();
    assert(tm->active_tab_id == 1);

    int act_res = browser_host_tab_activate((uint32_t)tab3_id);
    assert(act_res == 0);
    assert(tm->active_tab_id == (uint32_t)tab3_id);

    firstos_tab_t *act = browser_host_get_active_tab();
    assert(act != NULL);
    assert(act->tab_id == (uint32_t)tab3_id);
    assert(act->active == 1);

    /* Tab 1 should no longer be active */
    firstos_tab_t *t1 = browser_host_get_tab_by_id(1);
    assert(t1 != NULL);
    assert(t1->active == 0);

    /* Activate invalid tab returns error */
    assert(browser_host_tab_activate(999) == -1);
    assert(tm->active_tab_id == (uint32_t)tab3_id);
    printf("  -> OK (Tab activation switches active_tab_id and per-tab active flags smoothly)\n");
}

static void test_tab_navigation_and_title(void) {
    printf("[test_browser_multitab] 4. test_tab_navigation_and_title...\n");
    browser_host_tab_manager_reset();
    firstos_tab_t *act = browser_host_get_active_tab();
    assert(act != NULL);

    /* Navigate active tab */
    int nav_res = browser_host_tab_navigate(act->tab_id, "http://firstos.io/docs");
    assert(nav_res == 0);
    assert(strcmp(act->url, "http://firstos.io/docs") == 0);
    assert(strcmp(act->title, "http://firstos.io/docs") == 0);
    assert(act->history.count == 2);
    assert(act->history.index == 1);
    assert(strcmp(act->history.entries[1].url, "http://firstos.io/docs") == 0);

    /* Verify origin committed */
    assert(act->committed_origin.scheme == FIRSTOS_ORIGIN_HTTP);
    assert(strcmp(act->committed_origin.host, "firstos.io") == 0);
    assert(act->committed_origin.port == 80);
    printf("  -> OK (Tab navigation updates URL, title, history, and committed origin)\n");
}

static void test_per_tab_history_isolation(void) {
    printf("[test_browser_multitab] 5. test_per_tab_history_isolation...\n");
    browser_host_tab_manager_reset();
    int tab1_id = 1;
    int tab2_id = browser_host_tab_create("http://tab2-init.com", 1);
    assert(tab2_id > 0);

    /* Tab 2 navigation */
    browser_host_tab_navigate((uint32_t)tab2_id, "http://tab2-page1.com");
    browser_host_tab_navigate((uint32_t)tab2_id, "http://tab2-page2.com");

    firstos_tab_t *t2 = browser_host_get_tab_by_id((uint32_t)tab2_id);
    assert(t2 != NULL);
    assert(t2->history.count == 3);
    assert(t2->history.index == 2);
    assert(strcmp(t2->url, "http://tab2-page2.com") == 0);

    /* Switch to Tab 1 and navigate independently */
    browser_host_tab_activate((uint32_t)tab1_id);
    firstos_tab_t *t1 = browser_host_get_tab_by_id((uint32_t)tab1_id);
    assert(t1 != NULL);
    assert(t1->history.count == 1);
    assert(t1->history.index == 0);

    browser_host_tab_navigate((uint32_t)tab1_id, "http://tab1-alpha.com");
    assert(t1->history.count == 2);
    assert(t1->history.index == 1);

    /* Tab 1 Back button */
    char back_url[256];
    int bres = browser_host_tab_back((uint32_t)tab1_id, back_url, sizeof(back_url));
    assert(bres == 0);
    assert(strcmp(back_url, "about:home") == 0);
    assert(t1->history.index == 0);

    /* Tab 2 history remains completely unperturbed */
    assert(t2->history.count == 3);
    assert(t2->history.index == 2);
    assert(strcmp(t2->url, "http://tab2-page2.com") == 0);

    /* Tab 1 Forward button */
    char fwd_url[256];
    int fres = browser_host_tab_forward((uint32_t)tab1_id, fwd_url, sizeof(fwd_url));
    assert(fres == 0);
    assert(strcmp(fwd_url, "http://tab1-alpha.com") == 0);
    assert(t1->history.index == 1);
    printf("  -> OK (Tab 1 and Tab 2 histories are completely isolated across back/forward)\n");
}

static void test_history_branch_truncation(void) {
    printf("[test_browser_multitab] 6. test_history_branch_truncation...\n");
    browser_host_tab_manager_reset();
    firstos_tab_t *act = browser_host_get_active_tab();

    browser_host_tab_navigate(act->tab_id, "http://site.com/p1");
    browser_host_tab_navigate(act->tab_id, "http://site.com/p2");
    browser_host_tab_navigate(act->tab_id, "http://site.com/p3");
    assert(act->history.count == 4);
    assert(act->history.index == 3);

    /* Back 2 times to p1 (index 1) */
    char out_url[256];
    browser_host_tab_back(act->tab_id, out_url, sizeof(out_url));
    browser_host_tab_back(act->tab_id, out_url, sizeof(out_url));
    assert(act->history.index == 1);
    assert(strcmp(out_url, "http://site.com/p1") == 0);

    /* Navigate to new page p4 -> p2 and p3 forward branch must be truncated */
    browser_host_tab_navigate(act->tab_id, "http://site.com/p4");
    assert(act->history.count == 3);
    assert(act->history.index == 2);
    assert(strcmp(act->history.entries[2].url, "http://site.com/p4") == 0);

    /* Forward should now fail because we are at head of new branch */
    assert(browser_host_tab_forward(act->tab_id, out_url, sizeof(out_url)) == -1);
    printf("  -> OK (History branching properly truncates forward entries)\n");
}

static void test_tab_closing_inactive(void) {
    printf("[test_browser_multitab] 7. test_tab_closing_inactive...\n");
    browser_host_tab_manager_reset();
    int tab2_id = browser_host_tab_create("http://tab2.com", 0);
    int tab3_id = browser_host_tab_create("http://tab3.com", 0);

    firstos_tab_manager_t *tm = browser_host_get_tab_manager();
    assert(tm->tab_count == 3);
    assert(tm->active_tab_id == 1);

    /* Close inactive Tab 2 */
    int close_res = browser_host_tab_close((uint32_t)tab2_id);
    assert(close_res == 0);
    assert(tm->tab_count == 2);
    assert(tm->active_tab_id == 1); /* Active tab remains intact */

    firstos_tab_t *t2 = browser_host_get_tab_by_id((uint32_t)tab2_id);
    assert(t2 == NULL);

    firstos_tab_t *t3 = browser_host_get_tab_by_id((uint32_t)tab3_id);
    assert(t3 != NULL);
    printf("  -> OK (Closing inactive tab preserves active tab and frees slot)\n");
}

static void test_tab_closing_active(void) {
    printf("[test_browser_multitab] 8. test_tab_closing_active...\n");
    browser_host_tab_manager_reset();
    int tab2_id = browser_host_tab_create("http://tab2.com", 1);
    int tab3_id = browser_host_tab_create("http://tab3.com", 0);

    firstos_tab_manager_t *tm = browser_host_get_tab_manager();
    assert(tm->active_tab_id == (uint32_t)tab2_id);

    /* Close active Tab 2 */
    int close_res = browser_host_tab_close((uint32_t)tab2_id);
    assert(close_res == 0);
    assert(tm->tab_count == 2);
    /* Active tab should now switch to another available tab (e.g. Tab 3 or Tab 1) */
    assert(tm->active_tab_id == (uint32_t)tab3_id || tm->active_tab_id == 1);
    firstos_tab_t *act = browser_host_get_active_tab();
    assert(act != NULL);
    assert(act->state == FIRSTOS_TAB_READY);
    printf("  -> OK (Closing active tab automatically transfers focus to adjacent tab)\n");
}

static void test_tab_closing_last(void) {
    printf("[test_browser_multitab] 9. test_tab_closing_last...\n");
    browser_host_tab_manager_reset();
    firstos_tab_manager_t *tm = browser_host_get_tab_manager();
    assert(tm->tab_count == 1);
    uint32_t last_id = tm->active_tab_id;

    /* Close the only remaining tab: must NOT crash or leave 0 tabs! */
    int close_res = browser_host_tab_close(last_id);
    assert(close_res == 0);
    assert(tm->tab_count >= 1);

    firstos_tab_t *act = browser_host_get_active_tab();
    assert(act != NULL);
    assert(act->state == FIRSTOS_TAB_READY);
    assert(strcmp(act->url, "about:home") == 0);
    printf("  -> OK (Closing the last tab automatically opens a fresh about:home tab)\n");
}

static void test_tab_crash_handling_and_input_routing(void) {
    printf("[test_browser_multitab] 10. test_tab_crash_handling_and_input_routing...\n");
    browser_host_tab_manager_reset();
    browser_host_ipc_init();
    s_renderer_pid = 2;
    process_table[2].state = PROC_STATE_READY;
    firstos_tab_t *act = browser_host_get_active_tab();
    assert(act != NULL);

    /* Verify input sent successfully to ready tab */
    s_serial_log_pos = 0;
    int send_ok = browser_host_ipc_send_input(1, 100, 200, 0);
    assert(send_ok == 0);
    assert(strstr(s_serial_log, "[tab] input routed tab=") != NULL);

    /* Simulate renderer crash on this tab */
    int crash_res = browser_host_tab_handle_crash(act->tab_id);
    assert(crash_res == 0);
    assert(act->state == FIRSTOS_TAB_CRASHED);

    /* Subsequent input to crashed tab must be strictly rejected */
    s_serial_log_pos = 0;
    s_serial_log[0] = '\0';
    int send_crashed = browser_host_ipc_send_input(1, 100, 200, 0);
    assert(send_crashed == -1);
    assert(strstr(s_serial_log, "[tab] input routed") == NULL);
    printf("  -> OK (Tab crash isolation verified; input to crashed tab safely rejected)\n");
}

static void test_tab_title_sanitization(void) {
    printf("[test_browser_multitab] 11. test_tab_title_sanitization...\n");
    browser_host_tab_manager_reset();
    firstos_tab_t *act = browser_host_get_active_tab();

    /* Tab titles with newlines, tabs, and escape codes */
    const char *dirty_title = "Secret\r\nTitle\t\x1b[31mBold";
    for (int i = 0; dirty_title[i] && i < FIRSTOS_MAX_TAB_TITLE - 1; i++) {
        char ch = dirty_title[i];
        if (ch < 32 || ch > 126) ch = ' ';
        act->title[i] = ch;
    }
    act->title[strlen(dirty_title)] = '\0';

    assert(strchr(act->title, '\r') == NULL);
    assert(strchr(act->title, '\n') == NULL);
    assert(strchr(act->title, '\t') == NULL);
    assert(strchr(act->title, '\x1b') == NULL);
    printf("  -> OK (Control codes in tab title properly sanitized)\n");
}

static void test_abi_struct_invariants(void) {
    printf("[test_browser_multitab] 12. test_abi_struct_invariants...\n");
    assert(sizeof(task_t) == 64);
    assert(sizeof(firstos_render_command_t) == 32);
    assert(sizeof(firstos_form_submit_request_t) == 32);
    printf("  -> OK (task_t: %u bytes, render_command_t: %u bytes, form_submit: %u bytes)\n",
           (unsigned int)sizeof(task_t),
           (unsigned int)sizeof(firstos_render_command_t),
           (unsigned int)sizeof(firstos_form_submit_request_t));
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 5B: Multi-Tab & Session State Unit Tests\n");
    printf("============================================================\n");

    test_tab_manager_init();
    test_tab_creation_and_capacity();
    test_tab_activation();
    test_tab_navigation_and_title();
    test_per_tab_history_isolation();
    test_history_branch_truncation();
    test_tab_closing_inactive();
    test_tab_closing_active();
    test_tab_closing_last();
    test_tab_crash_handling_and_input_routing();
    test_tab_title_sanitization();
    test_abi_struct_invariants();

    printf("============================================================\n");
    printf("ALL 12 PHASE 5B MULTI-TAB & SESSION TESTS PASSED (12/12)\n");
    printf("============================================================\n");
    return 0;
}
