#include "html5_engine.h"
#include <stddef.h>
#include "html5_resources.h"
#include "html5_script.h"
#include "html5_media.h"
#include "framebuffer.h"
#include "font.h"
#include "serial.h"
#include "heap.h"
#include "string.h"
#include "stdio.h"
#include "http_client.h"
#include "browser_logger.h"

static html5_arena_t g_arena;
static html5_node_slab_t g_node_slab;
static int g_node_count = 0;
static html_node_t *g_root_dom = NULL;
static int g_doc_height = 0;
static char g_doc_title[128] = "FirstOS Web Browser";
static int g_has_explicit_body = 0;
static html_node_t *g_focused_input = NULL;
static char g_pending_navigation[512];
static char g_last_taken_navigation[512];
static int g_cursor_blink = 1;
static int g_blink_ticks = 0;

static void (*g_html5_status_cb)(const char *status) = NULL;
__attribute__((weak)) void web_browser_trigger_redraw(const char *reason) {
    (void)reason;
}

void html5_set_status_callback(void (*cb)(const char *status)) {
    g_html5_status_cb = cb;
}

static void html5_notify_status(const char *status) {
    if (g_html5_status_cb) g_html5_status_cb(status);
    extern void gui_pump_events(void);
    gui_pump_events();
}

static void on_resource_lifecycle(browser_lifecycle_state_t state, const char *status, void *user_data) {
    (void)state;
    (void)user_data;
    if (status && status[0]) {
        html5_notify_status(status);
    }
}

static void *kernel_default_alloc(size_t size, void *user_data) {
    (void)user_data;
    return kmalloc((u32)size);
}

static void kernel_default_free(void *ptr, void *user_data) {
    (void)user_data;
    kfree(ptr);
}

void html5_engine_init_with_allocator(const html5_allocator_t *allocator) {
    html5_allocator_set_default(allocator);
    html5_arena_init(&g_arena, allocator, 65536);
    html5_node_slab_init(&g_node_slab, allocator, sizeof(html_node_t));
    g_node_count = 0;
    g_root_dom = NULL;
    g_doc_height = 0;
    g_focused_input = NULL;
    g_pending_navigation[0] = 0;
    strcpy(g_doc_title, "FirstOS Web Browser");

    html5_resources_init();
    html5_resources_set_lifecycle_callback(on_resource_lifecycle, NULL);
    text_engine_init();
    html5_script_init();
    html5_media_init();

    serial_write("[html5_engine] HTML5 Layout, DOM and Presentation Engine initialized\n");
}

void html5_engine_init(void) {
    html5_allocator_t kalloc = { kernel_default_alloc, kernel_default_free, NULL };
    html5_engine_init_with_allocator(&kalloc);
}

void html5_engine_reset(void) {
    html5_arena_reset(&g_arena);
    html5_node_slab_reset(&g_node_slab);
    g_node_count = 0;
    g_root_dom = NULL;
    g_doc_height = 0;
    g_focused_input = NULL;
    g_has_explicit_body = 0;
}

void html5_engine_destroy(void) {
    html5_engine_reset();
    html5_arena_destroy(&g_arena);
    html5_node_slab_destroy(&g_node_slab);
    serial_write("[html5_engine] Engine resources destroyed\n");
}

html5_arena_t *html5_engine_get_arena(void) {
    return &g_arena;
}

html5_node_slab_t *html5_engine_get_node_slab(void) {
    return &g_node_slab;
}

static int g_last_css_bytes = 0;

int html5_engine_get_node_count(void) {
    return g_node_count;
}

int html5_engine_get_css_bytes(void) {
    return g_last_css_bytes;
}

html_node_t *html5_engine_get_node(int index) {
    return html5_node_slab_get(&g_node_slab, index);
}

int html5_get_document_height(void) {
    return g_doc_height;
}

const char *html5_get_document_title(void) {
    return g_doc_title;
}

void html5_set_document_title(const char *title) {
    if (!title) return;
    strncpy(g_doc_title, title, sizeof(g_doc_title) - 1);
    g_doc_title[sizeof(g_doc_title) - 1] = 0;
}

html_node_t *html5_get_element_by_id(const char *id) {
    if (!id || !id[0]) return NULL;
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && n->id && strcmp(n->id, id) == 0) return n;
    }
    return NULL;
}

html_node_t *html5_get_element_by_tag(html_tag_t tag) {
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && n->tag == tag) return n;
    }
    return NULL;
}

static html_tag_t tag_from_name(const char *name, int len);
static html_node_t *alloc_node(html_tag_t tag);
static int html_has_class(const char *class_list, const char *cls, int clen);

html_node_t *html5_get_element_by_class(const char *cls) {
    if (!cls || !cls[0]) return NULL;
    int clen = (int)strlen(cls);
    for (int i = 0; i < g_node_count; i++) {
        html_node_t *n = html5_engine_get_node(i);
        if (n && html_has_class(n->class_name, cls, clen)) return n;
    }
    return NULL;
}

int html5_get_elements_by_query(const char *selector, html_node_t **out_nodes, int max_nodes) {
    if (!selector || !selector[0] || !out_nodes || max_nodes <= 0) return 0;
    int count = 0;

    if (selector[0] == '#') {
        const char *id_to_find = selector + 1;
        html_node_t *n = html5_get_element_by_id(id_to_find);
        if (n) {
            out_nodes[count++] = n;
        }
        return count;
    } else if (selector[0] == '.') {
        const char *cls = selector + 1;
        int clen = (int)strlen(cls);
        for (int i = 0; i < g_node_count && count < max_nodes; i++) {
            html_node_t *n = html5_engine_get_node(i);
            if (n && html_has_class(n->class_name, cls, clen)) {
                out_nodes[count++] = n;
            }
        }
        return count;
    } else {
        /* Tag name query */
        html_tag_t tag = tag_from_name(selector, (int)strlen(selector));
        if (tag != HTML_TAG_UNKNOWN) {
            for (int i = 0; i < g_node_count && count < max_nodes; i++) {
                html_node_t *n = html5_engine_get_node(i);
                if (n && n->tag == tag) {
                    out_nodes[count++] = n;
                }
            }
        }
        return count;
    }
}

html_node_t *html5_create_element(const char *tag_name) {
    if (!tag_name || !tag_name[0]) return NULL;
    html_tag_t tag = tag_from_name(tag_name, (int)strlen(tag_name));
    if (tag == HTML_TAG_UNKNOWN) tag = HTML_TAG_DIV;
    html_node_t *n = alloc_node(tag);
    return n;
}

int html5_node_append_child(html_node_t *parent, html_node_t *child) {
    if (!parent || !child || parent == child) return 0;
    /* If child already has a parent, remove it from old parent */
    if (child->parent) {
        html5_node_remove_child(child->parent, child);
    }
    child->parent = parent;
    child->next_sibling = NULL;

    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        html_node_t *curr = parent->first_child;
        while (curr->next_sibling) {
            curr = curr->next_sibling;
        }
        curr->next_sibling = child;
    }
    html5_node_mark_dirty(parent);
    html5_node_mark_dirty(child);
    return 1;
}

int html5_node_remove_child(html_node_t *parent, html_node_t *child) {
    if (!parent || !child || !parent->first_child) return 0;
    if (parent->first_child == child) {
        parent->first_child = child->next_sibling;
        child->parent = NULL;
        child->next_sibling = NULL;
        html5_node_mark_dirty(parent);
        return 1;
    }
    html_node_t *curr = parent->first_child;
    while (curr && curr->next_sibling) {
        if (curr->next_sibling == child) {
            curr->next_sibling = child->next_sibling;
            child->parent = NULL;
            child->next_sibling = NULL;
            html5_node_mark_dirty(parent);
            return 1;
        }
        curr = curr->next_sibling;
    }
    return 0;
}

void html5_node_set_text_len(html_node_t *node, const char *text, int len) {
    if (!node) return;
    if (!text || len <= 0) {
        node->text = html5_empty_string();
        node->text_len = 0;
    } else {
        node->text = html5_arena_strndup(&g_arena, text, (size_t)len);
        node->text_len = len;
    }
    html5_node_mark_dirty(node);
}

void html5_node_set_text(html_node_t *node, const char *text) {
    if (!node) return;
    int len = text ? (int)strlen(text) : 0;
    html5_node_set_text_len(node, text, len);
}

void html5_node_append_text(html_node_t *node, const char *text) {
    if (!node || !text || !text[0]) return;
    int old_len = node->text_len;
    int add_len = (int)strlen(text);
    int new_len = old_len + add_len;
    char *buf = (char *)html5_arena_alloc(&g_arena, new_len + 1, 1);
    if (buf) {
        if (old_len > 0 && node->text) {
            memcpy(buf, node->text, old_len);
        }
        memcpy(buf + old_len, text, add_len);
        buf[new_len] = '\0';
        node->text = buf;
        node->text_len = new_len;
    }
    html5_node_mark_dirty(node);
}

void html5_node_set_value(html_node_t *node, const char *value) {
    if (!node) return;
    if (!value) value = "";
    int vlen = (int)strlen(value);
    if (node->value_cap >= vlen + 1 && node->value) {
        strcpy(node->value, value);
    } else {
        int cap = (vlen < 255) ? 256 : (vlen + 64);
        char *buf = (char *)html5_arena_alloc(&g_arena, cap, 1);
        if (buf) {
            memcpy(buf, value, vlen + 1);
            node->value = buf;
            node->value_cap = cap;
        } else {
            node->value = (char *)html5_empty_string();
            node->value_cap = 0;
        }
    }
    html5_node_mark_dirty(node);
}

void html5_node_mark_dirty(html_node_t *node) {
    if (!node) return;
    node->is_layout_dirty = 1;
    node->is_paint_dirty = 1;
    html_node_t *p = node->parent;
    while (p) {
        if (p->subtree_layout_dirty) break;
        p->subtree_layout_dirty = 1;
        p = p->parent;
    }
}

void html5_node_clear_dirty(html_node_t *node) {
    if (!node) return;
    node->is_layout_dirty = 0;
    node->subtree_layout_dirty = 0;
    node->is_paint_dirty = 0;
}

static void css_apply_declarations(html_node_t *node, const char *decls, int len);

static html_node_t *alloc_node(html_tag_t tag) {
    if (g_node_slab.node_size < sizeof(html_node_t) || !g_arena.allocator.alloc) {
        html5_engine_init();
    }
    html_node_t *n = html5_node_slab_alloc(&g_node_slab);
    if (!n) return NULL;
    n->id_num = g_node_count++;
    n->tag = tag;

    const char *empty = html5_empty_string();
    n->id = empty;
    n->class_name = empty;
    n->src = empty;
    n->href = empty;
    n->rel = empty;
    n->name = empty;
    n->alt = empty;
    n->placeholder = empty;
    n->input_type = empty;
    n->action = empty;
    n->method = empty;
    n->onclick = empty;
    n->for_id = empty;
    n->text = empty;
    n->text_len = 0;

    if (tag == HTML_TAG_INPUT || tag == HTML_TAG_TEXTAREA) {
        n->value_cap = 256;
        n->value = (char *)html5_arena_alloc(&g_arena, n->value_cap, 1);
        if (n->value) {
            n->value[0] = '\0';
        } else {
            n->value = (char *)empty;
            n->value_cap = 0;
        }
    } else {
        n->value = (char *)empty;
        n->value_cap = 0;
    }

    /* Default styles */
    n->style.color = 0x202124;
    n->style.font_size = 1;
    n->style.is_block = 1;
    n->style.flex_shrink = 1;
    n->style.flex_basis = -9999;
    n->style.row_gap = -1;
    n->style.column_gap = -1;

    if (tag == HTML_TAG_HEAD || tag == HTML_TAG_SCRIPT ||
        tag == HTML_TAG_STYLE || tag == HTML_TAG_META ||
        tag == HTML_TAG_LINK || tag == HTML_TAG_TITLE ||
        tag == HTML_TAG_SVG) {
        n->style.display_none = 1;
        n->style.is_block = 0;
    }

    /* Mark dirty */
    n->is_layout_dirty = 1;
    n->subtree_layout_dirty = 1;
    n->is_paint_dirty = 1;

    return n;
}

static html_tag_t tag_from_name(const char *name, int len) {
    if (len <= 0 || !name) return HTML_TAG_UNKNOWN;
    if (len == 1) {
        char c = h_tolower(name[0]);
        if (c == 'a') return HTML_TAG_A;
        if (c == 'p') return HTML_TAG_P;
        if (c == 'b') return HTML_TAG_B;
        if (c == 'i') return HTML_TAG_I;
        if (c == 'u') return HTML_TAG_U;
        if (c == 's') return HTML_TAG_SPAN;
        if (c == 'q') return HTML_TAG_SPAN;
        return HTML_TAG_UNKNOWN;
    }
    if (len == 2) {
        if (name[0] == 'h' || name[0] == 'H') {
            if (name[1] == '1') return HTML_TAG_H1;
            if (name[1] == '2') return HTML_TAG_H2;
            if (name[1] == '3') return HTML_TAG_H3;
            if (name[1] == '4') return HTML_TAG_H4;
            if (name[1] == '5') return HTML_TAG_H5;
            if (name[1] == '6') return HTML_TAG_H6;
            if (name[1] == 'r' || name[1] == 'R') return HTML_TAG_HR;
        }
        if (strncasecmp(name, "br", 2) == 0) return HTML_TAG_BR;
        if (strncasecmp(name, "em", 2) == 0) return HTML_TAG_EM;
        if (strncasecmp(name, "ul", 2) == 0) return HTML_TAG_UL;
        if (strncasecmp(name, "ol", 2) == 0) return HTML_TAG_OL;
        if (strncasecmp(name, "li", 2) == 0) return HTML_TAG_LI;
        if (strncasecmp(name, "dl", 2) == 0) return HTML_TAG_DL;
        if (strncasecmp(name, "dt", 2) == 0) return HTML_TAG_DT;
        if (strncasecmp(name, "dd", 2) == 0) return HTML_TAG_DD;
        if (strncasecmp(name, "tr", 2) == 0) return HTML_TAG_TR;
        if (strncasecmp(name, "th", 2) == 0) return HTML_TAG_TH;
        if (strncasecmp(name, "td", 2) == 0) return HTML_TAG_TD;
        return HTML_TAG_UNKNOWN;
    }
    if (len == 3) {
        if (strncasecmp(name, "div", 3) == 0) return HTML_TAG_DIV;
        if (strncasecmp(name, "nav", 3) == 0) return HTML_TAG_NAV;
        if (strncasecmp(name, "pre", 3) == 0) return HTML_TAG_PRE;
        if (strncasecmp(name, "img", 3) == 0) return HTML_TAG_IMG;
        if (strncasecmp(name, "svg", 3) == 0) return HTML_TAG_SVG;
        if (strncasecmp(name, "kbd", 3) == 0) return HTML_TAG_KBD;
        if (strncasecmp(name, "sub", 3) == 0) return HTML_TAG_SUB;
        if (strncasecmp(name, "sup", 3) == 0) return HTML_TAG_SUP;
        if (strncasecmp(name, "wbr", 3) == 0) return HTML_TAG_BR;
        if (strncasecmp(name, "col", 3) == 0) return HTML_TAG_META;
        return HTML_TAG_UNKNOWN;
    }
    if (len == 4) {
        if (strncasecmp(name, "span", 4) == 0) return HTML_TAG_SPAN;
        if (strncasecmp(name, "html", 4) == 0) return HTML_TAG_HTML;
        if (strncasecmp(name, "head", 4) == 0) return HTML_TAG_HEAD;
        if (strncasecmp(name, "body", 4) == 0) return HTML_TAG_BODY;
        if (strncasecmp(name, "main", 4) == 0) return HTML_TAG_MAIN;
        if (strncasecmp(name, "code", 4) == 0) return HTML_TAG_CODE;
        if (strncasecmp(name, "mark", 4) == 0) return HTML_TAG_MARK;
        if (strncasecmp(name, "form", 4) == 0) return HTML_TAG_FORM;
        if (strncasecmp(name, "link", 4) == 0) return HTML_TAG_LINK;
        if (strncasecmp(name, "meta", 4) == 0) return HTML_TAG_META;
        if (strncasecmp(name, "time", 4) == 0) return HTML_TAG_TIME;
        if (strncasecmp(name, "samp", 4) == 0) return HTML_TAG_SAMP;
        if (strncasecmp(name, "abbr", 4) == 0) return HTML_TAG_SPAN;
        if (strncasecmp(name, "cite", 4) == 0) return HTML_TAG_EM;
        if (strncasecmp(name, "area", 4) == 0 || strncasecmp(name, "base", 4) == 0) return HTML_TAG_META;
        return HTML_TAG_UNKNOWN;
    }
    if (len == 5) {
        if (strncasecmp(name, "title", 5) == 0) return HTML_TAG_TITLE;
        if (strncasecmp(name, "aside", 5) == 0) return HTML_TAG_ASIDE;
        if (strncasecmp(name, "small", 5) == 0) return HTML_TAG_SMALL;
        if (strncasecmp(name, "table", 5) == 0) return HTML_TAG_TABLE;
        if (strncasecmp(name, "thead", 5) == 0) return HTML_TAG_THEAD;
        if (strncasecmp(name, "tbody", 5) == 0) return HTML_TAG_TBODY;
        if (strncasecmp(name, "tfoot", 5) == 0) return HTML_TAG_TBODY;
        if (strncasecmp(name, "input", 5) == 0) return HTML_TAG_INPUT;
        if (strncasecmp(name, "label", 5) == 0) return HTML_TAG_LABEL;
        if (strncasecmp(name, "video", 5) == 0) return HTML_TAG_VIDEO;
        if (strncasecmp(name, "audio", 5) == 0) return HTML_TAG_AUDIO;
        if (strncasecmp(name, "style", 5) == 0) return HTML_TAG_STYLE;
        if (strncasecmp(name, "meter", 5) == 0) return HTML_TAG_METER;
        if (strncasecmp(name, "embed", 5) == 0 || strncasecmp(name, "param", 5) == 0 || strncasecmp(name, "track", 5) == 0) return HTML_TAG_META;
        return HTML_TAG_UNKNOWN;
    }
    if (len == 6) {
        if (strncasecmp(name, "header", 6) == 0) return HTML_TAG_HEADER;
        if (strncasecmp(name, "footer", 6) == 0) return HTML_TAG_FOOTER;
        if (strncasecmp(name, "strong", 6) == 0) return HTML_TAG_STRONG;
        if (strncasecmp(name, "button", 6) == 0) return HTML_TAG_BUTTON;
        if (strncasecmp(name, "select", 6) == 0) return HTML_TAG_SELECT;
        if (strncasecmp(name, "option", 6) == 0) return HTML_TAG_OPTION;
        if (strncasecmp(name, "legend", 6) == 0) return HTML_TAG_LEGEND;
        if (strncasecmp(name, "canvas", 6) == 0) return HTML_TAG_CANVAS;
        if (strncasecmp(name, "center", 6) == 0) return HTML_TAG_CENTER;
        if (strncasecmp(name, "script", 6) == 0) return HTML_TAG_SCRIPT;
        if (strncasecmp(name, "iframe", 6) == 0) return HTML_TAG_IFRAME;
        if (strncasecmp(name, "figure", 6) == 0) return HTML_TAG_FIGURE;
        if (strncasecmp(name, "dialog", 6) == 0) return HTML_TAG_DIALOG;
        if (strncasecmp(name, "source", 6) == 0) return HTML_TAG_META;
        return HTML_TAG_UNKNOWN;
    }
    if (len == 7) {
        if (strncasecmp(name, "section", 7) == 0) return HTML_TAG_SECTION;
        if (strncasecmp(name, "article", 7) == 0) return HTML_TAG_ARTICLE;
        if (strncasecmp(name, "details", 7) == 0) return HTML_TAG_DETAILS;
        if (strncasecmp(name, "summary", 7) == 0) return HTML_TAG_SUMMARY;
        if (strncasecmp(name, "address", 7) == 0) return HTML_TAG_ADDRESS;
        if (strncasecmp(name, "caption", 7) == 0) return HTML_TAG_FIGCAPTION;
        return HTML_TAG_UNKNOWN;
    }
    if (len == 8) {
        if (strncasecmp(name, "textarea", 8) == 0) return HTML_TAG_TEXTAREA;
        if (strncasecmp(name, "noscript", 8) == 0) return HTML_TAG_NOSCRIPT;
        if (strncasecmp(name, "fieldset", 8) == 0) return HTML_TAG_FIELDSET;
        if (strncasecmp(name, "progress", 8) == 0) return HTML_TAG_PROGRESS;
        if (strncasecmp(name, "colgroup", 8) == 0) return HTML_TAG_META;
        return HTML_TAG_UNKNOWN;
    }
    if (len == 10) {
        if (strncasecmp(name, "blockquote", 10) == 0) return HTML_TAG_BLOCKQUOTE;
        if (strncasecmp(name, "figcaption", 10) == 0) return HTML_TAG_FIGCAPTION;
    }
    return HTML_TAG_UNKNOWN;
}

static int is_self_closing(html_tag_t tag) {
    return (tag == HTML_TAG_IMG || tag == HTML_TAG_INPUT || tag == HTML_TAG_BR ||
            tag == HTML_TAG_HR || tag == HTML_TAG_LINK || tag == HTML_TAG_META);
}

/* Parse HTML attribute list into node fields */
static void parse_attributes(html_node_t *node, const char *str, int len) {
    int i = 0;
    while (i < len) {
        while (i < len && (str[i] == ' ' || str[i] == '\t' || str[i] == '\r' || str[i] == '\n')) i++;
        if (i >= len) break;

        /* A self-closing tag leaves '/' in the attribute slice.  Treat tag
         * delimiters as separators so malformed markup can never leave the
         * parser at the same byte forever. */
        if (str[i] == '/' || str[i] == '>') {
            i++;
            continue;
        }

        /* Extract attribute key */
        int key_start = i;
        while (i < len && str[i] != '=' && str[i] != ' ' && str[i] != '\t' && str[i] != '\r' && str[i] != '\n' && str[i] != '>' && str[i] != '/') i++;
        int key_len = i - key_start;
        if (key_len <= 0) {
            i++;
            continue;
        }
        char key[32] = {0};
        if (key_len > 0 && key_len < 31) {
            strncpy(key, str + key_start, key_len);
            key[key_len] = 0;
        }

        while (i < len && (str[i] == ' ' || str[i] == '\t' || str[i] == '\r' || str[i] == '\n')) i++;

        char val[256] = {0};
        if (i < len && str[i] == '=') {
            i++; /* skip '=' */
            while (i < len && (str[i] == ' ' || str[i] == '\t' || str[i] == '\r' || str[i] == '\n')) i++;
            char quote = 0;
            if (i < len && (str[i] == '"' || str[i] == '\'')) {
                quote = str[i++];
            }
            int val_start = i;
            if (quote) {
                while (i < len && str[i] != quote) i++;
            } else {
                while (i < len && str[i] != ' ' && str[i] != '\t' && str[i] != '\r' && str[i] != '\n' && str[i] != '>') i++;
            }
            int val_len = i - val_start;
            if (val_len > 0 && val_len < (int)sizeof(val) - 1) {
                strncpy(val, str + val_start, val_len);
                val[val_len] = 0;
            }
            if (quote && i < len && str[i] == quote) i++;
        }

        /* Assign recognized attributes */
        if (strcasecmp(key, "id") == 0) node->id = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "class") == 0) node->class_name = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "src") == 0) node->src = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "href") == 0) node->href = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "rel") == 0) node->rel = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "name") == 0) node->name = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "alt") == 0) node->alt = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "value") == 0) html5_node_set_value(node, val);
        else if (strcasecmp(key, "action") == 0) node->action = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "method") == 0) node->method = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "placeholder") == 0) node->placeholder = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "hidden") == 0) node->style.display_none = 1;
        else if (strcasecmp(key, "type") == 0) {
            node->input_type = html5_arena_strdup(&g_arena, val);
            if (strcasecmp(val, "hidden") == 0) node->style.display_none = 1;
        }
        else if (strcasecmp(key, "align") == 0) {
            if (strcasecmp(val, "center") == 0) { node->style.text_align = 1; node->style.has_explicit_align = 1; }
            else if (strcasecmp(val, "right") == 0) { node->style.text_align = 2; node->style.has_explicit_align = 1; }
        }
        else if (strcasecmp(key, "onclick") == 0) node->onclick = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "width") == 0) {
            int w = 0; sscanf(val, "%d", &w);
            if (strchr(val, '%')) {
                if (w > 0 && w <= 100) node->style.width = -w;
            } else {
                if (w > 0) node->style.width = w;
            }
        } else if (strcasecmp(key, "height") == 0) {
            if (!strchr(val, '%')) {
                int h = 0; sscanf(val, "%d", &h);
                if (h > 0) node->style.height = h;
            }
        }
        else if (strcasecmp(key, "for") == 0) node->for_id = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "rows") == 0) {
            int r = 0; sscanf(val, "%d", &r);
            if (r > 0) node->rows = r;
        }
        else if (strcasecmp(key, "cols") == 0) {
            int c = 0; sscanf(val, "%d", &c);
            if (c > 0) node->cols = c;
        }
        else if (strcasecmp(key, "size") == 0) {
            int s = 0; sscanf(val, "%d", &s);
            if (s > 0) node->size = s;
        }
        else if (strcasecmp(key, "disabled") == 0) node->disabled = 1;
        else if (strcasecmp(key, "readonly") == 0) node->read_only = 1;
        else if (strcasecmp(key, "style") == 0) {
            css_apply_declarations(node, val, strlen(val));
        }
    }
}

static int is_script_leakage(const char *s) {
    if (!s || !s[0]) return 0;
    if (strstr(s, "document.") || strstr(s, "querySelector") || strstr(s, "function(") ||
        strstr(s, "var ") || strstr(s, "let ") || strstr(s, "const ") || strstr(s, "window.") ||
        strstr(s, "return ") || strstr(s, "addEventListener") || strstr(s, "/*") ||
        strstr(s, "onclick=") || strstr(s, "catch(") || strstr(s, "catch (") ||
        strstr(s, "throw ") || strstr(s, "typeof ") || strstr(s, "try{") || strstr(s, "try {") ||
        strstr(s, "=>") || strstr(s, "function ") || strstr(s, "function()") ||
        strstr(s, ".call(") || strstr(s, ".apply(") || strstr(s, "eval(") ||
        strstr(s, "void 0") || strstr(s, "=== void") || strstr(s, "prototype.")) {
        return 1;
    }
    return 0;
}

