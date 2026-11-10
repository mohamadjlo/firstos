#include "process.h"
#include "pipe.h"
#include "shm.h"
#include "futex.h"
#include "paging.h"
#include "serial.h"
#include "heap.h"
#include "fs.h"
#include "userspace_loader.h"
#include "include/firstos/uapi/memory.h"
#include "syscall.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

process_info_t process_table[FIRSTOS_MAX_PROCESSES];
extern unsigned char task_user_kstacks[MAX_TASKS][4096];
extern task_t tasks[MAX_TASKS];
extern int current_task;
extern u64 kernel_pml4[512];
extern void load_page_directory(u64*);

static unsigned int p_strlen(const char *s) {
    unsigned int len = 0;
    if (!s) return 0;
    while (s[len]) len++;
    return len;
}

static void p_strcpy(char *dest, const char *src) {
    int i = 0;
    while (src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static u64 s_proc_pml4[FIRSTOS_MAX_PROCESSES][512] __attribute__((aligned(4096)));
static u64 s_proc_pdpt[FIRSTOS_MAX_PROCESSES][512] __attribute__((aligned(4096)));
static u64 s_proc_pd[FIRSTOS_MAX_PROCESSES][512] __attribute__((aligned(4096)));

static u64 s_exec_temp_pml4[512] __attribute__((aligned(4096)));
static u64 s_exec_temp_pdpt[512] __attribute__((aligned(4096)));
static u64 s_exec_temp_pd[512] __attribute__((aligned(4096)));

void process_subsystem_init(void) {
    pipe_subsystem_init();
    shm_subsystem_init();
    futex_subsystem_init();
    for (int i = 0; i < FIRSTOS_MAX_PROCESSES; i++) {
        process_table[i].pid = i;
        process_table[i].parent_pid = -1;
        process_table[i].state = PROC_STATE_UNUSED;
        process_table[i].exit_status = 0;
        process_table[i].exit_code_valid = 0;
        process_table[i].page_dir = NULL;
        process_table[i].proc_pd = NULL;
        process_table[i].user_mem = NULL;
        process_table[i].attached_shm_id = -1;
        for (int f = 0; f < FIRSTOS_MAX_FDS; f++) {
            process_table[i].fds[f].used = 0;
            process_table[i].fds[f].flags = 0;
            process_table[i].fds[f].kind = FD_KIND_NONE;
            process_table[i].fds[f].vfs_fd = -1;
        }
    }

    /* Process 0 is Kernel */
    process_table[0].pid = 0;
    process_table[0].parent_pid = 0;
    process_table[0].state = PROC_STATE_RUNNING;
    proc_fd_init(0);

    serial_write("[process] Process management subsystem initialized (16 slots)\n");
}

int proc_fd_init(int pid) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    for (int f = 0; f < FIRSTOS_MAX_FDS; f++) {
        process_table[pid].fds[f].used = 0;
        process_table[pid].fds[f].flags = 0;
        process_table[pid].fds[f].kind = FD_KIND_NONE;
        process_table[pid].fds[f].vfs_fd = -1;
    }
    /* Standard descriptors */
    process_table[pid].fds[0].used = 1;
    process_table[pid].fds[0].flags = 0; /* O_RDONLY */
    process_table[pid].fds[0].kind = FD_KIND_CONSOLE;

    process_table[pid].fds[1].used = 1;
    process_table[pid].fds[1].flags = 1; /* O_WRONLY */
    process_table[pid].fds[1].kind = FD_KIND_CONSOLE;

    process_table[pid].fds[2].used = 1;
    process_table[pid].fds[2].flags = 1; /* O_WRONLY */
    process_table[pid].fds[2].kind = FD_KIND_CONSOLE;
    return 0;
}

int proc_fd_close(int pid, int fd) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (fd < 0 || fd >= FIRSTOS_MAX_FDS) return -FIRSTOS_EBADF;
    process_fd_t *e = &process_table[pid].fds[fd];
    if (!e->used) return -FIRSTOS_EBADF;

    if (e->kind == FD_KIND_FILE && e->vfs_fd >= 0) {
        vfs_close(e->vfs_fd);
    } else if (e->kind == FD_KIND_PIPE_READ) {
        pipe_close_endpoint(e->pipe_idx, 0);
    } else if (e->kind == FD_KIND_PIPE_WRITE) {
        pipe_close_endpoint(e->pipe_idx, 1);
    }
    e->used = 0;
    e->flags = 0;
    e->kind = FD_KIND_NONE;
    e->target_idx = -1;
    return 0;
}

