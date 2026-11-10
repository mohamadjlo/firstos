#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "../bnet_error.h"
#include "../http_body.h"

/* Mock serial logging for host test verification */
static char s_last_serial_log[2048] = {0};
static int s_serial_log_len = 0;

void serial_write(const char *str) {
    if (!str) return;
    int len = (int)strlen(str);
    if (s_serial_log_len + len < (int)sizeof(s_last_serial_log) - 1) {
        memcpy(s_last_serial_log + s_serial_log_len, str, len);
        s_serial_log_len += len;
        s_last_serial_log[s_serial_log_len] = 0;
    }
}

void serial_write_hex(unsigned int val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%08X", val);
    serial_write(buf);
}

static void clear_serial_log(void) {
    s_last_serial_log[0] = 0;
    s_serial_log_len = 0;
}

/* Mock script evaluator tracker */
static int s_eval_called = 0;
static char s_eval_last_code[512] = {0};

void mock_script_eval(const char *code) {
    s_eval_called++;
    if (code) {
        strncpy(s_eval_last_code, code, sizeof(s_eval_last_code) - 1);
        s_eval_last_code[sizeof(s_eval_last_code) - 1] = 0;
    }
}

/* Mock CSS applicator tracker */
static int s_css_applied = 0;

void mock_css_apply(const char *css, int len) {
    (void)css;
    (void)len;
    s_css_applied++;
}

/* ========================================================================= */
/* Test Cases                                                                */
/* ========================================================================= */

static void test_body_dynamic_growth(void) {
    printf("[TEST 1] Testing HttpBody dynamic growth (1 KiB, 64 KiB, 65 KiB, 256 KiB, 1 MiB)...\n");

    const unsigned int sizes[] = {
        1024,                  /* 1 KiB */
        64 * 1024,             /* 64 KiB */
        65 * 1024,             /* 65 KiB (exceeds old 64 KiB cap) */
        256 * 1024,            /* 256 KiB */
        1024 * 1024            /* 1 MiB */
    };

    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        unsigned int target_size = sizes[s];
        http_body_t body;
        http_body_init(&body, 4096);
        assert(body.data != NULL);
        assert(body.length == 0);
        assert(body.capacity >= 4096);

        /* Fill in 4 KiB chunks */
        unsigned char chunk[4096];
        for (int i = 0; i < 4096; i++) chunk[i] = (unsigned char)(i & 0xFF);

        unsigned int written = 0;
        while (written < target_size) {
            unsigned int to_write = target_size - written;
            if (to_write > sizeof(chunk)) to_write = sizeof(chunk);
            int ok = http_body_append(&body, chunk, to_write, HTTP_BODY_MAX_JS);
            assert(ok == 1);
            written += to_write;
        }

        assert(body.length == target_size);
        assert(body.truncated == 0);

        http_body_finalize(&body, 200, "application/javascript", "identity", target_size, 1);
        assert(body.complete == 1);
        assert(body.truncated == 0);
        assert(body.data[body.length] == 0);

        http_body_free(&body);
        assert(body.data == NULL);
        assert(body.length == 0);
    }
    printf("  -> Dynamic growth up to 1 MiB verified successfully.\n");
}

static void test_body_content_length_mismatch(void) {
    printf("[TEST 2] Testing Content-Length mismatch and partial body...\n");

    http_body_t body;
    http_body_init(&body, 1024);

    unsigned char data[500];
    memset(data, 'A', sizeof(data));
    http_body_append(&body, data, sizeof(data), HTTP_BODY_MAX_HTML);

    /* Server declared 1000 bytes, but only 500 arrived (connection closed early) */
    http_body_finalize(&body, 200, "text/html", "identity", 1000, 1);
    assert(body.has_expected_length == 1);
    assert(body.expected_length == 1000);
    assert(body.length == 500);
    assert(body.complete == 0);
    assert(body.truncated == 1);

    clear_serial_log();
    int guard_passed = http_body_check_guard(&body, "test_doc.html");
    assert(guard_passed == 0);
    assert(strstr(s_last_serial_log, "incomplete body; refusing to parse/execute") != NULL);

    http_body_free(&body);
    printf("  -> Incomplete body correctly flagged as truncated and blocked by guard.\n");
}

static void test_body_oversize_clamping(void) {
    printf("[TEST 3] Testing oversize body past capacity limit...\n");

    http_body_t body;
    http_body_init(&body, 256);

    const unsigned int limit = 1000;
    unsigned char chunk[300];
    memset(chunk, 'B', sizeof(chunk));

    /* Append 300, 300, 300 (total 900) - fits */
    assert(http_body_append(&body, chunk, 300, limit) == 1);
    assert(http_body_append(&body, chunk, 300, limit) == 1);
    assert(http_body_append(&body, chunk, 300, limit) == 1);
    assert(body.length == 900);
    assert(body.truncated == 0);

    /* Append another 300 (total 1200 > limit 1000) - should clamp to 1000, set truncated */
    int ok = http_body_append(&body, chunk, 300, limit);
    assert(ok == 0);
    assert(body.length == limit);
    assert(body.truncated == 1);
    assert(body.complete == 0);

    http_body_finalize(&body, 200, "text/css", "identity", 1200, 1);
    assert(body.truncated == 1);
    assert(body.complete == 0);

    clear_serial_log();
    int guard_passed = http_body_check_guard(&body, "style.css");
    assert(guard_passed == 0);

    http_body_free(&body);
    printf("  -> Oversized payload strictly clamped to limit without buffer overrun.\n");
}

