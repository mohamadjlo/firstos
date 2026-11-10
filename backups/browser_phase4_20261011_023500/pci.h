#ifndef PCI_H
#define PCI_H

#include "io.h"

typedef unsigned int u32;

typedef struct {
    u8 valid;
    u8 bus;
    u8 slot;
    u8 function;
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 revision;
    u8 header_type;
} pci_device_t;

void pci_init(void);
int pci_has_display_adapter(void);
const pci_device_t* pci_primary_display_adapter(void);
int pci_display_adapter_count(void);
void pci_describe_primary_display(char* out, u32 out_size);
void pci_debug_dump(void);
u16 pci_config_read16(const pci_device_t* dev, u8 offset);
u32 pci_config_read32(const pci_device_t* dev, u8 offset);
void pci_config_write16(const pci_device_t* dev, u8 offset, u16 value);
void pci_config_write32(const pci_device_t* dev, u8 offset, u32 value);
void pci_enable_bus_master(const pci_device_t* dev);
u32 pci_read_bar32(const pci_device_t* dev, int bar_index);
u64 pci_read_bar64(const pci_device_t* dev, int bar_index);
const pci_device_t* pci_find_device(u16 vendor_id, u16 device_id);
const pci_device_t* pci_find_class(u8 class_code, u8 subclass);

#endif
