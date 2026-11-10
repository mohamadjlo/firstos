#define _POSIX_C_SOURCE 200809L
#include "html_parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void html_token_free(HTMLToken *token) {
    if (!token) return;
    free(token->tag_name);
    free(token->text);
    Attribute *attr = token->attributes;
    while (attr) {
        Attribute *next = attr->next;
        free(attr->key);
        free(attr->value);
        free(attr);
        attr = next;
    }
    memset(token, 0, sizeof(HTMLToken));
}

static bool is_void_tag(const char *tag) {
    if (!tag) return false;
    static const char *void_tags[] = {
        "area", "base", "br", "col", "embed", "hr", "img",
        "input", "link", "meta", "param", "source", "track", "wbr"
    };
    for (size_t i = 0; i < sizeof(void_tags) / sizeof(void_tags[0]); i++) {
        if (strcasecmp(tag, void_tags[i]) == 0) return true;
    }
    return false;
}

static char *to_lower_str(const char *src) {
    if (!src) return NULL;
    char *res = strdup(src);
    for (char *p = res; *p; p++) *p = (char)tolower((unsigned char)*p);
    return res;
}

typedef enum {
    LEX_DATA,
    LEX_TAG_OPEN,
    LEX_TAG_NAME,
    LEX_BEFORE_ATTR_NAME,
    LEX_ATTR_NAME,
    LEX_AFTER_ATTR_NAME,
    LEX_BEFORE_ATTR_VALUE,
    LEX_ATTR_VALUE_DOUBLE_QUOTED,
    LEX_ATTR_VALUE_SINGLE_QUOTED,
    LEX_ATTR_VALUE_UNQUOTED,
    LEX_AFTER_ATTR_VALUE,
    LEX_SELF_CLOSING_START_TAG,
    LEX_END_TAG_OPEN,
    LEX_END_TAG_NAME,
    LEX_COMMENT
} LexerState;

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
} Lexer;

static void append_token_attr(HTMLToken *token, char *attr_key, char *attr_val) {
    if (attr_key[0]) {
        Attribute *a = (Attribute *)calloc(1, sizeof(Attribute));
        a->key = to_lower_str(attr_key);
        a->value = strdup(attr_val);
        a->next = token->attributes;
        token->attributes = a;
        attr_key[0] = 0;
        attr_val[0] = 0;
    }
}

