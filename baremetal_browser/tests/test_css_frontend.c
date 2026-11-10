#include "../bb_types.h"
#include "../bb_libc.h"
#include "../bb_dom.h"
#include "../bb_html.h"
#include "../bb_css.h"
#include "../css_tokenizer.h"
#include "../css_selectors.h"
#include "../css_parser.h"
#include "../css_cascade.h"
#include "../style_resolver.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* =========================================================================
 * 1. Tokenizer Unit Tests
 * ========================================================================= */
static void test_tokenizer(void) {
    printf("[TEST] Running CSS Tokenizer Tests...\n");

    /* 1.1 Identifiers */
    {
        css_tokenizer_t tok;
        css_token_t t;
        const char *src = "display margin-top -webkit-box";
        css_tok_init(&tok, src, strlen(src));
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_IDENT && strncmp(t.text, "display", 7) == 0);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_WHITESPACE);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_IDENT && strncmp(t.text, "margin-top", 10) == 0);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_WHITESPACE);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_IDENT && strncmp(t.text, "-webkit-box", 11) == 0);
        assert(!css_tok_next(&tok, &t));
    }

    /* 1.2 Numbers and dimensions */
    {
        css_tokenizer_t tok;
        css_token_t t;
        const char *src = "10px 100% -5.5em 42";
        css_tok_init(&tok, src, strlen(src));
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_DIMENSION && t.num_val == 10.0 && strcmp(t.unit, "px") == 0);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_WHITESPACE);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_DIMENSION && t.num_val == 100.0 && strcmp(t.unit, "%") == 0);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_WHITESPACE);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_DIMENSION && t.num_val < -5.4 && strcmp(t.unit, "em") == 0);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_WHITESPACE);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_NUMBER && t.num_val == 42.0);
    }

    /* 1.3 Hash tokens */
    {
        css_tokenizer_t tok;
        css_token_t t;
        const char *src = "#header #ff0000";
        css_tok_init(&tok, src, strlen(src));
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_HASH && strncmp(t.text, "header", 6) == 0);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_WHITESPACE);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_HASH && strncmp(t.text, "ff0000", 6) == 0);
    }

    /* 1.4 Comments skipped */
    {
        css_tokenizer_t tok;
        css_token_t t;
        const char *src = "div/* comment */p";
        css_tok_init(&tok, src, strlen(src));
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_IDENT && strncmp(t.text, "div", 3) == 0);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_WHITESPACE);
        assert(css_tok_next(&tok, &t) && t.type == CSS_TOK_IDENT && strncmp(t.text, "p", 1) == 0);
    }

    printf("  -> Tokenizer Tests PASSED!\n");
}

/* =========================================================================
 * 2. Selector Matching Tests
 * ========================================================================= */
static void test_selectors(void) {
    printf("[TEST] Running CSS Selector Tests...\n");

    const char *html = 
        "<div id=\"root\" class=\"wrapper\">\n"
        "  <header id=\"hdr\" class=\"banner top\">\n"
        "    <h1 class=\"title\">Site</h1>\n"
        "    <nav class=\"links\"><a href=\"#\">Home</a></nav>\n"
        "  </header>\n"
        "  <main><p class=\"intro\">Welcome</p></main>\n"
        "</div>";

    bb_node_t *doc = bb_html_parse(html);
    assert(doc != NULL);

    bb_node_t *root = doc->first_child;
    assert(root != NULL && strcmp(root->tag_name, "div") == 0);
    bb_node_t *hdr = root->first_child;
    bb_node_t *h1 = hdr->first_child;
    bb_node_t *nav = h1->next_sibling;
    bb_node_t *a = nav->first_child;
    bb_node_t *main = hdr->next_sibling;
    bb_node_t *p = main->first_child;

    /* 2.1 Tag selector */
    {
        css_selector_step_t *step = css_selector_step_create();
        step->tag = bb_strdup("header");
        css_selector_chain_t *chain = css_selector_chain_create();
        chain->key_selector = step;
        chain->specificity = css_selector_calc_specificity(chain);
        assert(chain->specificity == 1);
        assert(css_selector_match(chain, hdr) == true);
        assert(css_selector_match(chain, root) == false);
        css_selector_chain_free(chain);
    }

    /* 2.2 Class selector */
    {
        css_selector_step_t *step = css_selector_step_create();
        step->class_name = bb_strdup("banner");
        css_selector_chain_t *chain = css_selector_chain_create();
        chain->key_selector = step;
        chain->specificity = css_selector_calc_specificity(chain);
        assert(chain->specificity == 100);
        assert(css_selector_match(chain, hdr) == true);
        assert(css_selector_match(chain, root) == false);
        css_selector_chain_free(chain);
    }

    /* 2.3 ID selector */
    {
        css_selector_step_t *step = css_selector_step_create();
        step->id = bb_strdup("hdr");
        css_selector_chain_t *chain = css_selector_chain_create();
        chain->key_selector = step;
        chain->specificity = css_selector_calc_specificity(chain);
        assert(chain->specificity == 10000);
        assert(css_selector_match(chain, hdr) == true);
        assert(css_selector_match(chain, root) == false);
        css_selector_chain_free(chain);
    }

    /* 2.4 Compound selector: header.banner#hdr */
    {
        css_selector_step_t *step = css_selector_step_create();
        step->tag = bb_strdup("header");
        step->class_name = bb_strdup("banner");
        step->id = bb_strdup("hdr");
        css_selector_chain_t *chain = css_selector_chain_create();
        chain->key_selector = step;
        chain->specificity = css_selector_calc_specificity(chain);
        assert(chain->specificity == 10101);
        assert(css_selector_match(chain, hdr) == true);
        css_selector_chain_free(chain);
    }

    /* 2.5 Descendant combinator: "header a" */
    {
        css_selector_step_t *anc = css_selector_step_create();
        anc->tag = bb_strdup("header");

        css_selector_step_t *key = css_selector_step_create();
        key->tag = bb_strdup("a");
        key->ancestor = anc;

        css_selector_chain_t *chain = css_selector_chain_create();
        chain->key_selector = key;
        chain->specificity = css_selector_calc_specificity(chain);
        assert(chain->specificity == 2);
        assert(css_selector_match(chain, a) == true);
        assert(css_selector_match(chain, p) == false);
        css_selector_chain_free(chain);
    }

    bb_node_free_tree(doc);
    printf("  -> Selector Tests PASSED!\n");
}

