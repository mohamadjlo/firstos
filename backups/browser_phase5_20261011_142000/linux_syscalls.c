#include "linux_syscalls.h"
#include "display_server.h"
#include "fs.h"
#include "heap.h"
#include "serial.h"
#include "rtc.h"
#include <string.h>

#define MAX_FD 128

typedef enum {
    FD_TYPE_NONE = 0,
    FD_TYPE_STDIN,
    FD_TYPE_STDOUT,
    FD_TYPE_STDERR,
    FD_TYPE_FILE,
    FD_TYPE_X11_SOCKET,
    FD_TYPE_NET_SOCKET,
    FD_TYPE_DEV_NULL,
    FD_TYPE_DEV_URANDOM
} fd_type_t;

typedef struct {
    int active;
    fd_type_t type;
    char path[256];
    u32 offset;
    u32 size;
    char *buffer;
} linux_fd_t;

static linux_fd_t fd_table[MAX_FD];
static u32 current_program_brk = 0x09000000;
static u32 current_mmap_top   = 0x70000000;

/* Linux structures for syscall translation */
struct linux_stat64 {
    u64 st_dev;
    u32 __pad1;
    u32 st_ino;
    u32 st_mode;
    u32 st_nlink;
    u32 st_uid;
    u32 st_gid;
    u64 st_rdev;
    u32 __pad2;
    u64 st_size;
    u32 st_blksize;
    u64 st_blocks;
    u32 st_atime;
    u32 st_atime_nsec;
    u32 st_mtime;
    u32 st_mtime_nsec;
    u32 st_ctime;
    u32 st_ctime_nsec;
    u64 st_ino64;
} __attribute__((packed));

struct linux_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct linux_timeval {
    int tv_sec;
    int tv_usec;
};

struct user_desc {
    unsigned int  entry_number;
    unsigned int  base_addr;
    unsigned int  limit;
    unsigned int  seg_32bit:1;
    unsigned int  contents:2;
    unsigned int  read_exec_only:1;
    unsigned int  limit_in_pages:1;
    unsigned int  seg_not_present:1;
    unsigned int  useable:1;
};

void linux_syscalls_init(void) {
    memset(fd_table, 0, sizeof(fd_table));
    
    /* 0: stdin */
    fd_table[0].active = 1;
    fd_table[0].type = FD_TYPE_STDIN;
    
    /* 1: stdout */
    fd_table[1].active = 1;
    fd_table[1].type = FD_TYPE_STDOUT;
    
    /* 2: stderr */
    fd_table[2].active = 1;
    fd_table[2].type = FD_TYPE_STDERR;
    
    serial_write("[linux_abi] Linux System Call Subsystem initialized\n");
}

static int alloc_fd(fd_type_t type) {
    for (int i = 3; i < MAX_FD; i++) {
        if (!fd_table[i].active) {
            fd_table[i].active = 1;
            fd_table[i].type = type;
            fd_table[i].offset = 0;
            fd_table[i].size = 0;
            fd_table[i].buffer = NULL;
            fd_table[i].path[0] = 0;
            return i;
        }
    }
    return -1;
}

static void free_fd(int fd) {
    if (fd >= 3 && fd < MAX_FD && fd_table[fd].active) {
        if (fd_table[fd].buffer) {
            kfree(fd_table[fd].buffer);
            fd_table[fd].buffer = NULL;
        }
        if (fd_table[fd].type == FD_TYPE_X11_SOCKET) {
            display_server_set_client_connected(0, 0);
        }
        memset(&fd_table[fd], 0, sizeof(linux_fd_t));
    }
}

