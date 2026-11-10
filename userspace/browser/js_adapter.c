#include "include/firstos_renderer.h"

#define MAX_SCRIPT_VARS 32
#define MAX_SCRIPT_FUNCS 16
#define MAX_EVENT_LISTENERS 16
#define MAX_INSTRUCTIONS_PER_TASK 1000
#define MAX_CALL_DEPTH 8
#define MAX_STRING_LEN 256

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(__KERNEL__) && !defined(_FIRSTOS_USERSPACE)
extern int printf(const char *format, ...);
static void js_print(const char *msg) {
    if (msg) printf("%s", msg);
}
#else
#include "../../include/firstos/syscall.h"
static void js_print(const char *msg) {
    if (msg) firstos_write(1, msg, (unsigned int)strlen(msg));
}
#endif

typedef struct {
    char name[32];
    char value[128];
} js_var_t;

typedef struct {
    char name[32];
    char body[512];
} js_func_t;

typedef struct {
    int  node_id;
    char event_name[16];
    char func_name[32];
    char inline_body[256];
} js_listener_t;

static js_var_t      s_vars[MAX_SCRIPT_VARS];
static int           s_var_count = 0;
static js_func_t     s_funcs[MAX_SCRIPT_FUNCS];
static int           s_func_count = 0;
static js_listener_t s_listeners[MAX_EVENT_LISTENERS];
static int           s_listener_count = 0;
static char          s_last_alert[128];
static int           s_instruction_count = 0;
static int           s_call_depth = 0;

static firstos_renderer_context_t *s_cur_ctx = NULL;

/* Phase 5C: window-level listener sentinel and bounded postMessage outbox */
#define JS_WINDOW_NODE_ID (-1)
#define JS_OUTBOX_MAX 8

typedef struct {
    int  target_kind;                 /* 0=self, 1=parent, 2=top */
    char data[MAX_STRING_LEN];
    char target_origin[128];
} js_outbound_msg_t;

static js_outbound_msg_t s_outbox[JS_OUTBOX_MAX];
static int s_outbox_head = 0;
static int s_outbox_count = 0;
static int s_outbox_dropped = 0;

static void js_copy_bounded(char *dst, const char *src, int max) {
    int l = 0;
    if (!src) src = "";
    while (src[l] && l < max - 1) { dst[l] = src[l]; l++; }
    dst[l] = '\0';
}

static void js_queue_outbound_message(int target_kind, const char *data, const char *torigin) {
    if (s_outbox_count >= JS_OUTBOX_MAX) { s_outbox_dropped++; return; }
    int idx = (s_outbox_head + s_outbox_count) % JS_OUTBOX_MAX;
    s_outbox[idx].target_kind = target_kind;
    js_copy_bounded(s_outbox[idx].data, data, MAX_STRING_LEN);
    js_copy_bounded(s_outbox[idx].target_origin, torigin, 128);
    s_outbox_count++;
}

int firstos_js_take_outbound_message(int *out_target_kind, char *out_data, int data_max,
                                     char *out_target_origin, int origin_max) {
    if (s_outbox_count <= 0) return 0;
    js_outbound_msg_t *m = &s_outbox[s_outbox_head];
    if (out_target_kind) *out_target_kind = m->target_kind;
    if (out_data && data_max > 0) js_copy_bounded(out_data, m->data, data_max);
    if (out_target_origin && origin_max > 0) js_copy_bounded(out_target_origin, m->target_origin, origin_max);
    s_outbox_head = (s_outbox_head + 1) % JS_OUTBOX_MAX;
    s_outbox_count--;
    return 1;
}

int firstos_js_outbound_dropped(void) { return s_outbox_dropped; }
int firstos_js_get_instruction_count(void) { return s_instruction_count; }
int firstos_js_get_call_depth(void) { return s_call_depth; }

void firstos_js_init(void) {
    s_outbox_head = 0; s_outbox_count = 0; s_outbox_dropped = 0;
    s_var_count = 0;
    s_func_count = 0;
    s_listener_count = 0;
    s_last_alert[0] = '\0';
    s_instruction_count = 0;
    s_call_depth = 0;
    s_cur_ctx = NULL;
}

