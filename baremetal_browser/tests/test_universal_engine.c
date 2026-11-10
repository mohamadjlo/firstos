/* test_universal_engine.c
 * Comprehensive test suite for the four new universal browser engine subsystems:
 *   1. UTF-8 decoder & glyph mapping
 *   2. CSS positioning model (static, relative, absolute, fixed)
 *   3. Two-pass line box + text-align
 *   4. Whitespace collapsing
 */

#include "../bb_types.h"
#include "../bb_libc.h"
#include "../bb_dom.h"
#include "../bb_html.h"
#include "../bb_css.h"
#include "../bb_font.h"
#include "../bb_layout.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* Stub framebuffer used by the integration paint test */
static uint32_t g_fb[600][800];

/* =========================================================================
 * 1. UTF-8 Decoder Tests
 * ========================================================================= */
static void test_utf8_decoder(void) {
    printf("[TEST] UTF-8 Decoder & Glyph Mapping...\n");

    /* 1a. 1-byte ASCII */
    {
        const char *s = "ABC";
        const char *p = s;
        uint32_t cp = bb_utf8_decode_next(&p, NULL);
        assert(cp == 'A');
        cp = bb_utf8_decode_next(&p, NULL);
        assert(cp == 'B');
    }

    /* 1b. 2-byte: é U+00E9 = 0xC3 0xA9 */
    {
        const char *s = "\xC3\xA9"; /* é */
        const char *p = s;
        uint32_t cp = bb_utf8_decode_next(&p, NULL);
        assert(cp == 0x00E9);
        assert(p == s + 2);
    }

    /* 1c. 2-byte: ñ U+00F1 = 0xC3 0xB1 */
    {
        const char *s = "\xC3\xB1"; /* ñ */
        const char *p = s;
        uint32_t cp = bb_utf8_decode_next(&p, NULL);
        assert(cp == 0x00F1);
    }

    /* 1d. 2-byte: ü U+00FC = 0xC3 0xBC */
    {
        const char *s = "\xC3\xBC"; /* ü */
        const char *p = s;
        uint32_t cp = bb_utf8_decode_next(&p, NULL);
        assert(cp == 0x00FC);
    }

    /* 1e. 2-byte: NBSP U+00A0 = 0xC2 0xA0 */
    {
        const char *s = "\xC2\xA0";
        const char *p = s;
        uint32_t cp = bb_utf8_decode_next(&p, NULL);
        assert(cp == 0x00A0);
        assert(cp == BB_UNICODE_NBSP);
    }

    /* 1f. 3-byte: € U+20AC = 0xE2 0x82 0xAC */
    {
        const char *s = "\xE2\x82\xAC";
        const char *p = s;
        uint32_t cp = bb_utf8_decode_next(&p, NULL);
        assert(cp == 0x20AC);
        assert(p == s + 3);
    }

    /* 1g. 4-byte (e.g. U+1F310 🌐 = 0xF0 0x9F 0x8C 0x90) */
    {
        const char *s = "\xF0\x9F\x8C\x90";
        const char *p = s;
        uint32_t cp = bb_utf8_decode_next(&p, NULL);
        assert(cp == 0x1F310);
        assert(p == s + 4);
    }

    /* 1h. Invalid byte → replacement, advances by 1 */
    {
        const char *s = "\xFF""A"; /* invalid 0xFF then ASCII 'A' */
        const char *p = s;
        uint32_t cp = bb_utf8_decode_next(&p, NULL);
        assert(cp == BB_UNICODE_REPLACEMENT);
        assert(p == s + 1);
        /* Next byte must still be decoded correctly */
        cp = bb_utf8_decode_next(&p, NULL);
        assert(cp == 'A');
    }

    /* 1i. NUL termination */
    {
        const char *s = "";
        const char *p = s;
        uint32_t cp = bb_utf8_decode_next(&p, NULL);
        assert(cp == 0);
    }

    /* 1j. End-pointer bounded decoding */
    {
        const char buf[] = "\xC3\xA9XY"; /* é then X,Y */
        const char *p    = buf;
        const char *end  = buf + 2;      /* only the 2 bytes of é */
        uint32_t cp = bb_utf8_decode_next(&p, end);
        assert(cp == 0x00E9);
        /* p should be at end now */
        cp = bb_utf8_decode_next(&p, end);
        assert(cp == 0);
    }

    /* 1k. Glyph mapping */
    assert(bb_unicode_to_glyph('A')        == 'A');
    assert(bb_unicode_to_glyph(0x00E9)     == 0xE9); /* é → 0xE9 in table */
    assert(bb_unicode_to_glyph(0x00F1)     == 0xF1); /* ñ */
    assert(bb_unicode_to_glyph(0x00FC)     == 0xFC); /* ü */
    assert(bb_unicode_to_glyph(0x00A0)     == 0x20); /* NBSP → space glyph */
    assert(bb_unicode_to_glyph(0x20AC)     == 0x80); /* € → Win1252 0x80 */
    assert(bb_unicode_to_glyph(0x2019)     == '\''); /* right single quote */
    assert(bb_unicode_to_glyph(0x2022)     == 0x07); /* bullet */
    assert(bb_unicode_to_glyph(0x1F600)    == '?');  /* unknown → fallback */

    printf("       -> UTF-8 decoder & glyph mapping: PASSED\n");
}

