#include "download_manager.h"
#include "http_v11.h"
#include "fs.h"
#include "serial.h"
#include "rtc.h"
#include "clock.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wincompatible-library-redeclaration"
#endif

int snprintf(char *str, unsigned int size, const char *format, ...);
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, unsigned int n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, unsigned int n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
void *memcpy(void *dest, const void *src, unsigned int n);
void *memset(void *s, int c, unsigned int n);

static download_task_t g_download_tasks[FIRSTOS_MAX_DOWNLOADS];
static u32 g_next_task_id = 1;

void download_manager_init(void) {
    memset(g_download_tasks, 0, sizeof(g_download_tasks));
    g_next_task_id = 1;
}

void download_manager_reset(void) {
    for (int i = 0; i < FIRSTOS_MAX_DOWNLOADS; i++) {
        if (g_download_tasks[i].state == DOWNLOAD_STATE_ACTIVE ||
            g_download_tasks[i].state == DOWNLOAD_STATE_PENDING) {
            download_cancel(g_download_tasks[i].id);
        }
    }
    memset(g_download_tasks, 0, sizeof(g_download_tasks));
    g_next_task_id = 1;
}

int download_get_count(void) {
    int cnt = 0;
    for (int i = 0; i < FIRSTOS_MAX_DOWNLOADS; i++) {
        if (g_download_tasks[i].state != DOWNLOAD_STATE_FREE) cnt++;
    }
    return cnt;
}

int download_get_active_count(void) {
    int cnt = 0;
    for (int i = 0; i < FIRSTOS_MAX_DOWNLOADS; i++) {
        if (g_download_tasks[i].state == DOWNLOAD_STATE_ACTIVE) cnt++;
    }
    return cnt;
}

download_task_t *download_get_task(int index) {
    int cur = 0;
    for (int i = 0; i < FIRSTOS_MAX_DOWNLOADS; i++) {
        if (g_download_tasks[i].state != DOWNLOAD_STATE_FREE) {
            if (cur == index) return &g_download_tasks[i];
            cur++;
        }
    }
    return NULL;
}

download_task_t *download_find_by_id(u32 id) {
    if (id == 0) return NULL;
    for (int i = 0; i < FIRSTOS_MAX_DOWNLOADS; i++) {
        if (g_download_tasks[i].id == id && g_download_tasks[i].state != DOWNLOAD_STATE_FREE) {
            return &g_download_tasks[i];
        }
    }
    return NULL;
}

int download_sanitize_filename(const char *input, char *out_buf, int max_len) {
    if (!out_buf || max_len <= 1) return 0;
    out_buf[0] = 0;
    if (!input || !input[0]) {
        strncpy(out_buf, "download", max_len - 1);
        out_buf[max_len - 1] = 0;
        return 1;
    }

    /* Skip leading directory separators or slashes */
    const char *p = input;
    const char *last_slash = strrchr(p, '/');
    if (last_slash) p = last_slash + 1;
    const char *last_bslash = strrchr(p, '\\');
    if (last_bslash && last_bslash >= p) p = last_bslash + 1;

    /* Skip leading dots (e.g. .bashrc, ..) */
    while (*p == '.') p++;

    int out_idx = 0;
    while (*p && out_idx < max_len - 1) {
        unsigned char c = (unsigned char)*p++;
        /* Disallow path separators, colons, wildcards, control chars, quotes */
        if (c < 32 || c >= 127 || c == '/' || c == '\\' || c == ':' ||
            c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            out_buf[out_idx++] = '_';
        } else {
            out_buf[out_idx++] = (char)c;
        }
    }
    out_buf[out_idx] = 0;

    /* Trim trailing dots or spaces */
    while (out_idx > 0 && (out_buf[out_idx - 1] == ' ' || out_buf[out_idx - 1] == '.')) {
        out_buf[--out_idx] = 0;
    }

    if (out_idx == 0) {
        strncpy(out_buf, "download", max_len - 1);
        out_buf[max_len - 1] = 0;
    }
    return 1;
}

