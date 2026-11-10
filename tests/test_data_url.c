#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "../data_url.h"
#include "../http_body.h"
#include "../bnet_error.h"

/* Mock serial logging for host test */
static char s_serial_buf[4096] = {0};
static int s_serial_len = 0;

void serial_write(const char *str) {
    if (!str) return;
    int len = (int)strlen(str);
    if (s_serial_len + len < (int)sizeof(s_serial_buf) - 1) {
        memcpy(s_serial_buf + s_serial_len, str, len);
        s_serial_len += len;
        s_serial_buf[s_serial_len] = 0;
    }
}

void serial_write_hex(unsigned int val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08X", val);
    serial_write(buf);
}

static void test_scheme_detection(void) {
    printf("[TEST 1] Testing URL scheme detection...\n");

    assert(url_detect_scheme("http://example.com") == URL_SCHEME_HTTP);
    assert(url_detect_scheme("HTTP://EXAMPLE.COM") == URL_SCHEME_HTTP);
    assert(url_detect_scheme("https://secure.site/path") == URL_SCHEME_HTTPS);
    assert(url_detect_scheme("HTTPS://SECURE.SITE") == URL_SCHEME_HTTPS);
    assert(url_detect_scheme("data:text/plain,hello") == URL_SCHEME_DATA);
    assert(url_detect_scheme("DATA:text/css;base64,abc") == URL_SCHEME_DATA);
    assert(url_detect_scheme("blob:http://example.com/uuid") == URL_SCHEME_BLOB);
    assert(url_detect_scheme("file:///etc/hosts") == URL_SCHEME_FILE);
    assert(url_detect_scheme("about:blank") == URL_SCHEME_ABOUT);
    assert(url_detect_scheme("javascript:void(0)") == URL_SCHEME_JAVASCRIPT);
    assert(url_detect_scheme("ftp://ftp.is.co.za") == URL_SCHEME_UNKNOWN);
    assert(url_detect_scheme("unknown_scheme:test") == URL_SCHEME_UNKNOWN);
    assert(url_detect_scheme("") == URL_SCHEME_UNKNOWN);
    assert(url_detect_scheme(NULL) == URL_SCHEME_UNKNOWN);

    assert(url_is_network_scheme(URL_SCHEME_HTTP) == 1);
    assert(url_is_network_scheme(URL_SCHEME_HTTPS) == 1);
    assert(url_is_network_scheme(URL_SCHEME_DATA) == 0);
    assert(url_is_network_scheme(URL_SCHEME_BLOB) == 0);
    assert(url_is_network_scheme(URL_SCHEME_FILE) == 0);
    assert(url_is_network_scheme(URL_SCHEME_ABOUT) == 0);

    assert(strcmp(url_scheme_name(URL_SCHEME_HTTP), "http") == 0);
    assert(strcmp(url_scheme_name(URL_SCHEME_HTTPS), "https") == 0);
    assert(strcmp(url_scheme_name(URL_SCHEME_DATA), "data") == 0);
    assert(strcmp(url_scheme_name(URL_SCHEME_BLOB), "blob") == 0);

    printf("  -> Scheme detection verified for network, data, blob, and local protocols.\n");
}

static void test_base64_decode(void) {
    printf("[TEST 2] Testing Base64 decoder with padding, urlsafe, and whitespace...\n");

    unsigned char out[256];
    int len;

    /* "Hello World" -> "SGVsbG8gV29ybGQ=" */
    len = base64_decode("SGVsbG8gV29ybGQ=", -1, out, sizeof(out));
    assert(len == 11);
    assert(memcmp(out, "Hello World", 11) == 0);

    /* "FirstOS" -> "Rmlyc3RPUw==" (2 padding chars) */
    len = base64_decode("Rmlyc3RPUw==", -1, out, sizeof(out));
    assert(len == 7);
    assert(memcmp(out, "FirstOS", 7) == 0);

    /* "Man" -> "TWFu" (0 padding chars) */
    len = base64_decode("TWFu", -1, out, sizeof(out));
    assert(len == 3);
    assert(memcmp(out, "Man", 3) == 0);

    /* Whitespace embedded inside base64 string */
    len = base64_decode("  TW\nFu  \r\n", -1, out, sizeof(out));
    assert(len == 3);
    assert(memcmp(out, "Man", 3) == 0);

    /* URL-safe characters '-' and '_' */
    /* 0xFB, 0xFF, 0xBF in standard base64 is "+/+/"; in URL-safe it is "-_-_" */
    len = base64_decode("-_-_", -1, out, sizeof(out));
    assert(len == 3);
    assert(out[0] == 0xFB && out[1] == 0xFF && out[2] == 0xBF);

    /* Invalid base64 character */
    len = base64_decode("TW!Fu", -1, out, sizeof(out));
    assert(len == -1);

    printf("  -> Base64 decoder passed all standard, padded, urlsafe, and whitespace tests.\n");
}

