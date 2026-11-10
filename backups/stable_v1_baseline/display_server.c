#include "display_server.h"
#include "heap.h"
#include "serial.h"
#include "virtio_gpu.h"
#include "virtio_gpu_2d.h"
#include <string.h>

display_server_state_t g_display_server = {0};

/* Ring buffer for outgoing X11 events/replies back to client */
#define X11_EVENT_RING_SIZE 4096
static u8 event_ring[X11_EVENT_RING_SIZE];
static u32 ring_head = 0;
static u32 ring_tail = 0;

static void ring_push(const void *src, u32 len) {
    const u8 *p = (const u8 *)src;
    for (u32 i = 0; i < len; i++) {
        event_ring[ring_head] = p[i];
        ring_head = (ring_head + 1) % X11_EVENT_RING_SIZE;
        if (ring_head == ring_tail) {
            ring_tail = (ring_tail + 1) % X11_EVENT_RING_SIZE; /* Drop oldest on overflow */
        }
    }
}

static u32 ring_pop(void *dst, u32 max_len) {
    u8 *p = (u8 *)dst;
    u32 count = 0;
    while (ring_tail != ring_head && count < max_len) {
        p[count++] = event_ring[ring_tail];
        ring_tail = (ring_tail + 1) % X11_EVENT_RING_SIZE;
    }
    return count;
}

void display_server_init(void) {
    if (g_display_server.active) return;
    
    g_display_server.surface_w = 944;
    g_display_server.surface_h = 620;
    u32 total_bytes = g_display_server.surface_w * g_display_server.surface_h * 4;
    
    g_display_server.surface_pixels = (u32 *)kmalloc_aligned(total_bytes, 4096);
    if (g_display_server.surface_pixels) {
        /* Initialize surface with clean default page background (pure white) */
        for (u32 i = 0; i < g_display_server.surface_w * g_display_server.surface_h; i++) {
            g_display_server.surface_pixels[i] = 0xFFFFFFFF;
        }
    }
    
    g_display_server.active = 1;
    g_display_server.client_connected = 0;
    g_display_server.client_fd = 0;
    g_display_server.dirty = 1;
    g_display_server.dirty_x = 0;
    g_display_server.dirty_y = 0;
    g_display_server.dirty_w = g_display_server.surface_w;
    g_display_server.dirty_h = g_display_server.surface_h;
    g_display_server.window_id = 0x00000001;
    strcpy(g_display_server.window_title, "FirstOS X11 Client");
    g_display_server.mapped = 0;
    g_display_server.sequence_number = 1;
    
    serial_write("[display_server] FirstOS Native X11 Display Server initialized (944x620)\n");
}

void display_server_set_client_connected(int connected, u32 fd) {
    g_display_server.client_connected = connected;
    g_display_server.client_fd = fd;
    if (connected) {
        serial_write("[display_server] X11 client connected to /tmp/.X11-unix/X0\n");
    } else {
        serial_write("[display_server] X11 client disconnected\n");
    }
}

void display_server_mark_dirty(u32 x, u32 y, u32 w, u32 h) {
    if (x >= g_display_server.surface_w || y >= g_display_server.surface_h) return;
    if (x + w > g_display_server.surface_w) w = g_display_server.surface_w - x;
    if (y + h > g_display_server.surface_h) h = g_display_server.surface_h - y;
    if (!w || !h) return;

    if (!g_display_server.dirty) {
        g_display_server.dirty_x = x;
        g_display_server.dirty_y = y;
        g_display_server.dirty_w = w;
        g_display_server.dirty_h = h;
        g_display_server.dirty = 1;
    } else {
        u32 x2 = (g_display_server.dirty_x + g_display_server.dirty_w > x + w) ?
                 (g_display_server.dirty_x + g_display_server.dirty_w) : (x + w);
        u32 y2 = (g_display_server.dirty_y + g_display_server.dirty_h > y + h) ?
                 (g_display_server.dirty_y + g_display_server.dirty_h) : (y + h);
        g_display_server.dirty_x = (g_display_server.dirty_x < x) ? g_display_server.dirty_x : x;
        g_display_server.dirty_y = (g_display_server.dirty_y < y) ? g_display_server.dirty_y : y;
        g_display_server.dirty_w = x2 - g_display_server.dirty_x;
        g_display_server.dirty_h = y2 - g_display_server.dirty_y;
    }
}

