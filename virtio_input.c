#include "virtio_input.h"
#include "serial.h"
#include "pci.h"
#include "io.h"
#include "virtio_gpu.h"

#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_INPUT_DEVICE_ID 0x1052

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG 3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2
#define VRING_ALIGN 4096
#define INPUT_QUEUE_SIZE 64
#define INPUT_EVENT_BUFFERS 64

typedef struct {
    u8 cap_vndr;
    u8 cap_next;
    u8 cap_len;
    u8 cfg_type;
    u8 bar;
    u8 padding[3];
    u32 offset;
    u32 length;
} __attribute__((packed)) virtio_pci_cap_t;

typedef struct {
    virtio_pci_cap_t cap;
    u32 notify_off_multiplier;
} __attribute__((packed)) virtio_pci_notify_cap_t;

typedef struct {
    u32 device_feature_select;
    u32 device_feature;
    u32 guest_feature_select;
    u32 guest_feature;
    u16 msix_config;
    u16 num_queues;
    u8 device_status;
    u8 config_generation;
    u16 queue_select;
    u16 queue_size;
    u16 queue_msix_vector;
    u16 queue_enable;
    u16 queue_notify_off;
    u64 queue_desc;
    u64 queue_driver;
    u64 queue_device;
} __attribute__((packed)) virtio_pci_common_cfg_t;

typedef struct {
    unsigned long long addr;
    u32 len;
    u16 flags;
    u16 next;
} __attribute__((packed)) vring_desc_t;

typedef struct {
    u32 id;
    u32 len;
} __attribute__((packed)) vring_used_elem_t;

static virtio_input_info_t input;
static const pci_device_t *input_dev;
static uintptr_t mmio_base;
static uintptr_t notify_base;
static uintptr_t isr_base;
static uintptr_t device_cfg_base;
static u32 notify_off_multiplier;
static volatile virtio_pci_common_cfg_t *common_cfg;
static u16 queue_size;
static int ready;
static u8 event_queue_bytes[VRING_TOTAL_BYTES] __attribute__((aligned(VRING_ALIGN)));
static virtio_input_event_t event_buffers[INPUT_EVENT_BUFFERS] __attribute__((aligned(VRING_ALIGN)));
static u32 vring_avail_offset;
static u32 vring_used_offset;
static u32 vring_total_bytes;
static u16 next_event_idx;

static inline u8 mmio_read8(uintptr_t addr) { return *(volatile u8*)addr; }
static inline u16 mmio_read16(uintptr_t addr) { return *(volatile u16*)addr; }
static inline u32 mmio_read32(uintptr_t addr) { return *(volatile u32*)addr; }
static inline void mmio_write8(uintptr_t addr, u8 v) { *(volatile u8*)addr = v; }
static inline void mmio_write16(uintptr_t addr, u16 v) { *(volatile u16*)addr = v; }
static inline void mmio_write32(uintptr_t addr, u32 v) { *(volatile u32*)addr = v; }
static inline void mmio_write64(uintptr_t addr, u64 v) { *(volatile u64*)addr = v; }

static uintptr_t pci_bar_mmio(const pci_device_t *dev, int index) {
    u64 base = pci_read_bar64(dev, index);
    if ((base & 1ULL) == 0) return (uintptr_t)(base & ~0xFULL);
    return 0;
}

static u8 pci_config_read8_local(const pci_device_t *dev, u8 offset) {
    u32 val = pci_config_read32(dev, (u8)(offset & ~3u));
    return (u8)((val >> ((offset & 3u) * 8u)) & 0xFFu);
}

static int find_pci_caps(const pci_device_t *dev) {
    u8 cap_ptr = (u8)(pci_config_read8_local(dev, 0x34) & 0xFCu);
    mmio_base = 0; notify_base = 0; isr_base = 0; device_cfg_base = 0;
    notify_off_multiplier = 1;
    while (cap_ptr != 0) {
        u8 cap_id = pci_config_read8_local(dev, cap_ptr);
        u8 cap_next = pci_config_read8_local(dev, cap_ptr + 1);
        if (cap_id == VIRTIO_PCI_CAP_VENDOR) {
            virtio_pci_cap_t cap;
            unsigned int i; for (i = 0; i < sizeof(cap); i++) ((u8*)&cap)[i] = pci_config_read8_local(dev, (u8)(cap_ptr + i));
            if (cap.cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
                mmio_base = pci_bar_mmio(dev, cap.bar) + cap.offset;
                common_cfg = (volatile virtio_pci_common_cfg_t*)mmio_base;
            } else if (cap.cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                virtio_pci_notify_cap_t nc;
                for (i = 0; i < sizeof(nc); i++) ((u8*)&nc)[i] = pci_config_read8_local(dev, (u8)(cap_ptr + i));
                notify_base = pci_bar_mmio(dev, nc.cap.bar) + nc.cap.offset;
                notify_off_multiplier = nc.notify_off_multiplier;
            } else if (cap.cfg_type == VIRTIO_PCI_CAP_ISR_CFG) {
                virtio_pci_cap_t cap2;
                for (i = 0; i < sizeof(cap2); i++) ((u8*)&cap2)[i] = pci_config_read8_local(dev, (u8)(cap_ptr + i));
                isr_base = pci_bar_mmio(dev, cap2.bar) + cap2.offset;
            } else if (cap.cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
                virtio_pci_cap_t cap2;
                for (i = 0; i < sizeof(cap2); i++) ((u8*)&cap2)[i] = pci_config_read8_local(dev, (u8)(cap_ptr + i));
                device_cfg_base = pci_bar_mmio(dev, cap2.bar) + cap2.offset;
            }
        }
        cap_ptr = cap_next;
    }
    return (mmio_base != 0 && notify_base != 0 && isr_base != 0);
}