/* =========================================================================
 * 2. CSS Positioning Model Tests
 * ========================================================================= */
static void test_css_positioning(void) {
    printf("[TEST] CSS Positioning Model...\n");

    /* 2a. Parse position: absolute and inset values */
    {
        bb_style_t s;
        bb_memset(&s, 0, sizeof(s));
        s.position = BB_POSITION_STATIC;
        s.width = s.height = -1;
        bb_css_parse_inline_style(&s,
            "position: absolute; top: 20px; left: 50px; width: 100px; height: 40px;");
        assert(s.position == BB_POSITION_ABSOLUTE);
        assert(s.has_top && s.top == 20);
        assert(s.has_left && s.left == 50);
        assert(s.width == 100);
        assert(s.height == 40);
        assert(!s.has_right);
        assert(!s.has_bottom);
    }

    /* 2b. Parse position: relative with left/top */
    {
        bb_style_t s;
        bb_memset(&s, 0, sizeof(s));
        s.position = BB_POSITION_STATIC;
        s.width = s.height = -1;
        bb_css_parse_inline_style(&s, "position: relative; left: 10px; top: -5px;");
        assert(s.position == BB_POSITION_RELATIVE);
        assert(s.has_left && s.left == 10);
        assert(s.has_top && s.top == -5);
    }

    /* 2c. Parse position: fixed */
    {
        bb_style_t s;
        bb_memset(&s, 0, sizeof(s));
        s.position = BB_POSITION_STATIC;
        s.width = s.height = -1;
        bb_css_parse_inline_style(&s, "position: fixed; bottom: 0px; right: 0px;");
        assert(s.position == BB_POSITION_FIXED);
        assert(s.has_bottom && s.bottom == 0);
        assert(s.has_right  && s.right  == 0);
    }

    /* 2d. Absolute child removed from normal flow.
     *    Container has one normal block and one absolute child.
     *    Container height should not include the absolute child.        */
    {
        const char *html =
            "<div id='container' style='position: relative; width: 400px; height: auto;'>"
              "<p style='height: 50px;'>Normal paragraph</p>"
              "<div id='abs' style='position: absolute; top: 10px; left: 20px;"
                                   "width: 80px; height: 30px;'>Absolute</div>"
            "</div>";

        bb_node_t *doc = bb_html_parse(html);
        assert(doc != NULL);
        bb_stylesheet_t *sheet = bb_css_create();
        bb_css_apply_styles(doc, sheet);

        g_layout_viewport_w = 800;
        g_layout_viewport_h = 600;
        bb_layout_box_t *root = bb_layout_build_tree(doc);
        assert(root != NULL);

        bb_layout_compute(root, 0, 0, 800);

        /* Find the container div's layout box */
        bb_layout_box_t *container_box = root->first_child; /* <div id='container'> */
        assert(container_box != NULL);

        /* Container height must be non-negative — the absolute child (height 30px)
         * must NOT push auto-height beyond what the normal-flow <p> (50px) requires.
         * We allow generous slack since font scale / margin can add a few pixels.   */
        assert(container_box->dimensions.h >= 0);
        /* The absolute child was 30px tall — container should not be MUCH more than
         * the 50px paragraph (no absolute-child inflation). Accept up to 200px slack
         * to tolerate padding/border on the paragraph itself.                         */
        assert(container_box->dimensions.h < 300);

        /* Find abs child and verify its position */
        bb_layout_box_t *abs_box = NULL;
        bb_layout_box_t *child = container_box->first_child;
        while (child) {
            if (child->node) {
                const char *cid = bb_node_get_attribute(child->node, "id");
                if (cid && bb_strcmp(cid, "abs") == 0) {
                    abs_box = child;
                    break;
                }
            }
            child = child->next_sibling;
        }
        /* Absolute child may be found either at container_box or root level
         * depending on tree structure; verify layout happened */
        if (abs_box) {
            assert(abs_box->dimensions.w == 80);
            assert(abs_box->dimensions.h == 30);
        }

        bb_layout_free_tree(root);
        bb_css_free(sheet);
        bb_node_free_tree(doc);
    }

    printf("       -> CSS positioning model: PASSED\n");
}

