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

#include "../download_manager.h"
#include "../http_v11.h"
#include "../include/firstos/uapi/browser_ipc.h"

/* Mock filesystem for host unit test */
static char s_mock_fs_files[16][128];
static char s_mock_fs_data[16][4096];
static int  s_mock_fs_lens[16];
static int  s_mock_fs_count = 0;

void fs_mkdir(const char *path) { (void)path; }
int fs_create_at(const char *path, int is_dir) {
    (void)is_dir;
    for (int i = 0; i < s_mock_fs_count; i++) {
        if (strcmp(s_mock_fs_files[i], path) == 0) return 1;
    }
    if (s_mock_fs_count >= 16) return 0;
    strncpy(s_mock_fs_files[s_mock_fs_count], path, 127);
    s_mock_fs_files[s_mock_fs_count][127] = 0;
    s_mock_fs_data[s_mock_fs_count][0] = 0;
    s_mock_fs_lens[s_mock_fs_count] = 0;
    s_mock_fs_count++;
    return 1;
}

int fs_write_at(const char *path, const char *data, uint32_t size) {
    for (int i = 0; i < s_mock_fs_count; i++) {
        if (strcmp(s_mock_fs_files[i], path) == 0) {
            if (size > sizeof(s_mock_fs_data[i])) size = sizeof(s_mock_fs_data[i]);
            memcpy(s_mock_fs_data[i] + s_mock_fs_lens[i], data, size);
            s_mock_fs_lens[i] += size;
            return 1;
        }
    }
    return 0;
}

int fs_read_at(const char *path, char *out, uint32_t cap) {
    for (int i = 0; i < s_mock_fs_count; i++) {
        if (strcmp(s_mock_fs_files[i], path) == 0) {
            if (out && cap > 0) {
                uint32_t sz = s_mock_fs_lens[i] < (int)cap ? (uint32_t)s_mock_fs_lens[i] : cap;
                memcpy(out, s_mock_fs_data[i], sz);
            }
            return s_mock_fs_lens[i];
        }
    }
    return -1;
}

int fs_rename_at(const char *old_path, const char *new_path) {
    int old_idx = -1;
    for (int i = 0; i < s_mock_fs_count; i++) {
        if (strcmp(s_mock_fs_files[i], old_path) == 0) { old_idx = i; break; }
    }
    if (old_idx < 0) return 0;
    for (int i = 0; i < s_mock_fs_count; i++) {
        if (strcmp(s_mock_fs_files[i], new_path) == 0) return 0; /* No overwrite */
    }
    strncpy(s_mock_fs_files[old_idx], new_path, 127);
    return 1;
}

int fs_delete_at(const char *path) {
    for (int i = 0; i < s_mock_fs_count; i++) {
        if (strcmp(s_mock_fs_files[i], path) == 0) {
            s_mock_fs_files[i][0] = 0;
            s_mock_fs_lens[i] = 0;
            return 1;
        }
    }
    return 0;
}

void serial_write(const char *str) { (void)str; }
void serial_write_hex(uint32_t v) { (void)v; }
uint32_t clock_monotonic_get_ms(void) { return 100000; }
uint32_t get_system_seconds(void) { return 100; }
int http_stream(const http_request_t *req, http_stream_cb cb, void *user_data) {
    (void)req; (void)cb; (void)user_data;
    return 1;
}

static void reset_mock_fs(void) {
    s_mock_fs_count = 0;
    memset(s_mock_fs_files, 0, sizeof(s_mock_fs_files));
    memset(s_mock_fs_data, 0, sizeof(s_mock_fs_data));
    memset(s_mock_fs_lens, 0, sizeof(s_mock_fs_lens));
}

/* Unit Tests */

static void test_abi_struct_invariants(void) {
    printf("[TEST] test_abi_struct_invariants...\n");
    assert(sizeof(firstos_render_command_t) == 32);
    assert(sizeof(firstos_form_submit_request_t) == 32);
    assert(sizeof(firstos_post_message_t) == 1332);
    assert(sizeof(firstos_download_request_t) == 32);
    printf("  -> PASS\n");
}

static void test_filename_sanitizer(void) {
    printf("[TEST] test_filename_sanitizer...\n");
    char out[128];

    /* Path traversal stripping */
    download_sanitize_filename("../../etc/passwd", out, sizeof(out));
    assert(strcmp(out, "passwd") == 0);

    /* Windows backslash traversal */
    download_sanitize_filename("..\\..\\windows\\system32\\calc.exe", out, sizeof(out));
    assert(strcmp(out, "calc.exe") == 0);

    /* Invalid characters replaced with _ */
    download_sanitize_filename("my:file*name?test<1>.pdf", out, sizeof(out));
    assert(strcmp(out, "my_file_name_test_1_.pdf") == 0);

    /* Fallback on empty or dot-only names */
    download_sanitize_filename("...", out, sizeof(out));
    assert(strcmp(out, "download") == 0);

    download_sanitize_filename("", out, sizeof(out));
    assert(strcmp(out, "download") == 0);
    printf("  -> PASS\n");
}

