#ifndef TASK_H
#define TASK_H

#include "io.h"

#define MAX_TASKS 16

/*
 * 64-bit Task Structure
 * Total size: 64 bytes
 */
typedef struct {
    u64 rsp;        /* offset 0: saved RSP */
    u64 cr3;        /* offset 8: CR3 page table base */
    int active;     /* offset 16: 1 if active, 0 if unused */
    u32 tick_count; /* offset 20: ticks run */
    u64 rsp0;       /* offset 24: kernel stack top for Ring 3 interrupts/syscalls */
    char name[32];  /* offset 32: task name */
} task_t; /* sizeof = 64 bytes */

void tasking_init(void);
u64 timer_handler(u64 current_rsp);
int task_create(u64 cr3, u64 entry_point, u64 stack_top, u64 stack_phys);
int task_create_user(u64 cr3, u64 entry_point, u64 user_stack_top, u64 user_stack_phys);
u64 task_yield(u64 current_rsp);
u32 get_system_ticks(void);

#endif
