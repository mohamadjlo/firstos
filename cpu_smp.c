#include "cpu_smp.h"
#include "serial.h"

static smp_cpu_core_t s_cores[SMP_MAX_CORES];
static int s_core_count = 0;
static int s_bsp_id = 0;
static u64 s_lapic_base = 0xFEE00000ULL;
static u32 s_io_apic_addr = 0xFEC00000;
static int s_smp_initialized = 0;

static inline u64 rdmsr(u32 msr) {
    u32 low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((u64)high << 32) | low;
}

static inline void wrmsr(u32 msr, u64 val) {
    u32 low = (u32)val;
    u32 high = (u32)(val >> 32);
    __asm__ volatile ("wrmsr" :: "a"(low), "d"(high), "c"(msr));
}

static int acpi_checksum(const void *addr, int len) {
    const u8 *p = (const u8*)addr;
    u8 sum = 0;
    while (len--) sum += *p++;
    return (sum == 0);
}

static const acpi_rsdp_t *find_rsdp(void) {
    /* Search in the BIOS read-only memory space 0xE0000 - 0xFFFFF */
    const u8 *bios = (const u8 *)0xE0000;
    for (u32 i = 0; i < 0x20000; i += 16) {
        const acpi_rsdp_t *candidate = (const acpi_rsdp_t *)(bios + i);
        if (candidate->signature[0] == 'R' &&
            candidate->signature[1] == 'S' &&
            candidate->signature[2] == 'D' &&
            candidate->signature[3] == ' ' &&
            candidate->signature[4] == 'P' &&
            candidate->signature[5] == 'T' &&
            candidate->signature[6] == 'R' &&
            candidate->signature[7] == ' ') {
            if (acpi_checksum(candidate, 20)) {
                return candidate;
            }
        }
    }
    return (const acpi_rsdp_t *)0;
}

static const acpi_madt_t *find_madt(const acpi_rsdp_t *rsdp) {
    if (!rsdp || !rsdp->rsdt_address) return (const acpi_madt_t *)0;
    const acpi_sdt_header_t *rsdt = (const acpi_sdt_header_t *)(unsigned long)rsdp->rsdt_address;
    if (!rsdt || !acpi_checksum(rsdt, rsdt->length)) return (const acpi_madt_t *)0;

    int entries = (int)(rsdt->length - sizeof(acpi_sdt_header_t)) / 4;
    const u32 *tables = (const u32 *)(rsdt + 1);

    for (int i = 0; i < entries; i++) {
        const acpi_sdt_header_t *hdr = (const acpi_sdt_header_t *)(unsigned long)tables[i];
        if (hdr && hdr->signature[0] == 'A' &&
            hdr->signature[1] == 'P' &&
            hdr->signature[2] == 'I' &&
            hdr->signature[3] == 'C') {
            if (acpi_checksum(hdr, hdr->length)) {
                return (const acpi_madt_t *)hdr;
            }
        }
    }
    return (const acpi_madt_t *)0;
}

