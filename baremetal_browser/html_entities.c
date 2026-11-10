#include "html_entities.h"
#include "bb_libc.h"

typedef struct {
    const char *name;
    uint32_t    codepoint;
} named_entity_t;

static const named_entity_t g_named_entities[] = {
    {"amp",    0x0026},
    {"lt",     0x003C},
    {"gt",     0x003E},
    {"quot",   0x0022},
    {"apos",   0x0027},
    {"nbsp",   0x00A0},
    {"bull",   0x2022},
    {"mdash",  0x2014},
    {"ndash",  0x2013},
    {"lsquo",  0x2018},
    {"rsquo",  0x2019},
    {"ldquo",  0x201C},
    {"rdquo",  0x201D},
    {"hellip", 0x2026},
    {"copy",   0x00A9},
    {"reg",    0x00AE},
    {"trade",  0x2122},
    {"cent",   0x00A2},
    {"pound",  0x00A3},
    {"yen",    0x00A5},
    {"euro",   0x20AC},
    {NULL,     0}
};

static size_t encode_utf8(uint32_t cp, char *out, size_t out_max) {
    if (cp <= 0x7F) {
        if (out && out_max >= 1) {
            out[0] = (char)cp;
        }
        return 1;
    } else if (cp <= 0x7FF) {
        if (out && out_max >= 2) {
            out[0] = (char)(0xC0 | (cp >> 6));
            out[1] = (char)(0x80 | (cp & 0x3F));
        }
        return 2;
    } else if (cp <= 0xFFFF) {
        if (out && out_max >= 3) {
            out[0] = (char)(0xE0 | (cp >> 12));
            out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[2] = (char)(0x80 | (cp & 0x3F));
        }
        return 3;
    } else if (cp <= 0x10FFFF) {
        if (out && out_max >= 4) {
            out[0] = (char)(0xF0 | (cp >> 18));
            out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[3] = (char)(0x80 | (cp & 0x3F));
        }
        return 4;
    }
    /* Fallback replacement character U+FFFD */
    if (out && out_max >= 3) {
        out[0] = (char)0xEF;
        out[1] = (char)0xBF;
        out[2] = (char)0xBD;
    }
    return 3;
}

size_t html_entity_decode(const char *src, const char *src_end,
                          char *out, size_t out_max) {
    if (!src || src >= src_end || *src != '&') {
        return 0;
    }

    const char *p = src + 1;

    /* Check for numeric entity &#...; */
    if (p < src_end && *p == '#') {
        p++;
        bool is_hex = false;
        if (p < src_end && (*p == 'x' || *p == 'X')) {
            is_hex = true;
            p++;
        }

        uint32_t val = 0;
        int digits = 0;

        if (is_hex) {
            while (p < src_end && digits < 8) {
                char c = *p;
                int d = -1;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else break;
                val = (val << 4) | (uint32_t)d;
                digits++;
                p++;
            }
        } else {
            while (p < src_end && digits < 8) {
                char c = *p;
                if (c >= '0' && c <= '9') {
                    val = val * 10 + (uint32_t)(c - '0');
                    digits++;
                    p++;
                } else {
                    break;
                }
            }
        }

        if (digits > 0) {
            bool has_semicolon = (p < src_end && *p == ';');
            size_t consumed = (size_t)((has_semicolon ? p + 1 : p) - src);
            encode_utf8(val, out, out_max);
            return consumed;
        }

        /* Invalid numeric entity: fallback to literal '&' */
        if (out && out_max >= 1) out[0] = '&';
        return 1;
    }

    /* Named entity &name; */
    const char *name_start = p;
    while (p < src_end && *p != ';' && *p != ' ' && *p != '\t' && *p != '\n' && *p != '<' && *p != '&') {
        p++;
    }

    if (p < src_end && *p == ';') {
        size_t name_len = (size_t)(p - name_start);
        for (int i = 0; g_named_entities[i].name != NULL; i++) {
            size_t elen = bb_strlen(g_named_entities[i].name);
            if (elen == name_len && bb_strncmp(name_start, g_named_entities[i].name, name_len) == 0) {
                size_t consumed = (size_t)(p + 1 - src);
                encode_utf8(g_named_entities[i].codepoint, out, out_max);
                return consumed;
            }
        }
    }

    /* Unknown entity or no semicolon: emit literal '&' and consume 1 byte */
    if (out && out_max >= 1) {
        out[0] = '&';
    }
    return 1;
}

char *html_entities_decode_alloc(const char *src, size_t src_len) {
    if (!src) return NULL;

    const char *src_end = src + src_len;

    /* Since each entity &...; is at least 3 bytes (like &#1;) and produces 1-4 bytes,
     * allocating (src_len * 2 + 16) is completely safe and fast. */
    size_t alloc_cap = (src_len * 2) + 16;
    char *buf = (char *)bb_malloc(alloc_cap);
    if (!buf) return NULL;

    size_t w = 0;
    const char *p = src;
    while (p < src_end) {
        if (*p == '&') {
            char utf8_bytes[8];
            size_t consumed = html_entity_decode(p, src_end, utf8_bytes, sizeof(utf8_bytes));
            if (consumed > 0) {
                /* Determine length of utf8_bytes */
                /* UTF-8 length from first byte: */
                unsigned char c0 = (unsigned char)utf8_bytes[0];
                size_t ulen = 1;
                if ((c0 & 0xE0) == 0xC0) ulen = 2;
                else if ((c0 & 0xF0) == 0xE0) ulen = 3;
                else if ((c0 & 0xF8) == 0xF0) ulen = 4;

                for (size_t k = 0; k < ulen; k++) {
                    buf[w++] = utf8_bytes[k];
                }
                p += consumed;
                continue;
            }
        }
        buf[w++] = *p++;
    }

    buf[w] = '\0';
    return buf;
}
