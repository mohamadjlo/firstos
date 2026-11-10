#include "virtio_gpu.h"
#include "memory.h"
#include "pci.h"
#include "serial.h"
#include "io.h"

#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_GPU_DEVICE_ID_MODERN 0x1050
#define VIRTIO_GPU_DEVICE_ID_LEGACY 0x1010
#define VIRTIO_PCI_CAP_VENDOR 0x09
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG 3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4
#define STATUS_ACK 1
#define STATUS_DRIVER 2
#define STATUS_DRIVER_OK 4
#define STATUS_FEATURES_OK 8
#define STATUS_FAILED 128
#define VIRTIO_GPU_QUEUE_CTRL 0
#define VIRTQ_NUM 8
#define VIRTQ_ALIGN 4096
#define GPU_REQ_BYTES 1024
#define GPU_RESP_BYTES 4096
#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2
#define SUBMIT_TIMEOUT 5000000u

typedef struct {
    u8 cap_vndr;
    u8 cap_next;
    u8 cap_len;
    u8 cfg_type;
    u8 bar;
    u8 padding[3];
    u32 offset;
    u32 length;
    u32 notify_off_multiplier;
} virtio_pci_cap_data_t;

typedef struct {
    u32 device_feature_select;
    u32 device_feature;
    u32 driver_feature_select;
    u32 driver_feature;
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
    u16 queue_notify_data;
    u16 queue_reset;
    u16 admin_queue_index;
    u16 admin_queue_num;
} __attribute__((packed)) virtio_pci_common_cfg_t;

typedef struct {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
} __attribute__((packed)) virtq_desc_t;

