#include "html5_script.h"
#include "html5_resources.h"
#include "serial.h"
#include "string.h"
#include "stdio.h"
#include "heap.h"
#include "http_client.h"
#include "browser_logger.h"

static char g_last_alert[128] = {0};

typedef struct {
    char name[32];
    char value[128];
} script_var_t;

#define MAX_SCRIPT_VARS 128
static script_var_t g_vars[MAX_SCRIPT_VARS];
static int g_var_count = 0;

/* Web Storage API: localStorage & sessionStorage storage pools */
#define MAX_WEB_STORAGE_ITEMS 64
typedef struct {
    char key[48];
    char val[192];
    int  used;
} web_storage_item_t;

static web_storage_item_t g_local_storage[MAX_WEB_STORAGE_ITEMS];
static web_storage_item_t g_session_storage[MAX_WEB_STORAGE_ITEMS];

static const char *storage_get(web_storage_item_t *pool, const char *key) {
    if (!pool || !key || !key[0]) return NULL;
    for (int i = 0; i < MAX_WEB_STORAGE_ITEMS; i++) {
        if (pool[i].used && strcmp(pool[i].key, key) == 0) {
            return pool[i].val;
        }
    }
    return NULL;
}

static void storage_set(web_storage_item_t *pool, const char *key, const char *val) {
    if (!pool || !key || !key[0]) return;
    for (int i = 0; i < MAX_WEB_STORAGE_ITEMS; i++) {
        if (pool[i].used && strcmp(pool[i].key, key) == 0) {
            strncpy(pool[i].val, val ? val : "", sizeof(pool[i].val) - 1);
            pool[i].val[sizeof(pool[i].val) - 1] = 0;
            return;
        }
    }
    for (int i = 0; i < MAX_WEB_STORAGE_ITEMS; i++) {
        if (!pool[i].used) {
            pool[i].used = 1;
            strncpy(pool[i].key, key, sizeof(pool[i].key) - 1);
            pool[i].key[sizeof(pool[i].key) - 1] = 0;
            strncpy(pool[i].val, val ? val : "", sizeof(pool[i].val) - 1);
            pool[i].val[sizeof(pool[i].val) - 1] = 0;
            return;
        }
    }
}

static void storage_remove(web_storage_item_t *pool, const char *key) {
    if (!pool || !key || !key[0]) return;
    for (int i = 0; i < MAX_WEB_STORAGE_ITEMS; i++) {
        if (pool[i].used && strcmp(pool[i].key, key) == 0) {
            pool[i].used = 0;
            pool[i].key[0] = 0;
            pool[i].val[0] = 0;
            return;
        }
    }
}

static void storage_clear(web_storage_item_t *pool) {
    if (!pool) return;
    memset(pool, 0, sizeof(web_storage_item_t) * MAX_WEB_STORAGE_ITEMS);
}

void html5_script_init(void) {
    g_last_alert[0] = 0;
    g_var_count = 0;
    memset(g_vars, 0, sizeof(g_vars));
    memset(g_local_storage, 0, sizeof(g_local_storage));
    memset(g_session_storage, 0, sizeof(g_session_storage));
    serial_write("[html5_script] Embedded script execution engine ready\n");
}

const char *html5_script_get_last_alert(void) {
    return g_last_alert;
}

void html5_script_clear_alert(void) {
    g_last_alert[0] = 0;
}

static const char *get_var(const char *name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < g_var_count; i++) {
        if (strcmp(g_vars[i].name, name) == 0) return g_vars[i].value;
    }
    return NULL;
}

static void set_var(const char *name, const char *val) {
    if (!name || !name[0]) return;
    for (int i = 0; i < g_var_count; i++) {
        if (strcmp(g_vars[i].name, name) == 0) {
            strncpy(g_vars[i].value, val ? val : "", sizeof(g_vars[i].value) - 1);
            g_vars[i].value[sizeof(g_vars[i].value) - 1] = 0;
            return;
        }
    }
    if (g_var_count < MAX_SCRIPT_VARS) {
        strncpy(g_vars[g_var_count].name, name, sizeof(g_vars[g_var_count].name) - 1);
        g_vars[g_var_count].name[sizeof(g_vars[g_var_count].name) - 1] = 0;
        strncpy(g_vars[g_var_count].value, val ? val : "", sizeof(g_vars[g_var_count].value) - 1);
        g_vars[g_var_count].value[sizeof(g_vars[g_var_count].value) - 1] = 0;
        g_var_count++;
    }
}

/* Helper to trim leading/trailing whitespace */
static char *trim_whitespace(char *str) {
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (!*str) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n' || *end == ';')) {
        *end = 0;
        end--;
    }
    return str;
}

