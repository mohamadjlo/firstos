#ifndef GPU_SYSCALL_H
#define GPU_SYSCALL_H

#include "io.h"
#include "gpu_abi.h"

int gpu_syscall_init(void);

/* Safe User Memory Copy Abstractions */
int gpu_copy_from_user(void *kdst, const void *usrc, u32 size);
int gpu_copy_to_user(void *udst, const void *ksrc, u32 size);

/* Header validation helper */
int gpu_validate_abi_header(const gpu_abi_header_t *hdr, u16 expected_size, u32 allowed_flags);

/* Central Syscall Dispatcher */
int gpu_syscall_dispatch(u32 op,
                         gpu_client_handle_t client_handle,
                         const void *user_in,
                         u32 in_size,
                         void *user_out,
                         u32 out_size);

#endif /* GPU_SYSCALL_H */