typedef struct {
    u32 id;
    u32 len;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct {
    volatile virtq_desc_t desc[VIRTQ_NUM];
    volatile u16 avail_flags;
    volatile u16 avail_idx;
    volatile u16 avail_ring[VIRTQ_NUM];
    volatile u16 avail_used_event;
    u8 avail_pad[VIRTQ_ALIGN - (sizeof(virtq_desc_t) * VIRTQ_NUM + sizeof(u16) * (3 + VIRTQ_NUM))];
    volatile u16 used_flags;
    volatile u16 used_idx;
    volatile virtq_used_elem_t used_ring[VIRTQ_NUM];
    volatile u16 used_avail_event;
    u8 used_pad[VIRTQ_ALIGN - (sizeof(u16) * 3 + sizeof(virtq_used_elem_t) * VIRTQ_NUM)];
} __attribute__((packed, aligned(VIRTQ_ALIGN))) virtq_storage_t;

typedef struct {
    virtq_storage_t vring;
    u16 last_used_idx;
    u16 next_slot;
    u8 req[GPU_REQ_BYTES];
    u8 resp[GPU_RESP_BYTES];
} __attribute__((aligned(VIRTQ_ALIGN))) virtio_gpu_ctrl_queue_t;

typedef struct {
    virtio_pci_common_cfg_t *common;
    volatile u16 *notify;
    volatile u16 *ctrl_notify_ptr;
    volatile u8 *isr;
    volatile void *device_cfg;
    u32 notify_off_multiplier;
    virtio_gpu_ctrl_queue_t ctrl;
    int modern;
    int ctrl_ready;
} virtio_gpu_transport_t;

static virtio_gpu_info_t gpu;
static virtio_gpu_transport_t transport;
static const pci_device_t *gpu_dev;

#if defined(__GNUC__)
_Static_assert(__builtin_offsetof(virtio_pci_common_cfg_t, queue_select) == 22, "queue_select offset mismatch");
_Static_assert(__builtin_offsetof(virtio_pci_common_cfg_t, queue_size) == 24, "queue_size offset mismatch");
_Static_assert(__builtin_offsetof(virtio_pci_common_cfg_t, queue_msix_vector) == 26, "queue_msix_vector offset mismatch");
_Static_assert(__builtin_offsetof(virtio_pci_common_cfg_t, queue_enable) == 28, "queue_enable offset mismatch");
_Static_assert(__builtin_offsetof(virtio_pci_common_cfg_t, queue_notify_off) == 30, "queue_notify_off offset mismatch");
_Static_assert(__builtin_offsetof(virtio_pci_common_cfg_t, queue_desc) == 32, "queue_desc offset mismatch");
_Static_assert(__builtin_offsetof(virtio_pci_common_cfg_t, queue_driver) == 40, "queue_driver offset mismatch");
_Static_assert(__builtin_offsetof(virtio_pci_common_cfg_t, queue_device) == 48, "queue_device offset mismatch");
_Static_assert(__builtin_offsetof(virtio_pci_common_cfg_t, queue_notify_data) == 56, "queue_notify_data offset mismatch");
_Static_assert(sizeof(virtq_storage_t) == (VIRTQ_ALIGN * 2), "virtq_storage_t size mismatch");
#endif

static void mem_zero(volatile void *ptr, u32 len) {
    if (!ptr || !len) return;
    __asm__ volatile (
        "cld\n\t"
        "rep stosb"
        : "+D"(ptr), "+c"(len)
        : "a"(0)
        : "memory"
    );
}
static void mem_copy(volatile void *dst, const volatile void *src, u32 len) {
    if (!dst || !src || !len) return;
    __asm__ volatile (
        "cld\n\t"
        "rep movsb"
        : "+D"(dst), "+S"(src), "+c"(len)
        :
        : "memory"
    );
}
static volatile void *map_bar_ptr(u64 bar, u32 offset) {
    if (!bar) return (void*)0;
    return (volatile void*)(uintptr_t)((bar & ~0xFULL) + (u64)offset);
}
static void mmio_write16(volatile void *base, u32 off, u16 value) { *(volatile u16*)((volatile u8*)base + off) = value; }

static void serial_hex64(u64 value) {
    serial_write("0x");
    serial_write_hex((u32)(value >> 32));
    serial_write_hex((u32)value);
}

static int pci_find_virtio_cap(const pci_device_t *dev, u8 cfg_type, virtio_pci_cap_data_t *out) {
    u8 cap_ptr;
    if (!dev || !out) return 0;
    cap_ptr = (u8)(pci_config_read32(dev, 0x34) & 0xFC);
    while (cap_ptr) {
        u32 hdr0 = pci_config_read32(dev, cap_ptr);
        u8 cap_id = (u8)(hdr0 & 0xFF);
        u8 next = (u8)((hdr0 >> 8) & 0xFF);
        if (cap_id == VIRTIO_PCI_CAP_VENDOR) {
            u32 hdr1 = pci_config_read32(dev, cap_ptr + 4);
            u32 hdr2 = pci_config_read32(dev, cap_ptr + 8);
            u32 hdr3 = pci_config_read32(dev, cap_ptr + 12);
            out->cap_vndr = (u8)(hdr0 & 0xFF);
            out->cap_next = (u8)((hdr0 >> 8) & 0xFF);
            out->cap_len = (u8)((hdr0 >> 16) & 0xFF);
            out->cfg_type = (u8)((hdr0 >> 24) & 0xFF);
            out->bar = (u8)(hdr1 & 0xFF);
            out->padding[0] = (u8)((hdr1 >> 8) & 0xFF);
            out->padding[1] = (u8)((hdr1 >> 16) & 0xFF);
            out->padding[2] = (u8)((hdr1 >> 24) & 0xFF);
            out->offset = hdr2;
            out->length = hdr3;
            out->notify_off_multiplier = pci_config_read32(dev, cap_ptr + 16);
            if (out->cfg_type == cfg_type) return 1;
        }
        cap_ptr = next;
    }
    return 0;
}

static void gpu_hdr_init(virtio_gpu_ctrl_hdr_t *hdr, u32 type) {
    hdr->type = type;
    hdr->flags = 0;
    hdr->fence_id = 0;
    hdr->ctx_id = 0;
    hdr->ring_idx = 0;
    hdr->padding[0] = 0;
    hdr->padding[1] = 0;
    hdr->padding[2] = 0;
}

static volatile u8 gpu_req_buf[GPU_REQ_BYTES] __attribute__((aligned(4096)));
static volatile u8 gpu_resp_buf[GPU_RESP_BYTES] __attribute__((aligned(4096)));

static int virtio_gpu_setup_caps(const pci_device_t *dev) {
    virtio_pci_cap_data_t common_cap;
    virtio_pci_cap_data_t notify_cap;
    virtio_pci_cap_data_t isr_cap;
    virtio_pci_cap_data_t device_cap;
    u32 notify_len;
    u16 cmd_before, cmd_after;

    if (!pci_find_virtio_cap(dev, VIRTIO_PCI_CAP_COMMON_CFG, &common_cap)) return 0;
    if (!pci_find_virtio_cap(dev, VIRTIO_PCI_CAP_NOTIFY_CFG, &notify_cap)) return 0;
    if (!pci_find_virtio_cap(dev, VIRTIO_PCI_CAP_ISR_CFG, &isr_cap)) return 0;
    if (!pci_find_virtio_cap(dev, VIRTIO_PCI_CAP_DEVICE_CFG, &device_cap)) return 0;

    cmd_before = pci_config_read16(dev, 0x04);
    pci_enable_bus_master(dev);
    cmd_after = pci_config_read16(dev, 0x04);
    serial_write("[virtio-gpu] pci cmd before=");
    serial_write_hex((u32)cmd_before);
    serial_write(" after=");
    serial_write_hex((u32)cmd_after);
    serial_write("\n");

    notify_len = notify_cap.notify_off_multiplier;
    transport.common = (virtio_pci_common_cfg_t*)map_bar_ptr(pci_read_bar64(dev, common_cap.bar), common_cap.offset);
    transport.notify = (volatile u16*)map_bar_ptr(pci_read_bar64(dev, notify_cap.bar), notify_cap.offset);
    transport.isr = (volatile u8*)map_bar_ptr(pci_read_bar64(dev, isr_cap.bar), isr_cap.offset);
    transport.device_cfg = map_bar_ptr(pci_read_bar64(dev, device_cap.bar), device_cap.offset);
    transport.notify_off_multiplier = notify_len;
    transport.modern = 1;

    serial_write("[virtio-gpu] modern PCI capabilities found\n");
    serial_write("[virtio-gpu] common="); serial_hex64((u64)(unsigned long)transport.common);
    serial_write(" notify="); serial_hex64((u64)(unsigned long)transport.notify);
    serial_write(" isr="); serial_hex64((u64)(unsigned long)transport.isr);
    serial_write(" device="); serial_hex64((u64)(unsigned long)transport.device_cfg);
    serial_write(" notify_mult="); serial_write_hex(transport.notify_off_multiplier);
    serial_write("\n");
    serial_write("[virtio-gpu] common field addrs: queue_notify_off="); serial_hex64((u64)(unsigned long)&transport.common->queue_notify_off);
    serial_write(" queue_desc="); serial_hex64((u64)(unsigned long)&transport.common->queue_desc);
    serial_write(" queue_driver="); serial_hex64((u64)(unsigned long)&transport.common->queue_driver);
    serial_write(" queue_device="); serial_hex64((u64)(unsigned long)&transport.common->queue_device);
    serial_write("\n");
    return 1;
}

static int virtio_gpu_reset_and_negotiate(void) {
    if (!transport.common) return 0;
    transport.common->device_status = 0;
    __sync_synchronize();
    transport.common->device_status = STATUS_ACK;
    __sync_synchronize();
    transport.common->device_status = STATUS_ACK | STATUS_DRIVER;
    __sync_synchronize();
    transport.common->driver_feature_select = 1;
    transport.common->driver_feature = 1; /* VIRTIO_F_VERSION_1 */
    __sync_synchronize();
    transport.common->driver_feature_select = 0;
    transport.common->driver_feature = 0;
    __sync_synchronize();
    transport.common->device_status |= STATUS_FEATURES_OK;
    __sync_synchronize();
    if (!(transport.common->device_status & STATUS_FEATURES_OK)) {
        transport.common->device_status |= STATUS_FAILED;
        __sync_synchronize();
        return 0;
    }
    return 1;
}

static int virtio_gpu_setup_ctrl_queue(void) {
    virtio_gpu_ctrl_queue_t *q = &transport.ctrl;
    u16 qsz;
    u16 notify_off;
    u64 expected_desc;
    u64 expected_driver;
    u64 expected_device;
    u16 readback_select;
    u16 readback_size;
    u64 readback_desc;
    u64 readback_driver;
    u64 readback_device;
    u16 readback_enable;
    u16 readback_notify_off;

    if (!transport.common) return 0;
    mem_zero(q, sizeof(*q));
    transport.common->queue_select = VIRTIO_GPU_QUEUE_CTRL;
    __sync_synchronize();
    qsz = transport.common->queue_size;
    serial_write("[virtio-gpu] qsz=");
    serial_write_hex(qsz);
    serial_write(" queue_enable(before)=");
    serial_write_hex(transport.common->queue_enable);
    serial_write("\n");
    if (qsz < VIRTQ_NUM || qsz == 0) return 0;

    expected_desc = virt_to_phys64(&q->vring.desc[0]);
    expected_driver = virt_to_phys64(&q->vring.avail_flags);
    expected_device = virt_to_phys64(&q->vring.used_flags);

    transport.common->queue_size = VIRTQ_NUM;
    transport.common->queue_desc = expected_desc;
    transport.common->queue_driver = expected_driver;
    transport.common->queue_device = expected_device;
    __sync_synchronize();

    readback_select = transport.common->queue_select;
    readback_size = transport.common->queue_size;
    readback_desc = transport.common->queue_desc;
    readback_driver = transport.common->queue_driver;
    readback_device = transport.common->queue_device;

    serial_write("[virtio-gpu] queue expected: desc="); serial_hex64(expected_desc);
    serial_write(" driver="); serial_hex64(expected_driver);
    serial_write(" device="); serial_hex64(expected_device);
    serial_write("\n");
    serial_write("[virtio-gpu] queue readback(before enable): select="); serial_write_hex(readback_select);
    serial_write(" size="); serial_write_hex(readback_size);
    serial_write(" desc="); serial_hex64(readback_desc);
    serial_write(" driver="); serial_hex64(readback_driver);
    serial_write(" device="); serial_hex64(readback_device);
    serial_write("\n");

    if (readback_select != VIRTIO_GPU_QUEUE_CTRL ||
        readback_size != VIRTQ_NUM ||
        readback_desc != expected_desc ||
        readback_driver != expected_driver ||
        readback_device != expected_device) {
        serial_write("[virtio-gpu] queue readback mismatch before enable\n");
        return 0;
    }

    serial_write("[virtio-gpu] queue_desc="); serial_hex64(transport.common->queue_desc);
    serial_write(" queue_driver="); serial_hex64(transport.common->queue_driver);
    serial_write(" queue_device="); serial_hex64(transport.common->queue_device);
    serial_write("\n");

    transport.common->queue_enable = 1;
    __sync_synchronize();
    notify_off = transport.common->queue_notify_off;

    readback_select = transport.common->queue_select;
    readback_size = transport.common->queue_size;
    readback_desc = transport.common->queue_desc;
    readback_driver = transport.common->queue_driver;
    readback_device = transport.common->queue_device;
    readback_enable = transport.common->queue_enable;
    readback_notify_off = transport.common->queue_notify_off;

    serial_write("[virtio-gpu] queue readback(after enable): select="); serial_write_hex(readback_select);
    serial_write(" size="); serial_write_hex(readback_size);
    serial_write(" desc="); serial_hex64(readback_desc);
    serial_write(" driver="); serial_hex64(readback_driver);
    serial_write(" device="); serial_hex64(readback_device);
    serial_write(" enable="); serial_write_hex(readback_enable);
    serial_write(" notify_off="); serial_write_hex(readback_notify_off);
    serial_write("\n");

    if (readback_select != VIRTIO_GPU_QUEUE_CTRL ||
        readback_size != VIRTQ_NUM ||
        readback_desc != expected_desc ||
        readback_driver != expected_driver ||
        readback_device != expected_device ||
        readback_enable != 1) {
        serial_write("[virtio-gpu] queue readback mismatch after enable\n");
        return 0;
    }

    serial_write("[virtio-gpu] queue_enable(after)=");
    serial_write_hex(transport.common->queue_enable);
    serial_write(" notify_off=");
    serial_write_hex(notify_off);
    serial_write("\n");

    transport.ctrl_ready = (transport.common->queue_enable == 1);
    transport.ctrl_notify_ptr = (volatile u16*)((volatile u8*)transport.notify + (u32)notify_off * transport.notify_off_multiplier);
    q->last_used_idx = 0;
    q->next_slot = 0;
    q->vring.used_idx = 0;
    q->vring.avail_idx = 0;
    return transport.ctrl_ready;
}

static void virtio_gpu_notify_queue(void) {
    if (!transport.ctrl_notify_ptr) return;
    *transport.ctrl_notify_ptr = VIRTIO_GPU_QUEUE_CTRL;
}

static int virtio_gpu_submit_common(void *a, u32 al, void *b, u32 bl, void *r, u32 rl, int desc_count) {
    virtio_gpu_ctrl_queue_t *q = &transport.ctrl;
    virtq_desc_t *d = q->vring.desc;
    u16 head;
    u16 curr;
    u16 avail_idx;
    u32 spins;
    u16 used_slot;

    if (!transport.ctrl_ready || !a || !r) return -1;
    if (desc_count != 2 && desc_count != 3) return -2;

    u32 resp_clear_len = rl > 64 ? 64 : rl;
    mem_zero(gpu_resp_buf, resp_clear_len);
    mem_copy(gpu_req_buf, a, al);
    if (desc_count == 3 && b && bl) mem_copy(gpu_req_buf + al, b, bl);

    d[0].addr = virt_to_phys64(gpu_req_buf);
    d[0].len = al;
    d[0].flags = VIRTQ_DESC_F_NEXT;
    d[0].next = 1;

    if (desc_count == 3) {
        d[1].addr = virt_to_phys64(gpu_req_buf + al);
        d[1].len = bl;
        d[1].flags = VIRTQ_DESC_F_NEXT;
        d[1].next = 2;
        curr = 2;
    } else {
        curr = 1;
    }

    d[curr].addr = virt_to_phys64(gpu_resp_buf);
    d[curr].len = rl;
    d[curr].flags = VIRTQ_DESC_F_WRITE;
    d[curr].next = 0;

    head = 0;
    q->next_slot = 0;

    avail_idx = q->vring.avail_idx;
    q->vring.avail_ring[avail_idx % VIRTQ_NUM] = head;
    __sync_synchronize();
    q->vring.avail_idx = (u16)(avail_idx + 1);
    __sync_synchronize();

    virtio_gpu_notify_queue();

    spins = 0;
    while (q->vring.used_idx == q->last_used_idx) {
        __sync_synchronize();
        __asm__ volatile("pause" ::: "memory");
        if (transport.common && (transport.common->device_status & STATUS_FAILED)) {
            serial_write("[virtio-gpu] device failed during submit!\n");
            transport.ctrl_ready = 0;
            return -4;
        }
        if (++spins >= SUBMIT_TIMEOUT) {
            serial_write("[virtio-gpu] command submission timed out!\n");
            return -3;
        }
    }
    
    used_slot = q->vring.used_ring[q->last_used_idx % VIRTQ_NUM].id % VIRTQ_NUM;
    (void)used_slot;
    q->last_used_idx = q->vring.used_idx;
    __sync_synchronize();
    __asm__ volatile("" ::: "memory");
    mem_copy(r, (const volatile void*)gpu_resp_buf, rl);
    return 0;
}

static int virtio_gpu_get_display_size(u32 *width, u32 *height) {
    virtio_gpu_get_display_info_t req;
    virtio_gpu_resp_display_info_t resp;
    int rc;

    if (!width || !height || !transport.ctrl_ready) return -1;
    gpu_hdr_init(&req.hdr, VIRTIO_GPU_CMD_GET_DISPLAY_INFO);
    mem_zero(&resp, sizeof(resp));

    rc = virtio_gpu_submit2(&req, sizeof(req), &resp, sizeof(resp));
    if (rc < 0) {
        serial_write("[virtio-gpu] GET_DISPLAY_INFO submit failed\n");
        return -2;
    }

    if (resp.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) return -3;
    if (!resp.pmodes[0].enabled || !resp.pmodes[0].r.width || !resp.pmodes[0].r.height) return -4;
    *width = resp.pmodes[0].r.width;
    *height = resp.pmodes[0].r.height;
    return 0;
}

void virtio_gpu_probe(void) {
    const pci_device_t *dev;
    mem_zero(&gpu, sizeof(gpu));
    mem_zero(&transport, sizeof(transport));
    gpu_dev = 0;

    dev = pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_GPU_DEVICE_ID_MODERN);
    if (!dev) dev = pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_GPU_DEVICE_ID_LEGACY);
    if (!dev) return;

    gpu.present = 1;
    gpu.bus = dev->bus;
    gpu.slot = dev->slot;
    gpu.function = dev->function;
    gpu.vendor_id = dev->vendor_id;
    gpu.device_id = dev->device_id;
    gpu.bar0 = pci_read_bar32(dev, 0);
    gpu.bar1 = pci_read_bar32(dev, 1);
    gpu_dev = dev;

    serial_write("[virtio-gpu] detected vendor=");
    serial_write_hex(gpu.vendor_id);
    serial_write(" device=");
    serial_write_hex(gpu.device_id);
    serial_write(" bar0=");
    serial_write_hex(gpu.bar0);
    serial_write(" bar1=");
    serial_write_hex(gpu.bar1);
    serial_write("\n");
}

