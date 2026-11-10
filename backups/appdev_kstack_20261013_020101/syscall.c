#include "syscall.h"
#include "idt.h"
#include "serial.h"
#include "terminal.h"
#include "task.h"
#include "process.h"
#include "shm.h"
#include "futex.h"
#include "linux_syscalls.h"
#include "vfs_fd.h"
#include "net_socket.h"

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
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            serial_write("[syscall] sys_print: ");
            serial_write(str);
            ret = 0;
        }
    } else if (num == FIRSTOS_SYS_EXIT) { // 1
        process_exit(current_task, (int)arg1);
        __asm__ volatile ("int $0x81");
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
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (u64)(long)proc_fd_read(current_task, fd, buf, count);
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
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (u64)(long)proc_fd_write(current_task, fd, buf, count);
        }
    } else if (num == FIRSTOS_SYS_OPEN) { // 5
        const char *path = (const char *)arg1;
        int flags = (int)arg2;
        int mode = (int)arg3;
        if (process_table[current_task].role == PROC_ROLE_RENDERER || !process_has_capability(current_task, PROC_CAP_FILE_OPEN)) {
            serial_write("[policy] deny service=renderer action=file-open path=");
            serial_write(path ? path : "null");
            serial_write("\n");
            ret = (u64)(long)-FIRSTOS_EACCES;
        } else if (!validate_user_string(path, 256)) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            int vfd = vfs_open(path, flags, mode);
            if (vfd < 0) {
                ret = (u64)(long)-FIRSTOS_ENOENT;
            } else {
                int pfd = -1;
                for (int f = 3; f < FIRSTOS_MAX_FDS; f++) {
                    if (!process_table[current_task].fds[f].used) {
                        pfd = f;
                        break;
                    }
                }
                if (pfd == -1) {
                    vfs_close(vfd);
                    ret = (u64)(long)-FIRSTOS_EMFILE;
                } else {
                    process_table[current_task].fds[pfd].used = 1;
                    process_table[current_task].fds[pfd].flags = flags;
                    process_table[current_task].fds[pfd].kind = FD_KIND_FILE;
                    process_table[current_task].fds[pfd].vfs_fd = vfd;
                    ret = (u64)(long)pfd;
                }
            }
        }
    } else if (num == FIRSTOS_SYS_CLOSE) { // 6
        int fd = (int)arg1;
        ret = (u64)(long)proc_fd_close(current_task, fd);
    } else if (num == FIRSTOS_SYS_WAITPID) { // 7
        int target_pid = (int)arg1;
        int *st_ptr = (int *)arg2;
        int opts = (int)arg3;
        if (st_ptr && !validate_user_buffer(st_ptr, sizeof(int))) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (u64)(long)process_waitpid(current_task, target_pid, st_ptr, opts);
        }
    } else if (num == FIRSTOS_SYS_EXEC) { // 11
        const char *path = (const char *)arg1;
        const char *const *argv = (const char *const *)arg2;
        if (!validate_user_string(path, 256)) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (u64)(long)process_exec(current_task, path, argv);
        }
    } else if (num == FIRSTOS_SYS_LSEEK) { // 19
        int fd = (int)arg1;
        int offset = (int)arg2;
        int whence = (int)arg3;
        if (fd < 0 || fd >= FIRSTOS_MAX_FDS || !process_table[current_task].fds[fd].used) {
            ret = (u64)(long)-FIRSTOS_EBADF;
        } else if (process_table[current_task].fds[fd].kind != FD_KIND_FILE) {
            ret = (u64)(long)-FIRSTOS_EINVAL;
        } else {
            ret = (u64)(long)vfs_seek(process_table[current_task].fds[fd].vfs_fd, offset, whence);
        }
    } else if (num == FIRSTOS_SYS_GETPID) { // 20
        ret = (u64)(long)current_task;
    } else if (num == FIRSTOS_SYS_FSTAT) { // 28
        int fd = (int)arg1;
        void *st_ptr = (void *)arg2;
        if (!validate_user_buffer(st_ptr, sizeof(vfs_stat_t))) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else if (fd < 0 || fd >= FIRSTOS_MAX_FDS || !process_table[current_task].fds[fd].used) {
            ret = (u64)(long)-FIRSTOS_EBADF;
        } else if (process_table[current_task].fds[fd].kind == FD_KIND_FILE) {
            ret = (u64)(long)vfs_fstat(process_table[current_task].fds[fd].vfs_fd, (vfs_stat_t *)st_ptr);
        } else {
            ret = 0;
        }
    } else if (num == FIRSTOS_SYS_NANOSLEEP) { // 35
        u64 nsec = arg1;
        u32 start_ticks = get_system_ticks();
        u32 wait_ticks = (u32)(nsec / 10000000ULL);
        if (wait_ticks == 0) wait_ticks = 1;
        while ((get_system_ticks() - start_ticks) < wait_ticks) {
            __asm__ volatile ("int $0x81");
        }
        ret = 0;
    } else if (num == FIRSTOS_SYS_DUP) { // 41
        int oldfd = (int)arg1;
        ret = (u64)(long)proc_fd_dup(current_task, oldfd);
    } else if (num == FIRSTOS_SYS_PIPE) { // 42
        int *pipefd = (int *)arg1;
        if (!validate_user_buffer(pipefd, 2 * sizeof(int))) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (u64)(long)proc_pipe_create(current_task, pipefd);
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
        ret = (u64)(long)proc_fd_dup2(current_task, oldfd, newfd);
    } else if (num == FIRSTOS_SYS_GETPPID) { // 64
        ret = (u64)(long)process_table[current_task].parent_pid;
    } else if (num == FIRSTOS_SYS_SPAWN) { // 120
        const char *path = (const char *)arg1;
        const char *const *argv = (const char *const *)arg2;
        if (!validate_user_string(path, 256)) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (u64)(long)process_spawn(current_task, path, argv);
        }
    } else if (num == FIRSTOS_SYS_SHM_OPEN) { // 121
        const char *name = (const char *)arg1;
        int flags = (int)arg2;
        int mode = (int)arg3;
        if (!validate_user_string(name, 32)) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (u64)(long)shm_open(name, flags, mode);
        }
    } else if (num == FIRSTOS_SYS_SHM_MAP) { // 122
        int shmid = (int)arg1;
        void **out_ptr = (void **)arg2;
        if (process_table[current_task].role == PROC_ROLE_RENDERER && !process_has_capability(current_task, PROC_CAP_SHM_ATTACH)) {
            serial_write("[cap] role=renderer shm-attach denied\n");
            ret = (u64)(long)-FIRSTOS_EACCES;
        } else if (out_ptr && !validate_user_buffer(out_ptr, sizeof(void*))) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            void *v = NULL;
            int m_rc = shm_map(current_task, shmid, &v);
            if (m_rc == 0 && out_ptr) {
                *out_ptr = v;
            }
            ret = (u64)(long)m_rc;
        }
    } else if (num == FIRSTOS_SYS_SHM_UNMAP) { // 123
        int shmid = (int)arg1;
        ret = (u64)(long)shm_unmap(current_task, shmid);
    } else if (num == FIRSTOS_SYS_SOCKET) { // 198
        const char *host = (const char *)arg1;
        u16 port = (u16)arg2;
        int proto = (int)arg3;
        if (process_table[current_task].role == PROC_ROLE_RENDERER || !process_has_capability(current_task, PROC_CAP_NETWORK_DIRECT)) {
            serial_write("[policy] deny service=renderer action=network-direct\n");
            ret = (u64)(long)-FIRSTOS_EACCES;
        } else if (host && !validate_user_string(host, 128)) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            net_socket_t *sock = net_socket_open(host ? host : "127.0.0.1", port, (socket_proto_t)proto, 500000);
            if (!sock) {
                ret = (u64)(long)-FIRSTOS_ECONNREFUSED;
            } else {
                int sfd = proc_fd_open_socket(current_task, sock);
                if (sfd < 0) {
                    net_socket_close(sock);
                    ret = (u64)(long)-FIRSTOS_EMFILE;
                } else {
                    ret = (u64)(long)sfd;
                }
            }
        }
    } else if (num == FIRSTOS_SYS_CONNECT) { // 203
        ret = 0;
    } else if (num == FIRSTOS_SYS_SEND) { // 204
        int fd = (int)arg1;
        const void *buf = (const void *)arg2;
        unsigned int count = (unsigned int)arg3;
        if (!validate_user_buffer(buf, count)) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (u64)(long)proc_fd_write(current_task, fd, buf, count);
        }
    } else if (num == FIRSTOS_SYS_RECV) { // 205
        int fd = (int)arg1;
        void *buf = (void *)arg2;
        unsigned int count = (unsigned int)arg3;
        if (!validate_user_buffer(buf, count)) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else {
            ret = (u64)(long)proc_fd_read(current_task, fd, buf, count);
        }
    } else if (num == FIRSTOS_SYS_FUTEX) { // 240
        int *uaddr = (int *)arg1;
        int op = (int)arg2;
        int val = (int)arg3;
        if (!validate_user_buffer(uaddr, sizeof(int))) {
            ret = (u64)(long)-FIRSTOS_EFAULT;
        } else if (op == FIRSTOS_FUTEX_WAIT) {
            ret = (u64)(long)futex_wait(current_task, uaddr, val);
        } else if (op == FIRSTOS_FUTEX_WAKE) {
            ret = (u64)(long)futex_wake(uaddr, val);
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
