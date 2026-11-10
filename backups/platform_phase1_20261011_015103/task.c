#include "task.h"
#include "io.h"
#include "serial.h"
#include "gdt.h"
#include "heap.h"

task_t tasks[16];
int current_task = -1;
int multitasking_enabled = 1;

void tasking_init(void) {
    int i, j;
    for (i = 0; i < MAX_TASKS; i++) {
        tasks[i].active = 0;
        tasks[i].tick_count = 0;
        tasks[i].rsp0 = 0;
        tasks[i].cr3 = 0;
        for (j = 0; j < 32; j++) tasks[i].name[j] = 0;
    }
    current_task = 0;
    tasks[0].active = 1; /* Kernel task */
    tasks[0].name[0]='K'; tasks[0].name[1]='e'; tasks[0].name[2]='r';
    tasks[0].name[3]='n'; tasks[0].name[4]='e'; tasks[0].name[5]='l'; tasks[0].name[6]=0;

    extern u64 kernel_pml4[512];
    tasks[0].cr3 = (u64)(uintptr_t)kernel_pml4;

    extern unsigned int stack_top;
    tasks[0].rsp0 = (u64)(uintptr_t)&stack_top;
    tss_set_rsp0(tasks[0].rsp0);

    multitasking_enabled = 1;
    serial_write("[task] 64-bit multitasking initialized with TSS rsp0 switching\n");
}

extern int gui_is_idle;
extern void taskman_record_history(void);
static int tick_sec_counter = 0;
int idle_ticks_last_sec = 0;
static int current_idle_ticks = 0;
volatile u32 system_ticks = 0;

u32 get_system_ticks(void) {
    return system_ticks;
}

u64 timer_handler(u64 current_rsp) {
    system_ticks++;
    if (gui_is_idle && current_task == 0) current_idle_ticks++;
    tick_sec_counter++;
    if (tick_sec_counter >= 100) {
        idle_ticks_last_sec = current_idle_ticks;
        current_idle_ticks = 0;
        tick_sec_counter = 0;
        taskman_record_history();
        extern int needs_redraw;
        needs_redraw = 1;
    }

    outb(0x20, 0x20); // Send EOI to PIC

    if (!multitasking_enabled) {
        return current_rsp;
    }
    
    /* Save current task state and record tick */
    tasks[current_task].rsp = current_rsp;
    tasks[current_task].tick_count++;
    
    /* Quantum logic: give Kernel (task 0) more CPU time (10 ticks) compared to apps (1 tick) */
    static int current_quantum = 0;
    int max_quantum = (current_task == 0) ? 10 : 1;
    current_quantum++;
    if (current_quantum < max_quantum) {
        return current_rsp;
    }
    current_quantum = 0;
    
    // Find next task
    int next_task = current_task + 1;
    int found = 0;
    int k;
    for (k = 0; k < 16; k++) {
        if (next_task >= 16) next_task = 0;
        if (tasks[next_task].active) {
            found = 1;
            break;
        }
        next_task++;
    }
    
    if (found) {
        if (next_task != current_task && next_task == 1) {
            serial_write("[task] timer_handler switching to tid=1\n");
        }
        current_task = next_task;
    } else {
        // Fallback to task 0 unconditionally to prevent hangs!
        current_task = 0;
        tasks[0].active = 1;
    }

    /* Update TSS rsp0 for the active task to receive interrupts */
    if (tasks[current_task].rsp0) {
        tss_set_rsp0(tasks[current_task].rsp0);
    }
    
    // CR3 switching is handled in isr32!
    return tasks[current_task].rsp;
}

int task_create(u64 cr3, u64 entry_point, u64 stack_top, u64 stack_phys) {
    int i;
    for (i = 1; i < MAX_TASKS; i++) {
        if (!tasks[i].active) {
            int kk;
            tasks[i].cr3 = cr3;
            tasks[i].tick_count = 0;
            tasks[i].rsp0 = stack_top;
            for (kk = 0; kk < 32; kk++) tasks[i].name[kk] = 0;
            
            // Set up initial Ring 0 stack for the kernel-mode task
            // In 64-bit Long Mode: iretq frame (5 qwords: SS, RSP, RFLAGS, CS, RIP)
            // followed by 15 general purpose registers pushed by ISR
            u64 *sp = (u64 *)(uintptr_t)stack_phys;
            *--sp = 0x10;        // SS (Kernel Data)
            *--sp = stack_top;   // RSP
            *--sp = 0x202;       // RFLAGS (IF=1)
            *--sp = 0x08;        // CS (Kernel Code)
            *--sp = entry_point; // RIP
            
            // 15 general purpose registers (r15..rax)
            for (int k = 0; k < 15; k++) {
                *--sp = 0;
            }
            
            tasks[i].rsp = (u64)(uintptr_t)sp;
            tasks[i].active = 1;
            return i;
        }
    }
    return -1;
}

unsigned char task_user_kstacks[MAX_TASKS][4096] __attribute__((aligned(4096)));

int task_create_user(u64 cr3, u64 entry_point, u64 user_stack_top, u64 user_stack_phys) {
    (void)user_stack_phys;
    int i;
    for (i = 1; i < MAX_TASKS; i++) {
        if (!tasks[i].active) {
            u64 kstack_top = (u64)(uintptr_t)&task_user_kstacks[i][4096];

            tasks[i].rsp0 = kstack_top;
            tasks[i].cr3 = cr3;
            tasks[i].tick_count = 0;
            for (int kk = 0; kk < 32; kk++) tasks[i].name[kk] = 0;

            // Forge Ring 3 inter-privilege iretq frame on dedicated kstack
            // In x86_64: iretq expects 5 qwords:
            // [RSP+32] = SS (0x1B = 0x18 | 3 User Data)
            // [RSP+24] = RSP (user_stack_top)
            // [RSP+16] = RFLAGS (0x202)
            // [RSP+8]  = CS (0x23 = 0x20 | 3 User Code)
            // [RSP+0]  = RIP (entry_point)
            u64 *sp = (u64 *)(uintptr_t)kstack_top;
            *--sp = 0x1B;           // SS (User Data: 0x18 | 3)
            *--sp = user_stack_top; // RSP in Ring 3
            *--sp = 0x202;          // RFLAGS (IF=1)
            *--sp = 0x23;           // CS (User Code: 0x20 | 3)
            *--sp = entry_point;    // RIP

            // 15 general purpose registers (r15..rax)
            for (int k = 0; k < 15; k++) {
                *--sp = 0;
            }

            tasks[i].rsp = (u64)(uintptr_t)sp;
            tasks[i].active = 1;
            return i;
        }
    }
    return -1;
}

u64 task_yield(u64 current_rsp) {
    if (!multitasking_enabled) return current_rsp;
    tasks[current_task].rsp = current_rsp;
    
    int next_task = current_task + 1;
    int found = 0;
    int k;
    for (k = 0; k < 16; k++) {
        if (next_task >= 16) next_task = 0;
        if (tasks[next_task].active) {
            found = 1;
            break;
        }
        next_task++;
    }
    if (found) current_task = next_task;
    else current_task = 0;

    if (tasks[current_task].rsp0) {
        tss_set_rsp0(tasks[current_task].rsp0);
    }
    
    if (current_task > 0) {
        serial_write("[task_yield] switch to tid=");
        serial_write_hex((u32)current_task);
        serial_write("\n");
    }

    return tasks[current_task].rsp;
}
