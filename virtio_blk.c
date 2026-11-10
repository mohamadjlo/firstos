#include "virtio_blk.h"
#include "pci.h"
#include "serial.h"

#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_BLK_DEVICE_ID_LEGACY 0x1001
#define VIRTIO_PCI_HOST_FEATURES 0x00
#define VIRTIO_PCI_GUEST_FEATURES 0x04
#define VIRTIO_PCI_QUEUE_PFN 0x08
#define VIRTIO_PCI_QUEUE_NUM 0x0C
#define VIRTIO_PCI_QUEUE_SEL 0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10
#define VIRTIO_PCI_STATUS 0x12
#define VIRTIO_PCI_ISR 0x13
#define VIRTIO_BLK_SECTOR_SIZE 512
#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2
#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
#define STATUS_ACK 1
#define STATUS_DRIVER 2
#define STATUS_DRIVER_OK 4
#define STATUS_FAILED 128
#define DRIVER_QUEUE_SIZE 256
#define VRING_ALIGN 4096
#define VRING_DESC_BYTES_MAX (sizeof(vring_desc_t) * DRIVER_QUEUE_SIZE)
#define VRING_AVAIL_BYTES_MAX (sizeof(u16) * (2 + DRIVER_QUEUE_SIZE))
#define VRING_USED_BYTES_MAX (sizeof(u16) * 2 + sizeof(vring_used_elem_t) * DRIVER_QUEUE_SIZE)
#define VRING_AVAIL_OFFSET VRING_DESC_BYTES_MAX
#define VRING_USED_OFFSET (((VRING_AVAIL_OFFSET + VRING_AVAIL_BYTES_MAX) + (VRING_ALIGN - 1)) & ~(VRING_ALIGN - 1))
#define VRING_TOTAL_BYTES (VRING_USED_OFFSET + VRING_USED_BYTES_MAX)

typedef struct {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
} __attribute__((packed)) vring_desc_t;

typedef struct {
    u32 id;
    u32 len;
} __attribute__((packed)) vring_used_elem_t;

typedef struct {
    u32 type;
    u32 reserved;
    u64 sector;
} __attribute__((packed)) virtio_blk_req_hdr_local_t;

static volatile u8 queue_bytes[VRING_TOTAL_BYTES] __attribute__((aligned(VRING_ALIGN)));
static volatile virtio_blk_req_hdr_local_t req_hdr;
static volatile u8 req_status;
static const pci_device_t *blk_dev;
static u32 io_base;
static u16 queue_size;
static int ready;
static int diag_dumped;
static u32 vring_avail_offset;
static u32 vring_used_offset;
static u32 vring_total_bytes;

