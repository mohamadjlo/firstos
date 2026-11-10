#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../bnet_error.h"
#include "../browser_core.h"
#include "../http_v11.h"

/* Serial stubs for host test execution */
void serial_write(const char *str) {
    (void)str;
}

void serial_write_hex(u32 val) {
    (void)val;
}

void gui_pump_events(void) {}

static u32 g_mock_ticks = 0;
u32 get_system_ticks(void) {
    g_mock_ticks += 10;
    return g_mock_ticks;
}

/* ========================================================================= */
/* Mock Socket Infrastructure                                               */
/* ========================================================================= */

static bnet_error_t g_mock_open_error = BNET_OK;
static int g_mock_open_should_succeed = 0;
static const char *g_mock_response_data = NULL;
static int g_mock_response_len = 0;
static int g_mock_response_pos = 0;
static int g_mock_send_fail = 0;

static net_socket_t g_dummy_sock;

net_socket_t *net_socket_open_ex(const char *host, u16 port, socket_proto_t proto, u32 timeout_polls, bnet_error_t *out_err) {
    (void)host;
    (void)port;
    (void)proto;
    (void)timeout_polls;

    if (!g_mock_open_should_succeed) {
        if (out_err) *out_err = g_mock_open_error;
        return NULL;
    }
    if (out_err) *out_err = BNET_OK;
    memset(&g_dummy_sock, 0, sizeof(g_dummy_sock));
    g_dummy_sock.is_connected = 1;
    g_mock_response_pos = 0;
    return &g_dummy_sock;
}

net_socket_t *net_socket_open(const char *host, u16 port, socket_proto_t proto, u32 timeout_polls) {
    return net_socket_open_ex(host, port, proto, timeout_polls, NULL);
}

int net_socket_send(net_socket_t *sock, const void *buf, int len) {
    (void)sock;
    (void)buf;
    if (g_mock_send_fail) return -1;
    return len;
}

int net_socket_recv(net_socket_t *sock, void *buf, int max_len, u32 timeout_polls) {
    (void)sock;
    (void)timeout_polls;
    if (!g_mock_response_data || g_mock_response_pos >= g_mock_response_len) {
        return 0; /* EOF */
    }
    int avail = g_mock_response_len - g_mock_response_pos;
    int take = (avail < max_len) ? avail : max_len;
    memcpy(buf, g_mock_response_data + g_mock_response_pos, take);
    g_mock_response_pos += take;
    return take;
}

void net_socket_close(net_socket_t *sock) {
    if (sock) sock->is_connected = 0;
}

/* ========================================================================= */
/* Test Cases                                                                */
/* ========================================================================= */

