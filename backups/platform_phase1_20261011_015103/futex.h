#ifndef _FIRSTOS_FUTEX_H
#define _FIRSTOS_FUTEX_H

#include "include/firstos/uapi/syscall.h"

#define FIRSTOS_FUTEX_WAIT 0
#define FIRSTOS_FUTEX_WAKE 1

#define FIRSTOS_MAX_FUTEX_WAITERS 32

typedef struct {
    int pid;
    int *uaddr;
    int waiting;
} futex_waiter_t;

void futex_subsystem_init(void);
int  futex_wait(int pid, int *uaddr, int val);
int  futex_wake(int *uaddr, int count);

#endif /* _FIRSTOS_FUTEX_H */
