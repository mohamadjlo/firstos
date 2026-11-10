/*
 * utf8.c - UTF-8 decoder & glyph mapping (see utf8.h)
 */
#include "utf8.h"

static int utf8_is_continuation(uint8_t c)
{
    return (c & 0xC0u) == 0x80u;
}

uint32_t utf8_decode_next(const char **ptr, const char *end)
{
    const uint8_t *p = (const uint8_t *)*ptr;
    const uint8_t *e = (const uint8_t *)end;
    uint8_t c;

    if (p >= e) {
        return 0; /* precondition violated; callers must guard with p < e */
    }
    c = *p;

    /* 1-byte: ASCII */
    if (c < 0x80u) {
        *ptr += 1;
        return (uint32_t)c;
    }

    /* Stray continuation byte */
    if (utf8_is_continuation(c)) {
        *ptr += 1;
        return UL_REPLACEMENT_CHAR;
    }

    /* 2-byte: C2..DF + 1 continuation.
     * C0/C1 are always overlong encodings. */
    if (c < 0xE0u) {
        if (c < 0xC2u) {
            *ptr += 1;
            return UL_REPLACEMENT_CHAR;
        }
        if (p + 1 >= e || !utf8_is_continuation(p[1])) {
            *ptr += 1; /* truncated prefix: resync on next byte */
            return UL_REPLACEMENT_CHAR;
        }
        *ptr += 2;
        return ((uint32_t)(c & 0x1Fu) << 6) |
               (uint32_t)(p[1] & 0x3Fu);
    }

    /* 3-byte: E0..EF with range-checked second byte.
     * E0: second byte must be A0..BF (block overlongs below U+0800).
     * ED: second byte must be 80..9F (exclude UTF-16 surrogates). */
    if (c < 0xF0u) {
        uint8_t lo = 0x80u, hi = 0xBFu;
        if (c == 0xE0u) lo = 0xA0u;
        else if (c == 0xEDu) hi = 0x9Fu;

        if (p + 1 >= e || p[1] < lo || p[1] > hi) {
            *ptr += 1;
            return UL_REPLACEMENT_CHAR;
        }
        if (p + 2 >= e || !utf8_is_continuation(p[2])) {
            *ptr += 2; /* valid 2-byte prefix consumed; resync */
            return UL_REPLACEMENT_CHAR;
        }
        *ptr += 3;
        return ((uint32_t)(c & 0x0Fu) << 12) |
               ((uint32_t)(p[1] & 0x3Fu) << 6) |
               (uint32_t)(p[2] & 0x3Fu);
    }

    /* 4-byte: F0..F4 with range-checked second byte.
     * F0: second byte 90..BF (no overlongs below U+10000).
     * F4: second byte 80..8F (cap at U+10FFFF).
     * F5..FF are always invalid. */
    if (c <= 0xF4u) {
        uint8_t lo = 0x80u, hi = 0xBFu;
        if (c == 0xF0u) lo = 0x90u;
        else if (c == 0xF4u) hi = 0x8Fu;

        if (p + 1 >= e || p[1] < lo || p[1] > hi) {
            *ptr += 1;
            return UL_REPLACEMENT_CHAR;
        }
        if (p + 2 >= e || !utf8_is_continuation(p[2])) {
            *ptr += 2;
            return UL_REPLACEMENT_CHAR;
        }
        if (p + 3 >= e || !utf8_is_continuation(p[3])) {
            *ptr += 3;
            return UL_REPLACEMENT_CHAR;
        }
        *ptr += 4;
        return ((uint32_t)(c & 0x07u) << 18) |
               ((uint32_t)(p[1] & 0x3Fu) << 12) |
               ((uint32_t)(p[2] & 0x3Fu) << 6) |
               (uint32_t)(p[3] & 0x3Fu);
    }

    /* F5..FF: invalid lead byte */
    *ptr += 1;
    return UL_REPLACEMENT_CHAR;
}

int utf8_glyph_for(uint32_t cp)
{
    if (cp == UL_REPLACEMENT_CHAR)
        return GLYPH_REPLACEMENT;
    if (cp >= 0x20u && cp <= 0x7Eu)
        return GLYPH_ASCII_BASE + (int)(cp - 0x20u);
    if (cp == 0xA0u)
        return GLYPH_ASCII_BASE; /* space glyph, kept non-breakable */
    if (cp >= 0xA1u && cp <= 0xFFu)
        return GLYPH_LATIN1_BASE + (int)(cp - 0xA1u);
    return GLYPH_UNMAPPED;
}

int utf8_is_nbsp(uint32_t cp)
{
    return cp == 0xA0u;
}

int utf8_is_breakable(uint32_t cp)
{
    return cp != 0xA0u;
}

int utf8_glyph_advance(const FBContext *fb, uint32_t cp)
{
    /* Monospace engine: every codepoint (including U+00A0, which is a
     * visible space) occupies one glyph cell. Unsupported codepoints
     * still draw a box glyph and occupy a cell, so measurement matches
     * rasterization and byte-offset accounting stays intact. */
    (void)cp;
    return fb ? fb->glyph_width : 8;
}

int utf8_measure_width(const FBContext *fb, const char *text, int len)
{
    int cells = 0;
    const char *p = text;
    const char *end = text + len;

    if (!fb || !text || len <= 0)
        return 0;
    while (p < end) {
        (void)utf8_decode_next(&p, end); /* decode to stay in sync */
        cells++;
    }
    return cells * fb->glyph_width;
}
