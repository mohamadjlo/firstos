#include "../bb_types.h"
#include "../bb_libc.h"
#include "../bb_dom.h"
#include "../bb_html.h"
#include "../html_entities.h"
#include "../html_tokenizer.h"
#include "../html_parser.h"
#include "../bb_css.h"
#include "../bb_layout.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* =========================================================================
 * 1. Tokenizer Unit Tests
 * ========================================================================= */
static void test_tokenizer(void) {
    printf("[TEST] Running HTML Tokenizer Tests...\n");

    /* 1.1 Simple start tag */
    {
        html_tokenizer_t tok;
        html_token_t t;
        const char *src = "<div>";
        html_tok_init(&tok, src, strlen(src));
        assert(html_tok_next(&tok, &t) == true);
        assert(t.type == HTML_TOK_START_TAG);
        assert(t.tag_len == 3);
        assert(strncmp(t.tag, "div", 3) == 0);
        assert(html_tok_next(&tok, &t) == false);
    }

    /* 1.2 End tag */
    {
        html_tokenizer_t tok;
        html_token_t t;
        const char *src = "</div>";
        html_tok_init(&tok, src, strlen(src));
        assert(html_tok_next(&tok, &t) == true);
        assert(t.type == HTML_TOK_END_TAG);
        assert(t.tag_len == 3);
        assert(strncmp(t.tag, "div", 3) == 0);
    }

    /* 1.3 Self-closing / void tags */
    {
        html_tokenizer_t tok;
        html_token_t t;
        const char *src = "<br/><br /><br>";
        html_tok_init(&tok, src, strlen(src));
        
        assert(html_tok_next(&tok, &t) == true);
        assert(t.type == HTML_TOK_SELF_CLOSING);
        assert(strncmp(t.tag, "br", 2) == 0);

        assert(html_tok_next(&tok, &t) == true);
        assert(t.type == HTML_TOK_SELF_CLOSING);
        assert(strncmp(t.tag, "br", 2) == 0);

        assert(html_tok_next(&tok, &t) == true);
        assert(t.type == HTML_TOK_SELF_CLOSING); /* Void tag */
        assert(strncmp(t.tag, "br", 2) == 0);
    }

    /* 1.4 Quoted attribute */
    {
        html_tokenizer_t tok;
        html_token_t t;
        const char *src = "<a href=\"https://example.com/test\">";
        html_tok_init(&tok, src, strlen(src));
        assert(html_tok_next(&tok, &t) == true);
        assert(t.type == HTML_TOK_START_TAG);
        assert(t.attr_count == 1);
        assert(t.attrs[0].key_len == 4);
        assert(strncmp(t.attrs[0].key, "href", 4) == 0);
        assert(t.attrs[0].val_len == strlen("https://example.com/test"));
        assert(strncmp(t.attrs[0].val, "https://example.com/test", t.attrs[0].val_len) == 0);
    }

    /* 1.5 Single-quoted attribute */
    {
        html_tokenizer_t tok;
        html_token_t t;
        const char *src = "<span class='btn-primary'>";
        html_tok_init(&tok, src, strlen(src));
        assert(html_tok_next(&tok, &t) == true);
        assert(t.attr_count == 1);
        assert(strncmp(t.attrs[0].key, "class", 5) == 0);
        assert(strncmp(t.attrs[0].val, "btn-primary", 11) == 0);
    }

    /* 1.6 Unquoted attribute */
    {
        html_tokenizer_t tok;
        html_token_t t;
        const char *src = "<input type=text>";
        html_tok_init(&tok, src, strlen(src));
        assert(html_tok_next(&tok, &t) == true);
        assert(t.attr_count == 1);
        assert(strncmp(t.attrs[0].key, "type", 4) == 0);
        assert(strncmp(t.attrs[0].val, "text", 4) == 0);
    }

    /* 1.7 Boolean attribute */
    {
        html_tokenizer_t tok;
        html_token_t t;
        const char *src = "<input disabled required>";
        html_tok_init(&tok, src, strlen(src));
        assert(html_tok_next(&tok, &t) == true);
        assert(t.attr_count == 2);
        assert(strncmp(t.attrs[0].key, "disabled", 8) == 0);
        assert(t.attrs[0].has_val == false);
        assert(strncmp(t.attrs[1].key, "required", 8) == 0);
        assert(t.attrs[1].has_val == false);
    }

    /* 1.8 Comment token */
    {
        html_tokenizer_t tok;
        html_token_t t;
        const char *src = "<!-- hello world -->";
        html_tok_init(&tok, src, strlen(src));
        assert(html_tok_next(&tok, &t) == true);
        assert(t.type == HTML_TOK_COMMENT);
        assert(t.text_len == 13);
        assert(strncmp(t.text, " hello world ", 13) == 0);
    }

    /* 1.9 DOCTYPE token */
    {
        html_tokenizer_t tok;
        html_token_t t;
        const char *src = "<!DOCTYPE html>";
        html_tok_init(&tok, src, strlen(src));
        assert(html_tok_next(&tok, &t) == true);
        assert(t.type == HTML_TOK_DOCTYPE);
    }

    /* 1.10 Nested tags sequence */
    {
        html_tokenizer_t tok;
        html_token_t t;
        const char *src = "<p>Hi <b>there</b></p>";
        html_tok_init(&tok, src, strlen(src));
        
        assert(html_tok_next(&tok, &t) && t.type == HTML_TOK_START_TAG && strncmp(t.tag, "p", 1) == 0);
        assert(html_tok_next(&tok, &t) && t.type == HTML_TOK_TEXT && strncmp(t.text, "Hi ", 3) == 0);
        assert(html_tok_next(&tok, &t) && t.type == HTML_TOK_START_TAG && strncmp(t.tag, "b", 1) == 0);
        assert(html_tok_next(&tok, &t) && t.type == HTML_TOK_TEXT && strncmp(t.text, "there", 5) == 0);
        assert(html_tok_next(&tok, &t) && t.type == HTML_TOK_END_TAG && strncmp(t.tag, "b", 1) == 0);
        assert(html_tok_next(&tok, &t) && t.type == HTML_TOK_END_TAG && strncmp(t.tag, "p", 1) == 0);
        assert(!html_tok_next(&tok, &t));
    }

    /* 1.11 Malformed unclosed tag at EOF */
    {
        html_tokenizer_t tok;
        html_token_t t;
        const char *src = "<div class=\"foo\"";
        html_tok_init(&tok, src, strlen(src));
        assert(html_tok_next(&tok, &t) == true);
        assert(t.type == HTML_TOK_START_TAG);
        assert(strncmp(t.tag, "div", 3) == 0);
        assert(html_tok_next(&tok, &t) == false);
    }

    /* 1.12 Lone '<' alone */
    {
        html_tokenizer_t tok;
        html_token_t t;
        const char *src = "<";
        html_tok_init(&tok, src, strlen(src));
        assert(html_tok_next(&tok, &t) == true);
        assert(t.type == HTML_TOK_TEXT);
        assert(t.text_len == 1 && t.text[0] == '<');
        assert(html_tok_next(&tok, &t) == false);
    }

    printf("  -> Tokenizer Tests PASSED!\n");
}

