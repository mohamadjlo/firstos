#ifndef FAULT_H
#define FAULT_H

#include "io.h"

/* 64-bit Interrupt / Fault stack frame */
typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 int_no;
    u64 err_code;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
} __attribute__((packed)) fault_frame_t;

void fault_handler_full(fault_frame_t *frame);

#endif
