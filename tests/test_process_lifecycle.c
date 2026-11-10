#include <assert.h>
#include <stdint.h>

int printf(const char *format, ...);
void *memset(void *s, int c, unsigned long n);

#include "include/firstos/uapi/syscall.h"

#define MAX_TASKS 16
#define FIRSTOS_MAX_PROCESSES  MAX_TASKS
#define FIRSTOS_MAX_FDS        32

typedef enum {
    PROC_STATE_UNUSED = 0,
    PROC_STATE_NEW,
    PROC_STATE_READY,
    PROC_STATE_RUNNING,
    PROC_STATE_ZOMBIE,
    PROC_STATE_REAPED
} process_state_t;

typedef enum {
    FD_KIND_NONE = 0,
    FD_KIND_CONSOLE,
    FD_KIND_FILE,
    FD_KIND_PIPE_PLACEHOLDER
} fd_kind_t;

typedef struct {
    int used;
    int flags;
    fd_kind_t kind;
    int vfs_fd;
} process_fd_t;

typedef struct {
    int pid;
    int parent_pid;
    process_state_t state;
    int exit_status;
    int exit_code_valid;
    void *page_dir;
    void *user_mem;
    process_fd_t fds[FIRSTOS_MAX_FDS];
} mock_process_t;

static mock_process_t mock_table[FIRSTOS_MAX_PROCESSES];

static void mock_proc_init(void) {
    memset(mock_table, 0, sizeof(mock_table));
    for (int i = 0; i < FIRSTOS_MAX_PROCESSES; i++) {
        mock_table[i].pid = i;
        mock_table[i].parent_pid = -1;
        mock_table[i].state = PROC_STATE_UNUSED;
    }
    mock_table[0].pid = 0;
    mock_table[0].parent_pid = 0;
    mock_table[0].state = PROC_STATE_RUNNING;
    mock_table[0].fds[0].used = 1; mock_table[0].fds[0].kind = FD_KIND_CONSOLE;
    mock_table[0].fds[1].used = 1; mock_table[0].fds[1].kind = FD_KIND_CONSOLE;
    mock_table[0].fds[2].used = 1; mock_table[0].fds[2].kind = FD_KIND_CONSOLE;
}

static int mock_proc_fd_init(int pid) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    for (int f = 0; f < FIRSTOS_MAX_FDS; f++) {
        mock_table[pid].fds[f].used = 0;
        mock_table[pid].fds[f].kind = FD_KIND_NONE;
    }
    mock_table[pid].fds[0].used = 1; mock_table[pid].fds[0].kind = FD_KIND_CONSOLE;
    mock_table[pid].fds[1].used = 1; mock_table[pid].fds[1].kind = FD_KIND_CONSOLE;
    mock_table[pid].fds[2].used = 1; mock_table[pid].fds[2].kind = FD_KIND_CONSOLE;
    return 0;
}

static int mock_proc_fd_close(int pid, int fd) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (fd < 0 || fd >= FIRSTOS_MAX_FDS) return -FIRSTOS_EBADF;
    if (!mock_table[pid].fds[fd].used) return -FIRSTOS_EBADF;
    mock_table[pid].fds[fd].used = 0;
    mock_table[pid].fds[fd].kind = FD_KIND_NONE;
    return 0;
}