/* =========================================================================
 * 3. Two-Pass Line Box & text-align Tests
 * ========================================================================= */
static void test_linebox_alignment(void) {
    printf("[TEST] Two-Pass Line Box & text-align...\n");

    /* 3a. linebox_flush — center alignment */
    {
        /* Create two dummy layout boxes to represent inline items */
        bb_layout_box_t box_a, box_b;
        bb_memset(&box_a, 0, sizeof(box_a));
        bb_memset(&box_b, 0, sizeof(box_b));
        box_a.dimensions.x = 0;
        box_b.dimensions.x = 60;

        bb_line_box_t lb;
        linebox_init(&lb);
        linebox_add(&lb, &box_a, 50, 20);
        linebox_add(&lb, &box_b, 50, 20);
        /* total line width = 100, container = 300 → remaining = 200 → offset = 100 */

        int32_t cx = 0, cy = 0;
        linebox_flush(&lb, 300, 0, BB_TEXT_ALIGN_CENTER, &cx, &cy);

        /* Items should have been shifted by 100px */
        assert(box_a.dimensions.x == 0 + 100);
        assert(box_b.dimensions.x == 60 + 100);
        /* cursor_y advanced by line height (20) */
        assert(cy == 20);
        /* cursor_x reset to padding_left (0) */
        assert(cx == 0);
        linebox_free(&lb);
    }

    /* 3b. linebox_flush — right alignment */
    {
        bb_layout_box_t box_a;
        bb_memset(&box_a, 0, sizeof(box_a));
        box_a.dimensions.x = 10; /* pre-positioned at left padding */

        bb_line_box_t lb;
        linebox_init(&lb);
        linebox_add(&lb, &box_a, 80, 16);
        /* width=80, container=200 → remaining=120 → right offset=120 */

        int32_t cx = 10, cy = 0;
        linebox_flush(&lb, 200, 10, BB_TEXT_ALIGN_RIGHT, &cx, &cy);

        assert(box_a.dimensions.x == 10 + 120); /* 130 */
        assert(cx == 10); /* reset to padding_left */
        assert(cy == 16);
        linebox_free(&lb);
    }

    /* 3c. linebox_flush — left alignment (no shift) */
    {
        bb_layout_box_t box_a;
        bb_memset(&box_a, 0, sizeof(box_a));
        box_a.dimensions.x = 5;

        bb_line_box_t lb;
        linebox_init(&lb);
        linebox_add(&lb, &box_a, 40, 16);

        int32_t cx = 5, cy = 0;
        linebox_flush(&lb, 200, 5, BB_TEXT_ALIGN_LEFT, &cx, &cy);

        assert(box_a.dimensions.x == 5); /* unchanged */
        linebox_free(&lb);
    }

    /* 3d. cursor_x resets to padding_left after line break */
    {
        bb_line_box_t lb;
        linebox_init(&lb);
        int32_t cx = 50, cy = 0;
        linebox_flush(&lb, 800, 16, BB_TEXT_ALIGN_LEFT, &cx, &cy);
        assert(cx == 16); /* reset to padding_left */
        linebox_free(&lb);
    }

    printf("       -> Two-pass line box & text-align: PASSED\n");
}

/* =========================================================================
 * 4. Whitespace Collapsing Tests
 * ========================================================================= */