static void test_url_percent_decode(void) {
    printf("[TEST 3] Testing URL percent decoding...\n");

    unsigned char out[128];
    int len;

    len = url_percent_decode("Hello%20World%21", -1, out, sizeof(out));
    assert(len == 12);
    out[len] = 0;
    assert(strcmp((char *)out, "Hello World!") == 0);

    len = url_percent_decode("body%7Bcolor%3Ared%3B%7D", -1, out, sizeof(out));
    out[len] = 0;
    assert(strcmp((char *)out, "body{color:red;}") == 0);

    printf("  -> Percent decoding verified.\n");
}

static void test_data_url_text_and_base64(void) {
    printf("[TEST 4] Testing Data URL RFC 2397 plain and base64 decode...\n");

    char mime[64];
    unsigned char buf[512];
    unsigned int len = 0;
    int ret;

    /* 1. Default MIME type: data:,Hello%20World */
    ret = data_url_decode("data:,Hello%20World", mime, sizeof(mime), buf, sizeof(buf), &len);
    assert(ret == DATA_URL_OK);
    assert(strcmp(mime, "text/plain") == 0);
    assert(len == 11);
    assert(strcmp((char *)buf, "Hello World") == 0);

    /* 2. Explicit MIME: data:text/javascript,console.log(%22OK%22) */
    ret = data_url_decode("data:text/javascript,console.log(%22OK%22)", mime, sizeof(mime), buf, sizeof(buf), &len);
    assert(ret == DATA_URL_OK);
    assert(strcmp(mime, "text/javascript") == 0);
    assert(strcmp((char *)buf, "console.log(\"OK\")") == 0);

    /* 3. Base64 JavaScript: data:text/javascript;base64,dmFyIHggPSA0Mjs= */
    ret = data_url_decode("data:text/javascript;base64,dmFyIHggPSA0Mjs=", mime, sizeof(mime), buf, sizeof(buf), &len);
    assert(ret == DATA_URL_OK);
    assert(strcmp(mime, "text/javascript") == 0);
    assert(strcmp((char *)buf, "var x = 42;") == 0);

    /* 4. Base64 CSS: data:text/css;base64,aDEgeyBjb2xvcjogcmVkOyB9 */
    ret = data_url_decode("data:text/css;base64,aDEgeyBjb2xvcjogcmVkOyB9", mime, sizeof(mime), buf, sizeof(buf), &len);
    assert(ret == DATA_URL_OK);
    assert(strcmp(mime, "text/css") == 0);
    assert(strcmp((char *)buf, "h1 { color: red; }") == 0);

    /* 5. With parameters: data:text/css;charset=utf-8;base64,Ym9keSB7fQ== */
    ret = data_url_decode("data:text/css;charset=utf-8;base64,Ym9keSB7fQ==", mime, sizeof(mime), buf, sizeof(buf), &len);
    assert(ret == DATA_URL_OK);
    assert(strcmp(mime, "text/css") == 0);
    assert(strcmp((char *)buf, "body {}") == 0);

    printf("  -> Data URL decoding verified for plain, base64, parameters, and multiple MIME types.\n");
}

static void test_data_url_binary_preservation(void) {
    printf("[TEST 5] Testing binary payload with embedded NUL bytes via Data URL...\n");

    /* Create binary sequence: 0x89, 0x50, 0x4E, 0x47, 0x00, 0x00, 0x00, 0x0D */
    /* Base64 for 89 50 4E 47 00 00 00 0D is "iVBORwAAAA0=" */
    char mime[64];
    unsigned char buf[64];
    unsigned int len = 0;
    int ret = data_url_decode("data:image/png;base64,iVBORwAAAA0=", mime, sizeof(mime), buf, sizeof(buf), &len);

    assert(ret == DATA_URL_OK);
    assert(strcmp(mime, "image/png") == 0);
    assert(len == 8);
    assert(buf[0] == 0x89);
    assert(buf[1] == 0x50);
    assert(buf[2] == 0x4E);
    assert(buf[3] == 0x47);
    assert(buf[4] == 0x00);
    assert(buf[5] == 0x00);
    assert(buf[6] == 0x00);
    assert(buf[7] == 0x0D);

    printf("  -> Binary Data URL with embedded NUL bytes preserved byte-for-byte.\n");
}

