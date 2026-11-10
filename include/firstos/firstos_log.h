#ifndef _FIRSTOS_LOG_H
#define _FIRSTOS_LOG_H

#include "firstos_app.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void firstos_log_info(const char *tag, const char *msg) {
    firstos_app_print("[INFO] [");
    firstos_app_print(tag ? tag : "app");
    firstos_app_print("] ");
    firstos_app_println(msg ? msg : "");
}

static inline void firstos_log_warn(const char *tag, const char *msg) {
    firstos_app_print("[WARN] [");
    firstos_app_print(tag ? tag : "app");
    firstos_app_print("] ");
    firstos_app_println(msg ? msg : "");
}

static inline void firstos_log_error(const char *tag, const char *msg) {
    firstos_app_print("[ERROR] [");
    firstos_app_print(tag ? tag : "app");
    firstos_app_print("] ");
    firstos_app_println(msg ? msg : "");
}

#ifdef __cplusplus
}
#endif

#endif /* _FIRSTOS_LOG_H */
