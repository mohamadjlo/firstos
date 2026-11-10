#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../text_engine.h"

/* Dummy framebuffer callbacks for standalone test */
void fb_rect(int x, int y, int w, int h, u32 col) { (void)x; (void)y; (void)w; (void)h; (void)col; }
void fb_border(int x, int y, int w, int h, u32 col) { (void)x; (void)y; (void)w; (void)h; (void)col; }

/* ========================================================================= */
/* Test Cases                                                                */
/* ========================================================================= */

/* 1. Mixed Latin + Arabic Placeholder Fallback */
static void test_mixed_latin_arabic_fallback(void) {
    printf("[TEST] 1. Mixed Latin + Arabic placeholder fallback...\n");

    /* "Hello مرحبا" */
    const char *text = "Hello \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";
    text_cluster_iter_t iter;
    text_cluster_iter_init(&iter, text, -1, &g_browser_default_font_chain, 1);

    grapheme_cluster_t c;
    /* 'H' */
    assert(text_cluster_iter_next(&iter, &c));
    assert(c.base_codepoint == 'H');
    assert(c.is_rtl == 0);
    assert(c.is_fallback == 0);
    assert(c.start_byte_offset == 0 && c.end_byte_offset == 1);

    /* 'e', 'l', 'l', 'o', ' ' */
    assert(text_cluster_iter_next(&iter, &c)); assert(c.base_codepoint == 'e');
    assert(text_cluster_iter_next(&iter, &c)); assert(c.base_codepoint == 'l');
    assert(text_cluster_iter_next(&iter, &c)); assert(c.base_codepoint == 'l');
    assert(text_cluster_iter_next(&iter, &c)); assert(c.base_codepoint == 'o');
    assert(text_cluster_iter_next(&iter, &c)); assert(c.base_codepoint == ' ');
    assert(c.is_whitespace == 1);

    /* First Arabic character: Meem (U+0645) */
    assert(text_cluster_iter_next(&iter, &c));
    assert(c.base_codepoint == 0x0645);
    assert(c.is_rtl == 1);
    assert(c.is_fallback == 1); /* Arabic script placeholder indicator */
    assert(c.advance_w == 8);
    assert(c.start_byte_offset == 6 && c.end_byte_offset == 8);

    /* Second Arabic character: Reh (U+0631) */
    assert(text_cluster_iter_next(&iter, &c));
    assert(c.base_codepoint == 0x0631);
    assert(c.is_rtl == 1);
    assert(c.start_byte_offset == 8 && c.end_byte_offset == 10);

    /* Third Arabic character: Hah (U+062D) */
    assert(text_cluster_iter_next(&iter, &c));
    assert(c.base_codepoint == 0x062D);
    assert(c.is_rtl == 1);

    /* Fourth Arabic character: Beh (U+0628) */
    assert(text_cluster_iter_next(&iter, &c));
    assert(c.base_codepoint == 0x0628);
    assert(c.is_rtl == 1);

    /* Fifth Arabic character: Alef (U+0627) */
    assert(text_cluster_iter_next(&iter, &c));
    assert(c.base_codepoint == 0x0627);
    assert(c.is_rtl == 1);

    /* End of text */
    assert(!text_cluster_iter_next(&iter, &c));

    /* Measurement check */
    int w = text_engine_measure(text, &g_browser_default_font_chain, 1, 0, NULL, NULL);
    assert(w == (6 + 5) * 8); /* 6 Latin/space chars + 5 Arabic chars = 11 * 8 = 88px */

    printf("  -> Mixed Latin + Arabic: PASSED\n");
}

/* 2. Emoji & Missing Glyph Fallback Behavior */
static void test_emoji_and_missing_glyph_fallback(void) {
    printf("[TEST] 2. Emoji & missing glyph fallback behavior...\n");

    /* U+1F600 😀: \xF0\x9F\x98\x80, U+E000 (Private Use): \xEE\x80\x80 */
    const char *text = "\xF0\x9F\x98\x80 \xEE\x80\x80";
    text_cluster_iter_t iter;
    text_cluster_iter_init(&iter, text, -1, &g_browser_default_font_chain, 1);

    grapheme_cluster_t c;

    /* 1. Emoji Grinning Face U+1F600 */
    assert(text_cluster_iter_next(&iter, &c));
    assert(c.base_codepoint == 0x1F600);
    assert(c.advance_w == 12); /* Emoji provider advance */
    assert(c.is_fallback == 1);
    assert(c.start_byte_offset == 0 && c.end_byte_offset == 4);

    font_glyph_t g;
    font_provider_t *provider = font_fallback_resolve(&g_browser_default_font_chain, c.base_codepoint, &g);
    assert(provider == &g_font_provider_emoji);
    assert(strcmp(provider->name, "EmojiSymbols") == 0);

    /* 2. Space */
    assert(text_cluster_iter_next(&iter, &c));
    assert(c.base_codepoint == ' ');

    /* 3. Unassigned Private Use U+E000 */
    assert(text_cluster_iter_next(&iter, &c));
    assert(c.base_codepoint == 0xE000);
    assert(c.is_fallback == 1);
    provider = font_fallback_resolve(&g_browser_default_font_chain, c.base_codepoint, &g);
    assert(provider == &g_font_provider_notdef);
    assert(strcmp(provider->name, "NotDefBox") == 0);
    assert(c.advance_w == 8);

    assert(!text_cluster_iter_next(&iter, &c));

    printf("  -> Emoji & Missing Glyph (.notdef): PASSED\n");
}

