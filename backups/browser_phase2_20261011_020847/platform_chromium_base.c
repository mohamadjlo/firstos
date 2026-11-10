#include "include/firstos/platform/chromium_port_base.h"
#include "clock.h"
#include "heap.h"
#include "vfs_fd.h"
#include "framebuffer.h"
#include "compositor.h"
#include "task.h"
#include "process.h"
#include "shm.h"
#include "futex.h"
#include "serial.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t firstos_platform_get_capabilities(void) {
    uint32_t caps = FIRSTOS_CAP_NONE;

    caps |= FIRSTOS_CAP_MONOTONIC_CLOCK;
    caps |= FIRSTOS_CAP_HEAP_DIAGNOSTICS;
    caps |= FIRSTOS_CAP_VFS_FILE_HANDLES;
    caps |= FIRSTOS_CAP_SOFTWARE_SURFACE;
    caps |= FIRSTOS_CAP_VIRTIO_GPU;
    caps |= FIRSTOS_CAP_SOCKETS_TCP_UDP;
    caps |= FIRSTOS_CAP_DNS_RESOLVER;
    caps |= FIRSTOS_CAP_VFS_DIRECTORIES;
    caps |= FIRSTOS_CAP_USERS_AUTHENTICATED;

    return caps;
}

int firstos_platform_has_capability(firstos_platform_cap_t cap) {
    return (firstos_platform_get_capabilities() & (uint32_t)cap) == (uint32_t)cap;
}

firstos_monotonic_time_t firstos_monotonic_now(void) {
    firstos_monotonic_time_t t;
    t.nanoseconds = clock_monotonic_get_ns();
    return t;
}

uint64_t firstos_monotonic_nanos(void) {
    return clock_monotonic_get_ns();
}

uint64_t firstos_monotonic_micros(void) {
    return clock_monotonic_get_us();
}

uint32_t firstos_monotonic_millis(void) {
    return clock_monotonic_get_ms();
}

void firstos_get_heap_telemetry(firstos_heap_telemetry_t *out_stats) {
    if (!out_stats) return;
    heap_stats_t s;
    heap_get_stats(&s);
    out_stats->total_bytes = s.total_bytes;
    out_stats->used_bytes = s.used_bytes;
    out_stats->free_bytes = s.free_bytes;
    out_stats->peak_used_bytes = s.peak_used_bytes;
    out_stats->alloc_count = s.alloc_count;
    out_stats->free_count = s.free_count;
    out_stats->oom_failure_count = s.oom_count;
}

firstos_fd_t firstos_file_open(const char *path, int flags, int mode) {
    return (firstos_fd_t)vfs_open(path, flags, mode);
}

int firstos_file_read(firstos_fd_t fd, void *buf, size_t count) {
    return vfs_read((int)fd, buf, (u32)count);
}

int firstos_file_write(firstos_fd_t fd, const void *buf, size_t count) {
    return vfs_write((int)fd, buf, (u32)count);
}

int64_t firstos_file_seek(firstos_fd_t fd, int64_t offset, int whence) {
    return (int64_t)vfs_seek((int)fd, (int)offset, whence);
}

int firstos_file_close(firstos_fd_t fd) {
    return vfs_close((int)fd);
}

int firstos_file_stat(const char *path, firstos_stat_t *out_stat) {
    if (!out_stat) return -1;
    vfs_stat_t vs;
    int rc = vfs_stat(path, &vs);
    if (rc == 0) {
        out_stat->size = vs.size;
        out_stat->is_dir = vs.is_dir;
        out_stat->permissions = vs.permissions;
        out_stat->modified_timestamp = vs.modified_ticks;
        return 0;
    }
    return -1;
}

int firstos_file_fstat(firstos_fd_t fd, firstos_stat_t *out_stat) {
    if (!out_stat) return -1;
    vfs_stat_t vs;
    int rc = vfs_fstat((int)fd, &vs);
    if (rc == 0) {
        out_stat->size = vs.size;
        out_stat->is_dir = vs.is_dir;
        out_stat->permissions = vs.permissions;
        out_stat->modified_timestamp = vs.modified_ticks;
        return 0;
    }
    return -1;
}