/* =========================================================================
 * 3. Specificity & Cascade Precedence Tests
 * ========================================================================= */
static void test_cascade_and_specificity(void) {
    printf("[TEST] Running CSS Specificity & Cascade Tests...\n");

    const char *html = 
        "<div id=\"box\" class=\"panel card\">Content</div>\n"
        "<p id=\"txt\" class=\"highlight\" style=\"color: #00ff00;\">Inline</p>";

    bb_node_t *doc = bb_html_parse(html);
    assert(doc != NULL);
    bb_node_t *div = doc->first_child;
    bb_node_t *p = div->next_sibling;

    /* Stylesheet where:
     * - tag rule has color: black
     * - class rule has color: blue (wins over tag)
     * - id rule has color: red (wins over class)
     */
    const char *css = 
        "div { color: black; background-color: #111111; }\n"
        ".panel { color: blue; background-color: #222222; }\n"
        "#box { color: red; }\n"
        "/* Later rule with same specificity wins tie */\n"
        ".panel { padding: 10px; }\n"
        ".card { padding: 20px; }\n"
        "/* P has id and class in stylesheet, but inline style overrides all */\n"
        "#txt { color: #0000ff; }\n"
        ".highlight { color: #ff0000; }";

    bb_stylesheet_t *sheet = bb_css_create();
    bb_css_parse_rules(sheet, css);
    bb_css_apply_styles(doc, sheet);

    /* ID #box wins color over .panel and div */
    assert(div->computed_style->color == BB_RGB(230, 40, 40)); /* red */

    /* .panel wins background-color over div */
    assert(div->computed_style->background_color == BB_RGB(0x22, 0x22, 0x22));

    /* .card (source order 4) wins padding over .panel (source order 3) */
    assert(div->computed_style->padding_top == 20);

    /* Inline style on <p> wins over #txt */
    assert(p->computed_style->color == BB_RGB(0, 255, 0));

    bb_css_free(sheet);
    bb_node_free_tree(doc);
    printf("  -> Specificity & Cascade Tests PASSED!\n");
}

/* =========================================================================
 * 4. Property Inheritance Tests
 * ========================================================================= */
