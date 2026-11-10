#include "browser_core.h"
#include "browser_network_service.h"
#include "browser_renderer_pipeline.h"
#include "browser_storage_service.h"
#include "include/firstos/security/browser_security.h"
#include "browser_logger.h"
#include "http_v11.h"
#include "serial.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static browser_document_t g_doc;
static char g_status_msg[128] = "Ready";
static void (*g_core_status_cb)(const char *status) = NULL;

void browser_core_set_status_callback(void (*cb)(const char *status)) {
    g_core_status_cb = cb;
}

const char *browser_core_get_status_message(void) {
    return g_status_msg;
}

static void browser_core_notify_status(const char *msg) {
    if (!msg) return;
    strncpy(g_status_msg, msg, sizeof(g_status_msg) - 1);
    g_status_msg[sizeof(g_status_msg) - 1] = 0;
    if (g_core_status_cb) g_core_status_cb(g_status_msg);
}

static void http_status_forward(const char *status, u32 current_bytes, u32 total_bytes) {
    (void)current_bytes;
    (void)total_bytes;
    browser_core_notify_status(status);
}

void browser_core_init(void) {
    memset(&g_doc, 0, sizeof(g_doc));
    g_doc.state = BROWSER_CORE_IDLE;
    browser_core_notify_status("Ready");
    http_set_status_callback(http_status_forward);

    /* Initialize decoupled services */
    browser_network_service_init();
    browser_renderer_pipeline_init();
    browser_storage_service_init();

    serial_write("[browser] UI & Navigation Orchestrator initialized\n");
}

int browser_core_begin_navigation_gen(const char *url, u32 generation) {
    if (!url || !url[0]) return 0;
    browser_log_clear();
    browser_log_info(BLOG_CAT_PAGE, "Navigating to: %s", url);
    memset(&g_doc, 0, sizeof(g_doc));
    strncpy(g_doc.url, url, sizeof(g_doc.url) - 1);
    g_doc.state = BROWSER_CORE_LOADING;
    g_doc.generation = generation;
    g_doc.error_code = BNET_OK;

    serial_write("[browser] navigate url=");
    serial_write(url);
    serial_write("\n");
    serial_write("[browser] state=FETCHING\n");

    serial_write("[browser_core] navigation start: ");
    serial_write(url);
    serial_write("\n");

    char buf[128];
    snprintf(buf, sizeof(buf), "Requesting %.100s...", url);
    browser_core_notify_status(buf);
    return 1;
}

int browser_core_begin_navigation(const char *url) {
    return browser_core_begin_navigation_gen(url, 0);
}

int browser_core_is_in_flight(void) {
    return (browser_network_service_is_in_flight() ||
            g_doc.state == BROWSER_CORE_LOADING ||
            g_doc.state == BROWSER_CORE_NAV_CONNECTING ||
            g_doc.state == BROWSER_CORE_NAV_SENDING ||
            g_doc.state == BROWSER_CORE_NAV_RECEIVING);
}

void browser_core_cancel(void) {
    browser_network_service_cancel();
    g_doc.state = BROWSER_CORE_IDLE;
    browser_core_notify_status("Ready");
}

const browser_document_t *browser_core_document(void) {
    return &g_doc;
}

int browser_core_start_async(const char *url, u32 generation) {
    if (!url || !url[0]) return 0;
    browser_network_service_cancel();
    browser_log_clear();
    browser_log_info(BLOG_CAT_PAGE, "Async navigating to: %s", url);

    memset(&g_doc, 0, sizeof(g_doc));
    strncpy(g_doc.url, url, sizeof(g_doc.url) - 1);
    g_doc.state = BROWSER_CORE_NAV_CONNECTING;
    g_doc.generation = generation;
    g_doc.error_code = BNET_OK;

    serial_write("[browser] navigate url=");
    serial_write(url);
    serial_write("\n");
    serial_write("[browser] state=FETCHING\n");

    serial_write("[browser_core] async navigation start: ");
    serial_write(url);
    serial_write("\n");
    serial_write("[browser_core] navigation start: ");
    serial_write(url);
    serial_write("\n");

    char buf[128];
    snprintf(buf, sizeof(buf), "Connecting to %.80s...", url);
    browser_core_notify_status(buf);

    browser_network_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.on_status = browser_core_notify_status;

    return browser_network_service_start_fetch(url, generation, &cbs);
}

