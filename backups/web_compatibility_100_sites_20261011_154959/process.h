#ifndef _FIRSTOS_PROCESS_H
#define _FIRSTOS_PROCESS_H

#include "task.h"
#include "vfs_fd.h"
#include "include/firstos/uapi/syscall.h"

#define FIRSTOS_MAX_PROCESSES  MAX_TASKS /* 16 */
#define FIRSTOS_MAX_FDS        32

typedef enum {
    PROC_STATE_UNUSED = 0,
    PROC_STATE_NEW,
    PROC_STATE_READY,
    PROC_STATE_RUNNING,
    PROC_STATE_BLOCKED,
    PROC_STATE_EXITED,
    PROC_STATE_ZOMBIE,
    PROC_STATE_REAPED
} process_state_t;

typedef enum {
    FD_KIND_NONE = 0,
    FD_KIND_CONSOLE,   /* 0: stdin, 1: stdout, 2: stderr */
    FD_KIND_FILE,      /* backed by vfs_fd */
    FD_KIND_PIPE_READ, /* read endpoint of kernel pipe */
    FD_KIND_PIPE_WRITE, /* write endpoint of kernel pipe */
    FD_KIND_SOCKET     /* backed by net_socket */
} fd_kind_t;

#define FIRSTOS_MAX_PROC_SHM 4

typedef struct {
    int used;
    int flags;
    fd_kind_t kind;
    union {
        int vfs_fd;        /* index into s_vfs_fds if FD_KIND_FILE */
        int pipe_idx;      /* index into pipe_table if PIPE_READ or PIPE_WRITE */
        void *socket_ptr;  /* pointer to net_socket_t if FD_KIND_SOCKET */
        int target_idx;
    };
} process_fd_t;

typedef enum {
    PROC_ROLE_SYSTEM = 0,
    PROC_ROLE_BROWSER_HOST,
    PROC_ROLE_RENDERER,
    PROC_ROLE_NETWORK,
    PROC_ROLE_STORAGE
} process_role_t;

#define PROC_CAP_FILE_OPEN      (1 << 0)
#define PROC_CAP_NETWORK_DIRECT (1 << 1)
#define PROC_CAP_SHM_ATTACH     (1 << 2)
#define PROC_CAP_SPAWN          (1 << 3)
#define PROC_CAP_ALL            (0xFFFFFFFF)

typedef struct {
    int pid;           /* 0 = Kernel, 1..15 = User processes */
    int parent_pid;    /* PPID */
    process_state_t state;
    process_role_t  role;
    u32             capabilities;
    int exit_status;
    int exit_code_valid;
    u64 *page_dir;            /* PML4 page table pointer for memory reclamation */
    u64 *proc_pd;             /* Level 2 Page Directory for user/shm huge pages */
    char *user_mem;           /* 4MB physical block pointer for reclamation */
    int attached_shm_id;      /* Currently attached shared memory region index (-1 if none) */
    int attached_shm_ids[FIRSTOS_MAX_PROC_SHM]; /* Multi-region SHM attachments */
    process_fd_t fds[FIRSTOS_MAX_FDS];
} process_info_t;

extern process_info_t process_table[FIRSTOS_MAX_PROCESSES];

void process_set_role(int pid, process_role_t role);
process_role_t process_get_role(int pid);
int  process_has_capability(int pid, u32 cap);

void process_subsystem_init(void);
int  process_spawn(int parent_pid, const char *path, const char *const *argv);
int  process_exec(int pid, const char *path, const char *const *argv);
int  process_waitpid(int caller_pid, int target_pid, int *out_status, int options);
void process_exit(int pid, int exit_status);
void process_reap(int pid);

/* Per-process FD operations */
int  proc_fd_init(int pid);
int  proc_fd_close(int pid, int fd);
int  proc_fd_read(int pid, int fd, void *buf, unsigned int count);
int  proc_fd_write(int pid, int fd, const void *buf, unsigned int count);
int  proc_fd_dup(int pid, int oldfd);
int  proc_fd_dup2(int pid, int oldfd, int newfd);
int  proc_pipe_create(int pid, int out_pipefd[2]);
int  proc_fd_open_socket(int pid, void *sock_ptr);

#endif /* _FIRSTOS_PROCESS_H */