/* Helper to evaluate an expression (quoted string, variable, number, concatenation) */
static void extract_expr_val(const char *in, char *out, int max_len) {
    out[0] = 0;
    if (!in || max_len <= 0) return;
    while (*in == ' ' || *in == '\t') in++;

    /* Check if quoted string */
    const char *q1 = strchr(in, '"');
    const char *q2 = NULL;
    if (q1) {
        q2 = strchr(q1 + 1, '"');
    } else {
        q1 = strchr(in, '\'');
        if (q1) q2 = strchr(q1 + 1, '\'');
    }

    if (q1 && q2 && q2 > q1) {
        int len = (int)(q2 - q1 - 1);
        if (len > max_len - 1) len = max_len - 1;
        strncpy(out, q1 + 1, len);
        out[len] = 0;

        /* Check for concatenation + */
        const char *plus = strchr(q2 + 1, '+');
        if (plus) {
            char rest[128];
            extract_expr_val(plus + 1, rest, sizeof(rest));
            if (rest[0]) {
                int cur_len = strlen(out);
                int space = max_len - 1 - cur_len;
                if (space > 0) {
                    strncat(out, rest, space);
                }
            }
        }
        return;
    }

    /* Check if localStorage.getItem(...) or sessionStorage.getItem(...) */
    if (strstr(in, "localStorage.getItem") != NULL || strstr(in, "sessionStorage.getItem") != NULL) {
        int is_local = (strstr(in, "localStorage.getItem") != NULL);
        const char *p_in = strchr(in, '(');
        if (p_in) {
            char key_buf[48] = {0};
            extract_expr_val(p_in + 1, key_buf, sizeof(key_buf));
            const char *stored = storage_get(is_local ? g_local_storage : g_session_storage, key_buf);
            if (stored) {
                strncpy(out, stored, max_len - 1);
                out[max_len - 1] = 0;
            } else {
                strncpy(out, "", max_len - 1);
                out[max_len - 1] = 0;
            }
            return;
        }
    }

    /* Otherwise, check variable or bare word/number */
    char tok[64] = {0};
    int tlen = 0;
    while (in[tlen] && in[tlen] != ' ' && in[tlen] != '\t' && in[tlen] != ';' && in[tlen] != '+' && in[tlen] != ')' && tlen < (int)sizeof(tok) - 1) {
        tok[tlen] = in[tlen];
        tlen++;
    }
    tok[tlen] = 0;

    const char *v = get_var(tok);
    if (v) {
        strncpy(out, v, max_len - 1);
        out[max_len - 1] = 0;
    } else {
        strncpy(out, tok, max_len - 1);
        out[max_len - 1] = 0;
    }

    const char *plus = strchr(in + tlen, '+');
    if (plus) {
        char rest[128];
        extract_expr_val(plus + 1, rest, sizeof(rest));
        if (rest[0]) {
            int cur_len = strlen(out);
            int space = max_len - 1 - cur_len;
            if (space > 0) {
                strncat(out, rest, space);
            }
        }
    }
}

/* Parse color string into 0xRRGGBB */
static u32 parse_script_color(const char *col, u32 fallback) {
    if (!col || !col[0]) return fallback;
    if (strstr(col, "blue")) return 0x007AFF;
    if (strstr(col, "red")) return 0xFF453A;
    if (strstr(col, "green")) return 0x30D158;
    if (strstr(col, "white")) return 0xFFFFFF;
    if (strstr(col, "black")) return 0x000000;
    if (strstr(col, "yellow")) return 0xFFD60A;
    if (strstr(col, "gray") || strstr(col, "grey")) return 0x8E8E93;
    if (col[0] == '#') {
        unsigned int val = 0;
        int i;
        for (i = 1; i < 7 && col[i]; i++) {
            char c = col[i];
            unsigned int d = (c >= '0' && c <= '9') ? (c - '0') :
                             (c >= 'a' && c <= 'f') ? (c - 'a' + 10) :
                             (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : 16;
            if (d > 15) return fallback;
            val = (val << 4) | d;
        }
        return (u32)val;
    }
    return fallback;
}

#include "http_body.h"

static int is_valid_js_identifier(const char *name) {
    if (!name || !name[0]) return 0;
    if (!((name[0] >= 'a' && name[0] <= 'z') ||
          (name[0] >= 'A' && name[0] <= 'Z') ||
          name[0] == '_' || name[0] == '$')) return 0;
    for (int i = 1; name[i]; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '$')) return 0;
    }
    return 1;
}

typedef struct {
    char name[32];
    char body[1024];
} script_func_t;