int proc_fd_read(int pid, int fd, void *buf, unsigned int count) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (fd < 0 || fd >= FIRSTOS_MAX_FDS) return -FIRSTOS_EBADF;
    process_fd_t *e = &process_table[pid].fds[fd];
    if (!e->used) return -FIRSTOS_EBADF;

    if (e->kind == FD_KIND_CONSOLE) {
        if (fd == 0) {
            /* Stdin: non-blocking check from serial */
            extern int serial_has_char(void);
            extern char serial_read_char(void);
            unsigned int read_cnt = 0;
            char *cbuf = (char *)buf;
            while (read_cnt < count && serial_has_char()) {
                cbuf[read_cnt++] = serial_read_char();
            }
            return (int)read_cnt;
        }
        return -FIRSTOS_EBADF; /* stdout/stderr cannot be read */
    } else if (e->kind == FD_KIND_FILE) {
        if (e->vfs_fd < 0) return -FIRSTOS_EBADF;
        return vfs_read(e->vfs_fd, buf, count);
    } else if (e->kind == FD_KIND_PIPE_READ) {
        return pipe_read(e->pipe_idx, buf, count);
    } else if (e->kind == FD_KIND_PIPE_WRITE) {
        return -FIRSTOS_EBADF; /* cannot read from write end of pipe */
    }
    return -FIRSTOS_EBADF;
}

int proc_fd_write(int pid, int fd, const void *buf, unsigned int count) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (fd < 0 || fd >= FIRSTOS_MAX_FDS) return -FIRSTOS_EBADF;
    process_fd_t *e = &process_table[pid].fds[fd];
    if (!e->used) return -FIRSTOS_EBADF;

    if (e->kind == FD_KIND_CONSOLE) {
        if (fd == 1 || fd == 2) {
            const char *p = (const char *)buf;
            for (unsigned int i = 0; i < count; i++) {
                char s[2] = {p[i], 0};
                serial_write(s);
            }
            return (int)count;
        }
        return -FIRSTOS_EBADF; /* stdin cannot be written */
    } else if (e->kind == FD_KIND_FILE) {
        if (e->vfs_fd < 0) return -FIRSTOS_EBADF;
        return vfs_write(e->vfs_fd, buf, count);
    } else if (e->kind == FD_KIND_PIPE_WRITE) {
        return pipe_write(e->pipe_idx, buf, count);
    } else if (e->kind == FD_KIND_PIPE_READ) {
        return -FIRSTOS_EBADF; /* cannot write to read end of pipe */
    }
    return -FIRSTOS_EBADF;
}

int proc_fd_dup(int pid, int oldfd) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (oldfd < 0 || oldfd >= FIRSTOS_MAX_FDS) return -FIRSTOS_EBADF;
    process_fd_t *src = &process_table[pid].fds[oldfd];
    if (!src->used) return -FIRSTOS_EBADF;

    int newfd = -1;
    for (int f = 0; f < FIRSTOS_MAX_FDS; f++) {
        if (!process_table[pid].fds[f].used) {
            newfd = f;
            break;
        }
    }
    if (newfd == -1) return -FIRSTOS_EMFILE;

    process_table[pid].fds[newfd] = *src;
    if (src->kind == FD_KIND_PIPE_READ) {
        pipe_add_ref(src->pipe_idx, 0);
    } else if (src->kind == FD_KIND_PIPE_WRITE) {
        pipe_add_ref(src->pipe_idx, 1);
    }
    return newfd;
}

int proc_fd_dup2(int pid, int oldfd, int newfd) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (oldfd < 0 || oldfd >= FIRSTOS_MAX_FDS) return -FIRSTOS_EBADF;
    if (newfd < 0 || newfd >= FIRSTOS_MAX_FDS) return -FIRSTOS_EBADF;

    process_fd_t *src = &process_table[pid].fds[oldfd];
    if (!src->used) return -FIRSTOS_EBADF;

    if (oldfd == newfd) return newfd;

    if (process_table[pid].fds[newfd].used) {
        proc_fd_close(pid, newfd);
    }

    process_table[pid].fds[newfd] = *src;
    if (src->kind == FD_KIND_PIPE_READ) {
        pipe_add_ref(src->pipe_idx, 0);
    } else if (src->kind == FD_KIND_PIPE_WRITE) {
        pipe_add_ref(src->pipe_idx, 1);
    }
    return newfd;
}