int download_parse_content_disposition(const char *header_val, char *out_filename, int max_len, int *out_is_attachment) {
    if (out_is_attachment) *out_is_attachment = 0;
    if (out_filename && max_len > 0) out_filename[0] = 0;
    if (!header_val || !header_val[0]) return 0;

    /* Check disposition type */
    if (strstr(header_val, "attachment") || strstr(header_val, "ATTACHMENT")) {
        if (out_is_attachment) *out_is_attachment = 1;
    } else if (strstr(header_val, "inline") || strstr(header_val, "INLINE")) {
        if (out_is_attachment) *out_is_attachment = 0;
    }

    /* 1. Check for filename*=UTF-8''<encoded> */
    const char *fn_star = strstr(header_val, "filename*=");
    if (fn_star) {
        const char *val = fn_star + 10;
        /* Look for UTF-8'' or utf-8'' */
        const char *quote = strstr(val, "''");
        if (quote) {
            val = quote + 2;
        }
        char raw[128];
        int r = 0;
        while (*val && *val != ';' && *val != ' ' && r < (int)sizeof(raw) - 1) {
            raw[r++] = *val++;
        }
        raw[r] = 0;
        return download_sanitize_filename(raw, out_filename, max_len);
    }

    /* 2. Check for filename="<name>" or filename=<name> */
    const char *fn = strstr(header_val, "filename=");
    if (fn) {
        const char *val = fn + 9;
        char raw[128];
        int r = 0;
        if (*val == '"') {
            val++;
            while (*val && *val != '"' && r < (int)sizeof(raw) - 1) {
                raw[r++] = *val++;
            }
        } else {
            while (*val && *val != ';' && *val != ' ' && r < (int)sizeof(raw) - 1) {
                raw[r++] = *val++;
            }
        }
        raw[r] = 0;
        return download_sanitize_filename(raw, out_filename, max_len);
    }

    return 0;
}

int download_resolve_collision(const char *dir, const char *base_name, char *out_path, int max_len) {
    if (!dir || !base_name || !out_path || max_len <= 1) return 0;

    /* Ensure directory exists */
    fs_mkdir(dir);

    char candidate[FIRSTOS_MAX_DOWNLOAD_PATH];
    if (dir[strlen(dir) - 1] == '/') {
        snprintf(candidate, sizeof(candidate), "%s%s", dir, base_name);
    } else {
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, base_name);
    }

    /* If it does not exist, use directly */
    if (fs_read_at(candidate, NULL, 0) < 0) {
        strncpy(out_path, candidate, max_len - 1);
        out_path[max_len - 1] = 0;
        return 1;
    }

    /* Extract stem and extension */
    char stem[96] = "";
    char ext[32] = "";
    const char *dot = strrchr(base_name, '.');
    if (dot && dot != base_name) {
        int stem_len = (int)(dot - base_name);
        if (stem_len >= (int)sizeof(stem)) stem_len = sizeof(stem) - 1;
        memcpy(stem, base_name, stem_len);
        stem[stem_len] = 0;
        strncpy(ext, dot, sizeof(ext) - 1);
    } else {
        strncpy(stem, base_name, sizeof(stem) - 1);
    }

    /* Try (1), (2), ... up to (99) */
    for (int i = 1; i <= 99; i++) {
        if (dir[strlen(dir) - 1] == '/') {
            snprintf(candidate, sizeof(candidate), "%s%s (%d)%s", dir, stem, i, ext);
        } else {
            snprintf(candidate, sizeof(candidate), "%s/%s (%d)%s", dir, stem, i, ext);
        }
        if (fs_read_at(candidate, NULL, 0) < 0) {
            strncpy(out_path, candidate, max_len - 1);
            out_path[max_len - 1] = 0;
            return 1;
        }
    }

    /* Fallback */
    strncpy(out_path, candidate, max_len - 1);
    out_path[max_len - 1] = 0;
    return 1;
}

