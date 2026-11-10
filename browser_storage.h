#ifndef BROWSER_STORAGE_H
#define BROWSER_STORAGE_H

#include "io.h"

#define FIRSTOS_STORAGE_MAGIC         0x31535446u /* "F1ST" */
#define FIRSTOS_STORAGE_VERSION       1u
#define FIRSTOS_MAX_HISTORY_RECORDS   256u
#define FIRSTOS_MAX_STORAGE_COOKIES   128u
#define FIRSTOS_MAX_SESSION_TABS      8u
#define FIRSTOS_MAX_RECORD_BYTES      1024u
#define FIRSTOS_STORAGE_MAX_FILE_SIZE (512u * 1024u)

#define FIRSTOS_STORAGE_PATH          "/firstos_browser.dat"
#define FIRSTOS_STORAGE_TMP_PATH      "/firstos_browser.tmp"
#define FIRSTOS_STORAGE_BAK_PATH      "/firstos_browser.bak"

typedef enum {
    STORAGE_REC_NONE = 0,
    STORAGE_REC_HISTORY = 1,
    STORAGE_REC_COOKIE = 2,
    STORAGE_REC_SESSION_TAB = 3,
    STORAGE_REC_SESSION_META = 4,
    STORAGE_REC_WEB_STORAGE = 5
} firstos_storage_rec_type_t;

typedef struct {
    u32 magic;       /* FIRSTOS_STORAGE_MAGIC */
    u16 version;     /* FIRSTOS_STORAGE_VERSION */
    u16 type;        /* firstos_storage_rec_type_t */
    u32 length;      /* Payload length in bytes */
    u32 generation;  /* Monotonic sequence or timestamp */
    u32 checksum;    /* CRC-32 of payload */
} __attribute__((packed)) firstos_storage_header_t;

typedef struct {
    char url[512];
    char title[128];
    u32  timestamp;
    u32  visit_count;
    u8   visit_type; /* 0 = link, 1 = typed, 2 = redirect */
    u8   reserved[3];
} __attribute__((packed)) firstos_history_record_t;

typedef struct {
    char domain[64];
    char path[64];
    char name[64];
    char value[192];
    u32  expiry_timestamp;
    u8   secure;     /* 1 = HTTPS only */
    u8   http_only;   /* 1 = hidden from JS */
    u8   is_session;  /* If 1, not written to disk */
    u8   reserved;
} __attribute__((packed)) firstos_cookie_record_t;

typedef struct {
    char origin[96]; /* Scheme + host + port or "local" */
    char key[48];
    char val[192];
    u32  timestamp;
    u8   reserved[4];
} __attribute__((packed)) firstos_web_storage_record_t;

typedef struct {
    u32 tab_order;
    u32 is_active;
    char url[512];
    char title[128];
    int scroll_y;
    int history_cursor;
    u32 history_count;
} __attribute__((packed)) firstos_session_tab_record_t;

/* API Functions */
void browser_storage_init(void);
u32  browser_storage_crc32(const void *data, u32 len);

/* History Management */
int  browser_storage_history_add(const char *url, const char *title, u8 visit_type);
int  browser_storage_history_get_count(void);
const firstos_history_record_t *browser_storage_history_get(int index);
void browser_storage_history_clear(void);

/* Origin-Scoped Web Storage Management */
#define FIRSTOS_MAX_WEB_STORAGE_RECORDS 128
int         browser_storage_set_item(const char *origin, const char *key, const char *val);
const char *browser_storage_get_item(const char *origin, const char *key);
int         browser_storage_remove_item(const char *origin, const char *key);
void        browser_storage_clear_origin(const char *origin);
int         browser_storage_get_length(const char *origin);
const char *browser_storage_get_key(const char *origin, int index);

/* Persistence Operations */
int  browser_storage_save_all(void);
int  browser_storage_load_all(void);
int  browser_storage_restore_session(void);

#endif /* BROWSER_STORAGE_H */