int html5_node_is_inside_body(const html_node_t *node) {
    if (!node) return 0;
    if (html5_node_is_non_visual(node)) return 0;

    if (g_has_explicit_body) {
        const html_node_t *cur = node;
        while (cur) {
            if (cur->tag == HTML_TAG_BODY) {
                return 1;
            }
            cur = cur->parent;
        }
        return 0;
    }

    const html_node_t *cur = node;
    while (cur) {
        if (cur->tag == HTML_TAG_HEAD || cur->tag == HTML_TAG_SCRIPT ||
            cur->tag == HTML_TAG_STYLE || cur->tag == HTML_TAG_META ||
            cur->tag == HTML_TAG_LINK || cur->tag == HTML_TAG_TITLE ||
            cur->tag == HTML_TAG_SVG) {
            return 0;
        }
        cur = cur->parent;
    }
    return 1;
}

int html5_node_is_non_visual(const html_node_t *node) {
    if (!node) return 1;
    if (node->style.display_none) return 1;
    if (node->tag == HTML_TAG_HEAD || node->tag == HTML_TAG_SCRIPT ||
        node->tag == HTML_TAG_STYLE || node->tag == HTML_TAG_META ||
        node->tag == HTML_TAG_LINK || node->tag == HTML_TAG_TITLE ||
        node->tag == HTML_TAG_SVG) {
        return 1;
    }
    const html_node_t *cur = node->parent;
    while (cur) {
        if (cur->tag == HTML_TAG_HEAD || cur->tag == HTML_TAG_SCRIPT ||
            cur->tag == HTML_TAG_STYLE || cur->tag == HTML_TAG_META ||
            cur->tag == HTML_TAG_LINK || cur->tag == HTML_TAG_TITLE ||
            cur->tag == HTML_TAG_SVG || cur->style.display_none) {
            return 1;
        }
        cur = cur->parent;
    }
    return 0;
}

int html5_node_is_renderable_text(const html_node_t *node) {
    if (!node || node->tag != HTML_TAG_TEXT_NODE) return 0;
    if (!node->text || !node->text[0]) return 0;
    if (html5_node_is_non_visual(node)) return 0;
    if (!html5_node_is_inside_body(node)) return 0;
    if (is_script_leakage(node->text)) return 0;
    return 1;
}

/* Apply default CSS stylesheet rules according to tag */
static void apply_default_styles(html_node_t *node) {
    html_style_t *s = &node->style;
    switch (node->tag) {
        case HTML_TAG_HEAD:
        case HTML_TAG_STYLE:
        case HTML_TAG_SCRIPT:
        case HTML_TAG_META:
        case HTML_TAG_LINK:
        case HTML_TAG_TITLE:
        case HTML_TAG_SVG:
            s->display_none = 1;
            s->is_block = 0;
            break;
        case HTML_TAG_H1:
            s->color = 0x202124; s->font_size = 2; s->font_bold = 1;
            s->margin_top = 18; s->margin_bottom = 12; s->is_block = 1;
            break;
        case HTML_TAG_H2:
            s->color = 0x202124; s->font_size = 2; s->font_bold = 1;
            s->margin_top = 14; s->margin_bottom = 8; s->is_block = 1;
            break;
        case HTML_TAG_H3:
        case HTML_TAG_H4:
        case HTML_TAG_H5:
        case HTML_TAG_H6:
            s->color = 0x202124; s->font_size = 1; s->font_bold = 1;
            s->margin_top = 10; s->margin_bottom = 6; s->is_block = 1;
            break;
        case HTML_TAG_P:
            s->color = 0x202124; s->font_size = 1;
            s->margin_top = 4; s->margin_bottom = 12; s->is_block = 1;
            break;
        case HTML_TAG_A:
            s->color = 0x1A0DAB; s->font_underline = 1; s->is_block = 0; s->margin_right = 6;
            break;
        case HTML_TAG_B:
        case HTML_TAG_STRONG:
        case HTML_TAG_DT:
        case HTML_TAG_SUMMARY:
        case HTML_TAG_LEGEND:
            s->font_bold = 1;
            s->is_block = (node->tag == HTML_TAG_DT || node->tag == HTML_TAG_SUMMARY || node->tag == HTML_TAG_LEGEND);
            if (node->tag == HTML_TAG_DT) { s->margin_top = 8; s->margin_bottom = 2; }
            break;
        case HTML_TAG_DD:
            s->is_block = 1; s->margin_left = 20; s->margin_bottom = 6;
            break;
        case HTML_TAG_I:
        case HTML_TAG_EM:
        case HTML_TAG_ADDRESS:
            s->font_italic = 1;
            s->is_block = (node->tag == HTML_TAG_ADDRESS);
            break;
        case HTML_TAG_U:
            s->font_underline = 1; s->is_block = 0;
            break;
        case HTML_TAG_MARK:
            s->bg_color = 0xFEF08A; s->has_bg = 1; s->color = 0x202124; s->is_block = 0;
            break;
        case HTML_TAG_KBD:
        case HTML_TAG_SAMP:
            s->color = 0x202124; s->bg_color = 0xE8EAED; s->has_bg = 1;
            s->border_width = 1; s->border_color = 0xDADCE0; s->border_radius = 3;
            s->padding_left = 4; s->padding_right = 4; s->is_block = 0;
            break;
        case HTML_TAG_SMALL:
            s->font_size = 1;
            s->color = 0x54595D;
            s->has_explicit_color = 1;
            s->is_block = 0;
            break;
        case HTML_TAG_SUB:
        case HTML_TAG_SUP:
        case HTML_TAG_TIME:
            s->is_block = 0;
            break;
        case HTML_TAG_FIGCAPTION:
            s->color = 0x5F6368; s->font_italic = 1;
            s->margin_top = 4; s->margin_bottom = 8; s->is_block = 1;
            break;
        case HTML_TAG_BLOCKQUOTE:
            s->color = 0x5F6368; s->font_italic = 1;
            s->border_width = 3; s->border_color = 0xDADCE0;
            s->padding_left = 14; s->margin_top = 8; s->margin_bottom = 14; s->is_block = 1;
            break;
        case HTML_TAG_PRE:
            s->color = 0x202124; s->bg_color = 0xF1F3F4; s->has_bg = 1;
            s->padding_top = 6; s->padding_bottom = 6; s->padding_left = 10; s->padding_right = 10;
            s->border_radius = 4; s->margin_top = 6; s->margin_bottom = 12; s->is_block = 1;
            break;
        case HTML_TAG_CODE:
            s->color = 0x202124; s->bg_color = 0xF1F3F4; s->has_bg = 1;
            s->padding_left = 4; s->padding_right = 4;
            s->border_radius = 3; s->is_block = 0;
            break;
        case HTML_TAG_HR:
            s->border_width = 1; s->border_color = 0xDFE1E5;
            s->margin_top = 14; s->margin_bottom = 14; s->is_block = 1; s->height = 1;
            break;
        case HTML_TAG_INPUT: {
            int is_btn = (strcasecmp(node->input_type, "submit") == 0 || strcasecmp(node->input_type, "button") == 0);
            if (is_btn) {
                s->color = 0x3C4043; s->bg_color = 0xF8F9FA; s->has_bg = 1;
                s->border_width = 1; s->border_color = 0xDADCE0; s->border_radius = 4;
                s->padding_top = 6; s->padding_bottom = 6; s->padding_left = 16; s->padding_right = 16;
                s->margin_top = 4; s->margin_bottom = 8; s->margin_left = 4; s->margin_right = 4;
                s->is_block = 0; s->is_inline_block = 1;
            } else {
                s->color = 0x202124; s->bg_color = 0xFFFFFF; s->has_bg = 1;
                s->border_width = 1; s->border_color = 0xDFE1E5; s->border_radius = 4;
                s->padding_top = 6; s->padding_bottom = 6; s->padding_left = 8; s->padding_right = 8;
                s->height = (s->height > 0) ? s->height : 28;
                s->margin_top = 4; s->margin_bottom = 8; s->is_block = 0; s->is_inline_block = 1;
            }
            break;
        }
        case HTML_TAG_BUTTON:
        case HTML_TAG_SELECT:
            s->color = 0x3C4043; s->bg_color = 0xF8F9FA; s->has_bg = 1;
            s->border_width = 1; s->border_color = 0xDADCE0;
            s->border_radius = 4; s->padding_top = 6; s->padding_bottom = 6;
            s->padding_left = 12; s->padding_right = 12;
            s->margin_top = 4; s->margin_bottom = 8; s->margin_left = 4; s->margin_right = 4;
            s->is_block = 0; s->is_inline_block = 1;
            break;
        case HTML_TAG_TEXTAREA:
            s->color = 0x202124; s->bg_color = 0xFFFFFF; s->has_bg = 1;
            s->border_width = 1; s->border_color = 0xDFE1E5; s->border_radius = 4;
            s->padding_top = 6; s->padding_bottom = 6; s->padding_left = 8; s->padding_right = 8;
            s->margin_top = 4; s->margin_bottom = 8; s->is_block = 0; s->is_inline_block = 1;
            break;
        case HTML_TAG_FIELDSET:
            s->border_width = 1; s->border_color = 0xDADCE0; s->border_radius = 4;
            s->padding_top = 8; s->padding_bottom = 8; s->padding_left = 12; s->padding_right = 12;
            s->margin_top = 8; s->margin_bottom = 12; s->is_block = 1;
            break;
        case HTML_TAG_LABEL:
        case HTML_TAG_OPTION:
            s->color = 0x202124; s->is_block = 0; s->is_inline_block = 0;
            break;
        case HTML_TAG_TABLE:
            s->border_width = 0; s->border_color = 0xDFE1E5;
            s->margin_top = 6; s->margin_bottom = 12; s->is_block = 1;
            break;
        case HTML_TAG_TH:
            s->color = 0x202124; s->bg_color = 0xF1F3F4; s->has_bg = 1;
            s->font_bold = 1; s->padding_top = 4; s->padding_bottom = 4;
            s->padding_left = 8; s->padding_right = 8; s->is_block = 0;
            break;
        case HTML_TAG_TD:
            s->color = 0x202124; s->bg_color = 0xFFFFFF; s->has_bg = 0;
            s->padding_top = 4; s->padding_bottom = 4; s->padding_left = 8; s->padding_right = 8;
            s->is_block = 0;
            break;
        case HTML_TAG_IMG:
            s->is_block = 0;
            s->margin_top = 4; s->margin_bottom = 8; s->margin_right = 10;
            break;
        case HTML_TAG_VIDEO:
            s->is_block = 1;
            s->width = (s->width > 0) ? s->width : 520;
            s->height = (s->height > 0) ? s->height : 292;
            s->margin_top = 10; s->margin_bottom = 16;
            break;
        case HTML_TAG_CENTER:
            s->is_block = 1;
            s->text_align = 1;
            s->margin_top = 4;
            s->margin_bottom = 4;
            break;
        default:
            s->is_block = (node->tag == HTML_TAG_HTML || node->tag == HTML_TAG_BODY ||
                           node->tag == HTML_TAG_DIV || node->tag == HTML_TAG_SECTION ||
                           node->tag == HTML_TAG_ARTICLE || node->tag == HTML_TAG_HEADER ||
                           node->tag == HTML_TAG_FOOTER || node->tag == HTML_TAG_NAV ||
                           node->tag == HTML_TAG_MAIN || node->tag == HTML_TAG_ASIDE ||
                           node->tag == HTML_TAG_FORM || node->tag == HTML_TAG_DL ||
                           node->tag == HTML_TAG_UL || node->tag == HTML_TAG_OL ||
                           node->tag == HTML_TAG_LI || node->tag == HTML_TAG_TABLE ||
                           node->tag == HTML_TAG_THEAD || node->tag == HTML_TAG_TBODY ||
                           node->tag == HTML_TAG_TR || node->tag == HTML_TAG_CENTER ||
                           node->tag == HTML_TAG_NOSCRIPT || node->tag == HTML_TAG_FIGURE ||
                           node->tag == HTML_TAG_FIGCAPTION || node->tag == HTML_TAG_DETAILS ||
                           node->tag == HTML_TAG_SUMMARY || node->tag == HTML_TAG_DIALOG);
            break;
    }
}

static int css_starts(const char *s, const char *p) {
    while (*p) {
        if (h_tolower(*s++) != h_tolower(*p++)) return 0;
    }
    return 1;
}

/* CSS Custom Properties (--var) table */
#define MAX_CSS_VARS 96
typedef struct {
    char name[48];
    char val[96];
} css_var_entry_t;

static css_var_entry_t g_css_vars[MAX_CSS_VARS];
static int g_css_var_count = 0;

static void css_vars_reset(void) {
    g_css_var_count = 0;
}

static void css_var_set(const char *name, const char *val) {
    if (!name || name[0] != '-' || name[1] != '-' || !val) return;
    for (int i = 0; i < g_css_var_count; i++) {
        if (strcmp(g_css_vars[i].name, name) == 0) {
            strncpy(g_css_vars[i].val, val, sizeof(g_css_vars[i].val) - 1);
            g_css_vars[i].val[sizeof(g_css_vars[i].val) - 1] = 0;
            return;
        }
    }
    if (g_css_var_count < MAX_CSS_VARS) {
        strncpy(g_css_vars[g_css_var_count].name, name, sizeof(g_css_vars[0].name) - 1);
        g_css_vars[g_css_var_count].name[sizeof(g_css_vars[0].name) - 1] = 0;
        strncpy(g_css_vars[g_css_var_count].val, val, sizeof(g_css_vars[0].val) - 1);
        g_css_vars[g_css_var_count].val[sizeof(g_css_vars[0].val) - 1] = 0;
        g_css_var_count++;
    }
}

static const char *css_var_get(const char *name, int name_len) {
    if (!name || name_len <= 2) return NULL;
    for (int i = 0; i < g_css_var_count; i++) {
        if ((int)strlen(g_css_vars[i].name) == name_len &&
            strncmp(g_css_vars[i].name, name, name_len) == 0) {
            return g_css_vars[i].val;
        }
    }
    return NULL;
}

static void css_trim(char *s) {
    int n = strlen(s), a = 0;
    while (a < n && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (n > a && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n' || s[n-1] == ';')) n--;
    if (a > 0) {
        int j;
        for (j = 0; j < n - a; j++) s[j] = s[j + a];
    }
    s[n - a] = 0;
    /* Strip !important suffix if present */
    char *bang = strchr(s, '!');
    if (bang) {
        *bang = 0;
        int m = (int)(bang - s);
        while (m > 0 && (s[m-1] == ' ' || s[m-1] == '\t')) s[--m] = 0;
    }
}

/* Resolve var(--name, fallback) in-place */
static void css_resolve_vars(char *val, int max_len) {
    char *vp = strstr(val, "var(");
    if (!vp) return;
    char *open_p = vp + 4;
    while (*open_p == ' ' || *open_p == '\t') open_p++;
    char *end_p = strchr(open_p, ')');
    if (!end_p) return;

    char *comma = strchr(open_p, ',');
    int name_len = 0;
    const char *fallback = NULL;
    int fb_len = 0;
    if (comma && comma < end_p) {
        name_len = (int)(comma - open_p);
        fallback = comma + 1;
        while (fallback < end_p && (*fallback == ' ' || *fallback == '\t')) fallback++;
        fb_len = (int)(end_p - fallback);
    } else {
        name_len = (int)(end_p - open_p);
    }
    while (name_len > 0 && (open_p[name_len - 1] == ' ' || open_p[name_len - 1] == '\t')) name_len--;

    const char *resolved = css_var_get(open_p, name_len);
    char replacement[96] = {0};
    if (resolved && resolved[0]) {
        strncpy(replacement, resolved, sizeof(replacement) - 1);
    } else if (fallback && fb_len > 0) {
        if (fb_len >= (int)sizeof(replacement)) fb_len = sizeof(replacement) - 1;
        strncpy(replacement, fallback, fb_len);
        replacement[fb_len] = 0;
    }

    char out[160];
    int prefix_len = (int)(vp - val);
    int rep_len = (int)strlen(replacement);
    int suffix_len = (int)strlen(end_p + 1);
    if (prefix_len + rep_len + suffix_len < max_len - 1 && prefix_len + rep_len + suffix_len < (int)sizeof(out) - 1) {
        memcpy(out, val, prefix_len);
        memcpy(out + prefix_len, replacement, rep_len);
        memcpy(out + prefix_len + rep_len, end_p + 1, suffix_len + 1);
        strcpy(val, out);
    }
}

/* Parse CSS length with support for px, rem, em, vw, vh, pt, %, calc() */
static int css_parse_length(const char *val, int allow_pct, int default_val) {
    if (!val || !val[0]) return default_val;
    const char *p = val;
    if (css_starts(p, "calc(")) {
        p += 5;
        while (*p == ' ' || *p == '\t' || *p == '(') p++;
    }
    while (*p == ' ' || *p == '\t') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') { p++; }

    if (*p < '0' && *p != '.') return default_val;
    if (*p > '9' && *p != '.') return default_val;

    int whole = 0;
    while (*p >= '0' && *p <= '9') {
        whole = whole * 10 + (*p - '0');
        p++;
    }
    int frac = 0, frac_div = 1;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            if (frac_div < 1000) {
                frac = frac * 10 + (*p - '0');
                frac_div *= 10;
            }
            p++;
        }
    }
    while (*p == ' ' || *p == '\t') p++;

    int res = whole;
    if (*p == '%') {
        if (allow_pct) {
            if (whole <= 0) return 0;
            if (whole > 100) whole = 100;
            return -whole;
        }
        return default_val;
    } else if (css_starts(p, "rem") || css_starts(p, "em")) {
        res = whole * 16 + (frac * 16) / frac_div;
    } else if (css_starts(p, "vw")) {
        res = (whole * 1024) / 100;
    } else if (css_starts(p, "vh")) {
        res = (whole * 640) / 100;
    } else if (css_starts(p, "pt")) {
        res = (whole * 4) / 3;
    } else if (css_starts(p, "ch")) {
        res = whole * 8;
    }
    return neg ? -res : res;
}

