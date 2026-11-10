#ifndef _FIRSTOS_USER_SYSCALL_H
#define _FIRSTOS_USER_SYSCALL_H

#include "../../../include/firstos/uapi/syscall.h"

/* Low-level syscall invocation primitives */
long firstos_syscall0(long number);
long firstos_syscall1(long number, long arg1);
long firstos_syscall2(long number, long arg1, long arg2);
long firstos_syscall3(long number, long arg1, long arg2, long arg3);
long firstos_syscall4(long number, long arg1, long arg2, long arg3, long arg4);

/* Higher-level POSIX-like userspace wrappers */
int   firstos_write(int fd, const void *buffer, unsigned int length);
int   firstos_read(int fd, void *buffer, unsigned int length);
int   firstos_close(int fd);
void  firstos_exit(int status) __attribute__((noreturn));
void  firstos_yield(void);
int   firstos_getpid(void);
int   firstos_dup(int oldfd);
int   firstos_pipe(int pipefd[2]);
int   firstos_dup2(int oldfd, int newfd);
int   firstos_spawn(const char *path, const char *const *argv);
int   firstos_exec(const char *path, const char *const *argv);
int   firstos_waitpid(int pid, int *status, int options);
int   firstos_brk(void *addr);
void *firstos_sbrk(int increment);

/* Userspace dynamic memory allocation */
void *firstos_malloc(unsigned int size);
void  firstos_free(void *ptr);

/* Shared Memory & Synchronization */
int   firstos_shm_open(const char *name, int flags, int mode);
void *firstos_shm_map(int shmid);
int   firstos_shm_unmap(int shmid);
int   firstos_futex(int *uaddr, int op, int val);
int   firstos_mutex_lock(int *mutex);
void  firstos_mutex_unlock(int *mutex);

#endif /* _FIRSTOS_USER_SYSCALL_H */
