#include "pci.h"
#include "serial.h"

typedef unsigned int u32;

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_MAX_DISPLAY_DEVICES 8

static pci_device_t pci_display_devices[PCI_MAX_DISPLAY_DEVICES];
static int pci_display_count;

static u32 pci_config_address(u8 bus, u8 slot, u8 function, u8 offset) {
    return 0x80000000u |
           ((u32)bus << 16) |
           ((u32)(slot & 0x1F) << 11) |
           ((u32)(function & 0x07) << 8) |
           (offset & 0xFC);
}

static void outl(u16 port, u32 value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static u32 inl(u16 port) {
    u32 value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static u32 pci_read32(u8 bus, u8 slot, u8 function, u8 offset) {
    outl(PCI_CONFIG_ADDRESS, pci_config_address(bus, slot, function, offset));
    return inl(PCI_CONFIG_DATA);
}

static u16 pci_read16(u8 bus, u8 slot, u8 function, u8 offset) {
    u32 value = pci_read32(bus, slot, function, offset);
    return (u16)((value >> ((offset & 2) * 8)) & 0xFFFF);
}

static u8 pci_read8(u8 bus, u8 slot, u8 function, u8 offset) {
    u32 value = pci_read32(bus, slot, function, offset);
    return (u8)((value >> ((offset & 3) * 8)) & 0xFF);
}

static int pci_is_multifunction(u8 bus, u8 slot) {
    return (pci_read8(bus, slot, 0, 0x0E) & 0x80) != 0;
}

static const char* pci_vendor_name(u16 vendor_id) {
    switch (vendor_id) {
        case 0x1234: return "Bochs/QEMU";
        case 0x15AD: return "VMware";
        case 0x8086: return "Intel";
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x1022: return "AMD";
        case 0x1AF4: return "VirtIO";
        case 0x1414: return "Microsoft";
        case 0x5333: return "S3";
        case 0x80EE: return "VirtualBox";
        default: return "Unknown";
    }
}

static const char* pci_display_kind(u8 subclass) {
    switch (subclass) {
        case 0x00: return "VGA";
        case 0x01: return "XGA";
        case 0x02: return "3D";
        case 0x80: return "Display";
        default: return "Display";
    }
}

static void append_text(char* out, u32 out_size, const char* text) {
    u32 i = 0, j = 0;
    if (!out || !out_size) return;
    while (i < out_size && out[i]) i++;
    while (text[j] && i + 1 < out_size) out[i++] = text[j++];
    out[i < out_size ? i : (out_size - 1)] = 0;
}

static void append_hex16(char* out, u32 out_size, u16 value) {
    static const char hex[] = "0123456789ABCDEF";
    char tmp[5];
    tmp[0] = hex[(value >> 12) & 0xF];
    tmp[1] = hex[(value >> 8) & 0xF];
    tmp[2] = hex[(value >> 4) & 0xF];
    tmp[3] = hex[value & 0xF];
    tmp[4] = 0;
    append_text(out, out_size, tmp);
}

#define PCI_MAX_DEVICES 64
static pci_device_t pci_all_devices[PCI_MAX_DEVICES];
static int pci_all_device_count;

static void pci_record_display(u8 bus, u8 slot, u8 function) {
    pci_device_t* dev;
    if (pci_display_count >= PCI_MAX_DISPLAY_DEVICES) return;
    dev = &pci_display_devices[pci_display_count++];
    dev->valid = 1;
    dev->bus = bus;
    dev->slot = slot;
    dev->function = function;
    dev->vendor_id = pci_read16(bus, slot, function, 0x00);
    dev->device_id = pci_read16(bus, slot, function, 0x02);
    dev->revision = pci_read8(bus, slot, function, 0x08);
    dev->prog_if = pci_read8(bus, slot, function, 0x09);
    dev->subclass = pci_read8(bus, slot, function, 0x0A);
    dev->class_code = pci_read8(bus, slot, function, 0x0B);
    dev->header_type = pci_read8(bus, slot, function, 0x0E);
}

void pci_init(void) {
    u16 bus;
    pci_display_count = 0;
    pci_all_device_count = 0;
    for (bus = 0; bus < 256; bus++) {
        u8 slot;
        for (slot = 0; slot < 32; slot++) {
            u16 vendor = pci_read16((u8)bus, slot, 0, 0x00);
            int functions = pci_is_multifunction((u8)bus, slot) ? 8 : 1;
            u8 function;
            if (vendor == 0xFFFF) continue;
            for (function = 0; function < (u8)functions; function++) {
                u8 class_code;
                u8 subclass;
                vendor = pci_read16((u8)bus, slot, function, 0x00);
                if (vendor == 0xFFFF) continue;
                class_code = pci_read8((u8)bus, slot, function, 0x0B);
                subclass = pci_read8((u8)bus, slot, function, 0x0A);

                if (pci_all_device_count < PCI_MAX_DEVICES) {
                    pci_device_t *d = &pci_all_devices[pci_all_device_count++];
                    d->valid = 1;
                    d->bus = (u8)bus;
                    d->slot = slot;
                    d->function = function;
                    d->vendor_id = vendor;
                    d->device_id = pci_read16((u8)bus, slot, function, 0x02);
                    d->revision = pci_read8((u8)bus, slot, function, 0x08);
                    d->prog_if = pci_read8((u8)bus, slot, function, 0x09);
                    d->subclass = subclass;
                    d->class_code = class_code;
                    d->header_type = pci_read8((u8)bus, slot, function, 0x0E);
                }

                if (class_code == 0x03) {
                    pci_record_display((u8)bus, slot, function);
                    serial_write("[pci] display adapter found vendor=");
                    serial_write_hex(vendor);
                    serial_write(" device=");
                    serial_write_hex(pci_read16((u8)bus, slot, function, 0x02));
                    serial_write(" class=");
                    serial_write_hex(class_code);
                    serial_write(" subclass=");
                    serial_write_hex(subclass);
                    serial_write("\n");
                }
            }
        }
    }
    serial_write("[pci] display adapter count=");
    serial_write_hex((u32)pci_display_count);
    serial_write(" all device count=");
    serial_write_hex((u32)pci_all_device_count);
    serial_write("\n");
}

const pci_device_t* pci_find_class(u8 class_code, u8 subclass) {
    int i;
    for (i = 0; i < pci_all_device_count; ++i) {
        if (pci_all_devices[i].class_code == class_code && pci_all_devices[i].subclass == subclass) {
            return &pci_all_devices[i];
        }
    }
    return 0;
}

int pci_has_display_adapter(void) {
    return pci_display_count > 0;
}

const pci_device_t* pci_primary_display_adapter(void) {
    if (pci_display_count <= 0) return 0;
    return &pci_display_devices[0];
}

int pci_display_adapter_count(void) {
    return pci_display_count;
}

const pci_device_t* pci_get_display_adapter(int index) {
    if (index < 0 || index >= pci_display_count) return 0;
    return &pci_display_devices[index];
}

void pci_describe_primary_display(char* out, u32 out_size) {
    const pci_device_t* dev = pci_primary_display_adapter();
    if (!out || !out_size) return;
    out[0] = 0;
    if (!dev) {
        append_text(out, out_size, "No PCI display adapter detected");
        return;
    }
    append_text(out, out_size, pci_vendor_name(dev->vendor_id));
    append_text(out, out_size, " ");
    append_text(out, out_size, pci_display_kind(dev->subclass));
    append_text(out, out_size, " [VID:");
    append_hex16(out, out_size, dev->vendor_id);
    append_text(out, out_size, " DID:");
    append_hex16(out, out_size, dev->device_id);
    append_text(out, out_size, "]");
}

void pci_debug_dump(void) {
    int i;
    for (i = 0; i < pci_display_count; i++) {
        serial_write("[pci] gpu ");
        serial_write_hex((u32)i);
        serial_write(": vendor=");
        serial_write_hex(pci_display_devices[i].vendor_id);
        serial_write(" device=");
        serial_write_hex(pci_display_devices[i].device_id);
        serial_write(" bus=");
        serial_write_hex(pci_display_devices[i].bus);
        serial_write(" slot=");
        serial_write_hex(pci_display_devices[i].slot);
        serial_write(" func=");
        serial_write_hex(pci_display_devices[i].function);
        serial_write("\n");
    }
}


const pci_device_t* pci_find_device(u16 vendor_id, u16 device_id) {
    int i;
    for (i = 0; i < pci_all_device_count; ++i) {
        if (pci_all_devices[i].vendor_id == vendor_id && pci_all_devices[i].device_id == device_id) {
            return &pci_all_devices[i];
        }
    }
    return 0;
}

u16 pci_config_read16(const pci_device_t* dev, u8 offset) {
    if (!dev) return 0xFFFFu;
    return pci_read16(dev->bus, dev->slot, dev->function, offset);
}

u32 pci_config_read32(const pci_device_t* dev, u8 offset) {
    if (!dev) return 0xFFFFFFFFu;
    return pci_read32(dev->bus, dev->slot, dev->function, offset);
}

void pci_config_write32(const pci_device_t* dev, u8 offset, u32 value) {
    if (!dev) return;
    outl(PCI_CONFIG_ADDRESS, pci_config_address(dev->bus, dev->slot, dev->function, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(const pci_device_t* dev, u8 offset, u16 value) {
    if (!dev) return;
    outl(PCI_CONFIG_ADDRESS, pci_config_address(dev->bus, dev->slot, dev->function, offset));
    outw((u16)(PCI_CONFIG_DATA + (offset & 2)), value);
}

void pci_enable_bus_master(const pci_device_t* dev) {
    if (!dev) return;
    u16 cmd = pci_config_read16(dev, 0x04);
    cmd |= 0x0007; /* Bit 0: IO space, Bit 1: Memory space, Bit 2: Bus Master */
    pci_config_write16(dev, 0x04, cmd);
}

u32 pci_read_bar32(const pci_device_t* dev, int bar_index) {
    if (!dev || bar_index < 0 || bar_index > 5) return 0;
    return pci_read32(dev->bus, dev->slot, dev->function, (u8)(0x10 + bar_index * 4));
}

u64 pci_read_bar64(const pci_device_t* dev, int bar_index) {
    if (!dev || bar_index < 0 || bar_index > 5) return 0;
    u32 low = pci_read32(dev->bus, dev->slot, dev->function, (u8)(0x10 + bar_index * 4));
    /* Check if Memory Space BAR and 64-bit (bits [2:1] == 0b10 -> 0x04) */
    if ((low & 0x01) == 0 && (low & 0x06) == 0x04 && bar_index < 5) {
        u32 high = pci_read32(dev->bus, dev->slot, dev->function, (u8)(0x14 + bar_index * 4));
        return ((u64)high << 32) | (u64)(low & ~0x0Fu);
    }
    return (low & 0x01) ? (u64)(low & ~0x03u) : (u64)(low & ~0x0Fu);
}

