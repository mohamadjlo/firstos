#ifndef BB_OSAL_H
#define BB_OSAL_H

#include "bb_types.h"

/* Linear 32-bpp ARGB Framebuffer Info */
typedef struct {
    uint32_t *pixels;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;
    uint32_t  bpp;
} bb_fb_info_t;

/* Input Events */
typedef enum {
    BB_EVENT_NONE = 0,
    BB_EVENT_MOUSE_MOVE,
    BB_EVENT_MOUSE_DOWN,
    BB_EVENT_MOUSE_UP,
    BB_EVENT_MOUSE_SCROLL,
    BB_EVENT_KEY_DOWN,
    BB_EVENT_KEY_UP,
    BB_EVENT_CHAR,
    BB_EVENT_QUIT
} bb_event_type_t;

typedef struct {
    bb_event_type_t type;
    int32_t         mouse_x;
    int32_t         mouse_y;
    uint32_t        mouse_buttons; /* Bit 0: Left, Bit 1: Right, Bit 2: Middle */
    int32_t         scroll_delta;  /* Positive = up, Negative = down */
    uint32_t        keycode;
    char            char_code;
} bb_event_t;

/* Common Keycodes */
#define BB_KEY_ENTER     13
#define BB_KEY_BACKSPACE 8
#define BB_KEY_TAB       9
#define BB_KEY_ESCAPE    27
#define BB_KEY_UP        38
#define BB_KEY_DOWN      40
#define BB_KEY_LEFT      37
#define BB_KEY_RIGHT     39
#define BB_KEY_PAGE_UP   33
#define BB_KEY_PAGE_DOWN 34

/* Opaque Socket Handle */
typedef void* bb_socket_t;

/* OSAL Lifecycle & Graphics */
int           bb_osal_init(uint32_t width, uint32_t height);
void          bb_osal_shutdown(void);
bb_fb_info_t *bb_osal_get_fb(void);
void          bb_osal_present_fb(void);

/* OSAL Input */
int           bb_osal_poll_event(bb_event_t *ev);

/* OSAL Networking */
bb_socket_t   bb_osal_net_connect(const char *host, uint16_t port);
int           bb_osal_net_send(bb_socket_t sock, const void *data, size_t len);
int           bb_osal_net_recv(bb_socket_t sock, void *buf, size_t max_len, uint32_t timeout_ms);
void          bb_osal_net_close(bb_socket_t sock);

/* OSAL System, Time & Debug */
uint32_t      bb_osal_get_ticks(void);
void          bb_osal_sleep_ms(uint32_t ms);
void          bb_osal_log(const char *msg);

#endif /* BB_OSAL_H */
