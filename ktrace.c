#include "ktrace.h"
#include "serial.h"

extern u32 get_system_ticks(void);

static ktrace_record_t s_ring[KTRACE_RING_CAPACITY];
static int s_head = 0;
static int s_total_records = 0;
static ktrace_level_t s_min_level = KTRACE_LEVEL_DEBUG;

static const char *s_subsys_names[] = {
    "kernel", "task", "proc", "syscall", "mem", "vfs", "ipc", "fault"
};

static const char *s_level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "FAULT"
};

void ktrace_init(void) {
    s_head = 0;
    s_total_records = 0;
    s_min_level = KTRACE_LEVEL_DEBUG;
    serial_write("[ktrace] Kernel tracing framework initialized (ring=256 records)\n");
}

void ktrace_set_min_level(ktrace_level_t level) {
    s_min_level = level;
}

static void ktrace_strcpy(char *dest, const char *src, int max) {
    int i = 0;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    while (src[i] && i < max - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void ktrace_record(ktrace_subsys_t subsys, ktrace_level_t level, u16 pid, const char *msg) {
    ktrace_record_t *r = &s_ring[s_head];
    r->tick = get_system_ticks();
    r->pid = pid;
    r->subsys = (u8)subsys;
    r->level = (u8)level;
    ktrace_strcpy(r->msg, msg, KTRACE_MSG_MAX);

    s_head = (s_head + 1) % KTRACE_RING_CAPACITY;
    s_total_records++;
}

void ktrace_write(ktrace_subsys_t subsys, ktrace_level_t level, u16 pid, const char *msg) {
    ktrace_record(subsys, level, pid, msg);

    if (level < s_min_level) return;

    serial_write("[trace][");
    if (subsys <= KTRACE_SUBSYS_FAULT) {
        serial_write(s_subsys_names[subsys]);
    } else {
        serial_write("unknown");
    }
    serial_write("][");
    if (level <= KTRACE_LEVEL_FAULT) {
        serial_write(s_level_names[level]);
    } else {
        serial_write("?");
    }
    serial_write("] pid=");
    serial_write_hex((u32)pid);
    serial_write(" : ");
    if (msg) serial_write(msg);
    serial_write("\n");
}

void ktrace_write_hex(ktrace_subsys_t subsys, ktrace_level_t level, u16 pid, const char *prefix, u64 val) {
    char buf[KTRACE_MSG_MAX];
    int idx = 0;
    if (prefix) {
        while (prefix[idx] && idx < KTRACE_MSG_MAX - 18) {
            buf[idx] = prefix[idx];
            idx++;
        }
    }
    buf[idx++] = '0';
    buf[idx++] = 'x';
    for (int shift = 60; shift >= 0; shift -= 4) {
        if (idx < KTRACE_MSG_MAX - 1) {
            u8 nibble = (u8)((val >> shift) & 0x0F);
            buf[idx++] = (char)(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
        }
    }
    buf[idx] = '\0';

    ktrace_write(subsys, level, pid, buf);
}

void ktrace_dump_recent(int count) {
    if (count <= 0) count = 16;
    if (count > KTRACE_RING_CAPACITY) count = KTRACE_RING_CAPACITY;
    if (count > s_total_records) count = s_total_records;

    serial_write("\n========== [KTRACE RECENT RING BUFFER DUMP] ==========\n");
    int start = (s_head - count + KTRACE_RING_CAPACITY) % KTRACE_RING_CAPACITY;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % KTRACE_RING_CAPACITY;
        ktrace_record_t *r = &s_ring[idx];
        serial_write("  [t=");
        serial_write_hex(r->tick);
        serial_write(" pid=");
        serial_write_hex((u32)r->pid);
        serial_write(" ");
        if (r->subsys <= KTRACE_SUBSYS_FAULT) {
            serial_write(s_subsys_names[r->subsys]);
        }
        serial_write(":");
        if (r->level <= KTRACE_LEVEL_FAULT) {
            serial_write(s_level_names[r->level]);
        }
        serial_write("] ");
        serial_write(r->msg);
        serial_write("\n");
    }
    serial_write("======================================================\n\n");
}
