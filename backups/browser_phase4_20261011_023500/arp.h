#ifndef ARP_H
#define ARP_H
#include "io.h"
void arp_init(void);
int  arp_lookup(u32 ip, u8 out_mac[6]);
void arp_handle(const void *frame, u16 len);
void arp_announce(void);
#endif
