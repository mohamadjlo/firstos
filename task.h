#ifndef TASK_H
#define TASK_H

#include "io.h"

#define MAX_TASKS 16
/* Per-task Ring 0 stack size. 4 KB was too small: nested syscall + int 0x81
 * yield + logging overflowed into the adjacent task's stack, so re-spawning a
 * process into slot N corrupted the saved context of task N+1. */
#define TASK_KSTACK_SIZE 16384

/* Task States */
#define TASK_STATE_UNUSED   0
#define TASK_STATE_READY    1
#define TASK_STATE_RUNNING  2
#define TASK_STATE_BLOCKED  3
#define TASK_STATE_ZOMBIE   4

/*
 * 64-bit Task Structure
 * Total size: exactly 64 bytes (shl rax, 6 in isr.asm)
 */
typedef struct {
    u64 rsp;        /* offset 0: saved RSP */
    u64 cr3;        /* offset 8: CR3 page table base */
    int active;     /* offset 16: 1 if active, 0 if unused */
    u32 tick_count; /* offset 20: ticks run */
    u64 rsp0;       /* offset 24: kernel stack top for Ring 3 interrupts/syscalls */
    int state;      /* offset 32: TASK_STATE_* */
    char name[28];  /* offset 36: task name */
} task_t; /* sizeof = 64 bytes */

void tasking_init(void);
u64 timer_handler(u64 current_rsp);
int task_create(u64 cr3, u64 entry_point, u64 stack_top, u64 stack_phys);
int task_create_user(u64 cr3, u64 entry_point, u64 user_stack_top, u64 user_stack_phys);
u64 task_yield(u64 current_rsp);
u32 get_system_ticks(void);

void task_block(int tid);
void task_unblock(int tid);
int  task_get_state(int tid);
void task_set_state(int tid, int state);

#endif