static u32 css_color_ex(const char *v, u32 fallback, u8 *out_transparent) {
    if (out_transparent) *out_transparent = 0;
    if (!v || !v[0]) {
        if (out_transparent) *out_transparent = 1;
        return fallback;
    }
    while (*v == ' ' || *v == '\t') v++;
    if (css_starts(v, "transparent") || css_starts(v, "none") ||
        css_starts(v, "initial") || css_starts(v, "unset") || css_starts(v, "inherit")) {
        if (out_transparent) *out_transparent = 1;
        return fallback;
    }
    if (v[0] == '#') {
        unsigned int n = 0;
        int digits = 0;
        for (int i = 1; i <= 8 && v[i]; i++) {
            char c = v[i];
            unsigned int d = (c >= '0' && c <= '9') ? (unsigned int)(c - '0') :
                             (c >= 'a' && c <= 'f') ? (unsigned int)(c - 'a' + 10) :
                             (c >= 'A' && c <= 'F') ? (unsigned int)(c - 'A' + 10) : 16;
            if (d > 15) break;
            n = (n << 4) | d;
            digits++;
        }
        if (digits == 3) {
            unsigned int r = (n >> 8) & 0xF, g = (n >> 4) & 0xF, b = n & 0xF;
            return (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
        }
        if (digits == 4) {
            unsigned int a = n & 0xF;
            if (a < 2 && out_transparent) *out_transparent = 1;
            unsigned int r = (n >> 12) & 0xF, g = (n >> 8) & 0xF, b = (n >> 4) & 0xF;
            return (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
        }
        if (digits == 6) return n;
        if (digits == 8) {
            unsigned int a = n & 0xFF;
            if (a < 20 && out_transparent) *out_transparent = 1;
            return (n >> 8) & 0xFFFFFF;
        }
        return fallback;
    }
    if (css_starts(v, "rgb")) {
        const char *p = strchr(v, '(');
        if (!p) return fallback;
        p++;
        int r = 0, g = 0, b = 0;
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { r = r * 10 + (*p - '0'); p++; }
        while (*p && (*p == ' ' || *p == ',' || *p == '/')) p++;
        while (*p >= '0' && *p <= '9') { g = g * 10 + (*p - '0'); p++; }
        while (*p && (*p == ' ' || *p == ',' || *p == '/')) p++;
        while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; }
        while (*p && (*p == ' ' || *p == ',' || *p == '/')) p++;
        if (*p == '0' && (p[1] == ')' || p[1] == ' ' || (p[1] == '.' && p[2] == '0'))) {
            if (out_transparent) *out_transparent = 1;
        }
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
    }
    if (css_starts(v, "hsl")) {
        const char *p = strchr(v, '(');
        if (!p) return fallback;
        p++;
        int h = 0, s = 0, l = 0;
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { h = h * 10 + (*p - '0'); p++; }
        while (*p && (*p < '0' || *p > '9')) p++;
        while (*p >= '0' && *p <= '9') { s = s * 10 + (*p - '0'); p++; }
        while (*p && (*p < '0' || *p > '9')) p++;
        while (*p >= '0' && *p <= '9') { l = l * 10 + (*p - '0'); p++; }
        if (l >= 95) return 0xF8FAFC;
        if (l <= 10) return 0x111827;
        u32 gray = (u32)((l * 255) / 100);
        if (s <= 12) return (gray << 16) | (gray << 8) | gray;
        return fallback;
    }
    if (css_starts(v, "white") || css_starts(v, "snow") || css_starts(v, "ivory") || css_starts(v, "ghostwhite")) return 0xFFFFFF;
    if (css_starts(v, "whitesmoke") || css_starts(v, "aliceblue") || css_starts(v, "beige")) return 0xF5F5F5;
    if (css_starts(v, "black")) return 0x000000;
    if (css_starts(v, "red") || css_starts(v, "crimson") || css_starts(v, "tomato")) return 0xEF4444;
    if (css_starts(v, "darkred") || css_starts(v, "maroon") || css_starts(v, "firebrick")) return 0x991B1B;
    if (css_starts(v, "green") || css_starts(v, "forestgreen") || css_starts(v, "seagreen")) return 0x22C55E;
    if (css_starts(v, "darkgreen")) return 0x166534;
    if (css_starts(v, "blue") || css_starts(v, "royalblue") || css_starts(v, "dodgerblue")) return 0x2563EB;
    if (css_starts(v, "darkblue") || css_starts(v, "navy") || css_starts(v, "midnightblue")) return 0x1E3A8A;
    if (css_starts(v, "skyblue") || css_starts(v, "lightblue") || css_starts(v, "deepskyblue")) return 0x38BDF8;
    if (css_starts(v, "gray") || css_starts(v, "grey") || css_starts(v, "slategray")) return 0x6B7280;
    if (css_starts(v, "lightgray") || css_starts(v, "lightgrey") || css_starts(v, "silver") || css_starts(v, "gainsboro")) return 0xE5E7EB;
    if (css_starts(v, "darkgray") || css_starts(v, "darkgrey") || css_starts(v, "dimgray") || css_starts(v, "dimgrey")) return 0x4B5563;
    if (css_starts(v, "orange") || css_starts(v, "darkorange") || css_starts(v, "coral")) return 0xF97316;
    if (css_starts(v, "yellow") || css_starts(v, "gold")) return 0xEAB308;
    if (css_starts(v, "lightyellow") || css_starts(v, "lemonchiffon") || css_starts(v, "cornsilk")) return 0xFEF9C3;
    if (css_starts(v, "purple") || css_starts(v, "violet") || css_starts(v, "indigo") || css_starts(v, "blueviolet")) return 0x7C3AED;
    if (css_starts(v, "teal") || css_starts(v, "darkcyan") || css_starts(v, "cadetblue")) return 0x0D9488;
    if (css_starts(v, "cyan") || css_starts(v, "aqua") || css_starts(v, "turquoise")) return 0x06B6D4;
    if (css_starts(v, "pink") || css_starts(v, "hotpink") || css_starts(v, "magenta") || css_starts(v, "fuchsia")) return 0xEC4899;
    if (css_starts(v, "brown") || css_starts(v, "saddlebrown") || css_starts(v, "sienna") || css_starts(v, "chocolate")) return 0x78350F;
    if (out_transparent) *out_transparent = 1;
    return fallback;
}

static u32 css_color(const char *v, u32 fallback) {
    u8 tr = 0;
    return css_color_ex(v, fallback, &tr);
}

static void css_apply_declarations(html_node_t *node, const char *decls, int len) {
    int i = 0;
    int is_root_or_body = (node && (node->tag == HTML_TAG_HTML || node->tag == HTML_TAG_BODY || node->tag == HTML_TAG_MAIN));
    while (i < len) {
        int ks = i;
        while (i < len && decls[i] != ':' && decls[i] != ';') i++;
        if (i >= len || decls[i] != ':') { while (i < len && decls[i] != ';') i++; i++; continue; }
        int kl = i - ks; i++;
        int vs = i;
        int paren_depth = 0;
        while (i < len && (decls[i] != ';' || paren_depth > 0)) {
            if (decls[i] == '(') paren_depth++;
            else if (decls[i] == ')' && paren_depth > 0) paren_depth--;
            i++;
        }
        int vl = i - vs;
        char key[48], val[160];
        if (kl >= (int)sizeof(key)) kl = sizeof(key) - 1;
        if (vl >= (int)sizeof(val)) vl = sizeof(val) - 1;
        strncpy(key, decls + ks, kl); key[kl] = 0; css_trim(key);
        strncpy(val, decls + vs, vl); val[vl] = 0; css_trim(val);

        if (key[0] == '-' && key[1] == '-') {
            css_var_set(key, val);
            if (i < len && decls[i] == ';') i++;
            continue;
        }
        if (strstr(val, "var(")) {
            css_resolve_vars(val, sizeof(val));
        }

        if (strcasecmp(key, "color") == 0) {
            u8 tr = 0;
            u32 c = css_color_ex(val, node->style.color, &tr);
            if (!tr) {
                node->style.color = c;
                node->style.has_explicit_color = 1;
            }
        } else if (strcasecmp(key, "background") == 0 || strcasecmp(key, "background-color") == 0) {
            u8 tr = 0;
            const char *col_str = val;
            if (strstr(val, "url(") || strstr(val, "gradient(")) {
                const char *hash = strchr(val, '#');
                const char *rgb = strstr(val, "rgb");
                col_str = hash ? hash : (rgb ? rgb : "");
            }
            u32 bg = css_color_ex(col_str, node->style.bg_color, &tr);
            if (tr) {
                if (css_starts(val, "transparent") || css_starts(val, "none")) {
                    node->style.has_bg = 0;
                }
            } else {
                node->style.bg_color = bg;
                node->style.has_bg = 1;
            }
        } else if (strcasecmp(key, "font-size") == 0) {
            int px = css_parse_length(val, 0, 14);
            node->style.font_size = (node->tag == HTML_TAG_SMALL) ? 1 : (px >= 18 ? 2 : 1);
        } else if (strcasecmp(key, "font-weight") == 0) {
            if (css_starts(val, "bold") || css_starts(val, "600") || css_starts(val, "700") ||
                css_starts(val, "800") || css_starts(val, "900")) {
                node->style.font_bold = 1;
            } else if (css_starts(val, "normal") || css_starts(val, "400") || css_starts(val, "300")) {
                node->style.font_bold = 0;
            }
        } else if (strcasecmp(key, "font-style") == 0 && css_starts(val, "italic")) {
            node->style.font_italic = 1;
        } else if (strcasecmp(key, "text-decoration") == 0) {
            if (css_starts(val, "none")) node->style.font_underline = 0;
            else if (strstr(val, "underline")) node->style.font_underline = 1;
        } else if (strcasecmp(key, "text-align") == 0) {
            node->style.text_align = css_starts(val, "center") ? 1 : (css_starts(val, "right") || css_starts(val, "end")) ? 2 : 0;
            node->style.has_explicit_align = 1;
        } else if (strcasecmp(key, "position") == 0) {
            if (css_starts(val, "relative")) node->style.position = POS_RELATIVE;
            else if (css_starts(val, "absolute")) node->style.position = POS_ABSOLUTE;
            else if (css_starts(val, "fixed")) node->style.position = POS_FIXED;
            else node->style.position = POS_STATIC;
        } else if (strcasecmp(key, "display") == 0) {
            if (node->tag == HTML_TAG_HEAD || node->tag == HTML_TAG_STYLE ||
                node->tag == HTML_TAG_SCRIPT || node->tag == HTML_TAG_META ||
                node->tag == HTML_TAG_LINK || node->tag == HTML_TAG_TITLE ||
                node->tag == HTML_TAG_SVG) {
                node->style.display_none = 1;
                node->style.is_block = 0;
            } else if (css_starts(val, "none")) {
                if (!is_root_or_body) {
                    node->style.display_none = 1;
                }
            } else if (css_starts(val, "inline-block")) {
                node->style.is_block = 0;
                node->style.is_inline_block = 1;
                node->style.is_flex = 0;
                node->style.display_none = 0;
            } else if (css_starts(val, "inline-flex") || css_starts(val, "inline-grid")) {
                node->style.is_block = 0;
                node->style.is_inline_block = 1;
                node->style.is_flex = 1;
                node->style.display_none = 0;
            } else if (css_starts(val, "flex")) {
                node->style.is_block = 1;
                node->style.is_inline_block = 0;
                node->style.is_flex = 1;
                node->style.display_none = 0;
            } else if (css_starts(val, "grid")) {
                node->style.is_block = 1;
                node->style.is_inline_block = 0;
                node->style.is_flex = 1;
                node->style.is_grid = 1;
                node->style.flex_wrap = 1;
                node->style.display_none = 0;
            } else if (css_starts(val, "inline")) {
                node->style.is_block = 0;
                node->style.is_inline_block = 0;
                node->style.is_flex = 0;
                node->style.display_none = 0;
            } else {
                node->style.is_block = 1;
                node->style.is_inline_block = 0;
                node->style.is_flex = 0;
                node->style.display_none = 0;
            }
        }
        else if (strcasecmp(key, "grid-template-columns") == 0) {
            int cols = 0;
            const char *rep = strstr(val, "repeat(");
            if (rep) {
                sscanf(rep + 7, "%d", &cols);
            } else {
                const char *p = val;
                while (*p) {
                    while (*p == ' ' || *p == '\t') p++;
                    if (!*p) break;
                    cols++;
                    while (*p && *p != ' ' && *p != '\t') p++;
                }
            }
            if (cols > 0 && cols <= 12) node->style.grid_cols = (u8)cols;
        }
        else if (strcasecmp(key, "box-sizing") == 0) {
            node->style.box_sizing = css_starts(val, "border-box") ? 1 : 0;
        }
        else if (strcasecmp(key, "flex-direction") == 0) {
            if (css_starts(val, "column-reverse")) node->style.flex_direction = 3;
            else if (css_starts(val, "column")) node->style.flex_direction = 1;
            else if (css_starts(val, "row-reverse")) node->style.flex_direction = 2;
            else node->style.flex_direction = 0;
        }
        else if (strcasecmp(key, "flex-wrap") == 0) {
            if (css_starts(val, "wrap-reverse")) node->style.flex_wrap = 2;
            else if (css_starts(val, "wrap")) node->style.flex_wrap = 1;
            else node->style.flex_wrap = 0;
        }
        else if (strcasecmp(key, "justify-content") == 0) {
            if (css_starts(val, "center")) node->style.justify_content = 1;
            else if (css_starts(val, "flex-end") || css_starts(val, "end")) node->style.justify_content = 2;
            else if (css_starts(val, "space-between")) node->style.justify_content = 3;
            else if (css_starts(val, "space-around")) node->style.justify_content = 4;
            else if (css_starts(val, "space-evenly")) node->style.justify_content = 5;
            else node->style.justify_content = 0;
        }
        else if (strcasecmp(key, "align-items") == 0) {
            if (css_starts(val, "center")) node->style.align_items = 1;
            else if (css_starts(val, "flex-end") || css_starts(val, "end")) node->style.align_items = 2;
            else if (css_starts(val, "flex-start") || css_starts(val, "start")) node->style.align_items = 3;
            else if (css_starts(val, "baseline")) node->style.align_items = 4;
            else node->style.align_items = 0; /* stretch */
        }
        else if (strcasecmp(key, "align-self") == 0) {
            if (css_starts(val, "center")) node->style.align_self = 2;
            else if (css_starts(val, "flex-end") || css_starts(val, "end")) node->style.align_self = 3;
            else if (css_starts(val, "flex-start") || css_starts(val, "start")) node->style.align_self = 1;
            else if (css_starts(val, "stretch")) node->style.align_self = 4;
            else if (css_starts(val, "baseline")) node->style.align_self = 5;
            else node->style.align_self = 0;
        }
        else if (strcasecmp(key, "align-content") == 0) {
            if (css_starts(val, "flex-start") || css_starts(val, "start")) node->style.align_content = 1;
            else if (css_starts(val, "center")) node->style.align_content = 2;
            else if (css_starts(val, "flex-end") || css_starts(val, "end")) node->style.align_content = 3;
            else if (css_starts(val, "space-between")) node->style.align_content = 4;
            else if (css_starts(val, "space-around")) node->style.align_content = 5;
            else node->style.align_content = 0; /* stretch */
        }
        else if (strcasecmp(key, "flex-grow") == 0) {
            int fg = 0;
            if (sscanf(val, "%d", &fg) == 1 && fg >= 0) {
                node->style.flex_grow = fg;
                node->style.has_flex_grow = 1;
            }
        }
        else if (strcasecmp(key, "flex-shrink") == 0) {
            int fs = 1;
            if (sscanf(val, "%d", &fs) == 1 && fs >= 0) {
                node->style.flex_shrink = fs;
                node->style.has_flex_shrink = 1;
            }
        }
        else if (strcasecmp(key, "flex-basis") == 0) {
            if (css_starts(val, "auto")) {
                node->style.flex_basis = -9999;
                node->style.has_flex_basis = 1;
            } else {
                node->style.flex_basis = css_parse_length(val, 1, -9999);
                node->style.has_flex_basis = 1;
            }
        }
        else if (strcasecmp(key, "flex") == 0) {
            if (css_starts(val, "none")) {
                node->style.flex_grow = 0; node->style.flex_shrink = 0; node->style.flex_basis = -9999;
                node->style.has_flex_grow = node->style.has_flex_shrink = node->style.has_flex_basis = 1;
            } else if (css_starts(val, "auto")) {
                node->style.flex_grow = 1; node->style.flex_shrink = 1; node->style.flex_basis = -9999;
                node->style.has_flex_grow = node->style.has_flex_shrink = node->style.has_flex_basis = 1;
            } else {
                int g = 0, s = 1;
                char b_tok[32] = {0};
                int parsed = sscanf(val, "%d %d %31s", &g, &s, b_tok);
                if (parsed >= 1) {
                    node->style.flex_grow = (g >= 0) ? g : 0;
                    node->style.has_flex_grow = 1;
                }
                if (parsed >= 2) {
                    node->style.flex_shrink = (s >= 0) ? s : 1;
                    node->style.has_flex_shrink = 1;
                }
                if (parsed >= 3) {
                    if (strcmp(b_tok, "auto") == 0) {
                        node->style.flex_basis = -9999;
                    } else {
                        node->style.flex_basis = css_parse_length(b_tok, 1, 0);
                    }
                    node->style.has_flex_basis = 1;
                }
            }
        }
        else if (strcasecmp(key, "gap") == 0 || strcasecmp(key, "grid-gap") == 0) {
            int g = css_parse_length(val, 0, 0);
            if (g >= 0) node->style.gap = g;
        }
        else if (strcasecmp(key, "row-gap") == 0) {
            int g = css_parse_length(val, 0, 0);
            if (g >= 0) node->style.row_gap = g;
        }
        else if (strcasecmp(key, "column-gap") == 0) {
            int g = css_parse_length(val, 0, 0);
            if (g >= 0) node->style.column_gap = g;
        }
        else if (strcasecmp(key, "width") == 0) {
            if (!css_starts(val, "auto")) {
                node->style.width = css_parse_length(val, 1, 0);
            }
        }
        else if (strcasecmp(key, "max-width") == 0) {
            if (css_starts(val, "none")) {
                node->style.max_width = 0;
            } else {
                node->style.max_width = css_parse_length(val, 1, 0);
            }
        }
        else if (strcasecmp(key, "min-width") == 0) {
            node->style.min_width = css_parse_length(val, 1, 0);
        }
        else if (strcasecmp(key, "height") == 0) {
            if (!is_root_or_body && !strchr(val, '%') && !css_starts(val, "auto") && !css_starts(val, "100vh")) {
                int h = css_parse_length(val, 0, 0);
                if (h > 0) node->style.height = h;
            }
        }
        else if (strcasecmp(key, "max-height") == 0) {
            if (!is_root_or_body && !css_starts(val, "none") && !strchr(val, '%')) {
                int mh = css_parse_length(val, 0, 0);
                if (mh > 0) node->style.max_height = mh;
            }
        }
        else if (strcasecmp(key, "min-height") == 0) {
            if (!strchr(val, '%') && !css_starts(val, "100vh")) {
                int mh = css_parse_length(val, 0, 0);
                if (mh > 0 && mh <= 400) node->style.min_height = mh;
            }
        }
        else if (strcasecmp(key, "margin") == 0) {
            char t1[32] = {0}, t2[32] = {0}, t3[32] = {0}, t4[32] = {0};
            int ntokens = sscanf(val, "%31s %31s %31s %31s", t1, t2, t3, t4);
            if (ntokens == 1) {
                if (strstr(t1, "auto")) {
                    node->style.margin_left_auto = 1;
                    node->style.margin_right_auto = 1;
                } else {
                    int p = css_parse_length(t1, 0, 0);
                    node->style.margin_top = node->style.margin_bottom = node->style.margin_left = node->style.margin_right = p;
                }
            } else if (ntokens == 2) {
                int tb = css_parse_length(t1, 0, 0);
                node->style.margin_top = node->style.margin_bottom = tb;
                if (strstr(t2, "auto")) {
                    node->style.margin_left_auto = 1;
                    node->style.margin_right_auto = 1;
                    node->style.margin_left = 0;
                    node->style.margin_right = 0;
                } else {
                    int lr = css_parse_length(t2, 0, 0);
                    node->style.margin_left = node->style.margin_right = lr;
                }
            } else if (ntokens == 3) {
                int top = css_parse_length(t1, 0, 0), bot = css_parse_length(t3, 0, 0);
                node->style.margin_top = top;
                node->style.margin_bottom = bot;
                if (strstr(t2, "auto")) {
                    node->style.margin_left_auto = 1;
                    node->style.margin_right_auto = 1;
                    node->style.margin_left = 0;
                    node->style.margin_right = 0;
                } else {
                    int lr = css_parse_length(t2, 0, 0);
                    node->style.margin_left = node->style.margin_right = lr;
                }
            } else if (ntokens >= 4) {
                node->style.margin_top = css_parse_length(t1, 0, 0);
                node->style.margin_bottom = css_parse_length(t3, 0, 0);
                if (strstr(t2, "auto")) { node->style.margin_right_auto = 1; node->style.margin_right = 0; }
                else { node->style.margin_right = css_parse_length(t2, 0, 0); }
                if (strstr(t4, "auto")) { node->style.margin_left_auto = 1; node->style.margin_left = 0; }
                else { node->style.margin_left = css_parse_length(t4, 0, 0); }
            }
        }
        else if (strcasecmp(key, "margin-left") == 0 || strcasecmp(key, "margin-inline-start") == 0) {
            if (strstr(val, "auto")) {
                node->style.margin_left_auto = 1;
                node->style.margin_left = 0;
            } else {
                node->style.margin_left = css_parse_length(val, 0, 0);
            }
        }
        else if (strcasecmp(key, "margin-right") == 0 || strcasecmp(key, "margin-inline-end") == 0) {
            if (strstr(val, "auto")) {
                node->style.margin_right_auto = 1;
                node->style.margin_right = 0;
            } else {
                node->style.margin_right = css_parse_length(val, 0, 0);
            }
        }
        else if (strcasecmp(key, "margin-inline") == 0) {
            if (strstr(val, "auto")) {
                node->style.margin_left_auto = 1;
                node->style.margin_right_auto = 1;
            } else {
                int m = css_parse_length(val, 0, 0);
                node->style.margin_left = node->style.margin_right = m;
            }
        }
        else if (strcasecmp(key, "margin-top") == 0 || strcasecmp(key, "margin-block-start") == 0) {
            node->style.margin_top = css_parse_length(val, 0, 0);
        }
        else if (strcasecmp(key, "margin-bottom") == 0 || strcasecmp(key, "margin-block-end") == 0) {
            node->style.margin_bottom = css_parse_length(val, 0, 0);
        }
        else if (strcasecmp(key, "padding") == 0) {
            char t1[32] = {0}, t2[32] = {0}, t3[32] = {0}, t4[32] = {0};
            int ntokens = sscanf(val, "%31s %31s %31s %31s", t1, t2, t3, t4);
            if (ntokens == 1) {
                int p = css_parse_length(t1, 0, 0);
                if (p >= 0 && p <= 120) node->style.padding_top = node->style.padding_bottom = node->style.padding_left = node->style.padding_right = p;
            } else if (ntokens == 2) {
                int tb = css_parse_length(t1, 0, 0), lr = css_parse_length(t2, 0, 0);
                if (tb >= 0 && tb <= 120) node->style.padding_top = node->style.padding_bottom = tb;
                if (lr >= 0 && lr <= 120) node->style.padding_left = node->style.padding_right = lr;
            } else if (ntokens == 3) {
                int top = css_parse_length(t1, 0, 0), lr = css_parse_length(t2, 0, 0), bot = css_parse_length(t3, 0, 0);
                if (top >= 0 && top <= 120) node->style.padding_top = top;
                if (lr >= 0 && lr <= 120) node->style.padding_left = node->style.padding_right = lr;
                if (bot >= 0 && bot <= 120) node->style.padding_bottom = bot;
            } else if (ntokens >= 4) {
                int top = css_parse_length(t1, 0, 0), right = css_parse_length(t2, 0, 0);
                int bot = css_parse_length(t3, 0, 0), left = css_parse_length(t4, 0, 0);
                if (top >= 0 && top <= 120) node->style.padding_top = top;
                if (right >= 0 && right <= 120) node->style.padding_right = right;
                if (bot >= 0 && bot <= 120) node->style.padding_bottom = bot;
                if (left >= 0 && left <= 120) node->style.padding_left = left;
            }
        }
        else if (strcasecmp(key, "padding-left") == 0 || strcasecmp(key, "padding-inline-start") == 0) {
            int p = css_parse_length(val, 0, 0);
            if (p >= 0 && p <= 120) node->style.padding_left = p;
        }
        else if (strcasecmp(key, "padding-right") == 0 || strcasecmp(key, "padding-inline-end") == 0) {
            int p = css_parse_length(val, 0, 0);
            if (p >= 0 && p <= 120) node->style.padding_right = p;
        }
        else if (strcasecmp(key, "padding-top") == 0 || strcasecmp(key, "padding-block-start") == 0) {
            int p = css_parse_length(val, 0, 0);
            if (p >= 0 && p <= 120) node->style.padding_top = p;
        }
        else if (strcasecmp(key, "padding-bottom") == 0 || strcasecmp(key, "padding-block-end") == 0) {
            int p = css_parse_length(val, 0, 0);
            if (p >= 0 && p <= 120) node->style.padding_bottom = p;
        }
        else if (strcasecmp(key, "border") == 0) {
            if (css_starts(val, "none") || css_starts(val, "0")) {
                node->style.border_width = 0;
            } else {
                int bw = css_parse_length(val, 0, 1);
                if (bw > 0 && bw <= 12) node->style.border_width = bw;
                const char *col_tok = strchr(val, '#');
                if (!col_tok) col_tok = strstr(val, "rgb");
                if (!col_tok) {
                    char t1[32] = {0}, t2[32] = {0}, t3[32] = {0};
                    int ntok = sscanf(val, "%31s %31s %31s", t1, t2, t3);
                    if (ntok >= 3) col_tok = t3;
                    else if (ntok == 2) col_tok = t2;
                }
                if (col_tok) node->style.border_color = css_color(col_tok, node->style.border_color);
            }
        }
        else if (strcasecmp(key, "border-top") == 0 || strcasecmp(key, "border-bottom") == 0 ||
                 strcasecmp(key, "border-left") == 0 || strcasecmp(key, "border-right") == 0) {
            int bw = css_parse_length(val, 0, 0);
            if (bw > 0 && bw <= 12) node->style.border_width = bw;
        }
        else if (strcasecmp(key, "border-width") == 0) {
            int bw = css_parse_length(val, 0, 0);
            if (bw >= 0 && bw <= 12) node->style.border_width = bw;
        }
        else if (strcasecmp(key, "border-color") == 0) {
            node->style.border_color = css_color(val, node->style.border_color);
        }
        else if (strcasecmp(key, "border-radius") == 0) {
            int br = css_parse_length(val, 0, 0);
            if (br < 0) br = 8;
            if (br > 32) br = 32;
            node->style.border_radius = br;
        }
        else if (strcasecmp(key, "list-style") == 0 || strcasecmp(key, "list-style-type") == 0) {
            if (css_starts(val, "none")) node->style.list_style_none = 1;
        }
        else if (strcasecmp(key, "visibility") == 0) {
            if (css_starts(val, "hidden") && !is_root_or_body) {
                node->style.visibility_hidden = 1;
                node->style.display_none = 1;
            } else if (css_starts(val, "visible") && !html5_node_is_non_visual(node)) {
                node->style.visibility_hidden = 0;
                node->style.display_none = 0;
            }
        }
        else if (strcasecmp(key, "opacity") == 0) {
            if (!is_root_or_body && css_starts(val, "0") &&
                (val[1] == '\0' || (val[1] == '.' && val[2] == '0') || val[1] == ' ' || val[1] == ';')) {
                node->style.display_none = 1;
            }
        }
        if (i < len && decls[i] == ';') i++;
    }
}

static int html_has_class(const char *class_list, const char *cls, int clen) {
    if (!class_list || !class_list[0] || !cls || clen <= 0) return 0;
    const char *p = class_list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        if (p - start == clen && strncmp(start, cls, clen) == 0) return 1;
    }
    return 0;
}

/* Pre-compiled CSS Selector Chain for O(1) per-node tag/id/class filtering */
#define CSS_MAX_CHAIN 6
#define CSS_MAX_GROUP 8

typedef struct {
    html_tag_t tag;      /* HTML_TAG_UNKNOWN = wildcard '*' */
    char id[40];
    char cls1[40];
    int  cls1_len;
    char cls2[40];
    int  cls2_len;
    u8   combinator;     /* 0 = target (rightmost), 1 = descendant ' ', 2 = child '>' */
} css_sel_part_t;

typedef struct {
    u8 valid;
    u8 count;            /* parts[0] is rightmost (target node), parts[1..] are ancestors */
    css_sel_part_t parts[CSS_MAX_CHAIN];
} css_compiled_sel_t;

