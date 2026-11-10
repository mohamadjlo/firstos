#include "browser_logger.h"
#include "serial.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static browser_log_entry_t s_log_entries[BROWSER_MAX_LOG_ENTRIES];
static int s_log_count = 0;
static int s_log_head = 0;
static int s_error_count = 0;
static int s_warn_count = 0;
static int s_cat_errors[8] = {0};
static int s_cat_warnings[8] = {0};

static browser_site_stat_t s_site_stats[BROWSER_MAX_SITE_STATS];
static int s_site_count = 0;
static int s_site_head = 0;
static int s_active_site_idx = -1;

static char s_active_url[128] = "about:blank";
static uint32_t s_active_nav_id = 0;
static int s_live_track_enabled = 1;

static const char *get_cat_name(browser_log_cat_t cat) {
    switch (cat) {
        case BLOG_CAT_PAGE:     return "PAGE";
        case BLOG_CAT_CSS:      return "CSS";
        case BLOG_CAT_SCRIPT:   return "SCRIPT";
        case BLOG_CAT_RENDER:   return "RENDER";
        case BLOG_CAT_RESOURCE: return "RESOURCE";
        case BLOG_CAT_NET:      return "NET";
        case BLOG_CAT_SECURITY: return "SECURITY";
        default:                return "GENERAL";
    }
}

static const char *get_level_prefix(browser_log_level_t level) {
    switch (level) {
        case BLOG_LEVEL_ERROR: return "[browser:error]";
        case BLOG_LEVEL_WARN:  return "[browser:warn]";
        default:               return "[browser:info]";
    }
}

void browser_log_init(void) {
    memset(s_log_entries, 0, sizeof(s_log_entries));
    memset(s_site_stats, 0, sizeof(s_site_stats));
    memset(s_cat_errors, 0, sizeof(s_cat_errors));
    memset(s_cat_warnings, 0, sizeof(s_cat_warnings));
    s_log_count = 0;
    s_log_head = 0;
    s_error_count = 0;
    s_warn_count = 0;
    s_site_count = 0;
    s_site_head = 0;
    s_active_site_idx = -1;
    strncpy(s_active_url, "about:blank", sizeof(s_active_url) - 1);
    s_active_nav_id = 0;
}

void browser_log_clear(void) {
    browser_log_init();
}

void browser_log_set_live_track(int enabled) {
    s_live_track_enabled = enabled ? 1 : 0;
}

int browser_log_get_live_track(void) {
    return s_live_track_enabled;
}

void browser_log_begin_site(const char *url, uint32_t nav_id) {
    if (url && url[0]) {
        strncpy(s_active_url, url, sizeof(s_active_url) - 1);
        s_active_url[sizeof(s_active_url) - 1] = '\0';
    } else {
        strncpy(s_active_url, "about:blank", sizeof(s_active_url) - 1);
    }
    s_active_nav_id = nav_id;

    int idx;
    if (s_site_count < BROWSER_MAX_SITE_STATS) {
        idx = s_site_count++;
    } else {
        idx = s_site_head;
        s_site_head = (s_site_head + 1) % BROWSER_MAX_SITE_STATS;
    }
    s_active_site_idx = idx;

    browser_site_stat_t *st = &s_site_stats[idx];
    memset(st, 0, sizeof(*st));
    strncpy(st->url, s_active_url, sizeof(st->url) - 1);
    st->nav_id = nav_id;
    st->start_tick = get_system_ticks();
}

void browser_log_complete_site(int http_status, int dom_nodes, int doc_height, int css_bytes) {
    if (s_active_site_idx < 0 || s_active_site_idx >= BROWSER_MAX_SITE_STATS) return;
    browser_site_stat_t *st = &s_site_stats[s_active_site_idx];
    st->http_status = http_status;
    st->dom_nodes = dom_nodes;
    st->doc_height = doc_height;
    st->css_bytes = css_bytes;
    st->duration_ticks = get_system_ticks() - st->start_tick;
    st->completed = 1;
    st->failed = 0;
}

