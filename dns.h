#ifndef DNS_H
#define DNS_H
#include "io.h"
#include "bnet_error.h"
typedef struct {
    char host[64];
    char ip[16];
    int valid;
} dns_cache_item_t;

typedef struct {
    u32 queries_sent;
    u32 responses_received;
    u32 cache_hits;
    u32 cache_misses;
    u32 timeouts;
    u32 failures;
} dns_stats_t;

void dns_init(void);
int  dns_resolve(const char *hostname, char *out_ip, int max_len);
int  dns_resolve_ex(const char *hostname, char *out_ip, int max_len, bnet_error_t *out_err);
void dns_get_stats(dns_stats_t *out_stats);
int  dns_get_cache_entries(dns_cache_item_t *out_entries, int max_entries);
#endif
