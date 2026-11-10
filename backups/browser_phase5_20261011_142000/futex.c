#include "futex.h"
#include "serial.h"
#include "task.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

static futex_waiter_t s_waiters[FIRSTOS_MAX_FUTEX_WAITERS];

void futex_subsystem_init(void) {
    for (int i = 0; i < FIRSTOS_MAX_FUTEX_WAITERS; i++) {
        s_waiters[i].pid = -1;
        s_waiters[i].uaddr = NULL;
        s_waiters[i].waiting = 0;
    }
    serial_write("[futex] Fast userspace mutex & futex subsystem initialized\n");
}

int futex_wait(int pid, int *uaddr, int val) {
    if (!uaddr) return -FIRSTOS_EFAULT;

    /* Atomically check current value */
    if (*uaddr != val) {
        return -FIRSTOS_EAGAIN;
    }

    int slot = -1;
    for (int i = 0; i < FIRSTOS_MAX_FUTEX_WAITERS; i++) {
        if (!s_waiters[i].waiting) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return -FIRSTOS_ENOMEM;

    s_waiters[slot].pid = pid;
    s_waiters[slot].uaddr = uaddr;
    s_waiters[slot].waiting = 1;

    serial_write("[futex] wait pid=");
    serial_write_hex((u32)pid);
    serial_write(" val=");
    serial_write_hex((u32)val);
    serial_write("\n");

    /* Mark task as blocked */
    task_block(pid);

    int max_spins = 500;
    while (s_waiters[slot].waiting && max_spins-- > 0) {
#ifndef FIRSTOS_HOST_TEST
        __asm__ volatile ("int $0x81");
#else
        break;
#endif
    }

    /* Unblock if woke up or timed out */
    task_unblock(pid);

    s_waiters[slot].waiting = 0;
    s_waiters[slot].pid = -1;
    s_waiters[slot].uaddr = NULL;
    return 0;
}

int futex_wake(int *uaddr, int count) {
    if (!uaddr) return -FIRSTOS_EFAULT;
    if (count <= 0) return 0;

    int woken = 0;
    for (int i = 0; i < FIRSTOS_MAX_FUTEX_WAITERS && woken < count; i++) {
        if (s_waiters[i].waiting && s_waiters[i].uaddr == uaddr) {
            s_waiters[i].waiting = 0;
            task_unblock(s_waiters[i].pid);
            woken++;
        }
    }

    serial_write("[futex] wake count=");
    serial_write_hex((u32)woken);
    serial_write("\n");

    return woken;
}

void futex_process_cleanup(int pid) {
    if (pid <= 0) return;
    int cleaned = 0;
    for (int i = 0; i < FIRSTOS_MAX_FUTEX_WAITERS; i++) {
        if (s_waiters[i].pid == pid) {
            s_waiters[i].waiting = 0;
            s_waiters[i].pid = -1;
            s_waiters[i].uaddr = NULL;
            cleaned++;
        }
    }
    (void)cleaned;
    serial_write("[futex] cleanup done pid=");
    serial_write_hex((u32)pid);
    serial_write("\n");
}
