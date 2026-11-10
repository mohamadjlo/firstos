#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wincompatible-library-redeclaration"
#endif

int printf(const char *format, ...);
int snprintf(char *str, unsigned int size, const char *format, ...);
int strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, unsigned int n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, unsigned int n);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
void *memcpy(void *dest, const void *src, unsigned int n);
void *memset(void *s, int c, unsigned int n);
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
int atoi(const char *nptr);

void *kmalloc(unsigned int size) { return malloc((size_t)size); }
void *krealloc(void *ptr, unsigned int size) { return realloc(ptr, (size_t)size); }
void kfree(void *ptr) { free(ptr); }

#include "bnet_error.c"

/* System tick mock */
static uint32_t s_mock_ticks = 1000;
uint32_t get_system_ticks(void) {
    return s_mock_ticks;
}

/* Serial mock capture */
static char s_serial_log[16384];
static int  s_serial_log_pos = 0;

void serial_write(const char *str) {
    if (!str) return;
    int len = (int)strlen(str);
    if (s_serial_log_pos + len < (int)sizeof(s_serial_log) - 1) {
        memcpy(s_serial_log + s_serial_log_pos, str, (unsigned int)len);
        s_serial_log_pos += len;
        s_serial_log[s_serial_log_pos] = '\0';
    }
}

void serial_write_hex(unsigned int val) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%X", val);
    serial_write(buf);
}

#include "browser_core.h"
#include "net_socket.h"
#include "http_v11.h"

/* Mock net_socket infrastructure */
static int s_mock_socket_connected = 1;
static int s_mock_recv_offset = 0;
static const char *s_mock_http_response = 
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Content-Length: 48\r\n"
    "Connection: close\r\n"
    "\r\n"
    "<!DOCTYPE html><html><body><h1>Async OK</h1></body></html>";

static net_socket_t s_mock_sock;
static tcp_socket_t s_mock_tcp;

net_socket_t *net_socket_open_ex(const char *host, u16 port, socket_proto_t proto, u32 timeout_polls, bnet_error_t *out_err) {
    (void)host; (void)port; (void)proto; (void)timeout_polls;
    if (!s_mock_socket_connected) {
        if (out_err) *out_err = BNET_ERR_TCP_CONNECT_FAILED;
        return NULL;
    }
    memset(&s_mock_sock, 0, sizeof(s_mock_sock));
    memset(&s_mock_tcp, 0, sizeof(s_mock_tcp));
    s_mock_sock.is_connected = 1;
    s_mock_sock.proto = SOCKET_PROTO_PLAIN_TCP;
    s_mock_sock.tcp_sock = &s_mock_tcp;
    if (out_err) *out_err = BNET_OK;
    return &s_mock_sock;
}

int net_socket_send(net_socket_t *sock, const void *buf, int len) {
    (void)sock; (void)buf;
    return len;
}

int net_socket_recv(net_socket_t *sock, void *buf, int max_len, u32 timeout_polls) {
    (void)sock; (void)timeout_polls;
    if (!s_mock_http_response) return 0;
    int resp_len = (int)strlen(s_mock_http_response);
    if (s_mock_recv_offset >= resp_len) {
        s_mock_tcp.rx_fin = 1;
        return 0;
    }
    int chunk = 32; /* Small slices to test async chunk streaming */
    if (s_mock_recv_offset + chunk > resp_len) chunk = resp_len - s_mock_recv_offset;
    if (chunk > max_len) chunk = max_len;
    memcpy(buf, s_mock_http_response + s_mock_recv_offset, (unsigned int)chunk);
    s_mock_recv_offset += chunk;
    return chunk;
}

void net_socket_close(net_socket_t *sock) {
    if (sock) sock->is_connected = 0;
}

int http_parse_url(const char *url, char *out_host, int host_sz, u16 *out_port, char *out_path, int path_sz, int *out_is_ssl) {
    if (!url || !out_host || !out_port || !out_path || !out_is_ssl) return 0;
    *out_is_ssl = (strncmp(url, "https://", 8) == 0);
    *out_port = *out_is_ssl ? 443 : 80;
    strncpy(out_host, "example.com", host_sz - 1);
    out_host[host_sz - 1] = 0;
    strncpy(out_path, "/", path_sz - 1);
    out_path[path_sz - 1] = 0;
    return 1;
}

int strncasecmp(const char *s1, const char *s2, unsigned int n);
int http_decode_chunked(const u8 *src, u32 src_len, u8 *dst, u32 max_dst_len) {
    (void)src; (void)src_len; (void)dst; (void)max_dst_len;
    return 0;
}
int http_is_proxy_configured(void) { return 0; }
const char *http_get_proxy_host(void) { return NULL; }
u16 http_get_proxy_port(void) { return 0; }
void http_set_status_callback(http_status_cb_t cb) { (void)cb; }
http_response_t *http_execute_ex(const http_request_t *req, bnet_error_t *out_err) { (void)req; if (out_err) *out_err = BNET_OK; return NULL; }
void http_response_free(http_response_t *res) { (void)res; }

/* Include browser_core implementation */
#include "browser_core.c"

/* ============================================================
 * Phase 3 Unit Tests
 * ============================================================ */

