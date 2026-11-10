#include "text_engine.h"
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(__KERNEL__)
#include <string.h>
extern void fb_rect(int x, int y, int w, int h, u32 col);
extern void fb_border(int x, int y, int w, int h, u32 col);
#else
#include "framebuffer.h"
#include "font.h"
#include "string.h"
#endif

/* Standard 5x7 ASCII glyphs table from FirstOS font.c */
static const u8 s_ascii_glyphs[128][7] = {
    [' '] = {0,0,0,0,0,0,0},
    ['('] = {8, 16, 16, 16, 16, 16, 8},
    [')'] = {2, 1, 1, 1, 1, 1, 2},
    ['<'] = {2, 4, 8, 16, 8, 4, 2},
    ['\''] = {8, 8, 4, 0, 0, 0, 0},
    ['"'] = {10, 10, 5, 0, 0, 0, 0},
    ['!'] = {4, 4, 4, 4, 4, 0, 4},
    ['@'] = {14, 17, 21, 21, 22, 16, 14},
    ['#'] = {10, 10, 31, 10, 31, 10, 10},
    ['$'] = {4, 15, 20, 14, 5, 30, 4},
    ['%'] = {12, 13, 2, 4, 8, 22, 6},
    ['^'] = {4, 10, 17, 0, 0, 0, 0},
    ['&'] = {8, 20, 8, 21, 18, 13, 0},
    ['*'] = {0, 10, 4, 14, 4, 10, 0},
    ['+'] = {0, 4, 4, 31, 4, 4, 0},
    ['='] = {0, 0, 31, 0, 31, 0, 0},
    ['{'] = {3, 4, 4, 8, 4, 4, 3},
    ['}'] = {24, 4, 4, 2, 4, 4, 24},
    ['['] = {7, 4, 4, 4, 4, 4, 7},
    [']'] = {28, 4, 4, 4, 4, 4, 28},
    ['|'] = {4, 4, 4, 4, 4, 4, 4},
    ['\\'] = {16, 8, 4, 2, 1, 0, 0},
    [';'] = {0, 4, 4, 0, 4, 8, 0},
    [','] = {0, 0, 0, 0, 4, 8, 0},
    ['?'] = {14, 17, 1, 2, 4, 0, 4},
    ['~'] = {10, 21, 0, 0, 0, 0, 0},
    ['`'] = {8, 4, 0, 0, 0, 0, 0},
    ['-'] = {0,0,0,31,0,0,0},
    ['_'] = {0,0,0,0,0,0,31},
    ['/'] = {1,2,4,8,16,0,0},
    ['.'] = {0,0,0,0,0,12,12},
    [':'] = {0,12,12,0,12,12,0},
    ['>'] = {16,8,4,2,4,8,16},
    ['0'] = {14,17,19,21,25,17,14},
    ['1'] = {4,12,4,4,4,4,14},
    ['2'] = {14,17,1,2,4,8,31},
    ['3'] = {30,1,1,14,1,1,30},
    ['4'] = {2,6,10,18,31,2,2},
    ['5'] = {31,16,16,30,1,1,30},
    ['6'] = {14,16,16,30,17,17,14},
    ['7'] = {31,1,2,4,8,8,8},
    ['8'] = {14,17,17,14,17,17,14},
    ['9'] = {14,17,17,15,1,1,1},
    ['A'] = {14,17,17,31,17,17,17},
    ['B'] = {30,17,17,30,17,17,30},
    ['C'] = {14,17,16,16,16,17,14},
    ['D'] = {30,17,17,17,17,17,30},
    ['E'] = {31,16,16,30,16,16,31},
    ['F'] = {31,16,16,30,16,16,16},
    ['G'] = {14,17,16,23,17,17,14},
    ['H'] = {17,17,17,31,17,17,17},
    ['I'] = {14,4,4,4,4,4,14},
    ['J'] = {7,2,2,2,18,18,12},
    ['K'] = {17,18,20,24,20,18,17},
    ['L'] = {16,16,16,16,16,16,31},
    ['M'] = {17,27,21,21,17,17,17},
    ['N'] = {17,25,21,19,17,17,17},
    ['O'] = {14,17,17,17,17,17,14},
    ['P'] = {30,17,17,30,16,16,16},
    ['Q'] = {14,17,17,17,21,18,13},
    ['R'] = {30,17,17,30,20,18,17},
    ['S'] = {15,16,16,14,1,1,30},
    ['T'] = {31,4,4,4,4,4,4},
    ['U'] = {17,17,17,17,17,17,14},
    ['V'] = {17,17,17,17,17,10,4},
    ['W'] = {17,17,17,21,21,27,17},
    ['X'] = {17,17,10,4,10,17,17},
    ['Y'] = {17,17,10,4,4,4,4},
    ['Z'] = {31,1,2,4,8,16,31},
    ['a'] = {0,0,14,1,15,17,15},
    ['b'] = {16,16,30,17,17,17,30},
    ['c'] = {0,0,14,17,16,17,14},
    ['d'] = {1,1,15,17,17,17,15},
    ['e'] = {0,0,14,17,31,16,14},
    ['f'] = {6,8,30,8,8,8,8},
    ['g'] = {0,0,15,17,15,1,30},
    ['h'] = {16,16,30,17,17,17,17},
    ['i'] = {4,0,12,4,4,4,14},
    ['j'] = {2,0,2,2,2,18,12},
    ['k'] = {16,16,18,20,24,20,18},
    ['l'] = {12,4,4,4,4,4,6},
    ['m'] = {0,0,26,21,21,21,21},
    ['n'] = {0,0,30,17,17,17,17},
    ['o'] = {0,0,14,17,17,17,14},
    ['p'] = {0,0,30,17,30,16,16},
    ['q'] = {0,0,15,17,15,1,1},
    ['r'] = {0,0,22,25,16,16,16},
    ['s'] = {0,0,15,16,14,1,30},
    ['t'] = {8,8,30,8,8,9,6},
    ['u'] = {0,0,17,17,17,19,13},
    ['v'] = {0,0,17,17,17,10,4},
    ['w'] = {0,0,17,21,21,21,10},
    ['x'] = {0,0,17,10,4,10,17},
    ['y'] = {0,17,17,15,1,2,12},
    ['z'] = {0,0,31,2,4,8,31}
};