int browser_core_step(char *body, int body_max, browser_document_t *out_doc) {
    static int s_in_step = 0;
    if (s_in_step) return 0;
    s_in_step = 1;

    if (!browser_network_service_is_in_flight()) {
        if (out_doc) *out_doc = g_doc;
        s_in_step = 0;
        return (g_doc.state == BROWSER_CORE_READY) ? g_doc.body_len : 0;
    }

    browser_network_result_t net_res;
    memset(&net_res, 0, sizeof(net_res));
    int bytes = browser_network_service_step(body, body_max, &net_res);

    if (net_res.state == NET_SERVICE_STATE_CONNECTING) {
        g_doc.state = BROWSER_CORE_NAV_CONNECTING;
    } else if (net_res.state == NET_SERVICE_STATE_SENDING) {
        g_doc.state = BROWSER_CORE_NAV_SENDING;
    } else if (net_res.state == NET_SERVICE_STATE_RECEIVING) {
        g_doc.state = BROWSER_CORE_NAV_RECEIVING;
    } else if (net_res.state == NET_SERVICE_STATE_COMPLETE) {
        g_doc.state = BROWSER_CORE_READY;
        g_doc.body_len = net_res.body_len;
        g_doc.status_code = net_res.status_code;
        strncpy(g_doc.mime, net_res.mime, sizeof(g_doc.mime) - 1);
        g_doc.error_code = BNET_OK;
        browser_core_notify_status("Download complete");
        serial_write("[browser_core] async navigation complete: ");
        serial_write(g_doc.url);
        serial_write("\n");
        serial_write("[browser_core] navigation complete: ");
        serial_write(g_doc.url);
        serial_write("\n");
        browser_log_info(BLOG_CAT_PAGE, "Page load complete (%u bytes, HTTP %d)", g_doc.body_len, g_doc.status_code);
    } else if (net_res.state == NET_SERVICE_STATE_ERROR) {
        g_doc.state = BROWSER_CORE_ERROR;
        g_doc.error_code = net_res.error_code;
        strncpy(g_doc.error, net_res.error_msg, sizeof(g_doc.error) - 1);
        browser_core_notify_status(g_doc.error);
        browser_log_error(BLOG_CAT_PAGE, "Async fetch failed: %s (%s)", bnet_error_name(net_res.error_code), g_doc.error);
    }

    if (out_doc) *out_doc = g_doc;
    s_in_step = 0;
    return bytes;
}

int browser_core_fetch_gen(char *body, int body_max, browser_document_t *out_doc, u32 expected_generation) {
    if (!body || body_max <= 1) return 0;
    if (!browser_core_start_async(g_doc.url, expected_generation)) {
        if (out_doc) *out_doc = g_doc;
        return 0;
    }

    /* Synchronously pump the network service until finished */
    u32 poll_limit = 200000;
    while (poll_limit-- > 0 && browser_network_service_is_in_flight()) {
        int res = browser_core_step(body, body_max, out_doc);
        if (res > 0) return res;
        if (g_doc.state == BROWSER_CORE_ERROR) break;
    }

    if (out_doc) *out_doc = g_doc;
    return (g_doc.state == BROWSER_CORE_READY) ? g_doc.body_len : 0;
}

int browser_core_fetch(char *body, int body_max, browser_document_t *out_doc) {
    return browser_core_fetch_gen(body, body_max, out_doc, 0);
}
