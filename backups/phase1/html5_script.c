#include "html5_script.h"
#include "html5_resources.h"
#include "serial.h"
#include "string.h"
#include "stdio.h"
#include "heap.h"
#include "http_client.h"

static char g_last_alert[128] = {0};

typedef struct {
    char name[32];
    char value[128];
} script_var_t;

#define MAX_SCRIPT_VARS 32
static script_var_t g_vars[MAX_SCRIPT_VARS];
static int g_var_count = 0;

void html5_script_init(void) {
    g_last_alert[0] = 0;
    g_var_count = 0;
    memset(g_vars, 0, sizeof(g_vars));
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

#define HTML5_SCRIPT_MAX_LEN 65536
static char s_code_buf[HTML5_SCRIPT_MAX_LEN + 1];

void html5_script_eval(const char *script_code, html_node_t *root_dom) {
    if (!script_code || !script_code[0] || !root_dom) return;

    serial_write("[html5_script] Executing script block:\n");

    /* Copy script_code to static buffer to avoid kernel stack overflow */
    strncpy(s_code_buf, script_code, sizeof(s_code_buf) - 1);
    s_code_buf[sizeof(s_code_buf) - 1] = 0;

    char *p = s_code_buf;
    while (*p) {
        while (*p == ';' || *p == '\n' || *p == '\r' || *p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char *line = p;
        while (*p && *p != ';' && *p != '\n' && *p != '\r') p++;
        if (*p) { *p = 0; p++; }
        line = trim_whitespace(line);
        if (!line[0]) continue;

        /* 1. console.log(...) */
        if (strncmp(line, "console.log", 11) == 0) {
            char msg[128];
            const char *paren = strchr(line, '(');
            extract_expr_val(paren ? paren + 1 : line + 11, msg, sizeof(msg));
            serial_write("[console.log] ");
            serial_write(msg);
            serial_write("\n");
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
            const char *eq = strchr(line, '=');
            if (eq) {
                char nav_url[256];
                extract_expr_val(eq + 1, nav_url, sizeof(nav_url));
                if (nav_url[0]) {
                    html5_request_navigation(nav_url);
                    serial_write("[html5_script] Script requested navigation to: ");
                    serial_write(nav_url);
                    serial_write("\n");
                }
            }
        }
        /* 6. document.getElementById(...) manipulations */
        else if (strstr(line, "document.getElementById") != NULL) {
            const char *id_start = strchr(line, '(');
            if (id_start) {
                char target_id[32];
                extract_expr_val(id_start + 1, target_id, sizeof(target_id));
                html_node_t *target = html5_get_element_by_id(target_id);

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
                                serial_write("[html5_script] Updated #");
                                serial_write(target_id);
                                serial_write(".content = \"");
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
                        }
                    }
                }
            }
        }
        /* 7. Variable declarations: var x = ... or let x = ... or const x = ... */
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

                char val[128];
                extract_expr_val(eq + 1, val, sizeof(val));
                set_var(var_name, val);
                serial_write("[html5_script] Declared var ");
                serial_write(var_name);
                serial_write(" = ");
                serial_write(val);
                serial_write("\n");
            }
        }
    }
}

static int g_script_fetch_count = 0;

static void walk_and_run_scripts(html_node_t *node, html_node_t *root, const char *base_url) {
    if (!node) return;

    if (node->tag == HTML_TAG_SCRIPT) {
        if (node->src[0] && g_script_fetch_count < 4) {
            g_script_fetch_count++;
            static char s_resolved_url[256];
            html5_url_resolve(base_url, node->src, s_resolved_url, sizeof(s_resolved_url));
            serial_write("[html5_script] Fetching external script: ");
            serial_write(s_resolved_url);
            serial_write("\n");

            if (strncmp(s_resolved_url, "http://", 7) == 0 || strncmp(s_resolved_url, "https://", 8) == 0) {
                static unsigned char s_script_code[HTML5_SCRIPT_MAX_LEN + 1];
                int is_truncated = 0;
                unsigned int actual_len = 0;
                int n = http_get_binary_ex(s_resolved_url, s_script_code, HTML5_SCRIPT_MAX_LEN, &is_truncated, &actual_len);
                serial_write("[html5_script] script bytes received=");
                serial_write_hex((u32)n);
                serial_write("\n");
                serial_write("[html5_script] script truncated=");
                serial_write(is_truncated ? "1\n" : "0\n");

                if (n > 0) {
                    s_script_code[n] = 0;
                    html5_script_eval((const char*)s_script_code, root);
                }
            }
        } else if (node->text[0]) {
            html5_script_eval(node->text, root);
        }
    }

    walk_and_run_scripts(node->first_child, root, base_url);
    walk_and_run_scripts(node->next_sibling, root, base_url);
}

void html5_script_run_dom_scripts(html_node_t *root_dom, const char *base_url) {
    if (!root_dom) return;
    g_script_fetch_count = 0;
    walk_and_run_scripts(root_dom, root_dom, base_url);
}
