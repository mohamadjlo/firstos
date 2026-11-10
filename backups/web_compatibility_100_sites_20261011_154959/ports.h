#ifndef PORTS_H
#define PORTS_H

#include "io.h"

#define PORT_TYPE_TCP 1
#define PORT_TYPE_UDP 2

#define PORT_STATE_NONE        0
#define PORT_STATE_LISTEN      1
#define PORT_STATE_BOUND       2
#define PORT_STATE_ESTABLISHED 3
#define PORT_STATE_SYN_SENT    4
#define PORT_STATE_CLOSE_WAIT  5
#define PORT_STATE_TIME_WAIT   6

typedef struct {
    u8   proto;             /* PORT_TYPE_TCP or PORT_TYPE_UDP */
    u16  local_port;
    u16  remote_port;
    u32  local_ip;
    u32  remote_ip;
    int  state;             /* PORT_STATE_* */
    char service[16];      /* "http", "https", "dns", etc. */
    u32  rx_bytes;
    u32  tx_bytes;
    u32  rx_packets;
    u32  tx_packets;
} port_entry_t;

void        ports_init(void);
const char *ports_get_service_name(u16 port, u8 proto);
u16         ports_alloc_ephemeral(u8 proto);
int         ports_is_port_in_use(u8 proto, u16 port);
int         ports_get_active_list(port_entry_t *out_list, int max_entries);
int         ports_get_active_count(void);

#endif /* PORTS_H */