#define MAX_SCRIPT_FUNCS 64
static script_func_t g_funcs[MAX_SCRIPT_FUNCS];
static int g_func_count = 0;

static const char *get_func_body(const char *name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < g_func_count; i++) {
        if (strcmp(g_funcs[i].name, name) == 0) return g_funcs[i].body;
    }
    return NULL;
}

static void set_func(const char *name, const char *body) {
    if (!name || !name[0] || !body) return;
    for (int i = 0; i < g_func_count; i++) {
        if (strcmp(g_funcs[i].name, name) == 0) {
            strncpy(g_funcs[i].body, body, sizeof(g_funcs[i].body) - 1);
            g_funcs[i].body[sizeof(g_funcs[i].body) - 1] = 0;
            return;
        }
    }
    if (g_func_count < MAX_SCRIPT_FUNCS) {
        strncpy(g_funcs[g_func_count].name, name, sizeof(g_funcs[g_func_count].name) - 1);
        g_funcs[g_func_count].name[sizeof(g_funcs[g_func_count].name) - 1] = 0;
        strncpy(g_funcs[g_func_count].body, body, sizeof(g_funcs[g_func_count].body) - 1);
        g_funcs[g_func_count].body[sizeof(g_funcs[g_func_count].body) - 1] = 0;
        g_func_count++;
    }
}

#define MAX_SCRIPT_RECURSION_DEPTH 16
#define MAX_SCRIPT_STATEMENTS 25000

static int s_eval_depth = 0;
static int s_statement_count = 0;