const virtio_gpu_info_t* virtio_gpu_info(void) { return &gpu; }
int virtio_gpu_available(void) { return gpu.present; }

int virtio_gpu_init_safe(u32 width, u32 height) {
    u32 detected_width = 0;
    u32 detected_height = 0;
    (void)width;
    (void)height;

    serial_write("[virtio-gpu] virtio_gpu_init_safe called\n");
    if (!gpu.present || !gpu_dev) return -1;
    if (!virtio_gpu_setup_caps(gpu_dev)) { serial_write("[virtio-gpu] setup caps failed\n"); return -2; }
    if (!virtio_gpu_reset_and_negotiate()) { serial_write("[virtio-gpu] reset/negotiate failed\n"); return -3; }
    if (!virtio_gpu_setup_ctrl_queue()) { serial_write("[virtio-gpu] queue setup failed\n"); return -4; }
    transport.common->device_status = STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK;
    __sync_synchronize();
    transport.common->device_status |= STATUS_DRIVER_OK;
    __sync_synchronize();
    if (virtio_gpu_get_display_size(&detected_width, &detected_height) == 0) {
        gpu.resource_width = detected_width;
        gpu.resource_height = detected_height;
        gpu.scanout_id = 0;
        gpu.initialized = 1;
        serial_write("[virtio-gpu] display info ok w=");
        serial_write_hex(gpu.resource_width);
        serial_write(" h=");
        serial_write_hex(gpu.resource_height);
        serial_write("\n");
        return 0;
    }
    serial_write("[virtio-gpu] init failed\n");
    return -5;
}

