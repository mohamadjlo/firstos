#include "css_tokenizer.h"
#include "bb_libc.h"

static bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
}

static bool is_ident_char(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static bool is_digit(char c) {
    return (c >= '0' && c <= '9');
}

static bool is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f');
}

void css_tok_init(css_tokenizer_t *tok, const char *src, size_t len) {
    if (!tok) return;
    tok->src = src;
    tok->cur = src;
    tok->end = src ? src + len : NULL;
}

bool css_tok_next(css_tokenizer_t *tok, css_token_t *out) {
    if (!tok || !tok->cur || !tok->end || !out) return false;
    bb_memset(out, 0, sizeof(*out));

    /* Check for comments and whitespace */
    bool saw_whitespace = false;

    while (tok->cur < tok->end) {
        /* Skip CSS comment: / * ... * / */
        if (tok->cur + 1 < tok->end && tok->cur[0] == '/' && tok->cur[1] == '*') {
            tok->cur += 2;
            while (tok->cur + 1 < tok->end && !(tok->cur[0] == '*' && tok->cur[1] == '/')) {
                tok->cur++;
            }
            if (tok->cur + 1 < tok->end) {
                tok->cur += 2;
            } else {
                tok->cur = tok->end;
            }
            saw_whitespace = true;
            continue;
        }

        /* Whitespace */
        if (is_space(*tok->cur)) {
            saw_whitespace = true;
            tok->cur++;
            continue;
        }

        break;
    }

    if (saw_whitespace) {
        out->type = CSS_TOK_WHITESPACE;
        out->text = " ";
        out->len = 1;
        return true;
    }

    if (tok->cur >= tok->end) {
        out->type = CSS_TOK_EOF;
        return false;
    }

    char c = *tok->cur;

    /* 1. Single character punctuation */
    if (c == '{') {
        out->type = CSS_TOK_LBRACE;
        out->text = tok->cur++;
        out->len = 1;
        return true;
    }
    if (c == '}') {
        out->type = CSS_TOK_RBRACE;
        out->text = tok->cur++;
        out->len = 1;
        return true;
    }
    if (c == ':') {
        out->type = CSS_TOK_COLON;
        out->text = tok->cur++;
        out->len = 1;
        return true;
    }
    if (c == ';') {
        out->type = CSS_TOK_SEMICOLON;
        out->text = tok->cur++;
        out->len = 1;
        return true;
    }
    if (c == ',') {
        out->type = CSS_TOK_COMMA;
        out->text = tok->cur++;
        out->len = 1;
        return true;
    }
    if (c == '.') {
        /* Check if it's a number like .5 or a dot selector */
        if (tok->cur + 1 < tok->end && is_digit(tok->cur[1])) {
            /* Fall through to number parsing */
        } else {
            out->type = CSS_TOK_DOT;
            out->text = tok->cur++;
            out->len = 1;
            return true;
        }
    }
    if (c == '*') {
        out->type = CSS_TOK_ASTERISK;
        out->text = tok->cur++;
        out->len = 1;
        return true;
    }

    /* 2. String literal */
    if (c == '"' || c == '\'') {
        char quote = c;
        tok->cur++;
        const char *str_start = tok->cur;
        while (tok->cur < tok->end && *tok->cur != quote && *tok->cur != '\n') {
            if (*tok->cur == '\\' && tok->cur + 1 < tok->end) {
                tok->cur += 2;
            } else {
                tok->cur++;
            }
        }
        out->type = CSS_TOK_STRING;
        out->text = str_start;
        out->len = (size_t)(tok->cur - str_start);
        if (tok->cur < tok->end && *tok->cur == quote) {
            tok->cur++;
        }
        return true;
    }

    /* 3. Hash token (#id or #hexcolor) */
    if (c == '#') {
        tok->cur++;
        const char *h_start = tok->cur;
        while (tok->cur < tok->end && is_ident_char(*tok->cur)) {
            tok->cur++;
        }
        out->type = CSS_TOK_HASH;
        out->text = h_start;
        out->len = (size_t)(tok->cur - h_start);
        return true;
    }

    /* 4. Number or Dimension */
    bool is_num = is_digit(c) ||
                  ((c == '-' || c == '+') && tok->cur + 1 < tok->end && (is_digit(tok->cur[1]) || tok->cur[1] == '.')) ||
                  (c == '.' && tok->cur + 1 < tok->end && is_digit(tok->cur[1]));

    if (is_num) {
        const char *num_start = tok->cur;
        double sign = 1.0;
        if (*tok->cur == '-') {
            sign = -1.0;
            tok->cur++;
        } else if (*tok->cur == '+') {
            tok->cur++;
        }

        double integer_part = 0.0;
        while (tok->cur < tok->end && is_digit(*tok->cur)) {
            integer_part = integer_part * 10.0 + (double)(*tok->cur - '0');
            tok->cur++;
        }

        double fraction_part = 0.0;
        double div = 1.0;
        if (tok->cur < tok->end && *tok->cur == '.') {
            tok->cur++;
            while (tok->cur < tok->end && is_digit(*tok->cur)) {
                fraction_part = fraction_part * 10.0 + (double)(*tok->cur - '0');
                div *= 10.0;
                tok->cur++;
            }
        }

        out->num_val = sign * (integer_part + (fraction_part / div));
        out->text = num_start;

        /* Check for unit (% or ident chars) */
        if (tok->cur < tok->end && *tok->cur == '%') {
            out->type = CSS_TOK_DIMENSION;
            out->unit[0] = '%';
            out->unit[1] = '\0';
            tok->cur++;
        } else if (tok->cur < tok->end && is_ident_start(*tok->cur)) {
            const char *u_start = tok->cur;
            while (tok->cur < tok->end && is_ident_char(*tok->cur)) {
                tok->cur++;
            }
            out->type = CSS_TOK_DIMENSION;
            size_t ulen = (size_t)(tok->cur - u_start);
            if (ulen >= sizeof(out->unit)) ulen = sizeof(out->unit) - 1;
            bb_memcpy(out->unit, u_start, ulen);
            out->unit[ulen] = '\0';
        } else {
            out->type = CSS_TOK_NUMBER;
            out->unit[0] = '\0';
        }

        out->len = (size_t)(tok->cur - num_start);
        return true;
    }

    /* 5. Identifier */
    if (is_ident_start(c)) {
        const char *id_start = tok->cur;
        while (tok->cur < tok->end && is_ident_char(*tok->cur)) {
            tok->cur++;
        }
        out->type = CSS_TOK_IDENT;
        out->text = id_start;
        out->len = (size_t)(tok->cur - id_start);
        return true;
    }

    /* 6. Any other delimiter */
    out->type = CSS_TOK_DELIM;
    out->text = tok->cur++;
    out->len = 1;
    return true;
}
