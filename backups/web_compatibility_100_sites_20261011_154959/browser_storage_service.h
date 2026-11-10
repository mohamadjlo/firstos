#ifndef BROWSER_STORAGE_SERVICE_H
#define BROWSER_STORAGE_SERVICE_H

#include "io.h"
#include "cookies.h"
#include "download_manager.h"
#include "browser_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize all storage subsystems (cookies, downloads, persistent database) */
void browser_storage_service_init(void);

/* Cookie Operations */
void browser_storage_service_apply_cookie_headers(const char *headers, const char *req_host, const char *req_path, int is_https);
int  browser_storage_service_get_cookie_header(const char *host, const char *path, int is_https, char *out_buf, int max_len);
int  browser_storage_service_set_cookie(const char *domain, const char *path, const char *name, const char *value, int secure, int http_only);

/* Download Operations */
download_task_t *browser_storage_service_create_download(const char *url, const char *suggested_filename, u32 tab_id, u32 tab_gen);
void browser_storage_service_cancel_download(u32 download_id);

/* Persistent History & Session Operations */
int  browser_storage_service_history_add(const char *url, const char *title);
int  browser_storage_service_save_all(void);
int  browser_storage_service_load_all(void);

/* Cache Service Hooks (Phase 2 Foundation) */
int  browser_storage_service_cache_lookup(const char *url, char *out_buf, int max_len, int *out_len);
int  browser_storage_service_cache_store(const char *url, const char *buf, int len);

#ifdef __cplusplus
}
#endif

#endif /* BROWSER_STORAGE_SERVICE_H */
