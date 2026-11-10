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

int html5_engine_get_node_count(void) {
    return g_node_count;
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

    if (tag == HTML_TAG_HEAD || tag == HTML_TAG_SCRIPT ||
        tag == HTML_TAG_STYLE || tag == HTML_TAG_META ||
        tag == HTML_TAG_LINK || tag == HTML_TAG_TITLE) {
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
    if (len == 4 && strncasecmp(name, "html", 4) == 0) return HTML_TAG_HTML;
    if (len == 4 && strncasecmp(name, "head", 4) == 0) return HTML_TAG_HEAD;
    if (len == 4 && strncasecmp(name, "body", 4) == 0) return HTML_TAG_BODY;
    if (len == 5 && strncasecmp(name, "title", 5) == 0) return HTML_TAG_TITLE;
    if (len == 3 && strncasecmp(name, "div", 3) == 0) return HTML_TAG_DIV;
    if (len == 7 && strncasecmp(name, "section", 7) == 0) return HTML_TAG_SECTION;
    if (len == 7 && strncasecmp(name, "article", 7) == 0) return HTML_TAG_ARTICLE;
    if (len == 6 && strncasecmp(name, "header", 6) == 0) return HTML_TAG_HEADER;
    if (len == 6 && strncasecmp(name, "footer", 6) == 0) return HTML_TAG_FOOTER;
    if (len == 3 && strncasecmp(name, "nav", 3) == 0) return HTML_TAG_NAV;
    if (len == 4 && strncasecmp(name, "main", 4) == 0) return HTML_TAG_MAIN;
    if (len == 5 && strncasecmp(name, "aside", 5) == 0) return HTML_TAG_ASIDE;
    if (len == 2 && (name[0] == 'h' || name[0] == 'H')) {
        if (name[1] == '1') return HTML_TAG_H1;
        if (name[1] == '2') return HTML_TAG_H2;
        if (name[1] == '3') return HTML_TAG_H3;
        if (name[1] == '4') return HTML_TAG_H4;
        if (name[1] == '5') return HTML_TAG_H5;
        if (name[1] == '6') return HTML_TAG_H6;
    }
    if (len == 1 && (name[0] == 'p' || name[0] == 'P')) return HTML_TAG_P;
    if (len == 10 && strncasecmp(name, "blockquote", 10) == 0) return HTML_TAG_BLOCKQUOTE;
    if (len == 3 && strncasecmp(name, "pre", 3) == 0) return HTML_TAG_PRE;
    if (len == 4 && strncasecmp(name, "code", 4) == 0) return HTML_TAG_CODE;
    if (len == 2 && strncasecmp(name, "hr", 2) == 0) return HTML_TAG_HR;
    if (len == 2 && strncasecmp(name, "br", 2) == 0) return HTML_TAG_BR;
    if (len == 1 && (name[0] == 'a' || name[0] == 'A')) return HTML_TAG_A;
    if (len == 4 && strncasecmp(name, "span", 4) == 0) return HTML_TAG_SPAN;
    if (len == 1 && (name[0] == 'b' || name[0] == 'B')) return HTML_TAG_B;
    if (len == 6 && strncasecmp(name, "strong", 6) == 0) return HTML_TAG_STRONG;
    if (len == 1 && (name[0] == 'i' || name[0] == 'I')) return HTML_TAG_I;
    if (len == 2 && strncasecmp(name, "em", 2) == 0) return HTML_TAG_EM;
    if (len == 1 && (name[0] == 'u' || name[0] == 'U')) return HTML_TAG_U;
    if (len == 4 && strncasecmp(name, "mark", 4) == 0) return HTML_TAG_MARK;
    if (len == 5 && strncasecmp(name, "small", 5) == 0) return HTML_TAG_SMALL;
    if (len == 2 && strncasecmp(name, "ul", 2) == 0) return HTML_TAG_UL;
    if (len == 2 && strncasecmp(name, "ol", 2) == 0) return HTML_TAG_OL;
    if (len == 2 && strncasecmp(name, "li", 2) == 0) return HTML_TAG_LI;
    if (len == 5 && strncasecmp(name, "table", 5) == 0) return HTML_TAG_TABLE;
    if (len == 5 && strncasecmp(name, "thead", 5) == 0) return HTML_TAG_THEAD;
    if (len == 5 && strncasecmp(name, "tbody", 5) == 0) return HTML_TAG_TBODY;
    if (len == 2 && strncasecmp(name, "tr", 2) == 0) return HTML_TAG_TR;
    if (len == 2 && strncasecmp(name, "th", 2) == 0) return HTML_TAG_TH;
    if (len == 2 && strncasecmp(name, "td", 2) == 0) return HTML_TAG_TD;
    if (len == 4 && strncasecmp(name, "form", 4) == 0) return HTML_TAG_FORM;
    if (len == 5 && strncasecmp(name, "input", 5) == 0) return HTML_TAG_INPUT;
    if (len == 6 && strncasecmp(name, "button", 6) == 0) return HTML_TAG_BUTTON;
    if (len == 8 && strncasecmp(name, "textarea", 8) == 0) return HTML_TAG_TEXTAREA;
    if (len == 5 && strncasecmp(name, "label", 5) == 0) return HTML_TAG_LABEL;
    if (len == 3 && strncasecmp(name, "img", 3) == 0) return HTML_TAG_IMG;
    if (len == 5 && strncasecmp(name, "video", 5) == 0) return HTML_TAG_VIDEO;
    if (len == 5 && strncasecmp(name, "audio", 5) == 0) return HTML_TAG_AUDIO;
    if (len == 6 && strncasecmp(name, "canvas", 6) == 0) return HTML_TAG_CANVAS;
    if (len == 6 && strncasecmp(name, "center", 6) == 0) return HTML_TAG_CENTER;
    if (len == 5 && strncasecmp(name, "style", 5) == 0) return HTML_TAG_STYLE;
    if (len == 6 && strncasecmp(name, "script", 6) == 0) return HTML_TAG_SCRIPT;
    if (len == 4 && strncasecmp(name, "link", 4) == 0) return HTML_TAG_LINK;
    if (len == 4 && strncasecmp(name, "meta", 4) == 0) return HTML_TAG_META;
    if (len == 8 && strncasecmp(name, "noscript", 8) == 0) return HTML_TAG_NOSCRIPT;
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
        else if (strcasecmp(key, "value") == 0) html5_node_set_value(node, val);
        else if (strcasecmp(key, "action") == 0) node->action = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "method") == 0) node->method = html5_arena_strdup(&g_arena, val);
        else if (strcasecmp(key, "placeholder") == 0) node->placeholder = html5_arena_strdup(&g_arena, val);
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
        strstr(s, "var ") || strstr(s, "window.") || strstr(s, "()") || strstr(s, "};") ||
        strstr(s, "return ") || strstr(s, "addEventListener") || strstr(s, "/*") ||
        strstr(s, "onclick") || strstr(s, "new ") || strstr(s, "null") || strstr(s, "undefined") ||
        strstr(s, "catch(") || strstr(s, "catch (") || strstr(s, "throw ") || strstr(s, "typeof ") ||
        strstr(s, "try{") || strstr(s, "try {") || strstr(s, "=>") || strstr(s, "else ") ||
        strstr(s, "for(") || strstr(s, "for (") || strstr(s, "if(") || strstr(s, "if (") ||
        strstr(s, "function ") || strstr(s, "function()") || strstr(s, ".call(") || strstr(s, ".apply(") ||
        strchr(s, ';') || strchr(s, '{') || strchr(s, '}')) {
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
            cur->tag == HTML_TAG_LINK || cur->tag == HTML_TAG_TITLE) {
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
        node->tag == HTML_TAG_LINK || node->tag == HTML_TAG_TITLE) {
        return 1;
    }
    const html_node_t *cur = node->parent;
    while (cur) {
        if (cur->tag == HTML_TAG_HEAD || cur->tag == HTML_TAG_SCRIPT ||
            cur->tag == HTML_TAG_STYLE || cur->tag == HTML_TAG_META ||
            cur->tag == HTML_TAG_LINK || cur->tag == HTML_TAG_TITLE ||
            cur->style.display_none) {
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
            s->font_bold = 1; s->is_block = 0;
            break;
        case HTML_TAG_I:
        case HTML_TAG_EM:
            s->font_italic = 1; s->is_block = 0;
            break;
        case HTML_TAG_BLOCKQUOTE:
            s->color = 0x5F6368; s->font_italic = 1;
            s->border_width = 3; s->border_color = 0xDADCE0;
            s->padding_left = 14; s->margin_top = 8; s->margin_bottom = 14; s->is_block = 1;
            break;
        case HTML_TAG_PRE:
        case HTML_TAG_CODE:
            s->color = 0x202124; s->bg_color = 0xF1F3F4; s->has_bg = 1;
            s->padding_top = 6; s->padding_bottom = 6; s->padding_left = 10; s->padding_right = 10;
            s->border_radius = 4; s->margin_top = 6; s->margin_bottom = 12; s->is_block = 1;
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
            s->color = 0x3C4043; s->bg_color = 0xF8F9FA; s->has_bg = 1;
            s->border_width = 1; s->border_color = 0xDADCE0;
            s->border_radius = 4; s->padding_top = 6; s->padding_bottom = 6;
            s->padding_left = 16; s->padding_right = 16;
            s->margin_top = 4; s->margin_bottom = 8; s->margin_left = 4; s->margin_right = 4;
            s->is_block = 0; s->is_inline_block = 1;
            break;
        case HTML_TAG_TEXTAREA:
            s->color = 0x202124; s->bg_color = 0xFFFFFF; s->has_bg = 1;
            s->border_width = 1; s->border_color = 0xDFE1E5; s->border_radius = 4;
            s->padding_top = 6; s->padding_bottom = 6; s->padding_left = 8; s->padding_right = 8;
            s->margin_top = 4; s->margin_bottom = 8; s->is_block = 0; s->is_inline_block = 1;
            break;
        case HTML_TAG_LABEL:
            s->color = 0x202124; s->is_block = 0; s->is_inline_block = 0;
            break;
        case HTML_TAG_TABLE:
            s->border_width = 1; s->border_color = 0xDFE1E5;
            s->margin_top = 8; s->margin_bottom = 16; s->is_block = 1;
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
                           node->tag == HTML_TAG_MAIN || node->tag == HTML_TAG_FORM ||
                           node->tag == HTML_TAG_UL || node->tag == HTML_TAG_OL ||
                           node->tag == HTML_TAG_LI || node->tag == HTML_TAG_TABLE ||
                           node->tag == HTML_TAG_THEAD || node->tag == HTML_TAG_TBODY ||
                           node->tag == HTML_TAG_TR || node->tag == HTML_TAG_CENTER ||
                           node->tag == HTML_TAG_NOSCRIPT);
            break;
    }
}

