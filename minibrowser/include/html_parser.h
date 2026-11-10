#ifndef HTML_PARSER_H
#define HTML_PARSER_H

#include "dom.h"
#include <stdbool.h>

typedef enum {
    TOKEN_START_TAG,
    TOKEN_END_TAG,
    TOKEN_TEXT,
    TOKEN_COMMENT,
    TOKEN_EOF
} HTMLTokenType;

typedef struct HTMLToken {
    HTMLTokenType type;
    char *tag_name;
    char *text;
    Attribute *attributes;
    bool is_self_closing;
} HTMLToken;

void html_token_free(HTMLToken *token);

/* Parses HTML string into a DOMNode tree (returns root NODE_DOCUMENT) */
DOMNode *html_parse(const char *html_source);

#endif /* HTML_PARSER_H */
