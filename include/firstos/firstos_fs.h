#ifndef _FIRSTOS_FS_H
#define _FIRSTOS_FS_H

#include "firstos_app.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline int firstos_fs_open(const char *path, int flags, int mode) {
    return (int)firstos_syscall3(FIRSTOS_SYS_OPEN, (long)path, (long)flags, (long)mode);
}

static inline int firstos_fs_read(int fd, void *buf, unsigned int count) {
    return (int)firstos_syscall3(FIRSTOS_SYS_READ, (long)fd, (long)buf, (long)count);
}

static inline int firstos_fs_write(int fd, const void *buf, unsigned int count) {
    return (int)firstos_syscall3(FIRSTOS_SYS_WRITE, (long)fd, (long)buf, (long)count);
}

static inline int firstos_fs_close(int fd) {
    return (int)firstos_syscall1(FIRSTOS_SYS_CLOSE, (long)fd);
}

#ifdef __cplusplus
}
#endif

#endif /* _FIRSTOS_FS_H */
