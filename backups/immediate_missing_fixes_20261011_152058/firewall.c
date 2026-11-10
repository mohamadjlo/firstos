#include "firewall.h"
#include "ip.h"
#include "task.h"
#include "fs.h"
#include "serial.h"
#include <string.h>
#include <stdio.h>

static fw_state_t      g_fw;
static fw_conntrack_t  g_conntrack[MAX_CONNTRACK_SLOTS];
static fw_log_t        g_logs[MAX_FIREWALL_LOGS];
static int             g_log_idx = 0;

static void firewall_log_event(u8 proto, u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port,
                               fw_direction_t dir, fw_action_t act, const char *rule_name) {
    fw_log_t *l = &g_logs[g_log_idx % MAX_FIREWALL_LOGS];
    l->timestamp = get_system_ticks();
    l->proto = proto;
    l->src_ip = src_ip;
    l->dst_ip = dst_ip;
    l->src_port = src_port;
    l->dst_port = dst_port;
    l->dir = dir;
    l->action = act;
    strncpy(l->rule_name, rule_name ? rule_name : "-", sizeof(l->rule_name) - 1);
    g_log_idx++;
}

static void conntrack_add(u8 proto, u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port) {
    u32 now = get_system_ticks();
    int oldest_idx = 0;
    u32 oldest_tick = 0xFFFFFFFF;

    /* Check if already present */
    for (int i = 0; i < MAX_CONNTRACK_SLOTS; i++) {
        if (g_conntrack[i].used) {
            if (g_conntrack[i].proto == proto &&
                g_conntrack[i].src_ip == src_ip && g_conntrack[i].dst_ip == dst_ip &&
                g_conntrack[i].src_port == src_port && g_conntrack[i].dst_port == dst_port) {
                g_conntrack[i].last_seen = now;
                return;
            }
            if (g_conntrack[i].last_seen < oldest_tick) {
                oldest_tick = g_conntrack[i].last_seen;
                oldest_idx = i;
            }
        } else {
            oldest_idx = i;
            break;
        }
    }

    /* Assign slot */
    fw_conntrack_t *c = &g_conntrack[oldest_idx];
    c->used = 1;
    c->proto = proto;
    c->src_ip = src_ip;
    c->dst_ip = dst_ip;
    c->src_port = src_port;
    c->dst_port = dst_port;
    c->last_seen = now;
    c->state = 1;
}

static int conntrack_is_established(u8 proto, u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port) {
    u32 now = get_system_ticks();
    for (int i = 0; i < MAX_CONNTRACK_SLOTS; i++) {
        if (g_conntrack[i].used && g_conntrack[i].proto == proto) {
            /* Check reverse match: outbound was src->dst, return traffic is dst->src */
            if (g_conntrack[i].src_ip == dst_ip && g_conntrack[i].dst_ip == src_ip &&
                g_conntrack[i].src_port == dst_port && g_conntrack[i].dst_port == src_port) {
                g_conntrack[i].last_seen = now;
                return 1;
            }
        }
    }
    return 0;
}

void firewall_init(void) {
    memset(&g_fw, 0, sizeof(g_fw));
    memset(g_conntrack, 0, sizeof(g_conntrack));
    memset(g_logs, 0, sizeof(g_logs));
    g_log_idx = 0;

    firewall_reset_defaults();
    firewall_load_from_disk();
    serial_write("[firewall] stateful firewall initialized\n");
}

int firewall_reset_defaults(void) {
    g_fw.enabled = 1;
    g_fw.profile = FW_PROFILE_BALANCED;
    g_fw.stealth_mode = 0;
    g_fw.default_in = FW_ACTION_DROP;
    g_fw.default_out = FW_ACTION_ACCEPT;
    g_fw.rule_count = 0;

    /* Add default balanced rules */
    fw_rule_t r;

    /* 1. DHCP Client Inbound */
    memset(&r, 0, sizeof(r));
    r.enabled = 1;
    strncpy(r.name, "Allow DHCP Client", sizeof(r.name) - 1);
    r.dir = FW_DIR_IN;
    r.proto = FW_PROTO_UDP;
    r.port_start = 68; r.port_end = 68;
    r.action = FW_ACTION_ACCEPT;
    firewall_add_rule(&r);

    /* 2. DNS Inbound Response */
    memset(&r, 0, sizeof(r));
    r.enabled = 1;
    strncpy(r.name, "Allow DNS Client", sizeof(r.name) - 1);
    r.dir = FW_DIR_IN;
    r.proto = FW_PROTO_UDP;
    r.port_start = 53; r.port_end = 53;
    r.action = FW_ACTION_ACCEPT;
    firewall_add_rule(&r);

    /* 3. HTTP Inbound Service */
    memset(&r, 0, sizeof(r));
    r.enabled = 1;
    strncpy(r.name, "Allow HTTP Service", sizeof(r.name) - 1);
    r.dir = FW_DIR_IN;
    r.proto = FW_PROTO_TCP;
    r.port_start = 80; r.port_end = 80;
    r.action = FW_ACTION_ACCEPT;
    firewall_add_rule(&r);

    /* 4. HTTPS Inbound Service */
    memset(&r, 0, sizeof(r));
    r.enabled = 1;
    strncpy(r.name, "Allow HTTPS Service", sizeof(r.name) - 1);
    r.dir = FW_DIR_IN;
    r.proto = FW_PROTO_TCP;
    r.port_start = 443; r.port_end = 443;
    r.action = FW_ACTION_ACCEPT;
    firewall_add_rule(&r);

    return 1;
}

