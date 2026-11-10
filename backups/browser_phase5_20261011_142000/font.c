#include "font.h"

static const u8 glyphs[128][7] = {
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

static const u8 *glyph_for_char(char c) {
    if ((unsigned char)c < 128) {
        if (glyphs[(unsigned char)c][0] || glyphs[(unsigned char)c][1] || glyphs[(unsigned char)c][2] || glyphs[(unsigned char)c][3] || glyphs[(unsigned char)c][4] || glyphs[(unsigned char)c][5] || glyphs[(unsigned char)c][6]) return glyphs[(unsigned char)c];
        if (c >= 'a' && c <= 'z') return glyphs[(unsigned char)(c - 32)];
    }
    return glyphs[(unsigned char)' '];
}

static int glyph_width(const u8 *g) {
    int c;
    for (c = 4; c >= 0; c--) {
        int row;
        for (row = 0; row < 7; row++) {
            if (g[row] & (1 << (4 - c))) return c + 1;
        }
    }
    return 3;
}

void font_text(int x, int y, const char *text, u32 color, int scale) {
    int ox = x;
    int i;
    if (scale < 1) scale = 1;
    for (i = 0; text[i]; i++) {
        char c = text[i];
        const u8 *g;
        int width;
        if (c == '\n') { y += 8 * scale + 3; x = ox; continue; }
        if (c == ' ') { x += 4 * scale; continue; }
        g = glyph_for_char(c);
        width = glyph_width(g);
        {
            int row;
            for (row = 0; row < 7; row++) {
                int col;
                for (col = 0; col < 5; col++) {
                    if (g[row] & (1 << (4 - col))) fb_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += (width + 1) * scale;
    }
}

void font_text_rich(int x, int y, const char *text, u32 color, int scale, int bold, int italic, int underline) {
    int ox = x;
    int i;
    if (scale < 1) scale = 1;
    for (i = 0; text[i]; i++) {
        char c = text[i];
        const u8 *g;
        int width;
        if (c == '\n') { y += 8 * scale + 3; x = ox; continue; }
        if (c == ' ') { x += 4 * scale; continue; }
        g = glyph_for_char(c);
        width = glyph_width(g);
        {
            int row;
            for (row = 0; row < 7; row++) {
                int col;
                int px_offset = 0;
                if (italic) px_offset = (6 - row) / 2;
                for (col = 0; col < 5; col++) {
                    if (g[row] & (1 << (4 - col))) {
                        fb_rect(x + col * scale + px_offset, y + row * scale, scale, scale, color);
                        if (bold) fb_rect(x + col * scale + px_offset + 1, y + row * scale, scale, scale, color);
                    }
                }
            }
            if (underline) {
                fb_rect(x, y + 8 * scale, width * scale + (bold ? 1 : 0), scale, color);
            }
        }
        x += (width + 1) * scale + (bold ? 1 : 0);
    }
}

void font_text_buffer(u32 *buf, int bw, int bh, int x, int y, const char *text, u32 color) {
    if (!buf || bw <= 0 || bh <= 0 || !text) return;
    int ox = x;
    for (int i = 0; text[i]; i++) {
        char c = text[i];
        if (c == '\n') { y += 10; x = ox; continue; }
        if (c == ' ') { x += 5; continue; }
        const u8 *g = glyph_for_char(c);
        int width = glyph_width(g);
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (g[row] & (1 << (4 - col))) {
                    int px = x + col;
                    int py = y + row;
                    if (px >= 0 && px < bw && py >= 0 && py < bh) {
                        buf[py * bw + px] = color | 0xFF000000;
                    }
                }
            }
        }
        x += width + 1;
    }
}

void font_icon(int x, int y, int icon, u32 color) {
    int i;
    if (icon == ICON_POWER) {
        fb_rect(x + 9, y, 3, 10, color); fb_border(x + 4, y + 4, 13, 13, color);
    } else if (icon == ICON_RESTART) {
        fb_border(x + 3, y + 3, 14, 14, color); fb_rect(x + 10, y + 1, 8, 5, color);
    } else if (icon == ICON_SETTINGS) {
        fb_border(x + 4, y + 4, 12, 12, color); fb_rect(x + 8, y, 4, 20, color); fb_rect(x, y + 8, 20, 4, color);
    } else if (icon == ICON_TERMINAL) {
        fb_rect(x + 2, y + 4, 4, 3, color); fb_rect(x + 5, y + 7, 4, 3, color); fb_rect(x + 8, y + 10, 4, 3, color); fb_rect(x + 12, y + 14, 6, 2, color);
    } else if (icon == ICON_FILES || icon == ICON_FOLDER) {
        fb_rect(x + 3, y + 6, 15, 10, color); fb_rect(x + 5, y + 3, 7, 4, color);
    } else if (icon == ICON_DISPLAY) {
        fb_border(x + 2, y + 2, 16, 12, color); fb_rect(x + 7, y + 15, 6, 3, color);
    } else if (icon == ICON_NETWORK) {
        fb_rect(x + 9, y + 2, 3, 16, color); fb_rect(x + 2, y + 8, 17, 3, color); fb_rect(x + 4, y + 4, 4, 4, color); fb_rect(x + 14, y + 4, 4, 4, color);
    } else if (icon == ICON_EXPLORER) {
        fb_border(x + 2, y + 3, 16, 12, color); fb_rect(x + 5, y + 1, 6, 4, color); fb_rect(x + 6, y + 8, 8, 2, color);
    } else if (icon == ICON_FILE) {
        fb_border(x + 5, y + 2, 10, 14, color); fb_rect(x + 11, y + 2, 4, 4, color);
    } else if (icon == ICON_DISK) {
        fb_border(x + 3, y + 4, 14, 10, color); fb_rect(x + 6, y + 7, 8, 2, color); fb_rect(x + 12, y + 11, 2, 2, color);
    } else if (icon == ICON_PHOTO) {
        fb_border(x + 2, y + 3, 16, 13, color);
        fb_rect(x + 5, y + 6, 3, 3, color);
        fb_rect(x + 5, y + 11, 4, 3, color);
        fb_rect(x + 10, y + 9, 6, 5, color);
    } else {

        for (i = 0; i < 3; i++) fb_rect(x + i * 6, y + 6, 4, 4, color);
    }
}
