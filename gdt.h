#ifndef GDT_H
#define GDT_H

#include "io.h"

/* 64-bit Task State Segment (TSS) */
typedef struct {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __attribute__((packed)) tss_entry64_t;

void gdt_init(void);
void tss_init(void);
void tss_set_rsp0(u64 rsp0);
void tss_set_esp0(unsigned int esp0); /* 32-bit compatibility alias */

#endif
