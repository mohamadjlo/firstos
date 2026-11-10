#ifndef BB_HOST_BUILD

#include "bb_osal.h"
#include "bb_libc.h"

/* FirstOS Syscall wrappers */
static inline int sys_call0(int num) {
    long res;
    __asm__ volatile ("int $0x80" : "=a"(res) : "a"((long)num) : "rcx", "r11", "memory");
    return (int)res;
}

static inline int sys_call1(int num, long arg1) {
    long res;
    __asm__ volatile ("int $0x80" : "=a"(res) : "a"((long)num), "D"(arg1) : "rcx", "r11", "memory");
    return (int)res;
}

static inline int sys_call2(int num, long arg1, long arg2) {
    long res;
    __asm__ volatile ("int $0x80" : "=a"(res) : "a"((long)num), "D"(arg1), "S"(arg2) : "rcx", "r11", "memory");
    return (int)res;
}

static inline int sys_call3(int num, long arg1, long arg2, long arg3) {
    long res;
    __asm__ volatile ("int $0x80" : "=a"(res) : "a"((long)num), "D"(arg1), "S"(arg2), "d"(arg3) : "rcx", "r11", "memory");
    return (int)res;
}

/* FirstOS Framebuffer State */
static bb_fb_info_t g_firstos_fb;
#define FIRSTOS_SCREEN_W 1024
#define FIRSTOS_SCREEN_H 768

/* Static display buffer for standalone execution */
static uint32_t g_firstos_pixels[FIRSTOS_SCREEN_W * FIRSTOS_SCREEN_H] __attribute__((aligned(16)));

int bb_osal_init(uint32_t width, uint32_t height) {
    if (width == 0 || width > FIRSTOS_SCREEN_W) width = FIRSTOS_SCREEN_W;
    if (height == 0 || height > FIRSTOS_SCREEN_H) height = FIRSTOS_SCREEN_H;

    g_firstos_fb.width = width;
    g_firstos_fb.height = height;
    g_firstos_fb.pitch = width * sizeof(uint32_t);
    g_firstos_fb.bpp = 32;
    g_firstos_fb.pixels = g_firstos_pixels;

    bb_memset(g_firstos_pixels, 0xFF, width * height * sizeof(uint32_t));
    bb_osal_log("[FirstOS Browser] OSAL initialized with 1024x768 32-bpp ARGB framebuffer\n");
    return 0;
}

void bb_osal_shutdown(void) {
    bb_osal_log("[FirstOS Browser] OSAL shutdown\n");
}

bb_fb_info_t *bb_osal_get_fb(void) {
    return &g_firstos_fb;
}

void bb_osal_present_fb(void) {
    /* If X11 display socket exists, we can flush dirty regions.
       Or directly blit to linear framebuffer if mapped. */
    __asm__ volatile ("int $0x81"); /* Yield to FirstOS scheduler / compositor */
}

int bb_osal_poll_event(bb_event_t *ev) {
    if (!ev) return 0;
    /* In FirstOS userspace, event polling will check keyboard / mouse device or queue */
    return 0;
}

uint32_t bb_osal_get_ticks(void) {
    /* Read x86 Time Stamp Counter (RDTSC) scaled to approximate milliseconds */
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    /* Assuming ~2.0 GHz CPU clock: 2,000,000 cycles per ms.
       Avoid 64-bit division (__udivdi3) by computing in 32-bit:
       2^32 / 2000000 = 2147 */
    return hi * 2147 + (lo / 2000000);
}

void bb_osal_sleep_ms(uint32_t ms) {
    uint32_t start = bb_osal_get_ticks();
    while (bb_osal_get_ticks() - start < ms) {
        __asm__ volatile ("int $0x81"); /* sys_yield */
    }
}

void bb_osal_log(const char *msg) {
    if (!msg) return;
    /* FirstOS sys_print is syscall 0 */
    sys_call1(0, (long)(uintptr_t)msg);
}

/* FirstOS Networking via socketcall (102) */
typedef struct {
    int fd;
} firstos_sock_t;

static firstos_sock_t g_sock_pool[4];
static int g_sock_in_use[4] = {0};

bb_socket_t bb_osal_net_connect(const char *host, uint16_t port) {
    (void)host; (void)port;
    /* Connect via FirstOS socketcall or custom IPC */
    bb_osal_log("[FirstOS Browser] Connecting to network host...\n");
    int i;
    for (i = 0; i < 4; i++) {
        if (!g_sock_in_use[i]) {
            g_sock_in_use[i] = 1;
            g_sock_pool[i].fd = i + 10;
            return (bb_socket_t)&g_sock_pool[i];
        }
    }
    return NULL;
}

int bb_osal_net_send(bb_socket_t sock, const void *data, size_t len) {
    if (!sock || !data) return -1;
    /* FirstOS socket send */
    return (int)len;
}

int bb_osal_net_recv(bb_socket_t sock, void *buf, size_t max_len, uint32_t timeout_ms) {
    if (!sock || !buf) return -1;
    (void)max_len;
    (void)timeout_ms;
    /* FirstOS socket recv */
    return 0;
}

void bb_osal_net_close(bb_socket_t sock) {
    if (!sock) return;
    firstos_sock_t *s = (firstos_sock_t *)sock;
    int i;
    for (i = 0; i < 4; i++) {
        if (&g_sock_pool[i] == s) {
            g_sock_in_use[i] = 0;
            break;
        }
    }
}

#endif /* !BB_HOST_BUILD */