int proc_pipe_create(int pid, int out_pipefd[2]) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (!out_pipefd) return -FIRSTOS_EFAULT;

    int rfd = -1, wfd = -1;
    for (int f = 0; f < FIRSTOS_MAX_FDS; f++) {
        if (!process_table[pid].fds[f].used) {
            if (rfd == -1) rfd = f;
            else if (wfd == -1) { wfd = f; break; }
        }
    }
    if (rfd == -1 || wfd == -1) return -FIRSTOS_EMFILE;

    int pipe_idx = -1;
    int rc = pipe_create(&pipe_idx);
    if (rc != 0) return rc;

    process_table[pid].fds[rfd].used = 1;
    process_table[pid].fds[rfd].flags = 0; /* O_RDONLY */
    process_table[pid].fds[rfd].kind = FD_KIND_PIPE_READ;
    process_table[pid].fds[rfd].pipe_idx = pipe_idx;

    process_table[pid].fds[wfd].used = 1;
    process_table[pid].fds[wfd].flags = 1; /* O_WRONLY */
    process_table[pid].fds[wfd].kind = FD_KIND_PIPE_WRITE;
    process_table[pid].fds[wfd].pipe_idx = pipe_idx;

    out_pipefd[0] = rfd;
    out_pipefd[1] = wfd;
    return 0;
}

static int find_free_process_slot(void) {
    for (int i = 1; i < FIRSTOS_MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_STATE_UNUSED && !tasks[i].active) {
            return i;
        }
    }
    return -1;
}

struct elf64_header {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned int version;
    u64 entry;
    u64 phoff;
    u64 shoff;
    unsigned int flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
};

struct elf64_phdr {
    unsigned int type;
    unsigned int flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 filesz;
    u64 memsz;
    u64 align;
};

