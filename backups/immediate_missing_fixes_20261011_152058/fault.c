#include "fault.h"
#include "serial.h"
#include "task.h"

extern task_t tasks[16];
extern int current_task;
extern u64 kernel_pml4[512];
extern void load_page_directory(u64*);

static const char *exception_names[] = {
    "#DE: Divide By Zero",
    "#DB: Debug Exception",
    "#NMI: Non-Maskable Interrupt",
    "#BP: Breakpoint",
    "#OF: Overflow",
    "#BR: Bound Range Exceeded",
    "#UD: Invalid Opcode",
    "#NM: Device Not Available",
    "#DF: Double Fault",
    "#CSO: Coprocessor Segment Overrun",
    "#TS: Invalid TSS",
    "#NP: Segment Not Present",
    "#SS: Stack-Segment Fault",
    "#GP: General Protection Fault",
    "#PF: Page Fault",
    "#RES: Reserved",
    "#MF: x87 FPU Error",
    "#AC: Alignment Check",
    "#MC: Machine Check",
    "#XF: SIMD Exception"
};

void task_recover(void) {
    __asm__ volatile ("sti");
    while (1) {
        __asm__ volatile ("int $0x81");
    }
}

void fault_handler_full(fault_frame_t *frame) {
    serial_write("\n[FAULT] ");
    if (frame->int_no < 20) {
        serial_write(exception_names[frame->int_no]);
    } else {
        serial_write("#EX");
    }
    serial_write(" (vec=");
    serial_write_hex((u32)frame->int_no);
    serial_write(") RIP=");
    serial_write_hex((u32)(frame->rip >> 32));
    serial_write_hex((u32)frame->rip);
    serial_write(" CS=");
    serial_write_hex((u32)frame->cs);
    serial_write(" RFLAGS=");
    serial_write_hex((u32)frame->rflags);
    serial_write(" ERR=");
    serial_write_hex((u32)frame->err_code);

    if (frame->int_no == 14) {
        u64 cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        serial_write(" CR2=");
        serial_write_hex((u32)(cr2 >> 32));
        serial_write_hex((u32)cr2);
    }
    serial_write("\n");

    /* User process isolation: if current_task > 0, kill task and return to kernel Task 0 */
    if (current_task > 0) {
        serial_write("[fault] Terminating user process tid=");
        serial_write_hex((u32)current_task);
        serial_write(" name=");
        serial_write(tasks[current_task].name);
        serial_write("\n");

        tasks[current_task].active = 0;

        /* Restore kernel CR3 */
        load_page_directory(kernel_pml4);

        /* Redirect iret to kernel recover loop which calls task_yield (int 0x81) */
        frame->cs = 0x08;
        frame->rip = (u64)(uintptr_t)task_recover;
        frame->rflags = 0x202;
        return;
    }

    /* Kernel Mode Fault: Halt safely */
    serial_write("[fault] Kernel Panic: halted\n");
    __asm__ volatile("cli");
    while (1) {
        __asm__ volatile("hlt");
    }
}