static int css_compile_simple_part(const char *sel, int len, css_sel_part_t *out) {
    char tok[96];
    if (len <= 0 || len >= (int)sizeof(tok)) return 0;
    memcpy(tok, sel, len);
    tok[len] = 0;
    css_trim(tok);
    if (!tok[0]) return 0;

    /* Reject pseudo-elements (::before, ::after, ::-webkit-*, etc.) and attribute selectors ([hidden], [data-*]) */
    if (strstr(tok, "::") || strchr(tok, '[') || strchr(tok, '+') || strchr(tok, '~')) {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    out->tag = HTML_TAG_UNKNOWN;

    /* Check pseudo-classes (:root, :link, :hover, :not, :empty, etc.) */
    char *colon = strchr(tok, ':');
    if (colon) {
        if (strncasecmp(colon, ":root", 5) == 0 && (colon[5] == '\0' || colon[5] == ' ')) {
            *colon = '\0';
            if (!tok[0]) {
                out->tag = HTML_TAG_HTML;
                return 1;
            }
        } else if (strncasecmp(colon, ":link", 5) == 0 ||
                   strncasecmp(colon, ":first-child", 12) == 0 ||
                   strncasecmp(colon, ":last-child", 11) == 0) {
            *colon = '\0';
            if (!tok[0]) return 0;
        } else {
            /* Dynamic/negation/structural pseudo-class (:hover, :focus, :not, :empty, :disabled, :checked): do not apply statically */
            return 0;
        }
    }

    if (tok[0] == '*' && tok[1] == '\0') {
        return 1;
    }

    int p = 0;
    if (tok[p] != '.' && tok[p] != '#') {
        int ts = p;
        while (tok[p] && tok[p] != '.' && tok[p] != '#') p++;
        int tl = p - ts;
        if (!(tl == 1 && tok[ts] == '*')) {
            html_tag_t t = tag_from_name(tok + ts, tl);
            if (t == HTML_TAG_UNKNOWN) return 0; /* Unrecognized tag in CSS cannot match any known DOM tag */
            out->tag = t;
        }
    }

    while (tok[p]) {
        char kind = tok[p++];
        int s = p;
        while (tok[p] && tok[p] != '.' && tok[p] != '#') p++;
        int l = p - s;
        if (l <= 0 || l >= 39) return 0;
        if (kind == '#') {
            memcpy(out->id, tok + s, l);
            out->id[l] = 0;
        } else if (kind == '.') {
            if (!out->cls1[0]) {
                memcpy(out->cls1, tok + s, l);
                out->cls1[l] = 0;
                out->cls1_len = l;
            } else if (!out->cls2[0]) {
                memcpy(out->cls2, tok + s, l);
                out->cls2[l] = 0;
                out->cls2_len = l;
            }
        }
    }
    return 1;
}

static void css_compile_selector(const char *sel, int len, css_compiled_sel_t *out) {
    out->valid = 0;
    out->count = 0;

    int end = len;
    u8 next_comb = 0;
    while (end > 0 && out->count < CSS_MAX_CHAIN) {
        while (end > 0 && (sel[end-1] == ' ' || sel[end-1] == '\t' || sel[end-1] == '\r' || sel[end-1] == '\n')) end--;
        if (end <= 0) break;

        int start = end;
        while (start > 0 && sel[start-1] != ' ' && sel[start-1] != '\t' &&
               sel[start-1] != '\r' && sel[start-1] != '\n' && sel[start-1] != '>') {
            start--;
        }

        css_sel_part_t *part = &out->parts[out->count];
        if (!css_compile_simple_part(sel + start, end - start, part)) {
            out->valid = 0;
            return;
        }
        part->combinator = next_comb;
        out->count++;

        int pos = start;
        while (pos > 0 && (sel[pos-1] == ' ' || sel[pos-1] == '\t' || sel[pos-1] == '\r' || sel[pos-1] == '\n')) pos--;
        if (pos == 0) {
            out->valid = 1;
            return;
        }
        if (sel[pos-1] == '>') {
            next_comb = 2; /* child combinator */
            pos--;
            while (pos > 0 && (sel[pos-1] == ' ' || sel[pos-1] == '\t' || sel[pos-1] == '\r' || sel[pos-1] == '\n')) pos--;
            if (pos == 0) {
                out->valid = 0;
                return;
            }
        } else {
            next_comb = 1; /* descendant combinator */
        }
        end = pos;
    }
}

static inline int css_part_matches_node(const html_node_t *node, const css_sel_part_t *part) {
    if (part->tag != HTML_TAG_UNKNOWN && node->tag != part->tag) return 0;
    if (part->id[0] && (node->id[0] != part->id[0] || strcmp(node->id, part->id) != 0)) return 0;
    if (part->cls1_len > 0 && !html_has_class(node->class_name, part->cls1, part->cls1_len)) return 0;
    if (part->cls2_len > 0 && !html_has_class(node->class_name, part->cls2, part->cls2_len)) return 0;
    return 1;
}

static int css_compiled_matches_node(const html_node_t *node, const css_compiled_sel_t *sel) {
    if (!sel->valid || sel->count == 0) return 0;
    if (!css_part_matches_node(node, &sel->parts[0])) return 0;

    const html_node_t *cur = node;
    for (int idx = 1; idx < sel->count; idx++) {
        u8 comb = sel->parts[idx - 1].combinator;
        const css_sel_part_t *part = &sel->parts[idx];
        if (comb == 2) {
            /* Child combinator '>' */
            cur = cur->parent;
            if (!cur || !css_part_matches_node(cur, part)) return 0;
        } else {
            /* Descendant combinator ' ' */
            cur = cur->parent;
            while (cur && !css_part_matches_node(cur, part)) {
                cur = cur->parent;
            }
            if (!cur) return 0;
        }
    }
    return 1;
}

static void css_apply_rules_to_tree(html_node_t *root, const char *css, int len) {
    if (!root || !css || len <= 0) return;
    int i = 0;
    int rules = 0;

    while (i < len && rules < 2500) {
        /* 1. Skip whitespace */
        while (i < len && (css[i] == ' ' || css[i] == '\t' || css[i] == '\r' || css[i] == '\n')) i++;
        if (i >= len) break;

        /* 2. Skip comments */
        if (css[i] == '/' && i + 1 < len && css[i+1] == '*') {
            i += 2;
            while (i + 1 < len && !(css[i] == '*' && css[i+1] == '/')) i++;
            if (i + 1 < len) i += 2;
            continue;
        }

        /* 3. Handle @-rules (@media, @import, @charset, @keyframes, @font-face, etc.) */
        if (css[i] == '@') {
            int at_start = i;
            while (i < len && css[i] != '{' && css[i] != ';') {
                if (css[i] == '/' && i + 1 < len && css[i+1] == '*') {
                    i += 2;
                    while (i + 1 < len && !(css[i] == '*' && css[i+1] == '/')) i++;
                    if (i + 1 < len) i += 2;
                    continue;
                }
                i++;
            }
            if (i >= len) break;

            if (css[i] == ';') {
                i++;
                continue;
            }

            if (css[i] == '{') {
                int at_len = i - at_start;
                int is_media = 0;
                int is_print_or_small = 0;
                if (at_len >= 6 && strncasecmp(css + at_start, "@media", 6) == 0) {
                    is_media = 1;
                    for (int k = at_start; k < i; k++) {
                        if (k + 5 <= i && strncasecmp(css + k, "print", 5) == 0) { is_print_or_small = 1; break; }
                        if (k + 9 <= i && strncasecmp(css + k, "max-width", 9) == 0) { is_print_or_small = 1; break; }
                        if (k + 4 <= i && strncasecmp(css + k, "dark", 4) == 0) { is_print_or_small = 1; break; }
                    }
                }

                if (is_media && !is_print_or_small) {
                    i++;
                    int depth = 1;
                    int inner_start = i;
                    while (i < len && depth > 0) {
                        if (css[i] == '/' && i + 1 < len && css[i+1] == '*') {
                            i += 2;
                            while (i + 1 < len && !(css[i] == '*' && css[i+1] == '/')) i++;
                            if (i + 1 < len) i += 2;
                            continue;
                        }
                        if (css[i] == '{') depth++;
                        else if (css[i] == '}') depth--;
                        i++;
                    }
                    int inner_len = (depth == 0) ? (i - 1 - inner_start) : (i - inner_start);
                    if (inner_len > 0) {
                        css_apply_rules_to_tree(root, css + inner_start, inner_len);
                    }
                    continue;
                } else {
                    i++;
                    int depth = 1;
                    while (i < len && depth > 0) {
                        if (css[i] == '/' && i + 1 < len && css[i+1] == '*') {
                            i += 2;
                            while (i + 1 < len && !(css[i] == '*' && css[i+1] == '/')) i++;
                            if (i + 1 < len) i += 2;
                            continue;
                        }
                        if (css[i] == '{') depth++;
                        else if (css[i] == '}') depth--;
                        i++;
                    }
                    continue;
                }
            }
            continue;
        }

        /* 4. Normal rule: selector { declarations } */
        if ((++rules % 64) == 0) gui_pump_events();

        int ss = i;
        while (i < len && css[i] != '{') {
            if (css[i] == '/' && i + 1 < len && css[i+1] == '*') {
                i += 2;
                while (i + 1 < len && !(css[i] == '*' && css[i+1] == '/')) i++;
                if (i + 1 < len) i += 2;
                continue;
            }
            i++;
        }
        if (i >= len) break;
        int se = i++;

        int ds = i;
        while (i < len && css[i] != '}') {
            if (css[i] == '/' && i + 1 < len && css[i+1] == '*') {
                i += 2;
                while (i + 1 < len && !(css[i] == '*' && css[i+1] == '/')) i++;
                if (i + 1 < len) i += 2;
                continue;
            }
            i++;
        }
        int de = i;
        if (i < len && css[i] == '}') i++;

        if (se <= ss || de <= ds) continue;

        /* Pre-compile all selectors in this rule group before scanning DOM nodes */
        css_compiled_sel_t group[CSS_MAX_GROUP];
        int group_count = 0;
        int p = ss;
        while (p < se && group_count < CSS_MAX_GROUP) {
            while (p < se && (css[p] == ' ' || css[p] == '\t' || css[p] == '\r' || css[p] == '\n')) p++;
            int q = p;
            int paren = 0;
            while (q < se && (css[q] != ',' || paren > 0)) {
                if (css[q] == '(') paren++;
                else if (css[q] == ')' && paren > 0) paren--;
                q++;
            }
            if (q > p) {
                css_compile_selector(css + p, q - p, &group[group_count]);
                if (group[group_count].valid) {
                    group_count++;
                }
            }
            p = q + 1;
        }

        if (group_count == 0) continue;

        for (int n = 0; n < g_node_count; n++) {
            html_node_t *node = html5_engine_get_node(n);
            if (!node || node->tag == HTML_TAG_TEXT_NODE || html5_node_is_non_visual(node)) continue;

            for (int g = 0; g < group_count; g++) {
                if (css_compiled_matches_node(node, &group[g])) {
                    css_apply_declarations(node, css + ds, de - ds);
                    break;
                }
            }
        }
    }
}

static void css_collect_styles(char *buf, int *used, int max) {
    if (!buf || *used >= max - 1) return;
    for (int i = 0; i < g_node_count && *used < max - 1; i++) {
        html_node_t *sn = html5_engine_get_node(i);
        if (sn && sn->tag == HTML_TAG_STYLE) {
            int n = sn->text ? strlen(sn->text) : 0;
            if (n > 0) {
                if (n > max - 1 - *used) n = max - 1 - *used;
                memcpy(buf + *used, sn->text, n);
                *used += n;
            }
            for (html_node_t *c = sn->first_child; c && *used < max - 1; c = c->next_sibling) {
                int cn = c->text ? strlen(c->text) : 0;
                if (cn > max - 1 - *used) cn = max - 1 - *used;
                memcpy(buf + *used, c->text, cn);
                *used += cn;
            }
        }
    }
}



static void propagate_inherited_styles(html_node_t *node, u32 parent_color, u8 parent_align) {
    if (!node) return;

    u32 cur_color = parent_color;
    u8 cur_align = parent_align;

    if (node->style.has_explicit_color) {
        cur_color = node->style.color;
    } else if (node->tag == HTML_TAG_A) {
        cur_color = node->style.color;
    } else {
        node->style.color = cur_color;
    }

    if (node->style.has_explicit_align) {
        cur_align = node->style.text_align;
    } else if (node->tag == HTML_TAG_CENTER) {
        cur_align = 1;
        node->style.text_align = 1;
    } else {
        node->style.text_align = cur_align;
    }

    if (node->tag == HTML_TAG_SMALL) {
        node->style.font_size = 1;
    } else if (node->parent && node->parent->tag == HTML_TAG_SMALL) {
        node->style.font_size = 1;
    } else if (node->parent && node->parent->style.font_size >= 2 && node->style.font_size <= 1) {
        if (node->tag == HTML_TAG_SPAN || node->tag == HTML_TAG_STRONG ||
            node->tag == HTML_TAG_B || node->tag == HTML_TAG_EM ||
            node->tag == HTML_TAG_I || node->tag == HTML_TAG_A ||
            node->tag == HTML_TAG_TEXT_NODE) {
            node->style.font_size = node->parent->style.font_size;
        }
    }

    /* Text nodes inherit parent styling */
    if (node->tag == HTML_TAG_TEXT_NODE && node->parent) {
        node->style.color = cur_color;
        node->style.text_align = cur_align;
        if (node->parent->tag == HTML_TAG_SMALL) {
            node->style.font_size = 1;
        } else if (!node->style.font_size || node->style.font_size == 1) {
            node->style.font_size = node->parent->style.font_size;
        }
        if (node->parent->style.font_bold) node->style.font_bold = 1;
        if (node->parent->style.font_italic) node->style.font_italic = 1;
        if (node->parent->style.font_underline) node->style.font_underline = 1;
    }

    html_node_t *ch = node->first_child;
    while (ch) {
        propagate_inherited_styles(ch, cur_color, cur_align);
        ch = ch->next_sibling;
    }
}


int html5_parse_document(const char *html, int len, const char *base_url) {
    if (!html || len <= 0) {
        browser_log_error(BLOG_CAT_PAGE, "Cannot parse HTML document: %s", (!html) ? "NULL pointer" : "0 bytes length");
        return 0;
    }

    serial_write("[browser_core] html parse start\n");
    browser_log_info(BLOG_CAT_PAGE, "HTML parse begin (%d bytes)", len);

    html5_notify_status("Rendering HTML5 structure...");

    if (g_node_slab.node_size < sizeof(html_node_t) || !g_arena.allocator.alloc) {
        html5_engine_init();
    }

    html5_engine_reset();
    css_vars_reset();

    html_node_t *stack[128];
    int stack_top = 0;

    int has_body = 0;
    for (int k = 0; k < len - 5; k++) {
        if (html[k] == '<' && (html[k+1] == 'b' || html[k+1] == 'B') &&
            (html[k+2] == 'o' || html[k+2] == 'O') &&
            (html[k+3] == 'd' || html[k+3] == 'D') &&
            (html[k+4] == 'y' || html[k+4] == 'Y') &&
            (html[k+5] == ' ' || html[k+5] == '>' || html[k+5] == '\t' || html[k+5] == '\r' || html[k+5] == '\n')) {
            has_body = 1;
            break;
        }
    }
    g_has_explicit_body = has_body;

    html_node_t *root = alloc_node(has_body ? HTML_TAG_HTML : HTML_TAG_BODY);
    if (!root) {
        browser_log_error(BLOG_CAT_RENDER, "Failed to allocate DOM root node (out of memory)");
        return 0;
    }
    g_root_dom = root;
    stack[stack_top++] = root;

    serial_write("[browser_core] dom root ready\n");

    int i = 0;
    while (i < len && g_node_count < MAX_DOM_NODES) {
        /* Check comment <!-- ... --> or DOCTYPE <!DOCTYPE ...> or CDATA */
        if (html[i] == '<' && i + 1 < len && html[i+1] == '!') {
            if (i + 3 < len && strncmp(html + i, "<!--", 4) == 0) {
                i += 4;
                while (i < len - 2 && !(html[i] == '-' && html[i+1] == '-' && html[i+2] == '>')) i++;
                if (i < len - 2) i += 3; else i = len;
                continue;
            }
            while (i < len && html[i] != '>') i++;
            if (i < len && html[i] == '>') i++;
            continue;
        }

        /* Check processing instruction <? ... ?> */
        if (html[i] == '<' && i + 1 < len && html[i+1] == '?') {
            while (i < len && html[i] != '>') i++;
            if (i < len && html[i] == '>') i++;
            continue;
        }

        /* Check closing tag </tag> */
        if (html[i] == '<' && html[i+1] == '/') {
            i += 2;
            while (i < len && (html[i] == ' ' || html[i] == '\t' || html[i] == '\r' || html[i] == '\n')) i++;
            int name_start = i;
            while (i < len && html[i] != '>' && html[i] != ' ' && html[i] != '\t' && html[i] != '\r' && html[i] != '\n') i++;
            int name_len = i - name_start;
            html_tag_t tag = tag_from_name(html + name_start, name_len);
            while (i < len && html[i] != '>') i++;
            if (i < len && html[i] == '>') i++;

            /* Pop stack until matching tag */
            if (stack_top > 1 && tag != HTML_TAG_UNKNOWN) {
                if (stack[stack_top - 1]->tag == tag) {
                    stack_top--;
                } else {
                    for (int s = stack_top - 1; s > 0; s--) {
                        if (stack[s]->tag == tag) {
                            stack_top = s;
                            break;
                        }
                    }
                }
            }
            continue;
        }

        /* Check opening tag <tag ...> */
        if (html[i] == '<') {
            i++; /* skip '<' */
            while (i < len && (html[i] == ' ' || html[i] == '\t' || html[i] == '\r' || html[i] == '\n')) i++;
            int name_start = i;
            while (i < len && html[i] != ' ' && html[i] != '\t' && html[i] != '\r' && html[i] != '\n' && html[i] != '>' && html[i] != '/') i++;
            int name_len = i - name_start;
            if (name_len <= 0) continue;

            html_tag_t tag = tag_from_name(html + name_start, name_len);

            /* Parse attributes until '>' outside of quotes */
            int attr_start = i;
            int is_closed = 0;
            char in_quote = 0;
            while (i < len) {
                if (!in_quote && (html[i] == '"' || html[i] == '\'')) {
                    in_quote = html[i];
                } else if (in_quote && html[i] == in_quote) {
                    in_quote = 0;
                } else if (!in_quote) {
                    if (html[i] == '/' && i < len - 1 && html[i+1] == '>') {
                        is_closed = 1;
                        break;
                    }
                    if (html[i] == '>') {
                        break;
                    }
                }
                i++;
            }
            int attr_len = i - attr_start;
            if (i < len && html[i] == '/') i++;
            if (i < len && html[i] == '>') i++;

            /* For inline <svg>...</svg>, skip internal vector paths/defs up to </svg> so they don't consume DOM nodes or leak text */
            if (tag == HTML_TAG_SVG) {
                if (!is_closed) {
                    int svg_depth = 1;
                    while (i < len && svg_depth > 0) {
                        if (html[i] == '<') {
                            if (i + 5 <= len && strncasecmp(html + i, "</svg", 5) == 0) {
                                svg_depth--;
                                while (i < len && html[i] != '>') i++;
                                if (i < len) i++;
                                continue;
                            } else if (i + 4 <= len && strncasecmp(html + i, "<svg", 4) == 0 &&
                                       (html[i+4] == ' ' || html[i+4] == '>' || html[i+4] == '\t' || html[i+4] == '\n')) {
                                svg_depth++;
                            }
                        }
                        i++;
                    }
                }
                continue;
            }

            /* For <noscript>...</noscript>, skip children since scripting is enabled */
            if (tag == HTML_TAG_NOSCRIPT) {
                if (!is_closed) {
                    int ns_depth = 1;
                    while (i < len && ns_depth > 0) {
                        if (html[i] == '<') {
                            if (i + 10 <= len && strncasecmp(html + i, "</noscript", 10) == 0) {
                                ns_depth--;
                                while (i < len && html[i] != '>') i++;
                                if (i < len) i++;
                                continue;
                            } else if (i + 9 <= len && strncasecmp(html + i, "<noscript", 9) == 0 &&
                                       (html[i+9] == ' ' || html[i+9] == '>' || html[i+9] == '\t' || html[i+9] == '\n')) {
                                ns_depth++;
                            }
                        }
                        i++;
                    }
                }
                continue;
            }

            /* Unknown metadata is ignored; link nodes are retained so the
               stylesheet loader can resolve them after DOM construction. */
            if (tag == HTML_TAG_META) {
                continue;
            }
            if (tag == HTML_TAG_UNKNOWN) {
                /* Preserve unrecognized custom/future HTML5 elements as generic containers */
                tag = HTML_TAG_DIV;
            }

            if (tag == HTML_TAG_HTML && root && root->tag == HTML_TAG_HTML && stack_top == 1) {
                parse_attributes(root, html + attr_start, attr_len);
                continue;
            }

            html_node_t *node = alloc_node(tag);
            if (!node) break;

            apply_default_styles(node);
            parse_attributes(node, html + attr_start, attr_len);
            if (base_url && node->src[0]) {
                char abs_src[1024];
                html5_url_resolve(base_url, node->src, abs_src, sizeof(abs_src));
                node->src = html5_arena_strdup(&g_arena, abs_src);
            }
            if (base_url && node->href[0]) {
                char abs_href[1024];
                html5_url_resolve(base_url, node->href, abs_href, sizeof(abs_href));
                node->href = html5_arena_strdup(&g_arena, abs_href);
            }
            if (base_url && node->action[0]) {
                char abs_action[1024];
                html5_url_resolve(base_url, node->action, abs_action, sizeof(abs_action));
                node->action = html5_arena_strdup(&g_arena, abs_action);
            }
            if (tag == HTML_TAG_INPUT && strcasecmp(node->input_type, "hidden") == 0) {
                node->style.display_none = 1;
                node->style.is_block = 0;
            }

            /* If encountering a body/structural tag and <head> is still on the stack, close <head> implicitly */
            if (tag != HTML_TAG_HEAD && tag != HTML_TAG_TITLE && tag != HTML_TAG_META &&
                tag != HTML_TAG_LINK && tag != HTML_TAG_STYLE && tag != HTML_TAG_SCRIPT) {
                for (int s = stack_top - 1; s > 0; s--) {
                    if (stack[s]->tag == HTML_TAG_HEAD) {
                        stack_top = s; /* pops head and its unclosed children */
                        break;
                    }
                }
            }

            /* Implicitly close previous <p> or <li> or <dt>/<dd> or <td>/<th>/<tr> when a sibling of same kind opens */
            if (stack_top > 1) {
                html_tag_t top_tag = stack[stack_top - 1]->tag;
                if ((tag == HTML_TAG_LI && top_tag == HTML_TAG_LI) ||
                    (tag == HTML_TAG_P && top_tag == HTML_TAG_P) ||
                    ((tag == HTML_TAG_DT || tag == HTML_TAG_DD) && (top_tag == HTML_TAG_DT || top_tag == HTML_TAG_DD)) ||
                    ((tag == HTML_TAG_TD || tag == HTML_TAG_TH) && (top_tag == HTML_TAG_TD || top_tag == HTML_TAG_TH)) ||
                    (tag == HTML_TAG_TR && top_tag == HTML_TAG_TR) ||
                    (tag == HTML_TAG_OPTION && top_tag == HTML_TAG_OPTION)) {
                    stack_top--;
                }
            }

            /* Attach to tree */
            html_node_t *parent = (stack_top > 0) ? stack[stack_top - 1] : root;
            node->parent = parent;
            if (!parent->first_child) {
                parent->first_child = node;
            } else {
                html_node_t *sib = parent->first_child;
                while (sib->next_sibling) sib = sib->next_sibling;
                sib->next_sibling = node;
            }

            /* For <video>, create media player context */
            if (tag == HTML_TAG_VIDEO) {
                node->video_ctx = html5_media_create_video_ctx(node->src, 1, node->style.width, node->style.height);
            }

            /* For <script> and <style>, capture content directly up to closing tag to prevent leakage into body text */
            if (!is_closed && (tag == HTML_TAG_SCRIPT || tag == HTML_TAG_STYLE)) {
                const char *close_prefix = (tag == HTML_TAG_SCRIPT) ? "</script" : "</style";
                int close_len = (tag == HTML_TAG_SCRIPT) ? 8 : 7;
                int body_start = i;
                int body_end = len;
                while (i < len) {
                    if (html[i] == '<' && i + close_len <= len && strncasecmp(html + i, close_prefix, close_len) == 0) {
                        body_end = i;
                        while (i < len && html[i] != '>') i++;
                        if (i < len && html[i] == '>') i++;
                        break;
                    }
                    if (tag == HTML_TAG_STYLE && html[i] == '<' && i + 6 <= len &&
                        (strncasecmp(html + i, "</head", 6) == 0 || strncasecmp(html + i, "<body", 5) == 0)) {
                        body_end = i;
                        break;
                    }
                    i++;
                }
                if (body_end > body_start) {
                    int cap_len = body_end - body_start;
                    if (tag == HTML_TAG_STYLE || cap_len <= 16384) {
                        node->text = html5_arena_strndup(&g_arena, html + body_start, cap_len);
                        node->text_len = cap_len;
                    }
                }
                continue;
            }

            /* Push to stack if not self-closing */
            if (!is_self_closing(tag) && !is_closed && stack_top < 127) {
                stack[stack_top++] = node;
            }
            continue;
        }

        /* Text content between tags */
        int text_start = i;
        while (i < len && html[i] != '<') i++;
        int text_len = i - text_start;

        if (text_len > 0) {
            html_node_t *parent = (stack_top > 0) ? stack[stack_top - 1] : root;

            /* Check if parent is non-visual */
            if (parent && html5_node_is_non_visual(parent)) {
                if (parent->tag == HTML_TAG_TITLE) {
                    int c_len = (text_len < (int)sizeof(g_doc_title) - 1) ? text_len : (int)sizeof(g_doc_title) - 1;
                    strncpy(g_doc_title, html + text_start, c_len);
                    g_doc_title[c_len] = 0;
                    css_trim(g_doc_title);
                } else if (parent->tag == HTML_TAG_SCRIPT || parent->tag == HTML_TAG_STYLE) {
                    html5_node_set_text_len(parent, html + text_start, text_len);
                }
                continue;
            }

            /* Do not create text nodes outside <body> */
            if (!html5_node_is_inside_body(parent)) {
                continue;
            }

            /* Check if parent is <textarea> */
            if (parent && parent->tag == HTML_TAG_TEXTAREA) {
                char *tmp = html5_arena_strndup(&g_arena, html + text_start, text_len);
                if (tmp) {
                    html5_node_set_value(parent, tmp);
                    html5_node_set_text(parent, tmp);
                }
                continue;
            }

            /* Skip pure whitespace between structural/container tags to save DOM slots,
             * but collapse whitespace between inline elements (e.g. <a>, <span>) into a single space */
            int is_all_ws = 1;
            for (int k = 0; k < text_len; k++) {
                char ch = html[text_start + k];
                if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                    is_all_ws = 0;
                    break;
                }
            }
            if (is_all_ws) {
                if (parent && (parent->tag == HTML_TAG_HTML || parent->tag == HTML_TAG_HEAD ||
                               parent->tag == HTML_TAG_TABLE || parent->tag == HTML_TAG_THEAD ||
                               parent->tag == HTML_TAG_TBODY || parent->tag == HTML_TAG_TR ||
                               parent->tag == HTML_TAG_UL || parent->tag == HTML_TAG_OL ||
                               parent->tag == HTML_TAG_DL)) {
                    continue;
                }
                html_node_t *last = parent ? parent->first_child : NULL;
                while (last && last->next_sibling) last = last->next_sibling;
                if (!last || last->tag == HTML_TAG_BR || last->style.is_block) {
                    continue;
                }
                text_len = 1;
            }

            /* Create text node */
            html_node_t *tnode = alloc_node(HTML_TAG_TEXT_NODE);
            if (tnode) {
                if (is_all_ws) {
                    tnode->text = " ";
                    tnode->text_len = 1;
                } else {
                    char *clean = (char *)html5_arena_alloc(&g_arena, text_len + 16, 1);
                    int ci = 0;
                    int in_ws = 0;
                    if (clean) {
                        for (int k = 0; k < text_len; k++) {
                            char ch = html[text_start + k];
                            if (ch == '&') {
                                in_ws = 0;
                                if (k + 5 < text_len && strncmp(html + text_start + k, "&nbsp;", 6) == 0) { clean[ci++] = ' '; k += 5; }
                                else if (k + 4 < text_len && strncmp(html + text_start + k, "&amp;", 5) == 0) { clean[ci++] = '&'; k += 4; }
                                else if (k + 3 < text_len && strncmp(html + text_start + k, "&lt;", 4) == 0) { clean[ci++] = '<'; k += 3; }
                                else if (k + 3 < text_len && strncmp(html + text_start + k, "&gt;", 4) == 0) { clean[ci++] = '>'; k += 3; }
                                else if (k + 5 < text_len && strncmp(html + text_start + k, "&quot;", 6) == 0) { clean[ci++] = '"'; k += 5; }
                                else if (k + 5 < text_len && strncmp(html + text_start + k, "&apos;", 6) == 0) { clean[ci++] = '\''; k += 5; }
                                else if (k + 5 < text_len && strncmp(html + text_start + k, "&copy;", 6) == 0) { clean[ci++] = '('; clean[ci++] = 'c'; clean[ci++] = ')'; k += 5; }
                                else if (k + 4 < text_len && strncmp(html + text_start + k, "&reg;", 5) == 0) { clean[ci++] = '('; clean[ci++] = 'R'; clean[ci++] = ')'; k += 4; }
                                else if (k + 6 < text_len && strncmp(html + text_start + k, "&trade;", 7) == 0) { clean[ci++] = '('; clean[ci++] = 'T'; clean[ci++] = 'M'; clean[ci++] = ')'; k += 6; }
                                else if (k + 6 < text_len && strncmp(html + text_start + k, "&mdash;", 7) == 0) { clean[ci++] = '-'; clean[ci++] = '-'; k += 6; }
                                else if (k + 6 < text_len && strncmp(html + text_start + k, "&ndash;", 7) == 0) { clean[ci++] = '-'; k += 6; }
                                else if (k + 7 < text_len && strncmp(html + text_start + k, "&hellip;", 8) == 0) { clean[ci++] = '.'; clean[ci++] = '.'; clean[ci++] = '.'; k += 7; }
                                else if (k + 5 < text_len && (strncmp(html + text_start + k, "&bull;", 6) == 0 || strncmp(html + text_start + k, "&sdot;", 6) == 0)) { clean[ci++] = '*'; k += 5; }
                                else if (k + 7 < text_len && strncmp(html + text_start + k, "&middot;", 8) == 0) { clean[ci++] = '*'; k += 7; }
                                else if (k + 5 < text_len && strncmp(html + text_start + k, "&rarr;", 6) == 0) { clean[ci++] = '-'; clean[ci++] = '>'; k += 5; }
                                else if (k + 5 < text_len && strncmp(html + text_start + k, "&larr;", 6) == 0) { clean[ci++] = '<'; clean[ci++] = '-'; k += 5; }
                                else if (k + 6 < text_len && strncmp(html + text_start + k, "&raquo;", 7) == 0) { clean[ci++] = '>'; clean[ci++] = '>'; k += 6; }
                                else if (k + 6 < text_len && strncmp(html + text_start + k, "&laquo;", 7) == 0) { clean[ci++] = '<'; clean[ci++] = '<'; k += 6; }
                                else if (k + 6 < text_len && strncmp(html + text_start + k, "&times;", 7) == 0) { clean[ci++] = 'x'; k += 6; }
                                else if (k + 5 < text_len && strncmp(html + text_start + k, "&euro;", 6) == 0) { clean[ci++] = 'E'; clean[ci++] = 'U'; clean[ci++] = 'R'; k += 5; }
                                else if (k + 6 < text_len && strncmp(html + text_start + k, "&pound;", 7) == 0) { clean[ci++] = 'G'; clean[ci++] = 'B'; clean[ci++] = 'P'; k += 6; }
                                else if (k + 6 < text_len && (strncmp(html + text_start + k, "&rsquo;", 7) == 0 || strncmp(html + text_start + k, "&lsquo;", 7) == 0)) { clean[ci++] = '\''; k += 6; }
                                else if (k + 6 < text_len && (strncmp(html + text_start + k, "&rdquo;", 7) == 0 || strncmp(html + text_start + k, "&ldquo;", 7) == 0)) { clean[ci++] = '"'; k += 6; }
                                else if (k + 2 < text_len && html[text_start + k + 1] == '#') {
                                    /* Numeric entity: &#NNN; or &#xHHH; */
                                    int semi = k + 2;
                                    while (semi < text_len && semi < k + 10 && html[text_start + semi] != ';') semi++;
                                    if (semi < text_len && html[text_start + semi] == ';') {
                                        long cp = 0;
                                        if (html[text_start + k + 2] == 'x' || html[text_start + k + 2] == 'X') {
                                            for (int p = k + 3; p < semi; p++) {
                                                char c = html[text_start + p];
                                                int d = (c >= '0' && c <= '9') ? (c - '0') :
                                                        (c >= 'a' && c <= 'f') ? (c - 'a' + 10) :
                                                        (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : -1;
                                                if (d < 0) { cp = -1; break; }
                                                cp = (cp << 4) | d;
                                            }
                                        } else {
                                            for (int p = k + 2; p < semi; p++) {
                                                char c = html[text_start + p];
                                                if (c >= '0' && c <= '9') {
                                                    cp = cp * 10 + (c - '0');
                                                } else {
                                                    cp = -1; break;
                                                }
                                            }
                                        }
                                        if (cp >= 32 && cp <= 126) {
                                            clean[ci++] = (char)cp;
                                            k = semi;
                                        } else if (cp == 160 || cp == 8194 || cp == 8195 || cp == 8201) {
                                            clean[ci++] = ' ';
                                            k = semi;
                                        } else if (cp == 8211 || cp == 8212 || cp == 8722) {
                                            clean[ci++] = '-';
                                            k = semi;
                                        } else if (cp == 8216 || cp == 8217 || cp == 8242) {
                                            clean[ci++] = '\'';
                                            k = semi;
                                        } else if (cp == 8220 || cp == 8221 || cp == 8243 || cp == 171 || cp == 187) {
                                            clean[ci++] = '"';
                                            k = semi;
                                        } else if (cp == 8226 || cp == 183) {
                                            clean[ci++] = '*';
                                            k = semi;
                                        } else if (cp == 8230) {
                                            clean[ci++] = '.'; clean[ci++] = '.'; clean[ci++] = '.';
                                            k = semi;
                                        } else if (cp == 8594 || cp == 10140 || cp == 8658) {
                                            clean[ci++] = '-'; clean[ci++] = '>';
                                            k = semi;
                                        } else if (cp == 8592) {
                                            clean[ci++] = '<'; clean[ci++] = '-';
                                            k = semi;
                                        } else if (cp == 169) {
                                            clean[ci++] = '('; clean[ci++] = 'c'; clean[ci++] = ')';
                                            k = semi;
                                        } else if (cp > 127) {
                                            /* Skip unsupported decorative symbol rather than printing raw &#NNN; */
                                            k = semi;
                                        } else {
                                            clean[ci++] = ch;
                                        }
                                    } else {
                                        clean[ci++] = ch;
                                    }
                                }
                                else clean[ci++] = ch;
                            } else if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                                if (!in_ws && ci > 0 && parent->tag != HTML_TAG_PRE) {
                                    clean[ci++] = ' ';
                                    in_ws = 1;
                                } else if (parent->tag == HTML_TAG_PRE) {
                                    clean[ci++] = (ch == '\r') ? ' ' : ch;
                                }
                            } else {
                                clean[ci++] = ch;
                                in_ws = 0;
                            }
                        }
                        if (ci > 0 && in_ws && clean[ci - 1] == ' ') ci--;
                        clean[ci] = '\0';
                        tnode->text = clean;
                        tnode->text_len = ci;
                    } else {
                        tnode->text = html5_empty_string();
                        tnode->text_len = 0;
                    }
                }

                if (is_script_leakage(tnode->text)) {
                    /* Discard leaked script content */
                    g_node_count--;
                    continue;
                }

                tnode->style = parent->style;
                tnode->style.is_block = 0;
                tnode->parent = parent;

                if (!parent->first_child) parent->first_child = tnode;
                else {
                    html_node_t *sib = parent->first_child;
                    while (sib->next_sibling) sib = sib->next_sibling;
                    sib->next_sibling = tnode;
                }
            }
        }
    }


    serial_write("[html5_engine] DOM construction complete. Parsed nodes: ");
    serial_write_hex((u32)g_node_count);
    serial_write("\n");
    browser_log_info(BLOG_CAT_PAGE, "DOM construction complete (%d elements)", g_node_count);
    web_browser_trigger_redraw("html");

    /* Apply author CSS after the DOM exists so selectors can inspect parents,
       ids, and classes. Dynamically allocate 256 KiB buffer on heap. */
    html5_notify_status("Applying CSS stylesheets & cascade...");
    int max_css = 262144;
    g_last_css_bytes = 0;
    char *css_buf = (char *)kmalloc((u32)max_css);
    if (css_buf) {
        int css_len = 0;
        css_collect_styles(css_buf, &css_len, max_css);
        html5_resource_discover_and_fetch(g_root_dom, base_url, css_buf, &css_len, max_css);
        g_last_css_bytes = css_len;
        if (css_len > 0) {
            browser_log_info(BLOG_CAT_CSS, "Applying author stylesheets (%d bytes of CSS)", css_len);
            css_apply_rules_to_tree(g_root_dom, css_buf, css_len);
        }
        kfree(css_buf);
    } else {
        browser_log_error(BLOG_CAT_CSS, "OOM allocating buffer for CSS collection");
    }

    /* Propagate inherited styles (color, text-align, font properties) */
    propagate_inherited_styles(g_root_dom, g_root_dom->style.color, g_root_dom->style.text_align);
    web_browser_trigger_redraw("css");

    /* Run scripts */
    html5_notify_status("Executing JavaScript scripts...");
    html5_script_run_dom_scripts(g_root_dom, base_url);

    html5_notify_status("Done");

    serial_write("[browser_core] html parse done\n");

    return g_node_count;
}

