#ifndef BROWSER_LOGGER_H
#define BROWSER_LOGGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "io.h"

typedef enum {
    BLOG_LEVEL_INFO = 0,
    BLOG_LEVEL_WARN,
    BLOG_LEVEL_ERROR
} browser_log_level_t;

typedef enum {
    BLOG_CAT_PAGE = 0,   /* Page navigation, HTTP, headers, status codes */
    BLOG_CAT_CSS,        /* CSS stylesheets, syntax, selectors, cascade */
    BLOG_CAT_SCRIPT,     /* JS parsing, execution, DOM API, statements */
    BLOG_CAT_RENDER,     /* DOM layout, bounds, paint, dimensions */
    BLOG_CAT_RESOURCE,   /* Images, fonts, media, subresources */
    BLOG_CAT_NET,        /* DNS, TCP, TLS, Proxy transport */
    BLOG_CAT_SECURITY    /* URL policy, CSP, sandboxing */
} browser_log_cat_t;

#define BROWSER_MAX_LOG_ENTRIES 512
#define BROWSER_LOG_MSG_MAX     160
#define BROWSER_MAX_SITE_STATS  128

typedef struct {
    browser_log_level_t level;
    browser_log_cat_t category;
    char category_name[12];
    char url[128];
    char message[BROWSER_LOG_MSG_MAX];
    int http_status;
    int err_code;
    uint32_t nav_id;
    uint32_t timestamp;
} browser_log_entry_t;

typedef struct {
    char url[128];
    uint32_t nav_id;
    uint32_t start_tick;
    uint32_t duration_ticks;
    int http_status;
    int err_code;
    int dom_nodes;
    int doc_height;
    int css_bytes;
    int error_count;
    int warn_count;
    int completed;
    int failed;
} browser_site_stat_t;

/* Initialize or clear the browser diagnostic log ring */
void browser_log_init(void);
void browser_log_clear(void);

/* Track active navigation context without wiping historical site logs */
void browser_log_begin_site(const char *url, uint32_t nav_id);
void browser_log_complete_site(int http_status, int dom_nodes, int doc_height, int css_bytes);
void browser_log_fail_site(int http_status, int err_code, const char *reason);

/* Live tracking toggle for terminal / serial error stream */
void browser_log_set_live_track(int enabled);
int  browser_log_get_live_track(void);

/* Structured logging functions */
void browser_log(browser_log_level_t level, browser_log_cat_t cat, const char *fmt, ...);
void browser_log_ex(browser_log_level_t level, browser_log_cat_t cat, const char *url, int http_status, int err_code, const char *fmt, ...);
void browser_log_error(browser_log_cat_t cat, const char *fmt, ...);
void browser_log_warn(browser_log_cat_t cat, const char *fmt, ...);
void browser_log_info(browser_log_cat_t cat, const char *fmt, ...);

/* Inspection API for DevTools UI, Terminal error tracker, and status bar */
int browser_log_get_count(void);
int browser_log_get_error_count(void);
int browser_log_get_warn_count(void);
int browser_log_get_current_site_errors(void);
int browser_log_get_current_site_warnings(void);
const browser_log_entry_t *browser_log_get_entry(int index);

/* Per-site tracking API */
int browser_log_get_site_count(void);
const browser_site_stat_t *browser_log_get_site_stat(int index);
int browser_log_get_category_errors(browser_log_cat_t cat);
int browser_log_get_category_warnings(browser_log_cat_t cat);

/* Browser v2.1 Operational Metrics */
typedef struct {
    uint32_t pages_attempted;
    uint32_t pages_loaded;
    uint32_t pages_failed;
    uint32_t scripts_executed;
    uint32_t resources_loaded;
    uint32_t layout_passes;
    uint32_t paint_frames;
    uint32_t timeouts;
    uint32_t crashes;
    /* v2.1 Extensions */
    uint32_t storage_reads;
    uint32_t storage_writes;
    uint32_t storage_persisted_after_reboot;
    uint32_t js_errors;
    uint32_t timers_scheduled;
    uint32_t timers_cleared;
    uint32_t fetch_requests;
    uint32_t xhr_requests;
    uint32_t redirects_followed;
} browser_metrics_t;

/* Debug Logging Toggles */
extern int g_storage_debug;
extern int g_js_debug;
extern int g_net_debug;
extern int g_compat_debug;

void browser_metrics_init(void);
void browser_metrics_record_attempt(void);
void browser_metrics_record_success(void);
void browser_metrics_record_fail(void);
void browser_metrics_record_script(void);
void browser_metrics_record_resource(void);
void browser_metrics_record_layout(void);
void browser_metrics_record_frame(void);
void browser_metrics_record_storage_read(void);
void browser_metrics_record_storage_write(void);
void browser_metrics_record_redirect(void);
void browser_metrics_record_timer_sched(void);
void browser_metrics_record_timer_clear(void);
const browser_metrics_t *browser_metrics_get(void);

#endif /* BROWSER_LOGGER_H */