int firewall_is_enabled(void) {
    return g_fw.enabled;
}

void firewall_set_enabled(int en) {
    g_fw.enabled = en ? 1 : 0;
    firewall_save_to_disk();
}

void firewall_set_profile(fw_profile_t prof) {
    g_fw.profile = prof;
    if (prof == FW_PROFILE_OPEN) {
        g_fw.enabled = 0;
        g_fw.default_in = FW_ACTION_ACCEPT;
        g_fw.default_out = FW_ACTION_ACCEPT;
        g_fw.stealth_mode = 0;
    } else if (prof == FW_PROFILE_BALANCED) {
        g_fw.enabled = 1;
        g_fw.default_in = FW_ACTION_DROP;
        g_fw.default_out = FW_ACTION_ACCEPT;
        g_fw.stealth_mode = 0;
    } else if (prof == FW_PROFILE_STRICT) {
        g_fw.enabled = 1;
        g_fw.default_in = FW_ACTION_DROP;
        g_fw.default_out = FW_ACTION_ACCEPT;
        g_fw.stealth_mode = 1;
    }
    firewall_save_to_disk();
}

fw_profile_t firewall_get_profile(void) {
    return g_fw.profile;
}

void firewall_set_stealth(int stealth) {
    g_fw.stealth_mode = stealth ? 1 : 0;
    firewall_save_to_disk();
}

int firewall_is_stealth(void) {
    return g_fw.stealth_mode;
}

const fw_state_t *firewall_get_state(void) {
    return &g_fw;
}

int firewall_add_rule(const fw_rule_t *rule) {
    if (!rule || g_fw.rule_count >= MAX_FIREWALL_RULES) return 0;
    g_fw.rules[g_fw.rule_count] = *rule;
    g_fw.rule_count++;
    firewall_save_to_disk();
    return 1;
}

int firewall_delete_rule(int index) {
    if (index < 0 || index >= g_fw.rule_count) return 0;
    for (int i = index; i < g_fw.rule_count - 1; i++) {
        g_fw.rules[i] = g_fw.rules[i + 1];
    }
    g_fw.rule_count--;
    firewall_save_to_disk();
    return 1;
}

int firewall_get_logs(fw_log_t *out_logs, int max_entries) {
    if (!out_logs || max_entries <= 0) return 0;
    int count = (g_log_idx < MAX_FIREWALL_LOGS) ? g_log_idx : MAX_FIREWALL_LOGS;
    if (count > max_entries) count = max_entries;
    for (int i = 0; i < count; i++) {
        int idx = (g_log_idx - 1 - i + MAX_FIREWALL_LOGS) % MAX_FIREWALL_LOGS;
        out_logs[i] = g_logs[idx];
    }
    return count;
}

fw_action_t firewall_check_output(u8 proto, u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port, u16 len) {
    if (!g_fw.enabled) return FW_ACTION_ACCEPT;

    g_fw.bytes_processed += len;

    /* Track stateful connection */
    if (proto == IP_PROTO_TCP || proto == IP_PROTO_UDP) {
        conntrack_add(proto, src_ip, dst_ip, src_port, dst_port);
    }

    /* Check explicit outbound rules */
    for (int i = 0; i < g_fw.rule_count; i++) {
        fw_rule_t *r = &g_fw.rules[i];
        if (!r->enabled) continue;
        if (r->dir != FW_DIR_OUT && r->dir != FW_DIR_BOTH) continue;

        if (r->proto != FW_PROTO_ANY) {
            if (r->proto == FW_PROTO_TCP && proto != IP_PROTO_TCP) continue;
            if (r->proto == FW_PROTO_UDP && proto != IP_PROTO_UDP) continue;
            if (r->proto == FW_PROTO_ICMP && proto != IP_PROTO_ICMP) continue;
        }

        if (r->port_start != 0) {
            if (dst_port < r->port_start || dst_port > r->port_end) continue;
        }

        r->match_count++;
        r->byte_count += len;

        if (r->action == FW_ACTION_DROP || r->action == FW_ACTION_REJECT) {
            g_fw.packets_dropped++;
            firewall_log_event(proto, src_ip, dst_ip, src_port, dst_port, FW_DIR_OUT, r->action, r->name);
            return r->action;
        }
        g_fw.packets_accepted++;
        return FW_ACTION_ACCEPT;
    }

    if (g_fw.default_out == FW_ACTION_DROP) {
        g_fw.packets_dropped++;
        firewall_log_event(proto, src_ip, dst_ip, src_port, dst_port, FW_DIR_OUT, FW_ACTION_DROP, "DEFAULT_OUT_DROP");
        return FW_ACTION_DROP;
    }

    g_fw.packets_accepted++;
    return FW_ACTION_ACCEPT;
}

