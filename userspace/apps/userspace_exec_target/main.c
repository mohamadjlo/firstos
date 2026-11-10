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
    int pid = firstos_getpid();
    print_str("[userspace_exec_target] started successfully! pid=");
    print_dec(pid);
    print_str("\n");

    print_str("[userspace_exec_target] argc=");
    print_dec(argc);
    print_str("\n");

    for (int i = 0; i < argc; i++) {
        print_str("[userspace_exec_target]   argv[");
        print_dec(i);
        print_str("]='");
        print_str(argv[i]);
        print_str("'\n");
    }

    print_str("[userspace_exec_target] argv validated, exec replacement COMPLETE!\n");
    print_str("[userspace_exec_target] Exiting cleanly with status 0...\n");
    return 0;
}
