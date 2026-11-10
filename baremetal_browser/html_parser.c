#include "html_parser.h"
#include "html_tokenizer.h"
#include "html_entities.h"
#include "bb_libc.h"

#define MAX_PARSER_STACK 256

static bool is_void_tag(const char *tag) {
    if (!tag) return false;
    return (bb_strcasecmp(tag, "area") == 0 ||
            bb_strcasecmp(tag, "base") == 0 ||
            bb_strcasecmp(tag, "br") == 0 ||
            bb_strcasecmp(tag, "col") == 0 ||
            bb_strcasecmp(tag, "embed") == 0 ||
            bb_strcasecmp(tag, "hr") == 0 ||
            bb_strcasecmp(tag, "img") == 0 ||
            bb_strcasecmp(tag, "input") == 0 ||
            bb_strcasecmp(tag, "link") == 0 ||
            bb_strcasecmp(tag, "meta") == 0 ||
            bb_strcasecmp(tag, "param") == 0 ||
            bb_strcasecmp(tag, "source") == 0 ||
            bb_strcasecmp(tag, "track") == 0 ||
            bb_strcasecmp(tag, "wbr") == 0);
}

static bool should_auto_close(const char *open_tag, const char *new_tag) {
    if (!open_tag || !new_tag) return false;

    if (bb_strcasecmp(open_tag, "p") == 0) {
        if (bb_strcasecmp(new_tag, "p") == 0 ||
            bb_strcasecmp(new_tag, "div") == 0 ||
            bb_strcasecmp(new_tag, "h1") == 0 ||
            bb_strcasecmp(new_tag, "h2") == 0 ||
            bb_strcasecmp(new_tag, "h3") == 0 ||
            bb_strcasecmp(new_tag, "h4") == 0 ||
            bb_strcasecmp(new_tag, "h5") == 0 ||
            bb_strcasecmp(new_tag, "h6") == 0 ||
            bb_strcasecmp(new_tag, "ul") == 0 ||
            bb_strcasecmp(new_tag, "ol") == 0 ||
            bb_strcasecmp(new_tag, "li") == 0 ||
            bb_strcasecmp(new_tag, "table") == 0 ||
            bb_strcasecmp(new_tag, "blockquote") == 0 ||
            bb_strcasecmp(new_tag, "pre") == 0 ||
            bb_strcasecmp(new_tag, "section") == 0 ||
            bb_strcasecmp(new_tag, "article") == 0) {
            return true;
        }
    }

    if (bb_strcasecmp(open_tag, "li") == 0) {
        if (bb_strcasecmp(new_tag, "li") == 0) {
            return true;
        }
    }

    if (bb_strcasecmp(open_tag, "dt") == 0 || bb_strcasecmp(open_tag, "dd") == 0) {
        if (bb_strcasecmp(new_tag, "dt") == 0 || bb_strcasecmp(new_tag, "dd") == 0) {
            return true;
        }
    }

    if (bb_strcasecmp(open_tag, "td") == 0 || bb_strcasecmp(open_tag, "th") == 0) {
        if (bb_strcasecmp(new_tag, "td") == 0 || bb_strcasecmp(new_tag, "th") == 0 ||
            bb_strcasecmp(new_tag, "tr") == 0) {
            return true;
        }
    }

    if (bb_strcasecmp(open_tag, "tr") == 0) {
        if (bb_strcasecmp(new_tag, "tr") == 0) {
            return true;
        }
    }

    return false;
}

static bool is_in_invisible_context(bb_node_t **stack, int stack_top) {
    for (int s = stack_top; s > 0; s--) {
        if (stack[s]->tag_name) {
            if (bb_strcasecmp(stack[s]->tag_name, "head") == 0 ||
                bb_strcasecmp(stack[s]->tag_name, "style") == 0 ||
                bb_strcasecmp(stack[s]->tag_name, "script") == 0 ||
                bb_strcasecmp(stack[s]->tag_name, "title") == 0) {
                return true;
            }
        }
    }
    return false;
}

static bool is_in_pre_context(bb_node_t **stack, int stack_top) {
    for (int s = stack_top; s > 0; s--) {
        if (stack[s]->tag_name && bb_strcasecmp(stack[s]->tag_name, "pre") == 0) {
            return true;
        }
    }
    return false;
}