/* =========================================================================
 * 2. Entity Decoder Unit Tests
 * ========================================================================= */
static void test_entities(void) {
    printf("[TEST] Running HTML Entity Decoder Tests...\n");

    /* 2.1 Standard named entities */
    {
        char out[16];
        const char *s = "&amp;";
        size_t c = html_entity_decode(s, s + strlen(s), out, sizeof(out));
        assert(c == 5);
        assert(out[0] == '&');

        s = "&lt;";
        c = html_entity_decode(s, s + strlen(s), out, sizeof(out));
        assert(c == 4 && out[0] == '<');

        s = "&gt;";
        c = html_entity_decode(s, s + strlen(s), out, sizeof(out));
        assert(c == 4 && out[0] == '>');

        s = "&quot;";
        c = html_entity_decode(s, s + strlen(s), out, sizeof(out));
        assert(c == 6 && out[0] == '"');

        s = "&apos;";
        c = html_entity_decode(s, s + strlen(s), out, sizeof(out));
        assert(c == 6 && out[0] == '\'');
    }

    /* 2.2 &nbsp; -> UTF-8 U+00A0 (0xC2 0xA0) */
    {
        char out[16];
        const char *s = "&nbsp;";
        size_t c = html_entity_decode(s, s + strlen(s), out, sizeof(out));
        assert(c == 6);
        assert((unsigned char)out[0] == 0xC2 && (unsigned char)out[1] == 0xA0);
    }

    /* 2.3 Decimal numeric entity &#38; -> & */
    {
        char out[16];
        const char *s = "&#38;";
        size_t c = html_entity_decode(s, s + strlen(s), out, sizeof(out));
        assert(c == 5 && out[0] == '&');
    }

    /* 2.4 Hex numeric entity &#x26; -> & */
    {
        char out[16];
        const char *s = "&#x26;";
        size_t c = html_entity_decode(s, s + strlen(s), out, sizeof(out));
        assert(c == 6 && out[0] == '&');
    }

    /* 2.5 Hex numeric entity &#xA0; -> 0xC2 0xA0 */
    {
        char out[16];
        const char *s = "&#xA0;";
        size_t c = html_entity_decode(s, s + strlen(s), out, sizeof(out));
        assert(c == 6);
        assert((unsigned char)out[0] == 0xC2 && (unsigned char)out[1] == 0xA0);
    }

    /* 2.6 Unknown entity &foo; -> literal '&', consumed 1 */
    {
        char out[16];
        const char *s = "&unknown_entity;";
        size_t c = html_entity_decode(s, s + strlen(s), out, sizeof(out));
        assert(c == 1 && out[0] == '&');
    }

    /* 2.7 Full string decode helper */
    {
        const char *raw = "Fish &amp; Chips &lt;3 &quot;Good&quot; &#x26; Great";
        char *dec = html_entities_decode_alloc(raw, strlen(raw));
        assert(dec != NULL);
        assert(strcmp(dec, "Fish & Chips <3 \"Good\" & Great") == 0);
        bb_free(dec);
    }

    printf("  -> Entity Decoder Tests PASSED!\n");
}

