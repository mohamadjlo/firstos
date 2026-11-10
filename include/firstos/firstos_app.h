#ifndef _FIRSTOS_APP_H
#define _FIRSTOS_APP_H

#include "include/firstos/uapi/syscall.h"
#include "userspace/include/firstos/syscall.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard string / buffer helpers for userspace apps */
static inline size_t firstos_app_strlen(const char *s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static inline void firstos_app_print(const char *msg) {
    if (msg) {
        firstos_write(1, msg, (unsigned int)firstos_app_strlen(msg));
    }
}

static inline void firstos_app_println(const char *msg) {
    if (msg) {
        firstos_write(1, msg, (unsigned int)firstos_app_strlen(msg));
    }
    firstos_write(1, "\n", 1);
}

/* App lifecycle */
static inline void firstos_app_exit(int code) {
    firstos_exit(code);
}

static inline void firstos_app_yield(void) {
    firstos_yield();
}

#ifdef __cplusplus
}
#endif

#endif /* _FIRSTOS_APP_H */
