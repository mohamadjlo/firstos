#ifndef LINUX_SYSCALLS_H
#define LINUX_SYSCALLS_H

#include "io.h"

void linux_syscalls_init(void);
u64  linux_syscall_dispatcher(u64 num, u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6);

#endif