static void test_body_binary_with_nulls(void) {
    printf("[TEST 4] Testing binary body with embedded NUL (\\0) bytes...\n");

    http_body_t body;
    http_body_init(&body, 512);

    unsigned char binary_data[] = {
        0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R',
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x08, 0x06, 0x00, 0x00, 0x00
    };
    unsigned int raw_len = (unsigned int)sizeof(binary_data);

    assert(http_body_append(&body, binary_data, raw_len, HTTP_BODY_MAX_IMG) == 1);
    http_body_finalize(&body, 200, "image/png", "identity", raw_len, 1);

    assert(body.length == raw_len);
    assert(body.complete == 1);
    assert(body.truncated == 0);
    assert(memcmp(body.data, binary_data, raw_len) == 0);
    assert(body.data[raw_len] == 0); /* Null sentinel outside binary length */

    http_body_free(&body);
    printf("  -> Binary stream and embedded NULs preserved exactly.\n");
}

static void test_script_truncation_guard_execution(void) {
    printf("[TEST 5] Testing script truncation guard (refusing truncated JS)...\n");

    s_eval_called = 0;
    s_eval_last_code[0] = 0;

    /* Simulate truncated script from server */
    http_body_t script_body;
    http_body_init(&script_body, 1024);

    const char *broken_js = "var x = 10; var d = a(3";
    http_body_append(&script_body, broken_js, (unsigned int)strlen(broken_js), 1024);
    /* Server expected 5000 bytes, only partial arrived */
    http_body_finalize(&script_body, 200, "application/javascript", "identity", 5000, 1);

    clear_serial_log();
    if (!script_body.complete || script_body.truncated) {
        serial_write("[html5_script] skipped incomplete external script\n");
        serial_write("url=https://example.com/bundle.js\n");
    } else {
        mock_script_eval((const char *)script_body.data);
    }

    assert(s_eval_called == 0);
    assert(strstr(s_last_serial_log, "skipped incomplete external script") != NULL);
    assert(strstr(s_last_serial_log, "Executing script block") == NULL);

    http_body_free(&script_body);
    printf("  -> Truncated JS blocked from execution; no partial syntax evaluated.\n");
}

static void test_css_truncation_guard_execution(void) {
    printf("[TEST 6] Testing CSS truncation guard (refusing truncated CSS)...\n");

    s_css_applied = 0;

    http_body_t css_body;
    http_body_init(&css_body, 1024);

    const char *broken_css = "body { color: red; background: ";
    http_body_append(&css_body, broken_css, (unsigned int)strlen(broken_css), 1024);
    http_body_finalize(&css_body, 200, "text/css", "identity", 4000, 1);

    clear_serial_log();
    if (!http_body_check_guard(&css_body, "theme.css")) {
        http_body_log("https://example.com/theme.css", &css_body, "REJECTED_INCOMPLETE");
    } else {
        mock_css_apply((const char *)css_body.data, css_body.length);
    }

    assert(s_css_applied == 0);
    assert(strstr(s_last_serial_log, "incomplete body; refusing to parse/execute") != NULL);
    assert(strstr(s_last_serial_log, "parse_result=REJECTED_INCOMPLETE") != NULL);

    http_body_free(&css_body);
    printf("  -> Truncated CSS rejected before DOM cascade application.\n");
}

static void test_unsupported_content_encoding(void) {
    printf("[TEST 7] Testing unsupported Content-Encoding rejection (gzip, br)...\n");

    http_body_t body;
    http_body_init(&body, 1024);

    const char *gzip_compressed = "\x1F\x8B\x08\x00\x00\x00\x00\x00";
    http_body_append(&body, gzip_compressed, 8, HTTP_BODY_MAX_HTML);
    http_body_finalize(&body, 200, "text/html", "gzip", 8, 1);

    int encoding_supported = (strcasecmp(body.content_encoding, "identity") == 0 || body.content_encoding[0] == 0);
    assert(encoding_supported == 0);

    bnet_error_t err = BNET_ERR_UNSUPPORTED_ENCODING;
    const char *desc = bnet_error_description(err);
    assert(desc != NULL);
    assert(strstr(desc, "compression not supported") != NULL);

    http_body_free(&body);
    printf("  -> Unsupported Content-Encoding (gzip) correctly detected and rejected.\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS HttpBody & Truncation Guard Test Suite (Phase 1)\n");
    printf("============================================================\n");

    test_body_dynamic_growth();
    test_body_content_length_mismatch();
    test_body_oversize_clamping();
    test_body_binary_with_nulls();
    test_script_truncation_guard_execution();
    test_css_truncation_guard_execution();
    test_unsupported_content_encoding();

    printf("\nAll HttpBody and truncation guard tests PASSED successfully!\n");
    return 0;
}
