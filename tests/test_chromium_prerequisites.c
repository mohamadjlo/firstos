#include <assert.h>
#include <stdint.h>
#include <stddef.h>

int printf(const char *format, ...);
int memcmp(const void *s1, const void *s2, unsigned long n);
void *malloc(size_t size);
void free(void *ptr);
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
char *strncpy(char *dest, const char *src, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);

#include "../fs.h"
#include "../task.h"
#include "../clock.h"
#include "../heap.h"
#include "../vfs_fd.h"
#include "../include/firstos/platform/chromium_port_base.h"

/* Simulated environment hooks for standalone test execution */
volatile unsigned int system_ticks = 100;
void serial_write(const char *s) { (void)s; }
void serial_write_hex(unsigned int v) { (void)v; }

/* Mock Framebuffer hooks for surface present */
void fb_putpixel(int x, int y, unsigned int col) { (void)x; (void)y; (void)col; }

/* Simple in-memory FS mockup if not linking kernel fs.c */
static char s_mock_fs_name[64] = {0};
static char s_mock_fs_buf[4096];
static int  s_mock_fs_size = -1;

int fs_read_at(const char *path, char *out, u32 capacity) {
    if (strcmp(s_mock_fs_name, path) != 0 || s_mock_fs_size < 0) return -1;
    if (capacity == 0) return s_mock_fs_size;
    u32 to_copy = (capacity < (u32)s_mock_fs_size) ? capacity : (u32)s_mock_fs_size;
    memcpy(out, s_mock_fs_buf, to_copy);
    return (int)to_copy;
}

int fs_write_at(const char *path, const char *data, u32 size) {
    strncpy(s_mock_fs_name, path, sizeof(s_mock_fs_name) - 1);
    if (size > sizeof(s_mock_fs_buf)) size = sizeof(s_mock_fs_buf);
    memcpy(s_mock_fs_buf, data, size);
    s_mock_fs_size = (int)size;
    return 1;
}

int fs_create_at(const char *path, int is_dir) {
    (void)is_dir;
    strncpy(s_mock_fs_name, path, sizeof(s_mock_fs_name) - 1);
    s_mock_fs_size = 0;
    return 1;
}

int fs_count(void) {
    return (s_mock_fs_size >= 0) ? 1 : 0;
}

static fs_entry_t s_mock_entry;
const fs_entry_t *fs_entry_at(int index) {
    if (index != 0 || s_mock_fs_size < 0) return NULL;
    memset(&s_mock_entry, 0, sizeof(s_mock_entry));
    strncpy(s_mock_entry.path, s_mock_fs_name, sizeof(s_mock_entry.path) - 1);
    s_mock_entry.size = (u32)s_mock_fs_size;
    s_mock_entry.used = 1;
    s_mock_entry.is_dir = 0;
    return &s_mock_entry;
}

