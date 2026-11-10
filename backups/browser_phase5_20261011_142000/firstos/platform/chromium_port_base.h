#ifndef FIRSTOS_PLATFORM_CHROMIUM_PORT_BASE_H
#define FIRSTOS_PLATFORM_CHROMIUM_PORT_BASE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* FirstOS Chromium Porting Layer — Platform Capabilities & Abstraction      */
/* ========================================================================= */

/* Capability flags queried at runtime by the Chromium porting layer */
typedef enum {
    FIRSTOS_CAP_NONE               = 0,
    FIRSTOS_CAP_MONOTONIC_CLOCK    = (1 << 0),  /* High-resolution monotonic timing */
    FIRSTOS_CAP_HEAP_DIAGNOSTICS   = (1 << 1),  /* Memory tracking, telemetry, OOM hooks */
    FIRSTOS_CAP_VFS_FILE_HANDLES   = (1 << 2),  /* File descriptor abstraction with seek */
    FIRSTOS_CAP_SOFTWARE_SURFACE   = (1 << 3),  /* 32-bit linear RGBA/BGRA window surface */
    FIRSTOS_CAP_VIRTIO_GPU         = (1 << 4),  /* Virtio-GPU hardware display path */
    FIRSTOS_CAP_SOCKETS_TCP_UDP    = (1 << 5),  /* In-kernel TCP/UDP network sockets */
    FIRSTOS_CAP_DNS_RESOLVER       = (1 << 6),  /* Domain name resolution */
    FIRSTOS_CAP_VFS_DIRECTORIES    = (1 << 7),  /* Hierarchical directory traversal */
    FIRSTOS_CAP_USERS_AUTHENTICATED= (1 << 8),  /* User profile management */
    
    /* Planned Phase 2 / Phase 3 capabilities */
    FIRSTOS_CAP_RING3_USERSPACE    = (1 << 16), /* Ring 3 unprivileged execution */
    FIRSTOS_CAP_VIRTUAL_MEMORY_MMU = (1 << 17), /* Per-process paging & mmap/mprotect */
    FIRSTOS_CAP_POSIX_PTHREADS     = (1 << 18), /* Preemptive multithreading */
    FIRSTOS_CAP_SHARED_MEMORY      = (1 << 19), /* Cross-process shared memory */
    FIRSTOS_CAP_IPC_PIPES          = (1 << 20), /* Inter-process communication pipes */
    FIRSTOS_CAP_IO_POLLING         = (1 << 21), /* epoll / poll / select */
    FIRSTOS_CAP_HARDWARE_3D_GPU    = (1 << 22), /* Accelerated OpenGL ES / Vulkan */
    FIRSTOS_CAP_SECCOMP_SANDBOX    = (1 << 23)  /* Process sandboxing */
} firstos_platform_cap_t;

/* Status codes */
typedef enum {
    FIRSTOS_PLATFORM_OK = 0,
    FIRSTOS_PLATFORM_ERR_NOT_SUPPORTED = -1,
    FIRSTOS_PLATFORM_ERR_INVALID_PARAM = -2,
    FIRSTOS_PLATFORM_ERR_NO_MEMORY     = -3,
    FIRSTOS_PLATFORM_ERR_IO            = -4,
    FIRSTOS_PLATFORM_ERR_TIMEOUT       = -5
} firstos_platform_err_t;

/* Query active platform capability bitmask */
uint32_t firstos_platform_get_capabilities(void);

/* Check if a specific capability is present */
int firstos_platform_has_capability(firstos_platform_cap_t cap);

/* ------------------------------------------------------------------------- */
/* Time & Monotonic Clock Abstraction                                        */
/* ------------------------------------------------------------------------- */
typedef struct {
    uint64_t nanoseconds;
} firstos_monotonic_time_t;

firstos_monotonic_time_t firstos_monotonic_now(void);
uint64_t firstos_monotonic_nanos(void);
uint64_t firstos_monotonic_micros(void);
uint32_t firstos_monotonic_millis(void);

/* ------------------------------------------------------------------------- */
/* Memory & Heap Diagnostics                                                 */
/* ------------------------------------------------------------------------- */
typedef struct {
    uint32_t total_bytes;
    uint32_t used_bytes;
    uint32_t free_bytes;
    uint32_t peak_used_bytes;
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t oom_failure_count;
} firstos_heap_telemetry_t;

void firstos_get_heap_telemetry(firstos_heap_telemetry_t *out_stats);

/* ------------------------------------------------------------------------- */
/* Uniform File Descriptor Abstraction                                       */
/* ------------------------------------------------------------------------- */
typedef int firstos_fd_t;

#define FIRSTOS_FD_INVALID (-1)

typedef struct {
    uint32_t size;
    int      is_dir;
    uint32_t permissions;
    uint32_t modified_timestamp;
} firstos_stat_t;

firstos_fd_t firstos_file_open(const char *path, int flags, int mode);
int          firstos_file_read(firstos_fd_t fd, void *buf, size_t count);
int          firstos_file_write(firstos_fd_t fd, const void *buf, size_t count);
int64_t      firstos_file_seek(firstos_fd_t fd, int64_t offset, int whence);
int          firstos_file_close(firstos_fd_t fd);
int          firstos_file_stat(const char *path, firstos_stat_t *out_stat);
int          firstos_file_fstat(firstos_fd_t fd, firstos_stat_t *out_stat);

/* ------------------------------------------------------------------------- */
/* Graphics Surface Abstraction (Compositor / Software Rasterizer)           */
/* ------------------------------------------------------------------------- */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;     /* Bytes per scanline */
    uint32_t bpp;       /* Bits per pixel (typically 32) */
    uint32_t *pixels;   /* Linear RGBA/BGRA framebuffer */
    void     *internal_handle;
} firstos_surface_t;

int  firstos_surface_create(uint32_t width, uint32_t height, firstos_surface_t *out_surface);
void firstos_surface_present(firstos_surface_t *surface, int x, int y, int w, int h);
void firstos_surface_destroy(firstos_surface_t *surface);

/* ------------------------------------------------------------------------- */
/* Process & Threading Future Boundary                                       */
/* ------------------------------------------------------------------------- */
typedef struct {
    int max_tasks;
    int active_tasks;
    int is_ring3_isolated;
    int is_preemptive;
} firstos_task_telemetry_t;

void firstos_get_task_telemetry(firstos_task_telemetry_t *out_task_info);

/* ------------------------------------------------------------------------- */
/* Chromium Architecture Services & IPC Foundations (Phase 1)                */
/* ------------------------------------------------------------------------- */
void firstos_platform_init(void);
int  firstos_service_spawn(const char *name, const char *path, const char *const *argv);
int  firstos_ipc_channel_create(int out_fds[2]);
int  firstos_shm_create_region(const char *name, void **out_vaddr);
int  firstos_sync_futex_wait(int *uaddr, int val);
int  firstos_sync_futex_wake(int *uaddr, int count);

#ifdef __cplusplus
}
#endif

#endif /* FIRSTOS_PLATFORM_CHROMIUM_PORT_BASE_H */