static int mock_proc_spawn(int parent, const char *path) {
    if (!path || !path[0]) return -FIRSTOS_EINVAL;
    int slot = -1;
    for (int i = 1; i < FIRSTOS_MAX_PROCESSES; i++) {
        if (mock_table[i].state == PROC_STATE_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -FIRSTOS_EAGAIN;
    mock_table[slot].state = PROC_STATE_READY;
    mock_table[slot].parent_pid = parent;
    mock_table[slot].exit_status = 0;
    mock_table[slot].exit_code_valid = 0;
    mock_proc_fd_init(slot);
    return slot;
}

static void mock_proc_exit(int pid, int status) {
    if (pid <= 0 || pid >= FIRSTOS_MAX_PROCESSES) return;
    mock_table[pid].state = PROC_STATE_ZOMBIE;
    mock_table[pid].exit_status = status;
    mock_table[pid].exit_code_valid = 1;
    for (int f = 0; f < FIRSTOS_MAX_FDS; f++) {
        mock_proc_fd_close(pid, f);
    }
}

static int mock_proc_waitpid(int caller, int target, int *out_status) {
    if (target <= 0 || target >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_ECHILD;
    if (mock_table[target].parent_pid != caller) return -FIRSTOS_ECHILD;
    if (mock_table[target].state != PROC_STATE_ZOMBIE) return -FIRSTOS_EAGAIN;

    if (out_status) *out_status = mock_table[target].exit_status;
    /* Reap */
    mock_table[target].state = PROC_STATE_UNUSED;
    mock_table[target].parent_pid = -1;
    mock_table[target].exit_code_valid = 0;
    return target;
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 3A: Process Lifecycle & FD Host Tests\n");
    printf("============================================================\n");

    /* TEST 1: Subsystem Initialization */
    printf("[TEST 1] Testing process subsystem initialization...\n");
    mock_proc_init();
    assert(mock_table[0].pid == 0);
    assert(mock_table[0].state == PROC_STATE_RUNNING);
    assert(mock_table[0].fds[0].used == 1);
    assert(mock_table[0].fds[1].used == 1);
    assert(mock_table[0].fds[2].used == 1);
    printf("  -> Kernel process 0 and standard descriptors initialized.\n");

    /* TEST 2: Process Spawn */
    printf("[TEST 2] Testing process spawn and slot allocation...\n");
    int pid1 = mock_proc_spawn(0, "/userspace_hello.elf");
    assert(pid1 == 1);
    assert(mock_table[pid1].state == PROC_STATE_READY);
    assert(mock_table[pid1].parent_pid == 0);
    assert(mock_table[pid1].fds[0].used == 1);
    assert(mock_table[pid1].fds[1].used == 1);
    assert(mock_table[pid1].fds[2].used == 1);

    int pid2 = mock_proc_spawn(pid1, "/userspace_child.elf");
    assert(pid2 == 2);
    assert(mock_table[pid2].parent_pid == pid1);
    printf("  -> Process 1 and Child Process 2 spawned with correct PPID.\n");

    /* TEST 3: File Descriptor Isolation & Protection */
    printf("[TEST 3] Testing per-process file descriptor operations...\n");
    assert(mock_proc_fd_close(pid2, 2) == 0);
    assert(mock_table[pid2].fds[2].used == 0);
    assert(mock_table[pid1].fds[2].used == 1); /* PID 1 unaffected */
    assert(mock_proc_fd_close(pid2, 2) == -FIRSTOS_EBADF); /* Close twice rejected */
    assert(mock_proc_fd_close(pid2, 99) == -FIRSTOS_EBADF); /* Out of bounds rejected */
    printf("  -> Per-process FD table isolation and double-close protection verified.\n");

    /* TEST 4: Non-parent waitpid rejection */
    printf("[TEST 4] Testing unauthorized waitpid rejection...\n");
    int status = 0;
    assert(mock_proc_waitpid(0, pid2, &status) == -FIRSTOS_ECHILD);
    printf("  -> Process 0 cannot wait on Process 2 (PPID mismatch -> -ECHILD).\n");

    /* TEST 5: Child Exit & Zombie Transition */
    printf("[TEST 5] Testing child exit status and zombie transition...\n");
    mock_proc_exit(pid2, 42);
    assert(mock_table[pid2].state == PROC_STATE_ZOMBIE);
    assert(mock_table[pid2].exit_status == 42);
    assert(mock_table[pid2].fds[0].used == 0); /* Auto-closed on exit */
    assert(mock_table[pid2].fds[1].used == 0);
    printf("  -> Child transitioned to ZOMBIE with status 42 and closed all FDs.\n");

    /* TEST 6: Parent waitpid & Reaping */
    printf("[TEST 6] Testing parent waitpid and resource reaping...\n");
    int reaped = mock_proc_waitpid(pid1, pid2, &status);
    assert(reaped == pid2);
    assert(status == 42);
    assert(mock_table[pid2].state == PROC_STATE_UNUSED);
    printf("  -> Parent successfully reaped child, received status 42, slot reset to UNUSED.\n");

    /* TEST 7: Double Reap Prevention */
    printf("[TEST 7] Testing double reap prevention...\n");
    assert(mock_proc_waitpid(pid1, pid2, &status) == -FIRSTOS_ECHILD);
    printf("  -> Subsequent waitpid on reaped child returned -ECHILD.\n");

    /* TEST 8: Slot Reuse */
    printf("[TEST 8] Testing process slot reuse...\n");
    int pid_reuse = mock_proc_spawn(pid1, "/new_child.elf");
    assert(pid_reuse == 2);
    assert(mock_table[pid_reuse].state == PROC_STATE_READY);
    printf("  -> Reaped slot 2 successfully reused by new spawn.\n");

    /* TEST 9: Exhaustion Handling */
    printf("[TEST 9] Testing process table capacity exhaustion...\n");
    for (int i = 3; i < FIRSTOS_MAX_PROCESSES; i++) {
        int p = mock_proc_spawn(pid1, "/filler.elf");
        assert(p == i);
    }
    int oom_pid = mock_proc_spawn(pid1, "/too_many.elf");
    assert(oom_pid == -FIRSTOS_EAGAIN);
    printf("  -> Process table capacity limit (16) strictly enforced with -EAGAIN.\n");

    printf("============================================================\n");
    printf("All 9 Process Lifecycle & FD Host Tests PASSED successfully!\n");
    printf("============================================================\n");
    return 0;
}