static bool lexer_get_next_token(Lexer *lex, HTMLToken *token) {
    memset(token, 0, sizeof(HTMLToken));
    if (lex->pos >= lex->len) {
        token->type = TOKEN_EOF;
        return false;
    }

    LexerState state = LEX_DATA;
    char buf[4096];
    size_t blen = 0;

    char attr_key[512] = {0};
    char attr_val[1024] = {0};

    #define add_attr() append_token_attr(token, attr_key, attr_val)

    while (lex->pos < lex->len) {
        char c = lex->src[lex->pos];

        switch (state) {
            case LEX_DATA:
                if (c == '<') {
                    if (blen > 0) {
                        /* Return accumulated text */
                        buf[blen] = 0;
                        token->type = TOKEN_TEXT;
                        token->text = strdup(buf);
                        return true;
                    }
                    state = LEX_TAG_OPEN;
                    lex->pos++;
                } else {
                    if (blen + 1 < sizeof(buf)) buf[blen++] = c;
                    lex->pos++;
                }
                break;

            case LEX_TAG_OPEN:
                if (c == '!') {
                    /* Check for comment <!-- */
                    if (lex->pos + 2 < lex->len &&
                        lex->src[lex->pos + 1] == '-' &&
                        lex->src[lex->pos + 2] == '-') {
                        lex->pos += 3;
                        state = LEX_COMMENT;
                    } else {
                        /* Treat DOCTYPE or other declaration as comment */
                        state = LEX_COMMENT;
                        lex->pos++;
                    }
                } else if (c == '/') {
                    state = LEX_END_TAG_OPEN;
                    lex->pos++;
                } else if (isalpha((unsigned char)c)) {
                    state = LEX_TAG_NAME;
                    blen = 0;
                    buf[blen++] = c;
                    lex->pos++;
                } else {
                    /* Stray '<', treat as text */
                    buf[blen++] = '<';
                    state = LEX_DATA;
                }
                break;

            case LEX_TAG_NAME:
                if (isspace((unsigned char)c)) {
                    buf[blen] = 0;
                    token->type = TOKEN_START_TAG;
                    token->tag_name = to_lower_str(buf);
                    state = LEX_BEFORE_ATTR_NAME;
                    lex->pos++;
                } else if (c == '/') {
                    buf[blen] = 0;
                    token->type = TOKEN_START_TAG;
                    token->tag_name = to_lower_str(buf);
                    state = LEX_SELF_CLOSING_START_TAG;
                    lex->pos++;
                } else if (c == '>') {
                    buf[blen] = 0;
                    token->type = TOKEN_START_TAG;
                    token->tag_name = to_lower_str(buf);
                    lex->pos++;
                    return true;
                } else {
                    if (blen + 1 < sizeof(buf)) buf[blen++] = c;
                    lex->pos++;
                }
                break;

            case LEX_BEFORE_ATTR_NAME:
                if (isspace((unsigned char)c)) {
                    lex->pos++;
                } else if (c == '/') {
                    state = LEX_SELF_CLOSING_START_TAG;
                    lex->pos++;
                } else if (c == '>') {
                    lex->pos++;
                    return true;
                } else {
                    state = LEX_ATTR_NAME;
                    blen = 0;
                    buf[blen++] = c;
                    lex->pos++;
                }
                break;

            case LEX_ATTR_NAME:
                if (c == '=' || isspace((unsigned char)c) || c == '/' || c == '>') {
                    buf[blen] = 0;
                    strncpy(attr_key, buf, sizeof(attr_key) - 1);
                    attr_val[0] = 0;

                    if (c == '=') {
                        state = LEX_BEFORE_ATTR_VALUE;
                        lex->pos++;
                    } else if (isspace((unsigned char)c)) {
                        state = LEX_AFTER_ATTR_NAME;
                        lex->pos++;
                    } else {
                        /* Boolean attribute */
                        add_attr();
                        if (c == '/') state = LEX_SELF_CLOSING_START_TAG;
                        else if (c == '>') {
                            lex->pos++;
                            return true;
                        }
                    }
                } else {
                    if (blen + 1 < sizeof(buf)) buf[blen++] = c;
                    lex->pos++;
                }
                break;

            case LEX_AFTER_ATTR_NAME:
                if (isspace((unsigned char)c)) {
                    lex->pos++;
                } else if (c == '=') {
                    state = LEX_BEFORE_ATTR_VALUE;
                    lex->pos++;
                } else {
                    /* Attribute without value */
                    add_attr();
                    state = LEX_BEFORE_ATTR_NAME;
                }
                break;

            case LEX_BEFORE_ATTR_VALUE:
                if (isspace((unsigned char)c)) {
                    lex->pos++;
                } else if (c == '"') {
                    state = LEX_ATTR_VALUE_DOUBLE_QUOTED;
                    blen = 0;
                    lex->pos++;
                } else if (c == '\'') {
                    state = LEX_ATTR_VALUE_SINGLE_QUOTED;
                    blen = 0;
                    lex->pos++;
                } else if (c == '>') {
                    add_attr();
                    lex->pos++;
                    return true;
                } else {
                    state = LEX_ATTR_VALUE_UNQUOTED;
                    blen = 0;
                    buf[blen++] = c;
                    lex->pos++;
                }
                break;

            case LEX_ATTR_VALUE_DOUBLE_QUOTED:
                if (c == '"') {
                    buf[blen] = 0;
                    strncpy(attr_val, buf, sizeof(attr_val) - 1);
                    add_attr();
                    state = LEX_AFTER_ATTR_VALUE;
                    lex->pos++;
                } else {
                    if (blen + 1 < sizeof(buf)) buf[blen++] = c;
                    lex->pos++;
                }
                break;

            case LEX_ATTR_VALUE_SINGLE_QUOTED:
                if (c == '\'') {
                    buf[blen] = 0;
                    strncpy(attr_val, buf, sizeof(attr_val) - 1);
                    add_attr();
                    state = LEX_AFTER_ATTR_VALUE;
                    lex->pos++;
                } else {
                    if (blen + 1 < sizeof(buf)) buf[blen++] = c;
                    lex->pos++;
                }
                break;

            case LEX_ATTR_VALUE_UNQUOTED:
                if (isspace((unsigned char)c) || c == '>') {
                    buf[blen] = 0;
                    strncpy(attr_val, buf, sizeof(attr_val) - 1);
                    add_attr();
                    if (c == '>') {
                        lex->pos++;
                        return true;
                    }
                    state = LEX_BEFORE_ATTR_NAME;
                    lex->pos++;
                } else {
                    if (blen + 1 < sizeof(buf)) buf[blen++] = c;
                    lex->pos++;
                }
                break;

            case LEX_AFTER_ATTR_VALUE:
                if (isspace((unsigned char)c)) {
                    state = LEX_BEFORE_ATTR_NAME;
                    lex->pos++;
                } else if (c == '/') {
                    state = LEX_SELF_CLOSING_START_TAG;
                    lex->pos++;
                } else if (c == '>') {
                    lex->pos++;
                    return true;
                } else {
                    state = LEX_BEFORE_ATTR_NAME;
                }
                break;

            case LEX_SELF_CLOSING_START_TAG:
                if (c == '>') {
                    token->is_self_closing = true;
                    lex->pos++;
                    return true;
                }
                lex->pos++;
                break;

            case LEX_END_TAG_OPEN:
                if (isalpha((unsigned char)c)) {
                    state = LEX_END_TAG_NAME;
                    blen = 0;
                    buf[blen++] = c;
                    lex->pos++;
                } else {
                    state = LEX_DATA;
                }
                break;

            case LEX_END_TAG_NAME:
                if (c == '>') {
                    buf[blen] = 0;
                    token->type = TOKEN_END_TAG;
                    token->tag_name = to_lower_str(buf);
                    lex->pos++;
                    return true;
                } else if (isspace((unsigned char)c)) {
                    lex->pos++;
                } else {
                    if (blen + 1 < sizeof(buf)) buf[blen++] = c;
                    lex->pos++;
                }
                break;

            case LEX_COMMENT:
                if (c == '-' && lex->pos + 2 < lex->len &&
                    lex->src[lex->pos + 1] == '-' &&
                    lex->src[lex->pos + 2] == '>') {
                    lex->pos += 3;
                    state = LEX_DATA;
                } else if (c == '>') {
                    lex->pos++;
                    state = LEX_DATA;
                } else {
                    lex->pos++;
                }
                break;
        }
    }

    if (blen > 0 && state == LEX_DATA) {
        buf[blen] = 0;
        token->type = TOKEN_TEXT;
        token->text = strdup(buf);
        return true;
    }

    token->type = TOKEN_EOF;
#undef add_attr
    return false;
}