static int css_starts(const char *s, const char *p) {
    while (*p) {
        if (h_tolower(*s++) != h_tolower(*p++)) return 0;
    }
    return 1;
}

static u32 css_color(const char *v, u32 fallback) {
    if (css_starts(v, "red")) return 0xFF453A;
    if (css_starts(v, "green")) return 0x30D158;
    if (css_starts(v, "blue")) return 0x007AFF;
    if (css_starts(v, "white")) return 0xFFFFFF;
    if (css_starts(v, "black")) return 0x000000;
    if (css_starts(v, "gray") || css_starts(v, "grey")) return 0x8E8E93;
    if (v[0] == '#') {
        unsigned int n = 0;
        int i;
        for (i = 1; i < 7 && v[i]; i++) {
            char c = v[i];
            unsigned int d = (c >= '0' && c <= '9') ? (unsigned int)(c - '0') :
                             (c >= 'a' && c <= 'f') ? (unsigned int)(c - 'a' + 10) :
                             (c >= 'A' && c <= 'F') ? (unsigned int)(c - 'A' + 10) : 16;
            if (d > 15) return fallback;
            n = (n << 4) | d;
        }
        if (i == 4) n = ((n & 0xF00) << 8) | ((n & 0x0F0) << 4) | (n & 0x00F);
        if (i == 7 || i == 4) return n;
    }
    return fallback;
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
}