fw_action_t firewall_check_input(u8 proto, u32 src_ip, u32 dst_ip, u16 src_port, u16 dst_port, u16 len) {
    if (!g_fw.enabled) return FW_ACTION_ACCEPT;

    g_fw.bytes_processed += len;

    /* 1. Stateful Inspection: Established Return Traffic Bypass */
    if (proto == IP_PROTO_TCP || proto == IP_PROTO_UDP) {
        if (conntrack_is_established(proto, src_ip, dst_ip, src_port, dst_port)) {
            g_fw.packets_accepted++;
            return FW_ACTION_ACCEPT;
        }
    }

    /* 2. Check ICMP Stealth Mode */
    if (proto == IP_PROTO_ICMP && g_fw.stealth_mode) {
        g_fw.packets_dropped++;
        firewall_log_event(proto, src_ip, dst_ip, 0, 0, FW_DIR_IN, FW_ACTION_DROP, "STEALTH_ICMP");
        return FW_ACTION_DROP;
    }

    /* 3. Evaluate Configured Inbound Rules */
    for (int i = 0; i < g_fw.rule_count; i++) {
        fw_rule_t *r = &g_fw.rules[i];
        if (!r->enabled) continue;
        if (r->dir != FW_DIR_IN && r->dir != FW_DIR_BOTH) continue;

        if (r->proto != FW_PROTO_ANY) {
            if (r->proto == FW_PROTO_TCP && proto != IP_PROTO_TCP) continue;
            if (r->proto == FW_PROTO_UDP && proto != IP_PROTO_UDP) continue;
            if (r->proto == FW_PROTO_ICMP && proto != IP_PROTO_ICMP) continue;
        }

        if (r->port_start != 0) {
            if (dst_port < r->port_start || dst_port > r->port_end) continue;
        }

        r->match_count++;
        r->byte_count += len;

        if (r->action == FW_ACTION_DROP || r->action == FW_ACTION_REJECT) {
            g_fw.packets_dropped++;
            firewall_log_event(proto, src_ip, dst_ip, src_port, dst_port, FW_DIR_IN, r->action, r->name);
            return r->action;
        }
        g_fw.packets_accepted++;
        return FW_ACTION_ACCEPT;
    }

    /* 4. Default Policy */
    if (g_fw.default_in == FW_ACTION_DROP) {
        g_fw.packets_dropped++;
        firewall_log_event(proto, src_ip, dst_ip, src_port, dst_port, FW_DIR_IN, FW_ACTION_DROP, "DEFAULT_IN_DROP");
        return FW_ACTION_DROP;
    }

    g_fw.packets_accepted++;
    return FW_ACTION_ACCEPT;
}

void firewall_save_to_disk(void) {
    if (!fs_ready()) return;
    static char buf[2048];
    int pos = snprintf(buf, sizeof(buf), "enabled=%d\nprofile=%d\nstealth=%d\n",
                       g_fw.enabled, (int)g_fw.profile, g_fw.stealth_mode);
    for (int i = 0; i < g_fw.rule_count && pos < (int)sizeof(buf) - 128; i++) {
        fw_rule_t *r = &g_fw.rules[i];
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "rule=%s|%d|%d|%u|%u|%u|%u|%u|%u|%d\n",
                        r->name, (int)r->dir, (int)r->proto,
                        r->src_ip, r->src_mask, r->dst_ip, r->dst_mask,
                        r->port_start, r->port_end, (int)r->action);
    }
    fs_write_at("/firewall.conf", buf, pos);
}

void firewall_load_from_disk(void) {
    if (!fs_ready()) return;
    static char buf[2048];
    int sz = fs_read_at("/firewall.conf", buf, sizeof(buf) - 1);
    if (sz <= 0) return;
    buf[sz] = 0;

    char *line = buf;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = 0;

        if (strncmp(line, "enabled=", 8) == 0) {
            g_fw.enabled = (line[8] == '1');
        } else if (strncmp(line, "profile=", 8) == 0) {
            g_fw.profile = (fw_profile_t)(line[8] - '0');
        } else if (strncmp(line, "stealth=", 8) == 0) {
            g_fw.stealth_mode = (line[8] == '1');
        }

        if (eol) line = eol + 1;
        else break;
    }
}
