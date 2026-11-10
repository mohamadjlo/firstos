#include "browser_storage.h"
#include "fs.h"
#include "serial.h"
#include "clock.h"
#include "cookies.h"
#include "browser_host_ipc.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wincompatible-library-redeclaration"
#endif

int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, unsigned int n);
char *strncpy(char *dest, const char *src, unsigned int n);
void *memcpy(void *dest, const void *src, unsigned int n);
void *memset(void *s, int c, unsigned int n);

static firstos_history_record_t s_history[FIRSTOS_MAX_HISTORY_RECORDS];
static int s_history_count = 0;
static u32 s_storage_generation = 1;

/* IEEE 802.3 CRC-32 Lookup Table & Generator */
static u32 s_crc32_table[256];
static int s_crc32_initialized = 0;

static void crc32_init(void) {
    if (s_crc32_initialized) return;
    for (u32 i = 0; i < 256; i++) {
        u32 c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) c = 0xEDB88320u ^ (c >> 1);
            else c = c >> 1;
        }
        s_crc32_table[i] = c;
    }
    s_crc32_initialized = 1;
}

u32 browser_storage_crc32(const void *data, u32 len) {
    if (!s_crc32_initialized) crc32_init();
    if (!data || len == 0) return 0;
    const u8 *p = (const u8 *)data;
    u32 crc = 0xFFFFFFFFu;
    for (u32 i = 0; i < len; i++) {
        crc = s_crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void browser_storage_init(void) {
    crc32_init();
    memset(s_history, 0, sizeof(s_history));
    s_history_count = 0;
    s_storage_generation = 1;
}

static void sanitize_text(const char *in, char *out, int max_len) {
    if (!out || max_len <= 1) return;
    out[0] = 0;
    if (!in) return;
    int idx = 0;
    while (*in && idx < max_len - 1) {
        unsigned char c = (unsigned char)*in++;
        if (c >= 32 && c < 127) {
            out[idx++] = (char)c;
        } else {
            out[idx++] = ' ';
        }
    }
    out[idx] = 0;
}

int browser_storage_history_add(const char *url, const char *title, u8 visit_type) {
    if (!url || !url[0]) return -1;
    if (strncmp(url, "about:", 6) == 0 && strcmp(url, "about:home") != 0) return -1;

    /* Check if already at top of history */
    if (s_history_count > 0 && strcmp(s_history[s_history_count - 1].url, url) == 0) {
        s_history[s_history_count - 1].visit_count++;
        s_history[s_history_count - 1].timestamp = clock_monotonic_get_ms() / 1000;
        return s_history_count - 1;
    }

    /* FIFO Eviction if full */
    if (s_history_count >= (int)FIRSTOS_MAX_HISTORY_RECORDS) {
        for (int i = 0; i < s_history_count - 1; i++) {
            s_history[i] = s_history[i + 1];
        }
        s_history_count--;
    }

    firstos_history_record_t *rec = &s_history[s_history_count++];
    memset(rec, 0, sizeof(firstos_history_record_t));
    strncpy(rec->url, url, sizeof(rec->url) - 1);
    if (title && title[0]) {
        sanitize_text(title, rec->title, sizeof(rec->title));
    } else {
        strncpy(rec->title, url, sizeof(rec->title) - 1);
    }
    rec->timestamp = clock_monotonic_get_ms() / 1000;
    rec->visit_count = 1;
    rec->visit_type = visit_type;

    return s_history_count - 1;
}

int browser_storage_history_get_count(void) {
    return s_history_count;
}

const firstos_history_record_t *browser_storage_history_get(int index) {
    if (index < 0 || index >= s_history_count) return NULL;
    return &s_history[index];
}

void browser_storage_history_clear(void) {
    memset(s_history, 0, sizeof(s_history));
    s_history_count = 0;
}

/* Serialization / Deserialization Core */

static u32 append_record(char *buf, u32 offset, u32 cap, u16 type, const void *payload, u32 payload_len) {
    if (!buf || !payload || payload_len == 0) return offset;
    u32 total_needed = sizeof(firstos_storage_header_t) + payload_len;
    if (offset + total_needed > cap) return offset;

    firstos_storage_header_t hdr;
    hdr.magic = FIRSTOS_STORAGE_MAGIC;
    hdr.version = FIRSTOS_STORAGE_VERSION;
    hdr.type = type;
    hdr.length = payload_len;
    hdr.generation = ++s_storage_generation;
    hdr.checksum = browser_storage_crc32(payload, payload_len);

    memcpy(buf + offset, &hdr, sizeof(hdr));
    memcpy(buf + offset + sizeof(hdr), payload, payload_len);
    return offset + total_needed;
}

int browser_storage_save_all(void) {
    static char s_file_buffer[FIRSTOS_STORAGE_MAX_FILE_SIZE];
    u32 pos = 0;

    /* 1. Serialize History */
    for (int i = 0; i < s_history_count; i++) {
        pos = append_record(s_file_buffer, pos, sizeof(s_file_buffer),
                            STORAGE_REC_HISTORY, &s_history[i], sizeof(firstos_history_record_t));
    }

    /* 2. Serialize Cookies (only non-session cookies) */
    int cookie_count = cookies_get_count();
    for (int i = 0; i < cookie_count; i++) {
        const cookie_entry_t *c = cookies_get_entry(i);
        if (c && c->used) {
            firstos_cookie_record_t cr;
            memset(&cr, 0, sizeof(cr));
            strncpy(cr.domain, c->domain, sizeof(cr.domain) - 1);
            strncpy(cr.path, c->path, sizeof(cr.path) - 1);
            strncpy(cr.name, c->name, sizeof(cr.name) - 1);
            strncpy(cr.value, c->value, sizeof(cr.value) - 1);
            cr.secure = (u8)c->secure;
            cr.http_only = (u8)c->http_only;
            cr.is_session = 0;
            pos = append_record(s_file_buffer, pos, sizeof(s_file_buffer),
                                STORAGE_REC_COOKIE, &cr, sizeof(firstos_cookie_record_t));
        }
    }

    /* 3. Serialize Active Session Tabs */
    firstos_tab_manager_t *tm = browser_host_get_tab_manager();
    if (tm) {
        for (int i = 0; i < FIRSTOS_MAX_TABS; i++) {
            if (tm->tabs[i].state != FIRSTOS_TAB_FREE) {
                firstos_session_tab_record_t tr;
                memset(&tr, 0, sizeof(tr));
                tr.tab_order = (u32)i;
                tr.is_active = (tm->active_tab_id == tm->tabs[i].tab_id) ? 1 : 0;
                strncpy(tr.url, tm->tabs[i].url, sizeof(tr.url) - 1);
                strncpy(tr.title, tm->tabs[i].title, sizeof(tr.title) - 1);
                tr.history_cursor = tm->tabs[i].history.index;
                tr.history_count = tm->tabs[i].history.count;
                pos = append_record(s_file_buffer, pos, sizeof(s_file_buffer),
                                    STORAGE_REC_SESSION_TAB, &tr, sizeof(firstos_session_tab_record_t));
            }
        }
    }

    if (pos == 0) return 0;

    /* Atomic Flush Sequence: write .tmp -> backup .dat -> rename .tmp to .dat */
    fs_delete_at(FIRSTOS_STORAGE_TMP_PATH);
    if (!fs_create_at(FIRSTOS_STORAGE_TMP_PATH, 0)) return -1;
    if (!fs_write_at(FIRSTOS_STORAGE_TMP_PATH, s_file_buffer, pos)) {
        fs_delete_at(FIRSTOS_STORAGE_TMP_PATH);
        return -1;
    }
    fs_sync();

    /* Backup existing file if present */
    fs_delete_at(FIRSTOS_STORAGE_BAK_PATH);
    fs_rename_at(FIRSTOS_STORAGE_PATH, FIRSTOS_STORAGE_BAK_PATH);

    /* Atomic Rename */
    int ok = fs_rename_at(FIRSTOS_STORAGE_TMP_PATH, FIRSTOS_STORAGE_PATH);
    if (!ok) {
        /* Fallback: try to restore backup */
        fs_rename_at(FIRSTOS_STORAGE_BAK_PATH, FIRSTOS_STORAGE_PATH);
        return -1;
    }
    fs_sync();
    serial_write("[storage] atomic flush completed successfully\n");
    return (int)pos;
}

static int parse_storage_buffer(const char *buf, u32 total_bytes) {
    if (!buf || total_bytes < sizeof(firstos_storage_header_t)) return 0;
    u32 offset = 0;
    int records_loaded = 0;

    while (offset + sizeof(firstos_storage_header_t) <= total_bytes) {
        const firstos_storage_header_t *hdr = (const firstos_storage_header_t *)(buf + offset);
        if (hdr->magic != FIRSTOS_STORAGE_MAGIC || hdr->version != FIRSTOS_STORAGE_VERSION) {
            serial_write("[storage] warning: invalid header magic/version, stopping parse\n");
            break;
        }
        if (hdr->length == 0 || hdr->length > FIRSTOS_MAX_RECORD_BYTES) {
            serial_write("[storage] warning: corrupted record length\n");
            break;
        }
        if (offset + sizeof(firstos_storage_header_t) + hdr->length > total_bytes) {
            serial_write("[storage] warning: truncated record payload\n");
            break;
        }

        const void *payload = buf + offset + sizeof(firstos_storage_header_t);
        u32 actual_crc = browser_storage_crc32(payload, hdr->length);
        if (actual_crc != hdr->checksum) {
            serial_write("[storage] warning: checksum mismatch, skipping corrupted record\n");
            offset += sizeof(firstos_storage_header_t) + hdr->length;
            continue;
        }

        /* Dispatch by record type */
        if (hdr->type == STORAGE_REC_HISTORY && hdr->length == sizeof(firstos_history_record_t)) {
            const firstos_history_record_t *hr = (const firstos_history_record_t *)payload;
            if (s_history_count < (int)FIRSTOS_MAX_HISTORY_RECORDS) {
                s_history[s_history_count++] = *hr;
                records_loaded++;
            }
        } else if (hdr->type == STORAGE_REC_COOKIE && hdr->length == sizeof(firstos_cookie_record_t)) {
            const firstos_cookie_record_t *cr = (const firstos_cookie_record_t *)payload;
            cookie_set(cr->domain, cr->path, cr->name, cr->value, (int)cr->secure, (int)cr->http_only);
            records_loaded++;
        }

        offset += sizeof(firstos_storage_header_t) + hdr->length;
    }
    return records_loaded;
}

int browser_storage_load_all(void) {
    static char s_load_buffer[FIRSTOS_STORAGE_MAX_FILE_SIZE];
    int read_bytes = fs_read_at(FIRSTOS_STORAGE_PATH, s_load_buffer, sizeof(s_load_buffer));

    if (read_bytes <= 0) {
        /* Try recovery from backup */
        serial_write("[storage] checking backup file\n");
        read_bytes = fs_read_at(FIRSTOS_STORAGE_BAK_PATH, s_load_buffer, sizeof(s_load_buffer));
    }

    if (read_bytes <= 0) {
        serial_write("[storage] no storage file found, starting pristine\n");
        return 0;
    }

    int count = parse_storage_buffer(s_load_buffer, (u32)read_bytes);
    serial_write("[storage] records loaded successfully count=");
    serial_write_hex((unsigned int)count);
    serial_write("\n");
    return count;
}

int browser_storage_restore_session(void) {
    static char s_load_buffer[FIRSTOS_STORAGE_MAX_FILE_SIZE];
    int read_bytes = fs_read_at(FIRSTOS_STORAGE_PATH, s_load_buffer, sizeof(s_load_buffer));
    if (read_bytes <= 0) {
        read_bytes = fs_read_at(FIRSTOS_STORAGE_BAK_PATH, s_load_buffer, sizeof(s_load_buffer));
    }
    if (read_bytes <= 0) return 0;

    u32 offset = 0;
    int restored_tabs = 0;

    while (offset + sizeof(firstos_storage_header_t) <= (u32)read_bytes) {
        const firstos_storage_header_t *hdr = (const firstos_storage_header_t *)(s_load_buffer + offset);
        if (hdr->magic != FIRSTOS_STORAGE_MAGIC) break;
        if (offset + sizeof(firstos_storage_header_t) + hdr->length > (u32)read_bytes) break;

        const void *payload = s_load_buffer + offset + sizeof(firstos_storage_header_t);
        if (browser_storage_crc32(payload, hdr->length) == hdr->checksum) {
            if (hdr->type == STORAGE_REC_SESSION_TAB && hdr->length == sizeof(firstos_session_tab_record_t)) {
                const firstos_session_tab_record_t *tr = (const firstos_session_tab_record_t *)payload;
                /* Create fresh tab with fresh IDs */
                int tid = browser_host_tab_create(tr->url, tr->is_active);
                if (tid > 0) restored_tabs++;
            }
        }
        offset += sizeof(firstos_storage_header_t) + hdr->length;
    }

    serial_write("[storage] restored session tabs count=");
    serial_write_hex((unsigned int)restored_tabs);
    serial_write("\n");
    return restored_tabs;
}