static void css_apply_declarations(html_node_t *node, const char *decls, int len) {
    int i = 0;
    while (i < len) {
        int ks = i;
        while (i < len && decls[i] != ':' && decls[i] != ';') i++;
        if (i >= len || decls[i] != ':') { while (i < len && decls[i] != ';') i++; i++; continue; }
        int kl = i - ks; i++;
        int vs = i;
        while (i < len && decls[i] != ';') i++;
        int vl = i - vs;
        char key[48], val[160];
        if (kl >= (int)sizeof(key)) kl = sizeof(key) - 1;
        if (vl >= (int)sizeof(val)) vl = sizeof(val) - 1;
        strncpy(key, decls + ks, kl); key[kl] = 0; css_trim(key);
        strncpy(val, decls + vs, vl); val[vl] = 0; css_trim(val);
        if (strcasecmp(key, "color") == 0) {
            node->style.color = css_color(val, node->style.color);
            node->style.has_explicit_color = 1;
        } else if (strcasecmp(key, "background") == 0 || strcasecmp(key, "background-color") == 0) {
            node->style.bg_color = css_color(val, 0xFFFFFF); node->style.has_bg = 1;
        } else if (strcasecmp(key, "font-size") == 0) {
            int px = 0; sscanf(val, "%d", &px); node->style.font_size = px >= 22 ? 3 : px >= 17 ? 2 : 1;
        } else if (strcasecmp(key, "font-weight") == 0 && (css_starts(val, "bold") || css_starts(val, "700"))) node->style.font_bold = 1;
        else if (strcasecmp(key, "font-style") == 0 && css_starts(val, "italic")) node->style.font_italic = 1;
        else if (strcasecmp(key, "text-decoration") == 0 && css_starts(val, "underline")) node->style.font_underline = 1;
        else if (strcasecmp(key, "text-align") == 0) {
            node->style.text_align = css_starts(val, "center") ? 1 : css_starts(val, "right") ? 2 : 0;
            node->style.has_explicit_align = 1;
        } else if (strcasecmp(key, "display") == 0) {
            if (node->tag == HTML_TAG_HEAD || node->tag == HTML_TAG_STYLE ||
                node->tag == HTML_TAG_SCRIPT || node->tag == HTML_TAG_META ||
                node->tag == HTML_TAG_LINK || node->tag == HTML_TAG_TITLE) {
                node->style.display_none = 1;
                node->style.is_block = 0;
            } else {
                node->style.display_none = css_starts(val, "none");
                if (css_starts(val, "inline-block")) {
                    node->style.is_block = 0;
                    node->style.is_inline_block = 1;
                    node->style.display_none = 0;
                } else if (css_starts(val, "inline")) {
                    node->style.is_block = 0;
                    node->style.is_inline_block = 0;
                } else if (css_starts(val, "block")) {
                    node->style.is_block = 1;
                    node->style.is_inline_block = 0;
                } else if (!node->style.display_none) {
                    node->style.is_block = 1;
                }
            }
        }
        else if (strcasecmp(key, "width") == 0) {
            if (strchr(val, '%')) {
                int w = 0; sscanf(val, "%d", &w);
                if (w > 0 && w <= 100) node->style.width = -w;
            } else {
                sscanf(val, "%d", &node->style.width);
            }
        }
        else if (strcasecmp(key, "height") == 0) {
            if (!strchr(val, '%')) {
                sscanf(val, "%d", &node->style.height);
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
                    int p = 0; sscanf(t1, "%d", &p);
                    node->style.margin_top = node->style.margin_bottom = node->style.margin_left = node->style.margin_right = p;
                }
            } else if (ntokens == 2) {
                int tb = 0; sscanf(t1, "%d", &tb);
                node->style.margin_top = node->style.margin_bottom = tb;
                if (strstr(t2, "auto")) {
                    node->style.margin_left_auto = 1;
                    node->style.margin_right_auto = 1;
                    node->style.margin_left = 0;
                    node->style.margin_right = 0;
                } else {
                    int lr = 0; sscanf(t2, "%d", &lr);
                    node->style.margin_left = node->style.margin_right = lr;
                }
            } else if (ntokens == 3) {
                int top = 0, bot = 0;
                sscanf(t1, "%d", &top); sscanf(t3, "%d", &bot);
                node->style.margin_top = top;
                node->style.margin_bottom = bot;
                if (strstr(t2, "auto")) {
                    node->style.margin_left_auto = 1;
                    node->style.margin_right_auto = 1;
                    node->style.margin_left = 0;
                    node->style.margin_right = 0;
                } else {
                    int lr = 0; sscanf(t2, "%d", &lr);
                    node->style.margin_left = node->style.margin_right = lr;
                }
            } else if (ntokens >= 4) {
                int top = 0, bot = 0;
                sscanf(t1, "%d", &top); sscanf(t3, "%d", &bot);
                node->style.margin_top = top;
                node->style.margin_bottom = bot;
                if (strstr(t2, "auto")) { node->style.margin_right_auto = 1; node->style.margin_right = 0; }
                else { int r = 0; sscanf(t2, "%d", &r); node->style.margin_right = r; }
                if (strstr(t4, "auto")) { node->style.margin_left_auto = 1; node->style.margin_left = 0; }
                else { int l = 0; sscanf(t4, "%d", &l); node->style.margin_left = l; }
            }
        }
        else if (strcasecmp(key, "margin-left") == 0) {
            if (strstr(val, "auto")) {
                node->style.margin_left_auto = 1;
                node->style.margin_left = 0;
            } else {
                sscanf(val, "%d", &node->style.margin_left);
            }
        }
        else if (strcasecmp(key, "margin-right") == 0) {
            if (strstr(val, "auto")) {
                node->style.margin_right_auto = 1;
                node->style.margin_right = 0;
            } else {
                sscanf(val, "%d", &node->style.margin_right);
            }
        }
        else if (strcasecmp(key, "margin-top") == 0) {
            sscanf(val, "%d", &node->style.margin_top);
        }
        else if (strcasecmp(key, "margin-bottom") == 0) {
            sscanf(val, "%d", &node->style.margin_bottom);
        }
        else if (strcasecmp(key, "padding") == 0) {
            int p = 0; sscanf(val, "%d", &p);
            node->style.padding_top = node->style.padding_bottom = node->style.padding_left = node->style.padding_right = p;
        }
        else if (strcasecmp(key, "border-width") == 0) sscanf(val, "%d", &node->style.border_width);
        else if (strcasecmp(key, "border-color") == 0) {
            node->style.border_color = css_color(val, node->style.border_color);
        }
        else if (strcasecmp(key, "border-radius") == 0) sscanf(val, "%d", &node->style.border_radius);
        if (i < len && decls[i] == ';') i++;
    }
}

