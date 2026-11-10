#ifndef ARP_H
#define ARP_H
#include "io.h"
typedef struct {
    u32 ip;
    u8 mac[6];
    int valid;
} arp_entry_t;

void arp_init(void);
int  arp_lookup(u32 ip, u8 out_mac[6]);
void arp_handle(const void *frame, u16 len);
void arp_announce(void);
int  arp_get_table(arp_entry_t *out_entries, int max_entries);
#endif
