#include "vfs_fd.h"
#include "fs.h"
#include "heap.h"
#include "serial.h"
#include "clock.h"
#include "ktrace.h"
#include <string.h>

extern int current_task;

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int active;
    int flags;
    int mode;
    int ref_count;
    int owner_pid;
    char path[256];
    u32 offset;
    u32 size;
    char *buffer;
    u32 buffer_cap;
    int dirty;
} vfs_fd_entry_t;

static vfs_fd_entry_t s_vfs_fds[VFS_MAX_OPEN_FDS];
static int s_vfs_initialized = 0;

void vfs_init(void) {
    memset(s_vfs_fds, 0, sizeof(s_vfs_fds));
    s_vfs_initialized = 1;
    serial_write("[vfs] Virtual file descriptor subsystem initialized (64 fds)\n");
}

static int vfs_find_free_slot(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FDS; i++) {
        if (!s_vfs_fds[i].active) return i;
    }
    return -1;
}

int vfs_open(const char *path, int flags, int mode) {
    if (!path || !path[0]) return -1;
    if (!s_vfs_initialized) vfs_init();

    int fd = vfs_find_free_slot();
    if (fd < 0) return -1;

    vfs_fd_entry_t *e = &s_vfs_fds[fd];
    memset(e, 0, sizeof(*e));
    strncpy(e->path, path, sizeof(e->path) - 1);
    e->flags = flags;
    e->mode = mode;
    e->ref_count = 1;
    e->owner_pid = current_task;

    /* Check if file exists */
    char probe[4];
    int existing_size = fs_read_at(path, probe, 0);

    if (existing_size < 0) {
        /* File does not exist */
        if (!(flags & VFS_O_CREAT)) {
            return -1;
        }
        /* Create new file */
        if (!fs_create_at(path, 0)) {
            return -1;
        }
        e->size = 0;
        e->buffer_cap = 1024;
        e->buffer = (char *)kmalloc(e->buffer_cap);
        if (!e->buffer) return -1;
        e->buffer[0] = 0;
        e->dirty = 1;
    } else {
        /* File exists */
        if (flags & VFS_O_TRUNC) {
            e->size = 0;
            e->buffer_cap = 1024;
            e->buffer = (char *)kmalloc(e->buffer_cap);
            if (!e->buffer) return -1;
            e->buffer[0] = 0;
            e->dirty = 1;
        } else {
            e->size = (u32)existing_size;
            e->buffer_cap = e->size + 1024;
            e->buffer = (char *)kmalloc(e->buffer_cap);
            if (!e->buffer) return -1;
            if (e->size > 0) {
                fs_read_at(path, e->buffer, e->size);
            }
        }
    }

    if (flags & VFS_O_APPEND) {
        e->offset = e->size;
    } else {
        e->offset = 0;
    }

    e->active = 1;
    return fd;
}

int vfs_read(int fd, void *buf, u32 count) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FDS || !buf || count == 0) return -1;
    vfs_fd_entry_t *e = &s_vfs_fds[fd];
    if (!e->active) return -1;
    if ((e->flags & 3) == VFS_O_WRONLY) return -1;

    if (e->offset >= e->size) return 0; /* EOF */

    u32 avail = e->size - e->offset;
    u32 to_read = (count < avail) ? count : avail;

    if (to_read > 0 && e->buffer) {
        memcpy(buf, e->buffer + e->offset, to_read);
        e->offset += to_read;
    }
    return (int)to_read;
}

int vfs_write(int fd, const void *buf, u32 count) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FDS || !buf || count == 0) return -1;
    vfs_fd_entry_t *e = &s_vfs_fds[fd];
    if (!e->active) return -1;
    if ((e->flags & 3) == VFS_O_RDONLY) return -1;

    if (e->flags & VFS_O_APPEND) {
        e->offset = e->size;
    }

    u32 needed_cap = e->offset + count + 1;
    if (needed_cap > e->buffer_cap) {
        u32 new_cap = needed_cap + 2048;
        char *new_buf = (char *)krealloc(e->buffer, new_cap);
        if (!new_buf) return -1;
        e->buffer = new_buf;
        e->buffer_cap = new_cap;
    }

    memcpy(e->buffer + e->offset, buf, count);
    e->offset += count;
    if (e->offset > e->size) {
        e->size = e->offset;
    }
    e->dirty = 1;
    return (int)count;
}

int vfs_seek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FDS) return -1;
    vfs_fd_entry_t *e = &s_vfs_fds[fd];
    if (!e->active) return -1;

    int new_off = 0;
    if (whence == VFS_SEEK_SET) {
        new_off = offset;
    } else if (whence == VFS_SEEK_CUR) {
        new_off = (int)e->offset + offset;
    } else if (whence == VFS_SEEK_END) {
        new_off = (int)e->size + offset;
    } else {
        return -1;
    }

    if (new_off < 0) return -1;
    e->offset = (u32)new_off;
    return new_off;
}

int vfs_add_ref(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FDS) return -1;
    vfs_fd_entry_t *e = &s_vfs_fds[fd];
    if (!e->active) return -1;
    e->ref_count++;
    ktrace_record(KTRACE_SUBSYS_VFS, KTRACE_LEVEL_DEBUG, (u16)current_task, "vfs_add_ref");
    return e->ref_count;
}

int vfs_get_ref_count(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FDS) return -1;
    vfs_fd_entry_t *e = &s_vfs_fds[fd];
    if (!e->active) return 0;
    return e->ref_count;
}

int vfs_get_owner(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FDS) return -1;
    vfs_fd_entry_t *e = &s_vfs_fds[fd];
    if (!e->active) return -1;
    return e->owner_pid;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FDS) return -1;
    vfs_fd_entry_t *e = &s_vfs_fds[fd];
    if (!e->active) return -1;

    if (e->ref_count > 1) {
        e->ref_count--;
        ktrace_record(KTRACE_SUBSYS_VFS, KTRACE_LEVEL_DEBUG, (u16)current_task, "vfs_close: ref decremented");
        return 0;
    }
    e->ref_count = 0;

    if (e->dirty && e->buffer) {
        fs_write_at(e->path, e->buffer, e->size);
    }

    if (e->buffer) {
        kfree(e->buffer);
        e->buffer = NULL;
    }
    memset(e, 0, sizeof(*e));
    return 0;
}

int vfs_stat(const char *path, vfs_stat_t *st) {
    if (!path || !st) return -1;
    memset(st, 0, sizeof(*st));

    int count = fs_count();
    for (int i = 0; i < count; i++) {
        const fs_entry_t *fe = fs_entry_at(i);
        if (fe && fe->used && strcmp(fe->path, path) == 0) {
            st->size = fe->size;
            st->is_dir = fe->is_dir;
            st->permissions = fe->is_dir ? 0755 : 0644;
            st->modified_ticks = clock_monotonic_get_ticks();
            return 0;
        }
    }
    return -1;
}

int vfs_fstat(int fd, vfs_stat_t *st) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FDS || !st) return -1;
    vfs_fd_entry_t *e = &s_vfs_fds[fd];
    if (!e->active) return -1;

    memset(st, 0, sizeof(*st));
    st->size = e->size;
    st->is_dir = 0;
    st->permissions = 0644;
    st->modified_ticks = clock_monotonic_get_ticks();
    return 0;
}

#ifdef __cplusplus
}
#endif