static void test_inheritance(void) {
    printf("[TEST] Running CSS Property Inheritance Tests...\n");

    const char *html = 
        "<div id=\"container\">\n"
        "  <p>Paragraph with <span>Span</span></p>\n"
        "</div>";

    bb_node_t *doc = bb_html_parse(html);
    assert(doc != NULL);
    bb_node_t *div = doc->first_child;
    bb_node_t *p = div->first_child;
    bb_node_t *span = p->first_child->next_sibling; /* "Paragraph with " is text child 1 */

    const char *css = 
        "#container {\n"
        "  color: #ff00ff;\n"
        "  font-size: 20px;\n"
        "  text-align: right;\n"
        "  margin: 50px;\n"
        "  background-color: #eeeeee;\n"
        "}";

    bb_stylesheet_t *sheet = bb_css_create();
    bb_css_parse_rules(sheet, css);
    bb_css_apply_styles(doc, sheet);

    /* Div has explicit styles */
    assert(div->computed_style->font_size == 20);
    assert(div->computed_style->text_align == BB_TEXT_ALIGN_RIGHT);
    assert(div->computed_style->margin_top == 50);

    /* Paragraph inherits color, font-size, text-align */
    assert(p->computed_style->color == BB_RGB(255, 0, 255)); /* #ff00ff */
    assert(p->computed_style->font_size == 20);
    assert(p->computed_style->text_align == BB_TEXT_ALIGN_RIGHT);

    /* Paragraph does NOT inherit margin or background-color from container */
    assert(p->computed_style->margin_top == 10); /* UA default for p is 10, not 50 */
    assert(p->computed_style->background_color == BB_COLOR_TRANSPARENT);

    /* Span inherits from paragraph */
    assert(span->computed_style->color == BB_RGB(255, 0, 255));
    assert(span->computed_style->font_size == 20);

    bb_css_free(sheet);
    bb_node_free_tree(doc);
    printf("  -> Inheritance Tests PASSED!\n");
}

/* =========================================================================
 * 5. Shorthands and Extended Properties Tests
 * ========================================================================= */
static void test_extended_properties(void) {
    printf("[TEST] Running Extended Properties & Shorthands Tests...\n");

    const char *html = "<div id=\"test\"></div>";
    bb_node_t *doc = bb_html_parse(html);
    bb_node_t *div = doc->first_child;

    const char *css = 
        "#test {\n"
        "  display: inline-block;\n"
        "  line-height: 24px;\n"
        "  white-space: nowrap;\n"
        "  overflow: hidden;\n"
        "  visibility: hidden;\n"
        "  margin: 10px 20px 30px 40px;\n"
        "  border: 3px solid blue;\n"
        "}";

    bb_stylesheet_t *sheet = bb_css_create();
    bb_css_parse_rules(sheet, css);
    bb_css_apply_styles(doc, sheet);

    assert(div->computed_style->display == BB_DISPLAY_INLINE_BLOCK);
    assert(div->computed_style->line_height == 24);
    assert(div->computed_style->white_space == BB_WHITE_SPACE_NOWRAP);
    assert(div->computed_style->overflow == BB_OVERFLOW_HIDDEN);
    assert(div->computed_style->visibility == BB_VISIBILITY_HIDDEN);

    /* 4-value margin shorthand */
    assert(div->computed_style->margin_top == 10);
    assert(div->computed_style->margin_right == 20);
    assert(div->computed_style->margin_bottom == 30);
    assert(div->computed_style->margin_left == 40);

    /* Border shorthand */
    assert(div->computed_style->border_width == 3);
    assert(div->computed_style->border_color == BB_RGB(30, 110, 230));

    bb_css_free(sheet);
    bb_node_free_tree(doc);
    printf("  -> Extended Properties Tests PASSED!\n");
}

/* =========================================================================
 * 6. Malformed Input and Resilience Tests
 * ========================================================================= */
static void test_malformed_css(void) {
    printf("[TEST] Running Malformed CSS Resilience Tests...\n");

    const char *html = "<div id=\"target\">Text</div>";
    bb_node_t *doc = bb_html_parse(html);
    bb_node_t *div = doc->first_child;

    /* CSS with:
     * - broken declaration
     * - unknown property
     * - missing semicolon
     * - unclosed comment at EOF
     */
    const char *css = 
        "div { broken; invalid-property: 12345; width: 300px }\n"
        "#target { height: 150px; unknown: foo; }\n"
        "/* unclosed comment at end of stream ";

    bb_stylesheet_t *sheet = bb_css_create();
    bb_css_parse_rules(sheet, css);
    bb_css_apply_styles(doc, sheet);

    /* Valid properties in rules are parsed despite syntax noise */
    assert(div->computed_style->width == 300);
    assert(div->computed_style->height == 150);

    bb_css_free(sheet);
    bb_node_free_tree(doc);
    printf("  -> Malformed CSS Resilience Tests PASSED!\n");
}

int main(void) {
    printf("============================================================\n");
    printf("FirstOS CSS Front-End Comprehensive Test Suite\n");
    printf("============================================================\n");

    test_tokenizer();
    test_selectors();
    test_cascade_and_specificity();
    test_inheritance();
    test_extended_properties();
    test_malformed_css();

    printf("\nAll CSS Front-End Unit Tests PASSED!\n");
    return 0;
}
