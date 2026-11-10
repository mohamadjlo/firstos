#include "firstos_renderer.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

void firstos_ax_tree_init(firstos_ax_tree_t *tree, uint32_t tab_id, uint32_t frame_id) {
    if (!tree) return;
    tree->node_count = 0;
    tree->generation = 1;
    tree->tab_id = tab_id;
    tree->frame_id = frame_id;
    for (uint32_t i = 0; i < FIRSTOS_MAX_AX_NODES_PER_FRAME; i++) {
        tree->nodes[i].ax_id = 0;
    }
}

const firstos_ax_node_t *firstos_ax_tree_get_node(const firstos_ax_tree_t *tree, uint32_t ax_id) {
    if (!tree || ax_id == 0 || ax_id > tree->node_count) return NULL;
    for (uint32_t i = 0; i < tree->node_count; i++) {
        if (tree->nodes[i].ax_id == ax_id) return &tree->nodes[i];
    }
    return NULL;
}

static firstos_ax_role_t map_dom_role(html_node_t *node, uint8_t *out_heading_level) {
    if (out_heading_level) *out_heading_level = 0;
    if (!node) return AX_ROLE_NONE;

    switch (node->tag) {
        case HTML_TAG_HTML:
        case HTML_TAG_BODY:
            return AX_ROLE_DOCUMENT;
        case HTML_TAG_SECTION:
        case HTML_TAG_ARTICLE:
        case HTML_TAG_HEADER:
        case HTML_TAG_NAV:
        case HTML_TAG_FOOTER:
            return AX_ROLE_SECTION;
        case HTML_TAG_H1:
            if (out_heading_level) *out_heading_level = 1;
            return AX_ROLE_HEADING;
        case HTML_TAG_H2:
            if (out_heading_level) *out_heading_level = 2;
            return AX_ROLE_HEADING;
        case HTML_TAG_H3:
            if (out_heading_level) *out_heading_level = 3;
            return AX_ROLE_HEADING;
        case HTML_TAG_P:
        case HTML_TAG_TEXT_NODE:
            return AX_ROLE_PARAGRAPH;
        case HTML_TAG_A:
            return AX_ROLE_LINK;
        case HTML_TAG_BUTTON:
            return AX_ROLE_BUTTON;
        case HTML_TAG_INPUT:
            if (node->input_type) {
                if (u_strcmp(node->input_type, "button") == 0 || u_strcmp(node->input_type, "submit") == 0) {
                    return AX_ROLE_BUTTON;
                }
                if (u_strcmp(node->input_type, "checkbox") == 0) {
                    return AX_ROLE_CHECKBOX;
                }
                if (u_strcmp(node->input_type, "radio") == 0) {
                    return AX_ROLE_RADIO;
                }
                if (u_strcmp(node->input_type, "password") == 0 || u_strcmp(node->input_type, "text") == 0) {
                    return AX_ROLE_TEXTBOX;
                }
            }
            return AX_ROLE_TEXTBOX;
        case HTML_TAG_TEXTAREA:
            return AX_ROLE_TEXTBOX;
        case HTML_TAG_IMG:
            return AX_ROLE_IMAGE;
        case HTML_TAG_IFRAME:
            return AX_ROLE_IFRAME;
        case HTML_TAG_AUDIO:
        case HTML_TAG_VIDEO:
            return AX_ROLE_GENERIC;
        default:
            return AX_ROLE_GENERIC;
    }
}

static void sanitize_ax_string(const char *in, char *out, int max_len) {
    if (!out || max_len <= 1) return;
    out[0] = '\0';
    if (!in) return;
    int pos = 0;
    while (*in && pos < max_len - 1) {
        unsigned char c = (unsigned char)*in++;
        if (c >= 32 && c < 127) {
            out[pos++] = (char)c;
        } else {
            out[pos++] = ' ';
        }
    }
    out[pos] = '\0';
}

