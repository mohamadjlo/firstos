#ifndef DOWNLOAD_MANAGER_H
#define DOWNLOAD_MANAGER_H

#include "io.h"

#define FIRSTOS_MAX_DOWNLOADS            8
#define FIRSTOS_MAX_CONCURRENT_DOWNLOADS 2
#define FIRSTOS_MAX_DOWNLOAD_BYTES       (16u * 1024u * 1024u) /* 16 MiB */
#define FIRSTOS_MAX_DOWNLOAD_URL         1024
#define FIRSTOS_MAX_DOWNLOAD_FILENAME    128
#define FIRSTOS_MAX_DOWNLOAD_PATH        256
#define FIRSTOS_MAX_DOWNLOAD_REDIRECTS   8
#define FIRSTOS_DOWNLOAD_CHUNK_BYTES     4096

/* Legacy compatibility aliases */
#define MAX_DOWNLOAD_TASKS               FIRSTOS_MAX_DOWNLOADS

typedef enum {
    DOWNLOAD_STATE_FREE = 0,
    DOWNLOAD_STATE_PENDING,
    DOWNLOAD_STATE_ACTIVE,
    DOWNLOAD_STATE_COMPLETED,
    DOWNLOAD_STATE_FAILED,
    DOWNLOAD_STATE_CANCELLED
} firstos_download_state_t;

/* Legacy status enum alias */
typedef firstos_download_state_t download_status_t;
#define DOWNLOAD_QUEUED    DOWNLOAD_STATE_PENDING
#define DOWNLOAD_ACTIVE    DOWNLOAD_STATE_ACTIVE
#define DOWNLOAD_COMPLETED DOWNLOAD_STATE_COMPLETED
#define DOWNLOAD_FAILED    DOWNLOAD_STATE_FAILED
#define DOWNLOAD_CANCELLED DOWNLOAD_STATE_CANCELLED

typedef enum {
    DOWNLOAD_ERR_NONE = 0,
    DOWNLOAD_ERR_INVALID_REQ,
    DOWNLOAD_ERR_NO_GESTURE,
    DOWNLOAD_ERR_MAX_CONCURRENT,
    DOWNLOAD_ERR_CONNECT,
    DOWNLOAD_ERR_HTTP_STATUS,
    DOWNLOAD_ERR_SIZE_LIMIT,
    DOWNLOAD_ERR_DISK_FULL,
    DOWNLOAD_ERR_RENAME_FAILED,
    DOWNLOAD_ERR_CANCELLED,
    DOWNLOAD_ERR_UNSAFE_SCHEME,
    DOWNLOAD_ERR_REDIRECT_LIMIT
} firstos_download_error_t;

typedef struct download_task {
    u32 id;
    firstos_download_state_t state;
    firstos_download_state_t status; /* Legacy alias for state */
    firstos_download_error_t error;

    /* Context ownership & validation */
    u32 tab_id;
    u32 tab_generation;
    u32 frame_id;
    u32 frame_generation;
    u32 navigation_id;
    u32 document_generation;

    /* URLs and Paths */
    char url[FIRSTOS_MAX_DOWNLOAD_URL];
    char suggested_filename[FIRSTOS_MAX_DOWNLOAD_FILENAME];
    char filename[FIRSTOS_MAX_DOWNLOAD_FILENAME];
    char target_path[FIRSTOS_MAX_DOWNLOAD_PATH];
    char part_path[FIRSTOS_MAX_DOWNLOAD_PATH];

    /* Byte accounting */
    u32 bytes_received;
    u32 total_bytes;
    u32 speed_kbps;
    u32 start_tick;

    /* Legacy callback hooks */
    void (*on_progress)(struct download_task *task);
    void (*on_complete)(struct download_task *task);
} download_task_t;

typedef download_task_t firstos_download_task_t;

/* Download Manager API */
void download_manager_init(void);
void download_manager_reset(void);

/* Lifecycle */
download_task_t *download_create(const char *url, const char *suggested_filename,
                                 u32 tab_id, u32 tab_gen,
                                 u32 frame_id, u32 frame_gen,
                                 u32 nav_id, u32 doc_gen);
int  download_start_stream(download_task_t *task);
void download_cancel(u32 task_id);
void download_cancel_for_tab(u32 tab_id);
void download_cancel_for_frame(u32 tab_id, u32 frame_id);

/* Queries */
int              download_get_count(void);
int              download_get_active_count(void);
download_task_t *download_get_task(int index);
download_task_t *download_find_by_id(u32 id);

/* Sanitization and Utilities */
int  download_sanitize_filename(const char *input, char *out_buf, int max_len);
int  download_resolve_collision(const char *dir, const char *base_name, char *out_path, int max_len);
int  download_parse_content_disposition(const char *header_val, char *out_filename, int max_len, int *out_is_attachment);

/* Legacy API compatibility */
download_task_t *download_start(const char *url, const char *destination_dir,
                                void (*on_progress)(download_task_t*),
                                void (*on_complete)(download_task_t*));

#endif /* DOWNLOAD_MANAGER_H */
