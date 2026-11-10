#ifndef VFS_FD_H
#define VFS_FD_H

#include "io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Standard VFS Open Flags */
#define VFS_O_RDONLY 0x0000
#define VFS_O_WRONLY 0x0001
#define VFS_O_RDWR   0x0002
#define VFS_O_CREAT  0x0040
#define VFS_O_TRUNC  0x0200
#define VFS_O_APPEND 0x0400

/* Standard Seek Directives */
#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

/* Maximum concurrently open file descriptors across the kernel */
#define VFS_MAX_OPEN_FDS 64

/* VFS Stat Structure */
typedef struct {
    u32 size;
    int is_dir;
    u32 permissions;
    u32 modified_ticks;
} vfs_stat_t;

/* Initialize the kernel-wide virtual file descriptor table */
void vfs_init(void);

/* Open or create a file path, returning a non-negative file descriptor or -1 on error */
int  vfs_open(const char *path, int flags, int mode);

/* Read up to count bytes from an open file descriptor into buf. Returns bytes read, or -1 on error */
int  vfs_read(int fd, void *buf, u32 count);

/* Write up to count bytes from buf to an open file descriptor. Returns bytes written, or -1 on error */
int  vfs_write(int fd, const void *buf, u32 count);

/* Reposition file offset. Returns new offset from file start, or -1 on error */
int  vfs_seek(int fd, int offset, int whence);

/* Close an open file descriptor. Returns 0 on success, or -1 on error */
int  vfs_close(int fd);

/* Increment reference count of an open file descriptor (e.g. on dup or spawn inheritance) */
int  vfs_add_ref(int fd);

/* Query reference count of an open file descriptor */
int  vfs_get_ref_count(int fd);

/* Query owner PID of an open file descriptor */
int  vfs_get_owner(int fd);

/* Retrieve metadata for a file path */
int  vfs_stat(const char *path, vfs_stat_t *st);

/* Retrieve metadata for an open file descriptor */
int  vfs_fstat(int fd, vfs_stat_t *st);

#ifdef __cplusplus
}
#endif

#endif /* VFS_FD_H */
