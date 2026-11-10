#ifndef HTML_TOKENIZER_H
#define HTML_TOKENIZER_H

#include "bb_types.h"

typedef enum {
    HTML_TOK_EOF = 0,
    HTML_TOK_DOCTYPE,
    HTML_TOK_COMMENT,
    HTML_TOK_TEXT,
    HTML_TOK_START_TAG,
    HTML_TOK_END_TAG,
    HTML_TOK_SELF_CLOSING,
} html_tok_type_t;

#define HTML_TOK_MAX_ATTRS 32

typedef struct {
    const char *key;
    size_t      key_len;
    const char *val;       /* Raw bytes — entities not decoded here */
    size_t      val_len;
    bool        has_val;
} html_attr_t;

typedef struct {
    html_tok_type_t type;
    const char     *tag;       /* Pointer into source */
    size_t          tag_len;
    const char     *text;      /* Pointer into source for TEXT / COMMENT / DOCTYPE */
    size_t          text_len;
    html_attr_t     attrs[HTML_TOK_MAX_ATTRS];
    int             attr_count;
    const char     *src_start; /* Byte range for debugging / tracking */
    const char     *src_end;
} html_token_t;

typedef struct {
    const char *src;
    const char *cur;
    const char *end;
} html_tokenizer_t;

void html_tok_init(html_tokenizer_t *t, const char *src, size_t len);
bool html_tok_next(html_tokenizer_t *t, html_token_t *tok);

#endif /* HTML_TOKENIZER_H */
