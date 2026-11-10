#include "gdt.h"

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_middle;
    u8  access;
    u8  granularity;
    u8  base_high;
} __attribute__((packed));

struct tss_descriptor64 {
    u16 limit_low;
    u16 base_low;
    u8  base_middle;
    u8  access;
    u8  granularity;
    u8  base_high;
    u32 base_upper;
    u32 reserved;
} __attribute__((packed));

struct gdt_ptr64 {
    u16 limit;
    u64 base;
} __attribute__((packed));

/*
 * GDT Layout (64-bit Long Mode):
 * Index 0: 0x00 - Null Descriptor
 * Index 1: 0x08 - Kernel Code 64-bit (L=1, D=0, Access=0x9A)
 * Index 2: 0x10 - Kernel Data 64-bit (Access=0x92)
 * Index 3: 0x18 | 3 = 0x1B - User Data 64-bit (Access=0xF2)
 * Index 4: 0x20 | 3 = 0x23 - User Code 64-bit (L=1, D=0, Access=0xFA)
 * Index 5 & 6: 0x28 - TSS Descriptor (16 bytes in x86_64, spans 2 slots)
 * Index 7: 0x38 - Available / TLS slot
 */
struct gdt_entry gdt[8];
struct gdt_ptr64 gp;
tss_entry64_t tss64;

extern void gdt_flush(u64);
extern void tss_flush(void);
extern void serial_write(const char*);

void gdt_set_gate(int num, u32 base, u32 limit, u8 access, u8 gran) {
    if (num < 0 || num >= 8) return;
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F);
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

void tss_set_rsp0(u64 rsp0) {
    tss64.rsp0 = rsp0;
}

void tss_set_esp0(unsigned int esp0) {
    tss64.rsp0 = (u64)esp0;
}

void tss_init(void) {
    u64 base = (u64)&tss64;
    u32 limit = sizeof(tss_entry64_t) - 1;
    u8 *p = (u8 *)&tss64;
    for (u32 i = 0; i < sizeof(tss_entry64_t); i++) p[i] = 0;

    tss64.rsp0 = 0;
    tss64.iomap_base = sizeof(tss_entry64_t);

    /*
     * 16-byte TSS descriptor in 64-bit mode spanning gdt[5] and gdt[6]
     * Access: 0x89 (Present, 64-bit TSS Available, DPL 0)
     */
    struct tss_descriptor64 *tss_desc = (struct tss_descriptor64 *)&gdt[5];
    tss_desc->limit_low = limit & 0xFFFF;
    tss_desc->base_low = base & 0xFFFF;
    tss_desc->base_middle = (base >> 16) & 0xFF;
    tss_desc->access = 0x89;
    tss_desc->granularity = (limit >> 16) & 0x0F;
    tss_desc->base_high = (base >> 24) & 0xFF;
    tss_desc->base_upper = (u32)(base >> 32);
    tss_desc->reserved = 0;

    tss_flush();
    serial_write("[gdt] 64-bit TSS initialized and loaded (selector 0x28)\n");
}

void gdt_init(void) {
    gp.limit = (sizeof(struct gdt_entry) * 8) - 1;
    gp.base = (u64)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);                 // Null segment (0x00)
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0x20); // 64-bit Kernel Code (0x08, L=1, D=0)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0x00); // Kernel Data (0x10)
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xF2, 0x00); // User Data   (0x18 | 3 = 0x1B)
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xFA, 0x20); // 64-bit User Code (0x20 | 3 = 0x23, L=1)
    gdt_set_gate(5, 0, 0, 0, 0);                 // TSS descriptor low 8 bytes
    gdt_set_gate(6, 0, 0, 0, 0);                 // TSS descriptor high 8 bytes
    gdt_set_gate(7, 0, 0xFFFFFFFF, 0xF2, 0x00); // TLS slot (0x38 | 3 = 0x3B)

    gdt_flush((u64)&gp);
    tss_init();
}