static void test_async_state_progression(void) {
    printf("[test_browser_async] 1. test_async_state_progression...\n");
    browser_core_init();
    s_mock_socket_connected = 1;
    s_mock_recv_offset = 0;
    s_mock_ticks = 1000;

    int rc = browser_core_start_async("http://example.com/test", 1);
    assert(rc == 1);
    assert(browser_core_is_in_flight() == 1);

    const browser_document_t *doc = browser_core_document();
    assert(doc != NULL);
    assert(doc->state == BROWSER_CORE_NAV_CONNECTING);

    char body[1024];
    browser_document_t out_doc;

    /* Step 1: Connect slice */
    int len = browser_core_step(body, sizeof(body), &out_doc);
    assert(len == 0);
    assert(out_doc.state == BROWSER_CORE_NAV_SENDING);

    /* Step 2: Send request slice */
    len = browser_core_step(body, sizeof(body), &out_doc);
    assert(len == 0);
    assert(out_doc.state == BROWSER_CORE_NAV_RECEIVING);

    /* Step 3+: Stream response slices */
    int steps = 0;
    while (out_doc.state == BROWSER_CORE_NAV_RECEIVING && steps < 50) {
        len = browser_core_step(body, sizeof(body), &out_doc);
        steps++;
    }

    if (out_doc.state != BROWSER_CORE_READY) {
        printf("DEBUG: out_doc.state=%d err=%s len=%d steps=%d\n", out_doc.state, out_doc.error, len, steps);
    }
    assert(out_doc.state == BROWSER_CORE_READY);
    assert(len > 0);
    assert(strstr(body, "<h1>Async OK</h1>") != NULL);
    assert(browser_core_is_in_flight() == 0);
    printf("  -> OK (Connecting -> Sending -> Receiving -> Ready)\n");
}

static void test_async_cancellation_mid_flight(void) {
    printf("[test_browser_async] 2. test_async_cancellation_mid_flight...\n");
    browser_core_init();
    s_mock_socket_connected = 1;
    s_mock_recv_offset = 0;

    browser_core_start_async("http://example.com/long", 2);
    char body[1024];
    browser_document_t out_doc;

    browser_core_step(body, sizeof(body), &out_doc); /* Connect -> Sending */
    assert(browser_core_is_in_flight() == 1);

    /* User clicks Cancel or closes tab */
    browser_core_cancel();
    assert(browser_core_is_in_flight() == 0);
    const browser_document_t *doc = browser_core_document();
    assert(doc->state == BROWSER_CORE_IDLE);

    /* Subsequent step does nothing */
    int len = browser_core_step(body, sizeof(body), &out_doc);
    assert(len == 0);
    assert(out_doc.state == BROWSER_CORE_IDLE);
    printf("  -> OK (Graceful mid-flight cancellation)\n");
}

static void test_async_stale_generation_rejection(void) {
    printf("[test_browser_async] 3. test_async_stale_generation_rejection...\n");
    browser_core_init();
    s_mock_socket_connected = 1;
    s_mock_recv_offset = 0;

    /* Start request with generation 10 */
    browser_core_start_async("http://example.com/page1", 10);
    char body[1024];
    browser_document_t out_doc;

    browser_core_step(body, sizeof(body), &out_doc); /* Connect */
    browser_core_step(body, sizeof(body), &out_doc); /* Send */

    /* User navigates to page2 before page1 finishes (generation bumped to 11) */
    browser_core_start_async("http://example.com/page2", 11);
    assert(browser_core_document()->generation == 11);

    /* Step the new navigation */
    browser_core_step(body, sizeof(body), &out_doc);
    assert(out_doc.generation == 11);
    printf("  -> OK (Stale generation rejected without collision)\n");
}

static void test_async_connection_timeout(void) {
    printf("[test_browser_async] 4. test_async_connection_timeout...\n");
    browser_core_init();
    s_mock_socket_connected = 1;
    s_mock_recv_offset = 0;
    s_mock_ticks = 1000;

    browser_core_start_async("http://example.com/slow", 20);
    char body[1024];
    browser_document_t out_doc;

    browser_core_step(body, sizeof(body), &out_doc); /* Connect */
    browser_core_step(body, sizeof(body), &out_doc); /* Send */

    /* Advance time past deadline (400 ticks) */
    s_mock_ticks = 1500;

    int len = browser_core_step(body, sizeof(body), &out_doc);
    assert(len == 0);
    assert(out_doc.state == BROWSER_CORE_ERROR);
    assert(out_doc.error_code == BNET_ERR_HTTP_RECV_TIMEOUT);
    assert(browser_core_is_in_flight() == 0);
    printf("  -> OK (Timeout deadline enforced cleanly)\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Phase 3: Asynchronous Network Controller Unit Tests\n");
    printf("============================================================\n");

    test_async_state_progression();
    test_async_cancellation_mid_flight();
    test_async_stale_generation_rejection();
    test_async_connection_timeout();

    printf("============================================================\n");
    printf("ALL 4 ASYNC NETWORK CONTROLLER UNIT TESTS PASSED (4/4)\n");
    printf("============================================================\n");
    return 0;
}