/* =========================================================================
 * 3. Parser & DOM Tree Tests
 * ========================================================================= */
static void test_parser_dom(void) {
    printf("[TEST] Running HTML Parser & DOM Tree Tests...\n");

    /* 3.1 Basic tree structure */
    {
        const char *src = "<html><body><p>Hi</p></body></html>";
        bb_node_t *doc = bb_html_parse(src);
        assert(doc != NULL && doc->type == BB_NODE_DOCUMENT);
        assert(doc->first_child != NULL);
        bb_node_t *html = doc->first_child;
        assert(strcmp(html->tag_name, "html") == 0);
        bb_node_t *body = html->first_child;
        assert(strcmp(body->tag_name, "body") == 0);
        bb_node_t *p = body->first_child;
        assert(strcmp(p->tag_name, "p") == 0);
        assert(p->first_child != NULL && p->first_child->type == BB_NODE_TEXT);
        assert(strcmp(p->first_child->text_content, "Hi") == 0);
        bb_node_free_tree(doc);
    }

    /* 3.2 Auto-closing <p> tags: <p>A<p>B -> siblings */
    {
        const char *src = "<div><p>First<p>Second</div>";
        bb_node_t *doc = bb_html_parse(src);
        assert(doc != NULL);
        bb_node_t *div = doc->first_child;
        assert(div != NULL && strcmp(div->tag_name, "div") == 0);
        bb_node_t *p1 = div->first_child;
        assert(p1 != NULL && strcmp(p1->tag_name, "p") == 0);
        bb_node_t *p2 = p1->next_sibling;
        assert(p2 != NULL && strcmp(p2->tag_name, "p") == 0);
        assert(strcmp(p1->first_child->text_content, "First") == 0);
        assert(strcmp(p2->first_child->text_content, "Second") == 0);
        bb_node_free_tree(doc);
    }

    /* 3.3 Auto-closing <li> tags: <ul><li>A<li>B</ul> */
    {
        const char *src = "<ul><li>Item 1<li>Item 2</ul>";
        bb_node_t *doc = bb_html_parse(src);
        assert(doc != NULL);
        bb_node_t *ul = doc->first_child;
        assert(ul != NULL && strcmp(ul->tag_name, "ul") == 0);
        bb_node_t *li1 = ul->first_child;
        assert(li1 != NULL && strcmp(li1->tag_name, "li") == 0);
        bb_node_t *li2 = li1->next_sibling;
        assert(li2 != NULL && strcmp(li2->tag_name, "li") == 0);
        assert(strcmp(li1->first_child->text_content, "Item 1") == 0);
        assert(strcmp(li2->first_child->text_content, "Item 2") == 0);
        bb_node_free_tree(doc);
    }

    /* 3.4 Orphan end tag ignored */
    {
        const char *src = "<div></span><p>Valid</p></div>";
        bb_node_t *doc = bb_html_parse(src);
        assert(doc != NULL);
        bb_node_t *div = doc->first_child;
        assert(div != NULL && strcmp(div->tag_name, "div") == 0);
        bb_node_t *p = div->first_child;
        assert(p != NULL && strcmp(p->tag_name, "p") == 0);
        bb_node_free_tree(doc);
    }

    /* 3.5 Unclosed tags at EOF */
    {
        const char *src = "<div><span><p>Text";
        bb_node_t *doc = bb_html_parse(src);
        assert(doc != NULL);
        bb_node_t *div = doc->first_child;
        assert(div != NULL && strcmp(div->tag_name, "div") == 0);
        bb_node_t *span = div->first_child;
        assert(span != NULL && strcmp(span->tag_name, "span") == 0);
        bb_node_t *p = span->first_child;
        assert(p != NULL && strcmp(p->tag_name, "p") == 0);
        assert(strcmp(p->first_child->text_content, "Text") == 0);
        bb_node_free_tree(doc);
    }

    /* 3.6 Inline formatting & entities in text */
    {
        const char *src = "<b>bold &amp; <i>italic</i></b>";
        bb_node_t *doc = bb_html_parse(src);
        assert(doc != NULL);
        bb_node_t *b = doc->first_child;
        assert(b != NULL && strcmp(b->tag_name, "b") == 0);
        assert(b->first_child != NULL && b->first_child->type == BB_NODE_TEXT);
        assert(strcmp(b->first_child->text_content, "bold & ") == 0);
        bb_node_t *i = b->first_child->next_sibling;
        assert(i != NULL && strcmp(i->tag_name, "i") == 0);
        assert(strcmp(i->first_child->text_content, "italic") == 0);
        bb_node_free_tree(doc);
    }

    /* 3.7 Unknown custom elements */
    {
        const char *src = "<custom-widget class=\"cool\" data-id=\"100\">Content</custom-widget>";
        bb_node_t *doc = bb_html_parse(src);
        assert(doc != NULL);
        bb_node_t *cw = doc->first_child;
        assert(cw != NULL && strcmp(cw->tag_name, "custom-widget") == 0);
        assert(strcmp(bb_node_get_attribute(cw, "class"), "cool") == 0);
        assert(strcmp(bb_node_get_attribute(cw, "data-id"), "100") == 0);
        bb_node_free_tree(doc);
    }

    /* 3.8 <style> tag body is not leaked into DOM text nodes */
    {
        const char *src = "<head><style>body { color: red; }</style></head><body><p>Text</p></body>";
        bb_node_t *doc = bb_html_parse(src);
        assert(doc != NULL);
        bb_node_t *head = doc->first_child;
        assert(head != NULL && strcmp(head->tag_name, "head") == 0);
        bb_node_t *style = head->first_child;
        assert(style != NULL && strcmp(style->tag_name, "style") == 0);
        /* Ensure style has no BB_NODE_TEXT child */
        if (style->first_child) {
            assert(style->first_child->type != BB_NODE_TEXT);
        }
        bb_node_t *body = head->next_sibling;
        assert(body != NULL && strcmp(body->tag_name, "body") == 0);
        bb_node_free_tree(doc);
    }

    /* 3.9 <script> tag body is not leaked */
    {
        const char *src = "<div><script>alert('test &amp; 123');</script><p>Clean</p></div>";
        bb_node_t *doc = bb_html_parse(src);
        assert(doc != NULL);
        bb_node_t *div = doc->first_child;
        bb_node_t *script = div->first_child;
        assert(script != NULL && strcmp(script->tag_name, "script") == 0);
        if (script->first_child) {
            assert(script->first_child->type != BB_NODE_TEXT);
        }
        bb_node_t *p = script->next_sibling;
        assert(p != NULL && strcmp(p->tag_name, "p") == 0);
        assert(strcmp(p->first_child->text_content, "Clean") == 0);
        bb_node_free_tree(doc);
    }

    /* 3.10 Void tag <img> attributes */
    {
        const char *src = "<img src=\"logo.png\" alt=\"Logo &amp; Brand\">";
        bb_node_t *doc = bb_html_parse(src);
        assert(doc != NULL);
        bb_node_t *img = doc->first_child;
        assert(img != NULL && strcmp(img->tag_name, "img") == 0);
        assert(img->first_child == NULL);
        assert(strcmp(bb_node_get_attribute(img, "src"), "logo.png") == 0);
        assert(strcmp(bb_node_get_attribute(img, "alt"), "Logo & Brand") == 0);
        bb_node_free_tree(doc);
    }

    /* 3.11 Anchor with query parameter entities */
    {
        const char *src = "<a href=\"https://site.org/index?a=1&amp;b=2\">Click</a>";
        bb_node_t *doc = bb_html_parse(src);
        assert(doc != NULL);
        bb_node_t *a = doc->first_child;
        assert(a != NULL && strcmp(a->tag_name, "a") == 0);
        assert(strcmp(bb_node_get_attribute(a, "href"), "https://site.org/index?a=1&b=2") == 0);
        assert(strcmp(a->first_child->text_content, "Click") == 0);
        bb_node_free_tree(doc);
    }

    /* 3.12 Single-quoted class attribute */
    {
        const char *src = "<div class='container main-view'>Hello</div>";
        bb_node_t *doc = bb_html_parse(src);
        assert(doc != NULL);
        bb_node_t *div = doc->first_child;
        assert(div != NULL && strcmp(div->tag_name, "div") == 0);
        assert(strcmp(bb_node_get_attribute(div, "class"), "container main-view") == 0);
        assert(bb_node_has_class(div, "container") == true);
        assert(bb_node_has_class(div, "main-view") == true);
        bb_node_free_tree(doc);
    }

    printf("  -> Parser & DOM Tree Tests PASSED!\n");
}

