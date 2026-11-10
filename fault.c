#include "fault.h"
#include "serial.h"
#include "task.h"
#include "process.h"
#include "ktrace.h"
#include "app_supervisor.h"

extern task_t tasks[MAX_TASKS];
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

static void print_reg64(const char *name, u64 val) {
    serial_write("  ");
    serial_write(name);
    serial_write("=0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        u8 nibble = (u8)((val >> shift) & 0x0F);
        char ch = (char)(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
        char str[2] = { ch, '\0' };
        serial_write(str);
    }
}

void fault_handler_full(fault_frame_t *frame) {
    int is_user = ((frame->cs & 3) == 3);

    serial_write("\n======================================================\n");
    serial_write("[FAULT] ");
    if (frame->int_no < 20) {
        serial_write(exception_names[frame->int_no]);
    } else {
        serial_write("#EX");
    }
    serial_write(" (vector=");
    serial_write_hex((u32)frame->int_no);
    serial_write(is_user ? ") Mode: USER (Ring 3)\n" : ") Mode: KERNEL (Ring 0)\n");

    /* Comprehensive Register Dump */
    serial_write("Instruction & State:\n");
    print_reg64("RIP", frame->rip);
    print_reg64("CS ", frame->cs);
    print_reg64("RFLAGS", frame->rflags);
    print_reg64("ERR", frame->err_code);
    serial_write("\n");

    print_reg64("RSP", frame->rsp);
    print_reg64("SS ", frame->ss);
    print_reg64("RBP", frame->rbp);
    serial_write("\n");

    serial_write("General Purpose Registers:\n");
    print_reg64("RAX", frame->rax);
    print_reg64("RBX", frame->rbx);
    print_reg64("RCX", frame->rcx);
    print_reg64("RDX", frame->rdx);
    serial_write("\n");

    print_reg64("RSI", frame->rsi);
    print_reg64("RDI", frame->rdi);
    print_reg64("R8 ", frame->r8);
    print_reg64("R9 ", frame->r9);
    serial_write("\n");

    print_reg64("R10", frame->r10);
    print_reg64("R11", frame->r11);
    print_reg64("R12", frame->r12);
    print_reg64("R13", frame->r13);
    serial_write("\n");

    print_reg64("R14", frame->r14);
    print_reg64("R15", frame->r15);
    serial_write("\n");

    /* Vector 14 (#PF: Page Fault) detailed decode */
    if (frame->int_no == 14) {
        u64 cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        print_reg64("CR2 (Faulting Addr)", cr2);
        serial_write("\n  #PF Flags: ");
        serial_write((frame->err_code & 1) ? "[PRESENT] " : "[NOT-PRESENT] ");
        serial_write((frame->err_code & 2) ? "[WRITE] " : "[READ] ");
        serial_write((frame->err_code & 4) ? "[USER] " : "[SUPERVISOR] ");
        if (frame->err_code & 8) serial_write("[RSVD-WRITE] ");
        if (frame->err_code & 16) serial_write("[INSTR-FETCH] ");
        serial_write("\n");
    }

    /* Stack Inspection (Top 4 64-bit words) */
    if (frame->rsp && (frame->rsp & 0x7) == 0) {
        u64 *stack_ptr = (u64 *)(uintptr_t)frame->rsp;
        serial_write("Stack Words at RSP:\n");
        for (int s = 0; s < 4; s++) {
            print_reg64("+off", (u64)(s * 8));
            print_reg64("val", stack_ptr[s]);
            serial_write("\n");
        }
    }

    /* Record into ktrace framework and dump recent entries */
    ktrace_record(KTRACE_SUBSYS_FAULT, KTRACE_LEVEL_FAULT, (u16)current_task,
                  frame->int_no < 20 ? exception_names[frame->int_no] : "Exception");
    ktrace_dump_recent(10);
    serial_write("======================================================\n");

    /* GUI Application Isolation: If a GUI app faulted, isolate without kernel panic */
    int active_app = app_supervisor_get_active_app();
    if (active_app > 0) {
        u64 cr2 = 0;
        if (frame->int_no == 14) {
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        }
        app_supervisor_handle_fault(active_app, (u32)frame->int_no, frame->rip, cr2);
        app_supervisor_set_active_app(0);

        load_page_directory(kernel_pml4);
        frame->cs = 0x08;
        frame->ss = 0x10;
        if (current_task >= 0 && current_task < MAX_TASKS && tasks[current_task].rsp0) {
            frame->rsp = tasks[current_task].rsp0 - 16;
        }
        frame->rip = (u64)(uintptr_t)task_recover;
        frame->rflags = 0x202;
        return;
    }

    /* User process isolation: if fault originated in user mode or user task */
    if (is_user || current_task > 0) {
        serial_write("[fault] Terminating isolated user process pid=");
        serial_write_hex((u32)current_task);
        if (current_task >= 0 && current_task < FIRSTOS_MAX_PROCESSES) {
            serial_write(" name=");
            serial_write(tasks[current_task].name[0] ? tasks[current_task].name : "unknown");
            process_table[current_task].state = PROC_STATE_ZOMBIE;
            process_table[current_task].exit_status = 128 + (int)frame->int_no;
            process_table[current_task].exit_code_valid = 1;
        }
        serial_write("\n");

        tasks[current_task].active = 0;
        tasks[current_task].state = TASK_STATE_ZOMBIE;

        /* Restore kernel CR3 */
        load_page_directory(kernel_pml4);

        /* Redirect iret to kernel recover loop which calls task_yield (int 0x81) */
        frame->cs = 0x08;
        frame->ss = 0x10;
        if (current_task >= 0 && current_task < MAX_TASKS && tasks[current_task].rsp0) {
            frame->rsp = tasks[current_task].rsp0 - 16;
        }
        frame->rip = (u64)(uintptr_t)task_recover;
        frame->rflags = 0x202;
        return;
    }

    /* Kernel Mode Fault: System halt with detailed panic info */
    serial_write("[fault] KERNEL PANIC: unrecoverable kernel mode exception. CPU halted.\n");
    __asm__ volatile("cli");
    while (1) {
        __asm__ volatile("hlt");
    }
}
