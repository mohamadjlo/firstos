#ifndef CSS_TOKENIZER_H
#define CSS_TOKENIZER_H

#include "bb_types.h"

typedef enum {
    CSS_TOK_EOF = 0,
    CSS_TOK_IDENT,        /* e.g. "display", "margin-top", "red" */
    CSS_TOK_HASH,         /* e.g. "#id" or "#ffffff" (excluding '#') */
    CSS_TOK_DOT,          /* "." */
    CSS_TOK_COLON,        /* ":" */
    CSS_TOK_SEMICOLON,    /* ";" */
    CSS_TOK_COMMA,        /* "," */
    CSS_TOK_LBRACE,       /* "{" */
    CSS_TOK_RBRACE,       /* "}" */
    CSS_TOK_STRING,       /* "..." or '...' (excluding quotes) */
    CSS_TOK_NUMBER,       /* e.g. "12", "0", "-5" */
    CSS_TOK_DIMENSION,    /* e.g. "16px", "2em", "100%" */
    CSS_TOK_WHITESPACE,   /* Significant for descendant combinator */
    CSS_TOK_ASTERISK,     /* "*" universal selector */
    CSS_TOK_DELIM         /* Any other character */
} css_tok_type_t;

typedef struct {
    css_tok_type_t type;
    const char    *text;
    size_t         len;
    double         num_val;    /* For numbers/dimensions */
    char           unit[8];    /* e.g. "px", "%", "em" */
} css_token_t;

typedef struct {
    const char *src;
    const char *cur;
    const char *end;
} css_tokenizer_t;

void css_tok_init(css_tokenizer_t *tok, const char *src, size_t len);
bool css_tok_next(css_tokenizer_t *tok, css_token_t *out);

#endif /* CSS_TOKENIZER_H */