/* ========================================================================= */
/* Bounded UTF-8 Decoder (RFC 3629) with Malformed Recovery                  */
/* ========================================================================= */

u32 text_utf8_decode_next(const char **ptr, const char *end) {
    if (!ptr || !*ptr || *ptr >= end) return 0;

    const unsigned char *p = (const unsigned char *)(*ptr);
    const unsigned char *e = (const unsigned char *)end;
    unsigned char b0 = *p++;

    /* 1-byte ASCII (0x00..0x7F) */
    if (b0 < 0x80) {
        *ptr = (const char *)p;
        return (u32)b0;
    }

    /* 2-byte sequence (0xC2..0xDF) */
    if (b0 >= 0xC2 && b0 <= 0xDF) {
        if (p < e && (p[0] & 0xC0) == 0x80) {
            u32 cp = ((u32)(b0 & 0x1F) << 6) | (u32)(p[0] & 0x3F);
            p += 1;
            *ptr = (const char *)p;
            return cp;
        }
        *ptr = (const char *)p;
        return 0xFFFDu;
    }

    /* 3-byte sequence (0xE0..0xEF) */
    if (b0 >= 0xE0 && b0 <= 0xEF) {
        if ((e - p) >= 2 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
            if ((b0 == 0xE0 && p[0] >= 0xA0) ||
                (b0 >= 0xE1 && b0 <= 0xEC) ||
                (b0 == 0xED && p[0] <= 0x9F) ||
                (b0 >= 0xEE && b0 <= 0xEF)) {
                u32 cp = ((u32)(b0 & 0x0F) << 12) | ((u32)(p[0] & 0x3F) << 6) | (u32)(p[1] & 0x3F);
                p += 2;
                *ptr = (const char *)p;
                return cp;
            }
        }
        *ptr = (const char *)p;
        return 0xFFFDu;
    }

    /* 4-byte sequence (0xF0..0xF4) */
    if (b0 >= 0xF0 && b0 <= 0xF4) {
        if ((e - p) >= 3 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            if ((b0 == 0xF0 && p[0] >= 0x90) ||
                (b0 >= 0xF1 && b0 <= 0xF3) ||
                (b0 == 0xF4 && p[0] <= 0x8F)) {
                u32 cp = ((u32)(b0 & 0x07) << 18) | ((u32)(p[0] & 0x3F) << 12) |
                         ((u32)(p[1] & 0x3F) << 6)  | (u32)(p[2] & 0x3F);
                p += 3;
                *ptr = (const char *)p;
                return cp;
            }
        }
        *ptr = (const char *)p;
        return 0xFFFDu;
    }

    /* Invalid start byte (0x80..0xC1, 0xF5..0xFF) */
    *ptr = (const char *)p;
    return 0xFFFDu;
}

/* ========================================================================= */
/* Unicode Classification                                                    */
/* ========================================================================= */

int text_is_combining_mark(u32 cp) {
    /* Combining Diacritical Marks */
    if (cp >= 0x0300 && cp <= 0x036F) return 1;
    /* Combining Diacritical Marks Extended / Supplement */
    if (cp >= 0x1DC0 && cp <= 0x1DFF) return 1;
    if (cp >= 0x20D0 && cp <= 0x20FF) return 1;
    /* Cyrillic Combining */
    if (cp >= 0x0483 && cp <= 0x0489) return 1;
    /* Hebrew Points */
    if (cp >= 0x0591 && cp <= 0x05BD) return 1;
    if (cp == 0x05BF || (cp >= 0x05C1 && cp <= 0x05C2) || (cp >= 0x05C4 && cp <= 0x05C5) || cp == 0x05C7) return 1;
    /* Arabic Harakat / Tashkeel */
    if (cp >= 0x0610 && cp <= 0x061A) return 1;
    if (cp >= 0x064B && cp <= 0x065F) return 1;
    if (cp == 0x0670) return 1;
    if (cp >= 0x06D6 && cp <= 0x06DC) return 1;
    if (cp >= 0x06DF && cp <= 0x06E4) return 1;
    if (cp >= 0x06E7 && cp <= 0x06E8) return 1;
    if (cp >= 0x06EA && cp <= 0x06ED) return 1;
    /* Variation Selectors */
    if (cp >= 0xFE00 && cp <= 0xFE0F) return 1;
    return 0;
}