/* Encode an X11 Expose event (32 bytes) */
void display_server_send_expose(void) {
    u8 ev[32] = {0};
    ev[0] = 12; /* Expose */
    ev[1] = 0;
    *(u16 *)&ev[2] = (u16)g_display_server.sequence_number;
    *(u32 *)&ev[4] = g_display_server.window_id; /* window */
    *(u16 *)&ev[8] = 0; /* x */
    *(u16 *)&ev[10] = 0; /* y */
    *(u16 *)&ev[12] = (u16)g_display_server.surface_w; /* width */
    *(u16 *)&ev[14] = (u16)g_display_server.surface_h; /* height */
    *(u16 *)&ev[16] = 0; /* count */
    ring_push(ev, 32);
}

/* Encode KeyPress/KeyRelease event (32 bytes) */
void display_server_send_key(unsigned char key, char ascii, int pressed) {
    (void)ascii;
    u8 ev[32] = {0};
    ev[0] = pressed ? 2 : 3; /* KeyPress = 2, KeyRelease = 3 */
    ev[1] = key ? key : 0x24; /* keycode */
    *(u16 *)&ev[2] = (u16)g_display_server.sequence_number;
    *(u32 *)&ev[4] = 1000; /* time */
    *(u32 *)&ev[8] = 0x00000023; /* root window */
    *(u32 *)&ev[12] = g_display_server.window_id; /* event window */
    *(u32 *)&ev[16] = 0; /* child window */
    *(u16 *)&ev[20] = 100; /* root_x */
    *(u16 *)&ev[22] = 100; /* root_y */
    *(u16 *)&ev[24] = 100; /* event_x */
    *(u16 *)&ev[26] = 100; /* event_y */
    *(u16 *)&ev[28] = 0;   /* state */
    ev[30] = 1;            /* sameScreen */
    ring_push(ev, 32);
}

/* Encode ButtonPress/ButtonRelease event (32 bytes) */
void display_server_send_button(int button, int pressed, int x, int y) {
    u8 ev[32] = {0};
    ev[0] = pressed ? 4 : 5; /* ButtonPress = 4, ButtonRelease = 5 */
    ev[1] = (u8)button;      /* detail (1=Left, 2=Middle, 3=Right, 4=WheelUp, 5=WheelDown) */
    *(u16 *)&ev[2] = (u16)g_display_server.sequence_number;
    *(u32 *)&ev[4] = 1000;
    *(u32 *)&ev[8] = 0x00000023;
    *(u32 *)&ev[12] = g_display_server.window_id;
    *(u32 *)&ev[16] = 0;
    *(u16 *)&ev[20] = (u16)x;
    *(u16 *)&ev[22] = (u16)y;
    *(u16 *)&ev[24] = (u16)x;
    *(u16 *)&ev[26] = (u16)y;
    *(u16 *)&ev[28] = 0;
    ev[30] = 1;
    ring_push(ev, 32);
}

/* Encode MotionNotify event (32 bytes) */
void display_server_send_motion(int x, int y) {
    u8 ev[32] = {0};
    ev[0] = 6; /* MotionNotify */
    ev[1] = 0;
    *(u16 *)&ev[2] = (u16)g_display_server.sequence_number;
    *(u32 *)&ev[4] = 1000;
    *(u32 *)&ev[8] = 0x00000023;
    *(u32 *)&ev[12] = g_display_server.window_id;
    *(u32 *)&ev[16] = 0;
    *(u16 *)&ev[20] = (u16)x;
    *(u16 *)&ev[22] = (u16)y;
    *(u16 *)&ev[24] = (u16)x;
    *(u16 *)&ev[26] = (u16)y;
    *(u16 *)&ev[28] = 0;
    ev[30] = 1;
    ring_push(ev, 32);
}

