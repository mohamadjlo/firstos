#ifdef BB_HOST_BUILD

#include "bb_osal.h"
#include "bb_libc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

static bb_fb_info_t g_host_fb;
static uint32_t *g_host_pixels = NULL;

/* Simple ring buffer for simulated input events */
#define MAX_EVENTS 64
static bb_event_t g_event_queue[MAX_EVENTS];
static int g_event_head = 0;
static int g_event_tail = 0;

int bb_osal_init(uint32_t width, uint32_t height) {
    if (width == 0) width = 1024;
    if (height == 0) height = 768;

    g_host_fb.width = width;
    g_host_fb.height = height;
    g_host_fb.pitch = width * sizeof(uint32_t);
    g_host_fb.bpp = 32;

    g_host_pixels = (uint32_t *)malloc(width * height * sizeof(uint32_t));
    if (!g_host_pixels) return -1;

    memset(g_host_pixels, 0xFF, width * height * sizeof(uint32_t));
    g_host_fb.pixels = g_host_pixels;

    g_event_head = 0;
    g_event_tail = 0;
    return 0;
}

void bb_osal_shutdown(void) {
    if (g_host_pixels) {
        free(g_host_pixels);
        g_host_pixels = NULL;
    }
}

bb_fb_info_t *bb_osal_get_fb(void) {
    return &g_host_fb;
}

void bb_osal_present_fb(void) {
    /* In host simulator, frame presentation is a no-op unless dumping to disk */
}

void bb_osal_host_push_event(const bb_event_t *ev) {
    int next = (g_event_tail + 1) % MAX_EVENTS;
    if (next != g_event_head) {
        g_event_queue[g_event_tail] = *ev;
        g_event_tail = next;
    }
}

int bb_osal_poll_event(bb_event_t *ev) {
    if (g_event_head == g_event_tail) {
        return 0; /* No events */
    }
    *ev = g_event_queue[g_event_head];
    g_event_head = (g_event_head + 1) % MAX_EVENTS;
    return 1;
}

/* BMP file header structures */
#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMPHeader;

typedef struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMPInfoHeader;
#pragma pack(pop)

int bb_osal_host_dump_bmp(const char *filename) {
    if (!g_host_pixels || !filename) return -1;
    FILE *f = fopen(filename, "wb");
    if (!f) return -1;

    uint32_t w = g_host_fb.width;
    uint32_t h = g_host_fb.height;
    uint32_t row_size = w * 4;
    uint32_t image_size = row_size * h;

    BMPHeader hdr;
    hdr.bfType = 0x4D42; /* "BM" */
    hdr.bfSize = sizeof(BMPHeader) + sizeof(BMPInfoHeader) + image_size;
    hdr.bfReserved1 = 0;
    hdr.bfReserved2 = 0;
    hdr.bfOffBits = sizeof(BMPHeader) + sizeof(BMPInfoHeader);

    BMPInfoHeader info;
    info.biSize = sizeof(BMPInfoHeader);
    info.biWidth = (int32_t)w;
    info.biHeight = -(int32_t)h; /* Top-down BMP */
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = 0; /* BI_RGB */
    info.biSizeImage = image_size;
    info.biXPelsPerMeter = 2835;
    info.biYPelsPerMeter = 2835;
    info.biClrUsed = 0;
    info.biClrImportant = 0;

    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(&info, sizeof(info), 1, f);

    /* Write 32-bpp BGRA/ARGB data */
    /* Our format is ARGB: 0xAARRGGBB. On little-endian, byte order in memory is: B, G, R, A.
       Standard 32-bit BMP expects B, G, R, A, which matches little-endian uint32_t directly! */
    fwrite(g_host_pixels, 4, w * h, f);
    fclose(f);
    return 0;
}

uint32_t bb_osal_get_ticks(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)((tv.tv_sec * 1000) + (tv.tv_usec / 1000));
}

void bb_osal_sleep_ms(uint32_t ms) {
    usleep(ms * 1000);
}

void bb_osal_log(const char *msg) {
    if (msg) {
        fprintf(stderr, "%s", msg);
    }
}

/* Networking */
typedef struct {
    int fd;
} host_sock_t;

bb_socket_t bb_osal_net_connect(const char *host, uint16_t port) {
    if (!host) return NULL;

    struct hostent *he = gethostbyname(host);
    if (!he) return NULL;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return NULL;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr, he->h_addr_list[0], he->h_length);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return NULL;
    }

    host_sock_t *s = (host_sock_t *)malloc(sizeof(host_sock_t));
    s->fd = fd;
    return (bb_socket_t)s;
}

int bb_osal_net_send(bb_socket_t sock, const void *data, size_t len) {
    if (!sock || !data) return -1;
    host_sock_t *s = (host_sock_t *)sock;
    ssize_t sent = send(s->fd, data, len, 0);
    return (int)sent;
}

int bb_osal_net_recv(bb_socket_t sock, void *buf, size_t max_len, uint32_t timeout_ms) {
    if (!sock || !buf || max_len == 0) return -1;
    host_sock_t *s = (host_sock_t *)sock;

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(s->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t received = recv(s->fd, buf, max_len, 0);
    return (int)received;
}

void bb_osal_net_close(bb_socket_t sock) {
    if (!sock) return;
    host_sock_t *s = (host_sock_t *)sock;
    if (s->fd >= 0) {
        close(s->fd);
    }
    free(s);
}

#endif /* BB_HOST_BUILD */