int text_is_rtl(u32 cp) {
    if (cp >= 0x0590 && cp <= 0x05FF) return 1; /* Hebrew */
    if (cp >= 0x0600 && cp <= 0x06FF) return 1; /* Arabic */
    if (cp >= 0x0750 && cp <= 0x077F) return 1; /* Arabic Supplement */
    if (cp >= 0x08A0 && cp <= 0x08FF) return 1; /* Arabic Extended */
    if (cp >= 0xFB50 && cp <= 0xFDFF) return 1; /* Arabic Presentation Forms A */
    if (cp >= 0xFE70 && cp <= 0xFEFF) return 1; /* Arabic Presentation Forms B */
    return 0;
}

int text_is_whitespace(u32 cp) {
    return (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f' ||
            cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200B));
}

/* ========================================================================= */
/* Font Providers Implementation                                             */
/* ========================================================================= */

/* 1. Primary Basic Latin (8x16 box with 5x7 glyph) */
static int latin_has_glyph(font_provider_t *p, u32 cp) {
    (void)p;
    return (cp >= 0x20 && cp <= 0x7E);
}

static int latin_get_glyph(font_provider_t *p, u32 cp, font_glyph_t *g) {
    (void)p;
    if (cp < 0x20 || cp > 0x7E) return 0;
    memset(g, 0, sizeof(*g));
    g->codepoint = cp;
    g->advance_x = 8;
    g->advance_y = 16;
    g->bbox_w = 6;
    g->bbox_h = 7;
    g->bitmap = s_ascii_glyphs[cp];
    g->bitmap_pitch = 1;
    return 1;
}

