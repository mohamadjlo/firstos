#include "futex.h"
#include "serial.h"

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

    int max_spins = 200;
    while (s_waiters[slot].waiting && max_spins-- > 0) {
#ifndef FIRSTOS_HOST_TEST
        __asm__ volatile ("int $0x81");
#else
        break;
#endif
    }

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
            woken++;
        }
    }
    return woken;
}