static int css_simple_match(html_node_t *node, const char *sel, int len) {
    char tok[96];
    if (!node || len <= 0 || len >= (int)sizeof(tok)) return 0;
    strncpy(tok, sel, len); tok[len] = 0; css_trim(tok);
    if (!tok[0]) return 0;
    if (tok[0] == '#') return strcmp(node->id, tok + 1) == 0;
    if (tok[0] == '.') return strstr(node->class_name, tok + 1) != NULL;
    int n = 0; while (tok[n] && tok[n] != '.' && tok[n] != '#') n++;
    if (n > 0) {
        char name[32]; if (n >= (int)sizeof(name)) n = sizeof(name)-1;
        strncpy(name, tok, n); name[n] = 0;
        if (tag_from_name(name, n) != node->tag) return 0;
    }
    if (strchr(tok, '#')) { const char *p = strchr(tok, '#'); if (strcmp(node->id, p + 1) != 0) return 0; }
    if (strchr(tok, '.')) { const char *p = strchr(tok, '.'); if (!strstr(node->class_name, p + 1)) return 0; }
    return 1;
}

static int css_selector_match(html_node_t *node, const char *sel, int len) {
    int end = len;
    while (end > 0 && (sel[end-1] == ' ' || sel[end-1] == '\t')) end--;
    int start = end;
    while (start > 0 && sel[start-1] != ' ' && sel[start-1] != '\t') start--;
    if (!css_simple_match(node, sel + start, end - start)) return 0;
    while (start > 0 && (sel[start-1] == ' ' || sel[start-1] == '\t')) start--;
    if (start == 0) return 1;
    html_node_t *p = node->parent;
    int right = start;
    while (right > 0 && sel[right-1] != ' ' && sel[right-1] != '\t') right--;
    while (p) { if (css_simple_match(p, sel, right)) return 1; p = p->parent; }
    return 0;
}