void browser_log_fail_site(int http_status, int err_code, const char *reason) {
    if (s_active_site_idx >= 0 && s_active_site_idx < BROWSER_MAX_SITE_STATS) {
        browser_site_stat_t *st = &s_site_stats[s_active_site_idx];
        st->http_status = http_status;
        st->err_code = err_code;
        st->duration_ticks = get_system_ticks() - st->start_tick;
        st->completed = 0;
        st->failed = 1;
    }
    if (reason && reason[0]) {
        browser_log_ex(BLOG_LEVEL_ERROR, BLOG_CAT_NET, s_active_url, http_status, err_code,
                       "Navigation failed (status=%d, err=%d): %s", http_status, err_code, reason);
    }
}

static void record_log_internal(browser_log_level_t level, browser_log_cat_t cat,
                                const char *url, int http_status, int err_code,
                                const char *msg_buf) {
    int idx;
    if (s_log_count < BROWSER_MAX_LOG_ENTRIES) {
        idx = s_log_count++;
    } else {
        idx = s_log_head;
        s_log_head = (s_log_head + 1) % BROWSER_MAX_LOG_ENTRIES;
    }

    browser_log_entry_t *entry = &s_log_entries[idx];
    entry->level = level;
    entry->category = cat;
    const char *cat_str = get_cat_name(cat);
    strncpy(entry->category_name, cat_str, sizeof(entry->category_name) - 1);
    entry->category_name[sizeof(entry->category_name) - 1] = '\0';

    const char *eff_url = (url && url[0]) ? url : s_active_url;
    strncpy(entry->url, eff_url, sizeof(entry->url) - 1);
    entry->url[sizeof(entry->url) - 1] = '\0';

    strncpy(entry->message, msg_buf, sizeof(entry->message) - 1);
    entry->message[sizeof(entry->message) - 1] = '\0';
    entry->http_status = http_status;
    entry->err_code = err_code;
    entry->nav_id = s_active_nav_id;
    entry->timestamp = get_system_ticks();

    int cidx = (int)cat;
    if (cidx < 0 || cidx >= 8) cidx = 0;

    if (level == BLOG_LEVEL_ERROR) {
        s_error_count++;
        s_cat_errors[cidx]++;
        if (s_active_site_idx >= 0 && s_active_site_idx < BROWSER_MAX_SITE_STATS) {
            s_site_stats[s_active_site_idx].error_count++;
        }
    } else if (level == BLOG_LEVEL_WARN) {
        s_warn_count++;
        s_cat_warnings[cidx]++;
        if (s_active_site_idx >= 0 && s_active_site_idx < BROWSER_MAX_SITE_STATS) {
            s_site_stats[s_active_site_idx].warn_count++;
        }
    }

    if (s_live_track_enabled || level != BLOG_LEVEL_INFO) {
        serial_write(get_level_prefix(level));
        serial_write(" [");
        serial_write(entry->category_name);
        serial_write("] (");
        serial_write(entry->url);
        serial_write(") ");
        serial_write(entry->message);
        serial_write("\n");
    }
}

void browser_log(browser_log_level_t level, browser_log_cat_t cat, const char *fmt, ...) {
    if (!fmt) return;
    char msg_buf[BROWSER_LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);
    record_log_internal(level, cat, s_active_url, 0, 0, msg_buf);
}

void browser_log_ex(browser_log_level_t level, browser_log_cat_t cat, const char *url, int http_status, int err_code, const char *fmt, ...) {
    if (!fmt) return;
    char msg_buf[BROWSER_LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);
    record_log_internal(level, cat, url, http_status, err_code, msg_buf);
}

void browser_log_error(browser_log_cat_t cat, const char *fmt, ...) {
    if (!fmt) return;
    char msg_buf[BROWSER_LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);
    record_log_internal(BLOG_LEVEL_ERROR, cat, s_active_url, 0, 0, msg_buf);
}

