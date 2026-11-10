#include "syscall.h"
#include "idt.h"
#include "serial.h"
#include "terminal.h"
#include "task.h"
#include "process.h"
#include "shm.h"
#include "futex.h"
#include "linux_syscalls.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

extern task_t tasks[MAX_TASKS];
extern int current_task;
extern u64 kernel_pml4[512];
extern void load_page_directory(u64*);

int validate_user_buffer(const void *ptr, unsigned int len) {
    if (!ptr || len == 0) return 0;
    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end = start + len;
    if (end < start) return 0; /* integer overflow */
    if (start < USER_SPACE_START || end > USER_SPACE_END) {
        return 0;
    }
    return 1;
}

int validate_user_string(const char *str, unsigned int max_len) {
    if (!str || max_len == 0) return 0;
    uintptr_t start = (uintptr_t)str;
    if (start < USER_SPACE_START || start >= USER_SPACE_END) {
        return 0;
    }
    uintptr_t max_avail = USER_SPACE_END - start;
    if (max_len > max_avail) max_len = max_avail;

    for (unsigned int i = 0; i < max_len; i++) {
        if (str[i] == '\0') return 1;
    }
    return 0;
}

#include "include/firstos/uapi/syscall.h"

u64 syscall_handler(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6) {
    serial_write("[syscall] number=");
    serial_write_hex((u32)num);
    serial_write(" pid=");
    serial_write_hex((u32)current_task);
    serial_write("\n");

    u64 ret = 0;

    if (num == FIRSTOS_SYS_PRINT) { // 0
        const char *str = (const char *)arg1;
        if (!validate_user_string(str, 1024)) {
            serial_write("[syscall] EFAULT: rejected kernel or invalid pointer: ");
            serial_write_hex((u32)(uintptr_t)str);
            serial_write("\n");
            ret = (u64)-FIRSTOS_EFAULT;
        } else {
            serial_write("[syscall] sys_print: ");
            serial_write(str);
            ret = 0;
        }
    } else if (num == FIRSTOS_SYS_EXIT) { // 1
        process_exit(current_task, (int)arg1);
        ret = 0;
    } else if (num == FIRSTOS_SYS_YIELD) { // 2
        __asm__ volatile ("int $0x81");
        ret = 0;
    } else if (num == FIRSTOS_SYS_READ) { // 3
        int fd = (int)arg1;
        void *buf = (void *)arg2;
        unsigned int count = (unsigned int)arg3;
        if (count == 0) {
            ret = 0;
        } else if (!validate_user_buffer(buf, count)) {
            serial_write("[syscall] EFAULT: rejected kernel or invalid read buffer: ");
            serial_write_hex((u32)(uintptr_t)buf);
            serial_write("\n");
            ret = (u64)-FIRSTOS_EFAULT;
        } else {
            ret = (u64)proc_fd_read(current_task, fd, buf, count);
        }
    } else if (num == FIRSTOS_SYS_WRITE) { // 4
        int fd = (int)arg1;
        const void *buf = (const void *)arg2;
        unsigned int count = (unsigned int)arg3;
        if (count == 0) {
            ret = 0;
        } else if (!validate_user_buffer(buf, count)) {
            serial_write("[syscall] EFAULT: rejected kernel or invalid buffer: ");
            serial_write_hex((u32)(uintptr_t)buf);
            serial_write("\n");
            ret = (u64)-FIRSTOS_EFAULT;
        } else {
            ret = (u64)proc_fd_write(current_task, fd, buf, count);
        }
    } else if (num == FIRSTOS_SYS_CLOSE) { // 6
        int fd = (int)arg1;
        ret = (unsigned int)proc_fd_close(current_task, fd);
    } else if (num == FIRSTOS_SYS_WAITPID) { // 7
        int target_pid = (int)arg1;
        int *st_ptr = (int *)arg2;
        int opts = (int)arg3;
        if (st_ptr && !validate_user_buffer(st_ptr, sizeof(int))) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (unsigned int)process_waitpid(current_task, target_pid, st_ptr, opts);
        }
    } else if (num == FIRSTOS_SYS_EXEC) { // 11
        const char *path = (const char *)arg1;
        const char *const *argv = (const char *const *)arg2;
        if (!validate_user_string(path, 256)) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (unsigned int)process_exec(current_task, path, argv);
        }
    } else if (num == FIRSTOS_SYS_GETPID) { // 20
        ret = (unsigned int)current_task;
    } else if (num == FIRSTOS_SYS_DUP) { // 41
        int oldfd = (int)arg1;
        ret = (unsigned int)proc_fd_dup(current_task, oldfd);
    } else if (num == FIRSTOS_SYS_PIPE) { // 42
        int *pipefd = (int *)arg1;
        if (!validate_user_buffer(pipefd, 2 * sizeof(int))) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (unsigned int)proc_pipe_create(current_task, pipefd);
        }
    } else if (num == FIRSTOS_SYS_BRK) { // 45
        unsigned int req_brk = arg1;
        if (current_task >= 0 && current_task < MAX_TASKS) {
            static unsigned int task_user_brk[MAX_TASKS];
            if (task_user_brk[current_task] == 0) {
                task_user_brk[current_task] = FIRSTOS_USER_HEAP_BASE;
            }
            if (req_brk == 0) {
                ret = task_user_brk[current_task];
            } else if (req_brk >= FIRSTOS_USER_HEAP_BASE && req_brk < FIRSTOS_USER_HEAP_MAX) {
                task_user_brk[current_task] = req_brk;
                ret = req_brk;
            } else {
                ret = task_user_brk[current_task];
            }
        } else {
            ret = (u64)(long)-FIRSTOS_ENOMEM;
        }
    } else if (num == FIRSTOS_SYS_DUP2) { // 63
        int oldfd = (int)arg1;
        int newfd = (int)arg2;
        ret = (unsigned int)proc_fd_dup2(current_task, oldfd, newfd);
    } else if (num == FIRSTOS_SYS_SPAWN) { // 120
        const char *path = (const char *)arg1;
        const char *const *argv = (const char *const *)arg2;
        if (!validate_user_string(path, 256)) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (unsigned int)process_spawn(current_task, path, argv);
        }
    } else if (num == FIRSTOS_SYS_SHM_OPEN) { // 121
        const char *name = (const char *)arg1;
        int flags = (int)arg2;
        int mode = (int)arg3;
        if (!validate_user_string(name, 32)) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (unsigned int)shm_open(name, flags, mode);
        }
    } else if (num == FIRSTOS_SYS_SHM_MAP) { // 122
        int shmid = (int)arg1;
        void **out_ptr = (void **)arg2;
        if (out_ptr && !validate_user_buffer(out_ptr, sizeof(void*))) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            void *v = NULL;
            int m_rc = shm_map(current_task, shmid, &v);
            if (m_rc == 0 && out_ptr) {
                *out_ptr = v;
            }
            ret = (unsigned int)m_rc;
        }
    } else if (num == FIRSTOS_SYS_SHM_UNMAP) { // 123
        int shmid = (int)arg1;
        ret = (unsigned int)shm_unmap(current_task, shmid);
    } else if (num == FIRSTOS_SYS_FUTEX) { // 240
        int *uaddr = (int *)arg1;
        int op = (int)arg2;
        int val = (int)arg3;
        if (!validate_user_buffer(uaddr, sizeof(int))) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else if (op == FIRSTOS_FUTEX_WAIT) {
            ret = (unsigned int)futex_wait(current_task, uaddr, val);
        } else if (op == FIRSTOS_FUTEX_WAKE) {
            ret = (unsigned int)futex_wake(uaddr, val);
        } else {
            ret = (u64)(long)-FIRSTOS_EINVAL;
        }
    } else {
        ret = linux_syscall_dispatcher(num, arg1, arg2, arg3, arg4, arg5, arg6);
    }

    serial_write("[syscall] return=");
    serial_write_hex(ret);
    serial_write("\n");
    return ret;
}

void syscall_init(void) {
    idt_init();
    linux_syscalls_init();
    process_subsystem_init();
    serial_write("[kernel] IDT, Syscalls, Linux ABI, and Process Subsystem initialized\n");
}
