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
    BLOG_CAT_RESOURCE    /* Images, fonts, media, subresources */
} browser_log_cat_t;

#define BROWSER_MAX_LOG_ENTRIES 64
#define BROWSER_LOG_MSG_MAX     160

typedef struct {
    browser_log_level_t level;
    browser_log_cat_t category;
    char category_name[12];
    char message[BROWSER_LOG_MSG_MAX];
    uint32_t timestamp;
} browser_log_entry_t;

/* Initialize or clear the browser diagnostic log ring */
void browser_log_init(void);
void browser_log_clear(void);

/* Structured logging functions */
void browser_log(browser_log_level_t level, browser_log_cat_t cat, const char *fmt, ...);
void browser_log_error(browser_log_cat_t cat, const char *fmt, ...);
void browser_log_warn(browser_log_cat_t cat, const char *fmt, ...);
void browser_log_info(browser_log_cat_t cat, const char *fmt, ...);

/* Inspection API for DevTools UI and status bar */
int browser_log_get_count(void);
int browser_log_get_error_count(void);
int browser_log_get_warn_count(void);
const browser_log_entry_t *browser_log_get_entry(int index);

#endif /* BROWSER_LOGGER_H */
