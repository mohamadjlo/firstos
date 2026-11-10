/*
 * utf8.h - Zero-allocation UTF-8 stream decoder & glyph mapping
 *
 * Decodes multi-byte UTF-8 into Unicode codepoints, maps codepoints onto
 * the 8x16 console font (ASCII + Latin-1 Supplement), and provides
 * breakability/advance information the line-breaking pass needs
 * (including non-breaking space U+00A0).
 *
 * The decoder is resilient: malformed bytes never abort the stream, never
 * consume adjacent valid ASCII, and always make forward progress.
 */
#ifndef UL_UTF8_H
#define UL_UTF8_H

#include "ulayout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Unicode replacement character, returned for malformed input. */
#define UL_REPLACEMENT_CHAR 0xFFFDu

/*
 * Decode the next codepoint from [*ptr, end).
 *
 *  - Advances *ptr past the consumed bytes (1..4 on success).
 *  - On malformed input returns UL_REPLACEMENT_CHAR and advances past
 *    the maximal valid prefix of the bad sequence (always >= 1 byte), so
 *    adjacent valid ASCII is never dropped and the stream always moves.
 *  - Precondition: *ptr < end. Callers loop with that guard.
 */
uint32_t utf8_decode_next(const char **ptr, const char *end);

/* ------------------------------------------------------------------ */
/* Glyph mapping onto the 8x16 font                                    */
/* ------------------------------------------------------------------ */

/*
 * Glyph index space:
 *   0      -  95   : printable ASCII 0x20..0x7E  (glyph = cp - 0x20)
 *   96     - 222   : Latin-1 Supplement 0xA1..0xFF (glyph = 96 + cp - 0xA1)
 *   GLYPH_NBSP     : U+00A0 renders with the ASCII space glyph (index 0)
 *                    but is flagged non-breakable via utf8_is_breakable()
 *   GLYPH_REPLACEMENT : U+FFFD / malformed input
 *   GLYPH_UNMAPPED : any other unsupported higher codepoint (hollow box)
 */
#define GLYPH_ASCII_BASE      0
#define GLYPH_LATIN1_BASE     96
#define GLYPH_COUNT           223   /* 96 ASCII + 127 Latin-1 */
#define GLYPH_REPLACEMENT     223
#define GLYPH_UNMAPPED        224
#define GLYPH_TOTAL           225

/* Map a codepoint to a font glyph index. Never fails: unsupported
 * codepoints yield GLYPH_UNMAPPED (or GLYPH_REPLACEMENT for U+FFFD). */
int utf8_glyph_for(uint32_t cp);

/* U+00A0 NON-BREAKING SPACE test. */
int utf8_is_nbsp(uint32_t cp);

/*
 * May a line break occur immediately before this codepoint?
 * Returns 0 for U+00A0 so the two-pass line breaker keeps the join,
 * non-zero for ordinary spaces and any other codepoint.
 */
int utf8_is_breakable(uint32_t cp);

/* Horizontal advance in px for one codepoint. U+00A0 advances by the
 * glyph width (it is a visible space) but stays non-breakable. */
int utf8_glyph_advance(const FBContext *fb, uint32_t cp);

/*
 * Measure a full text run: number of visible glyph cells * glyph advance.
 * Malformed bytes each count as one replacement glyph, matching what
 * utf8_render would draw, so measurement and rasterization agree.
 */
int utf8_measure_width(const FBContext *fb, const char *text, int len);

#ifdef __cplusplus
}
#endif

#endif /* UL_UTF8_H */
