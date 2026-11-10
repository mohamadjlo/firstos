#include "virtio_net.h"
#include "pci.h"
#include "serial.h"
#include <string.h>

#define VIRTIO_VENDOR_ID        0x1AF4
#define VIRTIO_NET_DEVICE_ID    0x1000  /* legacy */
#define VIRTIO_NET_DEVICE_ID_MOD 0x1041 /* modern virtio 1.0 */

#define VNET_PCI_HOST_FEATURES  0x00
#define VNET_PCI_GUEST_FEATURES 0x04
#define VNET_PCI_QUEUE_PFN      0x08
#define VNET_PCI_QUEUE_NUM      0x0C
#define VNET_PCI_QUEUE_SEL      0x0E
#define VNET_PCI_QUEUE_NOTIFY   0x10
#define VNET_PCI_STATUS         0x12
#define VNET_PCI_ISR            0x13
#define VNET_PCI_MAC0           0x14

#define STATUS_ACK              1
#define STATUS_DRIVER           2
#define STATUS_DRIVER_OK        4

#define VRING_ALIGN             4096
#define VRING_DESC_F_NEXT       1
#define VRING_DESC_F_WRITE      2

#define QUEUE_SIZE              256
#define RX_BUFS                 32
#define PKT_BUF_SIZE            1546   /* virtio_net_hdr(10) + ETH_MAX(1536) */

/* Packet-by-packet serial tracing is prohibitively expensive during a web
 * transfer.  Keep it available for targeted driver debugging, but disable it
 * in normal builds so the serial console cannot become the network bottleneck.
 */
#define VNET_PACKET_LOG         0

static virtio_net_stats_t g_vnet_stats = {0};

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
    u8 flags;
    u8 gso_type;
    u16 hdr_len;
    u16 gso_size;
    u16 csum_start;
    u16 csum_offset;
} __attribute__((packed)) vnet_hdr_t;

/* Layout for QUEUE_SIZE = 256:
 * desc:  256 * 16 = 4096 bytes (offset 0)
 * avail: 4 + 2 * 256 = 516 bytes (offset 4096)
 * used:  aligned to 4096 -> (offset 8192)
 * used bytes: 4 + 8 * 256 = 2052 bytes
 * total bytes: 8192 + 2052 = 10244, padded to 12288
 */
#define VNET_VRING_TOTAL_BYTES 12288

/* ---- receiveq (index 0) ---- */
static volatile u8 rxq_bytes[VNET_VRING_TOTAL_BYTES] __attribute__((aligned(VRING_ALIGN)));

/* ---- transmitq (index 1) ---- */
static volatile u8 txq_bytes[VNET_VRING_TOTAL_BYTES] __attribute__((aligned(VRING_ALIGN)));

/* RX packet buffers */
static u8 rx_buf[RX_BUFS][PKT_BUF_SIZE] __attribute__((aligned(16)));

/* TX staging buffers (multi-buffer pool to prevent in-flight clobbering) */
#define TX_BUFS 16
static u8  tx_hdr_buf[TX_BUFS][sizeof(vnet_hdr_t)] __attribute__((aligned(16)));
static u8  tx_pkt_buf[TX_BUFS][1536] __attribute__((aligned(16)));

static u32 io_base = 0;
static int net_ready = 0;
static u8  nic_mac[6] = {0};

/* Avail/used ring offsets into q_bytes */
#define RXQ_DESC_OFF   0
#define RXQ_AVAIL_OFF  4096
#define RXQ_USED_OFF   8192

#define TXQ_DESC_OFF   0
#define TXQ_AVAIL_OFF  4096
#define TXQ_USED_OFF   8192

static u16 rxq_avail_idx = 0;
static u16 rxq_used_last = 0;
static u16 txq_avail_idx = 0;
static u16 txq_used_last = 0;

/* Accessors for RX queue */
static volatile vring_desc_t  *rxq_desc(void)  { return (volatile vring_desc_t*)(rxq_bytes + RXQ_DESC_OFF); }
static volatile u16           *rxq_avail(void) { return (volatile u16*)(rxq_bytes + RXQ_AVAIL_OFF); }
static volatile vring_used_elem_t *rxq_used_ring(void) {
    return (volatile vring_used_elem_t*)(rxq_bytes + RXQ_USED_OFF + 4);
}
static volatile u16 *rxq_used_idx(void) { return (volatile u16*)(rxq_bytes + RXQ_USED_OFF + 2); }

/* Accessors for TX queue */
static volatile vring_desc_t  *txq_desc(void)  { return (volatile vring_desc_t*)(txq_bytes + TXQ_DESC_OFF); }
static volatile u16           *txq_avail(void) { return (volatile u16*)(txq_bytes + TXQ_AVAIL_OFF); }
static volatile u16 *txq_used_idx(void) { return (volatile u16*)(txq_bytes + TXQ_USED_OFF + 2); }

