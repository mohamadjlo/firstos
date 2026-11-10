#include "http_body.h"
#include "serial.h"
#include <stdlib.h>
#include <string.h>

void http_body_init(http_body_t *body, unsigned int initial_capacity) {
    if (!body) return;
    memset(body, 0, sizeof(*body));

    if (initial_capacity > 0) {
        body->data = (unsigned char *)malloc(initial_capacity + 1);
        if (body->data) {
            body->capacity = initial_capacity;
            body->data[0] = 0;
        }
    }
}

int http_body_append(http_body_t *body, const void *chunk, unsigned int chunk_len, unsigned int max_capacity) {
    if (!body || !chunk || chunk_len == 0) return 0;

    /* Check if already truncated or if appending would exceed maximum capacity */
    if (body->truncated || (body->length + chunk_len > max_capacity)) {
        body->truncated = 1;
        body->complete = 0;

        /* If there is any remaining room up to max_capacity, clamp copy */
        if (body->length < max_capacity) {
            unsigned int space_left = max_capacity - body->length;
            if (space_left > 0) {
                if (body->length + space_left > body->capacity) {
                    unsigned int new_cap = max_capacity;
                    unsigned char *new_data = (unsigned char *)realloc(body->data, new_cap + 1);
                    if (new_data) {
                        body->data = new_data;
                        body->capacity = new_cap;
                    } else {
                        return 0;
                    }
                }
                memcpy(body->data + body->length, chunk, space_left);
                body->length += space_left;
                body->data[body->length] = 0;
            }
        }
        return 0;
    }

    /* Grow buffer exponentially if needed */
    if (body->length + chunk_len > body->capacity) {
        unsigned int new_cap = (body->capacity > 0) ? body->capacity * 2 : 16384;
        while (new_cap < body->length + chunk_len) {
            new_cap *= 2;
        }
        if (new_cap > max_capacity) {
            new_cap = max_capacity;
        }

        unsigned char *new_data = (unsigned char *)realloc(body->data, new_cap + 1);
        if (!new_data) {
            body->truncated = 1;
            body->complete = 0;
            return 0;
        }
        body->data = new_data;
        body->capacity = new_cap;
    }

    memcpy(body->data + body->length, chunk, chunk_len);
    body->length += chunk_len;
    body->data[body->length] = 0;
    return 1;
}

void http_body_finalize(http_body_t *body, int http_status, const char *content_type, const char *content_encoding, unsigned int expected_len, int has_expected_len) {
    if (!body) return;

    body->http_status = http_status;
    body->has_expected_length = has_expected_len;
    body->expected_length = expected_len;

    if (content_type && content_type[0]) {
        strncpy(body->content_type, content_type, sizeof(body->content_type) - 1);
        body->content_type[sizeof(body->content_type) - 1] = 0;
    } else {
        body->content_type[0] = 0;
    }

    if (content_encoding && content_encoding[0]) {
        strncpy(body->content_encoding, content_encoding, sizeof(body->content_encoding) - 1);
        body->content_encoding[sizeof(body->content_encoding) - 1] = 0;
    } else {
        strncpy(body->content_encoding, "identity", sizeof(body->content_encoding) - 1);
        body->content_encoding[sizeof(body->content_encoding) - 1] = 0;
    }

    if (has_expected_len) {
        if (body->length < expected_len) {
            body->truncated = 1;
            body->complete = 0;
        } else {
            if (!body->truncated) {
                body->complete = 1;
            }
        }
    } else {
        if (!body->truncated && body->http_status >= 200 && body->http_status < 400) {
            body->complete = 1;
        }
    }

    /* Ensure null-termination without changing binary length */
    if (body->data && body->length <= body->capacity) {
        body->data[body->length] = 0;
    }
}

void http_body_free(http_body_t *body) {
    if (!body) return;
    if (body->data) {
        free(body->data);
        body->data = NULL;
    }
    body->length = 0;
    body->capacity = 0;
    body->complete = 0;
    body->truncated = 0;
}

int http_body_check_guard(const http_body_t *body, const char *resource_name) {
    if (!body || !body->complete || body->truncated || !body->data) {
        serial_write("[resource] incomplete body; refusing to parse/execute: ");
        if (resource_name) serial_write(resource_name);
        serial_write("\n");
        return 0;
    }
    return 1;
}

void http_body_log(const char *url, const http_body_t *body, const char *parse_result) {
    serial_write("[resource]\n");
    serial_write("url=");
    serial_write(url ? url : "");
    serial_write("\nstatus=");
    serial_write_hex((u32)(body ? body->http_status : 0));
    serial_write("\ncontent_type=");
    serial_write((body && body->content_type[0]) ? body->content_type : "unknown");
    serial_write("\ncontent_encoding=");
    serial_write((body && body->content_encoding[0]) ? body->content_encoding : "identity");
    serial_write("\nexpected=");
    serial_write_hex((body && body->has_expected_length) ? body->expected_length : 0);
    serial_write("\nreceived=");
    serial_write_hex(body ? body->length : 0);
    serial_write("\ncapacity=");
    serial_write_hex(body ? body->capacity : 0);
    serial_write("\ncomplete=");
    serial_write((body && body->complete) ? "1\n" : "0\n");
    serial_write("truncated=");
    serial_write((body && body->truncated) ? "1\n" : "0\n");
    serial_write("parse_result=");
    serial_write(parse_result ? parse_result : "none");
    serial_write("\n");
}
