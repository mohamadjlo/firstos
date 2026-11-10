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

static const char *get_cat_name(browser_log_cat_t cat) {
    switch (cat) {
        case BLOG_CAT_PAGE:     return "PAGE";
        case BLOG_CAT_CSS:      return "CSS";
        case BLOG_CAT_SCRIPT:   return "SCRIPT";
        case BLOG_CAT_RENDER:   return "RENDER";
        case BLOG_CAT_RESOURCE: return "RESOURCE";
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
    s_log_count = 0;
    s_log_head = 0;
    s_error_count = 0;
    s_warn_count = 0;
}

void browser_log_clear(void) {
    memset(s_log_entries, 0, sizeof(s_log_entries));
    s_log_count = 0;
    s_log_head = 0;
    s_error_count = 0;
    s_warn_count = 0;
}

void browser_log(browser_log_level_t level, browser_log_cat_t cat, const char *fmt, ...) {
    if (!fmt) return;

    char msg_buf[BROWSER_LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);

    /* Insert into circular ring buffer */
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
    strncpy(entry->message, msg_buf, sizeof(entry->message) - 1);
    entry->message[sizeof(entry->message) - 1] = '\0';
    entry->timestamp = get_system_ticks();

    if (level == BLOG_LEVEL_ERROR) {
        s_error_count++;
    } else if (level == BLOG_LEVEL_WARN) {
        s_warn_count++;
    }

    /* Output to serial console with structured tags */
    serial_write(get_level_prefix(level));
    serial_write(" [");
    serial_write(entry->category_name);
    serial_write("] ");
    serial_write(entry->message);
    serial_write("\n");
}

void browser_log_error(browser_log_cat_t cat, const char *fmt, ...) {
    if (!fmt) return;
    char msg_buf[BROWSER_LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);
    browser_log(BLOG_LEVEL_ERROR, cat, "%s", msg_buf);
}

void browser_log_warn(browser_log_cat_t cat, const char *fmt, ...) {
    if (!fmt) return;
    char msg_buf[BROWSER_LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);
    browser_log(BLOG_LEVEL_WARN, cat, "%s", msg_buf);
}

void browser_log_info(browser_log_cat_t cat, const char *fmt, ...) {
    if (!fmt) return;
    char msg_buf[BROWSER_LOG_MSG_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg_buf, sizeof(msg_buf), fmt, args);
    va_end(args);
    browser_log(BLOG_LEVEL_INFO, cat, "%s", msg_buf);
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

const browser_log_entry_t *browser_log_get_entry(int index) {
    if (index < 0 || index >= s_log_count) return NULL;
    if (s_log_count < BROWSER_MAX_LOG_ENTRIES) {
        return &s_log_entries[index];
    }
    /* Ring buffer index: 0 is oldest, count-1 is newest */
    int ring_idx = (s_log_head + index) % BROWSER_MAX_LOG_ENTRIES;
    return &s_log_entries[ring_idx];
}
