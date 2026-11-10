#ifndef ICMP_H
#define ICMP_H
#include "io.h"
void icmp_init(void);
void icmp_handle(u32 src_ip, const void *data, u16 len);
int  icmp_ping(u32 dst_ip);
#endif
