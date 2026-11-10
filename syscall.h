#ifndef SYSCALL_H
#define SYSCALL_H

#include "include/firstos/uapi/memory.h"

#define USER_SPACE_START FIRSTOS_USER_BASE
#define USER_SPACE_END   FIRSTOS_USER_TOTAL_END

void syscall_init(void);
int  validate_user_buffer(const void *ptr, unsigned int len);
int  validate_user_string(const char *str, unsigned int max_len);
int  validate_user_argv(const char *const *argv, int max_argc, int max_len);

#endif