int process_spawn(int parent_pid, const char *path, const char *const *argv) {
    if (!path || !path[0]) return -FIRSTOS_EINVAL;
    if (!fs_ready()) return -FIRSTOS_ENOENT;

    int pid = find_free_process_slot();
    if (pid < 0) return -FIRSTOS_EAGAIN;

    process_table[pid].state = PROC_STATE_NEW;

    int file_size = fs_read_at(path, 0, 0);
    if (file_size <= 0) {
        process_table[pid].state = PROC_STATE_UNUSED;
        return -FIRSTOS_ENOENT;
    }
    if ((unsigned int)file_size > FIRSTOS_ELF_MAX_SIZE) {
        process_table[pid].state = PROC_STATE_UNUSED;
        return -FIRSTOS_ENOMEM;
    }

    char *file_buf = (char *)kmalloc((unsigned int)file_size);
    if (!file_buf) {
        process_table[pid].state = PROC_STATE_UNUSED;
        return -FIRSTOS_ENOMEM;
    }

    int read_bytes = fs_read_raw_at(path, file_buf, (unsigned int)file_size);
    if (read_bytes != file_size) {
        kfree(file_buf);
        process_table[pid].state = PROC_STATE_UNUSED;
        return -FIRSTOS_ENOENT;
    }

    userspace_image_t img;
    int val_rc = userspace_validate_elf(file_buf, (unsigned int)file_size, &img);
    if (val_rc != ELF_OK) {
        kfree(file_buf);
        process_table[pid].state = PROC_STATE_UNUSED;
        return -FIRSTOS_ENOEXEC;
    }

    const struct elf64_header *hdr = (const struct elf64_header *)file_buf;

    u64 *pml4 = &s_proc_pml4[pid][0];
    u64 *pdpt = &s_proc_pdpt[pid][0];
    u64 *pd   = &s_proc_pd[pid][0];

    for (int i = 0; i < 512; i++) {
        pml4[i] = kernel_pml4[i];
    }
    pml4[0] = ((u64)(uintptr_t)pdpt) | 0x07;

    for (int i = 0; i < 512; i++) {
        pdpt[i] = kernel_pdpt[i];
    }
    pdpt[0] = ((u64)(uintptr_t)pd) | 0x07;

    for (int i = 0; i < 512; i++) {
        pd[i] = kernel_pd[0][i];
    }

    char *user_mem = (char *)kmalloc_aligned(FIRSTOS_USER_SIZE, 0x200000);
    if (!user_mem) {
        kfree(file_buf);
        process_table[pid].state = PROC_STATE_UNUSED;
        return -FIRSTOS_ENOMEM;
    }

    u64 *pz = (u64 *)user_mem;
    for (int k = 0; k < (int)(FIRSTOS_USER_SIZE / 8); k++) pz[k] = 0;

    pd[FIRSTOS_USER_PDE_INDEX] = ((u64)(uintptr_t)user_mem) | 0x87;
    pd[FIRSTOS_USER_PDE_INDEX + 1] = ((u64)(uintptr_t)(user_mem + 0x200000)) | 0x87;

    for (int i = 0; i < hdr->phnum; i++) {
        const struct elf64_phdr *ph = (const struct elf64_phdr *)(file_buf + hdr->phoff + i * hdr->phentsize);
        if (ph->type == 1 && ph->memsz > 0) {
            u64 offset = ph->vaddr - FIRSTOS_USER_BASE;
            char *dest = user_mem + offset;
            const char *src = file_buf + ph->offset;
            for (u64 j = 0; j < ph->filesz; j++) dest[j] = src[j];
            for (u64 j = ph->filesz; j < ph->memsz; j++) dest[j] = 0;
        }
    }
    kfree(file_buf);

    /* Setup argc/argv */
    int argc = 0;
    if (argv) {
        while (argv[argc] && argc < FIRSTOS_ELF_MAX_ARGC) argc++;
    }

    u64 str_offset = FIRSTOS_USER_ARG_BASE - FIRSTOS_USER_BASE;
    u64 total_arg_bytes = 0;
    u64 v_args[FIRSTOS_ELF_MAX_ARGC];

    for (int a = 0; a < argc; a++) {
        const char *arg_str = argv[a] ? argv[a] : "";
        unsigned int len = p_strlen(arg_str);
        if (len >= FIRSTOS_ELF_MAX_ARG_LEN) len = FIRSTOS_ELF_MAX_ARG_LEN - 1;
        if (total_arg_bytes + len + 1 > FIRSTOS_ELF_MAX_TOTAL_ARG_LEN) {
            len = FIRSTOS_ELF_MAX_TOTAL_ARG_LEN - total_arg_bytes - 1;
        }
        char *dest_str = user_mem + str_offset;
        for (unsigned int c = 0; c < len; c++) dest_str[c] = arg_str[c];
        dest_str[len] = '\0';
        v_args[a] = FIRSTOS_USER_BASE + str_offset;
        str_offset += len + 1;
        total_arg_bytes += len + 1;
    }

    u64 user_rsp = FIRSTOS_USER_STACK_TOP;
    u64 stack_offset = user_rsp - FIRSTOS_USER_BASE;
    u64 *stack_slots = (u64 *)(user_mem + stack_offset);
    stack_slots[0] = (u64)argc;
    for (int a = 0; a < argc; a++) stack_slots[1 + a] = v_args[a];
    stack_slots[1 + argc] = 0;

    /* Forge task in slot pid for 64-bit Long Mode */
    u64 kstack_top = (u64)(uintptr_t)&task_user_kstacks[pid][4096];
    tasks[pid].rsp0 = kstack_top;
    tasks[pid].cr3 = (u64)(uintptr_t)pml4;
    tasks[pid].tick_count = 0;
    p_strcpy(tasks[pid].name, path);

    u64 *sp = (u64 *)(uintptr_t)kstack_top;
    *--sp = 0x1B;           // SS (User Data: 0x18 | 3)
    *--sp = user_rsp;       // RSP
    *--sp = 0x202;          // RFLAGS
    *--sp = 0x23;           // CS (User Code: 0x20 | 3)
    *--sp = hdr->entry;     // RIP
    for (int k = 0; k < 15; k++) *--sp = 0; // 15 general purpose registers
    tasks[pid].rsp = (u64)(uintptr_t)sp;
    tasks[pid].active = 1;

    /* Initialize process_table entry */
    process_table[pid].pid = pid;
    process_table[pid].parent_pid = parent_pid;
    process_table[pid].state = PROC_STATE_READY;
    process_table[pid].exit_status = 0;
    process_table[pid].exit_code_valid = 0;
    process_table[pid].page_dir = pml4;
    process_table[pid].proc_pd = pd;
    process_table[pid].user_mem = user_mem;
    process_table[pid].attached_shm_id = -1;
    proc_fd_init(pid);
    if (parent_pid >= 0 && parent_pid < FIRSTOS_MAX_PROCESSES) {
        for (int f = 3; f < FIRSTOS_MAX_FDS; f++) {
            if (process_table[parent_pid].fds[f].used) {
                process_table[pid].fds[f] = process_table[parent_pid].fds[f];
                if (process_table[pid].fds[f].kind == FD_KIND_PIPE_READ) {
                    pipe_add_ref(process_table[pid].fds[f].pipe_idx, 0);
                } else if (process_table[pid].fds[f].kind == FD_KIND_PIPE_WRITE) {
                    pipe_add_ref(process_table[pid].fds[f].pipe_idx, 1);
                }
            }
        }
        for (int f = 0; f < 3; f++) {
            if (process_table[parent_pid].fds[f].used && process_table[parent_pid].fds[f].kind != FD_KIND_CONSOLE) {
                process_table[pid].fds[f] = process_table[parent_pid].fds[f];
                if (process_table[pid].fds[f].kind == FD_KIND_PIPE_READ) {
                    pipe_add_ref(process_table[pid].fds[f].pipe_idx, 0);
                } else if (process_table[pid].fds[f].kind == FD_KIND_PIPE_WRITE) {
                    pipe_add_ref(process_table[pid].fds[f].pipe_idx, 1);
                }
            }
        }
    }
    serial_write(path);
    serial_write(" pid=");
    serial_write_hex((unsigned int)pid);
    serial_write(" parent=");
    serial_write_hex((unsigned int)parent_pid);
    serial_write(" state=READY\n");

    return pid;
}