/* Internal recursive evaluation with depth and budget tracking */
static void html5_script_eval_internal(const char *script_code, html_node_t *root_dom) {
    if (!script_code || !root_dom) return;

    if (s_eval_depth >= MAX_SCRIPT_RECURSION_DEPTH) {
        return;
    }
    if (s_statement_count >= MAX_SCRIPT_STATEMENTS) {
        return;
    }

    /* Verify there is actual non-whitespace executable code to run */
    const char *chk = script_code;
    while (*chk == ' ' || *chk == '\t' || *chk == '\r' || *chk == '\n' || *chk == ';') chk++;
    if (!*chk) return;

    if (s_eval_depth == 0) {
        serial_write("[html5_script] Executing script block\n");
    }

    /* Allocate buffer dynamically up to script length to avoid kernel stack overflow */
    int code_len = (int)strlen(script_code);
    char *code_buf = (char *)kmalloc((u32)code_len + 1);
    if (!code_buf) return;
    memcpy(code_buf, script_code, (u32)code_len);
    code_buf[code_len] = 0;


    /* Strip single-line (//) and multi-line comments from code_buf */
    char in_quote_char = 0;
    for (int ci = 0; ci < code_len; ci++) {
        if (!in_quote_char && (code_buf[ci] == '"' || code_buf[ci] == '\'')) {
            in_quote_char = code_buf[ci];
        } else if (in_quote_char && code_buf[ci] == in_quote_char) {
            in_quote_char = 0;
        } else if (!in_quote_char) {
            if (code_buf[ci] == '/' && ci + 1 < code_len && code_buf[ci+1] == '/') {
                code_buf[ci] = ' ';
                code_buf[ci+1] = ' ';
                ci += 2;
                while (ci < code_len && code_buf[ci] != '\n' && code_buf[ci] != '\r') {
                    code_buf[ci] = ' ';
                    ci++;
                }
            } else if (code_buf[ci] == '/' && ci + 1 < code_len && code_buf[ci+1] == '*') {
                code_buf[ci] = ' ';
                code_buf[ci+1] = ' ';
                ci += 2;
                while (ci + 1 < code_len && !(code_buf[ci] == '*' && code_buf[ci+1] == '/')) {
                    if (code_buf[ci] != '\n' && code_buf[ci] != '\r') code_buf[ci] = ' ';
                    ci++;
                }
                if (ci + 1 < code_len) {
                    code_buf[ci] = ' ';
                    code_buf[ci+1] = ' ';
                    ci++;
                }
            }
        }
    }

    char *p = code_buf;
    while (*p) {
        while (*p == ';' || *p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
        if (!*p) break;

        /* Check function declaration: function fname(...) { ... } */
        if (strncmp(p, "function ", 9) == 0) {
            const char *fn_start = p + 9;
            while (*fn_start == ' ') fn_start++;
            int fn_len = 0;
            while (fn_start[fn_len] && fn_start[fn_len] != '(' && fn_start[fn_len] != ' ' && fn_len < 31) fn_len++;
            char fn_name[32] = {0};
            strncpy(fn_name, fn_start, fn_len);
            fn_name[fn_len] = 0;

            const char *brace = strchr(fn_start, '{');
            if (brace) {
                int depth = 1;
                const char *b = brace + 1;
                while (*b && depth > 0) {
                    if (*b == '{') depth++;
                    else if (*b == '}') depth--;
                    b++;
                }
                int body_len = (depth == 0) ? (int)(b - 1 - (brace + 1)) : (int)(b - (brace + 1));
                if (body_len > 0 && body_len < 1020) {
                    char body_buf[1024] = {0};
                    strncpy(body_buf, brace + 1, body_len);
                    body_buf[body_len] = 0;
                    set_func(fn_name, body_buf);
                    serial_write("[html5_script] Registered function: ");
                    serial_write(fn_name);
                    serial_write("()\n");
                }
                p = (char *)b;
                continue;
            }
        }

        char *line = p;
        while (*p && *p != ';' && *p != '\n' && *p != '\r') p++;
        if (*p) { *p = 0; p++; }
        line = trim_whitespace(line);
        if (!line[0]) continue;

        s_statement_count++;
        if (s_statement_count >= MAX_SCRIPT_STATEMENTS) {
            serial_write("[html5_script] Script statement limit reached, halting execution\n");
            browser_log_warn(BLOG_CAT_SCRIPT, "Script execution halted: reached limit of %d statements", MAX_SCRIPT_STATEMENTS);
            break;
        }

        /* 1. console.log(...) */
        if (strncmp(line, "console.log", 11) == 0) {
            char msg[128];
            const char *paren = strchr(line, '(');
            extract_expr_val(paren ? paren + 1 : line + 11, msg, sizeof(msg));
            serial_write("[console.log] ");
            serial_write(msg);
            serial_write("\n");
            browser_log_info(BLOG_CAT_SCRIPT, "console.log: %s", msg);
        }
        /* 2. alert(...) */
        else if (strncmp(line, "alert", 5) == 0) {
            char msg[128];
            const char *paren = strchr(line, '(');
            extract_expr_val(paren ? paren + 1 : line + 5, msg, sizeof(msg));
            strncpy(g_last_alert, msg, sizeof(g_last_alert) - 1);
            g_last_alert[sizeof(g_last_alert) - 1] = 0;
            serial_write("[html5_script] Alert: ");
            serial_write(msg);
            serial_write("\n");
        }
        /* 3. document.title = ... */
        else if (strstr(line, "document.title") != NULL) {
            const char *eq = strchr(line, '=');
            if (eq) {
                char title[128];
                extract_expr_val(eq + 1, title, sizeof(title));
                if (title[0] && root_dom) {
                    html_node_t *el = html5_get_element_by_id("title");
                    if (el) {
                        html5_node_set_text(el, title);
                    }
                    html5_set_document_title(title);
                    serial_write("[html5_script] Set document.title = \"");
                    serial_write(title);
                    serial_write("\"\n");
                }
            }
        }
        /* 4. document.write(...) / document.writeln(...) */
        else if (strncmp(line, "document.write", 14) == 0) {
            const char *paren = strchr(line, '(');
            if (paren && root_dom) {
                char out_text[256];
                extract_expr_val(paren + 1, out_text, sizeof(out_text));
                if (out_text[0]) {
                    html5_node_append_text(root_dom, out_text);
                    serial_write("[html5_script] document.write: ");
                    serial_write(out_text);
                    serial_write("\n");
                }
            }
        }
        /* 5. window.location = ... / location.href = ... */
        else if (strstr(line, "window.location") != NULL || strstr(line, "location.href") != NULL) {
            const char *loc = strstr(line, "window.location");
            if (!loc) loc = strstr(line, "location.href");
            const char *eq = strchr(line, '=');
            if (loc && eq && loc < eq && *(eq + 1) != '=' && (eq == line || (*(eq - 1) != '!' && *(eq - 1) != '<' && *(eq - 1) != '>' && *(eq - 1) != '='))) {
                char nav_url[256];
                extract_expr_val(eq + 1, nav_url, sizeof(nav_url));
                if (nav_url[0] && (strncmp(nav_url, "http://", 7) == 0 ||
                                  strncmp(nav_url, "https://", 8) == 0 ||
                                  nav_url[0] == '/' ||
                                  strncmp(nav_url, "about:", 6) == 0)) {
                    html5_request_navigation(nav_url);
                    serial_write("[html5_script] Script requested navigation to: ");
                    serial_write(nav_url);
                    serial_write("\n");
                }
            }
        }
        /* 6. document.body.style manipulations */
        else if (strstr(line, "document.body.style") != NULL) {
            const char *eq = strchr(line, '=');
            if (eq) {
                char val[128];
                extract_expr_val(eq + 1, val, sizeof(val));
                html_node_t *body = html5_get_element_by_tag(HTML_TAG_BODY);
                if (body) {
                    if (strstr(line, "backgroundColor") || strstr(line, "background")) {
                        body->style.bg_color = parse_script_color(val, body->style.bg_color);
                        body->style.has_bg = 1;
                        serial_write("[html5_script] Set body.style.backgroundColor\n");
                    } else if (strstr(line, "color")) {
                        body->style.color = parse_script_color(val, body->style.color);
                        serial_write("[html5_script] Set body.style.color\n");
                    }
                }
            }
        }
        /* 7. document.getElementById(...) or document.querySelector(...) manipulations */
        else if (strstr(line, "document.getElementById") != NULL || strstr(line, "document.querySelector") != NULL) {
            int is_qs = (strstr(line, "document.querySelector") != NULL);
            const char *arg_start = strchr(line, '(');
            if (arg_start) {
                char target_sel[48];
                extract_expr_val(arg_start + 1, target_sel, sizeof(target_sel));
                html_node_t *target = NULL;
                if (!is_qs || target_sel[0] == '#') {
                    const char *id_to_find = (is_qs && target_sel[0] == '#') ? target_sel + 1 : target_sel;
                    target = html5_get_element_by_id(id_to_find);
                } else if (target_sel[0] == '.') {
                    target = html5_get_element_by_class(target_sel + 1);
                } else if (strcmp(target_sel, "body") == 0) {
                    target = html5_get_element_by_tag(HTML_TAG_BODY);
                }

                if (target) {
                    const char *eq = strchr(line, '=');
                    if (eq) {
                        char val[256];
                        extract_expr_val(eq + 1, val, sizeof(val));

                        if (strstr(line, "innerHTML") != NULL ||
                            strstr(line, "innerText") != NULL ||
                            strstr(line, "textContent") != NULL) {
                            if (val[0]) {
                                html5_node_set_text(target, val);
                                serial_write("[html5_script] Updated content: \"");
                                serial_write(val);
                                serial_write("\"\n");
                            }
                        } else if (strstr(line, "value") != NULL) {
                            html5_node_set_value(target, val);
                        } else if (strstr(line, "style.color") != NULL) {
                            target->style.color = parse_script_color(val, target->style.color);
                        } else if (strstr(line, "style.backgroundColor") != NULL || strstr(line, "style.background") != NULL) {
                            target->style.bg_color = parse_script_color(val, 0x242432);
                            target->style.has_bg = 1;
                        } else if (strstr(line, "style.display") != NULL) {
                            if (strstr(val, "none")) {
                                target->style.display_none = 1;
                                target->style.is_block = 0;
                            } else {
                                target->style.display_none = 0;
                                target->style.is_block = 1;
                            }
                        } else if (strstr(line, "style.visibility") != NULL) {
                            if (strstr(val, "hidden")) target->style.display_none = 1;
                            else target->style.display_none = 0;
                        } else if (strstr(line, "style.width") != NULL) {
                            int w = 0; sscanf(val, "%d", &w); if (w > 0) target->style.width = w;
                        } else if (strstr(line, "style.height") != NULL) {
                            int h = 0; sscanf(val, "%d", &h); if (h > 0) target->style.height = h;
                        }
                    } else if (strstr(line, "classList.add") != NULL) {
                        const char *cl_start = strstr(line, "classList.add");
                        const char *p1 = strchr(cl_start, '(');
                        if (p1) {
                            char new_cls[64];
                            extract_expr_val(p1 + 1, new_cls, sizeof(new_cls));
                            if (new_cls[0]) {
                                char combined[192];
                                if (target->class_name && target->class_name[0]) {
                                    sprintf(combined, "%s %s", target->class_name, new_cls);
                                } else {
                                    sprintf(combined, "%s", new_cls);
                                }
                                target->class_name = html5_arena_strdup(html5_engine_get_arena(), combined);
                            }
                        }
                    } else if (strstr(line, "classList.remove") != NULL) {
                        /* classList.remove support */
                        const char *cl_start = strstr(line, "classList.remove");
                        const char *p1 = strchr(cl_start, '(');
                        if (p1 && target->class_name) {
                            char rem_cls[64];
                            extract_expr_val(p1 + 1, rem_cls, sizeof(rem_cls));
                            char *found = strstr((char*)target->class_name, rem_cls);
                            if (found) {
                                int rlen = strlen(rem_cls);
                                while (found[rlen]) { *found = found[rlen]; found++; }
                                *found = '\0';
                            }
                        }
                    } else if (strstr(line, "classList.toggle") != NULL) {
                        /* Toggle visibility on classList change */
                        target->style.display_none = !target->style.display_none;
                    } else if (strstr(line, "addEventListener") != NULL) {
                        /* addEventListener("click"|"load", handler) basic hook */
                        serial_write("[html5_script] addEventListener registered\n");
                    }
                }
            }
        }
        /* 8. Web Storage Statements: localStorage / sessionStorage */
        else if (strstr(line, "localStorage.setItem") != NULL || strstr(line, "sessionStorage.setItem") != NULL) {
            int is_local = (strstr(line, "localStorage.setItem") != NULL);
            const char *paren = strchr(line, '(');
            if (paren) {
                char k[48] = {0}, v[192] = {0};
                const char *comma = strchr(paren + 1, ',');
                if (comma) {
                    int kl = (int)(comma - (paren + 1));
                    char k_raw[64] = {0};
                    if (kl > 0 && kl < 63) { strncpy(k_raw, paren + 1, kl); k_raw[kl] = 0; }
                    extract_expr_val(k_raw, k, sizeof(k));
                    extract_expr_val(comma + 1, v, sizeof(v));
                    storage_set(is_local ? g_local_storage : g_session_storage, k, v);
                    serial_write("[html5_script] Web Storage setItem key=\"");
                    serial_write(k);
                    serial_write("\"\n");
                }
            }
        }
        else if (strstr(line, "localStorage.removeItem") != NULL || strstr(line, "sessionStorage.removeItem") != NULL) {
            int is_local = (strstr(line, "localStorage.removeItem") != NULL);
            const char *paren = strchr(line, '(');
            if (paren) {
                char k[48] = {0};
                extract_expr_val(paren + 1, k, sizeof(k));
                storage_remove(is_local ? g_local_storage : g_session_storage, k);
                serial_write("[html5_script] Web Storage removeItem key=\"");
                serial_write(k);
                serial_write("\"\n");
            }
        }
        else if (strstr(line, "localStorage.clear") != NULL || strstr(line, "sessionStorage.clear") != NULL) {
            int is_local = (strstr(line, "localStorage.clear") != NULL);
            storage_clear(is_local ? g_local_storage : g_session_storage);
            serial_write("[html5_script] Web Storage clear\n");
        }
        /* 9. Timers & Promises: setTimeout(..., N) or clearTimeout(...) or Promise.resolve().then(...) */
        else if (strstr(line, "clearTimeout") != NULL || strstr(line, "clearInterval") != NULL) {
            serial_write("[html5_script] Timer cancelled\n");
        }
        else if (strstr(line, "setTimeout") != NULL || strstr(line, "setInterval") != NULL) {
            const char *paren = strchr(line, '(');
            if (paren) {
                char fn_to_run[64] = {0};
                int fi = 0;
                const char *fp = paren + 1;
                while (*fp == ' ' || *fp == '\'' || *fp == '"') fp++;
                while (*fp && *fp != '(' && *fp != ')' && *fp != ',' && *fp != '\'' && *fp != '"' && fi < 63) {
                    fn_to_run[fi++] = *fp++;
                }
                fn_to_run[fi] = 0;
                if (fn_to_run[0]) {
                    const char *body = get_func_body(fn_to_run);
                    if (body) {
                        html5_script_queue_deferred(body, root_dom);
                    }
                }
                serial_write("[html5_script] Timer registered\n");
            }
        }
        else if (strstr(line, "Promise.resolve") != NULL || strstr(line, "queueMicrotask") != NULL) {
            serial_write("[html5_script] Microtask registered via Promise/queueMicrotask\n");
            html5_script_queue_deferred("/* microtask tick */", root_dom);
        }
        /* 9. Variable declarations: var x = ... or let x = ... or const x = ... */
        else if (strncmp(line, "var ", 4) == 0 || strncmp(line, "let ", 4) == 0 || strncmp(line, "const ", 6) == 0) {
            const char *eq = strchr(line, '=');
            if (eq) {
                const char *name_start = strchr(line, ' ') + 1;
                while (*name_start == ' ') name_start++;
                int nlen = 0;
                while (name_start[nlen] && name_start[nlen] != ' ' && name_start[nlen] != '=' && nlen < 31) nlen++;
                char var_name[32] = {0};
                strncpy(var_name, name_start, nlen);
                var_name[nlen] = 0;

                if (!is_valid_js_identifier(var_name)) {
                    continue;
                }

                if (strstr(eq + 1, "function") || strchr(eq + 1, '{') || strchr(eq + 1, '}') || strchr(eq + 1, ',')) {
                    continue;
                }

                char val[128];
                extract_expr_val(eq + 1, val, sizeof(val));
                set_var(var_name, val);
                /* Quiet verbose var logging to avoid serial port stalls */
            }
        }
        /* 9. Standalone function call: funcName() */
        else if (strchr(line, '(') != NULL && strchr(line, ')') != NULL) {
            char fname[32] = {0};
            int flen = 0;
            while (line[flen] && line[flen] != '(' && line[flen] != ' ' && flen < 31) {
                fname[flen] = line[flen];
                flen++;
            }
            fname[flen] = 0;
            if (is_valid_js_identifier(fname)) {
                const char *body = get_func_body(fname);
                if (body && s_eval_depth < MAX_SCRIPT_RECURSION_DEPTH && s_statement_count < MAX_SCRIPT_STATEMENTS) {
                    s_eval_depth++;
                    html5_script_eval_internal(body, root_dom);
                    s_eval_depth--;
                }
            }
        }
    }
    kfree(code_buf);
}

void html5_script_eval(const char *script_code, html_node_t *root_dom) {
    if (!script_code || !root_dom) return;
    s_eval_depth = 0;
    s_statement_count = 0;
    html5_script_eval_internal(script_code, root_dom);
}

#define MAX_EXECUTED_SCRIPTS 64
#define MAX_EXECUTED_SCRIPT_URL_LEN 512
static char s_executed_script_urls[MAX_EXECUTED_SCRIPTS][MAX_EXECUTED_SCRIPT_URL_LEN];
static int s_executed_script_count = 0;
static int g_script_fetch_count = 0;

void html5_script_reset(void) {
    s_executed_script_count = 0;
    g_script_fetch_count = 0;
}

static void walk_and_run_scripts(html_node_t *node, html_node_t *root, const char *base_url) {
    if (!node) return;

    if (node->tag == HTML_TAG_SCRIPT) {
        if (node->script_executed) {
            /* Duplicate script execution suppressed */
            walk_and_run_scripts(node->first_child, root, base_url);
            walk_and_run_scripts(node->next_sibling, root, base_url);
            return;
        }
        node->script_executed = 1;

        if (node->src[0]) {
            /* Check if this external script URL was already executed */
            int already_run = 0;
            for (int i = 0; i < s_executed_script_count; i++) {
                if (strcmp(s_executed_script_urls[i], node->src) == 0) {
                    already_run = 1;
                    break;
                }
            }

            if (already_run) {
                /* Silently ignore already executed script to prevent serial port flooding */
            } else {
                if (s_executed_script_count < MAX_EXECUTED_SCRIPTS) {
                    strncpy(s_executed_script_urls[s_executed_script_count], node->src, MAX_EXECUTED_SCRIPT_URL_LEN - 1);
                    s_executed_script_urls[s_executed_script_count][MAX_EXECUTED_SCRIPT_URL_LEN - 1] = 0;
                    s_executed_script_count++;
                }

                if (strncmp(node->src, "data:", 5) == 0) {
                    /* Decode and evaluate data: script directly */
                    http_body_t body;
                    http_get_body(node->src, &body, HTTP_BODY_MAX_JS);
                    if (!resource_is_executable(&body)) {
                        serial_write("[html5_script] skipped non-executable data: script\n");
                    } else {
                        html5_script_eval((const char*)body.data, root);
                    }
                    http_body_free(&body);
                } else if (strncmp(node->src, "javascript:", 11) != 0 && g_script_fetch_count < 16) {
                    static char s_resolved_url[MAX_RESOURCE_URL_LEN];
                    html5_url_resolve(base_url, node->src, s_resolved_url, sizeof(s_resolved_url));

                    serial_write("[html5_script] Fetching external script: ");
                    serial_write(s_resolved_url);
                    serial_write("\n");

                    if (strncmp(s_resolved_url, "http://", 7) == 0 || strncmp(s_resolved_url, "https://", 8) == 0) {
                        g_script_fetch_count++;
                        http_body_t body;
                        http_get_body(s_resolved_url, &body, HTTP_BODY_MAX_JS);

                        if (!resource_is_executable(&body)) {
                            serial_write("[html5_script] skipped non-executable script: ");
                            serial_write(s_resolved_url);
                            serial_write(" state=");
                            serial_write(resource_state_name(body.state));
                            serial_write(" status=");
                            serial_write_hex((u32)body.http_status);
                            serial_write(" received=");
                            serial_write_hex(body.length);
                            serial_write("\n");
                            browser_log_warn(BLOG_CAT_SCRIPT, "Script failed to load or non-executable (HTTP %d): %s", body.http_status, s_resolved_url);
                        } else {
                            const char *sdata = (const char*)body.data;
                            if (body.length > 1048576) {
                                serial_write("[html5_script] Skipping ultra-heavy script (>1MB): ");
                                serial_write(s_resolved_url);
                                serial_write("\n");
                                browser_log_warn(BLOG_CAT_SCRIPT, "Skipping ultra-heavy script library (%u bytes): %s", body.length, s_resolved_url);
                            } else {
                                browser_log_info(BLOG_CAT_SCRIPT, "Evaluating external script (%u bytes): %s", body.length, s_resolved_url);
                                html5_script_eval(sdata, root);
                            }
                        }
                        http_body_free(&body);
                    }
                }
            }
        } else if (node->text && node->text[0]) {
            if (node->text_len > 1048576 || strlen(node->text) > 1048576) {
                serial_write("[html5_script] Skipping oversized inline script block (>1MB)\n");
                browser_log_warn(BLOG_CAT_SCRIPT, "Skipping oversized inline script block (>1MB)");
            } else {
                html5_script_eval(node->text, root);
            }
        }
    }

    walk_and_run_scripts(node->first_child, root, base_url);
    walk_and_run_scripts(node->next_sibling, root, base_url);
}

/* =========================================================================
 * Microtask Queue & Script Event Loop
 * ========================================================================= */
typedef struct {
    html5_task_fn_t fn;
    void *arg;
    char desc[48];
} microtask_item_t;

typedef struct {
    char script[512];
    html_node_t *root_dom;
    int valid;
} deferred_script_item_t;

#define MAX_MICRO_TASKS 32
#define MAX_DEFERRED_SCRIPTS 16

static microtask_item_t s_microtasks[MAX_MICRO_TASKS];
static int s_microtask_head = 0;
static int s_microtask_tail = 0;
static int s_microtask_count = 0;

static deferred_script_item_t s_deferred_scripts[MAX_DEFERRED_SCRIPTS];
static int s_deferred_count = 0;

int html5_script_queue_microtask(html5_task_fn_t fn, void *arg, const char *desc) {
    if (!fn || s_microtask_count >= MAX_MICRO_TASKS) return 0;
    microtask_item_t *item = &s_microtasks[s_microtask_tail];
    item->fn = fn;
    item->arg = arg;
    strncpy(item->desc, desc ? desc : "anonymous", sizeof(item->desc) - 1);
    item->desc[sizeof(item->desc) - 1] = 0;
    s_microtask_tail = (s_microtask_tail + 1) % MAX_MICRO_TASKS;
    s_microtask_count++;
    serial_write("[js] microtask queued: ");
    serial_write(item->desc);
    serial_write("\n");
    return 1;
}

int html5_script_queue_deferred(const char *script_code, html_node_t *root_dom) {
    if (!script_code || !script_code[0] || s_deferred_count >= MAX_DEFERRED_SCRIPTS) return 0;
    deferred_script_item_t *item = &s_deferred_scripts[s_deferred_count++];
    strncpy(item->script, script_code, sizeof(item->script) - 1);
    item->script[sizeof(item->script) - 1] = 0;
    item->root_dom = root_dom;
    item->valid = 1;
    serial_write("[js] deferred script queued\n");
    return 1;
}

void html5_script_drain_microtasks(void) {
    if (s_microtask_count == 0) return;
    int drained = 0;
    while (s_microtask_count > 0 && drained < MAX_MICRO_TASKS) {
        microtask_item_t item = s_microtasks[s_microtask_head];
        s_microtask_head = (s_microtask_head + 1) % MAX_MICRO_TASKS;
        s_microtask_count--;
        drained++;
        if (item.fn) {
            item.fn(item.arg);
        }
    }
    serial_write("[js] microtask drained\n");
}

void html5_script_event_loop_tick(void) {
    /* 1. Run any deferred scripts */
    if (s_deferred_count > 0) {
        for (int i = 0; i < s_deferred_count; i++) {
            if (s_deferred_scripts[i].valid) {
                serial_write("[js] deferred script run\n");
                html5_script_eval(s_deferred_scripts[i].script, s_deferred_scripts[i].root_dom);
                s_deferred_scripts[i].valid = 0;
            }
        }
        s_deferred_count = 0;
    }

    /* 2. Drain all pending microtasks */
    if (s_microtask_count > 0) {
        html5_script_drain_microtasks();
    }

    /* 3. Event loop tick checkpoint */
    static int s_tick_counter = 0;
    if ((++s_tick_counter % 30) == 0) {
        serial_write("[js] event loop tick\n");
    }
}

void html5_script_run_dom_scripts(html_node_t *root_dom, const char *base_url) {
    if (!root_dom) return;
    html5_script_reset();
    walk_and_run_scripts(root_dom, root_dom, base_url);
    /* Process immediate microtasks queued by initial evaluation */
    html5_script_drain_microtasks();
}