void firstos_js_reset(void) {
    s_outbox_head = 0; s_outbox_count = 0; s_outbox_dropped = 0;
    s_var_count = 0;
    s_func_count = 0;
    s_listener_count = 0;
    s_last_alert[0] = '\0';
    s_instruction_count = 0;
    s_call_depth = 0;
    s_cur_ctx = NULL;
}

static const char *get_var(const char *name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < s_var_count; i++) {
        if (strcmp(s_vars[i].name, name) == 0) return s_vars[i].value;
    }
    return NULL;
}

static void set_var(const char *name, const char *val) {
    if (!name || !name[0]) return;
    for (int i = 0; i < s_var_count; i++) {
        if (strcmp(s_vars[i].name, name) == 0) {
            int l = 0;
            const char *src = val ? val : "";
            while (src[l] && l < 127) { s_vars[i].value[l] = src[l]; l++; }
            s_vars[i].value[l] = '\0';
            return;
        }
    }
    if (s_var_count < MAX_SCRIPT_VARS) {
        int nl = 0;
        while (name[nl] && nl < 31) { s_vars[s_var_count].name[nl] = name[nl]; nl++; }
        s_vars[s_var_count].name[nl] = '\0';

        int vl = 0;
        const char *src = val ? val : "";
        while (src[vl] && vl < 127) { s_vars[s_var_count].value[vl] = src[vl]; vl++; }
        s_vars[s_var_count].value[vl] = '\0';
        s_var_count++;
    }
}

static const char *get_func_body(const char *name) {
    if (!name || !name[0]) return NULL;
    for (int i = 0; i < s_func_count; i++) {
        if (strcmp(s_funcs[i].name, name) == 0) return s_funcs[i].body;
    }
    return NULL;
}

static void set_func(const char *name, const char *body) {
    if (!name || !name[0] || !body) return;
    for (int i = 0; i < s_func_count; i++) {
        if (strcmp(s_funcs[i].name, name) == 0) {
            int l = 0;
            while (body[l] && l < 511) { s_funcs[i].body[l] = body[l]; l++; }
            s_funcs[i].body[l] = '\0';
            return;
        }
    }
    if (s_func_count < MAX_SCRIPT_FUNCS) {
        int nl = 0;
        while (name[nl] && nl < 31) { s_funcs[s_func_count].name[nl] = name[nl]; nl++; }
        s_funcs[s_func_count].name[nl] = '\0';

        int bl = 0;
        while (body[bl] && bl < 511) { s_funcs[s_func_count].body[bl] = body[bl]; bl++; }
        s_funcs[s_func_count].body[bl] = '\0';
        s_func_count++;
    }
}

static char *trim_str(char *str) {
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') str++;
    if (!*str) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n' || *end == ';')) {
        *end = '\0';
        end--;
    }
    return str;
}

static int is_numeric_str(const char *s) {
    if (!s || !s[0]) return 0;
    int i = 0;
    if (s[0] == '-' || s[0] == '+') i++;
    if (!s[i]) return 0;
    while (s[i]) {
        if (s[i] < '0' || s[i] > '9') return 0;
        i++;
    }
    return 1;
}

static int str_to_int(const char *s) {
    int res = 0;
    int sign = 1;
    int i = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (s[i] == '-') { sign = -1; i++; }
    else if (s[i] == '+') i++;
    while (s[i] >= '0' && s[i] <= '9') {
        res = res * 10 + (s[i] - '0');
        i++;
    }
    return res * sign;
}

static void int_to_str(int v, char *out, int max_len) {
    if (!out || max_len <= 1) return;
    int idx = 0;
    if (v == 0) {
        out[0] = '0'; out[1] = '\0';
        return;
    }
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    char tmp[16];
    int t = 0;
    while (v > 0) {
        tmp[t++] = '0' + (v % 10);
        v /= 10;
    }
    if (neg && idx < max_len - 1) out[idx++] = '-';
    while (t > 0 && idx < max_len - 1) {
        out[idx++] = tmp[--t];
    }
    out[idx] = '\0';
}