static const char *find_button_text_in_subtree(html_node_t *node) {
    if (!node) return NULL;
    if (node->tag == HTML_TAG_TEXT_NODE && node->text && node->text[0] && !is_script_leakage(node->text)) {
        return node->text;
    }
    for (html_node_t *ch = node->first_child; ch; ch = ch->next_sibling) {
        const char *t = find_button_text_in_subtree(ch);
        if (t && t[0]) return t;
    }
    return NULL;
}

static const char *get_button_label(html_node_t *node, const char *default_lbl) {
    if (!node) return default_lbl ? default_lbl : "Submit";
    const char *lbl = node->value[0] ? node->value : (node->text[0] ? node->text : NULL);
    if (!lbl || !lbl[0]) {
        lbl = find_button_text_in_subtree(node);
    }
    if (!lbl || !lbl[0] || is_script_leakage(lbl)) {
        if (node->alt[0]) return node->alt;
        if (strstr(node->name, "btnG") || strstr(node->value, "Google") || strstr(node->action, "google")) {
            return "Google Suche";
        }
        if (strstr(node->name, "btnI")) {
            return "Auf gut Glueck!";
        }
        if (strstr(node->name, "search") || strstr(node->id, "search") || (node->class_name && strstr(node->class_name, "search"))) {
            return "Search";
        }
        return default_lbl ? default_lbl : "Submit";
    }
    if (strstr(node->name, "btnI") || strstr(lbl, "Gl") || strstr(lbl, "Glueck")) {
        return "Auf gut Glueck!";
    }
    if (strstr(node->name, "btnG") || strstr(lbl, "Suche")) {
        return "Google Suche";
    }
    return lbl;
}

/* --------------------------------------------------------------------------
 * Layout Engine — Box Model & Reflow
 * -------------------------------------------------------------------------- */