int process_exec(int pid, const char *path, const char *const *argv) {
    if (pid <= 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (!path || !path[0]) return -FIRSTOS_EINVAL;
    if (!fs_ready()) return -FIRSTOS_ENOENT;

    serial_write("[process] exec path=");
    serial_write(path);
    serial_write(" pid=");
    serial_write_hex((unsigned int)pid);
    serial_write("\n");

    int file_size = fs_read_at(path, 0, 0);
    if (file_size <= 0) {
        serial_write("[process] exec failed errno=-ENOENT\n");
        return -FIRSTOS_ENOENT;
    }
    if ((unsigned int)file_size > FIRSTOS_ELF_MAX_SIZE) {
        return -FIRSTOS_ENOMEM;
    }

    char *file_buf = (char *)kmalloc((unsigned int)file_size);
    if (!file_buf) return -FIRSTOS_ENOMEM;

    int read_bytes = fs_read_raw_at(path, file_buf, (unsigned int)file_size);
    if (read_bytes != file_size) {
        kfree(file_buf);
        return -FIRSTOS_ENOENT;
    }

    userspace_image_t img;
    int val_rc = userspace_validate_elf(file_buf, (unsigned int)file_size, &img);
    if (val_rc != ELF_OK) {
        kfree(file_buf);
        serial_write("[process] exec failed errno=-ENOEXEC\n");
        return -FIRSTOS_ENOEXEC;
    }

    const struct elf64_header *hdr = (const struct elf64_header *)file_buf;

    u64 *new_pml4 = &s_exec_temp_pml4[0];
    u64 *new_pdpt = &s_exec_temp_pdpt[0];
    u64 *new_pd   = &s_exec_temp_pd[0];

    for (int i = 0; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }
    new_pml4[0] = ((u64)(uintptr_t)new_pdpt) | 0x07;

    for (int i = 0; i < 512; i++) {
        new_pdpt[i] = kernel_pdpt[i];
    }
    new_pdpt[0] = ((u64)(uintptr_t)new_pd) | 0x07;

    for (int i = 0; i < 512; i++) {
        new_pd[i] = kernel_pd[0][i];
    }

    char *new_user_mem = (char *)kmalloc_aligned(FIRSTOS_USER_SIZE, 0x200000);
    if (!new_user_mem) {
        kfree(file_buf);
        return -FIRSTOS_ENOMEM;
    }

    u64 *pz = (u64 *)new_user_mem;
    for (int k = 0; k < (int)(FIRSTOS_USER_SIZE / 8); k++) pz[k] = 0;

    new_pd[FIRSTOS_USER_PDE_INDEX] = ((u64)(uintptr_t)new_user_mem) | 0x87;
    new_pd[FIRSTOS_USER_PDE_INDEX + 1] = ((u64)(uintptr_t)(new_user_mem + 0x200000)) | 0x87;

    for (int i = 0; i < hdr->phnum; i++) {
        const struct elf64_phdr *ph = (const struct elf64_phdr *)(file_buf + hdr->phoff + i * hdr->phentsize);
        if (ph->type == 1 && ph->memsz > 0) {
            u64 offset = ph->vaddr - FIRSTOS_USER_BASE;
            char *dest = new_user_mem + offset;
            const char *src = file_buf + ph->offset;
            for (u64 j = 0; j < ph->filesz; j++) dest[j] = src[j];
            for (u64 j = ph->filesz; j < ph->memsz; j++) dest[j] = 0;
        }
    }
    kfree(file_buf);

    /* Setup new argc/argv */
    int argc = 0;
    if (argv) {
        while (argv[argc] && argc < FIRSTOS_ELF_MAX_ARGC) argc++;
    }

    u64 str_offset = FIRSTOS_USER_ARG_BASE - FIRSTOS_USER_BASE;
    u64 total_arg_bytes = 0;
    u64 v_args[FIRSTOS_ELF_MAX_ARGC];

    for (int a = 0; a < argc; a++) {
        const char *arg_str = argv[a] ? argv[a] : "";
        unsigned int len = p_strlen(arg_str);
        if (len >= FIRSTOS_ELF_MAX_ARG_LEN) len = FIRSTOS_ELF_MAX_ARG_LEN - 1;
        if (total_arg_bytes + len + 1 > FIRSTOS_ELF_MAX_TOTAL_ARG_LEN) {
            len = FIRSTOS_ELF_MAX_TOTAL_ARG_LEN - total_arg_bytes - 1;
        }
        char *dest_str = new_user_mem + str_offset;
        for (unsigned int c = 0; c < len; c++) dest_str[c] = arg_str[c];
        dest_str[len] = '\0';
        v_args[a] = FIRSTOS_USER_BASE + str_offset;
        str_offset += len + 1;
        total_arg_bytes += len + 1;
    }

    u64 user_rsp = FIRSTOS_USER_STACK_TOP;
    u64 stack_offset = user_rsp - FIRSTOS_USER_BASE;
    u64 *stack_slots = (u64 *)(new_user_mem + stack_offset);
    stack_slots[0] = (u64)argc;
    for (int a = 0; a < argc; a++) stack_slots[1 + a] = v_args[a];
    stack_slots[1 + argc] = 0;

    /* COMMIT TRANSACTION: Old image is replaced */
    serial_write("[process] old image retained until commit\n");
    serial_write("[process] exec commit pid=");
    serial_write_hex((unsigned int)pid);
    serial_write("\n");

    void *old_user_mem = process_table[pid].user_mem;

    u64 *final_pml4 = &s_proc_pml4[pid][0];
    u64 *final_pdpt = &s_proc_pdpt[pid][0];
    u64 *final_pd   = &s_proc_pd[pid][0];
    for (int i = 0; i < 512; i++) final_pml4[i] = new_pml4[i];
    for (int i = 0; i < 512; i++) final_pdpt[i] = new_pdpt[i];
    for (int i = 0; i < 512; i++) final_pd[i]   = new_pd[i];
    final_pml4[0] = ((u64)(uintptr_t)final_pdpt) | 0x07;
    final_pdpt[0] = ((u64)(uintptr_t)final_pd) | 0x07;

    process_table[pid].user_mem = new_user_mem;
    process_table[pid].page_dir = final_pml4;
    process_table[pid].proc_pd = final_pd;
    tasks[pid].cr3 = (u64)(uintptr_t)final_pml4;
    p_strcpy(tasks[pid].name, path);

    /* Re-forge kernel stack frame for 64-bit Long Mode */
    u64 kstack_top = (u64)(uintptr_t)&task_user_kstacks[pid][4096];
    tasks[pid].rsp0 = kstack_top;
    u64 *sp = (u64 *)(uintptr_t)kstack_top;
    *--sp = 0x1B;           // SS (User Data: 0x18 | 3)
    *--sp = user_rsp;       // RSP
    *--sp = 0x202;          // RFLAGS
    *--sp = 0x23;           // CS (User Code: 0x20 | 3)
    *--sp = hdr->entry;     // RIP
    for (int k = 0; k < 15; k++) *--sp = 0; // 15 general purpose registers
    tasks[pid].rsp = (u64)(uintptr_t)sp;

    /* If executing on current_task, switch CR3 immediately */
    if (pid == current_task) {
        load_page_directory(final_pml4);
    }

    if (old_user_mem) {
        kfree_aligned(old_user_mem);
    }

    return 0;
}

void process_exit(int pid, int exit_status) {
    if (pid <= 0 || pid >= FIRSTOS_MAX_PROCESSES) return;

    serial_write("[userspace] EXIT pid=");
    serial_write_hex((unsigned int)pid);
    serial_write(" status=");
    serial_write_hex((unsigned int)exit_status);
    serial_write("\n");

    tasks[pid].active = 0;
    process_table[pid].state = PROC_STATE_ZOMBIE;
    process_table[pid].exit_status = exit_status;
    process_table[pid].exit_code_valid = 1;

    /* Close all open file descriptors */
    for (int f = 0; f < FIRSTOS_MAX_FDS; f++) {
        proc_fd_close(pid, f);
    }

    /* Detach any active shared memory mapping */
    shm_process_cleanup(pid);

    serial_write("[process] pid=");
    serial_write_hex((unsigned int)pid);
    serial_write(" state=ZOMBIE\n");
}

void process_reap(int pid) {
    if (pid <= 0 || pid >= FIRSTOS_MAX_PROCESSES) return;

    shm_process_cleanup(pid);

    if (process_table[pid].user_mem) {
        kfree_aligned(process_table[pid].user_mem);
        process_table[pid].user_mem = NULL;
    }
    process_table[pid].page_dir = NULL;
    process_table[pid].proc_pd = NULL;
    process_table[pid].state = PROC_STATE_REAPED;
    process_table[pid].state = PROC_STATE_UNUSED;
    process_table[pid].parent_pid = -1;
    process_table[pid].exit_code_valid = 0;

    serial_write("[process] resources freed pid=");
    serial_write_hex((unsigned int)pid);
    serial_write("\n");
}

int process_waitpid(int caller_pid, int target_pid, int *out_status, int options) {
    (void)options;
    if (target_pid > 0 && (target_pid >= FIRSTOS_MAX_PROCESSES || process_table[target_pid].parent_pid != caller_pid)) {
        return -FIRSTOS_ECHILD;
    }

    int child = -1;
    if (target_pid > 0) {
        child = target_pid;
    } else if (target_pid == -1) {
        /* Find any child of caller */
        int found_any = 0;
        for (int i = 1; i < FIRSTOS_MAX_PROCESSES; i++) {
            if (process_table[i].parent_pid == caller_pid) {
                found_any = 1;
                if (process_table[i].state == PROC_STATE_ZOMBIE) {
                    child = i;
                    break;
                }
            }
        }
        if (!found_any) return -FIRSTOS_ECHILD;
    } else {
        return -FIRSTOS_EINVAL;
    }

    /* Cooperative bounded polling for child to exit */
    int max_polls = 1000;
    while (max_polls-- > 0) {
        if (child > 0 && process_table[child].state == PROC_STATE_ZOMBIE) {
            int st = process_table[child].exit_status;
            if (out_status) {
                *out_status = st;
            }
            serial_write("[process] waitpid parent=");
            serial_write_hex((unsigned int)caller_pid);
            serial_write(" child=");
            serial_write_hex((unsigned int)child);
            serial_write(" status=");
            serial_write_hex((unsigned int)st);
            serial_write("\n");

            process_reap(child);
            return child;
        }

        /* Yield to let child run */
        __asm__ volatile("int $0x81");

        /* If waiting for any child, re-scan for newly zombie children */
        if (target_pid == -1) {
            for (int i = 1; i < FIRSTOS_MAX_PROCESSES; i++) {
                if (process_table[i].parent_pid == caller_pid && process_table[i].state == PROC_STATE_ZOMBIE) {
                    child = i;
                    break;
                }
            }
        }
    }

    return -FIRSTOS_EAGAIN;
}