void browser_log_warn(browser_log_cat_t cat, const char *fmt, ...) {
    if (!fmt) return;
    char msg_buf[BROWSER_LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);
    record_log_internal(BLOG_LEVEL_WARN, cat, s_active_url, 0, 0, msg_buf);
}

void browser_log_info(browser_log_cat_t cat, const char *fmt, ...) {
    if (!fmt) return;
    char msg_buf[BROWSER_LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);
    record_log_internal(BLOG_LEVEL_INFO, cat, s_active_url, 0, 0, msg_buf);
}

int browser_log_get_count(void) {
    return s_log_count;
}

int browser_log_get_error_count(void) {
    return s_error_count;
}

int browser_log_get_warn_count(void) {
    return s_warn_count;
}

int browser_log_get_current_site_errors(void) {
    if (s_active_site_idx >= 0 && s_active_site_idx < BROWSER_MAX_SITE_STATS) {
        return s_site_stats[s_active_site_idx].error_count;
    }
    return 0;
}

int browser_log_get_current_site_warnings(void) {
    if (s_active_site_idx >= 0 && s_active_site_idx < BROWSER_MAX_SITE_STATS) {
        return s_site_stats[s_active_site_idx].warn_count;
    }
    return 0;
}

const browser_log_entry_t *browser_log_get_entry(int index) {
    if (index < 0 || index >= s_log_count) return NULL;
    if (s_log_count < BROWSER_MAX_LOG_ENTRIES) {
        return &s_log_entries[index];
    }
    int ring_idx = (s_log_head + index) % BROWSER_MAX_LOG_ENTRIES;
    return &s_log_entries[ring_idx];
}

int browser_log_get_site_count(void) {
    return s_site_count;
}

const browser_site_stat_t *browser_log_get_site_stat(int index) {
    if (index < 0 || index >= s_site_count) return NULL;
    if (s_site_count < BROWSER_MAX_SITE_STATS) {
        return &s_site_stats[index];
    }
    int ring_idx = (s_site_head + index) % BROWSER_MAX_SITE_STATS;
    return &s_site_stats[ring_idx];
}

int browser_log_get_category_errors(browser_log_cat_t cat) {
    int cidx = (int)cat;
    if (cidx < 0 || cidx >= 8) return 0;
    return s_cat_errors[cidx];
}

int browser_log_get_category_warnings(browser_log_cat_t cat) {
    int cidx = (int)cat;
    if (cidx < 0 || cidx >= 8) return 0;
    return s_cat_warnings[cidx];
}

/* Operational Metrics Implementation */
static browser_metrics_t s_metrics;

int g_storage_debug = 0;
int g_js_debug = 0;
int g_net_debug = 0;
int g_compat_debug = 0;

void browser_metrics_init(void) {
    memset(&s_metrics, 0, sizeof(s_metrics));
}

void browser_metrics_record_attempt(void) {
    s_metrics.pages_attempted++;
}

void browser_metrics_record_success(void) {
    s_metrics.pages_loaded++;
}

void browser_metrics_record_fail(void) {
    s_metrics.pages_failed++;
}

void browser_metrics_record_script(void) {
    s_metrics.scripts_executed++;
}

void browser_metrics_record_resource(void) {
    s_metrics.resources_loaded++;
}

void browser_metrics_record_layout(void) {
    s_metrics.layout_passes++;
}

void browser_metrics_record_frame(void) {
    s_metrics.paint_frames++;
}

void browser_metrics_record_storage_read(void) {
    s_metrics.storage_reads++;
}

void browser_metrics_record_storage_write(void) {
    s_metrics.storage_writes++;
}

void browser_metrics_record_redirect(void) {
    s_metrics.redirects_followed++;
}

void browser_metrics_record_timer_sched(void) {
    s_metrics.timers_scheduled++;
}

void browser_metrics_record_timer_clear(void) {
    s_metrics.timers_cleared++;
}

const browser_metrics_t *browser_metrics_get(void) {
    return &s_metrics;
}
