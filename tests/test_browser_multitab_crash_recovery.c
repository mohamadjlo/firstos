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

void image_free(uint8_t *data) {
    (void)data;
}

uint8_t *image_load_from_memory(const uint8_t *buf, int len, int *w, int *h) {
    (void)buf; (void)len; (void)w; (void)h;
    return NULL;
}

void *kmalloc(uint32_t size) {
    static char s_heap_pool[262144];
    static uint32_t s_heap_pos = 0;
    uint32_t aligned = (size + 15) & ~15;
    if (s_heap_pos + aligned > sizeof(s_heap_pool)) return NULL;
    void *ptr = &s_heap_pool[s_heap_pos];
    s_heap_pos += aligned;
    return ptr;
}

void kfree(void *ptr) {
    (void)ptr;
}

void font_text(int x, int y, const char *text, unsigned int color, int scale) {
    (void)x; (void)y; (void)text; (void)color; (void)scale;
}

void fb_rect(int x, int y, int w, int h, unsigned int c) {
    (void)x; (void)y; (void)w; (void)h; (void)c;
}

void fb_border(int x, int y, int w, int h, unsigned int c) {
    (void)x; (void)y; (void)w; (void)h; (void)c;
}

void fb_putpixel(int x, int y, unsigned int c) {
    (void)x; (void)y; (void)c;
}