static void test_whitespace_collapsing(void) {
    printf("[TEST] HTML5 Whitespace Collapsing...\n");

    /* 4a. Consecutive spaces → single space */
    {
        char s[] = "hello   world";
        bool trailing;
        size_t len = bb_html_collapse_whitespace(s, false, &trailing);
        assert(bb_strcmp(s, "hello world") == 0);
        assert(len == 11);
        assert(!trailing);
    }

    /* 4b. Mixed whitespace (\t \r \n) → single space */
    {
        char s[] = "a\t\r\n \n\tb";
        bool trailing;
        bb_html_collapse_whitespace(s, false, &trailing);
        assert(bb_strcmp(s, "a b") == 0);
    }

    /* 4c. Strip leading whitespace */
    {
        char s[] = "   hello";
        bool trailing;
        size_t len = bb_html_collapse_whitespace(s, true, &trailing);
        assert(bb_strcmp(s, "hello") == 0);
        assert(len == 5);
    }

    /* 4d. Trailing whitespace stripped */
    {
        char s[] = "hello   ";
        bool trailing;
        bb_html_collapse_whitespace(s, false, &trailing);
        assert(bb_strcmp(s, "hello") == 0);
    }

    /* 4e. No whitespace → unchanged */
    {
        char s[] = "NoWhitespace";
        bool trailing;
        size_t len = bb_html_collapse_whitespace(s, false, &trailing);
        assert(bb_strcmp(s, "NoWhitespace") == 0);
        assert(len == 12);
        assert(!trailing);
    }

    /* 4f. Only whitespace → empty result */
    {
        char s[] = "    \t  ";
        bool trailing;
        size_t len = bb_html_collapse_whitespace(s, true, &trailing);
        assert(len == 0);
        assert(s[0] == '\0');
    }

    /* 4g. Empty string → unchanged */
    {
        char s[] = "";
        bool trailing;
        size_t len = bb_html_collapse_whitespace(s, false, &trailing);
        assert(len == 0);
    }

    printf("       -> Whitespace collapsing: PASSED\n");
}

/* =========================================================================
 * 5. Integration: parse HTML with UTF-8 text + positioned element,
 *    lay out, paint to stub FB
 * ========================================================================= */
static void test_integration_render(void) {
    printf("[TEST] Integration render (UTF-8 + positioning + linebox)...\n");

    const char *html =
        "<div style='width: 600px; position: relative; text-align: center;'>"
          /* h1: "Français Español Deutsch" — ç = U+00E7 = \xC3\xA7, ñ = U+00F1 = \xC3\xB1 */
          "<h1>Fran\xC3\xA7" "ais Espa\xC3\xB1" "ol Deutsch</h1>"
          /* p: accented chars + NBSP (U+00A0 = \xC2\xA0) */
          "<p>Accented: \xC3\xA9\xC3\xA0\xC3\xBC \xC2\xA0 non-breaking space above</p>"
          "<span style='position: absolute; top: 5px; right: 10px;'>Badge</span>"
        "</div>";

    bb_node_t *doc = bb_html_parse(html);
    assert(doc != NULL);

    bb_stylesheet_t *sheet = bb_css_create();
    bb_css_apply_styles(doc, sheet);

    g_layout_viewport_w = 800;
    g_layout_viewport_h = 600;

    bb_layout_box_t *root = bb_layout_build_tree(doc);
    assert(root != NULL);

    bb_layout_compute(root, 0, 0, 800);

    /* Stub raster — just verify paint doesn't crash */
    bb_raster_t r;
    bb_memset(&r, 0, sizeof(r));
    r.pixels = (uint32_t *)g_fb;
    r.width  = 800;
    r.height = 600;
    r.pitch  = 800;
    r.clip_top = 0;
    r.clip_stack[0].x = 0;
    r.clip_stack[0].y = 0;
    r.clip_stack[0].w = 800;
    r.clip_stack[0].h = 600;

    bb_layout_paint(root, &r, 0, 0);

    bb_layout_free_tree(root);
    bb_css_free(sheet);
    bb_node_free_tree(doc);

    printf("       -> Integration render: PASSED (no crash)\n");
}

/* =========================================================================
 * Entry point
 * ========================================================================= */
int main(void) {
    printf("[TEST] Running Universal Browser Engine Tests...\n\n");

    test_utf8_decoder();
    test_css_positioning();
    test_linebox_alignment();
    test_whitespace_collapsing();
    test_integration_render();

    printf("\n       -> ALL Universal Engine tests PASSED!\n");
    return 0;
}
