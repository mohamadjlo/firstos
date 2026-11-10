#ifndef _FIRSTOS_KTRACE_H
#define _FIRSTOS_KTRACE_H

#include "io.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Trace Severity Levels */
typedef enum {
    KTRACE_LEVEL_DEBUG = 0,
    KTRACE_LEVEL_INFO  = 1,
    KTRACE_LEVEL_WARN  = 2,
    KTRACE_LEVEL_ERROR = 3,
    KTRACE_LEVEL_FAULT = 4
} ktrace_level_t;

/* Trace Subsystem Identifiers */
typedef enum {
    KTRACE_SUBSYS_KERNEL  = 0,
    KTRACE_SUBSYS_TASK    = 1,
    KTRACE_SUBSYS_PROC    = 2,
    KTRACE_SUBSYS_SYSCALL = 3,
    KTRACE_SUBSYS_MEM     = 4,
    KTRACE_SUBSYS_VFS     = 5,
    KTRACE_SUBSYS_IPC     = 6,
    KTRACE_SUBSYS_FAULT   = 7
} ktrace_subsys_t;

#define KTRACE_RING_CAPACITY 256
#define KTRACE_MSG_MAX       64

typedef struct {
    u32 tick;
    u16 pid;
    u8  subsys;
    u8  level;
    char msg[KTRACE_MSG_MAX];
} ktrace_record_t;

void ktrace_init(void);
void ktrace_record(ktrace_subsys_t subsys, ktrace_level_t level, u16 pid, const char *msg);
void ktrace_write(ktrace_subsys_t subsys, ktrace_level_t level, u16 pid, const char *msg);
void ktrace_write_hex(ktrace_subsys_t subsys, ktrace_level_t level, u16 pid, const char *prefix, u64 val);
void ktrace_dump_recent(int count);
void ktrace_set_min_level(ktrace_level_t level);

#ifdef __cplusplus
}
#endif

#endif /* _FIRSTOS_KTRACE_H */