static void extract_expr_val(const char *in, char *out, int max_len);

static uint32_t parse_js_color(const char *col, uint32_t fallback) {
    if (!col || !col[0]) return fallback;
    if (strstr(col, "blue")) return 0x007AFF;
    if (strstr(col, "red")) return 0xFF453A;
    if (strstr(col, "green")) return 0x30D158;
    if (strstr(col, "white")) return 0xFFFFFF;
    if (strstr(col, "black")) return 0x000000;
    if (strstr(col, "gray") || strstr(col, "grey")) return 0x8E8E93;
    if (col[0] == '#') {
        unsigned int val = 0;
        for (int i = 1; i < 7 && col[i]; i++) {
            char c = col[i];
            unsigned int d = (c >= '0' && c <= '9') ? (c - '0') :
                             (c >= 'a' && c <= 'f') ? (c - 'a' + 10) :
                             (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : 16;
            if (d > 15) return fallback;
            val = (val << 4) | d;
        }
        return (uint32_t)val;
    }
    return fallback;
}

static void extract_expr_val(const char *in, char *out, int max_len) {
    out[0] = '\0';
    if (!in || max_len <= 0) return;
    while (*in == ' ' || *in == '\t') in++;

    /* 1. DOM bridge property read: document.getElementById('id').value / .textContent */
    if (strncmp(in, "document.getElementById", 23) == 0) {
        const char *p1 = strchr(in, '(');
        if (p1) {
            char id_buf[64];
            extract_expr_val(p1 + 1, id_buf, sizeof(id_buf));
            const char *p2 = strchr(p1, ')');
            if (p2 && s_cur_ctx) {
                html_node_t *node = firstos_renderer_get_element_by_id(s_cur_ctx, id_buf);
                if (node) {
                    if (strncmp(p2 + 1, ".value", 6) == 0) {
                        int l = 0;
                        while (node->value[l] && l < max_len - 1) { out[l] = node->value[l]; l++; }
                        out[l] = '\0';
                        return;
                    } else if (strncmp(p2 + 1, ".textContent", 12) == 0 || strncmp(p2 + 1, ".innerText", 10) == 0) {
                        const char *t = node->text ? node->text : "";
                        int l = 0;
                        while (t[l] && l < max_len - 1) { out[l] = t[l]; l++; }
                        out[l] = '\0';
                        return;
                    } else if (strncmp(p2 + 1, ".checked", 8) == 0) {
                        if (node->checked) { out[0] = '1'; out[1] = '\0'; }
                        else { out[0] = '0'; out[1] = '\0'; }
                        return;
                    }
                }
            }
        }
    }

    /* 2. Quoted String */
    if (*in == '"' || *in == '\'') {
        char quote = *in;
        const char *q2 = strchr(in + 1, quote);
        if (q2) {
            int len = (int)(q2 - (in + 1));
            if (len > max_len - 1) len = max_len - 1;
            for (int i = 0; i < len; i++) out[i] = in[1 + i];
            out[len] = '\0';

            /* Check for concatenation + */
            const char *plus = strchr(q2 + 1, '+');
            if (plus) {
                char rest[128];
                extract_expr_val(plus + 1, rest, sizeof(rest));
                if (rest[0]) {
                    int cur_l = (int)strlen(out);
                    int space = max_len - 1 - cur_l;
                    if (space > 0) {
                        int r_l = (int)strlen(rest);
                        if (r_l > space) r_l = space;
                        for (int i = 0; i < r_l; i++) out[cur_l + i] = rest[i];
                        out[cur_l + r_l] = '\0';
                    }
                }
            }
            return;
        }
    }

    /* 3. Bare Token (variable or number) */
    char tok[64];
    int tlen = 0;
    while (in[tlen] && in[tlen] != ' ' && in[tlen] != '\t' && in[tlen] != ';' &&
           in[tlen] != '+' && in[tlen] != '-' && in[tlen] != '*' && in[tlen] != '/' &&
           in[tlen] != ')' && in[tlen] != ',' && tlen < (int)sizeof(tok) - 1) {
        tok[tlen] = in[tlen];
        tlen++;
    }
    tok[tlen] = '\0';

    const char *v = get_var(tok);
    if (v) {
        int l = 0;
        while (v[l] && l < max_len - 1) { out[l] = v[l]; l++; }
        out[l] = '\0';
    } else {
        int l = 0;
        while (tok[l] && l < max_len - 1) { out[l] = tok[l]; l++; }
        out[l] = '\0';
    }

    /* 4. Arithmetic / Concatenation operators (+, -, *, /) */
    const char *op_ptr = in + tlen;
    while (*op_ptr == ' ' || *op_ptr == '\t') op_ptr++;
    char op = *op_ptr;
    if (op == '+' || op == '-' || op == '*' || op == '/') {
        char rest[128];
        extract_expr_val(op_ptr + 1, rest, sizeof(rest));
        if (is_numeric_str(out) && is_numeric_str(rest)) {
            int a = str_to_int(out);
            int b = str_to_int(rest);
            int res = 0;
            if (op == '+') res = a + b;
            else if (op == '-') res = a - b;
            else if (op == '*') res = a * b;
            else if (op == '/' && b != 0) res = a / b;
            int_to_str(res, out, max_len);
        } else if (op == '+') {
            /* String concatenation */
            int cur_l = (int)strlen(out);
            int space = max_len - 1 - cur_l;
            if (space > 0) {
                int r_l = (int)strlen(rest);
                if (r_l > space) r_l = space;
                for (int i = 0; i < r_l; i++) out[cur_l + i] = rest[i];
                out[cur_l + r_l] = '\0';
            }
        }
    }
}

static int js_eval_internal(firstos_renderer_context_t *ctx, char *code) {
    if (!code) return 0;
    if (++s_call_depth > MAX_CALL_DEPTH) {
        js_print("[js] ERROR: call depth limit exceeded\n");
        s_call_depth--;
        return -2;
    }

    char *p = code;
    while (*p) {
        while (*p == ';' || *p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (++s_instruction_count > MAX_INSTRUCTIONS_PER_TASK) {
            js_print("[js] ERROR: instruction limit exceeded (budget exhausted)\n");
            s_call_depth--;
            return -1;
        }

        /* 1. Function declaration: function fname(...) { ... } */
        if (strncmp(p, "function ", 9) == 0) {
            const char *fn_start = p + 9;
            while (*fn_start == ' ') fn_start++;
            int fn_len = 0;
            while (fn_start[fn_len] && fn_start[fn_len] != '(' && fn_start[fn_len] != ' ' && fn_len < 31) fn_len++;
            char fn_name[32];
            for (int i = 0; i < fn_len; i++) fn_name[i] = fn_start[i];
            fn_name[fn_len] = '\0';

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
                if (body_len > 0 && body_len < 510) {
                    char body_buf[512];
                    for (int i = 0; i < body_len; i++) body_buf[i] = brace[1 + i];
                    body_buf[body_len] = '\0';
                    set_func(fn_name, body_buf);
                }
                p = (char *)b;
                continue;
            }
        }

        /* 2. While loop: while (...) { ... } */
        if (strncmp(p, "while", 5) == 0) {
            const char *paren = strchr(p, '(');
            const char *brace = strchr(p, '{');
            if (paren && brace && brace > paren) {
                int depth = 1;
                const char *b = brace + 1;
                while (*b && depth > 0) {
                    if (*b == '{') depth++;
                    else if (*b == '}') depth--;
                    b++;
                }
                int body_len = (depth == 0) ? (int)(b - 1 - (brace + 1)) : (int)(b - (brace + 1));
                char body_buf[512];
                if (body_len > 510) body_len = 510;
                for (int i = 0; i < body_len; i++) body_buf[i] = brace[1 + i];
                body_buf[body_len] = '\0';

                while (1) {
                    if (++s_instruction_count > MAX_INSTRUCTIONS_PER_TASK) {
                        js_print("[js] instruction budget exhausted in loop\n");
                        s_call_depth--;
                        return -1;
                    }
                    /* Execute body */
                    char iter_code[512];
                    strcpy(iter_code, body_buf);
                    int res = js_eval_internal(ctx, iter_code);
                    if (res != 0) {
                        s_call_depth--;
                        return res;
                    }
                }
            }
        }

        /* Read one statement up to ';' or '\n' */
        char *line = p;
        while (*p && *p != ';' && *p != '\n' && *p != '\r') p++;
        if (*p) { *p = '\0'; p++; }
        line = trim_str(line);
        if (!line[0]) continue;

        /* Statement types */
        /* A. console.log(...) */
        if (strncmp(line, "console.log", 11) == 0) {
            char msg[128];
            const char *paren = strchr(line, '(');
            extract_expr_val(paren ? paren + 1 : line + 11, msg, sizeof(msg));
            js_print("[console.log] ");
            js_print(msg);
            js_print("\n");
        }
        /* B. alert(...) */
        else if (strncmp(line, "alert", 5) == 0) {
            char msg[128];
            const char *paren = strchr(line, '(');
            extract_expr_val(paren ? paren + 1 : line + 5, msg, sizeof(msg));
            int l = 0;
            while (msg[l] && l < 127) { s_last_alert[l] = msg[l]; l++; }
            s_last_alert[l] = '\0';
            js_print("[alert] ");
            js_print(msg);
            js_print("\n");
        }
        /* C. document.title = ... */
        else if (strstr(line, "document.title") != NULL && ctx) {
            const char *eq = strchr(line, '=');
            if (eq) {
                char title[128];
                extract_expr_val(eq + 1, title, sizeof(title));
                int l = 0;
                while (title[l] && l < 127) { ctx->doc_title[l] = title[l]; l++; }
                ctx->doc_title[l] = '\0';
            }
        }
        /* D. document.getElementById(...) manipulations */
        else if (strstr(line, "document.getElementById") != NULL && ctx) {
            const char *arg_start = strchr(line, '(');
            if (arg_start) {
                char target_id[64];
                extract_expr_val(arg_start + 1, target_id, sizeof(target_id));
                html_node_t *target = firstos_renderer_get_element_by_id(ctx, target_id);

                if (target) {
                    if (strstr(line, "addEventListener") != NULL) {
                        /* Register event listener */
                        const char *ev_p = strstr(line, "addEventListener");
                        const char *p1 = strchr(ev_p, '(');
                        if (p1 && s_listener_count < MAX_EVENT_LISTENERS) {
                            char ev_name[16];
                            extract_expr_val(p1 + 1, ev_name, sizeof(ev_name));
                            const char *comma = strchr(p1, ',');
                            if (comma) {
                                while (*comma == ',' || *comma == ' ') comma++;
                                char fn_name[32];
                                int fl = 0;
                                while (comma[fl] && comma[fl] != ')' && comma[fl] != ' ' && comma[fl] != ';' && fl < 31) {
                                    fn_name[fl] = comma[fl];
                                    fl++;
                                }
                                fn_name[fl] = '\0';

                                s_listeners[s_listener_count].node_id = target->id_num;
                                strcpy(s_listeners[s_listener_count].event_name, ev_name);
                                strcpy(s_listeners[s_listener_count].func_name, fn_name);
                                s_listeners[s_listener_count].inline_body[0] = '\0';
                                s_listener_count++;
                            }
                        }
                    } else {
                        const char *eq = strchr(line, '=');
                        if (eq) {
                            char val[256];
                            extract_expr_val(eq + 1, val, sizeof(val));

                            if (strstr(line, "textContent") || strstr(line, "innerText")) {
                                target->text = html5_arena_strndup(&ctx->arena, val, strlen(val));
                                target->text_len = (int)strlen(val);
                                if (target->first_child && target->first_child->tag == HTML_TAG_TEXT_NODE) {
                                    target->first_child->text = target->text;
                                    target->first_child->text_len = target->text_len;
                                }
                            } else if (strstr(line, "value")) {
                                int l = 0;
                                while (val[l] && l < 127) { target->value[l] = val[l]; l++; }
                                target->value[l] = '\0';
                                target->value_len = l;
                                target->cursor_pos = l;
                            } else if (strstr(line, "checked")) {
                                target->checked = (val[0] == '1' || strncmp(val, "true", 4) == 0);
                            } else if (strstr(line, "style.color")) {
                                target->style.color = parse_js_color(val, target->style.color);
                            } else if (strstr(line, "style.backgroundColor")) {
                                target->style.bg_color = parse_js_color(val, target->style.bg_color);
                                target->style.has_bg = 1;
                            } else if (strstr(line, "style.display")) {
                                if (strstr(val, "none")) target->style.display_none = 1;
                                else target->style.display_none = 0;
                            }
                        }
                    }
                }
            }
        }
        /* Phase 5C: window[.parent|.top].postMessage(data, targetOrigin) */
        else if (strstr(line, ".postMessage") != NULL || strncmp(line, "postMessage", 11) == 0) {
            const char *pm = strstr(line, "postMessage");
            const char *paren = pm ? strchr(pm, '(') : NULL;
            int target_kind = 0; /* 0=self, 1=parent, 2=top */
            if (strstr(line, "window.parent.") || strncmp(line, "parent.", 7) == 0) target_kind = 1;
            else if (strstr(line, "window.top.") || strncmp(line, "top.", 4) == 0) target_kind = 2;
            if (paren) {
                char data[MAX_STRING_LEN];
                extract_expr_val(paren + 1, data, sizeof(data));
                /* Locate top-level comma separating the two arguments */
                const char *q = paren + 1;
                char inq = 0;
                while (*q) {
                    if (inq) { if (*q == inq) inq = 0; }
                    else if (*q == '"' || *q == '\'') inq = *q;
                    else if (*q == ',') break;
                    q++;
                }
                char torigin[128];
                torigin[0] = '\0';
                if (*q == ',') extract_expr_val(q + 1, torigin, sizeof(torigin));
                js_queue_outbound_message(target_kind, data, torigin);
            }
        }
        /* Phase 5C: window.addEventListener("message", fn) */
        else if ((strncmp(line, "window.addEventListener", 23) == 0 ||
                  strncmp(line, "addEventListener", 16) == 0) && strstr(line, "message")) {
            const char *p1 = strchr(line, '(');
            const char *comma = p1 ? strchr(p1, ',') : NULL;
            if (comma && s_listener_count < MAX_EVENT_LISTENERS) {
                while (*comma == ',' || *comma == ' ') comma++;
                char fn_name[32];
                int fl = 0;
                while (comma[fl] && comma[fl] != ')' && comma[fl] != ' ' && fl < 31) { fn_name[fl] = comma[fl]; fl++; }
                fn_name[fl] = '\0';
                s_listeners[s_listener_count].node_id = JS_WINDOW_NODE_ID;
                strcpy(s_listeners[s_listener_count].event_name, "message");
                strcpy(s_listeners[s_listener_count].func_name, fn_name);
                s_listeners[s_listener_count].inline_body[0] = '\0';
                s_listener_count++;
            }
        }
        /* E. Variable Assignment: var x = ..., let x = ..., const x = ..., x = ... */
        else if (strchr(line, '=') != NULL) {
            char *eq = strchr(line, '=');
            *eq = '\0';
            char *var_part = trim_str(line);
            if (strncmp(var_part, "var ", 4) == 0) var_part = trim_str(var_part + 4);
            else if (strncmp(var_part, "let ", 4) == 0) var_part = trim_str(var_part + 4);
            else if (strncmp(var_part, "const ", 6) == 0) var_part = trim_str(var_part + 6);

            char val[128];
            extract_expr_val(eq + 1, val, sizeof(val));
            set_var(var_part, val);
        }
        /* F. Function Call: fname() */
        else if (strchr(line, '(') != NULL) {
            char fn_name[32];
            int fl = 0;
            while (line[fl] && line[fl] != '(' && line[fl] != ' ' && fl < 31) {
                fn_name[fl] = line[fl];
                fl++;
            }
            fn_name[fl] = '\0';

            const char *body = get_func_body(fn_name);
            if (body) {
                char body_copy[512];
                strcpy(body_copy, body);
                int res = js_eval_internal(ctx, body_copy);
                if (res != 0) {
                    s_call_depth--;
                    return res;
                }
            }
        }
    }

    s_call_depth--;
    return 0;
}

int firstos_js_eval(firstos_renderer_context_t *ctx, const char *script_code) {
    if (!script_code) return 0;
    while (*script_code == ' ' || *script_code == '\t' || *script_code == '\r' || *script_code == '\n' || *script_code == ';') script_code++;
    if (!*script_code) return 0;

    s_cur_ctx = ctx;
    s_instruction_count = 0;

    int len = (int)strlen(script_code);
    if (len > 4096) len = 4096;

    char code_buf[4096];
    for (int i = 0; i < len; i++) code_buf[i] = script_code[i];
    code_buf[len] = '\0';

    /* Strip single-line (//) comments outside of strings */
    char in_quote = 0;
    for (int i = 0; i < len; i++) {
        if (in_quote) {
            if (code_buf[i] == in_quote) in_quote = 0;
        } else {
            if (code_buf[i] == '"' || code_buf[i] == '\'') {
                in_quote = code_buf[i];
            } else if (code_buf[i] == '/' && i + 1 < len && code_buf[i+1] == '/') {
                code_buf[i] = ' ';
                code_buf[i+1] = ' ';
                i += 2;
                while (i < len && code_buf[i] != '\n' && code_buf[i] != '\r') {
                    code_buf[i] = ' ';
                    i++;
                }
            }
        }
    }

    return js_eval_internal(ctx, code_buf);
}

int firstos_js_dispatch_event(firstos_renderer_context_t *ctx, html_node_t *node, const char *event_name) {
    if (!ctx || !node || !event_name) return 0;

    int handled = 0;
    for (int i = 0; i < s_listener_count; i++) {
        if (s_listeners[i].node_id == node->id_num && strcmp(s_listeners[i].event_name, event_name) == 0) {
            if (s_listeners[i].func_name[0]) {
                const char *body = get_func_body(s_listeners[i].func_name);
                if (body) {
                    firstos_js_eval(ctx, body);
                    handled = 1;
                }
            } else if (s_listeners[i].inline_body[0]) {
                firstos_js_eval(ctx, s_listeners[i].inline_body);
                handled = 1;
            }
        }
    }
    return handled;
}

static void run_scripts_rec(firstos_renderer_context_t *ctx, html_node_t *node) {
    if (!node) return;
    if (node->tag == HTML_TAG_SCRIPT && node->text && node->text_len > 0) {
        firstos_js_eval(ctx, node->text);
    }
    for (html_node_t *child = node->first_child; child; child = child->next_sibling) {
        run_scripts_rec(ctx, child);
    }
}

int firstos_js_run_dom_scripts(firstos_renderer_context_t *ctx) {
    if (!ctx || !ctx->root_dom) return 0;
    firstos_js_init();
    run_scripts_rec(ctx, ctx->root_dom);
    return 1;
}

/* Phase 5C: Deliver an inbound postMessage (already validated by the Host)
 * to window-level "message" listeners. Exposes event.data / event.origin. */
int firstos_js_deliver_message(firstos_renderer_context_t *ctx, const char *data, const char *origin) {
    if (!ctx) return 0;
    set_var("event.data", data ? data : "");
    set_var("event.origin", origin ? origin : "null");
    int handled = 0;
    for (int i = 0; i < s_listener_count; i++) {
        if (s_listeners[i].node_id == JS_WINDOW_NODE_ID &&
            strcmp(s_listeners[i].event_name, "message") == 0) {
            const char *body = get_func_body(s_listeners[i].func_name);
            if (body) {
                firstos_js_eval(ctx, body);
                handled++;
            }
        }
    }
    return handled;
}