/* 3. Combining Mark Grouping Groundwork */
static void test_combining_mark_grouping(void) {
    printf("[TEST] 3. Combining mark grouping groundwork...\n");

    /* "e\u0301" = 'e' (0x65) followed by Combining Acute Accent (0xCC 0x81) */
    const char *text = "e\xCC\x81";
    text_cluster_iter_t iter;
    text_cluster_iter_init(&iter, text, -1, &g_browser_default_font_chain, 1);

    grapheme_cluster_t c;
    assert(text_cluster_iter_next(&iter, &c));

    assert(c.base_codepoint == 'e');
    assert(c.mark_count == 1);
    assert(c.marks[0] == 0x0301);
    assert(c.start_byte_offset == 0);
    assert(c.end_byte_offset == 3); /* 1 byte ('e') + 2 bytes (0xCC 0x81) */
    assert(c.advance_w == 8);        /* Advance width remains base character width */

    /* Must terminate without producing a second standalone cluster for the combining mark */
    assert(!text_cluster_iter_next(&iter, &c));

    /* Test multiple combining marks: 'a' + acute (0x0301) + diaeresis (0x0308) */
    const char *multi_text = "a\xCC\x81\xCC\x88";
    text_cluster_iter_init(&iter, multi_text, -1, &g_browser_default_font_chain, 1);
    assert(text_cluster_iter_next(&iter, &c));
    assert(c.base_codepoint == 'a');
    assert(c.mark_count == 2);
    assert(c.marks[0] == 0x0301);
    assert(c.marks[1] == 0x0308);
    assert(c.start_byte_offset == 0 && c.end_byte_offset == 5);
    assert(c.advance_w == 8);
    assert(!text_cluster_iter_next(&iter, &c));

    printf("  -> Combining Mark Grouping: PASSED\n");
}

/* 4. Malformed UTF-8 Recovery Preservation */
static void test_malformed_utf8_recovery(void) {
    printf("[TEST] 4. Malformed UTF-8 recovery preservation...\n");

    /* Test stream with:
     * - 'A' (valid ASCII)
     * - 0xFF (invalid start byte)
     * - 0x80 (stray continuation byte)
     * - 'B' (valid ASCII)
     * - 0xC3 0x28 (2-byte header followed by invalid non-continuation byte '(')
     * - 'C' (valid ASCII)
     * - 0xC0 0x80 (overlong NULL)
     * - 'D' (valid ASCII)
     */
    const char malformed[] = {'A', (char)0xFF, (char)0x80, 'B', (char)0xC3, '(', 'C', (char)0xC0, (char)0x80, 'D', 0};
    const char *p = malformed;
    const char *end = malformed + sizeof(malformed) - 1;

    /* 'A' */
    u32 cp = text_utf8_decode_next(&p, end);
    assert(cp == 'A');

    /* 0xFF -> 0xFFFD, advances 1 byte */
    cp = text_utf8_decode_next(&p, end);
    assert(cp == 0xFFFD);

    /* 0x80 -> 0xFFFD, advances 1 byte */
    cp = text_utf8_decode_next(&p, end);
    assert(cp == 0xFFFD);

    /* 'B' */
    cp = text_utf8_decode_next(&p, end);
    assert(cp == 'B');

    /* 0xC3 followed by '(' -> 0xFFFD, advances 1 byte */
    cp = text_utf8_decode_next(&p, end);
    assert(cp == 0xFFFD);

    /* '(' */
    cp = text_utf8_decode_next(&p, end);
    assert(cp == '(');

    /* 'C' */
    cp = text_utf8_decode_next(&p, end);
    assert(cp == 'C');

    /* 0xC0 0x80 overlong -> 0xFFFD */
    cp = text_utf8_decode_next(&p, end);
    assert(cp == 0xFFFD);

    /* 0x80 stray continuation */
    cp = text_utf8_decode_next(&p, end);
    assert(cp == 0xFFFD);

    /* 'D' */
    cp = text_utf8_decode_next(&p, end);
    assert(cp == 'D');

    assert(p == end);

    printf("  -> Malformed UTF-8 Recovery: PASSED\n");
}

/* 5. Caret Source Mapping Hit-Testing */
static void test_caret_source_mapping_hit_test(void) {
    printf("[TEST] 5. Caret source mapping hit-testing...\n");

    /* "e\u0301B" -> cluster 1 (bytes 0..3), cluster 2 (bytes 3..4) */
    const char *text = "e\xCC\x81" "B";
    int offset = 0;

    /* Click near start of cluster 1 */
    text_engine_hit_test(text, &g_browser_default_font_chain, 1, 2, &offset);
    assert(offset == 0);

    /* Click near end of cluster 1 */
    text_engine_hit_test(text, &g_browser_default_font_chain, 1, 6, &offset);
    assert(offset == 3);

    /* Click near start of cluster 2 ('B') */
    text_engine_hit_test(text, &g_browser_default_font_chain, 1, 10, &offset);
    assert(offset == 3);

    /* Click near end of cluster 2 ('B') */
    text_engine_hit_test(text, &g_browser_default_font_chain, 1, 14, &offset);
    assert(offset == 4);

    printf("  -> Caret Source Mapping Hit-Testing: PASSED\n");
}

int main(void) {
    printf("=====================================================\n");
    printf("     FirstOS Multilingual Text Subsystem Tests       \n");
    printf("=====================================================\n");

    text_engine_init();

    test_mixed_latin_arabic_fallback();
    test_emoji_and_missing_glyph_fallback();
    test_combining_mark_grouping();
    test_malformed_utf8_recovery();
    test_caret_source_mapping_hit_test();

    printf("\nAll 5 test suites (28 assertions) passed successfully!\n");
    return 0;
}