static __attribute__((unused)) int html_is_space_byte(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

uint32_t utf8_decode_next(const char **ptr, const char *end) {
    return text_utf8_decode_next(ptr, end);
}

int text_measure_collapsed_utf8(const char *text, const html_style_t *s, int max_width, int at_line_start, int *out_ends_with_space, int *out_has_nbsp) {
    (void)at_line_start;
    int scale = (s && s->font_size >= 2) ? 2 : 1;
    return text_engine_measure(text, &g_browser_default_font_chain, scale, max_width, out_ends_with_space, out_has_nbsp);
}

static html_node_t *find_positioned_ancestor(html_node_t *node) {
    html_node_t *p = node ? node->parent : NULL;
    while (p) {
        if (p->style.position != POS_STATIC) return p;
        p = p->parent;
    }
    return NULL;
}

static void shift_subtree_x(html_node_t *node, int shift_x);

static __attribute__((unused)) void apply_relative_offset(html_node_t *node) {
    if (!node || node->style.position != POS_RELATIVE) return;
    shift_subtree_x(node, node->style.has_left ? node->style.left : 0);
    node->bounds.y += node->style.has_top ? node->style.top : 0;
}

static __attribute__((unused)) void apply_out_of_flow_position(html_node_t *node, int viewport_x, int viewport_y, int viewport_w, int viewport_h) {
    html_node_t *cb;
    int cbx = viewport_x, cby = viewport_y, cbw = viewport_w, cbh = viewport_h;
    if (!node) return;
    if (!(node->style.position == POS_ABSOLUTE || node->style.position == POS_FIXED)) return;
    if (node->style.position == POS_ABSOLUTE) {
        cb = find_positioned_ancestor(node);
        if (cb) {
            cbx = cb->bounds.x;
            cby = cb->bounds.y;
            cbw = cb->bounds.w;
            cbh = cb->bounds.h;
        }
    }
    if (node->style.has_left) node->bounds.x = cbx + node->style.left + node->style.margin_left;
    else if (node->style.has_right) node->bounds.x = cbx + cbw - node->style.right - node->bounds.w - node->style.margin_right;
    if (node->style.has_top) node->bounds.y = cby + node->style.top + node->style.margin_top;
    else if (node->style.has_bottom) node->bounds.y = cby + cbh - node->style.bottom - node->bounds.h - node->style.margin_bottom;
}

static __attribute__((unused)) void linebox_apply_alignment_to_children(html_node_t *parent, int text_align, int content_x, int content_w) {
    html_node_t *ch;
    html_node_t *line_start;
    int line_y, min_x, max_x, shift;
    if (!parent || !(text_align == 1 || text_align == 2)) return;
    ch = parent->first_child;
    while (ch) {
        while (ch && (ch->style.display_none || ch->bounds.w == 0 || ch->tag == HTML_TAG_BR || ch->style.is_block)) ch = ch->next_sibling;
        if (!ch) break;
        line_start = ch;
        line_y = ch->bounds.y;
        min_x = ch->bounds.x;
        max_x = ch->bounds.x + ch->bounds.w;
        ch = ch->next_sibling;
        while (ch && !ch->style.is_block && ch->tag != HTML_TAG_BR && abs(ch->bounds.y - line_y) <= 12) {
            if (!ch->style.display_none && ch->bounds.w > 0) {
                if (ch->bounds.x < min_x) min_x = ch->bounds.x;
                if (ch->bounds.x + ch->bounds.w > max_x) max_x = ch->bounds.x + ch->bounds.w;
            }
            ch = ch->next_sibling;
        }
        shift = 0;
        if (text_align == 1) shift = content_x + (content_w - (max_x - min_x)) / 2 - min_x;
        else if (text_align == 2) shift = content_x + content_w - (max_x - min_x) - min_x;
        if (shift > 0) {
            html_node_t *curr = line_start;
            while (curr && curr != ch) {
                shift_subtree_x(curr, shift);
                curr = curr->next_sibling;
            }
        }
    }
}



static void shift_subtree_x(html_node_t *node, int shift_x) {
    if (!node || shift_x == 0) return;
    node->bounds.x += shift_x;
    html_node_t *ch = node->first_child;
    while (ch) {
        shift_subtree_x(ch, shift_x);
        ch = ch->next_sibling;
    }
}

static void shift_subtree_y(html_node_t *node, int shift_y) {
    if (!node || shift_y == 0) return;
    node->bounds.y += shift_y;
    html_node_t *ch = node->first_child;
    while (ch) {
        shift_subtree_y(ch, shift_y);
        ch = ch->next_sibling;
    }
}

static int layout_node(html_node_t *node, int *inout_x, int cur_y, int avail_w, int *out_row_h, int base_x);

/* =========================================================================
 * Full 2-Axis Flexbox Engine (Flex lines, wrap, grow/shrink, align-content)
 * ========================================================================= */

#define FLEX_MAX_ITEMS 64
#define FLEX_MAX_LINES 32

#ifdef FLEX_DEBUG
#define FLEX_LOG(...) do { serial_write(__VA_ARGS__); } while (0)
#else
#define FLEX_LOG(...) do {} while (0)
#endif

typedef struct {
    html_node_t *node;
    int base_main;
    int target_main;
    int min_main;
    int max_main;
    int main_margin_start;
    int main_margin_end;
    int cross_margin_start;
    int cross_margin_end;
    int cross_size;
    int min_cross;
    int max_cross;
    int flex_grow;
    int flex_shrink;
    int frozen;
    int main_pos;
    int cross_pos;
} flex_item_t;

typedef struct {
    int start_item_idx;
    int count;
    int line_main_size;      /* sum of item target_main + margins */
    int line_cross_size;     /* max cross_size + cross margins */
    int line_cross_pos;      /* cross axis position offset */
} flex_line_t;

static int layout_flex_container(html_node_t *node, int box_x, int box_y, int box_w, int avail_w) {
    (void)avail_w;
    if (!node) return box_y;
    html_style_t *s = &node->style;

    int pad_left = s->padding_left + s->border_width;
    int pad_right = s->padding_right + s->border_width;
    int pad_top = s->padding_top + s->border_width;
    int pad_bottom = s->padding_bottom + s->border_width;

    int inner_w = box_w - pad_left - pad_right;
    if (inner_w < 1) inner_w = 1;
    int start_x = box_x + pad_left;
    int start_y = box_y + pad_top;

    /* 1. Axis Determination */
    int is_column = (s->flex_direction == 1 || s->flex_direction == 3);
    int is_reverse = (s->flex_direction == 2 || s->flex_direction == 3);
    int is_wrap = (s->flex_wrap == 1 || s->flex_wrap == 2);
    int is_wrap_reverse = (s->flex_wrap == 2);

    int container_main_size = is_column ? ((s->height > 0) ? (s->height - pad_top - pad_bottom) : 0) : inner_w;
    if (container_main_size < 0) container_main_size = 0;
    int container_cross_size = is_column ? inner_w : ((s->height > 0) ? (s->height - pad_top - pad_bottom) : 0);
    if (container_cross_size < 0) container_cross_size = 0;

    int main_gap = (s->gap > 0) ? s->gap : 0;
    int cross_gap = (s->gap > 0) ? s->gap : 0;
    if (is_column) {
        if (s->row_gap >= 0) main_gap = s->row_gap;
        if (s->column_gap >= 0) cross_gap = s->column_gap;
    } else {
        if (s->column_gap >= 0) main_gap = s->column_gap;
        if (s->row_gap >= 0) cross_gap = s->row_gap;
    }

    /* 2. Collect Flex Items & Measure Intrinsic Sizes */
    flex_item_t items[FLEX_MAX_ITEMS];
    int item_count = 0;

    html_node_t *ch = node->first_child;
    while (ch && item_count < FLEX_MAX_ITEMS) {
        if (!ch->style.display_none && !html5_node_is_non_visual(ch)) {
            flex_item_t *it = &items[item_count];
            it->node = ch;
            it->flex_grow = (ch->style.flex_grow >= 0) ? ch->style.flex_grow : 0;
            it->flex_shrink = (ch->style.flex_shrink >= 0) ? ch->style.flex_shrink : 1;
            it->frozen = 0;
            it->main_pos = 0;
            it->cross_pos = 0;

            /* Margins */
            if (is_column) {
                it->main_margin_start = ch->style.margin_top;
                it->main_margin_end = ch->style.margin_bottom;
                it->cross_margin_start = ch->style.margin_left;
                it->cross_margin_end = ch->style.margin_right;
            } else {
                it->main_margin_start = ch->style.margin_left;
                it->main_margin_end = ch->style.margin_right;
                it->cross_margin_start = ch->style.margin_top;
                it->cross_margin_end = ch->style.margin_bottom;
            }

            /* Pre-layout item to get intrinsic bounds if not explicitly sized */
            int dummy_x = start_x;
            int dummy_rh = 16;
            int pre_w = inner_w;
            int grid_col_w = 0;
            if (s->is_grid && !is_column) {
                int gcols = (s->grid_cols > 0) ? s->grid_cols : 2;
                grid_col_w = (inner_w - (gcols - 1) * main_gap) / gcols;
                if (grid_col_w < 60) grid_col_w = inner_w;
                pre_w = grid_col_w;
            }
            if (!is_column && ch->style.width > 0) {
                pre_w = ch->style.width;
            } else if (!is_column && ch->style.width < 0) {
                pre_w = inner_w * (-ch->style.width) / 100;
            }
            if (pre_w < 1) pre_w = 1;

            layout_node(ch, &dummy_x, start_y, pre_w, &dummy_rh, start_x);

            /* Min/Max constraints */
            if (is_column) {
                it->min_main = (ch->style.min_height > 0) ? ch->style.min_height : 0;
                it->max_main = (ch->style.max_height > 0) ? ch->style.max_height : 0x7FFFFFFF;
                it->min_cross = (ch->style.min_width > 0) ? ch->style.min_width : 0;
                it->max_cross = (ch->style.max_width > 0) ? ch->style.max_width : 0x7FFFFFFF;
            } else {
                it->min_main = (ch->style.min_width > 0) ? ch->style.min_width : 0;
                it->max_main = (ch->style.max_width > 0) ? ch->style.max_width : 0x7FFFFFFF;
                it->min_cross = (ch->style.min_height > 0) ? ch->style.min_height : 0;
                it->max_cross = (ch->style.max_height > 0) ? ch->style.max_height : 0x7FFFFFFF;
            }

            /* Resolve base main size */
            int base = 0;
            if (ch->style.has_flex_basis && ch->style.flex_basis != -9999) {
                if (ch->style.flex_basis >= 0) {
                    base = ch->style.flex_basis;
                } else if (ch->style.flex_basis < 0 && container_main_size > 0) {
                    base = container_main_size * (-ch->style.flex_basis) / 100;
                }
            } else if (is_column) {
                if (ch->style.height > 0) base = ch->style.height;
                else base = ch->bounds.h;
            } else {
                if (ch->style.width > 0) base = ch->style.width;
                else if (ch->style.width < 0 && inner_w > 0) base = inner_w * (-ch->style.width) / 100;
                else if (grid_col_w > 0) base = grid_col_w;
                else base = ch->bounds.w;
            }

            if (base < it->min_main) base = it->min_main;
            if (it->max_main > 0 && base > it->max_main) base = it->max_main;
            it->base_main = base;
            it->target_main = base;

            /* Cross size */
            int csize = 0;
            if (is_column) {
                if (ch->style.width > 0) csize = ch->style.width;
                else if (ch->style.width < 0 && inner_w > 0) csize = inner_w * (-ch->style.width) / 100;
                else csize = ch->bounds.w;
            } else {
                if (ch->style.height > 0) csize = ch->style.height;
                else csize = ch->bounds.h;
            }
            if (csize < it->min_cross) csize = it->min_cross;
            if (it->max_cross > 0 && csize > it->max_cross) csize = it->max_cross;
            it->cross_size = csize;

            item_count++;
        }
        ch = ch->next_sibling;
    }

    if (item_count == 0) {
        int final_h = (s->height > 0) ? s->height : (pad_top + pad_bottom);
        node->bounds = (rect_t){box_x, box_y, box_w, final_h, 0};
        return box_y + final_h + s->margin_bottom;
    }

    /* 3. Break Items into Flex Lines (flex-wrap) */
    flex_line_t lines[FLEX_MAX_LINES];
    int line_count = 0;

    lines[0].start_item_idx = 0;
    lines[0].count = 0;
    lines[0].line_main_size = 0;
    lines[0].line_cross_size = 0;
    lines[0].line_cross_pos = 0;
    line_count = 1;

    for (int i = 0; i < item_count; i++) {
        flex_item_t *it = &items[i];
        int item_outer_main = it->target_main + it->main_margin_start + it->main_margin_end;
        flex_line_t *cur_line = &lines[line_count - 1];

        int added_gap = (cur_line->count > 0) ? main_gap : 0;
        int would_be_size = cur_line->line_main_size + added_gap + item_outer_main;

        if (is_wrap && cur_line->count > 0 && container_main_size > 0 && would_be_size > container_main_size && line_count < FLEX_MAX_LINES) {
            /* Create new line */
            line_count++;
            cur_line = &lines[line_count - 1];
            cur_line->start_item_idx = i;
            cur_line->count = 1;
            cur_line->line_main_size = item_outer_main;
            int outer_cross = it->cross_size + it->cross_margin_start + it->cross_margin_end;
            cur_line->line_cross_size = outer_cross;
            cur_line->line_cross_pos = 0;
        } else {
            cur_line->count++;
            cur_line->line_main_size += added_gap + item_outer_main;
            int outer_cross = it->cross_size + it->cross_margin_start + it->cross_margin_end;
            if (outer_cross > cur_line->line_cross_size) {
                cur_line->line_cross_size = outer_cross;
            }
        }
    }

    /* 4. Resolve Flex-Grow & Flex-Shrink on Each Line */
    int total_lines_cross = 0;
    for (int l = 0; l < line_count; l++) {
        flex_line_t *line = &lines[l];
        int avail_main = container_main_size;
        if (avail_main <= 0) {
            /* If container main size is auto, fit to content */
            avail_main = line->line_main_size;
        }

        /* Outer gap space */
        int total_line_gap = (line->count > 1) ? (line->count - 1) * main_gap : 0;
        int total_items_outer_main = 0;
        for (int i = 0; i < line->count; i++) {
            flex_item_t *it = &items[line->start_item_idx + i];
            total_items_outer_main += it->target_main + it->main_margin_start + it->main_margin_end;
        }

        int free_main = avail_main - (total_items_outer_main + total_line_gap);

        if (free_main > 0) {
            /* Flex-Grow loop with freeze clamping */
            while (free_main > 0) {
                int total_grow = 0;
                for (int i = 0; i < line->count; i++) {
                    flex_item_t *it = &items[line->start_item_idx + i];
                    if (!it->frozen && it->flex_grow > 0) {
                        total_grow += it->flex_grow;
                    }
                }
                if (total_grow <= 0) break;

                int newly_frozen = 0;
                int space_distributed = 0;
                for (int i = 0; i < line->count; i++) {
                    flex_item_t *it = &items[line->start_item_idx + i];
                    if (!it->frozen && it->flex_grow > 0) {
                        int delta = (int)((int64_t)free_main * it->flex_grow / total_grow);
                        int proposed = it->target_main + delta;
                        if (it->max_main > 0 && proposed >= it->max_main) {
                            space_distributed += (it->max_main - it->target_main);
                            it->target_main = it->max_main;
                            it->frozen = 1;
                            newly_frozen = 1;
                        } else {
                            it->target_main = proposed;
                            space_distributed += delta;
                        }
                    }
                }
                free_main -= space_distributed;
                if (!newly_frozen || space_distributed <= 0) break;
            }
        } else if (free_main < 0) {
            /* Flex-Shrink loop with scaled shrink factor and min clamping */
            int deficiency = -free_main;
            while (deficiency > 0) {
                int64_t total_scaled_shrink = 0;
                for (int i = 0; i < line->count; i++) {
                    flex_item_t *it = &items[line->start_item_idx + i];
                    if (!it->frozen && it->flex_shrink > 0) {
                        total_scaled_shrink += (int64_t)it->flex_shrink * it->base_main;
                    }
                }
                if (total_scaled_shrink <= 0) break;

                int newly_frozen = 0;
                int space_shrunk = 0;
                for (int i = 0; i < line->count; i++) {
                    flex_item_t *it = &items[line->start_item_idx + i];
                    if (!it->frozen && it->flex_shrink > 0) {
                        int64_t item_scaled = (int64_t)it->flex_shrink * it->base_main;
                        int delta = (int)((int64_t)deficiency * item_scaled / total_scaled_shrink);
                        int proposed = it->target_main - delta;
                        if (proposed <= it->min_main) {
                            space_shrunk += (it->target_main - it->min_main);
                            it->target_main = it->min_main;
                            it->frozen = 1;
                            newly_frozen = 1;
                        } else {
                            it->target_main = proposed;
                            space_shrunk += delta;
                        }
                    }
                }
                deficiency -= space_shrunk;
                if (!newly_frozen || space_shrunk <= 0) break;
            }
        }

        /* Recompute line sizes after grow/shrink */
        int new_line_main = 0;
        int max_line_cross = 0;
        for (int i = 0; i < line->count; i++) {
            flex_item_t *it = &items[line->start_item_idx + i];
            new_line_main += it->target_main + it->main_margin_start + it->main_margin_end;

            /* Cross-axis stretch check: if stretch and cross size auto, expand to line */
            int eff_align = (it->node->style.align_self > 0) ? it->node->style.align_self : (s->align_items + 1);
            if (eff_align == 1 && s->align_items == 0) eff_align = 4; /* stretch default */

            int it_cross = it->cross_size + it->cross_margin_start + it->cross_margin_end;
            if (it_cross > max_line_cross) max_line_cross = it_cross;
        }
        line->line_main_size = new_line_main + total_line_gap;
        line->line_cross_size = max_line_cross;
        total_lines_cross += max_line_cross;
    }

    int total_lines_gap = (line_count > 1) ? (line_count - 1) * cross_gap : 0;
    int total_cross_with_gaps = total_lines_cross + total_lines_gap;

    /* 5. Align-Content (Distribute lines along cross-axis) */
    int avail_cross = container_cross_size;
    if (avail_cross < total_cross_with_gaps) {
        avail_cross = total_cross_with_gaps;
    }
    int free_cross = avail_cross - total_cross_with_gaps;
    int current_line_cross_offset = 0;
    int line_cross_spacing = cross_gap;

    if (line_count > 1 && free_cross > 0) {
        if (s->align_content == 1) { /* flex-start */
            current_line_cross_offset = 0;
        } else if (s->align_content == 2) { /* center */
            current_line_cross_offset = free_cross / 2;
        } else if (s->align_content == 3) { /* flex-end */
            current_line_cross_offset = free_cross;
        } else if (s->align_content == 4 && line_count > 1) { /* space-between */
            line_cross_spacing = cross_gap + free_cross / (line_count - 1);
        } else if (s->align_content == 5) { /* space-around */
            int extra_per_line = free_cross / line_count;
            current_line_cross_offset = extra_per_line / 2;
            line_cross_spacing = cross_gap + extra_per_line;
        } else if (s->align_content == 0) { /* stretch */
            int extra_per_line = free_cross / line_count;
            for (int l = 0; l < line_count; l++) {
                lines[l].line_cross_size += extra_per_line;
            }
        }
    } else if (line_count == 1 && free_cross > 0) {
        if (s->align_content == 2) {
            current_line_cross_offset = free_cross / 2;
        } else if (s->align_content == 3) {
            current_line_cross_offset = free_cross;
        }
    }

    /* Assign cross positions to lines */
    if (is_wrap_reverse) {
        /* In wrap-reverse, lines flow from end to start */
        int cur_pos = avail_cross - current_line_cross_offset;
        for (int l = 0; l < line_count; l++) {
            cur_pos -= lines[l].line_cross_size;
            lines[l].line_cross_pos = cur_pos;
            cur_pos -= line_cross_spacing;
        }
    } else {
        int cur_pos = current_line_cross_offset;
        for (int l = 0; l < line_count; l++) {
            lines[l].line_cross_pos = cur_pos;
            cur_pos += lines[l].line_cross_size + line_cross_spacing;
        }
    }

    /* 6. Justify-Content, Align-Items/Align-Self & Position Application */
    int line_avail_main = container_main_size;

    for (int l = 0; l < line_count; l++) {
        flex_line_t *line = &lines[l];
        int l_avail = (line_avail_main > 0) ? line_avail_main : line->line_main_size;
        int l_gap = main_gap;
        int l_start_main = 0;

        int total_items_outer = 0;
        for (int i = 0; i < line->count; i++) {
            flex_item_t *it = &items[line->start_item_idx + i];
            total_items_outer += it->target_main + it->main_margin_start + it->main_margin_end;
        }
        int l_total_gap = (line->count > 1) ? (line->count - 1) * l_gap : 0;
        int l_free_main = l_avail - (total_items_outer + l_total_gap);

        if (l_free_main > 0) {
            if (s->justify_content == 1) { /* center */
                l_start_main = l_free_main / 2;
            } else if (s->justify_content == 2) { /* flex-end */
                l_start_main = l_free_main;
            } else if (s->justify_content == 3 && line->count > 1) { /* space-between */
                l_gap += l_free_main / (line->count - 1);
            } else if (s->justify_content == 4 && line->count > 0) { /* space-around */
                int step = l_free_main / line->count;
                l_start_main = step / 2;
                l_gap += step;
            } else if (s->justify_content == 5 && line->count > 0) { /* space-evenly */
                int step = l_free_main / (line->count + 1);
                l_start_main = step;
                l_gap += step;
            }
        }

        /* Reverse main direction if row-reverse or column-reverse */
        if (is_reverse) {
            int cur_main = l_avail - l_start_main;
            for (int i = 0; i < line->count; i++) {
                flex_item_t *it = &items[line->start_item_idx + i];
                cur_main -= (it->target_main + it->main_margin_end);
                it->main_pos = cur_main;
                cur_main -= (it->main_margin_start + l_gap);
            }
        } else {
            int cur_main = l_start_main;
            for (int i = 0; i < line->count; i++) {
                flex_item_t *it = &items[line->start_item_idx + i];
                it->main_pos = cur_main + it->main_margin_start;
                cur_main += it->main_margin_start + it->target_main + it->main_margin_end + l_gap;
            }
        }

        /* Cross Alignment for items on this line */
        for (int i = 0; i < line->count; i++) {
            flex_item_t *it = &items[line->start_item_idx + i];
            int eff_align = (it->node->style.align_self > 0) ? it->node->style.align_self : (s->align_items + 1);
            /* eff_align: 0=stretch, 1=flex-start, 2=center, 3=flex-end, 4=stretch, 5=baseline */

            /* Stretch applies if eff_align == 4 (or 0) AND item cross size is auto */
            int cross_free = line->line_cross_size - (it->cross_size + it->cross_margin_start + it->cross_margin_end);
            if ((eff_align == 4 || eff_align == 0) && cross_free > 0) {
                if (is_column) {
                    if (it->node->style.width <= 0) it->cross_size += cross_free;
                } else {
                    if (it->node->style.height <= 0) it->cross_size += cross_free;
                }
                cross_free = 0;
            }

            int item_cross_off = it->cross_margin_start;
            if (eff_align == 2 && cross_free > 0) { /* center */
                item_cross_off += cross_free / 2;
            } else if (eff_align == 3 && cross_free > 0) { /* flex-end */
                item_cross_off += cross_free;
            }
            it->cross_pos = line->line_cross_pos + item_cross_off;
        }
    }

    /* 7. Apply Geometry to DOM Nodes and Shift Subtrees */
    for (int i = 0; i < item_count; i++) {
        flex_item_t *it = &items[i];
        int final_x = is_column ? (start_x + it->cross_pos) : (start_x + it->main_pos);
        int final_y = is_column ? (start_y + it->main_pos) : (start_y + it->cross_pos);
        int final_w = is_column ? it->cross_size : it->target_main;
        int final_h = is_column ? it->target_main : it->cross_size;

        if (final_w < 0) final_w = 0;
        if (final_h < 0) final_h = 0;

        /* If sizes changed, reflow children of nested flex containers or block containers */
        int size_changed = (final_w != it->node->bounds.w || final_h != it->node->bounds.h);
        it->node->bounds.w = final_w;
        it->node->bounds.h = final_h;

        int shift_x = final_x - it->node->bounds.x;
        int shift_y = final_y - it->node->bounds.y;
        shift_subtree_x(it->node, shift_x);
        shift_subtree_y(it->node, shift_y);

        if (size_changed && it->node->first_child) {
            /* If this item is itself a flex or block container, re-layout its subtree */
            if (it->node->style.is_flex) {
                layout_flex_container(it->node, it->node->bounds.x, it->node->bounds.y, final_w, final_w);
            }
        }
    }

    /* 8. Final Container Dimensions */
    int final_h = (s->height > 0) ? s->height : (avail_cross + pad_top + pad_bottom);
    if (!is_column) {
        final_h = (s->height > 0) ? s->height : (avail_cross + pad_top + pad_bottom);
    } else {
        int max_col_end = 0;
        for (int i = 0; i < item_count; i++) {
            int item_bot = items[i].node->bounds.y + items[i].node->bounds.h + items[i].node->style.margin_bottom;
            if (item_bot > max_col_end) max_col_end = item_bot;
        }
        final_h = (s->height > 0) ? s->height : (max_col_end - box_y + pad_bottom);
    }

    if (s->min_height > 0 && final_h < s->min_height) final_h = s->min_height;
    if (s->max_height > 0 && final_h > s->max_height) final_h = s->max_height;
    node->bounds = (rect_t){box_x, box_y, box_w, final_h, 0};

    FLEX_LOG(is_column ? "[layout] box metrics corrected (flex-col)\n" : "[layout] box metrics corrected (flex-row)\n");

    return box_y + final_h + s->margin_bottom;
}

static void linebox_reset(LineBox *line) {
    if (!line) return;
    line->item_count = 0;
    line->width = 0;
    line->height = 0;
    line->baseline = 0;
}

static void linebox_push(LineBox *line, html_node_t *item) {
    if (!line || !item || line->item_count >= 64) return;
    line->items[line->item_count++] = item;
}

static void linebox_apply_alignment(LineBox *line, int text_align, int container_width) {
    if (!line || line->item_count <= 0) return;
    int off = 0;
    if (text_align == 1) off = (container_width - line->width) / 2;
    else if (text_align == 2) off = (container_width - line->width);
    if (off <= 0) return;
    for (int i = 0; i < line->item_count; ++i) {
        shift_subtree_x(line->items[i], off);
    }
}

static int measure_collapsed_text_bytes(const char *text, html_style_t *s, int max_width, int line_start) {
    return text_measure_collapsed_utf8(text, s, max_width, line_start, NULL, NULL);
}

static int layout_node(html_node_t *node, int *inout_x, int cur_y, int avail_w, int *out_row_h, int base_x) {
    if (!node) return cur_y;

    if (html5_node_is_non_visual(node)) {
        node->bounds = (rect_t){0, 0, 0, 0, 0};
        return cur_y;
    }

    if (node->tag == HTML_TAG_INPUT && strcasecmp(node->input_type, "hidden") == 0) {
        node->bounds = (rect_t){0, 0, 0, 0, 0};
        return cur_y;
    }

    html_style_t *s = &node->style;
    LineBox current_line = {0};
    (void)linebox_push;
    (void)linebox_apply_alignment;

    /* Block element breaks into a new row */
    if (s->is_block) {
        if (*inout_x > base_x) {
            cur_y += *out_row_h;
            *inout_x = base_x;
            *out_row_h = 16;
        }
        cur_y += s->margin_top;
    }

    int box_w = 0;
    if (s->width < 0) {
        box_w = (avail_w * (-s->width)) / 100;
    } else if (s->width > 0) {
        if (s->box_sizing == 1) { /* border-box */
            box_w = s->width;
        } else { /* content-box */
            box_w = s->width + s->padding_left + s->padding_right + 2 * s->border_width;
        }
    } else {
        box_w = avail_w - (*inout_x - base_x) - s->margin_right;
    }

    /* Apply min-width and max-width */
    if (s->max_width > 0) {
        int mw = s->max_width;
        if (s->box_sizing == 0) mw += s->padding_left + s->padding_right + 2 * s->border_width;
        if (box_w > mw) box_w = mw;
    } else if (s->max_width < 0) {
        int mw = (avail_w * (-s->max_width)) / 100;
        if (box_w > mw) box_w = mw;
    }
    if (s->min_width > 0) {
        int mw = s->min_width;
        if (s->box_sizing == 0) mw += s->padding_left + s->padding_right + 2 * s->border_width;
        if (box_w < mw) box_w = mw;
    }

    if (box_w > avail_w) box_w = avail_w;
    if (box_w < 10) box_w = 10;
    int box_h = s->height;
    if (box_h > 0 && s->box_sizing == 0) {
        box_h += s->padding_top + s->padding_bottom + 2 * s->border_width;
    }

    int box_x = *inout_x + s->margin_left;
    int box_y = cur_y;

    /* Auto margins & Block Centering */
    if (s->margin_left_auto && s->margin_right_auto) {
        if (box_w < avail_w) {
            box_x = base_x + (avail_w - box_w) / 2;
        }
    } else if (s->margin_left_auto) {
        if (box_w < avail_w) {
            box_x = base_x + (avail_w - box_w) - s->margin_right;
        }
    } else if (s->is_block && (node->parent && (node->parent->tag == HTML_TAG_CENTER || node->parent->style.text_align == 1))) {
        if (box_w < avail_w) {
            box_x = base_x + (avail_w - box_w) / 2;
        }
    }
    if (box_x < base_x) box_x = base_x;

    /* Handle line breaks */
    if (node->tag == HTML_TAG_BR) {
        linebox_reset(&current_line);
        cur_y += (*out_row_h > 0) ? *out_row_h : 16;
        *inout_x = base_x;
        *out_row_h = 16;
        node->bounds = (rect_t){*inout_x, cur_y, 0, 0, 0};
        return cur_y;
    }

    /* Layout specific element types */
    if (node->tag == HTML_TAG_TEXT_NODE) {
        if (!html5_node_is_renderable_text(node) || is_script_leakage(node->text) || !node->text[0]) {
            node->bounds = (rect_t){0, 0, 0, 0, 0};
            node->style.display_none = 1;
            return cur_y;
        }
        int line_h = (s->font_size >= 2) ? 24 : 16;
        int rem_w = (base_x + avail_w) - *inout_x;
        int full_w = measure_collapsed_text_bytes(node->text, s, 0, (*inout_x == base_x));
        if (full_w <= 0) {
            node->bounds = (rect_t){box_x, box_y, 0, 0, 0};
            return cur_y;
        }

        /* Check if text fits on the current line */
        if (full_w <= rem_w) {
            box_w = full_w;
            box_h = line_h;
            if (line_h > *out_row_h) *out_row_h = line_h;
            node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
            *inout_x += box_w;
            return cur_y;
        }

        /* If we are not at start of line, wrap to new line first if that gives enough room */
        if (*inout_x > base_x) {
            cur_y += *out_row_h;
            *inout_x = base_x;
            *out_row_h = line_h;
            box_x = *inout_x;
            box_y = cur_y;
            rem_w = avail_w;
        }

        /* If it still exceeds available width, compute multi-line text height by word-wrapping */
        if (full_w > avail_w && avail_w > 40) {
            int char_w = (s->font_size >= 2) ? 12 : 8;
            int num_lines = 1;
            int cur_lw = 0;
            const char *tp = node->text;
            while (*tp) {
                while (*tp == ' ' || *tp == '\t') { cur_lw += char_w; tp++; }
                if (!*tp) break;
                int wl = 0;
                while (tp[wl] && tp[wl] != ' ' && tp[wl] != '\t' && tp[wl] != '\n') wl++;
                int ww = wl * char_w;
                if (cur_lw + ww > avail_w && cur_lw > 0) {
                    num_lines++;
                    cur_lw = ww;
                } else {
                    cur_lw += ww;
                }
                tp += wl;
                if (*tp == '\n') {
                    num_lines++;
                    cur_lw = 0;
                    tp++;
                }
            }
            box_w = avail_w;
            box_h = num_lines * line_h;
            node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
            cur_y += box_h;
            *inout_x = base_x;
            *out_row_h = line_h;
            return cur_y;
        }

        box_w = full_w;
        box_h = line_h;
        if (line_h > *out_row_h) *out_row_h = line_h;
        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        *inout_x += box_w;
        return cur_y;
    }
    else if (node->tag == HTML_TAG_IMG) {
        int orig_w = (node->img_w > 0) ? node->img_w : 0;
        int orig_h = (node->img_h > 0) ? node->img_h : 0;
        int img_w = 0, img_h = 0;

        if (s->width > 0 && s->height > 0) {
            img_w = s->width;
            img_h = s->height;
        } else if (s->width < 0 && s->height > 0) {
            img_w = (avail_w * (-s->width)) / 100;
            img_h = s->height;
        } else if (s->width < 0) {
            img_w = (avail_w * (-s->width)) / 100;
            img_h = (orig_w > 0 && orig_h > 0) ? ((img_w * orig_h) / orig_w) : ((img_w * 92) / 272);
        } else if (s->width > 0) {
            img_w = s->width;
            img_h = (orig_w > 0 && orig_h > 0) ? ((s->width * orig_h) / orig_w) : ((s->width * 92) / 272);
        } else if (s->height > 0) {
            img_h = s->height;
            img_w = (orig_w > 0 && orig_h > 0) ? ((s->height * orig_w) / orig_h) : ((s->height * 272) / 92);
        } else if (orig_w > 0 && orig_h > 0) {
            img_w = orig_w;
            img_h = orig_h;
        } else {
            img_w = 180;
            img_h = 48;
        }

        /* Max-width constraint with proportional height */
        if (s->max_width > 0 && img_w > s->max_width) {
            img_h = (img_w > 0) ? ((img_h * s->max_width) / img_w) : s->max_width;
            img_w = s->max_width;
        } else if (s->max_width < 0) {
            int max_px = (avail_w * (-s->max_width)) / 100;
            if (img_w > max_px) {
                img_h = (img_w > 0) ? ((img_h * max_px) / img_w) : max_px;
                img_w = max_px;
            }
        }

        /* Container boundary clamping: image must never spill beyond available container width */
        int container_limit = avail_w - s->margin_left - s->margin_right;
        if (container_limit > 0 && img_w > container_limit) {
            img_h = (img_w > 0) ? ((img_h * container_limit) / img_w) : container_limit;
            img_w = container_limit;
        }

        if (img_w < 16) img_w = 16;
        if (img_h < 16) img_h = 16;

        if (*inout_x + img_w + s->margin_left + s->margin_right > base_x + avail_w && *inout_x > base_x) {
            cur_y += *out_row_h;
            *inout_x = base_x;
            *out_row_h = 16;
        }
        box_x = *inout_x + s->margin_left;
        box_y = cur_y + s->margin_top;

        node->bounds = (rect_t){box_x, box_y, img_w, img_h, 0};
        if (img_h + s->margin_top + s->margin_bottom > *out_row_h) {
            *out_row_h = img_h + s->margin_top + s->margin_bottom;
        }
        *inout_x = box_x + img_w + s->margin_right + 8;
        return cur_y;
    }
    else if (node->tag == HTML_TAG_VIDEO) {
        box_w = (s->width > 0) ? s->width : 540;
        box_h = (s->height > 0) ? s->height : 190;
        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        cur_y += box_h + s->margin_bottom;
        *inout_x = base_x;
        *out_row_h = 16;
        return cur_y;
    }
    else if (node->tag == HTML_TAG_PROGRESS || node->tag == HTML_TAG_METER) {
        box_w = (s->width > 0) ? s->width : 160;
        box_h = (s->height > 0) ? s->height : 14;
        node->bounds = (rect_t){box_x, box_y + 2, box_w, box_h, 0};
        *inout_x = box_x + box_w + 8;
        return cur_y;
    }
    else if (node->tag == HTML_TAG_INPUT) {
        int is_btn = (strcasecmp(node->input_type, "submit") == 0 || strcasecmp(node->input_type, "button") == 0);
        if (is_btn) {
            const char *blabel = get_button_label(node, "Submit");
            box_w = (s->width > 0) ? s->width : (strlen(blabel) * 8 + 24);
            if (box_w < 64) box_w = 64;
            box_h = (s->height > 0) ? s->height : 28;
        } else {
            if (s->width > 0) {
                box_w = s->width;
            } else if (node->size > 0) {
                box_w = node->size * 8 + 16;
                if (box_w > 360) box_w = 360;
            } else {
                box_w = 260;
            }
            if (box_w > avail_w) box_w = avail_w;
            box_h = (s->height > 0) ? s->height : 28;
        }

        int need_w = box_w + s->margin_left + s->margin_right;
        if (*inout_x + need_w > base_x + avail_w && *inout_x > base_x) {
            cur_y += *out_row_h;
            *inout_x = base_x;
            *out_row_h = 16;
        }
        box_x = *inout_x + s->margin_left;
        box_y = cur_y + s->margin_top;

        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        if (box_h + s->margin_top + s->margin_bottom > *out_row_h) {
            *out_row_h = box_h + s->margin_top + s->margin_bottom;
        }
        *inout_x = box_x + box_w + s->margin_right + 8;
        return cur_y;
    }
    else if (node->tag == HTML_TAG_BUTTON || node->tag == HTML_TAG_SELECT) {
        const char *blabel = get_button_label(node, (node->tag == HTML_TAG_SELECT) ? "Select..." : "Button");
        box_w = (s->width > 0) ? s->width : (strlen(blabel) * 8 + 24);
        if (box_w < 64) box_w = 64;
        box_h = (s->height > 0) ? s->height : 28;

        int need_w = box_w + s->margin_left + s->margin_right;
        if (*inout_x + need_w > base_x + avail_w && *inout_x > base_x) {
            cur_y += *out_row_h;
            *inout_x = base_x;
            *out_row_h = 16;
        }
        box_x = *inout_x + s->margin_left;
        box_y = cur_y + s->margin_top;

        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        if (box_h + s->margin_top + s->margin_bottom > *out_row_h) {
            *out_row_h = box_h + s->margin_top + s->margin_bottom;
        }
        *inout_x = box_x + box_w + s->margin_right + 8;
        return cur_y;
    }
    else if (node->tag == HTML_TAG_TEXTAREA) {
        int cols = (node->cols > 0) ? node->cols : 20;
        int rows = (node->rows > 0) ? node->rows : 3;
        if (s->width > 0) {
            box_w = s->width;
        } else if (s->width < 0) {
            box_w = (avail_w * (-s->width)) / 100;
        } else {
            box_w = cols * 8 + 16;
        }
        if (rows <= 1) {
            /* Single-row search textareas: cap width so buttons fit on line and layout looks balanced */
            if (box_w > 500) box_w = 500;
            if (avail_w > 160 && box_w > avail_w - 90) box_w = avail_w - 90;
            if (box_w < 120) box_w = 120;
        }
        if (box_w > avail_w) box_w = avail_w;
        box_h = (s->height > 0) ? s->height : (rows * 16 + 14);

        int need_w = box_w + s->margin_left + s->margin_right;
        if (*inout_x + need_w > base_x + avail_w && *inout_x > base_x) {
            cur_y += *out_row_h;
            *inout_x = base_x;
            *out_row_h = 16;
        }
        box_x = *inout_x + s->margin_left;
        box_y = cur_y + s->margin_top;

        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        if (box_h + s->margin_top + s->margin_bottom > *out_row_h) {
            *out_row_h = box_h + s->margin_top + s->margin_bottom;
        }
        *inout_x = box_x + box_w + s->margin_right + 8;
        return cur_y;
    }
    else if (s->is_inline_block) {
        box_w = (s->width > 0) ? s->width : 120;
        box_h = (s->height > 0) ? s->height : 28;
        if (box_w > avail_w - 20) box_w = avail_w - 20;

        int need_w = box_w + s->margin_left + s->margin_right;
        if (*inout_x + need_w > base_x + avail_w && *inout_x > base_x) {
            cur_y += *out_row_h;
            *inout_x = base_x;
            *out_row_h = 16;
        }
        box_x = *inout_x + s->margin_left;
        box_y = cur_y + s->margin_top;

        int cb_x = box_x + s->padding_left;
        int cx = cb_x;
        int cy = box_y + s->padding_top;
        int crh = 16;
        html_node_t *ch = node->first_child;
        while (ch) {
            cy = layout_node(ch, &cx, cy, box_w, &crh, cb_x);
            ch = ch->next_sibling;
        }
        if (cy - box_y > box_h && s->height <= 0) {
            box_h = cy - box_y;
        }

        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        if (box_h + s->margin_top + s->margin_bottom > *out_row_h) {
            *out_row_h = box_h + s->margin_top + s->margin_bottom;
        }
        *inout_x = box_x + box_w + s->margin_right + 8;
        return cur_y;
    }
    else if (node->tag == HTML_TAG_HR) {
        if (*inout_x > base_x) {
            cur_y += *out_row_h;
            *inout_x = base_x;
            *out_row_h = 16;
        }
        node->bounds = (rect_t){box_x, box_y, avail_w - 32, 1, 0};
        cur_y += 1 + s->margin_bottom + 6;
        *inout_x = base_x;
        *out_row_h = 16;
        return cur_y;
    }
    else if (node->tag == HTML_TAG_TR) {
        /* Multi-column horizontal table row layout */
        html_node_t *cells[16];
        int cell_w[16];
        int ncells = 0;
        for (html_node_t *c = node->first_child; c && ncells < 16; c = c->next_sibling) {
            if (!c->style.display_none && (c->tag == HTML_TAG_TD || c->tag == HTML_TAG_TH)) {
                cells[ncells++] = c;
            }
        }
        if (ncells > 0) {
            int used_w = 0;
            int auto_count = 0;
            for (int c = 0; c < ncells; c++) {
                html_node_t *cell = cells[c];
                if (cell->style.width > 0) {
                    cell_w[c] = cell->style.width;
                    used_w += cell_w[c];
                } else if (cell->style.width < 0) {
                    cell_w[c] = (box_w * (-cell->style.width)) / 100;
                    used_w += cell_w[c];
                } else if (ncells > 1 && c < ncells - 1 &&
                           cell->first_child && !cell->first_child->next_sibling &&
                           cell->first_child->tag == HTML_TAG_TEXT_NODE &&
                           cell->first_child->text_len <= 6) {
                    /* Compact index/vote cell (e.g. "1." on Hacker News) */
                    cell_w[c] = cell->first_child->text_len * 8 + 20;
                    used_w += cell_w[c];
                } else {
                    cell_w[c] = 0;
                    auto_count++;
                }
            }
            int rem_w = box_w - used_w;
            if (rem_w < auto_count * 40) rem_w = auto_count * 40;
            int each_auto = (auto_count > 0) ? (rem_w / auto_count) : 0;
            int cx = box_x;
            int max_ch = 22;
            for (int c = 0; c < ncells; c++) {
                html_node_t *cell = cells[c];
                int cw = (cell_w[c] > 0) ? cell_w[c] : each_auto;
                if (cw < 24) cw = 24;
                int c_pad_l = cell->style.padding_left + cell->style.border_width;
                int c_pad_r = cell->style.padding_right + cell->style.border_width;
                int c_pad_t = cell->style.padding_top + cell->style.border_width;
                int c_pad_b = cell->style.padding_bottom + cell->style.border_width;
                int c_inner_w = cw - c_pad_l - c_pad_r;
                if (c_inner_w < 10) c_inner_w = 10;

                int c_base_x = cx + c_pad_l;
                int c_cur_x = c_base_x;
                int c_cur_y = box_y + c_pad_t;
                int c_row_h = 16;
                for (html_node_t *sub = cell->first_child; sub; sub = sub->next_sibling) {
                    c_cur_y = layout_node(sub, &c_cur_x, c_cur_y, c_inner_w, &c_row_h, c_base_x);
                }
                if (c_cur_x > c_base_x) c_cur_y += c_row_h;
                int ch_total = (c_cur_y - box_y) + c_pad_b;
                if (cell->style.height > 0 && ch_total < cell->style.height) ch_total = cell->style.height;
                if (ch_total < 22) ch_total = 22;
                cell->bounds = (rect_t){cx, box_y, cw, ch_total, 0};
                if (ch_total > max_ch) max_ch = ch_total;
                cx += cw;
            }
            for (int c = 0; c < ncells; c++) {
                cells[c]->bounds.h = max_ch;
            }
            node->bounds = (rect_t){box_x, box_y, cx - box_x, max_ch, 0};
            cur_y = box_y + max_ch + s->margin_bottom;
            *inout_x = base_x;
            *out_row_h = 16;
            return cur_y;
        }
    }
    else if (node->tag == HTML_TAG_TH || node->tag == HTML_TAG_TD) {
        if (s->width < 0) {
            box_w = (avail_w * (-s->width)) / 100;
        } else if (s->width > 0) {
            if (s->box_sizing == 1) box_w = s->width;
            else box_w = s->width + s->padding_left + s->padding_right + 2 * s->border_width;
        } else {
            box_w = (avail_w > *inout_x - base_x + 160) ? (avail_w - (*inout_x - base_x)) : 160;
            if (box_w > 480) box_w = 480;
        }
        int cell_pad_l = s->padding_left + s->border_width;
        int cell_pad_r = s->padding_right + s->border_width;
        int cell_pad_t = s->padding_top + s->border_width;
        int cell_pad_b = s->padding_bottom + s->border_width;
        int cell_inner_w = box_w - cell_pad_l - cell_pad_r;
        if (cell_inner_w < 1) cell_inner_w = 1;

        int cell_base_x = box_x + cell_pad_l;
        int cell_x = cell_base_x;
        int cell_y = box_y + cell_pad_t;
        int cell_row_h = 16;
        html_node_t *ch = node->first_child;
        while (ch) {
            cell_y = layout_node(ch, &cell_x, cell_y, cell_inner_w, &cell_row_h, cell_base_x);
            ch = ch->next_sibling;
        }
        if (cell_x > cell_base_x) cell_y += cell_row_h;
        box_h = cell_y - box_y + cell_pad_b;
        if (s->height > 0 && box_h < s->height) box_h = s->height;
        if (box_h < 24) box_h = 24;
        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        if (box_h > *out_row_h) *out_row_h = box_h;
        *inout_x += box_w;
        return cur_y;
    }
    else if (node->tag == HTML_TAG_LI || node->tag == HTML_TAG_SUMMARY) {
        if (*inout_x > base_x) {
            cur_y += *out_row_h;
            *inout_x = base_x;
            *out_row_h = 16;
        }
        *inout_x = base_x + 16;
        box_x = *inout_x;
        box_y = cur_y;
    }

    /* Check flexbox container */
    if (s->is_flex) {
        cur_y = layout_flex_container(node, box_x, box_y, box_w, avail_w);
        *inout_x = base_x;
        *out_row_h = 16;
        return cur_y;
    }

    /* Recurse children for container tags */
    int pad_left = s->padding_left + s->border_width;
    int pad_right = s->padding_right + s->border_width;
    int pad_top = s->padding_top + s->border_width;
    int pad_bottom = s->padding_bottom + s->border_width;
    int inner_w = box_w - pad_left - pad_right;
    if (inner_w < 1) inner_w = 1;

    int child_base_x = box_x + pad_left;
    int child_x = child_base_x;
    int child_y = box_y + pad_top;
    int child_row_h = 16;

    /* Subtitle <small> inside <a> (e.g. Wikipedia language link-box) breaks to new line below <strong> */
    if (node->tag == HTML_TAG_SMALL && node->parent && node->parent->tag == HTML_TAG_A && *inout_x > base_x) {
        cur_y += *out_row_h;
        *inout_x = base_x;
        *out_row_h = 16;
        box_x = *inout_x + s->margin_left;
        box_y = cur_y;
        child_base_x = box_x + pad_left;
        child_x = child_base_x;
        child_y = box_y + pad_top;
    }

    int max_child_right = child_base_x;
    html_node_t *ch = node->first_child;
    while (ch) {
        child_y = layout_node(ch, &child_x, child_y, inner_w, &child_row_h, child_base_x);
        if (child_x > max_child_right) max_child_right = child_x;
        if (!ch->style.display_none && ch->bounds.x + ch->bounds.w > max_child_right) {
            max_child_right = ch->bounds.x + ch->bounds.w;
        }
        ch = ch->next_sibling;
    }

    /* Row centering for text-align: center/right or <center> (only on block or multi-line containers) */
    if ((s->is_block || child_y > box_y + pad_top || node->tag == HTML_TAG_CENTER) &&
        (s->text_align == 1 || s->text_align == 2 || node->tag == HTML_TAG_CENTER)) {
        html_node_t *line_start = node->first_child;
        while (line_start) {
            if (line_start->style.display_none || line_start->bounds.w == 0 ||
                line_start->tag == HTML_TAG_BR || line_start->style.is_block) {
                line_start = line_start->next_sibling;
                continue;
            }

            int cur_line_y = line_start->bounds.y;
            int min_x = line_start->bounds.x;
            int max_x = line_start->bounds.x + line_start->bounds.w;
            html_node_t *line_end = line_start;
            html_node_t *curr = line_start->next_sibling;

            while (curr) {
                if (curr->style.display_none || curr->bounds.w == 0) {
                    curr = curr->next_sibling;
                    continue;
                }
                if (curr->tag == HTML_TAG_BR || curr->style.is_block || h_abs(curr->bounds.y - cur_line_y) > 12) {
                    break;
                }
                if (curr->bounds.x < min_x) min_x = curr->bounds.x;
                if (curr->bounds.x + curr->bounds.w > max_x) max_x = curr->bounds.x + curr->bounds.w;
                line_end = curr;
                curr = curr->next_sibling;
            }

            int line_w = max_x - min_x;
            if (line_w > 0 && line_w < inner_w) {
                int desired_min_x = (s->text_align == 2)
                    ? (child_base_x + inner_w - line_w)
                    : (child_base_x + (inner_w - line_w) / 2);
                int shift = desired_min_x - min_x;
                if (shift > 0) {
                    html_node_t *p = line_start;
                    while (p) {
                        if (!p->style.display_none && p->bounds.w > 0) {
                            shift_subtree_x(p, shift);
                            if (p->bounds.x + p->bounds.w > max_child_right) {
                                max_child_right = p->bounds.x + p->bounds.w;
                            }
                        }
                        if (p == line_end) break;
                        p = p->next_sibling;
                    }
                }
            }

            line_start = line_end->next_sibling;
        }
    }

    if (s->is_block) {
        if (child_x > child_base_x) {
            child_y += child_row_h;
            child_x = child_base_x;
        }
        child_y += pad_bottom;
        box_h = (s->height > 0) ? s->height : (child_y - box_y);
        if (s->height > 0 && s->box_sizing == 0) {
            box_h += pad_top + pad_bottom;
        }
        if (s->min_height > 0 && box_h < s->min_height) box_h = s->min_height;
        if (s->max_height > 0 && box_h > s->max_height) box_h = s->max_height;
        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        cur_y = box_y + box_h + s->margin_bottom;
        *inout_x = base_x;
        *out_row_h = 16;
    } else if (child_y > box_y + pad_top) {
        /* Multi-line or block-containing inline container (e.g. <a class="link-box">) */
        if (child_x > child_base_x) {
            child_y += child_row_h;
            child_x = child_base_x;
        }
        child_y += pad_bottom;
        box_h = child_y - box_y;
        box_w = (max_child_right > box_x) ? (max_child_right - box_x + pad_right) : avail_w;
        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        cur_y = child_y + s->margin_bottom;
        *inout_x = base_x;
        *out_row_h = 16;
    } else {
        /* Single-line inline container (e.g. <span>, <a>) */
        box_h = child_row_h;
        box_w = (child_x > box_x) ? (child_x - box_x) : 0;
        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        *inout_x = child_x;
        if (child_row_h > *out_row_h) *out_row_h = child_row_h;
    }

    return cur_y;
}

static int compute_max_descendant_bottom(html_node_t *node) {
    if (!node || node->style.display_none) return 0;
    int max_b = (node->bounds.w > 0 && node->bounds.h > 0) ? (node->bounds.y + node->bounds.h) : 0;
    for (html_node_t *ch = node->first_child; ch; ch = ch->next_sibling) {
        int cb = compute_max_descendant_bottom(ch);
        if (cb > max_b) max_b = cb;
    }
    return max_b;
}

void html5_layout_document(int content_w, int content_h) {
    (void)content_h;
    if (!g_root_dom) {
        browser_log_error(BLOG_CAT_RENDER, "Layout skipped: DOM root is NULL");
        return;
    }

    serial_write("[browser_core] layout start\n");

    int cur_x = 16;
    int cur_y = 16;
    int row_h = 16;

    int root_end_y = layout_node(g_root_dom, &cur_x, cur_y, content_w, &row_h, 16);
    int max_child_bottom = compute_max_descendant_bottom(g_root_dom);
    int total_h = (max_child_bottom > root_end_y) ? max_child_bottom : root_end_y;
    g_doc_height = total_h + 40;
    serial_write("[html5_engine] Layout pass complete. Total document height: ");
    serial_write_hex((u32)g_doc_height);
    serial_write("\n");
    browser_log_info(BLOG_CAT_RENDER, "Layout complete: viewport=%dx%d, document height=%dpx", content_w, content_h, g_doc_height);
}

/* Find effective background color behind a node */
static u32 find_effective_bg(html_node_t *node, u32 default_bg) {
    html_node_t *cur = node;
    while (cur) {
        if (cur->style.has_bg && cur->bounds.w > 0 && cur->bounds.h > 0) {
            return cur->style.bg_color;
        }
        cur = cur->parent;
    }
    return default_bg;
}

static void render_node(html_node_t *node, int content_x, int content_y, int content_w, int content_h, int scroll_y) {
    if (!node) return;
    if (node->style.display_none || html5_node_is_non_visual(node)) return;

    int vx = content_x + node->bounds.x;
    int vy = content_y + node->bounds.y - scroll_y;
    int vw = node->bounds.w;
    int vh = node->bounds.h;

    /* Viewport Culling */
    if (vy + vh >= content_y && vy <= content_y + content_h) {
        html_style_t *s = &node->style;

        /* 1. Background Fill */
        if (s->has_bg && vw > 0 && vh > 0) {
            if (s->border_radius > 0) {
                fb_round_rect(vx, vy, vw, vh, s->border_radius, s->bg_color);
            } else {
                fb_rect(vx, vy, vw, vh, s->bg_color);
            }
        }

        /* 2. Borders */
        if (s->border_width > 0) {
            if (s->border_radius > 0) {
                fb_round_rect_border(vx, vy, vw, vh, s->border_radius, s->border_color);
            } else if (node->tag == HTML_TAG_HR) {
                fb_rect(vx, vy, vw, 1, s->border_color);
            } else if (node->tag == HTML_TAG_BLOCKQUOTE) {
                fb_rect(vx, vy, s->border_width, vh, s->border_color);
            } else {
                fb_border(vx, vy, vw, vh, s->border_color);
            }
        }

        /* 3. Text Nodes & Headings */
        if (node->tag == HTML_TAG_TEXT_NODE && node->text[0]) {
            if (!html5_node_is_renderable_text(node) || is_script_leakage(node->text)) {
                /* Skip non-renderable or leaked script text */
            } else {
                int is_link = 0;
                for (html_node_t *anc = node->parent; anc; anc = anc->parent) {
                    if (anc->tag == HTML_TAG_A) { is_link = 1; break; }
                }
                u32 col = (is_link && !s->has_explicit_color) ? 0x1A0DAB : s->color;
                /* Automatic contrast correction for both dark-on-dark and light-on-light text */
                u32 eff_bg = find_effective_bg(node->parent, 0xFFFFFF);
                u8 bg_r = (eff_bg >> 16) & 0xFF;
                u8 bg_g = (eff_bg >> 8) & 0xFF;
                u8 bg_b = eff_bg & 0xFF;
                u32 bg_lum = (u32)(bg_r * 299 + bg_g * 587 + bg_b * 114) / 1000;
                u8 fg_r = (col >> 16) & 0xFF;
                u8 fg_g = (col >> 8) & 0xFF;
                u8 fg_b = col & 0xFF;
                u32 fg_lum = (u32)(fg_r * 299 + fg_g * 587 + fg_b * 114) / 1000;
                if (bg_lum < 80 && fg_lum < 100) {
                    col = is_link ? 0x64D2FF : 0xF0F0F5;
                } else if (bg_lum > 170 && fg_lum > 170) {
                    col = is_link ? 0x1A0DAB : 0x202124;
                }
                int fscale = (s->font_size >= 2) ? 2 : 1;
                int line_h = (fscale == 2) ? 24 : 16;
                if (vh > line_h && vw > 40) {
                    /* Multi-line wrapped text */
                    int char_w = (fscale == 2) ? 12 : 8;
                    int cur_lw = 0;
                    int cur_ly = vy;
                    char line_buf[256];
                    int lb_pos = 0;
                    const char *tp = node->text;
                    while (*tp) {
                        while (*tp == ' ' || *tp == '\t') {
                            if (lb_pos > 0 && lb_pos < (int)sizeof(line_buf) - 1) {
                                line_buf[lb_pos++] = ' ';
                                cur_lw += char_w;
                            }
                            tp++;
                        }
                        if (!*tp) break;
                        int wl = 0;
                        while (tp[wl] && tp[wl] != ' ' && tp[wl] != '\t' && tp[wl] != '\n') wl++;
                        int ww = wl * char_w;
                        if (cur_lw + ww > vw && cur_lw > 0) {
                            /* Flush current line */
                            line_buf[lb_pos] = '\0';
                            int lx = vx;
                            if (s->text_align == 1 && cur_lw < vw) lx = vx + (vw - cur_lw) / 2;
                            else if (s->text_align == 2 && cur_lw < vw) lx = vx + (vw - cur_lw);
                            text_engine_render(lx, cur_ly + 2, line_buf, col, fscale, &g_browser_default_font_chain, NULL);
                            cur_ly += line_h;
                            lb_pos = 0;
                            cur_lw = 0;
                        }
                        for (int k = 0; k < wl && lb_pos < (int)sizeof(line_buf) - 1; k++) {
                            line_buf[lb_pos++] = tp[k];
                        }
                        cur_lw += ww;
                        tp += wl;
                        if (*tp == '\n') {
                            line_buf[lb_pos] = '\0';
                            int lx = vx;
                            if (s->text_align == 1 && cur_lw < vw) lx = vx + (vw - cur_lw) / 2;
                            else if (s->text_align == 2 && cur_lw < vw) lx = vx + (vw - cur_lw);
                            text_engine_render(lx, cur_ly + 2, line_buf, col, fscale, &g_browser_default_font_chain, NULL);
                            cur_ly += line_h;
                            lb_pos = 0;
                            cur_lw = 0;
                            tp++;
                        }
                    }
                    if (lb_pos > 0) {
                        line_buf[lb_pos] = '\0';
                        int lx = vx;
                        if (s->text_align == 1 && cur_lw < vw) lx = vx + (vw - cur_lw) / 2;
                        else if (s->text_align == 2 && cur_lw < vw) lx = vx + (vw - cur_lw);
                        text_engine_render(lx, cur_ly + 2, line_buf, col, fscale, &g_browser_default_font_chain, NULL);
                    }
                } else {
                    text_engine_render(vx, vy + 2, node->text, col, fscale, &g_browser_default_font_chain, NULL);
                    if (s->font_underline || is_link) {
                        int ulen = text_engine_measure(node->text, &g_browser_default_font_chain, fscale, 0, NULL, NULL);
                        int uy = (fscale == 2) ? (vy + 18) : (vy + 12);
                        fb_rect(vx, uy, ulen, 1, col);
                    }
                }
            }
        }
        /* 4. Images (Fast 16.16 fixed-point direct backbuffer blit) */
        else if (node->tag == HTML_TAG_IMG) {
            if (node->img_pixels && node->img_w > 0 && node->img_h > 0) {
                fb_blit_rgba_scaled(vx, vy, vw, vh, node->img_pixels, node->img_w, node->img_h);
            } else {
                fb_round_rect(vx, vy, vw, vh, 4, 0xF1F3F4);
                fb_round_rect_border(vx, vy, vw, vh, 4, 0xDADCE0);
                if (node->alt && node->alt[0] && vw > 48) {
                    font_icon(vx + 8, vy + (vh - 16) / 2, ICON_PHOTO, 0x5F6368);
                    char alt_clip[48];
                    int max_chars = (vw - 32) / 8;
                    if (max_chars > 46) max_chars = 46;
                    if (max_chars > 0) {
                        strncpy(alt_clip, node->alt, max_chars);
                        alt_clip[max_chars] = 0;
                        text_engine_render(vx + 28, vy + (vh - 14) / 2, alt_clip, 0x5F6368, 1, &g_browser_default_font_chain, NULL);
                    }
                } else {
                    font_icon(vx + vw/2 - 8, vy + vh/2 - 8, ICON_PHOTO, 0x1A73E8);
                }
            }
        }
        /* 5. Video Player Context */
        else if (node->tag == HTML_TAG_VIDEO && node->video_ctx) {
            html5_media_render_video((html5_video_ctx_t*)node->video_ctx, vx, vy, vw, vh);
        }
        /* 5b. Progress / Meter Bar */
        else if (node->tag == HTML_TAG_PROGRESS || node->tag == HTML_TAG_METER) {
            fb_round_rect(vx, vy, vw, vh, vh / 2, 0xE5E7EB);
            fb_round_rect(vx, vy, (vw * 2) / 3, vh, vh / 2, 0x2563EB);
        }
        /* 6. Form Input */
        else if (node->tag == HTML_TAG_INPUT) {
            int is_btn = (strcasecmp(node->input_type, "submit") == 0 || strcasecmp(node->input_type, "button") == 0);
            if (is_btn) {
                u32 bg = node->disabled ? 0xE8EAED : (s->has_bg ? s->bg_color : 0xF8F9FA);
                u32 bcol = s->border_color ? s->border_color : 0xDADCE0;
                int rad = s->border_radius > 0 ? s->border_radius : 4;
                fb_round_rect(vx, vy, vw, vh, rad, bg);
                fb_round_rect_border(vx, vy, vw, vh, rad, bcol);
                if (node->is_focused) {
                    fb_round_rect_border(vx - 1, vy - 1, vw + 2, vh + 2, rad + 1, 0x1A73E8);
                }
                const char *label = get_button_label(node, "Submit");
                int tw = text_engine_measure(label, &g_browser_default_font_chain, 1, 0, NULL, NULL);
                int tx = vx + (vw - tw) / 2;
                if (tx < vx + 4) tx = vx + 4;
                int ty = vy + (vh - 14) / 2;
                u32 text_col = node->disabled ? 0x80868B : (s->color ? s->color : 0x3C4043);
                text_engine_render(tx, ty, label, text_col, 1, &g_browser_default_font_chain, NULL);
            } else {
                u32 border_col = node->is_focused ? 0x1A73E8 : (s->border_color ? s->border_color : 0xDFE1E5);
                u32 bg = node->disabled ? 0xF1F3F4 : (s->has_bg ? s->bg_color : 0xFFFFFF);
                int rad = s->border_radius > 0 ? s->border_radius : 4;
                fb_round_rect(vx, vy, vw, vh, rad, bg);
                fb_round_rect_border(vx, vy, vw, vh, rad, border_col);
                if (node->is_focused) {
                    fb_round_rect_border(vx - 1, vy - 1, vw + 2, vh + 2, rad + 1, 0x1A73E8);
                }

                int is_pw = (strcasecmp(node->input_type, "password") == 0);
                char pw_buf[256];
                const char *display_txt = node->value;
                if (is_pw && node->value[0]) {
                    int vlen = strlen(node->value);
                    if (vlen > 250) vlen = 250;
                    for (int k = 0; k < vlen; k++) pw_buf[k] = '*';
                    pw_buf[vlen] = 0;
                    display_txt = pw_buf;
                } else if (!node->value[0]) {
                    display_txt = node->placeholder;
                }
                if (is_script_leakage(display_txt)) display_txt = "";
                u32 txt_col = node->disabled ? 0x80868B : (node->value[0] ? (s->color ? s->color : 0x202124) : 0x757575);
                text_engine_render(vx + 8, vy + (vh - 14) / 2, display_txt, txt_col, 1, &g_browser_default_font_chain, NULL);

                /* Blinking Caret when focused */
                if (node->is_focused && !node->disabled && !node->read_only && g_cursor_blink) {
                    int cpos = node->cursor_pos;
                    int vlen = strlen(node->value);
                    if (cpos < 0) cpos = 0;
                    if (cpos > vlen) cpos = vlen;
                    int cx = vx + 8 + cpos * 8;
                    if (cx > vx + vw - 4) cx = vx + vw - 4;
                    fb_rect(cx, vy + (vh - 16) / 2, 2, 16, 0x1A73E8);
                }
            }
        }
        /* 7. Form Button / Select */
        else if (node->tag == HTML_TAG_BUTTON || node->tag == HTML_TAG_SELECT) {
            u32 bg = node->disabled ? 0xE8EAED : (s->has_bg ? s->bg_color : 0xF8F9FA);
            u32 bcol = s->border_color ? s->border_color : 0xDADCE0;
            int rad = s->border_radius > 0 ? s->border_radius : 4;
            fb_round_rect(vx, vy, vw, vh, rad, bg);
            fb_round_rect_border(vx, vy, vw, vh, rad, bcol);
            if (node->is_focused) {
                fb_round_rect_border(vx - 1, vy - 1, vw + 2, vh + 2, rad + 1, 0x1A73E8);
            }
            const char *label = get_button_label(node, (node->tag == HTML_TAG_SELECT) ? "Select" : "Button");
            int tw = text_engine_measure(label, &g_browser_default_font_chain, 1, 0, NULL, NULL);
            int tx = vx + (vw - tw) / 2;
            if (tx < vx + 4) tx = vx + 4;
            int ty = vy + (vh - 14) / 2;
            u32 text_col = node->disabled ? 0x80868B : (s->color ? s->color : 0x3C4043);
            text_engine_render(tx, ty, label, text_col, 1, &g_browser_default_font_chain, NULL);
        }
        /* 7b. Form Textarea */
        else if (node->tag == HTML_TAG_TEXTAREA) {
            u32 border_col = node->is_focused ? 0x1A73E8 : (s->border_color ? s->border_color : 0xDFE1E5);
            u32 bg = node->disabled ? 0xF1F3F4 : (s->has_bg ? s->bg_color : 0xFFFFFF);
            int rad = s->border_radius > 0 ? s->border_radius : 4;
            fb_round_rect(vx, vy, vw, vh, rad, bg);
            fb_round_rect_border(vx, vy, vw, vh, rad, border_col);
            if (node->is_focused) {
                fb_round_rect_border(vx - 1, vy - 1, vw + 2, vh + 2, rad + 1, 0x1A73E8);
            }

            if (!node->value[0] && node->placeholder[0]) {
                text_engine_render(vx + 8, vy + 6, node->placeholder, 0x757575, 1, &g_browser_default_font_chain, NULL);
            } else {
                int ty = vy + 6;
                const char *p = node->value;
                int caret_r = -1, caret_c = -1;
                int cur_char_idx = 0;
                int row = 0;

                while (*p && ty + 14 <= vy + vh) {
                    char line_buf[128];
                    int col = 0;
                    while (*p && *p != '\n' && col < (int)sizeof(line_buf) - 1) {
                        if (cur_char_idx == node->cursor_pos) {
                            caret_r = row;
                            caret_c = col;
                        }
                        line_buf[col++] = *p++;
                        cur_char_idx++;
                    }
                    if (cur_char_idx == node->cursor_pos && caret_r < 0) {
                        caret_r = row;
                        caret_c = col;
                    }
                    line_buf[col] = 0;
                    u32 txt_col = node->disabled ? 0x80868B : (s->color ? s->color : 0x202124);
                    text_engine_render(vx + 8, ty, line_buf, txt_col, 1, &g_browser_default_font_chain, NULL);
                    if (*p == '\n') {
                        cur_char_idx++;
                        p++;
                    }
                    row++;
                    ty += 16;
                }
                if (cur_char_idx <= node->cursor_pos && caret_r < 0) {
                    caret_r = row > 0 ? (row - 1) : 0;
                    caret_c = 0;
                }

                if (node->is_focused && !node->disabled && !node->read_only && g_cursor_blink) {
                    if (caret_r >= 0 && caret_c >= 0) {
                        int cx = vx + 8 + caret_c * 8;
                        int cy = vy + 6 + caret_r * 16;
                        if (cx > vx + vw - 4) cx = vx + vw - 4;
                        if (cy + 14 <= vy + vh) {
                            fb_rect(cx, cy, 2, 14, 0x1A73E8);
                        }
                    }
                }
            }
        }
        /* 8. Table Cells */
        else if (node->tag == HTML_TAG_TH || node->tag == HTML_TAG_TD) {
            if (s->has_bg) {
                fb_rect(vx, vy, vw, vh, s->bg_color);
            }
            if (s->border_width > 0 || node->tag == HTML_TAG_TH) {
                fb_border(vx, vy, vw, vh, 0xDFE1E5);
            }
        }
        /* 9. List Item Bullet / Summary Disclosure Marker */
        else if (node->tag == HTML_TAG_LI && !s->list_style_none && (!node->parent || !node->parent->style.list_style_none)) {
            fb_round_rect(vx - 12, vy + 6, 4, 4, 2, 0x1A73E8);
        }
        else if (node->tag == HTML_TAG_SUMMARY) {
            fb_round_rect(vx - 12, vy + 5, 6, 6, 1, 0x5F6368);
        }
    }

    /* Recurse visible children only. Metadata, styles, scripts, and titles
       participate in parsing but must never appear as page text. */
    if (!html5_node_is_non_visual(node) &&
        node->tag != HTML_TAG_BUTTON && node->tag != HTML_TAG_SELECT && node->tag != HTML_TAG_TEXTAREA) {
        html_node_t *ch = node->first_child;
        while (ch) {
            render_node(ch, content_x, content_y, content_w, content_h, scroll_y);
            ch = ch->next_sibling;
        }
    }
}

void html5_render_document(int content_x, int content_y, int content_w, int content_h, int scroll_y) {
    if (!g_root_dom) {
        serial_write("[browser_core] paint skipped: DOM root is null\n");
        browser_log_warn(BLOG_CAT_RENDER, "Painting skipped: DOM root is NULL");
        return;
    }
    if (content_w <= 0 || content_h <= 0) {
        browser_log_warn(BLOG_CAT_RENDER, "Painting skipped: invalid viewport dimensions (%dx%d)", content_w, content_h);
        return;
    }

    serial_write("[browser_core] paint start\n");

    /* Base viewport background */
    u32 canvas_bg = 0xFFFFFF; /* Web standard opaque white default */
    if (g_root_dom) {
        if (g_root_dom->style.has_bg) {
            canvas_bg = g_root_dom->style.bg_color;
        } else {
            for (int i = 0; i < g_node_count; i++) {
                html_node_t *bn = html5_engine_get_node(i);
                if (bn && bn->tag == HTML_TAG_BODY && bn->style.has_bg) {
                    canvas_bg = bn->style.bg_color;
                    break;
                }
            }
        }
    }
    fb_rect(content_x, content_y, content_w, content_h, canvas_bg);

    /* Clip to content area */
    fb_set_clip(content_x, content_y, content_w - 10, content_h);

    render_node(g_root_dom, content_x, content_y, content_w, content_h, scroll_y);

    fb_reset_clip();

    /* Render vertical scrollbar if document height exceeds viewport */
    if (g_doc_height > content_h) {
        int sb_x = content_x + content_w - 8;
        fb_rect(sb_x, content_y, 8, content_h, 0xF1F5F9);

        int thumb_h = (content_h * content_h) / g_doc_height;
        if (thumb_h < 24) thumb_h = 24;
        int max_scroll = g_doc_height - content_h;
        int thumb_y = content_y + ((content_h - thumb_h) * scroll_y) / (max_scroll > 0 ? max_scroll : 1);

        fb_round_rect(sb_x + 1, thumb_y, 6, thumb_h, 3, 0x94A3B8);
    }

    serial_write("[browser_core] paint done\n");
}

/* --------------------------------------------------------------------------
 * User Interaction — Mouse Clicks & Keyboard Input
 * -------------------------------------------------------------------------- */
static html_node_t *find_clicked_node(html_node_t *node, int doc_x, int doc_y) {
    if (!node) return NULL;
    if (node->style.display_none || html5_node_is_non_visual(node)) return NULL;

    /* Check children first (topmost in tree) */
    html_node_t *ch = node->first_child;
    while (ch) {
        html_node_t *hit = find_clicked_node(ch, doc_x, doc_y);
        if (hit) return hit;
        ch = ch->next_sibling;
    }

    if (point_in_rect(doc_x, doc_y, node->bounds)) {
        return node;
    }
    return NULL;
}

static void form_url_encode_append(char *dst, int max_len, const char *src) {
    if (!dst || !src || max_len <= 0) return;
    int dlen = strlen(dst);
    for (int i = 0; src[i] && dlen < max_len - 4; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[dlen++] = c;
        } else if (c == ' ') {
            dst[dlen++] = '+';
        } else if (c == '\n') {
            dst[dlen++] = '%';
            dst[dlen++] = '0';
            dst[dlen++] = 'D';
            dst[dlen++] = '%';
            dst[dlen++] = '0';
            dst[dlen++] = 'A';
        } else if (c == '\r') {
            continue;
        } else {
            static const char hex[] = "0123456789ABCDEF";
            dst[dlen++] = '%';
            dst[dlen++] = hex[(c >> 4) & 0x0F];
            dst[dlen++] = hex[c & 0x0F];
        }
    }
    dst[dlen] = 0;
}

