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
void *memcpy(void *dest, const void *src, unsigned int n);
void *memset(void *s, int c, unsigned int n);

#include "../browser_storage.h"
#include "../include/firstos/uapi/browser_ipc.h"

/* Mock filesystem for host unit test */
static char s_mock_fs_files[16][128];
static char s_mock_fs_data[16][65536];
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
    s_mock_fs_lens[s_mock_fs_count] = 0;
    s_mock_fs_count++;
    return 1;
}

int fs_write_at(const char *path, const char *data, uint32_t size) {
    for (int i = 0; i < s_mock_fs_count; i++) {
        if (strcmp(s_mock_fs_files[i], path) == 0) {
            if (size > sizeof(s_mock_fs_data[i])) size = sizeof(s_mock_fs_data[i]);
            memcpy(s_mock_fs_data[i], data, size);
            s_mock_fs_lens[i] = size;
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
        if (strcmp(s_mock_fs_files[i], new_path) == 0) return 0;
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

int fs_sync(void) { return 1; }
void serial_write(const char *str) { (void)str; }
void serial_write_hex(uint32_t v) { (void)v; }
uint32_t clock_monotonic_get_ms(void) { return 100000; }

/* Mock cookies */
static int s_mock_cookie_count = 0;
int cookies_get_count(void) { return s_mock_cookie_count; }
const void *cookies_get_entry(int index) { (void)index; return NULL; }
int cookie_set(const char *d, const char *p, const char *n, const char *v, int sec, int h) {
    (void)d; (void)p; (void)n; (void)v; (void)sec; (void)h;
    return 1;
}

/* Mock tab manager */
void *browser_host_get_tab_manager(void) { return NULL; }
int   browser_host_tab_create(const char *url, int act) { (void)url; (void)act; return 1; }

static void reset_mock_fs(void) {
    s_mock_fs_count = 0;
    memset(s_mock_fs_files, 0, sizeof(s_mock_fs_files));
    memset(s_mock_fs_data, 0, sizeof(s_mock_fs_data));
    memset(s_mock_fs_lens, 0, sizeof(s_mock_fs_lens));
}

/* Tests */

static void test_crc32_computation(void) {
    printf("[TEST] test_crc32_computation...\n");
    const char *msg = "123456789";
    uint32_t crc = browser_storage_crc32(msg, 9);
    /* Standard IEEE 802.3 CRC32 for "123456789" is 0xCBF43926 */
    assert(crc == 0xCBF43926u);
    printf("  -> PASS (CRC32: 0x%08X)\n", crc);
}

static void test_history_records_and_eviction(void) {
    printf("[TEST] test_history_records_and_eviction...\n");
    browser_storage_init();

    int idx0 = browser_storage_history_add("http://example.com/p0", "Page 0", 0);
    assert(idx0 == 0);
    assert(browser_storage_history_get_count() == 1);

    const firstos_history_record_t *r0 = browser_storage_history_get(0);
    assert(r0 != NULL);
    assert(strcmp(r0->url, "http://example.com/p0") == 0);
    assert(strcmp(r0->title, "Page 0") == 0);
    assert(r0->visit_count == 1);

    /* Consecutive duplicate URL increments visit count without duplicating record */
    browser_storage_history_add("http://example.com/p0", "Page 0 New", 0);
    assert(browser_storage_history_get_count() == 1);
    assert(browser_storage_history_get(0)->visit_count == 2);

    /* Fill up to FIFO limit */
    for (int i = 1; i < (int)FIRSTOS_MAX_HISTORY_RECORDS; i++) {
        char url[64];
        snprintf(url, sizeof(url), "http://example.com/page%d", i);
        browser_storage_history_add(url, "Page", 0);
    }
    assert(browser_storage_history_get_count() == (int)FIRSTOS_MAX_HISTORY_RECORDS);

    /* Adding 257th item evicts oldest item */
    browser_storage_history_add("http://example.com/overflow", "Overflow", 0);
    assert(browser_storage_history_get_count() == (int)FIRSTOS_MAX_HISTORY_RECORDS);
    assert(strcmp(browser_storage_history_get(0)->url, "http://example.com/page1") == 0);
    assert(strcmp(browser_storage_history_get(FIRSTOS_MAX_HISTORY_RECORDS - 1)->url, "http://example.com/overflow") == 0);
    printf("  -> PASS\n");
}

static void test_atomic_save_and_load_recovery(void) {
    printf("[TEST] test_atomic_save_and_load_recovery...\n");
    reset_mock_fs();
    browser_storage_init();

    browser_storage_history_add("http://site.com/home", "Home", 0);
    browser_storage_history_add("http://site.com/about", "About", 0);

    /* Atomic Save */
    int bytes = browser_storage_save_all();
    assert(bytes > 0);
    assert(fs_read_at(FIRSTOS_STORAGE_PATH, NULL, 0) == bytes);

    /* Reset memory and Load from storage */
    browser_storage_history_clear();
    assert(browser_storage_history_get_count() == 0);

    int loaded = browser_storage_load_all();
    assert(loaded == 2);
    assert(browser_storage_history_get_count() == 2);
    assert(strcmp(browser_storage_history_get(0)->url, "http://site.com/home") == 0);
    assert(strcmp(browser_storage_history_get(1)->url, "http://site.com/about") == 0);

    /* Simulate Corrupted Checksum in file */
    for (int i = 0; i < s_mock_fs_count; i++) {
        if (strcmp(s_mock_fs_files[i], FIRSTOS_STORAGE_PATH) == 0) {
            /* Corrupt payload byte */
            s_mock_fs_data[i][sizeof(firstos_storage_header_t) + 5] ^= 0xFF;
            break;
        }
    }

    browser_storage_history_clear();
    int rec_after_corrupt = browser_storage_load_all();
    /* Corrupted record safely dropped; second uncorrupted record parsed */
    assert(rec_after_corrupt == 1);
    assert(browser_storage_history_get_count() == 1);
    assert(strcmp(browser_storage_history_get(0)->url, "http://site.com/about") == 0);

    printf("  -> PASS\n");
}

int main(void) {
    printf("=== Running Phase 6A.2 Persistent Storage Unit Tests ===\n");
    test_crc32_computation();
    test_history_records_and_eviction();
    test_atomic_save_and_load_recovery();
    printf(">>> All 3 Persistent Storage unit tests PASSED! <<<\n");
    return 0;
}