static inline void io_wait_spin(void) { __asm__ volatile("pause" ::: "memory"); }
static inline u8 v_in8(u16 off) { return inb((u16)(io_base + off)); }
static inline u16 v_in16(u16 off) { u16 v; __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"((u16)(io_base + off))); return v; }
static inline u32 v_in32(u16 off) { u32 v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"((u16)(io_base + off))); return v; }
static inline void v_out8(u16 off, u8 v) { outb((u16)(io_base + off), v); }
static inline void v_out16(u16 off, u16 v) { __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"((u16)(io_base + off))); }
static inline void v_out32(u16 off, u32 v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"((u16)(io_base + off))); }

static u32 pci_bar0_io(const pci_device_t *dev) {
    u32 bar = pci_read_bar32(dev, 0);
    if (!(bar & 1u)) return 0;
    return bar & ~3u;
}

static volatile vring_desc_t *queue_desc(void) { return (volatile vring_desc_t*)(volatile void*)queue_bytes; }
static volatile u16 *queue_avail_flags_ptr(void) { return (volatile u16*)(volatile void*)(queue_bytes + vring_avail_offset); }
static volatile u16 *queue_avail_idx_ptr(void) { return (volatile u16*)(volatile void*)(queue_bytes + vring_avail_offset + sizeof(u16)); }
static volatile u16 *queue_avail_ring(void) { return (volatile u16*)(volatile void*)(queue_bytes + vring_avail_offset + sizeof(u16) * 2); }
static volatile u16 *queue_used_flags_ptr(void) { return (volatile u16*)(volatile void*)(queue_bytes + vring_used_offset); }
static volatile u16 *queue_used_idx_ptr(void) { return (volatile u16*)(volatile void*)(queue_bytes + vring_used_offset + sizeof(u16)); }
static volatile vring_used_elem_t *queue_used_ring(void) { return (volatile vring_used_elem_t*)(volatile void*)(queue_bytes + vring_used_offset + sizeof(u16) * 2); }

static void queue_layout_init(u16 qsz) {
    u32 avail_bytes = (u32)(sizeof(u16) * (2 + qsz));
    u32 used_bytes = (u32)(sizeof(u16) * 2 + sizeof(vring_used_elem_t) * qsz);
    vring_avail_offset = (u32)(sizeof(vring_desc_t) * qsz);
    vring_used_offset = (vring_avail_offset + avail_bytes + (VRING_ALIGN - 1)) & ~(VRING_ALIGN - 1);
    vring_total_bytes = vring_used_offset + used_bytes;
}

static void queue_reset_memory(void) {
    u8 *p = (u8*)queue_bytes;
    u32 i;
    for (i = 0; i < vring_total_bytes; ++i) p[i] = 0;
    req_status = 0xFF;
}

static void virtio_blk_dump_diag(const char *tag) {
    if (!blk_dev) return;
    serial_write("[virtio-blk] diag tag=");
    serial_write(tag);
    serial_write("\n");
    serial_write("[virtio-blk] diag io_base=");
    serial_write_hex(io_base);
    serial_write(" queue_size=");
    serial_write_hex(queue_size);
    serial_write(" ready=");
    serial_write_hex((u32)ready);
    serial_write("\n");
    serial_write("[virtio-blk] diag queue_bytes=");
    serial_write_hex((u32)(unsigned long)queue_bytes);
    serial_write(" desc=");
    serial_write_hex((u32)(unsigned long)queue_desc());
    serial_write(" avail=");
    serial_write_hex((u32)(unsigned long)queue_avail_flags_ptr());
    serial_write(" used=");
    serial_write_hex((u32)(unsigned long)queue_used_flags_ptr());
    serial_write("\n");
    serial_write("[virtio-blk] diag vring bytes total=");
    serial_write_hex(vring_total_bytes);
    serial_write(" avail_off=");
    serial_write_hex(vring_avail_offset);
    serial_write(" used_off=");
    serial_write_hex(vring_used_offset);
    serial_write("\n");
    serial_write("[virtio-blk] diag pci status=");
    serial_write_hex((u32)pci_config_read16(blk_dev, 0x06));
    serial_write(" command=");
    serial_write_hex((u32)pci_config_read16(blk_dev, 0x04));
    serial_write("\n");
    serial_write("[virtio-blk] diag queue pfn reg=");
    serial_write_hex(v_in32(VIRTIO_PCI_QUEUE_PFN));
    serial_write(" isr=");
    serial_write_hex(v_in8(VIRTIO_PCI_ISR));
    serial_write(" status=");
    serial_write_hex(v_in8(VIRTIO_PCI_STATUS));
    serial_write("\n");
}

static int virtio_blk_submit(u32 type, u32 sector, void *buffer, u32 sector_count) {
    volatile vring_desc_t *desc;
    volatile u16 *avail_flags;
    volatile u16 *avail_idx;
    volatile u16 *avail_ring;
    volatile u16 *used_idx;
    u16 head = 0;
    u16 data = 1;
    u16 status = 2;
    u32 total = sector_count * VIRTIO_BLK_SECTOR_SIZE;
    u32 spins = 0;
    u32 paddr_req_hdr;
    u32 paddr_req_status;
    u32 paddr_buffer;
    u16 start_idx;

    if (!ready || !buffer || sector_count == 0) return 0;
    if (queue_size < 3) return 0;

    desc = queue_desc();
    avail_flags = queue_avail_flags_ptr();
    avail_idx = queue_avail_idx_ptr();
    avail_ring = queue_avail_ring();
    used_idx = queue_used_idx_ptr();

    paddr_req_hdr = (u32)(unsigned long)&req_hdr;
    paddr_req_status = (u32)(unsigned long)&req_status;
    paddr_buffer = (u32)(unsigned long)buffer;

    v_out16(VIRTIO_PCI_QUEUE_SEL, 0);

    __sync_synchronize();
    __asm__ volatile("" ::: "memory");
    if (*used_idx != *avail_idx) {
        serial_write("[virtio-blk] queue not idle before submit\n");
        serial_write(" used.idx=");
        serial_write_hex(*used_idx);
        serial_write(" avail.idx=");
        serial_write_hex(*avail_idx);
        serial_write("\n");
        return 0;
    }

    start_idx = *avail_idx;
    req_status = 0xFF;
    *avail_flags = 0;

    req_hdr.type = type;
    req_hdr.reserved = 0;
    req_hdr.sector = (u64)sector;

    desc[head].addr = (u64)paddr_req_hdr;
    desc[head].len = sizeof(req_hdr);
    desc[head].flags = VRING_DESC_F_NEXT;
    desc[head].next = data;

    desc[data].addr = (u64)paddr_buffer;
    desc[data].len = total;
    desc[data].flags = VRING_DESC_F_NEXT | ((type == VIRTIO_BLK_T_IN) ? VRING_DESC_F_WRITE : 0);
    desc[data].next = status;

    desc[status].addr = (u64)paddr_req_status;
    desc[status].len = 1;
    desc[status].flags = VRING_DESC_F_WRITE;
    desc[status].next = 0;

    __sync_synchronize();
    avail_ring[start_idx % queue_size] = head;
    __sync_synchronize();
    *avail_idx = (u16)(start_idx + 1);
    __sync_synchronize();
    v_out16(VIRTIO_PCI_QUEUE_NOTIFY, 0);
    __sync_synchronize();
    __asm__ volatile("" ::: "memory");

    while (spins++ < 1000000u) {
        __sync_synchronize();
        __asm__ volatile("" ::: "memory");
        if (*used_idx == *avail_idx) break;
        io_wait_spin();
    }
    __sync_synchronize();
    __asm__ volatile("" ::: "memory");
    if (*used_idx != *avail_idx) {
        serial_write("[virtio-blk] simple request timeout\n");
        if (!diag_dumped) {
            diag_dumped = 1;
            virtio_blk_dump_diag("simple-timeout");
        }
        return 0;
    }
    if (req_status != 0) {
        serial_write("[virtio-blk] simple request failed status=");
        serial_write_hex(req_status);
        serial_write("\n");
        return 0;
    }
    return 1;
}

int virtio_blk_init(void) {
    u32 qsz;
    u32 pfn;
    u32 host_features;
    u32 queue_pfn_before;

    blk_dev = pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_BLK_DEVICE_ID_LEGACY);
    ready = 0;
    diag_dumped = 0;
    if (!blk_dev) {
        serial_write("[virtio-blk] legacy pci device not found\n");
        return 0;
    }
    pci_enable_bus_master(blk_dev);
    io_base = pci_bar0_io(blk_dev);
    serial_write("[virtio-blk] pci bdf=");
    serial_write_hex((u32)blk_dev->bus);
    serial_write(":");
    serial_write_hex((u32)blk_dev->slot);
    serial_write(":");
    serial_write_hex((u32)blk_dev->function);
    serial_write(" vendor=");
    serial_write_hex((u32)blk_dev->vendor_id);
    serial_write(" device=");
    serial_write_hex((u32)blk_dev->device_id);
    serial_write("\n");
    serial_write("[virtio-blk] pci command=");
    serial_write_hex((u32)pci_config_read16(blk_dev, 0x04));
    serial_write(" status=");
    serial_write_hex((u32)pci_config_read16(blk_dev, 0x06));
    serial_write(" revision=");
    serial_write_hex((u32)blk_dev->revision);
    serial_write(" prog_if=");
    serial_write_hex((u32)blk_dev->prog_if);
    serial_write("\n");
    serial_write("[virtio-blk] pci bar0 raw=");
    serial_write_hex(pci_read_bar32(blk_dev, 0));
    serial_write(" bar1 raw=");
    serial_write_hex(pci_read_bar32(blk_dev, 1));
    serial_write(" irq_line=");
    serial_write_hex((u32)pci_config_read16(blk_dev, 0x3C) & 0xFFu);
    serial_write(" irq_pin=");
    serial_write_hex(((u32)pci_config_read16(blk_dev, 0x3C) >> 8) & 0xFFu);
    serial_write("\n");
    if (!io_base) {
        serial_write("[virtio-blk] invalid io BAR\n");
        return 0;
    }

    v_out8(VIRTIO_PCI_STATUS, 0);
    serial_write("[virtio-blk] status reset=");
    serial_write_hex(v_in8(VIRTIO_PCI_STATUS));
    serial_write("\n");
    v_out8(VIRTIO_PCI_STATUS, STATUS_ACK);
    serial_write("[virtio-blk] status ack=");
    serial_write_hex(v_in8(VIRTIO_PCI_STATUS));
    serial_write("\n");
    v_out8(VIRTIO_PCI_STATUS, STATUS_ACK | STATUS_DRIVER);
    serial_write("[virtio-blk] status driver=");
    serial_write_hex(v_in8(VIRTIO_PCI_STATUS));
    serial_write("\n");

    host_features = v_in32(VIRTIO_PCI_HOST_FEATURES);
    serial_write("[virtio-blk] host features=");
    serial_write_hex(host_features);
    serial_write("\n");
    v_out32(VIRTIO_PCI_GUEST_FEATURES, 0);
    serial_write("[virtio-blk] guest features=");
    serial_write_hex(v_in32(VIRTIO_PCI_GUEST_FEATURES));
    serial_write("\n");

    v_out16(VIRTIO_PCI_QUEUE_SEL, 0);
    qsz = v_in16(VIRTIO_PCI_QUEUE_NUM);
    if (qsz == 0) {
        serial_write("[virtio-blk] queue unavailable\n");
        v_out8(VIRTIO_PCI_STATUS, STATUS_FAILED);
        return 0;
    }
    if (qsz > DRIVER_QUEUE_SIZE) {
        serial_write("[virtio-blk] queue too large, device qsz=");
        serial_write_hex(qsz);
        serial_write(" max=");
        serial_write_hex(DRIVER_QUEUE_SIZE);
        serial_write("\n");
        v_out8(VIRTIO_PCI_STATUS, STATUS_FAILED);
        return 0;
    }

    queue_size = (u16)qsz;
    queue_layout_init(queue_size);

    serial_write("[virtio-blk] device qsz=");
    serial_write_hex(qsz);
    serial_write(" driver qsz=");
    serial_write_hex(queue_size);
    serial_write(" vring_total=");
    serial_write_hex(vring_total_bytes);
    serial_write("\n");

    queue_reset_memory();

    queue_pfn_before = v_in32(VIRTIO_PCI_QUEUE_PFN);
    serial_write("[virtio-blk] queue pfn before=");
    serial_write_hex(queue_pfn_before);
    serial_write("\n");
    if (queue_pfn_before != 0) {
        serial_write("[virtio-blk] clearing stale queue pfn\n");
        v_out32(VIRTIO_PCI_QUEUE_PFN, 0);
        v_out16(VIRTIO_PCI_QUEUE_SEL, 0);
        serial_write("[virtio-blk] queue pfn after clear=");
        serial_write_hex(v_in32(VIRTIO_PCI_QUEUE_PFN));
        serial_write("\n");
    }

    pfn = ((u32)(unsigned long)queue_bytes) >> 12;
    serial_write("[virtio-blk] queue pfn write=");
    serial_write_hex(pfn);
    serial_write("\n");
    v_out32(VIRTIO_PCI_QUEUE_PFN, pfn);
    serial_write("[virtio-blk] queue pfn read=");
    serial_write_hex(v_in32(VIRTIO_PCI_QUEUE_PFN));
    serial_write("\n");
    serial_write("[virtio-blk] queue pfn read2=");
    serial_write_hex(v_in32(VIRTIO_PCI_QUEUE_PFN));
    serial_write("\n");
    v_out8(VIRTIO_PCI_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_DRIVER_OK);
    serial_write("[virtio-blk] status driver_ok=");
    serial_write_hex(v_in8(VIRTIO_PCI_STATUS));
    serial_write("\n");
    ready = 1;
    serial_write("[virtio-blk] initialized io=");
    serial_write_hex(io_base);
    serial_write(" qsz=");
    serial_write_hex(queue_size);
    serial_write("\n");
    return 1;
}

int virtio_blk_ready(void) {
    return ready;
}

int virtio_blk_read(u32 sector, void *buffer, u32 sector_count) {
    return virtio_blk_submit(VIRTIO_BLK_T_IN, sector, buffer, sector_count);
}

int virtio_blk_write(u32 sector, const void *buffer, u32 sector_count) {
    return virtio_blk_submit(VIRTIO_BLK_T_OUT, sector, (void*)buffer, sector_count);
}