download_task_t *download_create(const char *url, const char *suggested_filename,
                                 u32 tab_id, u32 tab_gen,
                                 u32 frame_id, u32 frame_gen,
                                 u32 nav_id, u32 doc_gen) {
    if (!url || !url[0]) return NULL;

    /* Only http and https allowed */
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        serial_write("[download] rejected: unsupported scheme\n");
        return NULL;
    }

    /* Find free slot or evict oldest completed/failed */
    int slot = -1;
    for (int i = 0; i < FIRSTOS_MAX_DOWNLOADS; i++) {
        if (g_download_tasks[i].state == DOWNLOAD_STATE_FREE) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        for (int i = 0; i < FIRSTOS_MAX_DOWNLOADS; i++) {
            if (g_download_tasks[i].state == DOWNLOAD_STATE_COMPLETED ||
                g_download_tasks[i].state == DOWNLOAD_STATE_FAILED ||
                g_download_tasks[i].state == DOWNLOAD_STATE_CANCELLED) {
                slot = i;
                break;
            }
        }
    }

    if (slot < 0) {
        serial_write("[download] rejected: max capacity reached\n");
        return NULL;
    }

    download_task_t *task = &g_download_tasks[slot];
    memset(task, 0, sizeof(download_task_t));
    task->id = g_next_task_id++;
    task->state = DOWNLOAD_STATE_PENDING;
    task->status = DOWNLOAD_STATE_PENDING;
    task->error = DOWNLOAD_ERR_NONE;

    task->tab_id = tab_id;
    task->tab_generation = tab_gen;
    task->frame_id = frame_id;
    task->frame_generation = frame_gen;
    task->navigation_id = nav_id;
    task->document_generation = doc_gen;

    strncpy(task->url, url, sizeof(task->url) - 1);
    if (suggested_filename && suggested_filename[0]) {
        strncpy(task->suggested_filename, suggested_filename, sizeof(task->suggested_filename) - 1);
        download_sanitize_filename(suggested_filename, task->filename, sizeof(task->filename));
    } else {
        /* Extract from URL path */
        const char *p = strrchr(url, '/');
        const char *raw = (p && p[1]) ? p + 1 : "download";
        char base[64];
        const char *q = strchr(raw, '?');
        if (q) {
            int len = (int)(q - raw);
            if (len >= (int)sizeof(base)) len = sizeof(base) - 1;
            memcpy(base, raw, len);
            base[len] = 0;
            download_sanitize_filename(base, task->filename, sizeof(task->filename));
        } else {
            download_sanitize_filename(raw, task->filename, sizeof(task->filename));
        }
    }

    /* Prepare paths */
    fs_mkdir("/Downloads");
    snprintf(task->part_path, sizeof(task->part_path), "/Downloads/.part_%u", task->id);
    download_resolve_collision("/Downloads", task->filename, task->target_path, sizeof(task->target_path));

    return task;
}

typedef struct {
    download_task_t *task;
    u32 received;
    int error_flag;
} download_context_t;

static int download_stream_callback(const u8 *chunk, u32 chunk_len, u32 total_expected, void *user_data) {
    download_context_t *ctx = (download_context_t *)user_data;
    if (!ctx || !ctx->task) return 0;
    download_task_t *task = ctx->task;

    if (task->state == DOWNLOAD_STATE_CANCELLED) {
        return 0; /* Abort stream */
    }

    task->state = DOWNLOAD_STATE_ACTIVE;
    if (total_expected > 0 && task->total_bytes == 0) {
        task->total_bytes = total_expected;
    }

    /* Check 16 MiB size limit */
    if (ctx->received + chunk_len > FIRSTOS_MAX_DOWNLOAD_BYTES) {
        task->state = DOWNLOAD_STATE_FAILED;
        task->error = DOWNLOAD_ERR_SIZE_LIMIT;
        serial_write("[download] error: size limit exceeded (16 MiB)\n");
        ctx->error_flag = 1;
        return 0;
    }

    /* Append to .part file */
    int ok = fs_write_at(task->part_path, (const char *)chunk, chunk_len);
    if (!ok && chunk_len > 0) {
        task->state = DOWNLOAD_STATE_FAILED;
        task->error = DOWNLOAD_ERR_DISK_FULL;
        serial_write("[download] error: disk write failed\n");
        ctx->error_flag = 1;
        return 0;
    }

    ctx->received += chunk_len;
    task->bytes_received = ctx->received;

    /* Speed calculation */
    u32 now_sec = clock_monotonic_get_ms() / 1000;
    u32 elapsed = (now_sec >= task->start_tick) ? (now_sec - task->start_tick) : 1;
    if (elapsed > 0) {
        task->speed_kbps = (task->bytes_received / 1024) / elapsed;
    }

    if (task->on_progress) {
        task->on_progress(task);
    }
    return 1;
}