/* 1. Test bnet_error enum strings and descriptions */
static void test_bnet_error_descriptions(void) {
    printf("[TEST] 1. bnet_error enum strings and descriptions...\n");

    bnet_error_t codes[] = {
        BNET_OK,
        BNET_ERR_BAD_URL,
        BNET_ERR_UNSUPPORTED_SCHEME,
        BNET_ERR_DNS_START_FAILED,
        BNET_ERR_DNS_TIMEOUT,
        BNET_ERR_DNS_NO_RECORD,
        BNET_ERR_TCP_CONNECT_FAILED,
        BNET_ERR_TCP_TIMEOUT,
        BNET_ERR_TCP_RESET,
        BNET_ERR_TLS_NOT_AVAILABLE,
        BNET_ERR_TLS_HANDSHAKE_FAILED,
        BNET_ERR_TLS_TIMEOUT,
        BNET_ERR_HTTP_SEND_FAILED,
        BNET_ERR_HTTP_RECV_TIMEOUT,
        BNET_ERR_HTTP_BAD_RESPONSE,
        BNET_ERR_BODY_TOO_LARGE,
        BNET_ERR_ABORTED,
        BNET_ERR_INTERNAL,
        BNET_ERR_PROXY_REQUIRED,
        BNET_ERR_PROXY_NOT_CONFIGURED,
        BNET_ERR_PROXY_CONNECT_FAILED,
        BNET_ERR_PROXY_TIMEOUT,
        BNET_ERR_PROXY_BAD_RESPONSE,
        BNET_ERR_PROXY_HTTP_ERROR,
        BNET_ERR_HTTPS_BRIDGE_UNAVAILABLE
    };

    int num_codes = sizeof(codes) / sizeof(codes[0]);
    for (int i = 0; i < num_codes; i++) {
        bnet_error_t code = codes[i];
        const char *name = bnet_error_name(code);
        const char *desc = bnet_error_description(code);
        assert(name != NULL && strlen(name) > 0);
        assert(desc != NULL && strlen(desc) > 0);
    }

    assert(strcmp(bnet_error_name(BNET_ERR_DNS_TIMEOUT), "BNET_ERR_DNS_TIMEOUT") == 0);
    assert(strstr(bnet_error_description(BNET_ERR_DNS_TIMEOUT), "timed out") != NULL);

    assert(strcmp(bnet_error_name(BNET_ERR_TCP_TIMEOUT), "BNET_ERR_TCP_TIMEOUT") == 0);
    assert(strstr(bnet_error_description(BNET_ERR_TCP_TIMEOUT), "timed out") != NULL);

    assert(strcmp(bnet_error_name(BNET_ERR_TCP_RESET), "BNET_ERR_TCP_RESET") == 0);
    assert(strstr(bnet_error_description(BNET_ERR_TCP_RESET), "reset") != NULL);

    assert(strcmp(bnet_error_name(BNET_ERR_PROXY_CONNECT_FAILED), "BNET_ERR_PROXY_CONNECT_FAILED") == 0);
    assert(strstr(bnet_error_description(BNET_ERR_PROXY_CONNECT_FAILED), "tools/firstos_webproxy.py") != NULL);

    assert(strcmp(bnet_error_name(BNET_ERR_PROXY_TIMEOUT), "BNET_ERR_PROXY_TIMEOUT") == 0);
    assert(strstr(bnet_error_description(BNET_ERR_PROXY_TIMEOUT), "timed out") != NULL);

    assert(strcmp(bnet_error_name(BNET_ERR_PROXY_NOT_CONFIGURED), "BNET_ERR_PROXY_NOT_CONFIGURED") == 0);
    assert(strstr(bnet_error_description(BNET_ERR_PROXY_NOT_CONFIGURED), "not configured") != NULL);

    assert(strcmp(bnet_error_name(BNET_ERR_PROXY_HTTP_ERROR), "BNET_ERR_PROXY_HTTP_ERROR") == 0);
    assert(strstr(bnet_error_description(BNET_ERR_PROXY_HTTP_ERROR), "upstream fetch failed") != NULL);

    printf("  -> All 25 error codes and human-readable descriptions verified.\n");
}

/* 2. Test URL Parsing and Scheme Validation */
static void test_url_scheme_validation(void) {
    printf("[TEST] 2. URL parsing and unsupported scheme detection...\n");

    char host[128];
    char path[256];
    u16 port = 0;
    int is_ssl = 0;

    int ok = http_parse_url("http://google.com/search?q=os", host, sizeof(host), &port, path, sizeof(path), &is_ssl);
    assert(ok == 1);
    assert(strcmp(host, "google.com") == 0);
    assert(strcmp(path, "/search?q=os") == 0);
    assert(port == 80);
    assert(is_ssl == 0);

    ok = http_parse_url("https://kernel.org:8443/pub", host, sizeof(host), &port, path, sizeof(path), &is_ssl);
    assert(ok == 1);
    assert(strcmp(host, "kernel.org") == 0);
    assert(strcmp(path, "/pub") == 0);
    assert(port == 8443);
    assert(is_ssl == 1);

    bnet_error_t err = BNET_OK;
    http_request_t req;
    memset(&req, 0, sizeof(req));
    req.url = "ftp://ftp.gnu.org/archive";
    http_response_t *res = http_execute_ex(&req, &err);
    assert(res == NULL);
    assert(err == BNET_ERR_UNSUPPORTED_SCHEME);

    printf("  -> URL scheme validation and unsupported scheme detection verified.\n");
}