static void css_apply_rules_to_tree(html_node_t *root, const char *css, int len) {
    if (!root || !css || len <= 0) return;
    int i = 0;
    int rules = 0;
    while (i < len) {
        if ((++rules % 16) == 0) gui_pump_events();
        int ss = i; while (i < len && css[i] != '{') i++;
        if (i >= len) break;
        int se = i++; int ds = i; while (i < len && css[i] != '}') i++;
        int de = i++; if (se <= ss) continue;
        for (int n = 0; n < g_node_count; n++) {
            html_node_t *node = html5_engine_get_node(n);
            if (!node) continue;
            int p = ss;
            while (p < se) {
                int q = p; while (q < se && css[q] != ',') q++;
                if (css_selector_match(node, css + p, q - p)) {
                    css_apply_declarations(node, css + ds, de - ds);
                }
                p = q + 1;
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

    /* Text nodes inherit parent styling */
    if (node->tag == HTML_TAG_TEXT_NODE && node->parent) {
        node->style.color = cur_color;
        node->style.text_align = cur_align;
        if (!node->style.font_size || node->style.font_size == 1) {
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
    if (!html || len <= 0) return 0;

    html5_notify_status("Rendering HTML5 structure...");

    html5_engine_reset();

    html_node_t *stack[32];
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
    if (!root) return 0;
    g_root_dom = root;
    stack[stack_top++] = root;

    int i = 0;
    while (i < len && g_node_count < 8192) {
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
            if (stack_top > 1) {
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

            /* Parse attributes until '>' */
            int attr_start = i;
            int is_closed = 0;
            while (i < len && html[i] != '>') {
                if (html[i] == '/' && i < len - 1 && html[i+1] == '>') {
                    is_closed = 1;
                    break;
                }
                i++;
            }
            int attr_len = i - attr_start;
            if (i < len && html[i] == '/') i++;
            if (i < len && html[i] == '>') i++;

            /* Unknown metadata is ignored; link nodes are retained so the
               stylesheet loader can resolve them after DOM construction. */
            if (tag == HTML_TAG_UNKNOWN || tag == HTML_TAG_META) {
                continue;
            }

            if (tag == HTML_TAG_HTML && root && root->tag == HTML_TAG_HTML && stack_top == 1) {
                parse_attributes(root, html + attr_start, attr_len);
                continue;
            }

            html_node_t *node = alloc_node(tag);
            if (!node) break;

            parse_attributes(node, html + attr_start, attr_len);
            if (base_url && node->src[0]) {
                char abs_src[256];
                html5_url_resolve(base_url, node->src, abs_src, sizeof(abs_src));
                node->src = html5_arena_strdup(&g_arena, abs_src);
            }
            if (base_url && node->href[0]) {
                char abs_href[256];
                html5_url_resolve(base_url, node->href, abs_href, sizeof(abs_href));
                node->href = html5_arena_strdup(&g_arena, abs_href);
            }
            apply_default_styles(node);
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
                int body_end = i;
                while (i < len) {
                    if (html[i] == '<' && i + close_len <= len && strncasecmp(html + i, close_prefix, close_len) == 0) {
                        body_end = i;
                        while (i < len && html[i] != '>') i++;
                        if (i < len && html[i] == '>') i++;
                        break;
                    }
                    /* Safety guard: do not allow unclosed <script>/<style> to swallow structural HTML */
                    if (html[i] == '<' && i + 2 < len) {
                        if (strncasecmp(html + i, "<body", 5) == 0 || strncasecmp(html + i, "<html", 5) == 0 ||
                            strncasecmp(html + i, "</head", 6) == 0 || strncasecmp(html + i, "<div", 4) == 0 ||
                            strncasecmp(html + i, "<p", 2) == 0 || strncasecmp(html + i, "<h1", 3) == 0 ||
                            strncasecmp(html + i, "<h2", 3) == 0 || strncasecmp(html + i, "<h3", 3) == 0 ||
                            strncasecmp(html + i, "<table", 6) == 0 || strncasecmp(html + i, "<section", 8) == 0 ||
                            strncasecmp(html + i, "<main", 5) == 0 || strncasecmp(html + i, "<footer", 7) == 0 ||
                            strncasecmp(html + i, "<header", 7) == 0) {
                            body_end = i;
                            break;
                        }
                    }
                    i++;
                }
                if (body_end > body_start) {
                    node->text = html5_arena_strndup(&g_arena, html + body_start, body_end - body_start);
                    node->text_len = body_end - body_start;
                }
                continue;
            }

            /* Push to stack if not self-closing */
            if (!is_self_closing(tag) && !is_closed && stack_top < 31) {
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
                               parent->tag == HTML_TAG_UL || parent->tag == HTML_TAG_OL)) {
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
                    int has_amp = 0;
                    for (int k = 0; k < text_len; k++) {
                        if (html[text_start + k] == '&') { has_amp = 1; break; }
                    }

                    if (has_amp) {
                        char *clean = (char *)html5_arena_alloc(&g_arena, text_len + 1, 1);
                        int ci = 0;
                        if (clean) {
                            for (int k = 0; k < text_len; k++) {
                                if (html[text_start + k] == '&') {
                                    if (k + 5 < text_len && strncmp(html + text_start + k, "&nbsp;", 6) == 0) { clean[ci++] = ' '; k += 5; }
                                    else if (k + 4 < text_len && strncmp(html + text_start + k, "&amp;", 5) == 0) { clean[ci++] = '&'; k += 4; }
                                    else if (k + 3 < text_len && strncmp(html + text_start + k, "&lt;", 4) == 0) { clean[ci++] = '<'; k += 3; }
                                    else if (k + 3 < text_len && strncmp(html + text_start + k, "&gt;", 4) == 0) { clean[ci++] = '>'; k += 3; }
                                    else if (k + 5 < text_len && strncmp(html + text_start + k, "&quot;", 6) == 0) { clean[ci++] = '"'; k += 5; }
                                    else clean[ci++] = html[text_start + k];
                                } else {
                                    clean[ci++] = html[text_start + k];
                                }
                            }
                            clean[ci] = '\0';
                            tnode->text = clean;
                            tnode->text_len = ci;
                        } else {
                            tnode->text = html5_empty_string();
                            tnode->text_len = 0;
                        }
                    } else {
                        tnode->text = html5_arena_strndup(&g_arena, html + text_start, text_len);
                        tnode->text_len = text_len;
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

    /* Apply author CSS after the DOM exists so selectors can inspect parents,
       ids, and classes.  The buffer is intentionally bounded for kernel use. */
    html5_notify_status("Applying CSS stylesheets & cascade...");
    char css_buf[8192];
    int css_len = 0;
    css_collect_styles(css_buf, &css_len, sizeof(css_buf));
    html5_resource_discover_and_fetch(g_root_dom, base_url, css_buf, &css_len, sizeof(css_buf));
    if (css_len > 0) css_apply_rules_to_tree(g_root_dom, css_buf, css_len);

    /* Propagate inherited styles (color, text-align, font properties) */
    propagate_inherited_styles(g_root_dom, g_root_dom->style.color, g_root_dom->style.text_align);

    /* Run scripts */
    html5_notify_status("Executing JavaScript scripts...");
    html5_script_run_dom_scripts(g_root_dom, base_url);

    html5_notify_status("Done");

    return g_node_count;
}

static const char *get_button_label(html_node_t *node, const char *default_lbl) {
    const char *lbl = node->value[0] ? node->value : (node->text[0] ? node->text : NULL);
    if (node->first_child && node->first_child->tag == HTML_TAG_TEXT_NODE && node->first_child->text[0]) {
        lbl = node->first_child->text;
    }
    if (!lbl || !lbl[0] || is_script_leakage(lbl)) {
        if (strstr(node->name, "btnG") || strstr(node->value, "Google") || strstr(node->action, "google")) {
            return "Google Suche";
        }
        if (strstr(node->name, "btnI")) {
            return "Auf gut Glueck!";
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

static int html_is_space_byte(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

uint32_t utf8_decode_next(const char **ptr, const char *end) {
    return text_utf8_decode_next(ptr, end);
}

int text_measure_collapsed_utf8(const char *text, const html_style_t *s, int max_width, int at_line_start, int *out_ends_with_space, int *out_has_nbsp) {
    (void)at_line_start;
    int scale = (s && s->font_size == 2) ? 2 : 1;
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

static void apply_relative_offset(html_node_t *node) {
    if (!node || node->style.position != POS_RELATIVE) return;
    shift_subtree_x(node, node->style.has_left ? node->style.left : 0);
    node->bounds.y += node->style.has_top ? node->style.top : 0;
}

static void apply_out_of_flow_position(html_node_t *node, int viewport_x, int viewport_y, int viewport_w, int viewport_h) {
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

static void linebox_apply_alignment_to_children(html_node_t *parent, int text_align, int content_x, int content_w) {
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
    int out_of_flow = (s->position == POS_ABSOLUTE || s->position == POS_FIXED);
    LineBox current_line = {0};

    /* Block element breaks into a new row */
    if (s->is_block && !out_of_flow) {
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
        box_w = s->width;
    } else {
        box_w = avail_w - (*inout_x - base_x) - s->margin_right;
    }
    if (box_w > avail_w) box_w = avail_w;
    if (box_w < 10) box_w = 10;
    int box_h = s->height;

    int box_x = (out_of_flow ? base_x : *inout_x) + s->margin_left;
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
        int line_h = (s->font_size == 2) ? 24 : 16;
        int rem_w = (base_x + avail_w) - *inout_x;
        box_w = measure_collapsed_text_bytes(node->text, s, rem_w, (*inout_x == base_x));
        if (box_w <= 0) {
            node->bounds = (rect_t){box_x, box_y, 0, 0, 0};
            return cur_y;
        }
        if (box_w > rem_w && *inout_x > base_x) {
            cur_y += *out_row_h;
            *inout_x = base_x;
            *out_row_h = line_h;
            box_x = *inout_x;
            box_y = cur_y;
            rem_w = avail_w;
            box_w = measure_collapsed_text_bytes(node->text, s, rem_w, 1);
        }
        box_h = line_h;
        if (line_h > *out_row_h) *out_row_h = line_h;
        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        linebox_push(&current_line, node);
        current_line.width = box_w;
        current_line.height = box_h;
        linebox_apply_alignment(&current_line, node->parent ? node->parent->style.text_align : 0, avail_w);
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
            img_w = 272;
            img_h = 92;
        }

        if (img_w > avail_w - 20) {
            img_h = (img_h * (avail_w - 20)) / img_w;
            img_w = avail_w - 20;
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
            } else {
                box_w = 160;
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
    else if (node->tag == HTML_TAG_BUTTON) {
        const char *blabel = get_button_label(node, "Button");
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
        box_w = (s->width > 0) ? s->width : (cols * 8 + 16);
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
    else if (node->tag == HTML_TAG_TH || node->tag == HTML_TAG_TD) {
        if (s->width < 0) {
            box_w = (avail_w * (-s->width)) / 100;
        } else if (s->width > 0) {
            box_w = s->width;
        } else {
            box_w = (avail_w > *inout_x - base_x + 160) ? (avail_w - (*inout_x - base_x)) : 160;
            if (box_w > 480) box_w = 480;
        }
        int cell_base_x = box_x + s->padding_left;
        int cell_x = cell_base_x;
        int cell_y = box_y + s->padding_top;
        int cell_row_h = 16;
        html_node_t *ch = node->first_child;
        while (ch) {
            cell_y = layout_node(ch, &cell_x, cell_y, box_w, &cell_row_h, cell_base_x);
            ch = ch->next_sibling;
        }
        if (cell_x > cell_base_x) cell_y += cell_row_h;
        box_h = cell_y - box_y;
        if (box_h < s->height) box_h = s->height;
        if (box_h < 26) box_h = 26;
        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        if (box_h > *out_row_h) *out_row_h = box_h;
        *inout_x += box_w;
        return cur_y;
    }
    else if (node->tag == HTML_TAG_LI) {
        if (*inout_x > base_x) {
            cur_y += *out_row_h;
            *inout_x = base_x;
            *out_row_h = 16;
        }
        *inout_x = base_x + 16;
        box_x = *inout_x;
        box_y = cur_y;
    }

    /* Recurse children for container tags */
    int child_base_x = box_x + s->padding_left;
    int child_x = child_base_x;
    int child_y = box_y + s->padding_top;
    int child_row_h = 16;

    html_node_t *ch = node->first_child;
    while (ch) {
        child_y = layout_node(ch, &child_x, child_y, box_w, &child_row_h, child_base_x);
        ch = ch->next_sibling;
    }

    /* Row centering for text-align: center or <center> */
    if (s->text_align == 1 || node->tag == HTML_TAG_CENTER) {
        html_node_t *line_start = node->first_child;
        while (line_start) {
            if (line_start->style.display_none || line_start->bounds.w == 0 || line_start->tag == HTML_TAG_BR) {
                line_start = line_start->next_sibling;
                continue;
            }
            if (line_start->style.is_block && line_start->bounds.w >= box_w - 10) {
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
            if (line_w > 0 && line_w < box_w) {
                int desired_min_x = child_base_x + (box_w - line_w) / 2;
                int shift = desired_min_x - min_x;
                if (shift > 0) {
                    html_node_t *p = line_start;
                    while (p) {
                        if (!p->style.display_none && p->bounds.w > 0) {
                            shift_subtree_x(p, shift);
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
        child_y += s->padding_bottom;
        box_h = (s->height > 0) ? s->height : (child_y - box_y);
        node->bounds = (rect_t){box_x, box_y, box_w, box_h, 0};
        cur_y = box_y + box_h + s->margin_bottom;
        *inout_x = base_x;
        *out_row_h = 16;
    } else {
        /* Inline container (e.g. <span>, <a>) */
        box_h = child_row_h;
        box_w = child_x - box_x;
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
    if (!g_root_dom) return;

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
}

/* --------------------------------------------------------------------------
 * Rendering Pass — Painting DOM Elements into Framebuffer Viewport
 * -------------------------------------------------------------------------- */
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
                u32 col = (node->parent && node->parent->tag == HTML_TAG_A && !s->has_explicit_color) ? 0x1A0DAB : s->color;
                text_engine_render(vx, vy + 2, node->text, col, s->font_size, &g_browser_default_font_chain, NULL);
                if (s->font_underline || (node->parent && node->parent->tag == HTML_TAG_A)) {
                    int ulen = text_engine_measure(node->text, &g_browser_default_font_chain, s->font_size, 0, NULL, NULL);
                    int uy = (s->font_size == 2) ? (vy + 26) : (vy + 19);
                    fb_rect(vx, uy, ulen, 1, col);
                }
            }
        }
        /* 4. Images */
        else if (node->tag == HTML_TAG_IMG) {
            if (node->img_pixels && node->img_w > 0 && node->img_h > 0) {
                /* Blit decoded RGBA buffer */
                for (int py = 0; py < vh; py++) {
                    int sy = (py * node->img_h) / vh;
                    for (int px = 0; px < vw; px++) {
                        int sx = (px * node->img_w) / vw;
                        u32 p = node->img_pixels[sy * node->img_w + sx];
                        if ((p >> 24) > 10) { /* alpha check */
                            fb_putpixel(vx + px, vy + py, p & 0x00FFFFFF);
                        }
                    }
                }
            } else {
                /* Placeholder Icon */
                fb_round_rect(vx, vy, vw, vh, 4, 0xF1F3F4);
                fb_round_rect_border(vx, vy, vw, vh, 4, 0xDADCE0);
                font_icon(vx + vw/2 - 8, vy + vh/2 - 8, ICON_PHOTO, 0x1A73E8);
            }
        }
        /* 5. Video Player Context */
        else if (node->tag == HTML_TAG_VIDEO && node->video_ctx) {
            html5_media_render_video((html5_video_ctx_t*)node->video_ctx, vx, vy, vw, vh);
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
        /* 7. Form Button */
        else if (node->tag == HTML_TAG_BUTTON) {
            u32 bg = node->disabled ? 0xE8EAED : (s->has_bg ? s->bg_color : 0xF8F9FA);
            u32 bcol = s->border_color ? s->border_color : 0xDADCE0;
            int rad = s->border_radius > 0 ? s->border_radius : 4;
            fb_round_rect(vx, vy, vw, vh, rad, bg);
            fb_round_rect_border(vx, vy, vw, vh, rad, bcol);
            if (node->is_focused) {
                fb_round_rect_border(vx - 1, vy - 1, vw + 2, vh + 2, rad + 1, 0x1A73E8);
            }
            const char *label = get_button_label(node, "Button");
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
            fb_rect(vx, vy, vw, vh, s->bg_color);
            fb_border(vx, vy, vw, vh, 0xDFE1E5);
        }
        /* 9. List Item Bullet */
        else if (node->tag == HTML_TAG_LI) {
            fb_round_rect(vx - 12, vy + 6, 4, 4, 2, 0x1A73E8);
        }
    }

    /* Recurse visible children only. Metadata, styles, scripts, and titles
       participate in parsing but must never appear as page text. */
    if (!html5_node_is_non_visual(node) &&
        node->tag != HTML_TAG_BUTTON && node->tag != HTML_TAG_TEXTAREA) {
        html_node_t *ch = node->first_child;
        while (ch) {
            render_node(ch, content_x, content_y, content_w, content_h, scroll_y);
            ch = ch->next_sibling;
        }
    }
}

void html5_render_document(int content_x, int content_y, int content_w, int content_h, int scroll_y) {
    if (!g_root_dom) return;

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
        fb_rect(sb_x, content_y, 8, content_h, 0x1C1C24);

        int thumb_h = (content_h * content_h) / g_doc_height;
        if (thumb_h < 24) thumb_h = 24;
        int max_scroll = g_doc_height - content_h;
        int thumb_y = content_y + ((content_h - thumb_h) * scroll_y) / (max_scroll > 0 ? max_scroll : 1);

        fb_round_rect(sb_x + 1, thumb_y, 6, thumb_h, 3, 0x484856);
    }
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
    const char *action = form->action[0] ? form->action : "about:form";
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
        if (g_focused_input->tag == HTML_TAG_TEXTAREA) {
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
            /* Input or Button submit */
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
        case HTML_TAG_IMG:        return "img";
        case HTML_TAG_VIDEO:      return "video";
        case HTML_TAG_AUDIO:      return "audio";
        case HTML_TAG_CANVAS:     return "canvas";
        case HTML_TAG_CENTER:     return "center";
        case HTML_TAG_STYLE:      return "style";
        case HTML_TAG_SCRIPT:     return "script";
        case HTML_TAG_LINK:       return "link";
        case HTML_TAG_META:       return "meta";
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
