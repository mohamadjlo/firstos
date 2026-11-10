#ifndef BB_FONT_H
#define BB_FONT_H

#include "bb_types.h"
#include "bb_rasterizer.h"

/* Unicode special codepoints */
#define BB_UNICODE_NBSP        0x00A0u  /* Non-breaking space: advance cursor, no line break */
#define BB_UNICODE_REPLACEMENT 0xFFFDu  /* Replacement character used for invalid UTF-8 */

typedef struct {
    int  scale;       /* 1 = normal (8x16), 2 = 2x (16x32), 3 = 3x (24x48) */
    bool bold;        /* 1px synthetic dilation */
    bool italic;      /* Shear angle */
    bool underline;   /* Baseline bar */
} bb_font_style_t;

/* Default font styles */
extern const bb_font_style_t BB_FONT_STYLE_REGULAR;
extern const bb_font_style_t BB_FONT_STYLE_BOLD;
extern const bb_font_style_t BB_FONT_STYLE_H1;
extern const bb_font_style_t BB_FONT_STYLE_H2;
extern const bb_font_style_t BB_FONT_STYLE_H3;

/* -----------------------------------------------------------------------
 * UTF-8 Stream Decoder (zero-allocation)
 *
 *  Decodes the next Unicode codepoint from the UTF-8 byte stream starting
 *  at *ptr.  *ptr is advanced past the consumed bytes.  When end != NULL
 *  the decoder never reads past it.  Returns 0 at end-of-string.
 *  On invalid byte sequences the ill-formed byte is consumed and
 *  BB_UNICODE_REPLACEMENT (U+FFFD) is returned so that the stream never
 *  stalls and adjacent valid characters are not dropped.
 * --------------------------------------------------------------------- */
uint32_t bb_utf8_decode_next(const char **ptr, const char *end);

/* Map a Unicode codepoint to a glyph index in the internal 256-glyph
 * 8x16 bitmap table.  Standard ASCII 0x20-0x7E is passed through.
 * Latin-1 Supplement 0xA0-0xFF maps to glyph indices 0xA0-0xFF.
 * U+00A0 (NBSP) returns 0x20 (space glyph).
 * Common typographic characters (curly quotes, dashes, bullet) are mapped
 * to nearest ASCII equivalents.
 * All other codepoints return '?' (0x3F).
 */
uint8_t bb_unicode_to_glyph(uint32_t codepoint);

/* --------------------------------------------------------------------- */
void bb_font_init(void);
int  bb_font_get_char_width(char c, const bb_font_style_t *style);
int  bb_font_get_line_height(const bb_font_style_t *style);

/* Measure a NUL-terminated UTF-8 string; interprets multibyte sequences */
void bb_font_measure_string(const char *text, const bb_font_style_t *style, int32_t *out_w, int32_t *out_h);

/* Draw a single glyph (ASCII index expected; for codepoints use draw_string) */
void bb_font_draw_char(bb_raster_t *r, int32_t x, int32_t y, char c, bb_color_t color, const bb_font_style_t *style);

/* Draw a NUL-terminated UTF-8 string; decodes multi-byte sequences and
 * handles U+00A0 as a non-breaking advance without line-break opportunity. */
void bb_font_draw_string(bb_raster_t *r, int32_t x, int32_t y, const char *text, bb_color_t color, const bb_font_style_t *style);

#endif /* BB_FONT_H */