static void test_content_disposition_parser(void) {
    printf("[TEST] test_content_disposition_parser...\n");
    char fn[128];
    int is_attach = 0;

    /* Basic attachment */
    int ok = download_parse_content_disposition("attachment; filename=\"report.pdf\"", fn, sizeof(fn), &is_attach);
    assert(ok == 1);
    assert(is_attach == 1);
    assert(strcmp(fn, "report.pdf") == 0);

    /* Inline */
    ok = download_parse_content_disposition("inline; filename=image.png", fn, sizeof(fn), &is_attach);
    assert(ok == 1);
    assert(is_attach == 0);
    assert(strcmp(fn, "image.png") == 0);

    /* Traversal attempt inside header */
    ok = download_parse_content_disposition("attachment; filename=\"../../exploit.bin\"", fn, sizeof(fn), &is_attach);
    assert(ok == 1);
    assert(is_attach == 1);
    assert(strcmp(fn, "exploit.bin") == 0);

    /* RFC 5987 filename* */
    ok = download_parse_content_disposition("attachment; filename*=UTF-8''my_doc.txt", fn, sizeof(fn), &is_attach);
    assert(ok == 1);
    assert(is_attach == 1);
    assert(strcmp(fn, "my_doc.txt") == 0);
    printf("  -> PASS\n");
}

static void test_collision_resolution(void) {
    printf("[TEST] test_collision_resolution...\n");
    reset_mock_fs();
    char out_path[256];

    /* First file */
    download_resolve_collision("/Downloads", "data.csv", out_path, sizeof(out_path));
    assert(strcmp(out_path, "/Downloads/data.csv") == 0);
    fs_create_at(out_path, 0);

    /* Second file collisions */
    download_resolve_collision("/Downloads", "data.csv", out_path, sizeof(out_path));
    assert(strcmp(out_path, "/Downloads/data (1).csv") == 0);
    fs_create_at(out_path, 0);

    /* Third file collisions */
    download_resolve_collision("/Downloads", "data.csv", out_path, sizeof(out_path));
    assert(strcmp(out_path, "/Downloads/data (2).csv") == 0);
    printf("  -> PASS\n");
}

static void test_download_capacity_and_eviction(void) {
    printf("[TEST] test_download_capacity_and_eviction...\n");
    download_manager_init();

    /* Create up to FIRSTOS_MAX_DOWNLOADS */
    for (int i = 0; i < FIRSTOS_MAX_DOWNLOADS; i++) {
        char url[64];
        snprintf(url, sizeof(url), "http://example.com/file%d.bin", i);
        download_task_t *t = download_create(url, NULL, 1, 1, 1, 1, 1, 1);
        assert(t != NULL);
        assert(t->id == (uint32_t)(i + 1));
    }
    assert(download_get_count() == FIRSTOS_MAX_DOWNLOADS);

    /* Exceed capacity when all active/pending -> rejected */
    download_task_t *overflow = download_create("http://example.com/overflow.bin", NULL, 1, 1, 1, 1, 1, 1);
    assert(overflow == NULL);

    /* Mark one completed -> slots recycled safely */
    download_task_t *t0 = download_find_by_id(1);
    assert(t0 != NULL);
    t0->state = DOWNLOAD_STATE_COMPLETED;

    overflow = download_create("http://example.com/overflow.bin", NULL, 1, 1, 1, 1, 1, 1);
    assert(overflow != NULL);
    assert(overflow->id == FIRSTOS_MAX_DOWNLOADS + 1);
    printf("  -> PASS\n");
}

static void test_download_scheme_policy(void) {
    printf("[TEST] test_download_scheme_policy...\n");
    download_manager_init();

    /* Only http and https allowed */
    assert(download_create("file:///etc/shadow", NULL, 1, 1, 1, 1, 1, 1) == NULL);
    assert(download_create("data:text/plain;base64,AAA", NULL, 1, 1, 1, 1, 1, 1) == NULL);
    assert(download_create("javascript:alert(1)", NULL, 1, 1, 1, 1, 1, 1) == NULL);
    assert(download_create("http://example.com/test.bin", NULL, 1, 1, 1, 1, 1, 1) != NULL);
    assert(download_create("https://example.com/secure.bin", NULL, 1, 1, 1, 1, 1, 1) != NULL);
    printf("  -> PASS\n");
}

static void test_cancellation_and_tab_close(void) {
    printf("[TEST] test_cancellation_and_tab_close...\n");
    reset_mock_fs();
    download_manager_init();

    download_task_t *t1 = download_create("http://example.com/tab1.bin", NULL, 1, 1, 1, 1, 1, 1);
    download_task_t *t2 = download_create("http://example.com/tab2.bin", NULL, 2, 1, 1, 1, 1, 1);
    assert(t1 != NULL && t2 != NULL);

    /* Create dummy part files */
    fs_create_at(t1->part_path, 0);
    fs_create_at(t2->part_path, 0);
    assert(fs_read_at(t1->part_path, NULL, 0) >= 0);

    /* Cancel for Tab 1 */
    download_cancel_for_tab(1);
    assert(t1->state == DOWNLOAD_STATE_CANCELLED);
    assert(t2->state == DOWNLOAD_STATE_PENDING);

    /* Part file deleted */
    assert(fs_read_at(t1->part_path, NULL, 0) < 0);
    assert(fs_read_at(t2->part_path, NULL, 0) >= 0);
    printf("  -> PASS\n");
}

int main(void) {
    printf("=== Running Phase 6A.1 Download Manager Unit Tests ===\n");
    test_abi_struct_invariants();
    test_filename_sanitizer();
    test_content_disposition_parser();
    test_collision_resolution();
    test_download_capacity_and_eviction();
    test_download_scheme_policy();
    test_cancellation_and_tab_close();
    printf(">>> All 7 Download Manager unit tests PASSED! <<<\n");
    return 0;
}