/* Process incoming X11 data stream from a Linux client */
int display_server_handle_client_data(const u8 *data, u32 len, u8 *out_resp, u32 max_resp_len) {
    if (!data || !len) {
        /* Check if there are queued events to drain */
        return (int)ring_pop(out_resp, max_resp_len);
    }

    static int handshake_done = 0;

    /* Handle Initial Connection Setup Handshake */
    if (!handshake_done && len >= 12) {
        handshake_done = 1;
        serial_write("[display_server] X11 Client Handshake received\n");

        /* Build X11 Connection Setup Success structure */
        u8 resp[512] = {0};
        resp[0] = 1;  /* Success */
        resp[1] = 0;  /* unused */
        *(u16 *)&resp[2] = 11; /* major = 11 */
        *(u16 *)&resp[4] = 0;  /* minor = 0 */
        
        const char *vendor = "FirstOS-Native-VirtIOGPU";
        u16 vendor_len = (u16)strlen(vendor);
        u16 vendor_pad = (4 - (vendor_len % 4)) % 4;

        *(u32 *)&resp[8]  = 12600000;   /* release number */
        *(u32 *)&resp[12] = 0x00000001; /* resource id base */
        *(u32 *)&resp[16] = 0x000FFFFF; /* resource id mask */
        *(u32 *)&resp[20] = 256;        /* motion buffer size */
        *(u16 *)&resp[24] = vendor_len; /* nbytes vendor */
        *(u16 *)&resp[26] = 65535;      /* max request length */
        resp[28] = 1;                   /* num roots (1 screen) */
        resp[29] = 1;                   /* num formats (1 format: 32bpp) */
        resp[30] = 0;                   /* image byte order = LSBFirst */
        resp[31] = 0;                   /* bitmap bit order = LSBFirst */
        resp[32] = 32;                  /* scanline unit */
        resp[33] = 32;                  /* scanline pad */
        resp[34] = 8;                   /* min keycode */
        resp[35] = 255;                 /* max keycode */

        u32 offset = 40;
        /* Copy vendor string with padding */
        memcpy(&resp[offset], vendor, vendor_len);
        offset += vendor_len + vendor_pad;

        /* Format list: depth 24, bpp 32, scanline-pad 32 */
        resp[offset + 0] = 24; /* depth */
        resp[offset + 1] = 32; /* bpp */
        resp[offset + 2] = 32; /* scanline pad */
        offset += 8;

        /* Screen descriptor */
        *(u32 *)&resp[offset + 0]  = 0x00000023; /* root window ID */
        *(u32 *)&resp[offset + 4]  = 0x00000024; /* default colormap ID */
        *(u32 *)&resp[offset + 8]  = 0xFFFFFFFF; /* white pixel */
        *(u32 *)&resp[offset + 12] = 0x00000000; /* black pixel */
        *(u32 *)&resp[offset + 16] = 0;          /* current input masks */
        *(u16 *)&resp[offset + 20] = (u16)g_display_server.surface_w; /* width pixels */
        *(u16 *)&resp[offset + 22] = (u16)g_display_server.surface_h; /* height pixels */
        *(u16 *)&resp[offset + 24] = 250;        /* width mm */
        *(u16 *)&resp[offset + 26] = 164;        /* height mm */
        *(u16 *)&resp[offset + 28] = 1;          /* min maps */
        *(u16 *)&resp[offset + 30] = 1;          /* max maps */
        *(u32 *)&resp[offset + 32] = 0x00000020; /* root visual ID */
        resp[offset + 36] = 2;                   /* backing store = Always */
        resp[offset + 37] = 1;                   /* save unders */
        resp[offset + 38] = 24;                  /* root depth */
        resp[offset + 39] = 1;                   /* num depths */
        offset += 40;

        /* Depth descriptor */
        resp[offset + 0] = 24; /* depth */
        resp[offset + 1] = 0;  /* unused */
        *(u16 *)&resp[offset + 2] = 1; /* num visuals */
        offset += 8;

        /* Visual descriptor (24-bit TrueColor) */
        *(u32 *)&resp[offset + 0]  = 0x00000020; /* visualID */
        resp[offset + 4] = 4;                    /* class = TrueColor */
        resp[offset + 5] = 8;                    /* bits per rgb */
        *(u16 *)&resp[offset + 6]  = 256;        /* colormap entries */
        *(u32 *)&resp[offset + 8]  = 0x00FF0000; /* red mask */
        *(u32 *)&resp[offset + 12] = 0x0000FF00; /* green mask */
        *(u32 *)&resp[offset + 16] = 0x000000FF; /* blue mask */
        offset += 24;

        /* Set length in 4-byte units minus header */
        *(u16 *)&resp[6] = (u16)((offset - 8) / 4);

        u32 copy_len = (offset <= max_resp_len) ? offset : max_resp_len;
        memcpy(out_resp, resp, copy_len);
        return (int)copy_len;
    }

    /* Process X11 protocol request stream */
    u32 cursor = 0;
    while (cursor + 4 <= len) {
        u8 opcode = data[cursor + 0];
        u16 req_len_words = *(u16 *)&data[cursor + 2];
        u32 req_len_bytes = (u32)req_len_words * 4;
        if (!req_len_bytes || cursor + req_len_bytes > len) break;

        g_display_server.sequence_number++;

        switch (opcode) {
            case 1: /* X_CreateWindow */
                if (cursor + 32 <= len) {
                    g_display_server.window_id = *(u32 *)&data[cursor + 4];
                    u16 w = *(u16 *)&data[cursor + 16];
                    u16 h = *(u16 *)&data[cursor + 18];
                    if (w > 0 && h > 0 && w <= X11_SURFACE_MAX_W && h <= X11_SURFACE_MAX_H) {
                        g_display_server.surface_w = w;
                        g_display_server.surface_h = h;
                    }
                    serial_write("[display_server] X_CreateWindow received\n");
                }
                break;

            case 8: /* X_MapWindow */
                g_display_server.mapped = 1;
                display_server_mark_dirty(0, 0, g_display_server.surface_w, g_display_server.surface_h);
                display_server_send_expose();
                serial_write("[display_server] X_MapWindow received - client window exposed\n");
                break;

            case 12: /* X_ConfigureWindow */
                break;

            case 14: /* X_GetGeometry */ {
                u8 reply[32] = {0};
                reply[0] = 1; /* Reply */
                reply[1] = 24; /* Depth */
                *(u16 *)&reply[2] = (u16)g_display_server.sequence_number;
                *(u32 *)&reply[4] = 0; /* length */
                *(u32 *)&reply[8] = 0x00000023; /* root */
                *(u16 *)&reply[12] = 0; /* x */
                *(u16 *)&reply[14] = 0; /* y */
                *(u16 *)&reply[16] = (u16)g_display_server.surface_w;
                *(u16 *)&reply[18] = (u16)g_display_server.surface_h;
                *(u16 *)&reply[20] = 0; /* border */
                ring_push(reply, 32);
                break;
            }

            case 18: /* X_ChangeProperty */
                /* Could extract window title */
                break;

            case 43: /* X_GetInputFocus */ {
                u8 reply[32] = {0};
                reply[0] = 1;
                reply[1] = 1; /* revert to Parent */
                *(u16 *)&reply[2] = (u16)g_display_server.sequence_number;
                *(u32 *)&reply[8] = g_display_server.window_id; /* focus */
                ring_push(reply, 32);
                break;
            }

            case 70: /* X_PolyFillRectangle */
                /* Fill background rectangle directly on surface */
                if (cursor + 20 <= len && g_display_server.surface_pixels) {
                    u16 rx = *(u16 *)&data[cursor + 12];
                    u16 ry = *(u16 *)&data[cursor + 14];
                    u16 rw = *(u16 *)&data[cursor + 16];
                    u16 rh = *(u16 *)&data[cursor + 18];
                    u32 col = 0xFFFFFFFF;
                    for (u32 py = ry; py < (u32)(ry + rh) && py < g_display_server.surface_h; py++) {
                        for (u32 px = rx; px < (u32)(rx + rw) && px < g_display_server.surface_w; px++) {
                            g_display_server.surface_pixels[py * g_display_server.surface_w + px] = col;
                        }
                    }
                    display_server_mark_dirty(rx, ry, rw, rh);
                }
                break;

            case 72: /* X_PutImage (Core raster pixel transfer) */
                if (cursor + 24 <= len && g_display_server.surface_pixels) {
                    u16 img_w = *(u16 *)&data[cursor + 8];
                    u16 img_h = *(u16 *)&data[cursor + 10];
                    u16 dst_x = *(u16 *)&data[cursor + 12];
                    u16 dst_y = *(u16 *)&data[cursor + 14];
                    u32 pix_offset = cursor + 24;
                    const u32 *src_pixels = (const u32 *)&data[pix_offset];

                    if (cursor + 24 + img_w * img_h * 4 <= len) {
                        for (u32 row = 0; row < img_h && (dst_y + row) < g_display_server.surface_h; row++) {
                            u32 dest_idx = (dst_y + row) * g_display_server.surface_w + dst_x;
                            u32 src_idx = row * img_w;
                            for (u32 col = 0; col < img_w && (dst_x + col) < g_display_server.surface_w; col++) {
                                g_display_server.surface_pixels[dest_idx + col] = src_pixels[src_idx + col];
                            }
                        }
                        display_server_mark_dirty(dst_x, dst_y, img_w, img_h);
                    }
                }
                break;

            case 98: /* X_QueryExtension */ {
                u8 reply[32] = {0};
                reply[0] = 1;
                *(u16 *)&reply[2] = (u16)g_display_server.sequence_number;
                /* Reply that extension is present with standard major opcode */
                reply[8] = 1;   /* present */
                reply[9] = 130; /* major opcode */
                reply[10] = 0;  /* first event */
                reply[11] = 0;  /* first error */
                ring_push(reply, 32);
                break;
            }

            default:
                break;
        }

        cursor += req_len_bytes;
    }

    return (int)ring_pop(out_resp, max_resp_len);
}

void display_server_tick(void) {
    if (!g_display_server.active || !g_display_server.dirty) return;

    extern void fb_mark_dirty_rect(int x, int y, int w, int h);
    fb_mark_dirty_rect((int)g_display_server.dirty_x, (int)g_display_server.dirty_y,
                       (int)g_display_server.dirty_w, (int)g_display_server.dirty_h);
    g_display_server.dirty = 0;
}
