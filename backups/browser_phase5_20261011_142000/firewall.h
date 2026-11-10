#ifndef FIREWALL_H
#define FIREWALL_H

#include "io.h"

#define MAX_FIREWALL_RULES   32
#define MAX_CONNTRACK_SLOTS  64
#define MAX_FIREWALL_LOGS    32

typedef enum {
    FW_DIR_IN = 0,
    FW_DIR_OUT = 1,
    FW_DIR_BOTH = 2
} fw_direction_t;

typedef enum {
    FW_PROTO_ANY = 0,
    FW_PROTO_TCP = 1,
    FW_PROTO_UDP = 2,
    FW_PROTO_ICMP = 3
} fw_proto_t;

typedef enum {
    FW_ACTION_ACCEPT = 0,
    FW_ACTION_DROP   = 1,
    FW_ACTION_REJECT = 2
} fw_action_t;

typedef enum {
    FW_PROFILE_OPEN     = 0,
    FW_PROFILE_BALANCED = 1,
    FW_PROFILE_STRICT   = 2,
    FW_PROFILE_CUSTOM   = 3
} fw_profile_t;

typedef struct {
    int            enabled;
    char           name[32];
    fw_direction_t dir;
    fw_proto_t     proto;
    u32            src_ip;
    u32            src_mask;
    u32            dst_ip;
    u32            dst_mask;
    u16            port_start;
    u16            port_end;
    fw_action_t    action;
    u32            match_count;
    u32            byte_count;
} fw_rule_t;

typedef struct {
    int   used;
    u8    proto;
    u32   src_ip;
    u32   dst_ip;
    u16   src_port;
    u16   dst_port;
    u32   last_seen;
    int   state;
} fw_conntrack_t;

typedef struct {
    u32            timestamp;
    u8             proto;
    u32            src_ip;
    u32            dst_ip;
    u16            src_port;
    u16            dst_port;
    fw_direction_t dir;
    fw_action_t    action;
    char           rule_name[24];
} fw_log_t;

typedef struct {
    int          enabled;
    fw_profile_t profile;
    int          stealth_mode;       /* Suppress ICMP echo responses */
    fw_action_t  default_in;
    fw_action_t  default_out;
    u32          packets_accepted;
    u32          packets_dropped;
    u32          packets_rejected;
    u32          bytes_processed;
    int          rule_count;
    fw_rule_t    rules[MAX_FIREWALL_RULES];
} fw_state_t;

void              firewall_init(void);
int               firewall_is_enabled(void);
void              firewall_set_enabled(int en);
void              firewall_set_profile(fw_profile_t prof);
fw_profile_t      firewall_get_profile(void);
void              firewall_set_stealth(int stealth);
int               firewall_is_stealth(void);

fw_action_t       firewall_check_input(u8 proto, u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port, u16 len);
fw_action_t       firewall_check_output(u8 proto, u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port, u16 len);

int               firewall_add_rule(const fw_rule_t *rule);
int               firewall_delete_rule(int index);
int               firewall_reset_defaults(void);
const fw_state_t *firewall_get_state(void);
int               firewall_get_logs(fw_log_t *out_logs, int max_entries);
void              firewall_save_to_disk(void);
void              firewall_load_from_disk(void);

#endif /* FIREWALL_H */