int firstos_surface_create(uint32_t width, uint32_t height, firstos_surface_t *out_surface) {
    if (!out_surface || width == 0 || height == 0) return FIRSTOS_PLATFORM_ERR_INVALID_PARAM;
    memset(out_surface, 0, sizeof(*out_surface));

    uint32_t bytes = width * height * 4;
    uint32_t *pix = (uint32_t *)kmalloc(bytes);
    if (!pix) return FIRSTOS_PLATFORM_ERR_NO_MEMORY;

    memset(pix, 0xFF, bytes); /* Default to opaque white canvas */
    out_surface->width = width;
    out_surface->height = height;
    out_surface->pitch = width * 4;
    out_surface->bpp = 32;
    out_surface->pixels = pix;
    out_surface->internal_handle = pix;
    return FIRSTOS_PLATFORM_OK;
}

void firstos_surface_present(firstos_surface_t *surface, int x, int y, int w, int h) {
    if (!surface || !surface->pixels) return;
    int draw_w = (w > (int)surface->width) ? (int)surface->width : w;
    int draw_h = (h > (int)surface->height) ? (int)surface->height : h;

    for (int r = 0; r < draw_h; r++) {
        for (int c = 0; c < draw_w; c++) {
            uint32_t color = surface->pixels[r * surface->width + c];
            fb_putpixel(x + c, y + r, color);
        }
    }
}

void firstos_surface_destroy(firstos_surface_t *surface) {
    if (!surface) return;
    if (surface->pixels) {
        kfree(surface->pixels);
        surface->pixels = NULL;
    }
    memset(surface, 0, sizeof(*surface));
}

void firstos_get_task_telemetry(firstos_task_telemetry_t *out_task_info) {
    if (!out_task_info) return;
    out_task_info->max_tasks = MAX_TASKS;
    out_task_info->active_tasks = 1;
    extern task_t tasks[MAX_TASKS];
    int active = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].active) active++;
    }
    out_task_info->active_tasks = active;
    out_task_info->is_ring3_isolated = 1; /* Ring 3 unprivileged execution supported */
    out_task_info->is_preemptive = 1;     /* Timer quantum preemptive scheduler */
}

void firstos_platform_init(void) {
    uint32_t caps = firstos_platform_get_capabilities();
    caps |= FIRSTOS_CAP_RING3_USERSPACE;
    caps |= FIRSTOS_CAP_SHARED_MEMORY;
    caps |= FIRSTOS_CAP_IPC_PIPES;
    serial_write("[platform] chromium base init caps=");
    serial_write_hex(caps);
    serial_write("\n");
}

int firstos_service_spawn(const char *name, const char *path, const char *const *argv) {
    if (!path) return -1;
    serial_write("[platform] spawning service '");
    serial_write(name ? name : "unknown");
    serial_write("' path=");
    serial_write(path);
    serial_write("\n");
    return process_spawn(0, path, argv);
}

int firstos_ipc_channel_create(int out_fds[2]) {
    if (!out_fds) return -1;
    int rc = proc_pipe_create(0, out_fds);
    if (rc == 0) {
        serial_write("[platform] ipc channel created rfd=");
        serial_write_hex((u32)out_fds[0]);
        serial_write(" wfd=");
        serial_write_hex((u32)out_fds[1]);
        serial_write("\n");
    }
    return rc;
}

int firstos_shm_create_region(const char *name, void **out_vaddr) {
    if (!name || !out_vaddr) return -1;
    int shmid = shm_open(name, 0, 0);
    if (shmid < 0) return shmid;
    int rc = shm_map(0, shmid, out_vaddr);
    if (rc == 0) {
        serial_write("[platform] shm region mapped for platform: ");
        serial_write(name);
        serial_write("\n");
    }
    return rc;
}

int firstos_sync_futex_wait(int *uaddr, int val) {
    return futex_wait(0, uaddr, val);
}

int firstos_sync_futex_wake(int *uaddr, int count) {
    return futex_wake(uaddr, count);
}

#ifdef __cplusplus
}
#endif