/* =========================================================================
 * 4. Layout Engine Integration Test
 * ========================================================================= */
static void test_layout_integration(void) {
    printf("[TEST] Running Layout Pipeline Integration Test...\n");

    const char *html = 
        "<div id=\"root\" style=\"width: 600px;\">\n"
        "  <h1>Universal Browser</h1>\n"
        "  <p>FirstOS with <b>HTML frontend</b> &amp; modern styling.</p>\n"
        "  <ul>\n"
        "    <li>Modular token stream</li>\n"
        "    <li>Entity resolution</li>\n"
        "  </ul>\n"
        "</div>";

    bb_node_t *doc = bb_html_parse(html);
    assert(doc != NULL);

    /* Apply default styles */
    bb_stylesheet_t *sheet = bb_css_create();
    bb_css_apply_styles(doc, sheet);

    /* Build layout tree */
    bb_layout_box_t *root_box = bb_layout_build_tree(doc);
    assert(root_box != NULL);

    /* Compute layout */
    bb_layout_compute(root_box, 0, 0, 800);

    /* Verify dimensions */
    assert(root_box->dimensions.w > 0);
    assert(root_box->dimensions.h > 0);

    bb_layout_free_tree(root_box);
    bb_css_free(sheet);
    bb_node_free_tree(doc);

    printf("  -> Layout Integration Test PASSED!\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS HTML Front-End Comprehensive Test Suite\n");
    printf("============================================================\n");

    test_tokenizer();
    test_entities();
    test_parser_dom();
    test_layout_integration();

    printf("\nAll HTML Front-End Unit & Integration Tests PASSED!\n");
    return 0;
}