static void compute_accessible_name(html_node_t *node, firstos_ax_role_t role, char *out, int max_len) {
    if (!out || max_len <= 1) return;
    out[0] = '\0';

    /* 1. Explicit text */
    if (node->text && node->text[0]) {
        sanitize_ax_string(node->text, out, max_len);
        if (out[0]) return;
    }

    /* 2. Button value / label */
    if ((role == AX_ROLE_BUTTON) && node->value[0]) {
        sanitize_ax_string(node->value, out, max_len);
        if (out[0]) return;
    }

    /* 3. Input Placeholder */
    if (node->placeholder && node->placeholder[0]) {
        sanitize_ax_string(node->placeholder, out, max_len);
        if (out[0]) return;
    }

    /* 4. Frame Title */
    if (role == AX_ROLE_IFRAME) {
        if (node->frame_title && node->frame_title[0]) {
            sanitize_ax_string(node->frame_title, out, max_len);
            return;
        }
        sanitize_ax_string("Embedded Frame", out, max_len);
        return;
    }

    /* 5. Role Fallback */
    if (role == AX_ROLE_LINK) sanitize_ax_string("Link", out, max_len);
    else if (role == AX_ROLE_BUTTON) sanitize_ax_string("Button", out, max_len);
    else if (role == AX_ROLE_IMAGE) sanitize_ax_string("Image", out, max_len);
    else if (role == AX_ROLE_DOCUMENT) sanitize_ax_string("Document", out, max_len);
    else out[0] = '\0';
}

static uint32_t traverse_and_build(firstos_renderer_context_t *ctx,
                                   html_node_t *node,
                                   firstos_ax_tree_t *tree,
                                   uint32_t parent_ax_id,
                                   int depth) {
    if (!node || !tree || depth > 16) return 0;
    if (tree->node_count >= FIRSTOS_MAX_AX_NODES_PER_FRAME) return 0;

    /* Skip non-visual / non-semantic nodes */
    if (node->tag == HTML_TAG_HEAD || node->tag == HTML_TAG_SCRIPT || node->tag == HTML_TAG_STYLE) {
        return 0;
    }
    if (node->style.display_none) return 0;

    uint8_t heading_lvl = 0;
    firstos_ax_role_t role = map_dom_role(node, &heading_lvl);

    /* Allocate AX node */
    uint32_t my_ax_id = ++tree->node_count;
    firstos_ax_node_t *ax = &tree->nodes[my_ax_id - 1];
    ax->ax_id = my_ax_id;
    ax->generation = tree->generation;
    ax->tab_id = tree->tab_id;
    ax->frame_id = tree->frame_id;
    ax->dom_node_id = (uint32_t)node->id_num;
    ax->role = role;
    ax->heading_level = heading_lvl;
    ax->parent_ax_id = parent_ax_id;
    ax->child_count = 0;
    ax->state_flags = AX_STATE_NONE;

    /* Copy geometry */
    ax->bounds.x = node->bounds.x;
    ax->bounds.y = node->bounds.y;
    ax->bounds.w = node->bounds.w;
    ax->bounds.h = node->bounds.h;

    /* Compute Accessible Name */
    compute_accessible_name(node, role, ax->name, sizeof(ax->name));

    /* Accessible Value (with strict password masking) */
    if (role == AX_ROLE_TEXTBOX) {
        if (node->input_type && u_strcmp(node->input_type, "password") == 0) {
            /* MASK PASSWORD UNCONDITIONALLY */
            int vlen = node->value_len > 0 ? node->value_len : (int)u_strlen(node->value);
            int m = 0;
            while (m < vlen && m < 8) {
                ax->value[m++] = '*';
            }
            if (m == 0 && vlen > 0) ax->value[m++] = '*';
            ax->value[m] = '\0';
        } else {
            sanitize_ax_string(node->value, ax->value, sizeof(ax->value));
        }
    } else {
        ax->value[0] = '\0';
    }
    ax->description[0] = '\0';

    /* State mappings */
    if (node->is_focused || (ctx->focus_state.focused_node_id > 0 && ctx->focus_state.focused_node_id == node->id_num)) {
        ax->state_flags |= AX_STATE_FOCUSED;
    }
    if (node->disabled) ax->state_flags |= AX_STATE_DISABLED;
    if (node->checked) ax->state_flags |= AX_STATE_CHECKED;
    if (node->readonly_state) ax->state_flags |= AX_STATE_READONLY;

    /* Recurse on children */
    uint32_t direct_children = 0;
    for (html_node_t *child = node->first_child; child; child = child->next_sibling) {
        uint32_t child_id = traverse_and_build(ctx, child, tree, my_ax_id, depth + 1);
        if (child_id > 0) direct_children++;
    }
    ax->child_count = direct_children;

    return my_ax_id;
}