static inline u8  v_in8(u16 off)  { return inb((u16)(io_base + off)); }
static inline u16 v_in16(u16 off) { u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"((u16)(io_base + off))); return v; }
static inline u32 v_in32(u16 off) { u32 v; __asm__ volatile("inl %1,%0":"=a"(v):"Nd"((u16)(io_base + off))); return v; }
static inline void v_out8(u16 off, u8 v)   { outb((u16)(io_base + off), v); }
static inline void v_out16(u16 off, u16 v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"((u16)(io_base + off))); }
static inline void v_out32(u16 off, u32 v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"((u16)(io_base + off))); }

static void setup_queue(int qidx, volatile u8 *q_bytes, u32 q_pfn_base) {
    (void)q_bytes;
    v_out16(VNET_PCI_QUEUE_SEL, (u16)qidx);
    u16 qsize = v_in16(VNET_PCI_QUEUE_NUM);
    serial_write("[vnet] queue "); serial_write_hex((u32)qidx);
    serial_write(" qsize="); serial_write_hex((u32)qsize);
    if (qsize == 0) return;

    u32 pfn = q_pfn_base / VRING_ALIGN;
    v_out32(VNET_PCI_QUEUE_PFN, pfn);
    serial_write(" pfn="); serial_write_hex(pfn);
    u32 check = v_in32(VNET_PCI_QUEUE_PFN);
    serial_write(" check="); serial_write_hex(check); serial_write("\n");
}

static void rxq_post_buf(int i) {
    if (i < 0 || i >= RX_BUFS) return;
    volatile vring_desc_t *d = rxq_desc();
    d[i].addr  = (u32)(unsigned long)rx_buf[i];
    d[i].len   = PKT_BUF_SIZE;
    d[i].flags = VRING_DESC_F_WRITE;
    d[i].next  = 0;

    volatile u16 *avail = rxq_avail();
    avail[2 + (rxq_avail_idx % QUEUE_SIZE)] = (u16)i;
    __asm__ volatile("" ::: "memory");
    avail[1] = ++rxq_avail_idx;
}

int virtio_net_init(void) {
    /* Try legacy 0x1000, then modern 0x1041, then any Ethernet class (0x02, 0x00) */
    const pci_device_t *dev = pci_find_device(VIRTIO_VENDOR_ID, 0x1000);
    if (!dev) dev = pci_find_device(VIRTIO_VENDOR_ID, 0x1041);
    if (!dev) dev = pci_find_class(0x02, 0x00);
    if (!dev) {
        serial_write("[vnet] no NIC found\n");
        return 0;
    }
    serial_write("[vnet] NIC vendor="); serial_write_hex(dev->vendor_id);
    serial_write(" dev="); serial_write_hex(dev->device_id); serial_write("\n");

    u32 bar = pci_read_bar32(dev, 0);
    if (!(bar & 1)) { serial_write("[vnet] BAR0 not IO\n"); return 0; }
    io_base = bar & ~3u;
    serial_write("[vnet] io_base="); serial_write_hex(io_base); serial_write("\n");

    pci_enable_bus_master(dev);

    /* 1. Reset device */
    v_out8(VNET_PCI_STATUS, 0);
    /* 2. ACK */
    v_out8(VNET_PCI_STATUS, STATUS_ACK);
    /* 3. DRIVER */
    v_out8(VNET_PCI_STATUS, STATUS_ACK | STATUS_DRIVER);

    /* 4. Negotiate features (GUEST_FEATURES = 0 for standard legacy frames) */
    u32 host_feat = v_in32(VNET_PCI_HOST_FEATURES);
    serial_write("[vnet] host_feat="); serial_write_hex(host_feat); serial_write("\n");
    v_out32(VNET_PCI_GUEST_FEATURES, 0);

    /* Read MAC address */
    int i;
    for (i = 0; i < 6; i++) nic_mac[i] = v_in8((u16)(VNET_PCI_MAC0 + i));
    serial_write("[vnet] mac=");
    for (i = 0; i < 6; i++) { serial_write_hex(nic_mac[i]); if (i < 5) serial_write(":"); }
    serial_write("\n");

    /* 5. Set up RX queue (0) */
    memset((void*)rxq_bytes, 0, sizeof(rxq_bytes));
    setup_queue(0, rxq_bytes, (u32)(unsigned long)rxq_bytes);

    /* 6. Set up TX queue (1) */
    memset((void*)txq_bytes, 0, sizeof(txq_bytes));
    setup_queue(1, txq_bytes, (u32)(unsigned long)txq_bytes);

    /* Verify both queues are intact and non-zero */
    v_out16(VNET_PCI_QUEUE_SEL, 0);
    serial_write("[vnet] q0 final pfn="); serial_write_hex(v_in32(VNET_PCI_QUEUE_PFN)); serial_write("\n");
    v_out16(VNET_PCI_QUEUE_SEL, 1);
    serial_write("[vnet] q1 final pfn="); serial_write_hex(v_in32(VNET_PCI_QUEUE_PFN)); serial_write("\n");

    /* 7. Pre-fill RX descriptors before DRIVER_OK per virtio spec */
    rxq_avail_idx = 0;
    rxq_used_last = 0;
    for (i = 0; i < RX_BUFS; i++) rxq_post_buf(i);

    /* 8. Status: DRIVER_OK (device is now live with populated RX queue) */
    v_out8(VNET_PCI_STATUS, STATUS_ACK | STATUS_DRIVER | STATUS_DRIVER_OK);

    /* 9. Notify receiveq */
    v_out16(VNET_PCI_QUEUE_NOTIFY, 0);

    net_ready = 1;
    serial_write("[vnet] ready\n");
    return 1;
}

int virtio_net_ready(void) { return net_ready; }

void virtio_net_get_mac(u8 out_mac[6]) {
    int i; for (i = 0; i < 6; i++) out_mac[i] = nic_mac[i];
}

void virtio_net_get_stats(virtio_net_stats_t *out_stats) {
    if (out_stats) {
        memcpy(out_stats, &g_vnet_stats, sizeof(virtio_net_stats_t));
    }
}

int virtio_net_tx(const void *data, u16 len) {
    if (!net_ready || len == 0 || len > 1514) return 0;

    int slot = txq_avail_idx % TX_BUFS;
    memset(tx_hdr_buf[slot], 0, sizeof(vnet_hdr_t));
    memcpy(tx_pkt_buf[slot], data, len);

    /* 2-descriptor chain: Descriptor 0: virtio-net header, Descriptor 1: payload */
    volatile vring_desc_t *d = txq_desc();
    int d0 = slot * 2;
    int d1 = d0 + 1;

    d[d0].addr  = (u32)(unsigned long)tx_hdr_buf[slot];
    d[d0].len   = sizeof(vnet_hdr_t);
    d[d0].flags = VRING_DESC_F_NEXT;
    d[d0].next  = (u16)d1;

    d[d1].addr  = (u32)(unsigned long)tx_pkt_buf[slot];
    d[d1].len   = len;
    d[d1].flags = 0;
    d[d1].next  = 0;

    /* Post to avail ring */
    volatile u16 *avail = txq_avail();
    avail[2 + (txq_avail_idx % QUEUE_SIZE)] = (u16)d0;
    __asm__ volatile("" ::: "memory");
    avail[1] = (u16)(txq_avail_idx + 1);
    txq_avail_idx++;

    /* Notify TX queue (queue index 1) */
    v_out16(VNET_PCI_QUEUE_NOTIFY, 1);

#if VNET_PACKET_LOG
    serial_write("[vnet_tx] len="); serial_write_hex((u32)len);
    serial_write(" avail="); serial_write_hex((u32)txq_avail_idx);
#endif

    /* Spin-wait for used ring update with pause to yield host CPU */
    int spin;
    for (spin = 0; spin < 2000000; spin++) {
        __asm__ volatile("pause" ::: "memory");
        if (*txq_used_idx() != txq_used_last) {
            txq_used_last = *txq_used_idx();
#if VNET_PACKET_LOG
            serial_write(" OK used="); serial_write_hex((u32)txq_used_last); serial_write("\n");
#endif
            g_vnet_stats.tx_packets++;
            g_vnet_stats.tx_bytes += len;
            return 1;
        }
    }
    if (*txq_used_idx() != txq_used_last) {
        txq_used_last = *txq_used_idx();
    }
    g_vnet_stats.tx_timeouts++;
    serial_write("[vnet_tx] completion timeout\n");
    return 1;
}

int virtio_net_rx_poll(void *buf, u16 *out_len) {
    if (!net_ready) return 0;
    /* Kick RX queue so QEMU flushes any pending incoming packets */
    v_out16(VNET_PCI_QUEUE_NOTIFY, 0);
    __asm__ volatile("" ::: "memory");
    u16 r_used = *rxq_used_idx();
    if (r_used == rxq_used_last) return 0;

#if VNET_PACKET_LOG
    serial_write("[vnet_rx] got packet! r_used="); serial_write_hex((u32)r_used);
    serial_write(" last="); serial_write_hex((u32)rxq_used_last); serial_write("\n");
#endif

    volatile vring_used_elem_t *used = rxq_used_ring();
    u32 idx = rxq_used_last % QUEUE_SIZE;
    u32 desc_id = used[idx].id;
    u32 pkt_len = used[idx].len;
    rxq_used_last++;

    if (desc_id >= RX_BUFS) desc_id = 0;

    /* Received frame: vnet_hdr (10 bytes) + ethernet frame */
    u32 eth_len = (pkt_len > sizeof(vnet_hdr_t)) ? (pkt_len - sizeof(vnet_hdr_t)) : 0;
    if (eth_len > 1514) eth_len = 1514;

    if (eth_len > 0 && buf && out_len) {
        memcpy(buf, rx_buf[desc_id] + sizeof(vnet_hdr_t), eth_len);
        *out_len = (u16)eth_len;
        g_vnet_stats.rx_packets++;
        g_vnet_stats.rx_bytes += eth_len;
    } else {
        g_vnet_stats.rx_drops++;
    }

    /* Re-post this descriptor buffer */
    rxq_post_buf((int)desc_id);
    v_out16(VNET_PCI_QUEUE_NOTIFY, 0);

    return (eth_len > 0) ? 1 : 0;
}
