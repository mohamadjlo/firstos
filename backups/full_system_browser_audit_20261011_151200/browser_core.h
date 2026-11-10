#ifndef BROWSER_CORE_H
#define BROWSER_CORE_H

#include "io.h"
#include "bnet_error.h"

#define BROWSER_URL_MAX 1024
#define BROWSER_MIME_MAX 64

typedef enum {
    BROWSER_CORE_IDLE = 0,
    BROWSER_CORE_LOADING,
    BROWSER_CORE_NAV_CONNECTING,
    BROWSER_CORE_NAV_SENDING,
    BROWSER_CORE_NAV_RECEIVING,
    BROWSER_CORE_READY,
    BROWSER_CORE_ERROR
} browser_core_state_t;

typedef struct {
    browser_core_state_t state;
    u32 generation;
    int status_code;
    int body_len;
    char url[BROWSER_URL_MAX];
    char mime[BROWSER_MIME_MAX];
    char error[128];
    bnet_error_t error_code;
} browser_document_t;

void browser_core_init(void);
int browser_core_begin_navigation(const char *url);
int browser_core_begin_navigation_gen(const char *url, u32 generation);
int browser_core_start_async(const char *url, u32 generation);
int browser_core_step(char *body, int body_max, browser_document_t *out_doc);
int browser_core_is_in_flight(void);
int browser_core_fetch(char *body, int body_max, browser_document_t *out_doc);
int browser_core_fetch_gen(char *body, int body_max, browser_document_t *out_doc, u32 expected_generation);
void browser_core_cancel(void);
const browser_document_t *browser_core_document(void);
void browser_core_set_status_callback(void (*cb)(const char *status));
const char *browser_core_get_status_message(void);

#endif
