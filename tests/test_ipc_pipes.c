#include <assert.h>
#include <stdint.h>

int printf(const char *format, ...);
void *memset(void *s, int c, unsigned long n);

#define FIRSTOS_HOST_TEST 1
#include "include/firstos/uapi/syscall.h"
#include "pipe.h"

/* Serial dummy for host test */
void serial_write(const char *s) { (void)s; }

/* Mock process table for FD testing */
#define FIRSTOS_MAX_PROCESSES 16
#define FIRSTOS_MAX_FDS       32

typedef enum {
    FD_KIND_NONE = 0,
    FD_KIND_CONSOLE,
    FD_KIND_FILE,
    FD_KIND_PIPE_READ,
    FD_KIND_PIPE_WRITE
} fd_kind_t;

typedef struct {
    int used;
    int flags;
    fd_kind_t kind;
    union {
        int vfs_fd;
        int pipe_idx;
        int target_idx;
    };
} mock_proc_fd_t;

typedef struct {
    int pid;
    mock_proc_fd_t fds[FIRSTOS_MAX_FDS];
} mock_process_t;

static mock_process_t mock_procs[FIRSTOS_MAX_PROCESSES];

static void mock_init(void) {
    pipe_subsystem_init();
    memset(mock_procs, 0, sizeof(mock_procs));
    for (int p = 0; p < FIRSTOS_MAX_PROCESSES; p++) {
        mock_procs[p].pid = p;
        mock_procs[p].fds[0].used = 1; mock_procs[p].fds[0].kind = FD_KIND_CONSOLE;
        mock_procs[p].fds[1].used = 1; mock_procs[p].fds[1].kind = FD_KIND_CONSOLE;
        mock_procs[p].fds[2].used = 1; mock_procs[p].fds[2].kind = FD_KIND_CONSOLE;
    }
}

static int mock_proc_fd_close(int pid, int fd) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (fd < 0 || fd >= FIRSTOS_MAX_FDS) return -FIRSTOS_EBADF;
    mock_proc_fd_t *e = &mock_procs[pid].fds[fd];
    if (!e->used) return -FIRSTOS_EBADF;

    if (e->kind == FD_KIND_PIPE_READ) {
        pipe_close_endpoint(e->pipe_idx, 0);
    } else if (e->kind == FD_KIND_PIPE_WRITE) {
        pipe_close_endpoint(e->pipe_idx, 1);
    }
    e->used = 0;
    e->kind = FD_KIND_NONE;
    e->target_idx = -1;
    return 0;
}

static int mock_pipe_create(int pid, int out_fds[2]) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (!out_fds) return -FIRSTOS_EFAULT;

    int rfd = -1, wfd = -1;
    for (int f = 0; f < FIRSTOS_MAX_FDS; f++) {
        if (!mock_procs[pid].fds[f].used) {
            if (rfd == -1) rfd = f;
            else if (wfd == -1) { wfd = f; break; }
        }
    }
    if (rfd == -1 || wfd == -1) return -FIRSTOS_EMFILE;

    int pidx = -1;
    int rc = pipe_create(&pidx);
    if (rc != 0) return rc;

    mock_procs[pid].fds[rfd].used = 1;
    mock_procs[pid].fds[rfd].flags = 0;
    mock_procs[pid].fds[rfd].kind = FD_KIND_PIPE_READ;
    mock_procs[pid].fds[rfd].pipe_idx = pidx;

    mock_procs[pid].fds[wfd].used = 1;
    mock_procs[pid].fds[wfd].flags = 1;
    mock_procs[pid].fds[wfd].kind = FD_KIND_PIPE_WRITE;
    mock_procs[pid].fds[wfd].pipe_idx = pidx;

    out_fds[0] = rfd;
    out_fds[1] = wfd;
    return 0;
}

static int mock_proc_fd_dup(int pid, int oldfd) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (oldfd < 0 || oldfd >= FIRSTOS_MAX_FDS) return -FIRSTOS_EBADF;
    mock_proc_fd_t *src = &mock_procs[pid].fds[oldfd];
    if (!src->used) return -FIRSTOS_EBADF;

    int newfd = -1;
    for (int f = 0; f < FIRSTOS_MAX_FDS; f++) {
        if (!mock_procs[pid].fds[f].used) {
            newfd = f;
            break;
        }
    }
    if (newfd == -1) return -FIRSTOS_EMFILE;

    mock_procs[pid].fds[newfd] = *src;
    if (src->kind == FD_KIND_PIPE_READ) {
        pipe_add_ref(src->pipe_idx, 0);
    } else if (src->kind == FD_KIND_PIPE_WRITE) {
        pipe_add_ref(src->pipe_idx, 1);
    }
    return newfd;
}

