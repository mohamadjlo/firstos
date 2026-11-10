#include "include/firstos_renderer.h"

html_node_t *firstos_form_find_parent_form(html_node_t *node) {
    if (!node) return NULL;
    for (html_node_t *curr = node->parent; curr; curr = curr->parent) {
        if (curr->tag == HTML_TAG_FORM) return curr;
    }
    return NULL;
}

int firstos_form_handle_char(firstos_renderer_context_t *ctx, char c) {
    if (!ctx) return 0;
    html_node_t *node = firstos_renderer_get_focused(ctx);
    if (!node || node->tag != HTML_TAG_INPUT || node->disabled || node->readonly_state) return 0;
    if (node->input_type && strcmp(node->input_type, "checkbox") == 0) return 0;
    if (node->input_type && strcmp(node->input_type, "submit") == 0) return 0;

    if (node->value_len >= 126) return 0;

    if (node->cursor_pos < 0) node->cursor_pos = 0;
    if (node->cursor_pos > node->value_len) node->cursor_pos = node->value_len;

    for (int i = node->value_len; i > node->cursor_pos; i--) {
        node->value[i] = node->value[i - 1];
    }
    node->value[node->cursor_pos] = c;
    node->cursor_pos++;
    node->value_len++;
    node->value[node->value_len] = '\0';

    return 1;
}

int firstos_form_handle_backspace(firstos_renderer_context_t *ctx) {
    if (!ctx) return 0;
    html_node_t *node = firstos_renderer_get_focused(ctx);
    if (!node || node->tag != HTML_TAG_INPUT || node->disabled || node->readonly_state) return 0;
    if (node->value_len <= 0 || node->cursor_pos <= 0) return 0;

    if (node->cursor_pos > node->value_len) node->cursor_pos = node->value_len;

    for (int i = node->cursor_pos - 1; i < node->value_len; i++) {
        node->value[i] = node->value[i + 1];
    }
    node->cursor_pos--;
    node->value_len--;
    node->value[node->value_len] = '\0';

    return 1;
}

int firstos_form_toggle_checkbox(html_node_t *node) {
    if (!node || node->tag != HTML_TAG_INPUT || node->disabled) return 0;
    node->checked = !node->checked;
    return 1;
}

static void url_encode_append(char *out_buf, int *pos, int max_len, const char *str) {
    if (!str || !out_buf || *pos >= max_len - 1) return;
    static const char hex_digits[] = "0123456789ABCDEF";
    for (int i = 0; str[i] && *pos < max_len - 4; i++) {
        unsigned char c = (unsigned char)str[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out_buf[(*pos)++] = (char)c;
        } else if (c == ' ') {
            out_buf[(*pos)++] = '+';
        } else {
            out_buf[(*pos)++] = '%';
            out_buf[(*pos)++] = hex_digits[(c >> 4) & 0x0F];
            out_buf[(*pos)++] = hex_digits[c & 0x0F];
        }
    }
    out_buf[*pos] = '\0';
}

static void collect_form_inputs_rec(html_node_t *node, html_node_t *target_form, char *out_buf, int *pos, int max_len, int *field_count) {
    if (!node) return;

    if (node->tag == HTML_TAG_INPUT && node->name && node->name[0] && !node->disabled) {
        /* Check if input belongs to this form */
        html_node_t *pf = firstos_form_find_parent_form(node);
        if (pf == target_form) {
            int include = 1;
            if (node->input_type && strcmp(node->input_type, "checkbox") == 0 && !node->checked) {
                include = 0;
            }
            if (node->input_type && strcmp(node->input_type, "submit") == 0) {
                include = 0; /* Submit button only included if clicked, handled separately */
            }

            if (include && *pos < max_len - 2) {
                if (*field_count > 0) {
                    out_buf[(*pos)++] = '&';
                }
                url_encode_append(out_buf, pos, max_len, node->name);
                out_buf[(*pos)++] = '=';
                const char *val = node->value;
                if (node->input_type && strcmp(node->input_type, "checkbox") == 0) {
                    val = node->value[0] ? node->value : "on";
                }
                url_encode_append(out_buf, pos, max_len, val);
                (*field_count)++;
            }
        }
    }

    for (html_node_t *child = node->first_child; child; child = child->next_sibling) {
        collect_form_inputs_rec(child, target_form, out_buf, pos, max_len, field_count);
    }
}

int firstos_form_collect_query(firstos_renderer_context_t *ctx, html_node_t *form, char *out_buf, int max_len) {
    if (!ctx || !form || !out_buf || max_len <= 1) return 0;
    out_buf[0] = '\0';
    int pos = 0;
    int field_count = 0;
    collect_form_inputs_rec(ctx->root_dom ? ctx->root_dom : form, form, out_buf, &pos, max_len, &field_count);
    out_buf[pos] = '\0';
    return pos;
}
