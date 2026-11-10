#include "browser_storage_service.h"
#include "include/firstos/uapi/browser_ipc.h"
#include "serial.h"
#include <stddef.h>
#include <string.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

static int s_strncasecmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c1 = s1[i], c2 = s2[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return (int)((unsigned char)c1 - (unsigned char)c2);
        if (!c1) break;
    }
    return 0;
}

#define BROWSER_CACHE_MAX_ENTRIES 16
#define BROWSER_CACHE_MAX_SIZE    16384

typedef struct {
    int used;
    char url[256];
    char data[BROWSER_CACHE_MAX_SIZE];
    int len;
} browser_cache_entry_t;

static browser_cache_entry_t s_cache[BROWSER_CACHE_MAX_ENTRIES];

void browser_storage_service_init(void) {
    cookies_init();
    download_manager_init();
    browser_storage_init();
    for (int i = 0; i < BROWSER_CACHE_MAX_ENTRIES; i++) {
        s_cache[i].used = 0;
        s_cache[i].url[0] = '\0';
        s_cache[i].len = 0;
    }
    serial_write("[storage] Browser Storage Service initialized (Cookies, Downloads, DB, Cache)\n");
}

void browser_storage_service_apply_cookie_headers(const char *headers, const char *req_host, const char *req_path, int is_https) {
    if (!headers || !req_host) return;

    const char *p = headers;
    while (*p) {
        if (s_strncasecmp(p, "Set-Cookie:", 11) == 0) {
            p += 11;
            while (*p == ' ' || *p == '\t') p++;
            const char *val_start = p;
            while (*p && *p != '\r' && *p != '\n') p++;
            int val_len = (int)(p - val_start);
            if (val_len > 0 && val_len < 256) {
                char cookie_val[256];
                memcpy(cookie_val, val_start, val_len);
                cookie_val[val_len] = '\0';
                cookies_parse_set_cookie(cookie_val, req_host, req_path ? req_path : "/", is_https);
                serial_write("[storage] cookie applied\n");
            }
        }
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

int browser_storage_service_get_cookie_header(const char *host, const char *path, int is_https, char *out_buf, int max_len) {
    serial_write("[storage] cookie query origin=");
    serial_write(host ? host : "unknown");
    serial_write("\n");
    firstos_ipc_log_msg(FIRSTOS_BROWSER_MSG_STORAGE_COOKIE_QUERY);
    return cookies_build_header(host, path, is_https, out_buf, max_len);
}

int browser_storage_service_set_cookie(const char *domain, const char *path, const char *name, const char *value, int secure, int http_only) {
    firstos_ipc_log_msg(FIRSTOS_BROWSER_MSG_STORAGE_COOKIE_UPDATE);
    int rc = cookie_set(domain, path, name, value, secure, http_only);
    if (rc > 0) {
        serial_write("[storage] cookie applied\n");
    }
    return rc;
}

download_task_t *browser_storage_service_create_download(const char *url, const char *suggested_filename, u32 tab_id, u32 tab_gen) {
    if (!url) return NULL;
    download_task_t *task = download_create(url, suggested_filename, tab_id, tab_gen, 1, 1, 1, 1);
    if (task) {
        serial_write("[storage] download created id=");
        serial_write_hex(task->id);
        serial_write(" url=");
        serial_write(url);
        serial_write("\n");
    }
    return task;
}

void browser_storage_service_cancel_download(u32 download_id) {
    download_cancel(download_id);
}

int browser_storage_service_history_add(const char *url, const char *title) {
    return browser_storage_history_add(url, title, 0);
}

int browser_storage_service_save_all(void) {
    return browser_storage_save_all();
}

int browser_storage_service_load_all(void) {
    return browser_storage_load_all();
}

int browser_storage_service_cache_lookup(const char *url, char *out_buf, int max_len, int *out_len) {
    if (!url || !out_buf || max_len <= 0) return 0;
    for (int i = 0; i < BROWSER_CACHE_MAX_ENTRIES; i++) {
        if (s_cache[i].used && strcmp(s_cache[i].url, url) == 0) {
            int copy_len = s_cache[i].len < max_len ? s_cache[i].len : max_len;
            memcpy(out_buf, s_cache[i].data, copy_len);
            if (out_len) *out_len = copy_len;
            return 1;
        }
    }
    return 0;
}

int browser_storage_service_cache_store(const char *url, const char *buf, int len) {
    if (!url || !buf || len <= 0 || len > BROWSER_CACHE_MAX_SIZE) return 0;
    int slot = -1;
    for (int i = 0; i < BROWSER_CACHE_MAX_ENTRIES; i++) {
        if (s_cache[i].used && strcmp(s_cache[i].url, url) == 0) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        for (int i = 0; i < BROWSER_CACHE_MAX_ENTRIES; i++) {
            if (!s_cache[i].used) {
                slot = i;
                break;
            }
        }
    }
    if (slot == -1) slot = 0; /* LRU / simple overwrite */

    s_cache[slot].used = 1;
    strncpy(s_cache[slot].url, url, sizeof(s_cache[slot].url) - 1);
    s_cache[slot].url[sizeof(s_cache[slot].url) - 1] = '\0';
    memcpy(s_cache[slot].data, buf, len);
    s_cache[slot].len = len;
    return 1;
}