int download_start_stream(download_task_t *task) {
    if (!task || task->state != DOWNLOAD_STATE_PENDING) return 0;

    /* Enforce concurrency limit */
    if (download_get_active_count() >= FIRSTOS_MAX_CONCURRENT_DOWNLOADS) {
        task->state = DOWNLOAD_STATE_FAILED;
        task->error = DOWNLOAD_ERR_MAX_CONCURRENT;
        serial_write("[download] error: maximum concurrent downloads reached\n");
        return 0;
    }

    /* Initialize temporary file */
    fs_delete_at(task->part_path);
    if (!fs_create_at(task->part_path, 0)) {
        task->state = DOWNLOAD_STATE_FAILED;
        task->error = DOWNLOAD_ERR_DISK_FULL;
        return 0;
    }

    task->state = DOWNLOAD_STATE_ACTIVE;
    task->start_tick = clock_monotonic_get_ms() / 1000;

    download_context_t ctx;
    ctx.task = task;
    ctx.received = 0;
    ctx.error_flag = 0;

    http_request_t req;
    memset(&req, 0, sizeof(req));
    req.url = task->url;
    req.method = HTTP_METHOD_GET;
    req.follow_redirects = FIRSTOS_MAX_DOWNLOAD_REDIRECTS;
    req.timeout_polls = 300000;

    serial_write("[download] streaming: ");
    serial_write(task->url);
    serial_write(" -> ");
    serial_write(task->part_path);
    serial_write("\n");

    int stream_ok = http_stream(&req, download_stream_callback, &ctx);

    if (task->state == DOWNLOAD_STATE_CANCELLED) {
        fs_delete_at(task->part_path);
        serial_write("[download] cancelled: cleaned up part file\n");
        if (task->on_complete) task->on_complete(task);
        return 0;
    }

    if (stream_ok && !ctx.error_flag && ctx.received > 0) {
        /* Atomic rename to final path */
        int renamed = fs_rename_at(task->part_path, task->target_path);
        if (renamed) {
            task->state = DOWNLOAD_STATE_COMPLETED;
            task->error = DOWNLOAD_ERR_NONE;
            serial_write("[download] completed successfully: ");
            serial_write(task->target_path);
            serial_write("\n");
        } else {
            task->state = DOWNLOAD_STATE_FAILED;
            task->error = DOWNLOAD_ERR_RENAME_FAILED;
            fs_delete_at(task->part_path);
            serial_write("[download] error: rename failed\n");
        }
    } else {
        task->state = DOWNLOAD_STATE_FAILED;
        if (task->error == DOWNLOAD_ERR_NONE) {
            task->error = DOWNLOAD_ERR_CONNECT;
        }
        fs_delete_at(task->part_path);
        serial_write("[download] failed: cleaned up part file\n");
    }

    if (task->on_complete) {
        task->on_complete(task);
    }
    return (task->state == DOWNLOAD_STATE_COMPLETED);
}

void download_cancel(u32 task_id) {
    download_task_t *task = download_find_by_id(task_id);
    if (task && (task->state == DOWNLOAD_STATE_ACTIVE || task->state == DOWNLOAD_STATE_PENDING)) {
        task->state = DOWNLOAD_STATE_CANCELLED;
        task->error = DOWNLOAD_ERR_CANCELLED;
        fs_delete_at(task->part_path);
        serial_write("[download] task cancelled by user\n");
    }
}

void download_cancel_for_tab(u32 tab_id) {
    for (int i = 0; i < FIRSTOS_MAX_DOWNLOADS; i++) {
        if (g_download_tasks[i].tab_id == tab_id &&
            (g_download_tasks[i].state == DOWNLOAD_STATE_ACTIVE ||
             g_download_tasks[i].state == DOWNLOAD_STATE_PENDING)) {
            download_cancel(g_download_tasks[i].id);
        }
    }
}

void download_cancel_for_frame(u32 tab_id, u32 frame_id) {
    for (int i = 0; i < FIRSTOS_MAX_DOWNLOADS; i++) {
        if (g_download_tasks[i].tab_id == tab_id &&
            g_download_tasks[i].frame_id == frame_id &&
            (g_download_tasks[i].state == DOWNLOAD_STATE_ACTIVE ||
             g_download_tasks[i].state == DOWNLOAD_STATE_PENDING)) {
            download_cancel(g_download_tasks[i].id);
        }
    }
}

download_task_t *download_start(const char *url, const char *destination_dir,
                                void (*on_progress)(download_task_t*),
                                void (*on_complete)(download_task_t*)) {
    (void)destination_dir;
    download_task_t *task = download_create(url, NULL, 0, 0, 0, 0, 0, 0);
    if (!task) return NULL;
    task->on_progress = on_progress;
    task->on_complete = on_complete;
    download_start_stream(task);
    return task;
}