int virtio_gpu_submit2(void *req, u32 req_len, void *resp, u32 resp_len) {
    return virtio_gpu_submit_common(req, req_len, 0, 0, resp, resp_len, 2);
}

int virtio_gpu_submit3(void *a, u32 al, void *b, u32 bl, void *r, u32 rl) {
    return virtio_gpu_submit_common(a, al, b, bl, r, rl, 3);
}

#define VIRTIO_GPU_CURSOR_RES_ID 2
#define VIRTIO_GPU_CURSOR_W 64
#define VIRTIO_GPU_CURSOR_H 64

static u32 cursor_pixels[VIRTIO_GPU_CURSOR_W * VIRTIO_GPU_CURSOR_H] __attribute__((aligned(4096)));
static int hardware_cursor_available = 0;

int virtio_gpu_submit1(void *req, u32 req_len) {
    virtio_gpu_ctrl_queue_t *q = &transport.ctrl;
    virtq_desc_t *d = q->vring.desc;
    u16 avail_idx;
    u32 spins;

    if (!transport.ctrl_ready || !req) return -1;
    if (req_len > GPU_REQ_BYTES) return -2;

    mem_copy((void*)gpu_req_buf, req, req_len);

    d[0].addr = virt_to_phys64((const void*)gpu_req_buf);
    d[0].len = req_len;
    d[0].flags = 0;
    d[0].next = 0;

    avail_idx = q->vring.avail_idx;
    q->vring.avail_ring[avail_idx % VIRTQ_NUM] = 0;
    __sync_synchronize();
    q->vring.avail_idx = (u16)(avail_idx + 1);
    __sync_synchronize();

    virtio_gpu_notify_queue();

    spins = 0;
    while (q->vring.used_idx == q->last_used_idx) {
        __sync_synchronize();
        __asm__ volatile("pause" ::: "memory");
        if (transport.common && (transport.common->device_status & STATUS_FAILED)) {
            serial_write("[virtio-gpu] device failed during cursor submit!\n");
            hardware_cursor_available = 0;
            return -4;
        }
        if (++spins >= SUBMIT_TIMEOUT) {
            serial_write("[virtio-gpu] cursor submit timeout!\n");
            hardware_cursor_available = 0;
            return -3;
        }
    }
    q->last_used_idx++;
    __sync_synchronize();
    return 0;
}