int firstos_ax_tree_build(firstos_renderer_context_t *ctx, firstos_ax_tree_t *out_tree) {
    if (!ctx || !out_tree || !ctx->root_dom) return 0;
    firstos_ax_tree_init(out_tree, 1, 1);
    out_tree->generation = ctx->generation;

    traverse_and_build(ctx, ctx->root_dom, out_tree, 0, 0);
    return (int)out_tree->node_count;
}

static const char *role_to_string(firstos_ax_role_t role) {
    switch (role) {
        case AX_ROLE_DOCUMENT: return "document";
        case AX_ROLE_MAIN: return "main";
        case AX_ROLE_SECTION: return "section";
        case AX_ROLE_HEADING: return "heading";
        case AX_ROLE_PARAGRAPH: return "paragraph";
        case AX_ROLE_LINK: return "link";
        case AX_ROLE_BUTTON: return "button";
        case AX_ROLE_TEXTBOX: return "textbox";
        case AX_ROLE_CHECKBOX: return "checkbox";
        case AX_ROLE_RADIO: return "radio";
        case AX_ROLE_IMAGE: return "image";
        case AX_ROLE_IFRAME: return "iframe";
        default: return "generic";
    }
}

static int append_str(char *buf, int pos, int max, const char *str) {
    if (!buf || !str || pos >= max - 1) return pos;
    while (*str && pos < max - 1) {
        buf[pos++] = *str++;
    }
    buf[pos] = '\0';
    return pos;
}

int firstos_ax_tree_dump(const firstos_ax_tree_t *tree, char *out_buf, int max_len) {
    if (!tree || !out_buf || max_len <= 1) return 0;
    int pos = 0;
    out_buf[0] = '\0';

    pos = append_str(out_buf, pos, max_len, "[AX_TREE] Nodes=");
    char num_buf[16];
    /* Simple integer format */
    int n = tree->node_count;
    char tmp[16]; int ti = 0;
    if (n == 0) tmp[ti++] = '0';
    else { while (n > 0) { tmp[ti++] = (char)('0' + (n % 10)); n /= 10; } }
    int ni = 0;
    while (ti > 0) num_buf[ni++] = tmp[--ti];
    num_buf[ni] = '\0';
    pos = append_str(out_buf, pos, max_len, num_buf);
    pos = append_str(out_buf, pos, max_len, ":\n");

    for (uint32_t i = 0; i < tree->node_count; i++) {
        const firstos_ax_node_t *node = &tree->nodes[i];
        pos = append_str(out_buf, pos, max_len, "  id=");
        int id = node->ax_id; ti = 0;
        if (id == 0) tmp[ti++] = '0';
        else { while (id > 0) { tmp[ti++] = (char)('0' + (id % 10)); id /= 10; } }
        ni = 0; while (ti > 0) num_buf[ni++] = tmp[--ti]; num_buf[ni] = '\0';
        pos = append_str(out_buf, pos, max_len, num_buf);

        pos = append_str(out_buf, pos, max_len, " role=");
        pos = append_str(out_buf, pos, max_len, role_to_string(node->role));

        if (node->name[0]) {
            pos = append_str(out_buf, pos, max_len, " name=\"");
            pos = append_str(out_buf, pos, max_len, node->name);
            pos = append_str(out_buf, pos, max_len, "\"");
        }
        if (node->value[0]) {
            pos = append_str(out_buf, pos, max_len, " value=\"");
            pos = append_str(out_buf, pos, max_len, node->value);
            pos = append_str(out_buf, pos, max_len, "\"");
        }
        if (node->state_flags & AX_STATE_FOCUSED) {
            pos = append_str(out_buf, pos, max_len, " [focused]");
        }
        if (node->state_flags & AX_STATE_CHECKED) {
            pos = append_str(out_buf, pos, max_len, " [checked]");
        }
        pos = append_str(out_buf, pos, max_len, "\n");
    }
    return pos;
}