static void form_append_kv(char *url, int max_len, int *first, const char *key, const char *val) {
    if (!url || !key || !key[0] || max_len <= 0) return;
    int n = strlen(url);
    if (n >= max_len - 4) return;
    url[n++] = *first ? '&' : '?';
    url[n] = 0;
    *first = 1;
    form_url_encode_append(url, max_len, key);
    n = strlen(url);
    if (n < max_len - 2) {
        url[n++] = '=';
        url[n] = 0;
        if (val) {
            form_url_encode_append(url, max_len, val);
        }
    }
}

static void form_find_inputs(html_node_t *node, char *url, int max_len, int *first, html_node_t *submit_btn) {
    if (!node || !url || *first >= max_len - 1) return;
    if (!node->disabled && node->name[0]) {
        if (node->tag == HTML_TAG_INPUT) {
            int is_btn = (strcasecmp(node->input_type, "submit") == 0 ||
                          strcasecmp(node->input_type, "button") == 0);
            if (!is_btn || node == submit_btn) {
                const char *val = node->value;
                if (is_btn && !val[0]) val = get_button_label(node, "Submit");
                form_append_kv(url, max_len, first, node->name, val);
            }
        } else if (node->tag == HTML_TAG_TEXTAREA) {
            form_append_kv(url, max_len, first, node->name, node->value);
        } else if (node->tag == HTML_TAG_BUTTON && node == submit_btn) {
            const char *val = node->value[0] ? node->value : get_button_label(node, "Submit");
            form_append_kv(url, max_len, first, node->name, val);
        }
    }
    for (html_node_t *ch = node->first_child; ch; ch = ch->next_sibling) {
        form_find_inputs(ch, url, max_len, first, submit_btn);
    }
}

