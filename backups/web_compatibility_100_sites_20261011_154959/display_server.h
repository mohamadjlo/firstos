#ifndef DISPLAY_SERVER_H
#define DISPLAY_SERVER_H

#include "framebuffer.h"

#define X11_DISPLAY_SOCKET_PATH "/tmp/.X11-unix/X0"
#define X11_SURFACE_MAX_W 1024
#define X11_SURFACE_MAX_H 768

typedef struct {
    int active;
    int client_connected;
    u32 client_fd;
    u32 surface_w;
    u32 surface_h;
    u32 *surface_pixels; /* 32bpp BGRA/RGBA pixels */
    int dirty;
    u32 dirty_x, dirty_y, dirty_w, dirty_h;
    
    /* Window metadata */
    u32 window_id;
    char window_title[128];
    int mapped;
    u32 event_mask;
    u32 sequence_number;
} display_server_state_t;

extern display_server_state_t g_display_server;

void display_server_init(void);
void display_server_tick(void);
void display_server_mark_dirty(u32 x, u32 y, u32 w, u32 h);
void display_server_send_key(unsigned char key, char ascii, int pressed);
void display_server_send_button(int button, int pressed, int x, int y);
void display_server_send_motion(int x, int y);
void display_server_send_expose(void);
int  display_server_handle_client_data(const u8 *data, u32 len, u8 *out_resp, u32 max_resp_len);
void display_server_set_client_connected(int connected, u32 fd);

#endif
