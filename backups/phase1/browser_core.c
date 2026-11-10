#include "browser_core.h"
#include "http_v11.h"
#include "serial.h"
#include <string.h>
#include <stdio.h>

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
}

int browser_core_begin_navigation(const char *url) {
    if (!url || !url[0]) return 0;
    memset(&g_doc, 0, sizeof(g_doc));
    strncpy(g_doc.url, url, sizeof(g_doc.url) - 1);
    g_doc.state = BROWSER_CORE_LOADING;
    g_doc.error_code = BNET_OK;

    serial_write("[browser_core] navigation start: ");
    serial_write(url);
    serial_write("\n");

    char buf[128];
    sprintf(buf, "Requesting %.100s...", url);
    browser_core_notify_status(buf);
    return 1;
}

void browser_core_cancel(void) {
    g_doc.state = BROWSER_CORE_IDLE;
    browser_core_notify_status("Ready");
}

const browser_document_t *browser_core_document(void) {
    return &g_doc;
}

int browser_core_fetch(char *body, int body_max, browser_document_t *out_doc) {
    if (!body || body_max <= 1 || g_doc.state != BROWSER_CORE_LOADING) return 0;

    http_request_t req;
    memset(&req, 0, sizeof(req));
    req.url = g_doc.url;
    req.method = HTTP_METHOD_GET;
    req.follow_redirects = 1;
    /* Keep navigation bounded because this function is called from the GUI
     * tick.  HTTP enforces the same value as a total response deadline. */
    req.timeout_polls = 100000;

    serial_write("[browser_core] GET ");
    serial_write(g_doc.url);
    serial_write("\n");

    req.max_response_size = (u32)body_max;

    bnet_error_t net_err = BNET_OK;
    http_response_t *res = http_execute_ex(&req, &net_err);
    if (g_doc.state != BROWSER_CORE_LOADING) {
        if (res) http_response_free(res);
        return 0;
    }
    if (!res) {
        g_doc.state = BROWSER_CORE_ERROR;
        g_doc.error_code = net_err;
        const char *desc = bnet_error_description(net_err);
        strncpy(g_doc.error, desc ? desc : "Network or TLS connection failed", sizeof(g_doc.error) - 1);
        g_doc.error[sizeof(g_doc.error) - 1] = 0;
        browser_core_notify_status(g_doc.error);

        serial_write("[browser_core] fetch failed: ");
        serial_write(bnet_error_name(net_err));
        serial_write(" - ");
        serial_write(g_doc.error);
        serial_write("\n");

        if (out_doc) *out_doc = g_doc;
        return 0;
    }

    g_doc.status_code = res->status_code;
    strncpy(g_doc.mime, res->content_type, sizeof(g_doc.mime) - 1);
    if (res->status_code < 200 || res->status_code >= 400 || !res->body) {
        g_doc.state = BROWSER_CORE_ERROR;
        g_doc.error_code = (net_err != BNET_OK) ? net_err : ((res->status_code == 0) ? BNET_ERR_HTTP_BAD_RESPONSE : BNET_OK);
        if (net_err == BNET_ERR_PROXY_HTTP_ERROR) {
            if (res->body && res->body_len > 0) {
                char err_detail[80];
                int dlen = (int)res->body_len;
                if (dlen >= (int)sizeof(err_detail)) dlen = sizeof(err_detail) - 1;
                memcpy(err_detail, res->body, dlen);
                err_detail[dlen] = 0;
                char *nl = strchr(err_detail, '\n');
                if (nl) *nl = 0;
                char *cr = strchr(err_detail, '\r');
                if (cr) *cr = 0;
                snprintf(g_doc.error, sizeof(g_doc.error), "Proxy bridge (%d): %.60s", res->status_code, err_detail);
            } else {
                snprintf(g_doc.error, sizeof(g_doc.error), "Proxy bridge upstream fetch failed (HTTP %d %s)",
                         res->status_code, res->status_message[0] ? res->status_message : "Error");
            }
        } else {
            snprintf(g_doc.error, sizeof(g_doc.error), "HTTP %d %s", res->status_code, res->status_message[0] ? res->status_message : "Error");
        }
        browser_core_notify_status(g_doc.error);

        serial_write("[browser_core] HTTP error: ");
        serial_write(g_doc.error);
        serial_write("\n");

        http_response_free(res);
        if (out_doc) *out_doc = g_doc;
        return 0;
    }

    if (res->is_truncated || !res->is_complete) {
        g_doc.state = BROWSER_CORE_ERROR;
        g_doc.error_code = BNET_ERR_BODY_TRUNCATED;
        strncpy(g_doc.error, "Page incomplete or exceeded buffer limit", sizeof(g_doc.error) - 1);
        g_doc.error[sizeof(g_doc.error) - 1] = 0;
        browser_core_notify_status(g_doc.error);

        serial_write("[browser_core] fetch failed: ");
        serial_write(bnet_error_name(BNET_ERR_BODY_TRUNCATED));
        serial_write(" - ");
        serial_write(g_doc.error);
        serial_write("\n");

        http_response_free(res);
        if (out_doc) *out_doc = g_doc;
        return 0;
    }

    u32 copy_len = res->body_len;
    if (copy_len >= (u32)body_max) copy_len = (u32)body_max - 1;
    memcpy(body, res->body, copy_len);
    body[copy_len] = 0;
    g_doc.body_len = (int)copy_len;
    g_doc.error_code = BNET_OK;
    g_doc.state = BROWSER_CORE_READY;
    browser_core_notify_status("Download complete");

    serial_write("[browser_core] navigation complete: ");
    serial_write(g_doc.url);
    serial_write("\n");

    http_response_free(res);
    if (out_doc) *out_doc = g_doc;
    return (int)copy_len;
}
