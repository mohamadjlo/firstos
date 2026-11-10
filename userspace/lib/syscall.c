#include "../include/firstos/syscall.h"

long firstos_syscall0(long number) {
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (number)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long firstos_syscall1(long number, long arg1) {
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (number), "D" (arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long firstos_syscall2(long number, long arg1, long arg2) {
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (number), "D" (arg1), "S" (arg2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long firstos_syscall3(long number, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (number), "D" (arg1), "S" (arg2), "d" (arg3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long firstos_syscall4(long number, long arg1, long arg2, long arg3, long arg4) {
    long ret;
    register long r10 __asm__("r10") = arg4;
    __asm__ volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (number), "D" (arg1), "S" (arg2), "d" (arg3), "r" (r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

int firstos_write(int fd, const void *buffer, unsigned int length) {
    return (int)firstos_syscall3(FIRSTOS_SYS_WRITE, (long)fd, (long)buffer, (long)length);
}

int firstos_read(int fd, void *buffer, unsigned int length) {
    return (int)firstos_syscall3(FIRSTOS_SYS_READ, (long)fd, (long)buffer, (long)length);
}

int firstos_close(int fd) {
    return (int)firstos_syscall1(FIRSTOS_SYS_CLOSE, (long)fd);
}

void firstos_yield(void) {
    firstos_syscall0(FIRSTOS_SYS_YIELD);
}

void firstos_exit(int status) {
    firstos_syscall1(FIRSTOS_SYS_EXIT, (long)status);
    while (1) {
        firstos_yield();
    }
}

int firstos_getpid(void) {
    return (int)firstos_syscall0(FIRSTOS_SYS_GETPID);
}

int firstos_dup(int oldfd) {
    return (int)firstos_syscall1(FIRSTOS_SYS_DUP, (long)oldfd);
}

int firstos_pipe(int pipefd[2]) {
    return (int)firstos_syscall1(FIRSTOS_SYS_PIPE, (long)pipefd);
}

int firstos_dup2(int oldfd, int newfd) {
    return (int)firstos_syscall2(FIRSTOS_SYS_DUP2, (long)oldfd, (long)newfd);
}

int firstos_spawn(const char *path, const char *const *argv) {
    return (int)firstos_syscall2(FIRSTOS_SYS_SPAWN, (long)path, (long)argv);
}

int firstos_exec(const char *path, const char *const *argv) {
    return (int)firstos_syscall2(FIRSTOS_SYS_EXEC, (long)path, (long)argv);
}

int firstos_waitpid(int pid, int *status, int options) {
    return (int)firstos_syscall3(FIRSTOS_SYS_WAITPID, (long)pid, (long)status, (long)options);
}

int firstos_brk(void *addr) {
    long ret = firstos_syscall1(FIRSTOS_SYS_BRK, (long)addr);
    if ((void *)ret == addr) return 0;
    return -1;
}

void *firstos_sbrk(int increment) {
    long cur_brk = firstos_syscall1(FIRSTOS_SYS_BRK, 0);
    if (increment == 0) return (void *)cur_brk;
    long new_brk = cur_brk + increment;
    long res = firstos_syscall1(FIRSTOS_SYS_BRK, new_brk);
    if (res == new_brk) return (void *)cur_brk;
    return (void *)-1;
}

int firstos_shm_open(const char *name, int flags, int mode) {
    return (int)firstos_syscall3(FIRSTOS_SYS_SHM_OPEN, (long)name, (long)flags, (long)mode);
}

void *firstos_shm_map(int shmid) {
    void *v = 0;
    int rc = (int)firstos_syscall2(FIRSTOS_SYS_SHM_MAP, (long)shmid, (long)&v);
    if (rc == 0) return v;
    return (void *)0;
}

int firstos_shm_unmap(int shmid) {
    return (int)firstos_syscall1(FIRSTOS_SYS_SHM_UNMAP, (long)shmid);
}

int firstos_futex(int *uaddr, int op, int val) {
    return (int)firstos_syscall3(FIRSTOS_SYS_FUTEX, (long)uaddr, (long)op, (long)val);
}

int firstos_mutex_lock(int *mutex) {
    if (!mutex) return -1;
    /* Try fast-path uncontended atomic test-and-set */
    if (__sync_bool_compare_and_swap(mutex, 0, 1)) {
        return 0;
    }
    /* Contended: spin and wait on futex */
    while (__sync_lock_test_and_set(mutex, 2) != 0) {
        firstos_futex(mutex, FIRSTOS_FUTEX_WAIT, 2);
    }
    return 0;
}

void firstos_mutex_unlock(int *mutex) {
    if (!mutex) return;
    if (__sync_lock_test_and_set(mutex, 0) == 2) {
        firstos_futex(mutex, FIRSTOS_FUTEX_WAKE, 1);
    }
}