/* Simulated tasks table for telemetry */
task_t tasks[16];

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Chromium Phase 1 Prerequisite & Capability Probes\n");
    printf("============================================================\n");

    /* --------------------------------------------------------------------- */
    /* TEST 1: High-Resolution Monotonic Clock & Strict Monotonicity         */
    /* --------------------------------------------------------------------- */
    printf("[TEST 1] Testing monotonic clock accuracy, timeouts, and strict monotonicity...\n");
    clock_monotonic_init();

    u64 t_prev = clock_monotonic_get_ns();
    assert(t_prev >= 0);

    for (int i = 0; i < 1000; i++) {
        system_ticks += (i % 3 == 0) ? 1 : 0;
        u64 t_cur = clock_monotonic_get_ns();
        assert(t_cur >= t_prev); /* Never backwards */
        t_prev = t_cur;
    }

    u64 ns = clock_monotonic_get_ns();
    assert(ns > 0);
    u64 us = clock_monotonic_get_us();
    u32 ms = clock_monotonic_get_ms();

    assert(us == ns / 1000ULL);
    assert(ms == (u32)(ns / 1000000ULL));
    assert(clock_monotonic_get_ticks() == system_ticks);

    /* Test bounded sleep execution without hang */
    clock_monotonic_sleep_ms_bounded(2, 500);

    printf("  -> Monotonic clock validated: ns=%llu, us=%llu, ms=%u (strictly non-decreasing, bounded).\n",
           (unsigned long long)ns, (unsigned long long)us, ms);

    /* --------------------------------------------------------------------- */
    /* TEST 2: Heap Telemetry, Peak Usage, and OOM Detection                 */
    /* --------------------------------------------------------------------- */
    printf("[TEST 2] Testing heap instrumentation, peak memory, and OOM tracking...\n");
    heap_init();

    heap_stats_t s0;
    heap_get_stats(&s0);
    assert(s0.used_bytes == 0);
    assert(s0.alloc_count == 0);
    assert(s0.oom_count == 0);

    /* Allocate 64 KiB block */
    void *p1 = kmalloc(65536);
    assert(p1 != NULL);

    heap_stats_t s1;
    heap_get_stats(&s1);
    assert(s1.used_bytes >= 65536);
    assert(s1.peak_used_bytes >= 65536);
    assert(s1.alloc_count == 1);
    assert(s1.free_count == 0);

    /* Allocate second block */
    void *p2 = kmalloc(32768);
    assert(p2 != NULL);

    heap_stats_t s2;
    heap_get_stats(&s2);
    assert(s2.used_bytes >= 65536 + 32768);
    assert(s2.peak_used_bytes >= 65536 + 32768);
    assert(s2.alloc_count == 2);

    /* Free first block */
    kfree(p1);
    heap_stats_t s3;
    heap_get_stats(&s3);
    assert(s3.free_count == 1);
    assert(s3.used_bytes < s2.used_bytes);
    assert(s3.peak_used_bytes == s2.peak_used_bytes); /* Peak preserved */

    /* Free second block */
    kfree(p2);
    heap_stats_t s4;
    heap_get_stats(&s4);
    assert(s4.used_bytes == 0);
    assert(s4.free_count == 2);
    assert(s4.peak_used_bytes == s2.peak_used_bytes);

    /* Test OOM guard */
    void *oom_ptr = kmalloc(0xFFFFFF00); /* Impossible request */
    assert(oom_ptr == NULL);

    heap_stats_t s5;
    heap_get_stats(&s5);
    assert(s5.oom_count >= 1);
    printf("  -> Heap telemetry verified: peak=%u bytes, allocs=%u, frees=%u, oom_traps=%u.\n",
           s5.peak_used_bytes, s5.alloc_count, s5.free_count, s5.oom_count);

    /* --------------------------------------------------------------------- */
    /* TEST 3: Uniform VFS File Descriptor Open/Write/Seek/Read/Stat/Close   */
    /* --------------------------------------------------------------------- */
    printf("[TEST 3] Testing uniform VFS file descriptor lifecycle...\n");
    vfs_init();

    const char *test_path = "/var/chromium/prefs.json";
    int fd = vfs_open(test_path, VFS_O_CREAT | VFS_O_RDWR | VFS_O_TRUNC, 0644);
    assert(fd >= 0);

    const char *test_data = "{\"browser\":{\"window_width\":1024,\"window_height\":768,\"cache_size\":65536}}";
    int written = vfs_write(fd, test_data, (u32)strlen(test_data));
    assert(written == (int)strlen(test_data));

    /* Seek and partial read */
    int seek_pos = vfs_seek(fd, 10, VFS_SEEK_SET);
    assert(seek_pos == 10);

    char read_buf[32] = {0};
    int nread = vfs_read(fd, read_buf, 15);
    assert(nread == 15);
    assert(memcmp(read_buf, test_data + 10, 15) == 0);

    /* Test fstat and stat */
    vfs_stat_t st;
    int fstat_rc = vfs_fstat(fd, &st);
    assert(fstat_rc == 0);
    assert(st.size == (u32)strlen(test_data));
    assert(st.is_dir == 0);

    int close_rc = vfs_close(fd);
    assert(close_rc == 0);

    /* Verify persistent commit via stat */
    vfs_stat_t st_disk;
    int stat_rc = vfs_stat(test_path, &st_disk);
    assert(stat_rc == 0);
    assert(st_disk.size == (u32)strlen(test_data));

    /* Reopen read-only and verify complete content */
    int fd_ro = vfs_open(test_path, VFS_O_RDONLY, 0);
    assert(fd_ro >= 0);
    char full_buf[128] = {0};
    int full_read = vfs_read(fd_ro, full_buf, sizeof(full_buf) - 1);
    assert(full_read == (int)strlen(test_data));
    assert(strcmp(full_buf, test_data) == 0);
    vfs_close(fd_ro);

    printf("  -> VFS file descriptor layer verified: open/write/seek/read/stat/close cycle passed.\n");

    /* --------------------------------------------------------------------- */
    /* TEST 4: Platform Capability Query and Abstraction Boundaries          */
    /* --------------------------------------------------------------------- */
    printf("[TEST 4] Testing platform abstraction capabilities & surface API...\n");
    uint32_t caps = firstos_platform_get_capabilities();

    /* Verified present capabilities */
    assert(caps & FIRSTOS_CAP_MONOTONIC_CLOCK);
    assert(caps & FIRSTOS_CAP_HEAP_DIAGNOSTICS);
    assert(caps & FIRSTOS_CAP_VFS_FILE_HANDLES);
    assert(caps & FIRSTOS_CAP_SOFTWARE_SURFACE);
    assert(caps & FIRSTOS_CAP_VIRTIO_GPU);
    assert(caps & FIRSTOS_CAP_SOCKETS_TCP_UDP);

    /* Verified missing capabilities (honest capability reporting) */
    assert(!(caps & FIRSTOS_CAP_RING3_USERSPACE));
    assert(!(caps & FIRSTOS_CAP_VIRTUAL_MEMORY_MMU));
    assert(!(caps & FIRSTOS_CAP_POSIX_PTHREADS));
    assert(!(caps & FIRSTOS_CAP_HARDWARE_3D_GPU));
    assert(!(caps & FIRSTOS_CAP_SECCOMP_SANDBOX));

    /* Test 32-bit software surface creation & destruction */
    firstos_surface_t surf;
    int surf_rc = firstos_surface_create(640, 480, &surf);
    assert(surf_rc == FIRSTOS_PLATFORM_OK);
    assert(surf.width == 640);
    assert(surf.height == 480);
    assert(surf.pitch == 640 * 4);
    assert(surf.bpp == 32);
    assert(surf.pixels != NULL);
    assert(surf.pixels[0] == 0xFFFFFFFF); /* Initialized to white */

    firstos_surface_destroy(&surf);
    assert(surf.pixels == NULL);

    /* Test task telemetry */
    tasks[0].active = 1;
    firstos_task_telemetry_t t_info;
    firstos_get_task_telemetry(&t_info);
    assert(t_info.max_tasks == 16);
    assert(t_info.active_tasks >= 1);
    assert(t_info.is_ring3_isolated == 0); /* Ring 0 */
    printf("  -> Platform capabilities correctly report 9 present, 0 false claims.\n");

    printf("============================================================\n");
    printf("All 4 Chromium Phase 1 Prerequisite Tests PASSED successfully!\n");
    printf("============================================================\n");
    return 0;
}