static void init_cursor_pixels(void) {
    u32 y, x;
    mem_zero(cursor_pixels, sizeof(cursor_pixels));
    static const char *cursor_art[16] = {
        "X               ",
        "XX              ",
        "X.X             ",
        "X..X            ",
        "X...X           ",
        "X....X          ",
        "X.....X         ",
        "X......X        ",
        "X.......X       ",
        "X.....XXXX      ",
        "X..X..X         ",
        "X.X X..X        ",
        "XX   X..X       ",
        "X     X..X      ",
        "       XX       ",
        "                "
    };
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            char ch = cursor_art[y][x];
            if (ch == 'X') {
                cursor_pixels[y * VIRTIO_GPU_CURSOR_W + x] = 0xFF000000;
            } else if (ch == '.') {
                cursor_pixels[y * VIRTIO_GPU_CURSOR_W + x] = 0xFFFFFFFF;
            } else {
                cursor_pixels[y * VIRTIO_GPU_CURSOR_W + x] = 0x00000000;
            }
        }
    }
}

int virtio_gpu_cursor_init(void) {
    if (!transport.ctrl_ready) return -1;

    init_cursor_pixels();

    /* 1. Create 2D resource for cursor (ID=2, 64x64, B8G8R8X8) */
    virtio_gpu_resource_create_2d_t create_cmd;
    virtio_gpu_ctrl_hdr_t resp;
    mem_zero(&create_cmd, sizeof(create_cmd));
    mem_zero(&resp, sizeof(resp));
    create_cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    create_cmd.resource_id = VIRTIO_GPU_CURSOR_RES_ID;
    create_cmd.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    create_cmd.width = VIRTIO_GPU_CURSOR_W;
    create_cmd.height = VIRTIO_GPU_CURSOR_H;

    if (virtio_gpu_submit2(&create_cmd, sizeof(create_cmd), &resp, sizeof(resp)) < 0 ||
        resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
        serial_write("[virtio-gpu] cursor resource create failed\n");
        hardware_cursor_available = 0;
        return -2;
    }

    /* 2. Attach backing */
    virtio_gpu_resource_attach_backing_t attach_cmd;
    virtio_gpu_mem_entry_t mem;
    mem_zero(&attach_cmd, sizeof(attach_cmd));
    mem_zero(&mem, sizeof(mem));
    mem_zero(&resp, sizeof(resp));
    attach_cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    attach_cmd.resource_id = VIRTIO_GPU_CURSOR_RES_ID;
    attach_cmd.nr_entries = 1;
    mem.addr = virt_to_phys64(cursor_pixels);
    mem.length = sizeof(cursor_pixels);

    if (virtio_gpu_submit3(&attach_cmd, sizeof(attach_cmd), &mem, sizeof(mem), &resp, sizeof(resp)) < 0 ||
        resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
        serial_write("[virtio-gpu] cursor attach failed\n");
        hardware_cursor_available = 0;
        return -3;
    }

    /* 3. Transfer to host */
    virtio_gpu_transfer_to_host_2d_t xfer_cmd;
    mem_zero(&xfer_cmd, sizeof(xfer_cmd));
    mem_zero(&resp, sizeof(resp));
    xfer_cmd.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    xfer_cmd.resource_id = VIRTIO_GPU_CURSOR_RES_ID;
    xfer_cmd.r.x = 0;
    xfer_cmd.r.y = 0;
    xfer_cmd.r.width = VIRTIO_GPU_CURSOR_W;
    xfer_cmd.r.height = VIRTIO_GPU_CURSOR_H;
    xfer_cmd.offset = 0;

    if (virtio_gpu_submit2(&xfer_cmd, sizeof(xfer_cmd), &resp, sizeof(resp)) < 0 ||
        resp.type != VIRTIO_GPU_RESP_OK_NODATA) {
        serial_write("[virtio-gpu] cursor transfer failed\n");
        hardware_cursor_available = 0;
        return -4;
    }

    /* 4. Update cursor position */
    virtio_gpu_update_cursor_t upd;
    mem_zero(&upd, sizeof(upd));
    upd.hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
    upd.resource_id = VIRTIO_GPU_CURSOR_RES_ID;
    upd.pos.scanout_id = 0;
    upd.pos.x = 512;
    upd.pos.y = 384;
    upd.hot_x = 0;
    upd.hot_y = 0;

    if (virtio_gpu_submit1(&upd, sizeof(upd)) < 0) {
        serial_write("[virtio-gpu] cursor update command failed\n");
        hardware_cursor_available = 0;
        return -5;
    }

    hardware_cursor_available = 1;
    serial_write("[virtio-gpu] hardware cursor initialized successfully\n");
    return 0;
}