static void test_data_url_guards(void) {
    printf("[TEST 6] Testing Data URL integration with resource_is_executable and resource_is_css...\n");

    /* 1. Valid JavaScript Data URL */
    http_body_t js_body;
    http_body_init(&js_body, 256);
    char mime[64];
    unsigned char buf[256];
    unsigned int len = 0;
    int ret = data_url_decode("data:text/javascript;base64,dmFyIHggPSA0Mjs=", mime, sizeof(mime), buf, sizeof(buf), &len);
    assert(ret == DATA_URL_OK);
    http_body_append(&js_body, buf, len, 1024);
    http_body_finalize(&js_body, 200, mime, "identity", len, 1);
    strcpy(js_body.scheme, "data");
    js_body.state = RESOURCE_STATE_LOADED;

    assert(resource_is_executable(&js_body) == 1);
    assert(resource_is_css(&js_body) == 0);
    http_body_free(&js_body);

    /* 2. Valid CSS Data URL */
    http_body_t css_body;
    http_body_init(&css_body, 256);
    ret = data_url_decode("data:text/css;base64,aDEgeyBjb2xvcjogcmVkOyB9", mime, sizeof(mime), buf, sizeof(buf), &len);
    assert(ret == DATA_URL_OK);
    http_body_append(&css_body, buf, len, 1024);
    http_body_finalize(&css_body, 200, mime, "identity", len, 1);
    strcpy(css_body.scheme, "data");
    css_body.state = RESOURCE_STATE_LOADED;

    assert(resource_is_css(&css_body) == 1);
    assert(resource_is_executable(&css_body) == 0);
    http_body_free(&css_body);

    /* 3. Image Data URL */
    http_body_t img_body;
    http_body_init(&img_body, 256);
    ret = data_url_decode("data:image/png;base64,iVBORwAAAA0=", mime, sizeof(mime), buf, sizeof(buf), &len);
    assert(ret == DATA_URL_OK);
    http_body_append(&img_body, buf, len, 1024);
    http_body_finalize(&img_body, 200, mime, "identity", len, 1);
    strcpy(img_body.scheme, "data");
    img_body.state = RESOURCE_STATE_LOADED;

    /* Image must NOT be executable as JS and must NOT be applied as CSS */
    assert(resource_is_executable(&img_body) == 0);
    assert(resource_is_css(&img_body) == 0);
    http_body_free(&img_body);

    printf("  -> Content-Type validation strictly prevents CSS/image execution as JS.\n");
}

static void test_invalid_data_urls(void) {
    printf("[TEST 7] Testing error handling on malformed Data URLs...\n");

    char mime[64];
    unsigned char buf[64];
    unsigned int len = 0;

    /* Missing comma */
    assert(data_url_decode("data:text/plain;base64", mime, sizeof(mime), buf, sizeof(buf), &len) == DATA_URL_ERR_INVALID);
    /* Not data: */
    assert(data_url_decode("http://foo.com", mime, sizeof(mime), buf, sizeof(buf), &len) == DATA_URL_ERR_INVALID);
    /* Bad base64 character */
    assert(data_url_decode("data:text/plain;base64,@@@@", mime, sizeof(mime), buf, sizeof(buf), &len) == DATA_URL_ERR_BAD_BASE64);
    /* NULL pointer */
    assert(data_url_decode(NULL, mime, sizeof(mime), buf, sizeof(buf), &len) == DATA_URL_ERR_INVALID);

    printf("  -> Malformed Data URLs rejected safely without crash or buffer overflow.\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS Data URL & Scheme Detection Test Suite (Phase B)\n");
    printf("============================================================\n");

    test_scheme_detection();
    test_base64_decode();
    test_url_percent_decode();
    test_data_url_text_and_base64();
    test_data_url_binary_preservation();
    test_data_url_guards();
    test_invalid_data_urls();

    printf("\nAll 7 Data URL & Scheme Detection tests PASSED successfully!\n");
    return 0;
}
