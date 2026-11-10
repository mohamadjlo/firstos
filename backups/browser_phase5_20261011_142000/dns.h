#ifndef DNS_H
#define DNS_H
#include "io.h"
#include "bnet_error.h"
void dns_init(void);
int  dns_resolve(const char *hostname, char *out_ip, int max_len);
int  dns_resolve_ex(const char *hostname, char *out_ip, int max_len, bnet_error_t *out_err);
#endif