int virtio_gpu_cursor_update(u32 x, u32 y) {
    if (!hardware_cursor_available) return -1;
    virtio_gpu_update_cursor_t upd;
    mem_zero(&upd, sizeof(upd));
    upd.hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
    upd.resource_id = VIRTIO_GPU_CURSOR_RES_ID;
    upd.pos.scanout_id = 0;
    upd.pos.x = x;
    upd.pos.y = y;
    upd.hot_x = 0;
    upd.hot_y = 0;
    return virtio_gpu_submit1(&upd, sizeof(upd));
}

int virtio_gpu_cursor_move(u32 x, u32 y) {
    if (!hardware_cursor_available) return -1;
    virtio_gpu_update_cursor_t upd;
    mem_zero(&upd, sizeof(upd));
    upd.hdr.type = VIRTIO_GPU_CMD_MOVE_CURSOR;
    upd.resource_id = VIRTIO_GPU_CURSOR_RES_ID;
    upd.pos.scanout_id = 0;
    upd.pos.x = x;
    upd.pos.y = y;
    return virtio_gpu_submit1(&upd, sizeof(upd));
}

int virtio_gpu_has_hardware_cursor(void) {
    return hardware_cursor_available;
}

int virtio_gpu_submit_batch2(void *req1, u32 len1, void *resp1, u32 rlen1,
                             void *req2, u32 len2, void *resp2, u32 rlen2) {
    virtio_gpu_ctrl_queue_t *q = &transport.ctrl;
    virtq_desc_t *d = q->vring.desc;
    u16 avail_idx;
    u32 spins;

    if (!transport.ctrl_ready || !req1 || !resp1 || !req2 || !resp2) return -1;
    if (len1 + len2 > GPU_REQ_BYTES || rlen1 + rlen2 > GPU_RESP_BYTES) return -2;

    /* Request buffers */
    mem_copy((void*)gpu_req_buf, req1, len1);
    mem_copy((void*)(gpu_req_buf + len1), req2, len2);

    /* Response buffers clear */
    u32 c1 = rlen1 > 64 ? 64 : rlen1;
    u32 c2 = rlen2 > 64 ? 64 : rlen2;
    mem_zero((void*)gpu_resp_buf, c1);
    mem_zero((void*)(gpu_resp_buf + rlen1), c2);

    /* Descriptor chain 0: request 1 -> response 1 */
    d[0].addr = virt_to_phys64((const void*)gpu_req_buf);
    d[0].len = len1;
    d[0].flags = VIRTQ_DESC_F_NEXT;
    d[0].next = 1;

    d[1].addr = virt_to_phys64((const void*)gpu_resp_buf);
    d[1].len = rlen1;
    d[1].flags = VIRTQ_DESC_F_WRITE;
    d[1].next = 0;

    /* Descriptor chain 1: request 2 -> response 2 */
    d[2].addr = virt_to_phys64((const void*)(gpu_req_buf + len1));
    d[2].len = len2;
    d[2].flags = VIRTQ_DESC_F_NEXT;
    d[2].next = 3;

    d[3].addr = virt_to_phys64((const void*)(gpu_resp_buf + rlen1));
    d[3].len = rlen2;
    d[3].flags = VIRTQ_DESC_F_WRITE;
    d[3].next = 0;

    /* Put both chain heads (0 and 2) into available ring */
    avail_idx = q->vring.avail_idx;
    q->vring.avail_ring[avail_idx % VIRTQ_NUM] = 0;
    q->vring.avail_ring[(avail_idx + 1) % VIRTQ_NUM] = 2;
    __sync_synchronize();
    q->vring.avail_idx = (u16)(avail_idx + 2);
    __sync_synchronize();

    /* Single MMIO notification for both commands */
    virtio_gpu_notify_queue();

    /* Wait for host to process both descriptors */
    spins = 0;
    while ((u16)(q->vring.used_idx - q->last_used_idx) < 2) {
        __sync_synchronize();
        __asm__ volatile("pause" ::: "memory");
        if (transport.common && (transport.common->device_status & STATUS_FAILED)) {
            serial_write("[virtio-gpu] device failed during batch submit!\n");
            transport.ctrl_ready = 0;
            return -4;
        }
        if (++spins >= SUBMIT_TIMEOUT) {
            serial_write("[virtio-gpu] batch submission timed out!\n");
            return -3;
        }
    }

    q->last_used_idx = (u16)(q->last_used_idx + 2);
    __sync_synchronize();
    __asm__ volatile("" ::: "memory");

    /* Copy back responses */
    mem_copy(resp1, (const volatile void*)gpu_resp_buf, rlen1);
    mem_copy(resp2, (const volatile void*)(gpu_resp_buf + rlen1), rlen2);

    extern void compositor_record_virtio(int batched, int transfers, int flushes);
    compositor_record_virtio(1, 1, 1);
    return 0;
}

u32 virtio_gpu_get_detected_width(void) { return gpu.resource_width; }
u32 virtio_gpu_get_detected_height(void) { return gpu.resource_height; }