static void form_submit(html_node_t *form, html_node_t *submit_btn) {
    if (!form || (form->method[0] && strcasecmp(form->method, "post") == 0)) return;
    const char *action = form->action;
    if (!action || !action[0]) {
        /* Detect search forms without explicit action */
        int has_q = 0, has_yt = 0, has_wiki = 0;
        for (html_node_t *ch = form->first_child; ch; ch = ch->next_sibling) {
            if (strcasecmp(ch->name, "search_query") == 0) has_yt = 1;
            else if (strcasecmp(ch->name, "q") == 0) has_q = 1;
            else if (strcasecmp(ch->name, "search") == 0) has_wiki = 1;
        }
        if (has_yt) action = "https://www.youtube.com/results";
        else if (has_wiki) action = "https://en.wikipedia.org/wiki/Special:Search";
        else if (has_q) action = "https://www.google.com/search";
        else action = "about:form";
    }
    strncpy(g_pending_navigation, action, sizeof(g_pending_navigation) - 1);
    g_pending_navigation[sizeof(g_pending_navigation) - 1] = 0;
    int first = (strchr(g_pending_navigation, '?') != NULL) ? 1 : 0;
    for (html_node_t *ch = form->first_child; ch; ch = ch->next_sibling) {
        form_find_inputs(ch, g_pending_navigation, sizeof(g_pending_navigation), &first, submit_btn);
    }
    serial_write("[html5_engine] Form submitted -> ");
    serial_write(g_pending_navigation);
    serial_write("\n");
}

static void set_focused_control(html_node_t *node) {
    if (g_focused_input == node) return;
    if (g_focused_input) {
        g_focused_input->is_focused = 0;
    }
    g_focused_input = node;
    if (node) {
        node->is_focused = 1;
        g_cursor_blink = 1;
        g_blink_ticks = 0;
    }
    extern int needs_redraw;
    extern int gui_window_dirty_only;
    needs_redraw = 1;
    gui_window_dirty_only = 1;
}

static html_node_t *find_form_control_in_subtree(html_node_t *root) {
    if (!root) return NULL;
    if (root->tag == HTML_TAG_INPUT || root->tag == HTML_TAG_BUTTON || root->tag == HTML_TAG_TEXTAREA) {
        return root;
    }
    for (html_node_t *c = root->first_child; c; c = c->next_sibling) {
        html_node_t *found = find_form_control_in_subtree(c);
        if (found) return found;
    }
    return NULL;
}

static void collect_focusable_controls(html_node_t *node, html_node_t **out_list, int *out_count, int max_count) {
    if (!node || !out_list || *out_count >= max_count) return;
    if (!node->style.display_none && !node->disabled) {
        if (node->tag == HTML_TAG_INPUT) {
            if (strcasecmp(node->input_type, "hidden") != 0) {
                out_list[(*out_count)++] = node;
            }
        } else if (node->tag == HTML_TAG_BUTTON || node->tag == HTML_TAG_TEXTAREA) {
            out_list[(*out_count)++] = node;
        }
    }
    for (html_node_t *ch = node->first_child; ch; ch = ch->next_sibling) {
        collect_focusable_controls(ch, out_list, out_count, max_count);
    }
}

int html5_focus_next_control(int direction) {
    if (!g_root_dom) return 0;
    html_node_t *controls[64];
    int count = 0;
    collect_focusable_controls(g_root_dom, controls, &count, 64);
    if (count == 0) {
        set_focused_control(NULL);
        return 0;
    }
    int cur_idx = -1;
    for (int i = 0; i < count; i++) {
        if (controls[i] == g_focused_input) {
            cur_idx = i;
            break;
        }
    }
    int next_idx = 0;
    if (cur_idx >= 0) {
        next_idx = (cur_idx + direction) % count;
        if (next_idx < 0) next_idx += count;
    } else {
        next_idx = (direction >= 0) ? 0 : (count - 1);
    }
    html_node_t *next_ctrl = controls[next_idx];
    set_focused_control(next_ctrl);
    next_ctrl->cursor_pos = strlen(next_ctrl->value);
    serial_write("[html5_engine] Focus cycled -> ");
    serial_write(next_ctrl->name[0] ? next_ctrl->name : html5_tag_name(next_ctrl->tag));
    serial_write("\n");
    return 1;
}

const char *html5_take_navigation_request(void) {
    if (!g_pending_navigation[0]) return NULL;
    strncpy(g_last_taken_navigation, g_pending_navigation, sizeof(g_last_taken_navigation) - 1);
    g_last_taken_navigation[sizeof(g_last_taken_navigation) - 1] = 0;
    g_pending_navigation[0] = 0;
    return g_last_taken_navigation;
}

void html5_request_navigation(const char *url) {
    if (!url || !url[0]) return;
    strncpy(g_pending_navigation, url, sizeof(g_pending_navigation) - 1);
    g_pending_navigation[sizeof(g_pending_navigation) - 1] = 0;
}

int html5_get_hovered_link(int content_x, int content_y, int scroll_y, int mx, int my, char *out_url, int max_len) {
    if (!g_root_dom || !out_url || max_len <= 0) return 0;
    out_url[0] = 0;
    int doc_x = mx - content_x;
    int doc_y = my - content_y + scroll_y;
    html_node_t *hit = find_clicked_node(g_root_dom, doc_x, doc_y);
    if (!hit) return 0;
    html_node_t *link = hit;
    while (link && link->tag != HTML_TAG_A) link = link->parent;
    if (link && link->href[0]) {
        strncpy(out_url, link->href, max_len - 1);
        out_url[max_len - 1] = 0;
        return 1;
    }
    return 0;
}

int html5_handle_click(int content_x, int content_y, int scroll_y, int mx, int my, char *out_nav_url, int max_len) {
    if (!g_root_dom) return 0;

    int doc_x = mx - content_x;
    int doc_y = my - content_y + scroll_y;

    html_node_t *hit = find_clicked_node(g_root_dom, doc_x, doc_y);
    if (!hit) {
        if (g_focused_input) {
            set_focused_control(NULL);
        }
        return 0;
    }

    /* Check onclick handler on hit node or its parents */
    html_node_t *on_node = hit;
    while (on_node) {
        if (on_node->onclick[0]) {
            serial_write("[html5_engine] Executing onclick: ");
            serial_write(on_node->onclick);
            serial_write("\n");
            html5_script_eval(on_node->onclick, g_root_dom);
            break;
        }
        on_node = on_node->parent;
    }

    /* 1. Clicked Hyperlink <a> */
    html_node_t *link = hit;
    while (link && link->tag != HTML_TAG_A) link = link->parent;
    if (link && link->href[0]) {
        if (out_nav_url) {
            strncpy(out_nav_url, link->href, max_len - 1);
            out_nav_url[max_len - 1] = 0;
        }
        serial_write("[html5_engine] Link clicked -> ");
        serial_write(link->href);
        serial_write("\n");
        return 1;
    }

    /* 2. Clicked <video> */
    html_node_t *vid = hit;
    while (vid && vid->tag != HTML_TAG_VIDEO) vid = vid->parent;
    if (vid && vid->video_ctx) {
        int handled = html5_media_handle_click((html5_video_ctx_t*)vid->video_ctx, doc_x, doc_y);
        if (handled) return 1;
    }

    /* 3. Clicked <label> or element within <label> */
    html_node_t *lbl = hit;
    while (lbl && lbl->tag != HTML_TAG_LABEL) lbl = lbl->parent;
    if (lbl) {
        html_node_t *target = NULL;
        if (lbl->for_id[0]) {
            target = html5_get_element_by_id(lbl->for_id);
        }
        if (!target) {
            target = find_form_control_in_subtree(lbl);
        }
        if (target && !target->disabled) {
            if (target->tag == HTML_TAG_BUTTON ||
                (target->tag == HTML_TAG_INPUT &&
                 (strcasecmp(target->input_type, "submit") == 0 ||
                  strcasecmp(target->input_type, "button") == 0))) {
                html_node_t *form = target->parent;
                while (form && form->tag != HTML_TAG_FORM) form = form->parent;
                form_submit(form, target);
                return 1;
            } else if (target->tag == HTML_TAG_INPUT || target->tag == HTML_TAG_TEXTAREA) {
                set_focused_control(target);
                target->cursor_pos = strlen(target->value);
                return 1;
            }
        }
    }

    /* 4. Clicked <input> */
    if (hit->tag == HTML_TAG_INPUT && !hit->disabled) {
        if (strcasecmp(hit->input_type, "submit") == 0 || strcasecmp(hit->input_type, "button") == 0) {
            serial_write("[html5_engine] Submit input clicked\n");
            html_node_t *form = hit->parent;
            while (form && form->tag != HTML_TAG_FORM) form = form->parent;
            form_submit(form, hit);
            return 1;
        }
        set_focused_control(hit);
        int rel_x = doc_x - (hit->bounds.x + 8);
        int cpos = rel_x / 8;
        int len = strlen(hit->value);
        if (cpos < 0) cpos = 0;
        if (cpos > len) cpos = len;
        hit->cursor_pos = cpos;
        serial_write("[html5_engine] Form input focused: ");
        serial_write(hit->name);
        serial_write("\n");
        return 1;
    }

    /* 5. Clicked <textarea> */
    if (hit->tag == HTML_TAG_TEXTAREA && !hit->disabled) {
        set_focused_control(hit);
        int rel_x = doc_x - (hit->bounds.x + 8);
        int rel_y = doc_y - (hit->bounds.y + 6);
        int target_row = rel_y / 16;
        int target_col = rel_x / 8;
        if (target_row < 0) target_row = 0;
        if (target_col < 0) target_col = 0;

        int cur_r = 0, cur_c = 0;
        int cidx = 0;
        const char *p = hit->value;
        while (*p && cur_r < target_row) {
            if (*p == '\n') cur_r++;
            p++;
            cidx++;
        }
        while (*p && *p != '\n' && cur_c < target_col) {
            p++;
            cur_c++;
            cidx++;
        }
        hit->cursor_pos = cidx;
        serial_write("[html5_engine] Textarea focused: ");
        serial_write(hit->name);
        serial_write("\n");
        return 1;
    }

    /* 6. Clicked <button> */
    html_node_t *btn_node = hit;
    while (btn_node && btn_node->tag != HTML_TAG_BUTTON) btn_node = btn_node->parent;
    if (btn_node && !btn_node->disabled) {
        serial_write("[html5_engine] Button clicked: ");
        serial_write(btn_node->value[0] ? btn_node->value : btn_node->text);
        serial_write("\n");
        set_focused_control(btn_node);
        html_node_t *form = btn_node->parent;
        while (form && form->tag != HTML_TAG_FORM) form = form->parent;
        form_submit(form, btn_node);
        return 1;
    }

    /* Reset focus if clicked elsewhere */
    if (g_focused_input) {
        set_focused_control(NULL);
    }
    return 0;
}

int html5_handle_key(unsigned char key, char ascii) {
    /* Tab / Shift-Tab */
    if (key == 0x0F || ascii == '\t') {
        return html5_focus_next_control(1);
    }

    if (!g_focused_input) return 0;
    if (g_focused_input->disabled) return 0;

    int len = strlen(g_focused_input->value);

    /* Left Arrow */
    if (key == 0x4B) {
        if (g_focused_input->cursor_pos > 0) {
            g_focused_input->cursor_pos--;
            g_cursor_blink = 1; g_blink_ticks = 0;
            extern int needs_redraw, gui_window_dirty_only;
            needs_redraw = 1; gui_window_dirty_only = 1;
        }
        return 1;
    }
    /* Right Arrow */
    if (key == 0x4D) {
        if (g_focused_input->cursor_pos < len) {
            g_focused_input->cursor_pos++;
            g_cursor_blink = 1; g_blink_ticks = 0;
            extern int needs_redraw, gui_window_dirty_only;
            needs_redraw = 1; gui_window_dirty_only = 1;
        }
        return 1;
    }
    /* Home */
    if (key == 0x47) {
        g_focused_input->cursor_pos = 0;
        g_cursor_blink = 1; g_blink_ticks = 0;
        extern int needs_redraw, gui_window_dirty_only;
        needs_redraw = 1; gui_window_dirty_only = 1;
        return 1;
    }
    /* End */
    if (key == 0x4F) {
        g_focused_input->cursor_pos = len;
        g_cursor_blink = 1; g_blink_ticks = 0;
        extern int needs_redraw, gui_window_dirty_only;
        needs_redraw = 1; gui_window_dirty_only = 1;
        return 1;
    }

    /* Backspace */
    if (key == 0x0E) {
        if (g_focused_input->read_only) return 1;
        if (g_focused_input->cursor_pos > 0 && len > 0) {
            int pos = g_focused_input->cursor_pos;
            for (int i = pos - 1; i < len; i++) {
                g_focused_input->value[i] = g_focused_input->value[i + 1];
            }
            g_focused_input->cursor_pos--;
            g_cursor_blink = 1; g_blink_ticks = 0;
            extern int needs_redraw, gui_window_dirty_only;
            needs_redraw = 1; gui_window_dirty_only = 1;
            return 1;
        }
        return 1;
    }

    /* Delete */
    if (key == 0x53) {
        if (g_focused_input->read_only) return 1;
        if (g_focused_input->cursor_pos < len && len > 0) {
            int pos = g_focused_input->cursor_pos;
            for (int i = pos; i < len; i++) {
                g_focused_input->value[i] = g_focused_input->value[i + 1];
            }
            g_cursor_blink = 1; g_blink_ticks = 0;
            extern int needs_redraw, gui_window_dirty_only;
            needs_redraw = 1; gui_window_dirty_only = 1;
            return 1;
        }
        return 1;
    }

    /* Space on Button triggers click/submit */
    if (ascii == ' ' && (g_focused_input->tag == HTML_TAG_BUTTON ||
        (g_focused_input->tag == HTML_TAG_INPUT &&
         (strcasecmp(g_focused_input->input_type, "submit") == 0 ||
          strcasecmp(g_focused_input->input_type, "button") == 0)))) {
        html_node_t *form = g_focused_input->parent;
        while (form && form->tag != HTML_TAG_FORM) form = form->parent;
        form_submit(form, g_focused_input);
        return 1;
    }

    /* Enter / Return */
    if (key == 0x1C || ascii == '\n') {
        if (g_focused_input->tag == HTML_TAG_TEXTAREA && g_focused_input->rows > 1 &&
            strcasecmp(g_focused_input->name, "q") != 0 &&
            strcasecmp(g_focused_input->name, "search") != 0 &&
            strcasecmp(g_focused_input->name, "search_query") != 0) {
            if (!g_focused_input->read_only && len < (int)sizeof(g_focused_input->value) - 2) {
                int pos = g_focused_input->cursor_pos;
                for (int i = len; i >= pos; i--) {
                    g_focused_input->value[i + 1] = g_focused_input->value[i];
                }
                g_focused_input->value[pos] = '\n';
                g_focused_input->cursor_pos++;
                g_cursor_blink = 1; g_blink_ticks = 0;
                extern int needs_redraw, gui_window_dirty_only;
                needs_redraw = 1; gui_window_dirty_only = 1;
                return 1;
            }
            return 1;
        } else {
            /* Input, search textarea, or Button submit */
            html_node_t *form = g_focused_input->parent;
            while (form && form->tag != HTML_TAG_FORM) form = form->parent;
            form_submit(form, g_focused_input);
            return 1;
        }
    }

    /* Printable ASCII */
    if (ascii >= 32 && ascii <= 126) {
        if (g_focused_input->read_only) return 1;
        if (len < (int)sizeof(g_focused_input->value) - 2) {
            int pos = g_focused_input->cursor_pos;
            if (pos > len) pos = len;
            if (pos < 0) pos = 0;
            for (int i = len; i >= pos; i--) {
                g_focused_input->value[i + 1] = g_focused_input->value[i];
            }
            g_focused_input->value[pos] = ascii;
            g_focused_input->cursor_pos = pos + 1;
            g_cursor_blink = 1; g_blink_ticks = 0;
            extern int needs_redraw, gui_window_dirty_only;
            needs_redraw = 1; gui_window_dirty_only = 1;
            return 1;
        }
    }

    return 0;
}

void html5_tick(void) {
    g_blink_ticks++;
    if (g_blink_ticks >= 30) {
        g_blink_ticks = 0;
        g_cursor_blink = !g_cursor_blink;
        if (g_focused_input) {
            extern int needs_redraw;
            extern int gui_window_dirty_only;
            needs_redraw = 1;
            gui_window_dirty_only = 1;
        }
    }

    html5_media_tick();
    html5_script_event_loop_tick();
}

const char *html5_tag_name(html_tag_t tag) {
    switch (tag) {
        case HTML_TAG_HTML:       return "html";
        case HTML_TAG_HEAD:       return "head";
        case HTML_TAG_BODY:       return "body";
        case HTML_TAG_TITLE:      return "title";
        case HTML_TAG_DIV:        return "div";
        case HTML_TAG_SECTION:    return "section";
        case HTML_TAG_ARTICLE:    return "article";
        case HTML_TAG_HEADER:     return "header";
        case HTML_TAG_FOOTER:     return "footer";
        case HTML_TAG_NAV:        return "nav";
        case HTML_TAG_MAIN:       return "main";
        case HTML_TAG_ASIDE:      return "aside";
        case HTML_TAG_H1:         return "h1";
        case HTML_TAG_H2:         return "h2";
        case HTML_TAG_H3:         return "h3";
        case HTML_TAG_H4:         return "h4";
        case HTML_TAG_H5:         return "h5";
        case HTML_TAG_H6:         return "h6";
        case HTML_TAG_P:          return "p";
        case HTML_TAG_BLOCKQUOTE: return "blockquote";
        case HTML_TAG_PRE:        return "pre";
        case HTML_TAG_CODE:       return "code";
        case HTML_TAG_HR:         return "hr";
        case HTML_TAG_BR:         return "br";
        case HTML_TAG_A:          return "a";
        case HTML_TAG_SPAN:       return "span";
        case HTML_TAG_B:          return "b";
        case HTML_TAG_STRONG:     return "strong";
        case HTML_TAG_I:          return "i";
        case HTML_TAG_EM:         return "em";
        case HTML_TAG_U:          return "u";
        case HTML_TAG_MARK:       return "mark";
        case HTML_TAG_SMALL:      return "small";
        case HTML_TAG_UL:         return "ul";
        case HTML_TAG_OL:         return "ol";
        case HTML_TAG_LI:         return "li";
        case HTML_TAG_DL:         return "dl";
        case HTML_TAG_DT:         return "dt";
        case HTML_TAG_DD:         return "dd";
        case HTML_TAG_TABLE:      return "table";
        case HTML_TAG_THEAD:      return "thead";
        case HTML_TAG_TBODY:      return "tbody";
        case HTML_TAG_TR:         return "tr";
        case HTML_TAG_TH:         return "th";
        case HTML_TAG_TD:         return "td";
        case HTML_TAG_FORM:       return "form";
        case HTML_TAG_INPUT:      return "input";
        case HTML_TAG_BUTTON:     return "button";
        case HTML_TAG_TEXTAREA:   return "textarea";
        case HTML_TAG_LABEL:      return "label";
        case HTML_TAG_SELECT:     return "select";
        case HTML_TAG_OPTION:     return "option";
        case HTML_TAG_FIELDSET:   return "fieldset";
        case HTML_TAG_LEGEND:     return "legend";
        case HTML_TAG_PROGRESS:   return "progress";
        case HTML_TAG_METER:      return "meter";
        case HTML_TAG_IMG:        return "img";
        case HTML_TAG_VIDEO:      return "video";
        case HTML_TAG_AUDIO:      return "audio";
        case HTML_TAG_CANVAS:     return "canvas";
        case HTML_TAG_CENTER:     return "center";
        case HTML_TAG_STYLE:      return "style";
        case HTML_TAG_SCRIPT:     return "script";
        case HTML_TAG_LINK:       return "link";
        case HTML_TAG_META:       return "meta";
        case HTML_TAG_NOSCRIPT:   return "noscript";
        case HTML_TAG_IFRAME:     return "iframe";
        case HTML_TAG_FIGURE:     return "figure";
        case HTML_TAG_FIGCAPTION: return "figcaption";
        case HTML_TAG_DETAILS:    return "details";
        case HTML_TAG_SUMMARY:    return "summary";
        case HTML_TAG_TIME:       return "time";
        case HTML_TAG_KBD:        return "kbd";
        case HTML_TAG_SAMP:       return "samp";
        case HTML_TAG_SUB:        return "sub";
        case HTML_TAG_SUP:        return "sup";
        case HTML_TAG_DIALOG:     return "dialog";
        case HTML_TAG_ADDRESS:    return "address";
        case HTML_TAG_SVG:        return "svg";
        case HTML_TAG_TEXT_NODE:  return "text";
        default:                  return "unknown";
    }
}

html_node_t *html5_get_root_dom(void) {
    return g_root_dom;
}

html_node_t *html5_find_node_at(int content_x, int content_y, int scroll_y, int mx, int my) {
    if (!g_root_dom) return NULL;
    int doc_x = mx - content_x;
    int doc_y = my - content_y + scroll_y;
    html_node_t *node = find_clicked_node(g_root_dom, doc_x, doc_y);
    if (node && node->tag == HTML_TAG_TEXT_NODE && node->parent) {
        return node->parent;
    }
    return node ? node : g_root_dom;
}

void html5_get_node_breadcrumb(html_node_t *node, char *buf, int max_len) {
    if (!node || !buf || max_len <= 0) return;
    buf[0] = 0;
    html_node_t *chain[24];
    int count = 0;
    html_node_t *curr = node;
    while (curr && count < 24) {
        chain[count++] = curr;
        curr = curr->parent;
    }
    int len = 0;
    for (int i = count - 1; i >= 0; i--) {
        const char *tname = html5_tag_name(chain[i]->tag);
        int tlen = strlen(tname);
        if (len + tlen + 4 >= max_len) break;
        if (len > 0) {
            buf[len++] = ' ';
            buf[len++] = '>';
            buf[len++] = ' ';
        }
        memcpy(buf + len, tname, tlen);
        len += tlen;
        if (chain[i]->id[0] && len + strlen(chain[i]->id) + 2 < max_len) {
            buf[len++] = '#';
            int idlen = strlen(chain[i]->id);
            memcpy(buf + len, chain[i]->id, idlen);
            len += idlen;
        } else if (chain[i]->class_name[0] && len + 2 < max_len) {
            buf[len++] = '.';
            int clen = strlen(chain[i]->class_name);
            for (int k = 0; k < clen && chain[i]->class_name[k] != ' ' && len < max_len - 1; k++) {
                buf[len++] = chain[i]->class_name[k];
            }
        }
    }
    buf[len] = 0;
}

static int count_visible_text_recursive(const html_node_t *node) {
    if (!node || node->style.display_none || html5_node_is_non_visual(node)) return 0;
    int cnt = 0;
    if (node->tag == HTML_TAG_TEXT_NODE && html5_node_is_renderable_text(node) &&
        node->bounds.w > 0 && node->bounds.h > 0) {
        for (int i = 0; i < node->text_len; i++) {
            char c = node->text[i];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                cnt = 1;
                break;
            }
        }
    }
    for (const html_node_t *ch = node->first_child; ch; ch = ch->next_sibling) {
        cnt += count_visible_text_recursive(ch);
    }
    return cnt;
}

int html5_engine_get_visible_text_count(void) {
    if (!g_root_dom) return 0;
    return count_visible_text_recursive(g_root_dom);
}

