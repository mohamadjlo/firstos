#ifndef HTML_ENTITIES_H
#define HTML_ENTITIES_H

#include "bb_types.h"

/*
 * Decode one entity starting at *src (must point at '&').
 * Writes decoded UTF-8 bytes into out[0..out_max-1].
 * Returns number of source bytes consumed (including trailing ';' if present).
 * On unrecognised entity, writes '&', returns 1.
 *
 * Named: &amp; &lt; &gt; &quot; &apos; &nbsp; &bull; &mdash; &ndash;
 *        &lsquo; &rsquo; &ldquo; &rdquo; &hellip; &copy; &reg; &trade;
 * Decimal numeric: &#38;  &#160;
 * Hex numeric:     &#x26; &#xA0;
 */
size_t html_entity_decode(const char *src, const char *src_end,
                          char *out, size_t out_max);

/*
 * Decode all entities in src[0..src_len) into a bb_malloc'd NUL-terminated
 * string. Caller must bb_free() the result. Returns NULL on OOM.
 */
char *html_entities_decode_alloc(const char *src, size_t src_len);

#endif /* HTML_ENTITIES_H */
