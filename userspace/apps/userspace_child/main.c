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
    (void)argc;
    (void)argv;
    int my_pid = firstos_getpid();
    print_str("[userspace_child] Hello from spawned child! pid=");
    print_dec(my_pid);
    print_str("\n");

    /* Phase 3C: Shared Memory & Fast Mutex */
    int shmid = firstos_shm_open("firstos_shm_channel", 0, 0);
    if (shmid >= 0) {
        void *shm_addr = firstos_shm_map(shmid);
        if (shm_addr) {
            int *shm_mutex = (int *)shm_addr;
            firstos_mutex_lock(shm_mutex);
            char *shm_buf = ((char *)shm_addr) + sizeof(int);
            const char *shm_payload = "SHARED_MEMORY_CHILD_PAYLOAD_LOCKED";
            int p_len = u_strlen(shm_payload);
            for (int i = 0; i <= p_len; i++) {
                shm_buf[i] = shm_payload[i];
            }
            firstos_mutex_unlock(shm_mutex);
            print_str("[userspace_child] Wrote payload to shared memory under mutex lock!\n");
            firstos_shm_unmap(shmid);
        }
    }

    /* Phase 3B IPC: Write message to inherited pipe write-end (fd 4) */
    const char *ipc_msg = "HELLO_PARENT_FROM_CHILD_PIPE";
    int w_ret = firstos_write(4, ipc_msg, u_strlen(ipc_msg));
    if (w_ret > 0) {
        print_str("[userspace_child] Sent IPC message to parent via pipe (fd=4)!\n");
        firstos_close(4);
    }

    print_str("[userspace_child] Exiting cleanly with status 42...\n");
    return 42;
}