u64 linux_syscall_dispatcher(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6) {
    u64 ebx = arg1;
    u64 ecx = arg2;
    u64 edx = arg3;
    u64 esi = arg4;
    u64 edi = arg5;
    u64 ebp = arg6;
    (void)esi; (void)edi; (void)ebp;

    switch (num) {
        case 1: /* __NR_exit */
            serial_write("[linux] exit() called\n");
            return 0;

        case 3: /* __NR_read (int fd, void *buf, size_t count) */ {
            int fd = (int)ebx;
            void *buf = (void *)ecx;
            u32 count = edx;
            if (fd < 0 || fd >= MAX_FD || !fd_table[fd].active || !buf) return (u32)-9; /* EBADF */

            if (fd_table[fd].type == FD_TYPE_X11_SOCKET) {
                int got = display_server_handle_client_data(NULL, 0, (u8 *)buf, count);
                return (u32)got;
            }

            if (fd_table[fd].type == FD_TYPE_DEV_URANDOM) {
                u8 *p = (u8 *)buf;
                static u32 seed = 0x12345678;
                for (u32 i = 0; i < count; i++) {
                    seed = seed * 1664525u + 1013904223u;
                    p[i] = (u8)(seed >> 16);
                }
                return count;
            }

            if (fd_table[fd].type == FD_TYPE_DEV_NULL) return 0;

            if (fd_table[fd].type == FD_TYPE_FILE && fd_table[fd].buffer) {
                u32 avail = (fd_table[fd].size > fd_table[fd].offset) ? 
                            (fd_table[fd].size - fd_table[fd].offset) : 0;
                u32 to_read = (count < avail) ? count : avail;
                if (to_read > 0) {
                    memcpy(buf, fd_table[fd].buffer + fd_table[fd].offset, to_read);
                    fd_table[fd].offset += to_read;
                }
                return to_read;
            }
            return 0;
        }

        case 4: /* __NR_write (int fd, const void *buf, size_t count) */ {
            int fd = (int)ebx;
            const void *buf = (const void *)ecx;
            u32 count = edx;
            if (fd < 0 || fd >= MAX_FD || !fd_table[fd].active || !buf) return (u32)-9;

            if (fd == 1 || fd == 2) {
                /* Write to serial log and FirstOS terminal */
                const char *p = (const char *)buf;
                for (u32 i = 0; i < count; i++) {
                    char c = p[i];
                    char s[2] = {c, 0};
                    serial_write(s);
                }
                return count;
            }

            if (fd_table[fd].type == FD_TYPE_X11_SOCKET) {
                /* Feed X11 data stream directly to native display server! */
                u8 reply_buf[1024];
                int reply_len = display_server_handle_client_data((const u8 *)buf, count, reply_buf, sizeof(reply_buf));
                (void)reply_len;
                return count;
            }

            return count;
        }

        case 5: /* __NR_open (const char *pathname, int flags, mode_t mode) */ {
            const char *path = (const char *)ebx;
            if (!path) return (u32)-14; /* EFAULT */

            /* Intercept X11 display socket */
            if (strstr(path, "X0") || strstr(path, ".X11-unix")) {
                int fd = alloc_fd(FD_TYPE_X11_SOCKET);
                if (fd >= 0) {
                    display_server_set_client_connected(1, (u32)fd);
                    return (u32)fd;
                }
                return (u32)-24; /* EMFILE */
            }

            if (strcmp(path, "/dev/null") == 0) return (u32)alloc_fd(FD_TYPE_DEV_NULL);
            if (strcmp(path, "/dev/urandom") == 0 || strcmp(path, "/dev/random") == 0) return (u32)alloc_fd(FD_TYPE_DEV_URANDOM);

            /* Check filesystem for file */
            int fd = alloc_fd(FD_TYPE_FILE);
            if (fd < 0) return (u32)-24;
            
            strncpy(fd_table[fd].path, path, sizeof(fd_table[fd].path) - 1);
            
            /* Attempt to read file data from FirstOS filesystem */
            char probe[4];
            int size = fs_read_at(path, probe, 0);
            if (size > 0) {
                fd_table[fd].size = (u32)size;
                fd_table[fd].buffer = kmalloc((u32)size + 1);
                if (fd_table[fd].buffer) {
                    fs_read_at(path, fd_table[fd].buffer, (u32)size);
                }
                return (u32)fd;
            }

            /* File exists as placeholder or not found */
            return (u32)fd;
        }

        case 6: /* __NR_close (int fd) */
            free_fd((int)ebx);
            return 0;

        case 19: /* __NR_lseek (int fd, off_t offset, int whence) */ {
            int fd = (int)ebx;
            int offset = (int)ecx;
            int whence = (int)edx;
            if (fd < 0 || fd >= MAX_FD || !fd_table[fd].active) return (u32)-9;
            if (whence == 0) fd_table[fd].offset = (u32)offset; /* SEEK_SET */
            else if (whence == 1) fd_table[fd].offset += (u32)offset; /* SEEK_CUR */
            else if (whence == 2) fd_table[fd].offset = fd_table[fd].size + (u32)offset; /* SEEK_END */
            return fd_table[fd].offset;
        }

        case 20: /* __NR_getpid */
            return 100;

        case 24: /* __NR_getuid */
        case 47: /* __NR_getgid */
        case 49: /* __NR_geteuid */
        case 50: /* __NR_getegid */
            return 0; /* root */

        case 33: /* __NR_access (const char *pathname, int mode) */
            return 0; /* file exists and accessible */

        case 45: /* __NR_brk (unsigned long brk) */ {
            u32 new_brk = ebx;
            if (new_brk == 0) return current_program_brk;
            if (new_brk > current_program_brk && new_brk < 0x20000000) {
                current_program_brk = new_brk;
            }
            return current_program_brk;
        }

        case 54: /* __NR_ioctl */
            return 0;

        case 64: /* __NR_getppid */
            return 1;

        case 78: /* __NR_gettimeofday (struct timeval *tv, struct timezone *tz) */ {
            struct linux_timeval *tv = (struct linux_timeval *)ebx;
            if (tv) {
                tv->tv_sec = 1727260800; /* Modern timestamp */
                tv->tv_usec = 0;
            }
            return 0;
        }

        case 85: /* __NR_readlink */
            return (u32)-22; /* EINVAL (not a symlink) */

        case 91: /* __NR_munmap (void *addr, size_t length) */
            return 0;

        case 102: /* __NR_socketcall (int call, unsigned long *args) */ {
            int call = (int)ebx;
            unsigned long *args = (unsigned long *)ecx;
            if (call == 1) { /* SYS_SOCKET (int domain, int type, int protocol) */
                int fd = alloc_fd(FD_TYPE_X11_SOCKET);
                return (fd >= 0) ? (u32)fd : (u32)-24;
            } else if (call == 3) { /* SYS_CONNECT (int sockfd, const struct sockaddr *addr, socklen_t addrlen) */
                int fd = (int)args[0];
                if (fd >= 0 && fd < MAX_FD) {
                    fd_table[fd].type = FD_TYPE_X11_SOCKET;
                    display_server_set_client_connected(1, (u32)fd);
                    return 0; /* Success */
                }
                return (u32)-9;
            } else if (call == 9 || call == 11) { /* SYS_SEND / SYS_SENDTO */
                int fd = (int)args[0];
                const void *buf = (const void *)args[1];
                u32 len = (u32)args[2];
                if (fd >= 0 && fd < MAX_FD && fd_table[fd].type == FD_TYPE_X11_SOCKET) {
                    u8 resp[1024];
                    display_server_handle_client_data((const u8 *)buf, len, resp, sizeof(resp));
                    return len;
                }
                return len;
            } else if (call == 10 || call == 12) { /* SYS_RECV / SYS_RECVFROM */
                int fd = (int)args[0];
                void *buf = (void *)args[1];
                u32 len = (u32)args[2];
                if (fd >= 0 && fd < MAX_FD && fd_table[fd].type == FD_TYPE_X11_SOCKET) {
                    int got = display_server_handle_client_data(NULL, 0, (u8 *)buf, len);
                    return (u32)got;
                }
                return 0;
            }
            return 0;
        }

        case 116: /* __NR_sysinfo */
            return 0;

        case 120: /* __NR_clone */
            return 101; /* child pid */

        case 122: /* __NR_uname (struct utsname *buf) */ {
            struct linux_utsname *u = (struct linux_utsname *)ebx;
            if (u) {
                strcpy(u->sysname, "Linux");
                strcpy(u->nodename, "firstos-box");
                strcpy(u->release, "5.15.0-firstos");
                strcpy(u->version, "FirstOS-Linux-Subsystem");
                strcpy(u->machine, "i686");
                strcpy(u->domainname, "local");
            }
            return 0;
        }

        case 125: /* __NR_mprotect */
            return 0;

        case 140: /* __NR__llseek */ {
            int fd = (int)ebx;
            u32 off_hi = ecx;
            u32 off_lo = edx;
            u64 *result = (u64 *)esi;
            int whence = (int)edi;
            u64 full_offset = ((u64)off_hi << 32) | off_lo;
            if (fd < 0 || fd >= MAX_FD || !fd_table[fd].active) return (u32)-9;
            if (whence == 0) fd_table[fd].offset = (u32)full_offset;
            else if (whence == 1) fd_table[fd].offset += (u32)full_offset;
            else if (whence == 2) fd_table[fd].offset = fd_table[fd].size + (u32)full_offset;
            if (result) *result = (u64)fd_table[fd].offset;
            return 0;
        }

        case 142: /* __NR__newselect */
            return 1; /* Always report ready */

        case 162: /* __NR_nanosleep */
            return 0;

        case 168: /* __NR_poll */
            return 1; /* Always report ready */

        case 174: /* __NR_rt_sigaction */
        case 175: /* __NR_rt_sigprocmask */
        case 186: /* __NR_sigaltstack */
            return 0;

        case 183: /* __NR_getcwd (char *buf, size_t size) */ {
            char *buf = (char *)ebx;
            u32 size = ecx;
            const char *cwd = "/home/user";
            if (buf && size > strlen(cwd)) {
                strcpy(buf, cwd);
                return (u32)strlen(cwd) + 1;
            }
            return (u32)-34; /* ERANGE */
        }

        case 192: /* __NR_mmap2 (void *addr, size_t length, int prot, int flags, int fd, off_t pgoff) */ {
            u32 addr = ebx;
            u32 len = ecx;
            u32 aligned_len = (len + 4095) & ~4095;
            if (!aligned_len) return (u32)-22; /* EINVAL */

            u32 ret_addr;
            if (addr != 0) {
                ret_addr = addr;
            } else {
                current_mmap_top -= aligned_len;
                ret_addr = current_mmap_top;
            }
            return ret_addr;
        }

        case 195: /* __NR_stat64 */
        case 197: /* __NR_fstat64 */ {
            struct linux_stat64 *st = (struct linux_stat64 *)ecx;
            if (st) {
                memset(st, 0, sizeof(*st));
                st->st_mode = 0100755; /* Regular executable file */
                st->st_nlink = 1;
                st->st_size = 1048576; /* 1MB */
                st->st_blksize = 4096;
            }
            return 0;
        }

        case 224: /* __NR_gettid */
            return 100;

        case 240: /* __NR_futex */
            return 0; /* Return immediately without blocking */

        case 243: /* __NR_set_thread_area (struct user_desc *u_info) */ {
            struct user_desc *info = (struct user_desc *)ebx;
            if (info) {
                extern void gdt_set_gate(int num, unsigned int base, unsigned int limit, unsigned char access, unsigned char gran);
                info->entry_number = 6; /* GDT slot 6 (selector 0x33) */
                gdt_set_gate(6, info->base_addr, info->limit, 0xF2, 0xCF);
                return 0;
            }
            return (u32)-14;
        }

        case 252: /* __NR_exit_group */
            serial_write("[linux] exit_group() called\n");
            return 0;

        case 258: /* __NR_set_tid_address */
            return 100;

        case 295: /* __NR_openat */ {
            /* Forward to sys_open using path at ecx */
            return linux_syscall_dispatcher(5, ecx, edx, esi, 0, 0, 0);
        }

        case 300: /* __NR_fstatat64 */
            return linux_syscall_dispatcher(195, ecx, edx, 0, 0, 0, 0);

        case 307: /* __NR_faccessat */
            return 0;

        default: {
            char buf[64];
            serial_write("[linux] unhandled syscall: ");
            /* Convert num to hex and log */
            buf[0] = '0'; buf[1] = 'x';
            for (int h = 0; h < 4; h++) {
                int v = (num >> ((3 - h) * 4)) & 0xF;
                buf[2 + h] = (v < 10) ? ('0' + v) : ('A' + v - 10);
            }
            buf[6] = '\n'; buf[7] = 0;
            serial_write(buf);
            return (u64)(long)-38; /* -ENOSYS */
        }
    }
}