static inline vring_desc_t* event_queue_desc(void) { return (vring_desc_t*)(void*)event_queue_bytes; }
static inline u16* event_queue_avail_flags(void) { return (u16*)(void*)(event_queue_bytes + vring_avail_offset); }
static inline u16* event_queue_avail_idx(void) { return (u16*)(void*)(event_queue_bytes + vring_avail_offset + sizeof(u16)); }
static inline u16* event_queue_avail_ring(void) { return (u16*)(void*)(event_queue_bytes + vring_avail_offset + sizeof(u16) * 2); }
static inline u16* event_queue_used_idx(void) { return (u16*)(void*)(event_queue_bytes + vring_used_offset); }
static inline vring_used_elem_t* event_queue_used_ring(void) { return (vring_used_elem_t*)(void*)(event_queue_bytes + vring_used_offset + sizeof(u16) * 2); }

static int event_queue_init(void) {
    vring_total_bytes = VRING_TOTAL_BYTES;
    vring_avail_offset = VRING_AVAIL_OFFSET;
    vring_used_offset = VRING_USED_OFFSET;
    uintptr_t paddr = (uintptr_t)event_queue_bytes;
    mmio_write16((uintptr_t)&common_cfg->queue_select, 0);
    if (mmio_read16((uintptr_t)&common_cfg->queue_size) < INPUT_QUEUE_SIZE) { serial_write("[virtio-input] queue too small\n"); return 0; }
    mmio_write16((uintptr_t)&common_cfg->queue_size, INPUT_QUEUE_SIZE);
    mmio_write16((uintptr_t)&common_cfg->queue_msix_vector, 0xFFFF);
    mmio_write64((uintptr_t)&common_cfg->queue_desc, (u64)paddr);
    mmio_write64((uintptr_t)&common_cfg->queue_driver, (u64)(paddr + vring_avail_offset));
    mmio_write64((uintptr_t)&common_cfg->queue_device, (u64)(paddr + vring_used_offset));
    mmio_write16((uintptr_t)&common_cfg->queue_enable, 1);
    queue_size = INPUT_QUEUE_SIZE;
    int i; for (i = 0; i < INPUT_EVENT_BUFFERS; i++) {
        event_queue_desc()[i].addr = (u64)(uintptr_t)&event_buffers[i];
        event_queue_desc()[i].len = sizeof(virtio_input_event_t);
        event_queue_desc()[i].flags = VRING_DESC_F_WRITE;
        event_queue_desc()[i].next = 0;
    }
    *event_queue_avail_flags() = 0;
    *event_queue_avail_idx() = INPUT_EVENT_BUFFERS;
    for (i = 0; i < INPUT_EVENT_BUFFERS; i++) event_queue_avail_ring()[i] = (u16)i;
    __sync_synchronize();
    return 1;
}

static void notify_queue(u16 queue_index) {
    u16 notify_off = mmio_read16((uintptr_t)&common_cfg->queue_notify_off);
    mmio_write16(notify_base + (uintptr_t)notify_off * notify_off_multiplier, queue_index);
}

static int input_config_read(u8 select, u8 subsel, void *dst, u32 len) {
    volatile virtio_input_config_t *cfg = (volatile virtio_input_config_t*)device_cfg_base;
    mmio_write8((uintptr_t)&cfg->select, select);
    mmio_write8((uintptr_t)&cfg->subsel, subsel);
    u32 size = mmio_read32((uintptr_t)&cfg->size);
    if (size == 0 || size > sizeof(cfg->data)) return 0;
    u32 i; for (i = 0; i < size && i < len; i++) ((u8*)dst)[i] = mmio_read8((uintptr_t)&cfg->data[i]);
    return (int)size;
}

