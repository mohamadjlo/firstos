#ifndef CPU_SMP_H
#define CPU_SMP_H

#include <stdint.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define SMP_MAX_CORES 16

/* ACPI MADT Structures */
typedef struct __attribute__((packed)) {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;
    u32 length;
    u64 xsdt_address;
    u8 extended_checksum;
    u8 reserved[3];
} acpi_rsdp_t;

typedef struct __attribute__((packed)) {
    char signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
} acpi_sdt_header_t;

typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    u32 local_apic_address;
    u32 flags;
} acpi_madt_t;

typedef struct __attribute__((packed)) {
    u8 type;
    u8 length;
} acpi_madt_entry_header_t;

typedef struct __attribute__((packed)) {
    acpi_madt_entry_header_t header;
    u8 processor_id;
    u8 apic_id;
    u32 flags;
} acpi_madt_local_apic_t;

typedef struct __attribute__((packed)) {
    acpi_madt_entry_header_t header;
    u8 io_apic_id;
    u8 reserved;
    u32 io_apic_address;
    u32 global_system_interrupt_base;
} acpi_madt_io_apic_t;

typedef struct {
    int core_id;
    u8 apic_id;
    int is_bsp;
    int enabled;
    u32 ticks;
    char name[24];
} smp_cpu_core_t;

/* SMP Subsystem API */
void cpu_smp_init(void);
int  cpu_smp_get_core_count(void);
int  cpu_smp_get_bsp_id(void);
u64  cpu_smp_get_lapic_base(void);
const smp_cpu_core_t *cpu_smp_get_core(int idx);
void cpu_smp_dump_diagnostics(void);

#endif /* CPU_SMP_H */