static void latin_render_glyph(font_provider_t *p, const font_glyph_t *g,
                               int x, int y, u32 color, int scale, void *target_fb) {
    (void)p; (void)target_fb;
    if (!g || !g->bitmap) return;
    if (scale < 1) scale = 1;

    for (int row = 0; row < 7; row++) {
        u8 mask = g->bitmap[row];
        for (int col = 0; col < 5; col++) {
            if (mask & (1 << (4 - col))) {
                fb_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

font_provider_t g_font_provider_latin8x16 = {
    .name = "Latin8x16",
    .base_width = 8,
    .base_height = 16,
    .ascent = 12,
    .descent = 4,
    .has_glyph = latin_has_glyph,
    .get_glyph = latin_get_glyph,
    .render_glyph = latin_render_glyph
};

/* 2. Extended Latin / Greek / Cyrillic / Punctuation / CJK Provider */
static int ext_has_glyph(font_provider_t *p, u32 cp) {
    (void)p;
    return ((cp >= 0x00A0 && cp <= 0x04FF) ||
            (cp >= 0x1E00 && cp <= 0x21FF) ||
            (cp >= 0x3000 && cp <= 0x30FF) ||
            (cp >= 0x4E00 && cp <= 0x9FFF) ||
            (cp >= 0xFF00 && cp <= 0xFFEF));
}

static u32 ext_map_latin_ext_a(u32 cp) {
    if (cp >= 0x0100 && cp <= 0x0105) return (cp & 1) ? 'a' : 'A';
    if (cp >= 0x0106 && cp <= 0x010D) return (cp & 1) ? 'c' : 'C';
    if (cp >= 0x010E && cp <= 0x0111) return (cp & 1) ? 'd' : 'D';
    if (cp >= 0x0112 && cp <= 0x011B) return (cp & 1) ? 'e' : 'E';
    if (cp >= 0x011C && cp <= 0x0123) return (cp & 1) ? 'g' : 'G';
    if (cp >= 0x0124 && cp <= 0x0127) return (cp & 1) ? 'h' : 'H';
    if (cp >= 0x0128 && cp <= 0x0131) return (cp & 1) ? 'i' : 'I';
    if (cp >= 0x0134 && cp <= 0x0135) return (cp & 1) ? 'j' : 'J';
    if (cp >= 0x0136 && cp <= 0x0138) return (cp & 1) ? 'k' : 'K';
    if (cp >= 0x0139 && cp <= 0x0142) return (cp & 1) ? 'l' : 'L';
    if (cp >= 0x0143 && cp <= 0x014B) return (cp & 1) ? 'n' : 'N';
    if (cp >= 0x014C && cp <= 0x0153) return (cp & 1) ? 'o' : 'O';
    if (cp >= 0x0154 && cp <= 0x0159) return (cp & 1) ? 'r' : 'R';
    if (cp >= 0x015A && cp <= 0x0161) return (cp & 1) ? 's' : 'S';
    if (cp >= 0x0162 && cp <= 0x0167) return (cp & 1) ? 't' : 'T';
    if (cp >= 0x0168 && cp <= 0x0173) return (cp & 1) ? 'u' : 'U';
    if (cp >= 0x0174 && cp <= 0x0175) return (cp & 1) ? 'w' : 'W';
    if (cp >= 0x0176 && cp <= 0x0178) return (cp == 0x0178) ? 'Y' : ((cp & 1) ? 'y' : 'Y');
    if (cp >= 0x0179 && cp <= 0x017E) return (cp & 1) ? 'z' : 'Z';
    return 'a';
}

static u32 ext_map_cyrillic(u32 cp) {
    static const char s_cyr_upper[32] = {
        'A','B','V','G','D','E','Z','Z','I','J','K','L','M','N','O','P',
        'R','S','T','U','F','H','C','C','S','S','\'','Y','\'','E','U','A'
    };
    static const char s_cyr_lower[32] = {
        'a','b','v','g','d','e','z','z','i','j','k','l','m','n','o','p',
        'r','s','t','u','f','h','c','c','s','s','\'','y','\'','e','u','a'
    };
    if (cp >= 0x0410 && cp <= 0x042F) return (u32)s_cyr_upper[cp - 0x0410];
    if (cp >= 0x0430 && cp <= 0x044F) return (u32)s_cyr_lower[cp - 0x0430];
    if (cp == 0x0401) return 'E';
    if (cp == 0x0451) return 'e';
    if (cp == 0x0404 || cp == 0x0454) return (cp == 0x0404) ? 'E' : 'e';
    if (cp == 0x0406 || cp == 0x0407) return 'I';
    if (cp == 0x0456 || cp == 0x0457) return 'i';
    return 'a';
}

static int ext_get_glyph(font_provider_t *p, u32 cp, font_glyph_t *g) {
    (void)p;
    if (!ext_has_glyph(p, cp)) return 0;
    memset(g, 0, sizeof(*g));
    g->codepoint = cp;
    g->advance_x = 8;
    g->advance_y = 16;
    g->bbox_w = 6;
    g->bbox_h = 7;

    u32 base = '?';
    if (cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200B) || cp == 0x3000) base = ' ';
    else if (cp == 0x00A9) base = 'C';
    else if (cp == 0x00AE) base = 'R';
    else if (cp == 0x00B7 || cp == 0x2022 || cp == 0x2023 || cp == 0x30FB) base = '.';
    else if (cp == 0x00AB || cp == 0x2039 || cp == 0x300A) base = '<';
    else if (cp == 0x00BB || cp == 0x203A || cp == 0x300B) base = '>';
    else if (cp >= 0x00C0 && cp <= 0x00C6) base = 'A';
    else if (cp == 0x00C7) base = 'C';
    else if (cp >= 0x00C8 && cp <= 0x00CB) base = 'E';
    else if (cp >= 0x00CC && cp <= 0x00CF) base = 'I';
    else if (cp == 0x00D0) base = 'D';
    else if (cp == 0x00D1) base = 'N';
    else if ((cp >= 0x00D2 && cp <= 0x00D6) || cp == 0x00D8) base = 'O';
    else if (cp == 0x00D7) base = 'x';
    else if (cp >= 0x00D9 && cp <= 0x00DC) base = 'U';
    else if (cp == 0x00DD) base = 'Y';
    else if (cp == 0x00DF) base = 's';
    else if (cp >= 0x00E0 && cp <= 0x00E6) base = 'a';
    else if (cp == 0x00E7) base = 'c';
    else if (cp >= 0x00E8 && cp <= 0x00EB) base = 'e';
    else if (cp >= 0x00EC && cp <= 0x00EF) base = 'i';
    else if (cp == 0x00F0) base = 'd';
    else if (cp == 0x00F1) base = 'n';
    else if ((cp >= 0x00F2 && cp <= 0x00F6) || cp == 0x00F8) base = 'o';
    else if (cp == 0x00F7) base = '/';
    else if (cp >= 0x00F9 && cp <= 0x00FC) base = 'u';
    else if (cp == 0x00FD || cp == 0x00FF) base = 'y';
    else if (cp >= 0x0100 && cp <= 0x024F) base = ext_map_latin_ext_a(cp);
    else if (cp >= 0x0400 && cp <= 0x04FF) base = ext_map_cyrillic(cp);
    else if (cp >= 0x2010 && cp <= 0x2015) base = '-';
    else if (cp >= 0x2018 && cp <= 0x201B) base = '\'';
    else if (cp >= 0x201C && cp <= 0x201F) base = '"';
    else if (cp == 0x2026) base = '.';
    else if (cp == 0x20AC) base = 'E';
    else if (cp == 0x2122) base = 'T';
    else if (cp >= 0x3040 && cp <= 0x9FFF) base = 'H';

    g->bitmap = s_ascii_glyphs[base < 128 ? base : '?'];
    g->is_placeholder = 0;
    return 1;
}

static void ext_render_glyph(font_provider_t *p, const font_glyph_t *g,
                             int x, int y, u32 color, int scale, void *target_fb) {
    (void)p; (void)target_fb;
    if (!g || !g->bitmap) return;
    if (scale < 1) scale = 1;

    u32 cp = g->codepoint;
    /* CJK & Japanese Hiragana/Katakana Ideograph stroke synthesis */
    if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x4E00 && cp <= 0x9FFF)) {
        fb_rect(x, y, 5 * scale, scale, color);
        fb_rect(x + 2 * scale, y, scale, 7 * scale, color);
        if (cp & 1) fb_rect(x, y + 3 * scale, 5 * scale, scale, color);
        if (cp & 2) fb_rect(x, y + 6 * scale, 5 * scale, scale, color);
        if (cp & 4) fb_rect(x, y + scale, scale, 5 * scale, color);
        if (cp & 8) fb_rect(x + 4 * scale, y + scale, scale, 5 * scale, color);
        return;
    }

    /* Render base character */
    for (int row = 0; row < 7; row++) {
        u8 mask = g->bitmap[row];
        for (int col = 0; col < 5; col++) {
            if (mask & (1 << (4 - col))) {
                fb_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
    /* Render accent marker dot above letter if accented Latin */
    if ((cp >= 0x00C0 && cp <= 0x017F) && cp != 0x00D7 && cp != 0x00F7) {
        fb_rect(x + 2 * scale, y - 2 * scale, scale, scale, color);
    }
}

font_provider_t g_font_provider_extended = {
    .name = "ExtendedLatinGreek",
    .base_width = 8,
    .base_height = 16,
    .ascent = 12,
    .descent = 4,
    .has_glyph = ext_has_glyph,
    .get_glyph = ext_get_glyph,
    .render_glyph = ext_render_glyph
};

/* 3. Arabic / RTL Script Provider */
static int arabic_has_glyph(font_provider_t *p, u32 cp) {
    (void)p;
    return text_is_rtl(cp);
}

static int arabic_get_glyph(font_provider_t *p, u32 cp, font_glyph_t *g) {
    (void)p;
    if (!arabic_has_glyph(p, cp)) return 0;
    memset(g, 0, sizeof(*g));
    g->codepoint = cp;
    g->advance_x = 8;
    g->advance_y = 16;
    g->bbox_w = 7;
    g->bbox_h = 7;
    g->is_placeholder = 1;
    return 1;
}

static void arabic_render_glyph(font_provider_t *p, const font_glyph_t *g,
                                int x, int y, u32 color, int scale, void *target_fb) {
    (void)p; (void)target_fb;
    if (!g) return;
    if (scale < 1) scale = 1;

    u32 cp = g->codepoint;
    int sy = y + 5 * scale;

    /* Continuous horizontal baseline connection for cursive appearance */
    fb_rect(x, sy, 8 * scale, scale, color);

    /* Shape discrimination based on primary Arabic letter classes */
    if (cp == 0x0627 || cp == 0x0622 || cp == 0x0623 || cp == 0x0625) {
        /* Alif: tall vertical stroke */
        fb_rect(x + 4 * scale, y - 2 * scale, scale, 7 * scale, color);
        if (cp == 0x0622 || cp == 0x0623) {
            /* Hamza / Madda above */
            fb_rect(x + 3 * scale, y - 4 * scale, 3 * scale, scale, color);
        } else if (cp == 0x0625) {
            /* Hamza below */
            fb_rect(x + 3 * scale, y + 6 * scale, 2 * scale, scale, color);
        }
    } else if (cp >= 0x0628 && cp <= 0x062A) {
        /* Ba / Ta / Tha: shallow basin with dots */
        fb_rect(x + 1 * scale, sy - 2 * scale, scale, 2 * scale, color);
        fb_rect(x + 6 * scale, sy - 2 * scale, scale, 2 * scale, color);
        if (cp == 0x0628) {
            /* Ba: 1 dot below */
            fb_rect(x + 3 * scale, sy + 2 * scale, scale, scale, color);
        } else if (cp == 0x062A) {
            /* Ta: 2 dots above */
            fb_rect(x + 2 * scale, sy - 4 * scale, scale, scale, color);
            fb_rect(x + 4 * scale, sy - 4 * scale, scale, scale, color);
        }
    } else if (cp == 0x062B) {
        /* Tha: 3 dots above */
        fb_rect(x + 1 * scale, sy - 2 * scale, scale, 2 * scale, color);
        fb_rect(x + 6 * scale, sy - 2 * scale, scale, 2 * scale, color);
        fb_rect(x + 2 * scale, sy - 4 * scale, scale, scale, color);
        fb_rect(x + 4 * scale, sy - 4 * scale, scale, scale, color);
        fb_rect(x + 3 * scale, sy - 5 * scale, scale, scale, color);
    } else if (cp >= 0x062C && cp <= 0x062E) {
        /* Jim / Ha / Kha: horizontal brow and chest curve */
        fb_rect(x + 2 * scale, sy - 3 * scale, 5 * scale, scale, color);
        fb_rect(x + 2 * scale, sy - 2 * scale, scale, 2 * scale, color);
        if (cp == 0x062C) {
            /* Jim: dot below/center */
            fb_rect(x + 4 * scale, sy + 2 * scale, scale, scale, color);
        } else if (cp == 0x062E) {
            /* Kha: dot above */
            fb_rect(x + 4 * scale, sy - 5 * scale, scale, scale, color);
        }
    } else if (cp == 0x062F || cp == 0x0630) {
        /* Dal / Dhal: angled open hook */
        fb_rect(x + 2 * scale, sy - 3 * scale, scale, 3 * scale, color);
        fb_rect(x + 2 * scale, sy, 4 * scale, scale, color);
        if (cp == 0x0630) fb_rect(x + 2 * scale, sy - 5 * scale, scale, scale, color);
    } else if (cp == 0x0631 || cp == 0x0632) {
        /* Ra / Zay: descending crescent hook */
        fb_rect(x + 4 * scale, sy, scale, 2 * scale, color);
        fb_rect(x + 2 * scale, sy + 2 * scale, 2 * scale, scale, color);
        if (cp == 0x0632) fb_rect(x + 4 * scale, sy - 2 * scale, scale, scale, color);
    } else if (cp == 0x0633 || cp == 0x0634) {
        /* Sin / Shin: 3 teeth */
        fb_rect(x + 1 * scale, sy - 2 * scale, scale, 2 * scale, color);
        fb_rect(x + 3 * scale, sy - 2 * scale, scale, 2 * scale, color);
        fb_rect(x + 5 * scale, sy - 2 * scale, scale, 2 * scale, color);
        if (cp == 0x0634) {
            fb_rect(x + 2 * scale, sy - 4 * scale, scale, scale, color);
            fb_rect(x + 4 * scale, sy - 4 * scale, scale, scale, color);
        }
    } else if (cp == 0x0645) {
        /* Meem: small round loop */
        fb_border(x + 2 * scale, sy - 2 * scale, 3 * scale, 3 * scale, color);
        fb_rect(x + 2 * scale, sy, scale, 3 * scale, color);
    } else if (cp == 0x0644) {
        /* Lam: tall vertical riser */
        fb_rect(x + 5 * scale, y - 2 * scale, scale, 7 * scale, color);
    } else if (cp == 0x0648) {
        /* Waw: head circle and tail */
        fb_border(x + 3 * scale, sy - 2 * scale, 3 * scale, 2 * scale, color);
        fb_rect(x + 2 * scale, sy + scale, 2 * scale, scale, color);
    } else if (cp == 0x064A || cp == 0x0649) {
        /* Ya: flat body with 2 dots below */
        fb_rect(x + 1 * scale, sy - scale, scale, scale, color);
        fb_rect(x + 6 * scale, sy - scale, scale, scale, color);
        if (cp == 0x064A) {
            fb_rect(x + 2 * scale, sy + 2 * scale, scale, scale, color);
            fb_rect(x + 4 * scale, sy + 2 * scale, scale, scale, color);
        }
    } else {
        /* General cursive letter token */
        fb_rect(x + 2 * scale, sy - 2 * scale, 3 * scale, 2 * scale, color);
    }
}

font_provider_t g_font_provider_arabic = {
    .name = "ArabicRTL",
    .base_width = 8,
    .base_height = 16,
    .ascent = 12,
    .descent = 4,
    .has_glyph = arabic_has_glyph,
    .get_glyph = arabic_get_glyph,
    .render_glyph = arabic_render_glyph
};

/* 4. Emoji & Symbol Provider */
static int emoji_has_glyph(font_provider_t *p, u32 cp) {
    (void)p;
    return ((cp >= 0x1F300 && cp <= 0x1F9FF) || (cp >= 0x2600 && cp <= 0x27BF));
}

static int emoji_get_glyph(font_provider_t *p, u32 cp, font_glyph_t *g) {
    (void)p;
    if (!emoji_has_glyph(p, cp)) return 0;
    memset(g, 0, sizeof(*g));
    g->codepoint = cp;
    g->advance_x = 12;
    g->advance_y = 16;
    g->bbox_w = 10;
    g->bbox_h = 10;
    g->is_placeholder = 1;
    return 1;
}

static void emoji_render_glyph(font_provider_t *p, const font_glyph_t *g,
                               int x, int y, u32 color, int scale, void *target_fb) {
    (void)p; (void)g; (void)target_fb;
    if (scale < 1) scale = 1;

    /* Render neat emoji badge box with inner icon highlight */
    u32 badge_col = 0xFFD60A; /* Pleasant yellow emoji hue */
    fb_border(x, y + scale, 10 * scale, 10 * scale, badge_col);
    fb_rect(x + 3 * scale, y + 4 * scale, 2 * scale, 2 * scale, color);
    fb_rect(x + 6 * scale, y + 4 * scale, 2 * scale, 2 * scale, color);
    fb_rect(x + 3 * scale, y + 8 * scale, 5 * scale, scale, color);
}

font_provider_t g_font_provider_emoji = {
    .name = "EmojiSymbols",
    .base_width = 12,
    .base_height = 16,
    .ascent = 12,
    .descent = 4,
    .has_glyph = emoji_has_glyph,
    .get_glyph = emoji_get_glyph,
    .render_glyph = emoji_render_glyph
};

/* 5. Universal .notdef Missing Glyph Box Provider */
static int notdef_has_glyph(font_provider_t *p, u32 cp) {
    (void)p; (void)cp;
    return 1; /* Always handles fallback */
}

static int notdef_get_glyph(font_provider_t *p, u32 cp, font_glyph_t *g) {
    (void)p;
    memset(g, 0, sizeof(*g));
    g->codepoint = cp;
    g->advance_x = 8;
    g->advance_y = 16;
    g->bbox_w = 6;
    g->bbox_h = 10;
    g->is_placeholder = 1;
    return 1;
}

static void notdef_render_glyph(font_provider_t *p, const font_glyph_t *g,
                                int x, int y, u32 color, int scale, void *target_fb) {
    (void)p; (void)g; (void)target_fb;
    if (scale < 1) scale = 1;

    /* Render crisp standard .notdef rectangle with diagonal cross */
    fb_border(x, y + scale, 7 * scale, 10 * scale, color);
    fb_rect(x + 3 * scale, y + 5 * scale, scale, scale, color);
}

font_provider_t g_font_provider_notdef = {
    .name = "NotDefBox",
    .base_width = 8,
    .base_height = 16,
    .ascent = 12,
    .descent = 4,
    .has_glyph = notdef_has_glyph,
    .get_glyph = notdef_get_glyph,
    .render_glyph = notdef_render_glyph
};

/* Default Browser Fallback Chain Instance */
font_fallback_chain_t g_browser_default_font_chain;

/* ========================================================================= */
/* Fallback Chain Operations                                                 */
/* ========================================================================= */

void font_fallback_chain_init(font_fallback_chain_t *chain) {
    if (!chain) return;
    memset(chain, 0, sizeof(*chain));
    font_fallback_chain_add(chain, &g_font_provider_latin8x16);
    font_fallback_chain_add(chain, &g_font_provider_extended);
    font_fallback_chain_add(chain, &g_font_provider_arabic);
    font_fallback_chain_add(chain, &g_font_provider_emoji);
    font_fallback_chain_add(chain, &g_font_provider_notdef);
}

int font_fallback_chain_add(font_fallback_chain_t *chain, font_provider_t *provider) {
    if (!chain || !provider || chain->count >= MAX_FONT_PROVIDERS) return 0;
    chain->providers[chain->count++] = provider;
    return 1;
}

font_provider_t *font_fallback_resolve(font_fallback_chain_t *chain, u32 codepoint, font_glyph_t *out_glyph) {
    if (!chain) chain = &g_browser_default_font_chain;

    for (int i = 0; i < chain->count; i++) {
        font_provider_t *p = chain->providers[i];
        if (p && p->has_glyph && p->has_glyph(p, codepoint)) {
            if (out_glyph && p->get_glyph) {
                p->get_glyph(p, codepoint, out_glyph);
            }
            return p;
        }
    }

    /* Fallback directly to .notdef provider */
    if (out_glyph && g_font_provider_notdef.get_glyph) {
        g_font_provider_notdef.get_glyph(&g_font_provider_notdef, codepoint, out_glyph);
    }
    return &g_font_provider_notdef;
}

/* ========================================================================= */
/* Grapheme Cluster Iterator                                                 */
/* ========================================================================= */

void text_cluster_iter_init(text_cluster_iter_t *iter, const char *text, int text_len,
                            font_fallback_chain_t *chain, int font_scale) {
    if (!iter) return;
    iter->text = text ? text : "";
    iter->text_len = (text_len >= 0) ? text_len : (int)strlen(iter->text);
    iter->current_ptr = iter->text;
    iter->end_ptr = iter->text + iter->text_len;
    iter->chain = chain ? chain : &g_browser_default_font_chain;
    iter->font_scale = (font_scale > 0) ? font_scale : 1;
}

int text_cluster_iter_next(text_cluster_iter_t *iter, grapheme_cluster_t *out) {
    if (!iter || !out || iter->current_ptr >= iter->end_ptr) return 0;
    memset(out, 0, sizeof(*out));

    const char *start_pos = iter->current_ptr;
    out->start_byte_offset = (int)(start_pos - iter->text);

    /* Decode base codepoint */
    u32 base = text_utf8_decode_next(&iter->current_ptr, iter->end_ptr);
    if (base == 0) return 0;

    out->base_codepoint = base;
    out->is_whitespace = text_is_whitespace(base);
    out->is_newline = (base == '\n' || base == '\r');
    out->is_rtl = text_is_rtl(base);

    /* Resolve font and metrics for base glyph */
    font_glyph_t g;
    font_provider_t *provider = font_fallback_resolve(iter->chain, base, &g);
    out->is_fallback = g.is_placeholder;

    int sc = (iter->font_scale > 1) ? iter->font_scale : 1;
    int scale_w = (sc == 1) ? 8 : (6 * sc);
    if (provider == &g_font_provider_emoji) scale_w = (sc == 1) ? 12 : (8 * sc);
    if (base == 0x200B || base == 0xFEFF) scale_w = 0;
    out->advance_w = scale_w;

    /* Scan subsequent combining marks attaching to this cluster */
    while (iter->current_ptr < iter->end_ptr) {
        const char *lookahead = iter->current_ptr;
        u32 next_cp = text_utf8_decode_next(&lookahead, iter->end_ptr);
        if (text_is_combining_mark(next_cp)) {
            if (out->mark_count < MAX_CLUSTER_COMBINING_MARKS) {
                out->marks[out->mark_count++] = next_cp;
            }
            iter->current_ptr = lookahead;
        } else {
            break;
        }
    }

    out->end_byte_offset = (int)(iter->current_ptr - iter->text);
    return 1;
}

/* ========================================================================= */
/* Subsystem High-Level API                                                  */
/* ========================================================================= */

void text_engine_init(void) {
    font_fallback_chain_init(&g_browser_default_font_chain);
}

int text_engine_measure(const char *text, font_fallback_chain_t *chain, int scale, int max_w,
                        int *out_ends_with_space, int *out_has_nbsp) {
    if (!text || !text[0]) {
        if (out_ends_with_space) *out_ends_with_space = 0;
        if (out_has_nbsp) *out_has_nbsp = 0;
        return 0;
    }

    text_cluster_iter_t iter;
    text_cluster_iter_init(&iter, text, -1, chain, scale);

    int sc = (scale > 1) ? scale : 1;
    grapheme_cluster_t c;
    int total_w = 0;
    int pending_space = 0;
    int ends_with_space = 0;
    int has_nbsp = 0;
    int at_start = 1;

    while (text_cluster_iter_next(&iter, &c)) {
        if (c.base_codepoint == 0x200B || c.base_codepoint == 0xFEFF) {
            continue;
        }
        if (c.base_codepoint == 0x00A0) {
            has_nbsp = 1;
            if (!at_start) {
                if (max_w > 0 && total_w + c.advance_w > max_w) break;
                total_w += c.advance_w;
            }
            pending_space = 0;
            ends_with_space = 1;
            at_start = 0;
            continue;
        }

        if (c.is_whitespace) {
            pending_space = 1;
            ends_with_space = 1;
            continue;
        }

        if (pending_space && !at_start) {
            int space_w = (sc == 1) ? 8 : (6 * sc);
            if (max_w > 0 && total_w + space_w > max_w) break;
            total_w += space_w;
            pending_space = 0;
        }

        if (max_w > 0 && total_w + c.advance_w > max_w) break;
        total_w += c.advance_w;
        at_start = 0;
        ends_with_space = 0;
    }

    if (out_ends_with_space) *out_ends_with_space = ends_with_space;
    if (out_has_nbsp) *out_has_nbsp = has_nbsp;
    return total_w;
}

void text_engine_render(int x, int y, const char *text, u32 color, int scale,
                        font_fallback_chain_t *chain, void *target_fb) {
    if (!text || !text[0]) return;
    if (scale < 1) scale = 1;

    text_cluster_iter_t iter;
    text_cluster_iter_init(&iter, text, -1, chain, scale);

    grapheme_cluster_t c;
    int cx = x;
    int ox = x;
    int space_w = (scale == 1) ? 8 : (6 * scale);

    while (text_cluster_iter_next(&iter, &c)) {
        if (c.base_codepoint == 0x200B || c.base_codepoint == 0xFEFF) {
            continue;
        }
        if (c.base_codepoint == '\n') {
            y += (scale == 1 ? 16 : (10 * scale + 2));
            cx = ox;
            continue;
        }
        if (c.is_whitespace) {
            cx += space_w;
            continue;
        }

        font_glyph_t glyph;
        font_provider_t *p = font_fallback_resolve(iter.chain, c.base_codepoint, &glyph);
        if (p && p->render_glyph) {
            p->render_glyph(p, &glyph, cx, y, color, scale, target_fb);
        }

        /* Render combining marks on top of base glyph if present */
        if (c.mark_count > 0) {
            for (int m = 0; m < c.mark_count; m++) {
                /* Render subtle accent pip */
                fb_rect(cx + 2 * scale, y - (2 + m * 2) * scale, scale, scale, color);
            }
        }

        cx += c.advance_w;
    }
}

int text_engine_hit_test(const char *text, font_fallback_chain_t *chain, int scale,
                         int click_x, int *out_byte_offset) {
    if (!text) {
        if (out_byte_offset) *out_byte_offset = 0;
        return 0;
    }

    text_cluster_iter_t iter;
    text_cluster_iter_init(&iter, text, -1, chain, scale);

    grapheme_cluster_t c;
    int cur_x = 0;

    while (text_cluster_iter_next(&iter, &c)) {
        int next_x = cur_x + c.advance_w;
        if (click_x <= cur_x + c.advance_w / 2) {
            if (out_byte_offset) *out_byte_offset = c.start_byte_offset;
            return 1;
        } else if (click_x <= next_x) {
            if (out_byte_offset) *out_byte_offset = c.end_byte_offset;
            return 1;
        }
        cur_x = next_x;
    }

    if (out_byte_offset) *out_byte_offset = (int)strlen(text);
    return 1;
}
