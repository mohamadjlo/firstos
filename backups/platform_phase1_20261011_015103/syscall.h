#ifndef _FIRSTOS_UAPI_SYSCALL_H
#define _FIRSTOS_UAPI_SYSCALL_H

/* FirstOS Core Syscall Numbers (ABI v1) */
#define FIRSTOS_SYS_PRINT   0
#define FIRSTOS_SYS_EXIT    1
#define FIRSTOS_SYS_YIELD   2
#define FIRSTOS_SYS_READ    3
#define FIRSTOS_SYS_WRITE   4
#define FIRSTOS_SYS_CLOSE   6
#define FIRSTOS_SYS_WAITPID 7
#define FIRSTOS_SYS_EXEC    11
#define FIRSTOS_SYS_GETPID  20
#define FIRSTOS_SYS_DUP     41
#define FIRSTOS_SYS_PIPE    42
#define FIRSTOS_SYS_BRK       45
#define FIRSTOS_SYS_DUP2      63
#define FIRSTOS_SYS_SPAWN     120
#define FIRSTOS_SYS_SHM_OPEN  121
#define FIRSTOS_SYS_SHM_MAP   122
#define FIRSTOS_SYS_SHM_UNMAP 123
#define FIRSTOS_SYS_FUTEX     240

/* Futex Operations */
#define FIRSTOS_FUTEX_WAIT    0
#define FIRSTOS_FUTEX_WAKE    1

/* FirstOS Standard Error Codes */
#define FIRSTOS_EPERM       1
#define FIRSTOS_ENOENT      2
#define FIRSTOS_ENOEXEC     8
#define FIRSTOS_EBADF       9
#define FIRSTOS_ECHILD      10
#define FIRSTOS_EAGAIN      11
#define FIRSTOS_ENOMEM      12
#define FIRSTOS_EFAULT      14
#define FIRSTOS_EINVAL      22
#define FIRSTOS_EMFILE      24
#define FIRSTOS_EPIPE       32
#define FIRSTOS_ENOSYS      38

#endif /* _FIRSTOS_UAPI_SYSCALL_H */