DOMNode *html_parse(const char *html_source) {
    if (!html_source) return NULL;

    DOMNode *doc = dom_node_create(NODE_DOCUMENT);
    if (!doc) return NULL;

    Lexer lex = {
        .src = html_source,
        .pos = 0,
        .len = strlen(html_source)
    };

    #define STACK_CAPACITY 256
    DOMNode *stack[STACK_CAPACITY];
    int top = 0;
    stack[top] = doc;

    HTMLToken token;
    while (lexer_get_next_token(&lex, &token)) {
        if (token.type == TOKEN_START_TAG) {
            DOMNode *elem = dom_node_create(NODE_ELEMENT);
            elem->tag_name = token.tag_name ? strdup(token.tag_name) : strdup("div");
            elem->attributes = token.attributes;
            token.attributes = NULL; /* Transfer ownership */

            dom_node_append_child(stack[top], elem);

            bool self_closing = token.is_self_closing || is_void_tag(elem->tag_name);
            if (!self_closing && top + 1 < STACK_CAPACITY) {
                stack[++top] = elem;
            }
        } else if (token.type == TOKEN_END_TAG) {
            if (token.tag_name) {
                /* Find matching element in stack */
                int match = -1;
                for (int i = top; i > 0; i--) {
                    if (stack[i]->tag_name && strcasecmp(stack[i]->tag_name, token.tag_name) == 0) {
                        match = i;
                        break;
                    }
                }
                if (match > 0) {
                    top = match - 1;
                }
            }
        } else if (token.type == TOKEN_TEXT) {
            if (token.text && token.text[0]) {
                DOMNode *text_node = dom_node_create(NODE_TEXT);
                text_node->text_content = strdup(token.text);
                dom_node_append_child(stack[top], text_node);
            }
        }

        html_token_free(&token);
    }

    return doc;
}
