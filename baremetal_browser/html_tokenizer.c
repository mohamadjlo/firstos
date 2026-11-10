#include "html_tokenizer.h"
#include "bb_libc.h"

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_tag_char(char c) {
    return is_alpha(c) || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ':';
}

static bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static bool is_void_tag_str(const char *tag, size_t len) {
    static const char *void_tags[] = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr", NULL
    };
    for (int i = 0; void_tags[i]; i++) {
        if (bb_strlen(void_tags[i]) == len && bb_strncasecmp(tag, void_tags[i], len) == 0) {
            return true;
        }
    }
    return false;
}

void html_tok_init(html_tokenizer_t *t, const char *src, size_t len) {
    if (!t) return;
    t->src = src;
    t->cur = src;
    t->end = src ? src + len : NULL;
}

bool html_tok_next(html_tokenizer_t *t, html_token_t *tok) {
    if (!t || !t->cur || !t->end || t->cur >= t->end || !tok) {
        if (tok) bb_memset(tok, 0, sizeof(*tok));
        return false;
    }

    bb_memset(tok, 0, sizeof(*tok));
    const char *start = t->cur;
    tok->src_start = start;

    /* 1. Tag or Comment or DOCTYPE */
    if (*t->cur == '<') {
        const char *p = t->cur + 1;

        /* Comment <!-- ... --> */
        if (p + 2 <= t->end && p[0] == '!' && p[1] == '-' && p[2] == '-') {
            p += 3;
            tok->type = HTML_TOK_COMMENT;
            tok->text = p;
            while (p + 2 < t->end && !(p[0] == '-' && p[1] == '-' && p[2] == '>')) {
                p++;
            }
            tok->text_len = (size_t)(p - tok->text);
            if (p + 2 < t->end && p[0] == '-' && p[1] == '-' && p[2] == '>') {
                p += 3;
            } else {
                p = t->end;
            }
            t->cur = p;
            tok->src_end = t->cur;
            return true;
        }

        /* DOCTYPE <!DOCTYPE ... > */
        if (p + 7 <= t->end && (p[0] == '!' || p[0] == '?')) {
            tok->type = HTML_TOK_DOCTYPE;
            tok->text = p;
            while (p < t->end && *p != '>') {
                p++;
            }
            tok->text_len = (size_t)(p - tok->text);
            if (p < t->end && *p == '>') {
                p++;
            }
            t->cur = p;
            tok->src_end = t->cur;
            return true;
        }

        /* End tag </tag> */
        if (p < t->end && *p == '/') {
            p++;
            while (p < t->end && is_whitespace(*p)) p++;
            const char *tag_start = p;
            while (p < t->end && is_tag_char(*p)) p++;

            tok->type = HTML_TOK_END_TAG;
            tok->tag = tag_start;
            tok->tag_len = (size_t)(p - tag_start);
            if (tok->tag_len > 63) tok->tag_len = 63;

            /* Skip any trailing attributes or characters until '>' */
            while (p < t->end && *p != '>') p++;
            if (p < t->end && *p == '>') p++;

            t->cur = p;
            tok->src_end = t->cur;
            return true;
        }

        /* Check for malformed `<` with no valid tag name start */
        if (p >= t->end || (!is_alpha(*p) && *p != '/' && *p != '!' && *p != '?')) {
            /* Emit as literal TEXT token */
            tok->type = HTML_TOK_TEXT;
            tok->text = t->cur;
            tok->text_len = 1;
            t->cur++;
            tok->src_end = t->cur;
            return true;
        }

        /* Start Tag or Self-Closing Tag */
        const char *tag_start = p;
        while (p < t->end && is_tag_char(*p)) p++;

        size_t tag_len = (size_t)(p - tag_start);
        if (tag_len == 0) {
            tok->type = HTML_TOK_TEXT;
            tok->text = t->cur;
            tok->text_len = 1;
            t->cur++;
            tok->src_end = t->cur;
            return true;
        }

        if (tag_len > 63) tag_len = 63;
        tok->tag = tag_start;
        tok->tag_len = tag_len;
        tok->attr_count = 0;

        bool is_self_closing = is_void_tag_str(tok->tag, tok->tag_len);

        /* Parse Attributes */
        while (p < t->end) {
            while (p < t->end && is_whitespace(*p)) p++;
            if (p >= t->end || *p == '>' || *p == '<') break;

            if (*p == '/') {
                is_self_closing = true;
                p++;
                while (p < t->end && is_whitespace(*p)) p++;
                if (p < t->end && *p == '>') {
                    p++;
                    break;
                }
                continue;
            }

            const char *key_start = p;
            while (p < t->end && *p != '=' && *p != '>' && *p != '/' && *p != '<' && !is_whitespace(*p)) {
                p++;
            }
            size_t key_len = (size_t)(p - key_start);
            if (key_len == 0) {
                p++;
                continue;
            }

            html_attr_t attr;
            bb_memset(&attr, 0, sizeof(attr));
            attr.key = key_start;
            attr.key_len = key_len;
            attr.has_val = false;

            while (p < t->end && is_whitespace(*p)) p++;

            if (p < t->end && *p == '=') {
                p++; /* skip '=' */
                while (p < t->end && is_whitespace(*p)) p++;

                if (p < t->end) {
                    if (*p == '"' || *p == '\'') {
                        char quote = *p++;
                        attr.val = p;
                        while (p < t->end && *p != quote && *p != '>') {
                            p++;
                        }
                        attr.val_len = (size_t)(p - attr.val);
                        attr.has_val = true;
                        if (p < t->end && *p == quote) {
                            p++;
                        }
                    } else {
                        attr.val = p;
                        while (p < t->end && !is_whitespace(*p) && *p != '>' && *p != '<') {
                            p++;
                        }
                        attr.val_len = (size_t)(p - attr.val);
                        attr.has_val = true;
                    }
                }
            }

            if (tok->attr_count < HTML_TOK_MAX_ATTRS) {
                tok->attrs[tok->attr_count++] = attr;
            }
        }

        if (p < t->end && *p == '>') {
            p++;
        }

        tok->type = is_self_closing ? HTML_TOK_SELF_CLOSING : HTML_TOK_START_TAG;
        t->cur = p;
        tok->src_end = t->cur;
        return true;
    }

    /* 2. Text node */
    tok->type = HTML_TOK_TEXT;
    tok->text = t->cur;
    while (t->cur < t->end && *t->cur != '<') {
        t->cur++;
    }
    tok->text_len = (size_t)(t->cur - tok->text);
    tok->src_end = t->cur;
    return true;
}