/* 3. Test Proxy Configuration API */
static void test_proxy_configuration_api(void) {
    printf("[TEST] 3. Proxy configuration API...\n");

    assert(http_is_proxy_configured() == 1);
    assert(strcmp(http_get_proxy_host(), "10.0.2.2") == 0);
    assert(http_get_proxy_port() == 18080);

    http_set_proxy("192.168.1.50", 8888);
    assert(http_is_proxy_configured() == 1);
    assert(strcmp(http_get_proxy_host(), "192.168.1.50") == 0);
    assert(http_get_proxy_port() == 8888);

    http_set_proxy(NULL, 0);
    assert(http_is_proxy_configured() == 0);
    assert(http_get_proxy_host() == NULL);
    assert(http_get_proxy_port() == 0);

    /* Reset to QEMU defaults */
    http_set_proxy("10.0.2.2", 18080);
    assert(http_is_proxy_configured() == 1);

    printf("  -> Dynamic proxy configuration functions verified.\n");
}

/* 4. Test DNS, TCP, and TLS Error Propagation in browser_core_fetch */
static void test_browser_core_error_propagation(void) {
    printf("[TEST] 4. Diagnostic error propagation through browser_core_fetch...\n");

    char body[4096];
    browser_document_t doc;

    /* A) DNS Lookup Timeout */
    g_mock_open_should_succeed = 0;
    g_mock_open_error = BNET_ERR_DNS_TIMEOUT;
    browser_core_init();
    browser_core_begin_navigation("http://unreachable-host.example.com");
    int res = browser_core_fetch(body, sizeof(body), &doc);
    assert(res == 0);
    assert(doc.state == BROWSER_CORE_ERROR);
    assert(doc.error_code == BNET_ERR_DNS_TIMEOUT);
    assert(strcmp(doc.error, bnet_error_description(BNET_ERR_DNS_TIMEOUT)) == 0);

    /* B) TCP Connect Failure (e.g. ARP resolution failure under QEMU) */
    g_mock_open_error = BNET_ERR_TCP_CONNECT_FAILED;
    browser_core_begin_navigation("http://192.168.1.1/index.html");
    res = browser_core_fetch(body, sizeof(body), &doc);
    assert(res == 0);
    assert(doc.state == BROWSER_CORE_ERROR);
    assert(doc.error_code == BNET_ERR_TCP_CONNECT_FAILED);
    assert(strcmp(doc.error, bnet_error_description(BNET_ERR_TCP_CONNECT_FAILED)) == 0);

    /* C) TCP Connection Reset (SYN-SENT received RST) */
    g_mock_open_error = BNET_ERR_TCP_RESET;
    browser_core_begin_navigation("http://10.0.2.2:8080/refused");
    res = browser_core_fetch(body, sizeof(body), &doc);
    assert(res == 0);
    assert(doc.state == BROWSER_CORE_ERROR);
    assert(doc.error_code == BNET_ERR_TCP_RESET);
    assert(strcmp(doc.error, bnet_error_description(BNET_ERR_TCP_RESET)) == 0);

    /* D1) HTTPS with Proxy Bridge Connection Refused */
    g_mock_open_error = BNET_ERR_TCP_RESET;
    browser_core_begin_navigation("https://facebook.com");
    res = browser_core_fetch(body, sizeof(body), &doc);
    assert(res == 0);
    assert(doc.state == BROWSER_CORE_ERROR);
    assert(doc.error_code == BNET_ERR_PROXY_CONNECT_FAILED);
    assert(strstr(doc.error, "tools/firstos_webproxy.py") != NULL);

    /* D2) HTTPS with Proxy Bridge Timeout */
    g_mock_open_error = BNET_ERR_TCP_TIMEOUT;
    browser_core_begin_navigation("https://facebook.com");
    res = browser_core_fetch(body, sizeof(body), &doc);
    assert(res == 0);
    assert(doc.state == BROWSER_CORE_ERROR);
    assert(doc.error_code == BNET_ERR_PROXY_TIMEOUT);
    assert(strstr(doc.error, "timed out") != NULL);

    /* D3) HTTPS with Proxy Not Configured */
    http_set_proxy(NULL, 0);
    browser_core_begin_navigation("https://facebook.com");
    res = browser_core_fetch(body, sizeof(body), &doc);
    assert(res == 0);
    assert(doc.state == BROWSER_CORE_ERROR);
    assert(doc.error_code == BNET_ERR_PROXY_NOT_CONFIGURED);
    assert(strcmp(doc.error, bnet_error_description(BNET_ERR_PROXY_NOT_CONFIGURED)) == 0);
    http_set_proxy("10.0.2.2", 18080); /* restore default */

    /* E) HTTP Send Failure */
    g_mock_open_should_succeed = 1;
    g_mock_send_fail = 1;
    browser_core_begin_navigation("http://10.0.2.2/test");
    res = browser_core_fetch(body, sizeof(body), &doc);
    assert(res == 0);
    assert(doc.state == BROWSER_CORE_ERROR);
    assert(doc.error_code == BNET_ERR_HTTP_SEND_FAILED);
    assert(strcmp(doc.error, bnet_error_description(BNET_ERR_HTTP_SEND_FAILED)) == 0);
    g_mock_send_fail = 0;

    /* F1) Plain HTTP Response Timeout */
    g_mock_response_data = "";
    g_mock_response_len = 0;
    browser_core_begin_navigation("http://10.0.2.2/timeout");
    res = browser_core_fetch(body, sizeof(body), &doc);
    assert(res == 0);
    assert(doc.state == BROWSER_CORE_ERROR);
    assert(doc.error_code == BNET_ERR_HTTP_RECV_TIMEOUT);
    assert(strcmp(doc.error, bnet_error_description(BNET_ERR_HTTP_RECV_TIMEOUT)) == 0);

    /* F2) Plain HTTP Malformed Response (Bad HTTP status line) */
    const char *garbage = "NOT_HTTP/1.1 GARBAGE\r\n\r\n";
    g_mock_response_data = garbage;
    g_mock_response_len = (int)strlen(garbage);
    browser_core_begin_navigation("http://10.0.2.2/bad_response");
    res = browser_core_fetch(body, sizeof(body), &doc);
    assert(res == 0);
    assert(doc.state == BROWSER_CORE_ERROR);
    assert(doc.error_code == BNET_ERR_HTTP_BAD_RESPONSE);
    assert(strcmp(doc.error, bnet_error_description(BNET_ERR_HTTP_BAD_RESPONSE)) == 0);

    /* F3) Proxy Bridge HTTP 502 Error Response (Host fetch failed) */
    const char *proxy_502 =
        "HTTP/1.0 502 Bad Gateway\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Content-Length: 35\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Host fetch failed: Connection reset\n";
    g_mock_response_data = proxy_502;
    g_mock_response_len = (int)strlen(proxy_502);
    browser_core_begin_navigation("https://facebook.com/error502");
    res = browser_core_fetch(body, sizeof(body), &doc);
    assert(res == 0);
    assert(doc.state == BROWSER_CORE_ERROR);
    assert(doc.error_code == BNET_ERR_PROXY_HTTP_ERROR);
    assert(strstr(doc.error, "Proxy bridge (502)") != NULL);
    assert(strstr(doc.error, "Host fetch failed") != NULL);

    /* G) Successful Fetch over Plain HTTP */
    const char *mock_http =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 22\r\n"
        "\r\n"
        "<h1>FirstOS Works</h1>";
    g_mock_response_data = mock_http;
    g_mock_response_len = (int)strlen(mock_http);
    browser_core_begin_navigation("http://10.0.2.2/index.html");
    res = browser_core_fetch(body, sizeof(body), &doc);
    assert(res == 22);
    assert(doc.state == BROWSER_CORE_READY);
    assert(doc.error_code == BNET_OK);
    assert(strcmp(body, "<h1>FirstOS Works</h1>") == 0);

    /* H) Successful Fetch over HTTPS via Host Proxy Bridge */
    const char *mock_proxy_https =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 26\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<h1>HTTPS Proxy Works</h1>";
    g_mock_response_data = mock_proxy_https;
    g_mock_response_len = (int)strlen(mock_proxy_https);
    browser_core_begin_navigation("https://en.wikipedia.org/wiki/Operating_system");
    res = browser_core_fetch(body, sizeof(body), &doc);
    assert(res == 26);
    assert(doc.state == BROWSER_CORE_READY);
    assert(doc.error_code == BNET_OK);
    assert(strcmp(body, "<h1>HTTPS Proxy Works</h1>") == 0);

    printf("  -> DNS, TCP, TLS, and Proxy Bridge error stages correctly mapped.\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Browser Network & Proxy Diagnostics Test Suite\n");
    printf("============================================================\n");

    test_bnet_error_descriptions();
    test_url_scheme_validation();
    test_proxy_configuration_api();
    test_browser_core_error_propagation();

    printf("\nAll browser network and proxy diagnostics tests PASSED successfully!\n");
    return 0;
}