static int input_detect_capabilities(void) {
    u8 ev_bits[64];
    virtio_input_devids_t devs;
    char name_buf[64];
    unsigned int i;
    input.is_mouse = 0; input.is_tablet = 0;
    if ((unsigned int)input_config_read(VIRTIO_INPUT_CFG_ID_DEVIDS, 0, &devs, sizeof(devs)) >= sizeof(devs)) {
        serial_write("[virtio-input] device IDs: bus=");
        serial_write_hex(devs.bustype);
        serial_write(" vendor="); serial_write_hex(devs.vendor);
        serial_write(" product="); serial_write_hex(devs.product);
        serial_write("\n");
    }
    if (input_config_read(VIRTIO_INPUT_CFG_ID_NAME, 0, name_buf, sizeof(name_buf) - 1) > 0) {
        name_buf[sizeof(name_buf) - 1] = 0;
        serial_write("[virtio-input] name=");
        int j; for (j = 0; j < 32 && name_buf[j]; j++) serial_write_char(name_buf[j]);
        serial_write("\n");
    }
    for (i = 0; i < sizeof(ev_bits); i++) ev_bits[i] = 0;
    if (input_config_read(VIRTIO_INPUT_CFG_EV_BITS, EV_REL, ev_bits, sizeof(ev_bits)) > 0) {
        if ((ev_bits[REL_X / 8] & (1 << (REL_X % 8))) && (ev_bits[REL_Y / 8] & (1 << (REL_Y % 8)))) {
            input.is_mouse = 1;
            serial_write("[virtio-input] relative mouse detected\n");
        }
    }
    for (i = 0; i < sizeof(ev_bits); i++) ev_bits[i] = 0;
    if (input_config_read(VIRTIO_INPUT_CFG_EV_BITS, EV_ABS, ev_bits, sizeof(ev_bits)) > 0) {
        if ((ev_bits[ABS_X / 8] & (1 << (ABS_X % 8))) && (ev_bits[ABS_Y / 8] & (1 << (ABS_Y % 8)))) {
            input.is_tablet = 1;
            serial_write("[virtio-input] absolute tablet detected\n");
        }
    }
    if (input.is_tablet) {
        virtio_input_absinfo_t absx, absy;
        if ((unsigned int)input_config_read(VIRTIO_INPUT_CFG_ABS_INFO, ABS_X, &absx, sizeof(absx)) >= sizeof(absx)) {
            input.abs_x_min = absx.min; input.abs_x_max = absx.max;
        }
        if ((unsigned int)input_config_read(VIRTIO_INPUT_CFG_ABS_INFO, ABS_Y, &absy, sizeof(absy)) >= sizeof(absy)) {
            input.abs_y_min = absy.min; input.abs_y_max = absy.max;
        }
    }
    return (input.is_mouse || input.is_tablet) ? 1 : 0;
}

void virtio_input_probe(void) {
    const pci_device_t *dev = pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_INPUT_DEVICE_ID);
    input.present = 0; input.initialized = 0; input_dev = 0;
    if (!dev) { serial_write("[virtio-input] no device found\n"); return; }
    input.bus = dev->bus; input.slot = dev->slot; input.function = dev->function;
    input.vendor_id = dev->vendor_id; input.device_id = dev->device_id;
    input.bar0 = pci_config_read32(dev, 0x10); input.bar1 = pci_config_read32(dev, 0x14);
    if (!find_pci_caps(dev)) { serial_write("[virtio-input] no modern caps\n"); return; }
    input_dev = dev; input.present = 1;
    serial_write("[virtio-input] found at ");
    serial_write_hex(input.bus); serial_write_char(':');
    serial_write_hex(input.slot); serial_write_char(':');
    serial_write_hex(input.function);
    serial_write("\n");
}

int virtio_input_init(void) {
    if (!input.present || !input_dev) return 0;
    serial_write("[virtio-input] initializing\n");
    if (!event_queue_init()) { serial_write("[virtio-input] queue init failed\n"); return 0; }
    if (!input_detect_capabilities()) { serial_write("[virtio-input] no mouse/tablet caps\n"); return 0; }
    next_event_idx = 0;
    ready = 1;
    serial_write("[virtio-input] initialized\n");
    return 1;
}

const virtio_input_info_t* virtio_input_info(void) { return &input; }
int virtio_input_available(void) { return ready; }

int virtio_input_poll_event(virtio_input_event_t *evt) {
    if (!ready || !evt) return 0;
    u16 used = *event_queue_used_idx();
    if (used <= next_event_idx) return 0;
    vring_used_elem_t *ue = &event_queue_used_ring()[next_event_idx % queue_size];
    if (ue->len < sizeof(virtio_input_event_t)) return 0;
    u32 desc_idx = ue->id;
    if (desc_idx >= INPUT_EVENT_BUFFERS) return 0;
    evt->type = event_buffers[desc_idx].type;
    evt->code = event_buffers[desc_idx].code;
    evt->value = event_buffers[desc_idx].value;
    next_event_idx++;
    return 1;
}

void virtio_input_requeue_event(void) {
    if (!ready) return;
    u16 avail = *event_queue_avail_idx();
    u16 idx = (avail) % queue_size;
    event_queue_avail_ring()[idx] = (u16)(avail % INPUT_EVENT_BUFFERS);
    *event_queue_avail_idx() = (u16)(avail + 1);
    __sync_synchronize();
    notify_queue(0);
}