void cpu_smp_init(void) {
    serial_write("[smp] Initializing Multi-Processor & APIC Subsystem...\n");
    s_core_count = 0;
    s_bsp_id = 0;

    /* Read APIC Base MSR (0x1B) */
    u64 apic_msr = rdmsr(0x1B);
    s_lapic_base = apic_msr & 0xFFFFF000ULL;
    int bsp_flag = (apic_msr & (1ULL << 8)) ? 1 : 0;
    (void)bsp_flag;

    serial_write("[smp] Local APIC physical base: 0x");
    serial_write_hex((u32)s_lapic_base);
    serial_write("\n");

    /* Find ACPI tables */
    const acpi_rsdp_t *rsdp = find_rsdp();
    const acpi_madt_t *madt = (void*)0;

    if (rsdp) {
        serial_write("[smp] ACPI RSDP found (OEM: ");
        char oem[7];
        for (int k = 0; k < 6; k++) oem[k] = rsdp->oem_id[k];
        oem[6] = 0;
        serial_write(oem);
        serial_write(")\n");
        madt = find_madt(rsdp);
    }

    if (madt) {
        serial_write("[smp] ACPI MADT (APIC table) verified. Parsing processor topology...\n");
        if (madt->local_apic_address) {
            s_lapic_base = (u64)madt->local_apic_address;
        }

        const u8 *ptr = (const u8 *)(madt + 1);
        const u8 *end = ((const u8 *)madt) + madt->header.length;

        while (ptr < end && s_core_count < SMP_MAX_CORES) {
            const acpi_madt_entry_header_t *entry = (const acpi_madt_entry_header_t *)ptr;
            if (entry->length == 0) break;

            if (entry->type == 0) {
                /* Type 0: Processor Local APIC */
                const acpi_madt_local_apic_t *lapic = (const acpi_madt_local_apic_t *)entry;
                /* Flag bit 0: Processor Enabled; bit 1: Online Capable */
                if (lapic->flags & 1) {
                    smp_cpu_core_t *c = &s_cores[s_core_count];
                    c->core_id = s_core_count;
                    c->apic_id = lapic->apic_id;
                    c->is_bsp = (s_core_count == 0) ? 1 : 0;
                    c->enabled = 1;
                    c->ticks = 0;
                    if (c->is_bsp) {
                        s_bsp_id = c->apic_id;
                        c->name[0] = 'C'; c->name[1] = 'P'; c->name[2] = 'U'; c->name[3] = ' ';
                        c->name[4] = '0'; c->name[5] = ' '; c->name[6] = '('; c->name[7] = 'B';
                        c->name[8] = 'S'; c->name[9] = 'P'; c->name[10] = ')'; c->name[11] = 0;
                    } else {
                        c->name[0] = 'C'; c->name[1] = 'P'; c->name[2] = 'U'; c->name[3] = ' ';
                        c->name[4] = '0' + (char)s_core_count; c->name[5] = ' ';
                        c->name[6] = '('; c->name[7] = 'A'; c->name[8] = 'P'; c->name[9] = ')'; c->name[10] = 0;
                    }
                    s_core_count++;
                }
            } else if (entry->type == 1) {
                /* Type 1: I/O APIC */
                const acpi_madt_io_apic_t *ioapic = (const acpi_madt_io_apic_t *)entry;
                s_io_apic_addr = ioapic->io_apic_address;
                serial_write("[smp] I/O APIC id=");
                serial_write_hex((u32)ioapic->io_apic_id);
                serial_write(" address=0x");
                serial_write_hex(s_io_apic_addr);
                serial_write("\n");
            }

            ptr += entry->length;
        }
    }

    /* Fallback if no MADT found (minimum 1 BSP core) */
    if (s_core_count == 0) {
        s_core_count = 1;
        s_bsp_id = 0;
        s_cores[0].core_id = 0;
        s_cores[0].apic_id = 0;
        s_cores[0].is_bsp = 1;
        s_cores[0].enabled = 1;
        s_cores[0].name[0] = 'C'; s_cores[0].name[1] = 'P'; s_cores[0].name[2] = 'U';
        s_cores[0].name[3] = ' '; s_cores[0].name[4] = '0'; s_cores[0].name[5] = 0;
    }

    s_smp_initialized = 1;
    cpu_smp_dump_diagnostics();
}

int cpu_smp_get_core_count(void) {
    return s_core_count > 0 ? s_core_count : 1;
}

int cpu_smp_get_bsp_id(void) {
    return s_bsp_id;
}

u64 cpu_smp_get_lapic_base(void) {
    return s_lapic_base;
}

const smp_cpu_core_t *cpu_smp_get_core(int idx) {
    if (idx >= 0 && idx < s_core_count) {
        return &s_cores[idx];
    }
    return (const smp_cpu_core_t *)0;
}

void cpu_smp_dump_diagnostics(void) {
    serial_write("[smp] Multi-Core Topology Detected: ");
    serial_write_hex((u32)s_core_count);
    serial_write(" processor core(s) available\n");

    for (int i = 0; i < s_core_count; i++) {
        serial_write("  -> ");
        serial_write(s_cores[i].name);
        serial_write(" | APIC ID: ");
        serial_write_hex((u32)s_cores[i].apic_id);
        serial_write(s_cores[i].is_bsp ? " [Active Primary]\n" : " [Ready for IPI]\n");
    }
}
