#include "http_body.h"
#include "serial.h"
#include <stdlib.h>
#include <string.h>

const char *resource_state_name(resource_state_t state) {
    switch (state) {
        case RESOURCE_STATE_IDLE:                 return "IDLE";
        case RESOURCE_STATE_LOADED:               return "LOADED";
        case RESOURCE_STATE_INCOMPLETE:           return "INCOMPLETE";
        case RESOURCE_STATE_TRUNCATED:            return "TRUNCATED";
        case RESOURCE_STATE_HTTP_ERROR:           return "HTTP_ERROR";
        case RESOURCE_STATE_PROXY_ERROR:          return "PROXY_ERROR";
        case RESOURCE_STATE_UNSUPPORTED_ENCODING: return "UNSUPPORTED_ENCODING";
        case RESOURCE_STATE_OOM:                  return "OOM";
        case RESOURCE_STATE_PARSE_ERROR:          return "PARSE_ERROR";
        default:                                  return "UNKNOWN";
    }
}

void http_body_init(http_body_t *body, unsigned int initial_capacity) {
    if (!body) return;
    memset(body, 0, sizeof(*body));
    body->state = RESOURCE_STATE_IDLE;

    if (initial_capacity > 0) {
        body->data = (unsigned char *)malloc(initial_capacity + 1);
        if (body->data) {
            body->capacity = initial_capacity;
            body->data[0] = 0;
        } else {
            body->state = RESOURCE_STATE_OOM;
        }
    }
}

int http_body_append(http_body_t *body, const void *chunk, unsigned int chunk_len, unsigned int max_capacity) {
    if (!body || !chunk || chunk_len == 0) return 0;
    body->max_capacity = max_capacity;

    /* Check if already truncated or if appending would exceed maximum capacity */
    if (body->truncated || (body->length + chunk_len > max_capacity)) {
        body->truncated = 1;
        body->complete = 0;
        body->state = RESOURCE_STATE_TRUNCATED;

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
                        body->state = RESOURCE_STATE_OOM;
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
        unsigned int new_cap = (body->capacity > 0) ? body->capacity * 2 : 4096;
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
            body->state = RESOURCE_STATE_OOM;
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

    /* Check for unsupported encodings */
    if (strstr(body->content_encoding, "gzip") || strstr(body->content_encoding, "deflate") || strstr(body->content_encoding, "br")) {
        body->state = RESOURCE_STATE_UNSUPPORTED_ENCODING;
        body->complete = 0;
        body->truncated = 1;
        return;
    }

    /* Check HTTP status codes */
    if (http_status >= 400) {
        body->state = RESOURCE_STATE_HTTP_ERROR;
        body->complete = 0;
        body->truncated = 1;
        return;
    }
    if (http_status < 200) {
        body->state = RESOURCE_STATE_INCOMPLETE;
        body->complete = 0;
        body->truncated = 1;
        return;
    }

    /* Check body completeness against Content-Length */
    if (has_expected_len) {
        if (body->truncated) {
            body->complete = 0;
            body->state = RESOURCE_STATE_TRUNCATED;
        } else if (body->length < expected_len) {
            body->truncated = 1;
            body->complete = 0;
            body->state = RESOURCE_STATE_INCOMPLETE;
        } else {
            body->complete = (body->length > 0) ? 1 : 0;
            body->state = (body->length > 0) ? RESOURCE_STATE_LOADED : RESOURCE_STATE_INCOMPLETE;
        }
    } else {
        if (body->truncated) {
            body->complete = 0;
            body->state = RESOURCE_STATE_TRUNCATED;
        } else if (body->length > 0) {
            body->complete = 1;
            body->state = RESOURCE_STATE_LOADED;
        } else {
            body->complete = 0;
            body->state = RESOURCE_STATE_INCOMPLETE;
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
    body->max_capacity = 0;
    body->complete = 0;
    body->truncated = 0;
    body->state = RESOURCE_STATE_IDLE;
}

int resource_is_executable(const http_body_t *body) {
    if (!body || !body->data) return 0;
    if (body->state != RESOURCE_STATE_LOADED) return 0;
    if (body->http_status < 200 || body->http_status >= 300) return 0;
    if (!body->complete || body->truncated) return 0;
    if (body->length == 0) return 0;

    /* Verify Content-Type: Must not be HTML, Image, CSS, or JSON */
    if (body->content_type[0]) {
        const char *ct = body->content_type;
        if (strstr(ct, "text/html") || strstr(ct, "text/css") || strstr(ct, "image/")) return 0;
        if (strstr(ct, "javascript") || strstr(ct, "ecmascript") || strstr(ct, "x-javascript") ||
            strstr(ct, "text/plain") || strcmp(ct, "unknown") == 0) {
            /* Valid JS type or raw text/unknown with JS payload */
        } else {
            return 0;
        }
    }

    /* Guard against HTML error pages returned with 200 status */
    if (body->length >= 6) {
        const char *start = (const char *)body->data;
        while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
        if (strncmp(start, "<!doctype", 9) == 0 || strncmp(start, "<!DOCTYPE", 9) == 0 ||
            strncmp(start, "<html", 5) == 0 || strncmp(start, "<HTML", 5) == 0) {
            return 0;
        }
    }

    return 1;
}

int resource_is_css(const http_body_t *body) {
    if (!body || !body->data) return 0;
    if (body->state != RESOURCE_STATE_LOADED) return 0;
    if (body->http_status < 200 || body->http_status >= 300) return 0;
    if (!body->complete || body->truncated) return 0;
    if (body->length == 0) return 0;

    /* Verify Content-Type: Must not be HTML, Image, or JS */
    if (body->content_type[0]) {
        const char *ct = body->content_type;
        if (strstr(ct, "text/html") || strstr(ct, "javascript") || strstr(ct, "image/")) return 0;
        if (strstr(ct, "css") || strstr(ct, "text/plain") || strcmp(ct, "unknown") == 0) {
            /* Valid CSS */
        } else {
            return 0;
        }
    }

    /* Guard against HTML error pages returned with 200 status */
    if (body->length >= 6) {
        const char *start = (const char *)body->data;
        while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
        if (strncmp(start, "<!doctype", 9) == 0 || strncmp(start, "<!DOCTYPE", 9) == 0 ||
            strncmp(start, "<html", 5) == 0 || strncmp(start, "<HTML", 5) == 0) {
            return 0;
        }
    }

    return 1;
}

int http_body_check_guard(const http_body_t *body, const char *resource_name) {
    if (!body || !body->complete || body->truncated || !body->data || body->state != RESOURCE_STATE_LOADED) {
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
    serial_write("\nscheme=");
    serial_write((body && body->scheme[0]) ? body->scheme : "unknown");
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
    serial_write("\nmax_capacity=");
    serial_write_hex(body ? body->max_capacity : 0);
    serial_write("\ncomplete=");
    serial_write((body && body->complete) ? "1\n" : "0\n");
    serial_write("truncated=");
    serial_write((body && body->truncated) ? "1\n" : "0\n");
    serial_write("state=");
    serial_write(resource_state_name(body ? body->state : RESOURCE_STATE_IDLE));
    serial_write("\nparse_result=");
    serial_write(parse_result ? parse_result : "none");
    serial_write("\n");
}