static int mock_proc_fd_dup2(int pid, int oldfd, int newfd) {
    if (pid < 0 || pid >= FIRSTOS_MAX_PROCESSES) return -FIRSTOS_EINVAL;
    if (oldfd < 0 || oldfd >= FIRSTOS_MAX_FDS) return -FIRSTOS_EBADF;
    if (newfd < 0 || newfd >= FIRSTOS_MAX_FDS) return -FIRSTOS_EBADF;

    mock_proc_fd_t *src = &mock_procs[pid].fds[oldfd];
    if (!src->used) return -FIRSTOS_EBADF;

    if (oldfd == newfd) return newfd;

    if (mock_procs[pid].fds[newfd].used) {
        mock_proc_fd_close(pid, newfd);
    }

    mock_procs[pid].fds[newfd] = *src;
    if (src->kind == FD_KIND_PIPE_READ) {
        pipe_add_ref(src->pipe_idx, 0);
    } else if (src->kind == FD_KIND_PIPE_WRITE) {
        pipe_add_ref(src->pipe_idx, 1);
    }
    return newfd;
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 3B: Inter-Process Communication & Pipes Tests\n");
    printf("============================================================\n");

    /* TEST 1 */
    printf("[TEST 1] Testing pipe creation and initialization...\n");
    mock_init();
    int pipe_idx = -1;
    int rc = pipe_create(&pipe_idx);
    assert(rc == 0);
    assert(pipe_idx >= 0 && pipe_idx < FIRSTOS_MAX_PIPES);
    assert(pipe_table[pipe_idx].used == 1);
    assert(pipe_table[pipe_idx].readers == 1);
    assert(pipe_table[pipe_idx].writers == 1);
    assert(pipe_table[pipe_idx].count == 0);
    printf("  -> Pipe %d created successfully (readers=1, writers=1).\n", pipe_idx);

    /* TEST 2 */
    printf("[TEST 2] Testing FIFO read/write correctness...\n");
    const char *msg = "Hello FirstOS IPC Pipe!";
    int wlen = pipe_write(pipe_idx, msg, 23);
    assert(wlen == 23);
    assert(pipe_table[pipe_idx].count == 23);

    char rbuf[64] = {0};
    int rlen = pipe_read(pipe_idx, rbuf, sizeof(rbuf));
    assert(rlen == 23);
    assert(pipe_table[pipe_idx].count == 0);
    for (int i = 0; i < 23; i++) {
        assert(rbuf[i] == msg[i]);
    }
    printf("  -> FIFO read/write matched 23 bytes exactly: '%s'.\n", rbuf);

    /* TEST 3 */
    printf("[TEST 3] Testing multi-chunk write and read...\n");
    assert(pipe_write(pipe_idx, "AAA", 3) == 3);
    assert(pipe_write(pipe_idx, "BBBB", 4) == 4);
    assert(pipe_write(pipe_idx, "CCCCC", 5) == 5);
    assert(pipe_table[pipe_idx].count == 12);

    char chunk[7] = {0};
    assert(pipe_read(pipe_idx, chunk, 6) == 6);
    chunk[6] = '\0';
    assert(chunk[0] == 'A' && chunk[1] == 'A' && chunk[2] == 'A' && chunk[3] == 'B' && chunk[4] == 'B' && chunk[5] == 'B');
    assert(pipe_read(pipe_idx, chunk, 6) == 6);
    assert(chunk[0] == 'B' && chunk[1] == 'C' && chunk[2] == 'C' && chunk[3] == 'C' && chunk[4] == 'C' && chunk[5] == 'C');
    assert(pipe_table[pipe_idx].count == 0);
    printf("  -> Chunked reading across writes verified.\n");

    /* TEST 4 */
    printf("[TEST 4] Testing circular ring buffer wraparound...\n");
    /* Write and drain 3000 bytes, then write 2000 bytes to force head/tail past 4096 */
    char pattern[1024];
    for (int i = 0; i < 1024; i++) pattern[i] = (char)('a' + (i % 26));

    for (int round = 0; round < 6; round++) {
        assert(pipe_write(pipe_idx, pattern, 1024) == 1024);
        char drain[1024];
        assert(pipe_read(pipe_idx, drain, 1024) == 1024);
        for (int i = 0; i < 1024; i++) {
            assert(drain[i] == pattern[i]);
        }
    }
    printf("  -> 6,144 bytes written/read across circular boundary cleanly.\n");

    /* TEST 5 */
    printf("[TEST 5] Testing EOF behavior when write end closes...\n");
    assert(pipe_write(pipe_idx, "final_data", 10) == 10);
    pipe_close_endpoint(pipe_idx, 1); /* close writer */
    assert(pipe_table[pipe_idx].writers == 0);

    char eof_buf[16] = {0};
    assert(pipe_read(pipe_idx, eof_buf, 10) == 10);
    /* Next read with buffer empty and writers==0 must return 0 (EOF) */
    assert(pipe_read(pipe_idx, eof_buf, 10) == 0);
    printf("  -> Reading from empty pipe with closed writer returned 0 (EOF).\n");

    /* TEST 6 */
    printf("[TEST 6] Testing EPIPE behavior when read end closes...\n");
    /* Close reader on previous pipe -> fully freed */
    pipe_close_endpoint(pipe_idx, 0);
    assert(pipe_table[pipe_idx].used == 0);

    /* Allocate new pipe */
    assert(pipe_create(&pipe_idx) == 0);
    /* Close reader immediately */
    pipe_close_endpoint(pipe_idx, 0);
    assert(pipe_table[pipe_idx].readers == 0);

    /* Write to pipe with no readers must return -FIRSTOS_EPIPE (-32) */
    int wrc = pipe_write(pipe_idx, "orphan", 6);
    assert(wrc == -FIRSTOS_EPIPE);
    printf("  -> Writing to pipe with readers=0 correctly returned -EPIPE (%d).\n", wrc);
    pipe_close_endpoint(pipe_idx, 1);
    assert(pipe_table[pipe_idx].used == 0);

    /* TEST 7 */
    printf("[TEST 7] Testing proc_pipe_create descriptor allocation...\n");
    int p_fds[2] = {-1, -1};
    assert(mock_pipe_create(1, p_fds) == 0);
    assert(p_fds[0] == 3); /* next free after 0, 1, 2 */
    assert(p_fds[1] == 4);
    assert(mock_procs[1].fds[3].kind == FD_KIND_PIPE_READ);
    assert(mock_procs[1].fds[4].kind == FD_KIND_PIPE_WRITE);
    int p_idx7 = mock_procs[1].fds[3].pipe_idx;
    assert(pipe_table[p_idx7].readers == 1);
    assert(pipe_table[p_idx7].writers == 1);
    printf("  -> Descriptors allocated: read_fd=%d, write_fd=%d linked to pipe %d.\n", p_fds[0], p_fds[1], p_idx7);

    /* TEST 8 */
    printf("[TEST 8] Testing dup() descriptor duplication & refcounting...\n");
    int dup_fd = mock_proc_fd_dup(1, p_fds[1]); /* duplicate write end */
    assert(dup_fd == 5);
    assert(mock_procs[1].fds[dup_fd].kind == FD_KIND_PIPE_WRITE);
    assert(pipe_table[p_idx7].writers == 2);

    /* Close original write_fd */
    assert(mock_proc_fd_close(1, p_fds[1]) == 0);
    assert(pipe_table[p_idx7].writers == 1); /* Still 1 writer remaining via dup_fd */

    /* Close duplicate write_fd */
    assert(mock_proc_fd_close(1, dup_fd) == 0);
    assert(pipe_table[p_idx7].writers == 0);
    printf("  -> dup() reference counting verified across multiple closures.\n");

    /* TEST 9 */
    printf("[TEST 9] Testing dup2() redirection...\n");
    /* dup2 read_fd (fd 3) to stdin (fd 0) */
    assert(mock_proc_fd_dup2(1, p_fds[0], 0) == 0);
    assert(mock_procs[1].fds[0].kind == FD_KIND_PIPE_READ);
    assert(pipe_table[p_idx7].readers == 2);

    /* dup2 to itself is a no-op returning target fd */
    assert(mock_proc_fd_dup2(1, 0, 0) == 0);
    assert(pipe_table[p_idx7].readers == 2);

    /* Close both read descriptors */
    assert(mock_proc_fd_close(1, p_fds[0]) == 0);
    assert(pipe_table[p_idx7].readers == 1);
    assert(mock_proc_fd_close(1, 0) == 0);
    assert(pipe_table[p_idx7].readers == 0);
    assert(pipe_table[p_idx7].used == 0); /* Reclaimed */
    printf("  -> dup2() successfully redirected fd 3 to fd 0 and cleanly reaped on close.\n");

    /* TEST 10 */
    printf("[TEST 10] Testing pipe capacity exhaustion...\n");
    int allocated_pipes[FIRSTOS_MAX_PIPES];
    for (int i = 0; i < FIRSTOS_MAX_PIPES; i++) {
        assert(pipe_create(&allocated_pipes[i]) == 0);
    }
    int overflow_idx = -1;
    assert(pipe_create(&overflow_idx) == -FIRSTOS_EMFILE);

    /* Free one and re-allocate */
    pipe_close_endpoint(allocated_pipes[0], 0);
    pipe_close_endpoint(allocated_pipes[0], 1);
    assert(pipe_create(&overflow_idx) == 0);
    assert(overflow_idx == allocated_pipes[0]);

    for (int i = 1; i < FIRSTOS_MAX_PIPES; i++) {
        pipe_close_endpoint(allocated_pipes[i], 0);
        pipe_close_endpoint(allocated_pipes[i], 1);
    }
    pipe_close_endpoint(overflow_idx, 0);
    pipe_close_endpoint(overflow_idx, 1);
    printf("  -> 16 pipe limit enforced with -EMFILE and slots recycled.\n");

    printf("============================================================\n");
    printf("All 10 IPC Pipe & FD Redirection Host Tests PASSED successfully!\n");
    printf("============================================================\n");
    return 0;
}