unsigned int fb_read_pixel(int x, int y) {
    (void)x; (void)y;
    return 0;
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
static int s_mock_next_pid = 2;

int process_spawn(int parent_pid, const char *path, const char *const *argv) {
    (void)parent_pid; (void)path; (void)argv;
    int pid = s_mock_next_pid++;
    if (pid >= FIRSTOS_MAX_PROCESSES) pid = 2;
    process_table[pid].state = PROC_STATE_READY;
    return pid;
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

/* Include implementation */
#include "browser_security.c"
#include "browser_host_ipc.c"

/* 1. Test Multiple Tab Creation and Isolated Slots */
static void test_multitab_slot_isolation(void) {
    printf("[test_phase6_recovery] 1. test_multitab_slot_isolation...\n");
    browser_host_tab_manager_reset();
    browser_host_ipc_init();

    int tab1_id = 1;
    int tab2_id = browser_host_tab_create("https://alpha.org", 0);
    int tab3_id = browser_host_tab_create("https://beta.org", 0);

    firstos_tab_t *t1 = browser_host_get_tab_by_id(tab1_id);
    firstos_tab_t *t2 = browser_host_get_tab_by_id(tab2_id);
    firstos_tab_t *t3 = browser_host_get_tab_by_id(tab3_id);

    assert(t1 != NULL && t2 != NULL && t3 != NULL);
    assert(t1->renderer_slot_id != t2->renderer_slot_id);
    assert(t2->renderer_slot_id != t3->renderer_slot_id);

    assert(t1->committed_origin.scheme == FIRSTOS_ORIGIN_ABOUT);
    assert(t2->committed_origin.scheme == FIRSTOS_ORIGIN_HTTPS);
    assert(strcmp(t2->committed_origin.host, "alpha.org") == 0);
    assert(strcmp(t3->committed_origin.host, "beta.org") == 0);

    printf("  -> OK (3 tabs created with distinct slots and isolated origins)\n");
}

/* 2. Test Isolated Tab Crash Containment */
static void test_tab_crash_containment(void) {
    printf("[test_phase6_recovery] 2. test_tab_crash_containment...\n");
    browser_host_tab_manager_reset();
    browser_host_ipc_init();

    int tab2_id = browser_host_tab_create("https://faulty.com", 0);
    int tab3_id = browser_host_tab_create("https://stable.com", 0);

    firstos_tab_t *t1 = browser_host_get_tab_by_id(1);
    firstos_tab_t *t2 = browser_host_get_tab_by_id(tab2_id);
    firstos_tab_t *t3 = browser_host_get_tab_by_id(tab3_id);

    /* Simulate crash in Tab 2 only */
    int res = browser_host_tab_handle_crash(tab2_id);
    assert(res == 0);

    /* Tab 2 must be crashed, but Tab 1 and Tab 3 must remain healthy */
    assert(t2->state == FIRSTOS_TAB_CRASHED);
    assert(t2->has_error == 1);
    assert(t1->state == FIRSTOS_TAB_READY);
    assert(t3->state == FIRSTOS_TAB_READY);

    firstos_tab_manager_t *tm = browser_host_get_tab_manager();
    assert(tm->tab_count == 3);

    printf("  -> OK (Tab 2 crash contained: sibling tabs unaffected)\n");
}

/* 3. Test Input Gating to Dead/Crashed Tab */
static void test_input_gating_to_crashed_tab(void) {
    printf("[test_phase6_recovery] 3. test_input_gating_to_crashed_tab...\n");
    browser_host_tab_manager_reset();
    browser_host_ipc_init();

    int tab2_id = browser_host_tab_create("https://crashtest.com", 1);
    firstos_tab_t *act = browser_host_get_active_tab();
    assert(act != NULL && act->tab_id == (uint32_t)tab2_id);

    /* Tab healthy -> input succeeds */
    s_renderer_pid = 2;
    process_table[2].state = PROC_STATE_READY;
    assert(browser_host_ipc_send_input(1, 50, 50, 0) == 0);

    /* Crash Tab 2 -> input strictly rejected (-1) */
    browser_host_tab_handle_crash(tab2_id);
    assert(browser_host_ipc_send_input(1, 50, 50, 0) == -1);

    printf("  -> OK (Input events to crashed tab safely gated and rejected)\n");
}

/* 4. Test Tab Respawn & Crash Recovery Flow */
static void test_tab_respawn_and_recovery(void) {
    printf("[test_phase6_recovery] 4. test_tab_respawn_and_recovery...\n");
    browser_host_tab_manager_reset();
    browser_host_ipc_init();

    int tab2_id = browser_host_tab_create("https://recoverable.org", 1);
    firstos_tab_t *tab = browser_host_get_tab_by_id(tab2_id);
    assert(tab != NULL);

    uint32_t old_nav = tab->navigation_id;
    uint32_t old_gen = tab->document_generation;

    /* Crash tab */
    browser_host_tab_handle_crash(tab2_id);
    assert(tab->state == FIRSTOS_TAB_CRASHED);

    /* Reload / Respawn tab */
    int reload_res = browser_host_tab_reload(tab2_id);
    assert(reload_res == 0);

    /* Tab must now be revived to READY with new navigation generation */
    assert(tab->state == FIRSTOS_TAB_READY);
    assert(tab->has_error == 0);
    assert(tab->navigation_id > old_nav);
    assert(tab->document_generation > old_gen);
    assert(tab->renderer_pid > 0);

    /* Input should now succeed again */
    assert(browser_host_ipc_send_input(1, 100, 100, 0) == 0);

    printf("  -> OK (Crashed tab cleanly respawned and recovered into READY state)\n");
}

/* 5. Test Stale Render Frame Dropping Across Navigations */
static void test_stale_render_frame_dropping(void) {
    printf("[test_phase6_recovery] 5. test_stale_render_frame_dropping...\n");
    browser_host_tab_manager_reset();
    browser_host_ipc_init();

    firstos_tab_t *act = browser_host_get_active_tab();
    assert(act != NULL);
    act->navigation_id = 10;

    /* Simulate incoming frame with stale navigation_id = 9 */
    firstos_browser_channel_t *r2h = (firstos_browser_channel_t *)(s_shm_phys + FIRSTOS_BROWSER_CHANNEL_R2H_OFF);
    r2h->msg.version = FIRSTOS_BROWSER_IPC_VERSION;
    r2h->msg.type = FIRSTOS_BROWSER_MSG_RENDER_FRAME;
    r2h->msg.status = 9; /* Stale nav id */
    r2h->has_message = 1;

    s_serial_log_pos = 0;
    s_serial_log[0] = '\0';

    browser_host_ipc_poll_render_commands(NULL, 0, 0, 800, 600);
    assert(r2h->has_message == 0); /* Host dropped and cleared the message */
    assert(strstr(s_serial_log, "dropped STALE render frame") != NULL);

    printf("  -> OK (Stale frames from prior navigations dropped without presenting)\n");
}

/* 6. Test ABI Struct Invariants */
static void test_abi_struct_invariants(void) {
    printf("[test_phase6_recovery] 6. test_abi_struct_invariants...\n");
    assert(sizeof(task_t) == 64);
    assert(sizeof(firstos_browser_message_t) == 32);
    assert(sizeof(firstos_render_command_t) == 32);
    assert(sizeof(firstos_form_submit_request_t) == 32);
    assert(sizeof(firstos_damage_rect_t) == 32);
    assert(sizeof(firstos_download_request_t) == 32);
    printf("  -> OK (All 32-byte and 64-byte ABI structures verified)\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 6: Multi-Tab Site Isolation & Crash Recovery Tests\n");
    printf("============================================================\n");

    test_multitab_slot_isolation();
    test_tab_crash_containment();
    test_input_gating_to_crashed_tab();
    test_tab_respawn_and_recovery();
    test_stale_render_frame_dropping();
    test_abi_struct_invariants();

    printf("============================================================\n");
    printf("ALL 6 PHASE 6 MULTI-TAB & RECOVERY TESTS PASSED (6/6)\n");
    printf("============================================================\n");
    return 0;
}
