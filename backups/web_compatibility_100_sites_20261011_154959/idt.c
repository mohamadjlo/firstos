#include "idt.h"
#include "io.h"

struct idt_entry64 {
    u16 offset_low;
    u16 selector;
    u8  ist;        /* Interrupt Stack Table index: bits 0..2, rest 0 */
    u8  type_attr;  /* Gate type, DPL, Present */
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
} __attribute__((packed));

struct idt_ptr64 {
    u16 limit;
    u64 base;
} __attribute__((packed));

struct idt_entry64 idt[256];
struct idt_ptr64 idtp;

extern void idt_load(void);
extern void isr80(void);
extern void isr81(void);
extern void isr32(void);
extern void isr_generic(void);
extern void (*isr_table[32])(void);

void idt_set_gate(unsigned char num, u64 base, u16 selector, u8 flags) {
    idt[num].offset_low  = (u16)(base & 0xFFFF);
    idt[num].selector    = selector;
    idt[num].ist         = 0;
    idt[num].type_attr   = flags;
    idt[num].offset_mid  = (u16)((base >> 16) & 0xFFFF);
    idt[num].offset_high = (u32)((base >> 32) & 0xFFFFFFFF);
    idt[num].zero        = 0;
}

void idt_init(void) {
    idtp.limit = (sizeof(struct idt_entry64) * 256) - 1;
    idtp.base  = (u64)&idt;

    int i;
    /* Default fallback for all 256 vectors (0x8E = Present, 64-bit Interrupt Gate, DPL 0) */
    for (i = 0; i < 256; i++) {
        idt_set_gate(i, (u64)isr_generic, 0x08, 0x8E);
    }

    /* CPU Exceptions 0..31 */
    for (i = 0; i < 32; i++) {
        idt_set_gate(i, (u64)isr_table[i], 0x08, 0x8E);
    }

    /* Timer IRQ 0 (vector 32) */
    idt_set_gate(32, (u64)isr32, 0x08, 0x8E);

    /* Linux System Call: 0x80 (128) - 0xEE = Present, 64-bit Interrupt Gate, Ring 3 (DPL 3) */
    idt_set_gate(128, (u64)isr80, 0x08, 0xEE);

    /* Process Yield: 0x81 (129) - 0xEE = Present, 64-bit Interrupt Gate, Ring 3 (DPL 3) */
    idt_set_gate(129, (u64)isr81, 0x08, 0xEE);

    idt_load();
}