bb_node_t *html_parse(const char *src, size_t len) {
    if (!src) return NULL;

    bb_node_t *doc = bb_node_create(BB_NODE_DOCUMENT);
    if (!doc) return NULL;

    bb_node_t *stack[MAX_PARSER_STACK];
    int stack_top = 0;
    stack[stack_top] = doc;

    html_tokenizer_t tok;
    html_tok_init(&tok, src, len);

    html_token_t t;
    while (html_tok_next(&tok, &t)) {
        if (t.type == HTML_TOK_DOCTYPE || t.type == HTML_TOK_EOF) {
            continue;
        }

        if (t.type == HTML_TOK_COMMENT) {
            if (t.text && t.text_len > 0) {
                char *comment_text = bb_strndup(t.text, t.text_len);
                if (comment_text) {
                    bb_node_t *cn = bb_node_create(BB_NODE_COMMENT);
                    if (cn) {
                        cn->text_content = comment_text;
                        bb_node_append_child(stack[stack_top], cn);
                    } else {
                        bb_free(comment_text);
                    }
                }
            }
            continue;
        }

        if (t.type == HTML_TOK_TEXT) {
            if (is_in_invisible_context(stack, stack_top)) {
                continue;
            }

            bool in_pre = is_in_pre_context(stack, stack_top);

            /* Check if text contains non-whitespace */
            bool has_non_space = false;
            for (size_t i = 0; i < t.text_len; i++) {
                char c = t.text[i];
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\f') {
                    has_non_space = true;
                    break;
                }
            }

            if (!has_non_space && !in_pre) {
                continue;
            }

            char *decoded = html_entities_decode_alloc(t.text, t.text_len);
            if (decoded) {
                bb_node_t *tn = bb_node_create_text(decoded);
                if (tn) {
                    bb_node_append_child(stack[stack_top], tn);
                }
                bb_free(decoded);
            }
            continue;
        }

        if (t.type == HTML_TOK_START_TAG || t.type == HTML_TOK_SELF_CLOSING) {
            if (t.tag_len == 0) continue;

            char tag_buf[64];
            size_t tlen = t.tag_len < sizeof(tag_buf) - 1 ? t.tag_len : sizeof(tag_buf) - 1;
            bb_memcpy(tag_buf, t.tag, tlen);
            tag_buf[tlen] = '\0';

            /* Auto-close open elements */
            while (stack_top > 0 && should_auto_close(stack[stack_top]->tag_name, tag_buf)) {
                stack_top--;
            }

            bb_node_t *elem = bb_node_create_element(tag_buf);
            if (!elem) continue;

            /* Parse and set attributes */
            for (int i = 0; i < t.attr_count; i++) {
                char key_buf[64];
                size_t klen = t.attrs[i].key_len < sizeof(key_buf) - 1 ? t.attrs[i].key_len : sizeof(key_buf) - 1;
                bb_memcpy(key_buf, t.attrs[i].key, klen);
                key_buf[klen] = '\0';
                for (size_t k = 0; k < klen; k++) {
                    if (key_buf[k] >= 'A' && key_buf[k] <= 'Z') {
                        key_buf[k] += ('a' - 'A');
                    }
                }

                if (t.attrs[i].has_val) {
                    char *val_dec = html_entities_decode_alloc(t.attrs[i].val, t.attrs[i].val_len);
                    bb_node_set_attribute(elem, key_buf, val_dec ? val_dec : "");
                    if (val_dec) bb_free(val_dec);
                } else {
                    bb_node_set_attribute(elem, key_buf, key_buf);
                }
            }

            /* Raw text elements: <style> and <script> */
            if (bb_strcasecmp(tag_buf, "style") == 0 || bb_strcasecmp(tag_buf, "script") == 0) {
                bb_node_append_child(stack[stack_top], elem);

                const char *p = tok.cur;
                char end_marker[16];
                bb_snprintf(end_marker, sizeof(end_marker), "</%s", tag_buf);
                size_t em_len = bb_strlen(end_marker);
                const char *raw_start = p;

                while (p < tok.end) {
                    if (p + em_len <= tok.end && bb_strncasecmp(p, end_marker, em_len) == 0) {
                        break;
                    }
                    p++;
                }

                size_t raw_len = (size_t)(p - raw_start);
                if (raw_len > 0) {
                    char *raw_text = bb_strndup(raw_start, raw_len);
                    if (raw_text) {
                        bb_node_t *comment_node = bb_node_create(BB_NODE_COMMENT);
                        if (comment_node) {
                            comment_node->text_content = raw_text;
                            bb_node_append_child(elem, comment_node);
                        } else {
                            bb_free(raw_text);
                        }
                    }
                }

                if (p < tok.end) {
                    p += em_len;
                    while (p < tok.end && *p != '>') p++;
                    if (p < tok.end && *p == '>') p++;
                }
                tok.cur = p;
                continue;
            }

            bb_node_append_child(stack[stack_top], elem);

            bool is_self = (t.type == HTML_TOK_SELF_CLOSING) || is_void_tag(tag_buf);
            if (!is_self && stack_top + 1 < MAX_PARSER_STACK) {
                stack[++stack_top] = elem;
            }
            continue;
        }

        if (t.type == HTML_TOK_END_TAG) {
            if (t.tag_len == 0) continue;

            char close_buf[64];
            size_t clen = t.tag_len < sizeof(close_buf) - 1 ? t.tag_len : sizeof(close_buf) - 1;
            bb_memcpy(close_buf, t.tag, clen);
            close_buf[clen] = '\0';

            int match_idx = -1;
            for (int s = stack_top; s > 0; s--) {
                if (stack[s]->tag_name && bb_strcasecmp(stack[s]->tag_name, close_buf) == 0) {
                    match_idx = s;
                    break;
                }
            }
            if (match_idx > 0) {
                stack_top = match_idx - 1;
            }
            continue;
        }
    }

    return doc;
}
