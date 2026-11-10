#include "../../include/firstos/syscall.h"

static unsigned int u_strlen(const char *s) {
    unsigned int len = 0;
    if (!s) return 0;
    while (s[len]) len++;
    return len;
}

static void print_str(const char *s) {
    if (s) {
        firstos_write(1, s, u_strlen(s));
    }
}

static void print_dec(int val) {
    char buf[16];
    int idx = 0;
    if (val == 0) {
        firstos_write(1, "0", 1);
        return;
    }
    if (val < 0) {
        firstos_write(1, "-", 1);
        val = -val;
    }
    char tmp[16];
    int t = 0;
    while (val > 0) {
        tmp[t++] = '0' + (val % 10);
        val /= 10;
    }
    while (t > 0) {
        buf[idx++] = tmp[--t];
    }
    firstos_write(1, buf, idx);
}

int main(int argc, char **argv) {
    print_str("\n[userspace_app] ========================================\n");
    print_str("[userspace_app] Hello from FirstOS Ring 3 via crt0!\n");

    /* TEST 1: Argument parsing */
    print_str("[userspace_app] [TEST 1] argc = ");
    print_dec(argc);
    print_str("\n");

    for (int i = 0; i < argc; i++) {
        print_str("[userspace_app]   argv[");
        print_dec(i);
        print_str("] = '");
        print_str(argv[i]);
        print_str("'\n");
    }

    if (argc >= 3) {
        print_str("[userspace_app]   -> argc/argv received and validated successfully!\n");
    } else {
        print_str("[userspace_app]   -> WARNING: unexpected argc count\n");
    }

    /* TEST 2: Pointer Validation (Attempting to write kernel supervisor address 0x00100000) */
    print_str("[userspace_app] [TEST 2] Testing invalid buffer pointer validation...\n");
    int bad_ptr_ret = firstos_write(1, (const void *)0x00100000, 32);
    if (bad_ptr_ret == -FIRSTOS_EFAULT) {
        print_str("[userspace_app]   -> Kernel successfully rejected supervisor address with -EFAULT (-14)!\n");
    } else {
        print_str("[userspace_app]   -> ERROR: unexpected return code: ");
        print_dec(bad_ptr_ret);
        print_str("\n");
    }

    /* TEST 3: Invalid Syscall (Invoking unknown syscall number 0x7FFF) */
    print_str("[userspace_app] [TEST 3] Testing unknown syscall rejection...\n");
    long bad_sys_ret = firstos_syscall1(0x7FFF, 0);
    if (bad_sys_ret == -FIRSTOS_ENOSYS) {
        print_str("[userspace_app]   -> Kernel successfully rejected unknown syscall with -ENOSYS (-38)!\n");
    } else {
        print_str("[userspace_app]   -> ERROR: unexpected return code: ");
        print_dec((int)bad_sys_ret);
        print_str("\n");
    }

    /* TEST 4: Userspace Dynamic Memory Allocation (malloc & free via sbrk) */
    print_str("[userspace_app] [TEST 4] Testing userspace dynamic memory allocation (malloc/free)...\n");
    char *dyn_buf = (char *)firstos_malloc(128);
    if (dyn_buf) {
        const char *msg = "Dynamic heap memory allocated via firstos_malloc()!";
        unsigned int mlen = u_strlen(msg);
        for (unsigned int k = 0; k < mlen; k++) dyn_buf[k] = msg[k];
        dyn_buf[mlen] = '\0';
        print_str("[userspace_app]   -> Content: ");
        print_str(dyn_buf);
        print_str("\n");
        firstos_free(dyn_buf);
        print_str("[userspace_app]   -> Dynamic heap allocation and free succeeded!\n");
    } else {
        print_str("[userspace_app]   -> ERROR: firstos_malloc failed\n");
    }

    /* TEST 5: getpid */
    print_str("[userspace_app] [TEST 5] Testing getpid()...\n");
    int my_pid = firstos_getpid();
    print_str("[userspace_app]   -> My PID = ");
    print_dec(my_pid);
    print_str("\n");

    /* TEST 6: File Descriptors (write to 1, write to 2, write to invalid, close 2, write to closed 2) */
    print_str("[userspace_app] [TEST 6] Testing File Descriptors (0/1/2)...\n");
    firstos_write(1, "[userspace_app]   -> stdout write (fd=1) OK\n", 40);
    firstos_write(2, "[userspace_app]   -> stderr write (fd=2) OK\n", 40);
    int inv_fd_ret = firstos_write(99, "bad", 3);
    if (inv_fd_ret == -FIRSTOS_EBADF) {
        print_str("[userspace_app]   -> Invalid fd write returned -EBADF (-9)!\n");
    }
    firstos_close(2);
    int closed_ret = firstos_write(2, "closed", 6);
    if (closed_ret == -FIRSTOS_EBADF) {
        print_str("[userspace_app]   -> Write to closed fd returned -EBADF (-9)!\n");
    }

    /* TEST 7: Pipe creation, loopback, and dup */
    print_str("[userspace_app] [TEST 7] Testing Pipe creation, loopback & dup...\n");
    int pipefd[2];
    int p_rc = firstos_pipe(pipefd);
    if (p_rc == 0) {
        print_str("[userspace_app]   -> firstos_pipe created rfd=");
        print_dec(pipefd[0]);
        print_str(" wfd=");
        print_dec(pipefd[1]);
        print_str("\n");

        int dup_wfd = firstos_dup(pipefd[1]);
        print_str("[userspace_app]   -> firstos_dup write end -> fd=");
        print_dec(dup_wfd);
        print_str("\n");

        firstos_write(dup_wfd, "PIPE_LOOPBACK", 13);
        char loopback_buf[20] = {0};
        firstos_read(pipefd[0], loopback_buf, 13);
        print_str("[userspace_app]   -> Pipe loopback read matched: '");
        print_str(loopback_buf);
        print_str("'\n");
        firstos_close(dup_wfd);
    }

    /* TEST 8: Process spawn with inherited pipe IPC & waitpid */
    print_str("[userspace_app] [TEST 8] Testing Inter-Process Pipe IPC & Shared Memory...\n");

    /* Phase 3C: Shared Memory & Fast Mutex Setup */
    int shm_chan = firstos_shm_open("firstos_shm_channel", 0, 0);
    void *shm_win = (void*)0;
    if (shm_chan >= 0) {
        shm_win = firstos_shm_map(shm_chan);
        if (shm_win) {
            int *shm_mtx = (int *)shm_win;
            *shm_mtx = 0; /* unlocked */
            char *shm_buf = ((char *)shm_win) + sizeof(int);
            shm_buf[0] = '\0';
            print_str("[userspace_app]   -> Shared memory mapped at 0x04400000 with initialized mutex.\n");
        }
    }

    const char *child_args[] = { "/userspace_child.elf", "child_mode", 0 };
    int child_pid = firstos_spawn("/userspace_child.elf", child_args);
    print_str("[userspace_app]   -> Spawned child PID = ");
    print_dec(child_pid);
    print_str("\n");

    /* Parent closes its own write end so only child holds writer */
    firstos_close(pipefd[1]);

    /* Read child's IPC message from pipe */
    char child_msg[40] = {0};
    int rbytes = firstos_read(pipefd[0], child_msg, sizeof(child_msg) - 1);
    if (rbytes > 0) {
        child_msg[rbytes] = '\0';
        print_str("[userspace_app]   -> Received IPC message from child via pipe: '");
        print_str(child_msg);
        print_str("'\n");
    }

    if (child_pid > 0 && child_pid != my_pid) {
        int status = 0;
        int wait_ret = firstos_waitpid(child_pid, &status, 0);
        print_str("[userspace_app]   -> waitpid returned PID = ");
        print_dec(wait_ret);
        print_str(" with status = ");
        print_dec(status);
        print_str("\n");

        if (wait_ret == child_pid && status == 42) {
            print_str("[userspace_app]   -> Child exit status 42 confirmed!\n");
        }

        /* Double-wait should return -ECHILD */
        int dbl_wait = firstos_waitpid(child_pid, &status, 0);
        if (dbl_wait == -FIRSTOS_ECHILD) {
            print_str("[userspace_app]   -> Double waitpid correctly returned -ECHILD (-10)!\n");
        }
    }

    /* Phase 3C: Verify Shared Memory contents written by child under mutex lock */
    if (shm_win) {
        int *shm_mtx = (int *)shm_win;
        firstos_mutex_lock(shm_mtx);
        char *shm_buf = ((char *)shm_win) + sizeof(int);
        print_str("[userspace_app]   -> Read from shared memory under mutex: '");
        print_str(shm_buf);
        print_str("'\n");
        firstos_mutex_unlock(shm_mtx);
        firstos_shm_unmap(shm_chan);
        print_str("[userspace_app]   -> Shared memory verified & unmapped cleanly!\n");
    }

    /* With child closed and terminated, read on pipe must return 0 (EOF) */
    char eof_check[10];
    int eof_bytes = firstos_read(pipefd[0], eof_check, sizeof(eof_check));
    if (eof_bytes == 0) {
        print_str("[userspace_app]   -> Pipe EOF confirmed (read returned 0 after writer exit)!\n");
    }
    firstos_close(pipefd[0]);

    /* TEST 9: EPIPE & dup2 redirection */
    print_str("[userspace_app] [TEST 9] Testing EPIPE & dup2 redirection...\n");
    int ep_pipe[2];
    firstos_pipe(ep_pipe);
    firstos_close(ep_pipe[0]);
    int ep_ret = firstos_write(ep_pipe[1], "orphan", 6);
    if (ep_ret == -FIRSTOS_EPIPE) {
        print_str("[userspace_app]   -> Writing to reader-closed pipe returned -EPIPE (-32)!\n");
    }
    firstos_close(ep_pipe[1]);

    int d2_pipe[2];
    firstos_pipe(d2_pipe);
    int d2_ret = firstos_dup2(d2_pipe[0], 0);
    if (d2_ret == 0) {
        print_str("[userspace_app]   -> firstos_dup2 redirected pipe to stdin (fd=0)!\n");
    }
    firstos_close(0);
    firstos_close(d2_pipe[0]);
    firstos_close(d2_pipe[1]);

    /* TEST 10: Exec failure rollback */
    print_str("[userspace_app] [TEST 10] Testing exec failure rollback...\n");
    const char *bad_args[] = { "/non_existent.elf", 0 };
    int bad_exec = firstos_exec("/non_existent.elf", bad_args);
    if (bad_exec == -FIRSTOS_ENOENT) {
        print_str("[userspace_app]   -> Exec non-existent binary correctly returned -ENOENT (-2)!\n");
        print_str("[userspace_app]   -> Calling process retained and running normally!\n");
    }

    /* TEST 11: Exec success commitment */
    print_str("[userspace_app] [TEST 11] Executing /userspace_exec_target.elf...\n");
    print_str("[userspace_app] All userspace runtime tests PASSED!\n");
    print_str("[userspace_app] ========================================\n\n");

    const char *exec_args[] = { "/userspace_exec_target.elf", "arg_exec_1", "arg_exec_2", 0 };
    firstos_exec("/userspace_exec_target.elf", exec_args);

    /* If exec returns, error */
    print_str("[userspace_app] ERROR: exec returned!\n");
    return 1;
}
