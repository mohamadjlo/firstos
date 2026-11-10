#ifndef HTML5_ENGINE_H
#define HTML5_ENGINE_H

#include "io.h"
#include "text_engine.h"
#include "gui_internal.h"
#include "stdio.h"
#include "string.h"
#include "html5_arena.h"
#include <stdint.h>

static inline char h_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static inline int h_abs(int x) {
    return (x < 0) ? -x : x;
}

static inline int h_strcmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}


static inline int h_strcasecmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && (h_tolower(*a) == h_tolower(*b))) { a++; b++; }
    return (unsigned char)h_tolower(*a) - (unsigned char)h_tolower(*b);
}

static inline int h_strncasecmp(const char *a, const char *b, int n) {
    if (n <= 0) return 0;
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (int i = 0; i < n; i++) {
        if (h_tolower(a[i]) != h_tolower(b[i]))
            return (unsigned char)h_tolower(a[i]) - (unsigned char)h_tolower(b[i]);
        if (!a[i]) break;
    }
    return 0;
}

static inline char *h_strcpy(char *dst, const char *src) {
    if (!dst) return dst;
    char *d = dst;
    if (!src) { *d = 0; return dst; }
    while ((*d++ = *src++));
    return dst;
}

static inline char *h_strncat(char *dst, const char *src, int n) {
    if (!dst || n <= 0) return dst;
    int dlen = strlen(dst);
    int i = 0;
    if (src) {
        for (; i < n && src[i]; i++) {
            dst[dlen + i] = src[i];
        }
    }
    dst[dlen + i] = 0;
    return dst;
}

static inline const char *h_strrchr(const char *s, int c) {
    if (!s) return NULL;
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if ((char)c == 0) return (char *)s;
    return (char *)last;
}

static inline char *h_strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack, *n = needle;
            while (*h && *n && *h == *n) { h++; n++; }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

#define strcmp h_strcmp
#define strcasecmp h_strcasecmp
#define strncasecmp h_strncasecmp
#define strcpy h_strcpy
#define strrchr h_strrchr
#define strncat h_strncat
#define strstr h_strstr
#define abs h_abs

#define MAX_DOM_NODES 512
#define MAX_ATTR_LEN  256
#define MAX_TEXT_LEN  1024

typedef enum {
    HTML_TAG_UNKNOWN = 0,
    HTML_TAG_HTML,
    HTML_TAG_HEAD,
    HTML_TAG_BODY,
    HTML_TAG_TITLE,
    HTML_TAG_DIV,
    HTML_TAG_SECTION,
    HTML_TAG_ARTICLE,
    HTML_TAG_HEADER,
    HTML_TAG_FOOTER,
    HTML_TAG_NAV,
    HTML_TAG_MAIN,
    HTML_TAG_ASIDE,
    HTML_TAG_H1,
    HTML_TAG_H2,
    HTML_TAG_H3,
    HTML_TAG_H4,
    HTML_TAG_H5,
    HTML_TAG_H6,
    HTML_TAG_P,
    HTML_TAG_BLOCKQUOTE,
    HTML_TAG_PRE,
    HTML_TAG_CODE,
    HTML_TAG_HR,
    HTML_TAG_BR,
    HTML_TAG_A,
    HTML_TAG_SPAN,
    HTML_TAG_B,
    HTML_TAG_STRONG,
    HTML_TAG_I,
    HTML_TAG_EM,
    HTML_TAG_U,
    HTML_TAG_MARK,
    HTML_TAG_SMALL,
    HTML_TAG_UL,
    HTML_TAG_OL,
    HTML_TAG_LI,
    HTML_TAG_TABLE,
    HTML_TAG_THEAD,
    HTML_TAG_TBODY,
    HTML_TAG_TR,
    HTML_TAG_TH,
    HTML_TAG_TD,
    HTML_TAG_FORM,
    HTML_TAG_INPUT,
    HTML_TAG_BUTTON,
    HTML_TAG_TEXTAREA,
    HTML_TAG_LABEL,
    HTML_TAG_IMG,
    HTML_TAG_VIDEO,
    HTML_TAG_AUDIO,
    HTML_TAG_CANVAS,
    HTML_TAG_CENTER,
    HTML_TAG_STYLE,
    HTML_TAG_SCRIPT,
    HTML_TAG_LINK,
    HTML_TAG_META,
    HTML_TAG_NOSCRIPT,
    HTML_TAG_IFRAME,
    HTML_TAG_FIGURE,
    HTML_TAG_FIGCAPTION,
    HTML_TAG_DETAILS,
    HTML_TAG_SUMMARY,
    HTML_TAG_TIME,
    HTML_TAG_SVG,
    HTML_TAG_TEXT_NODE
} html_tag_t;

typedef struct html_style html_style_t;
uint32_t utf8_decode_next(const char **ptr, const char *end);
int text_measure_collapsed_utf8(const char *text, const html_style_t *s, int max_width, int at_line_start, int *out_ends_with_space, int *out_has_nbsp);

/* CSS Box & Typography Computed Styles */
typedef struct html_style {
    u32 color;
    u32 bg_color;
    u8  has_bg;
    u8  has_explicit_color;
    u8  has_explicit_align;
    u8  font_size;    /* 1 = normal (14px), 2 = heading (20px), 3 = large (24px) */
    u8  font_bold;
    u8  font_italic;
    u8  font_underline;
    u8  is_block;
    u8  is_inline_block;
    u8  display_none;
    u8  text_align;   /* 0 = left, 1 = center, 2 = right */
    u8  position;     /* 0=static, 1=relative, 2=absolute, 3=fixed */
    u8  margin_left_auto;
    u8  margin_right_auto;
    int margin_top;
    int margin_bottom;
    int margin_left;
    int margin_right;
    int padding_top;
    int padding_bottom;
    int padding_left;
    int padding_right;
    int border_width;
    u32 border_color;
    int border_radius;
    int width;        /* <= 0 means auto / flow */
    int height;       /* <= 0 means auto */
    u8  box_sizing;   /* 0 = content-box (default), 1 = border-box */
    int min_width;    /* <= 0 means none */
    int max_width;    /* <= 0 means none, negative means percentage e.g. -100 for 100% */
    int min_height;   /* <= 0 means none */
    int max_height;   /* <= 0 means none */
    u8  is_flex;      /* 1 if display: flex or inline-flex */
    u8  flex_direction;  /* 0 = row, 1 = column, 2 = row-reverse, 3 = column-reverse */
    u8  flex_wrap;       /* 0 = nowrap, 1 = wrap, 2 = wrap-reverse */
    u8  justify_content; /* 0 = flex-start, 1 = center, 2 = flex-end, 3 = space-between, 4 = space-around, 5 = space-evenly */
    u8  align_items;     /* 0 = stretch, 1 = center, 2 = flex-end, 3 = flex-start, 4 = baseline */
    u8  align_self;      /* 0 = auto/inherit, 1 = flex-start, 2 = center, 3 = flex-end, 4 = stretch, 5 = baseline */
    u8  align_content;   /* 0 = stretch, 1 = flex-start, 2 = center, 3 = flex-end, 4 = space-between, 5 = space-around */
    int flex_grow;       /* 0 = none, integer scale factor e.g. 1, 2 */
    int flex_shrink;     /* default 1, 0 = no shrink, scaled factor */
    int flex_basis;      /* <= -9999 means auto, >= 0 px, -1..-100 percentage */
    u8  has_flex_grow;
    u8  has_flex_shrink;
    u8  has_flex_basis;
    int gap;             /* gap in px (applies to row & column unless overridden) */
    int row_gap;         /* row-gap in px, -1 means fallback to gap */
    int column_gap;      /* column-gap in px, -1 means fallback to gap */
    int left;
    int right;
    int top;
    int bottom;
    u8  has_left;
    u8  has_right;
    u8  has_top;
    u8  has_bottom;
    u8  list_style_none;
    u8  visibility_hidden;
} html_style_t;

typedef enum {
    POS_STATIC = 0,
    POS_RELATIVE,
    POS_ABSOLUTE,
    POS_FIXED
} position_type_t;

typedef struct LineBox {
    struct html_node *items[64];
    int item_count;
    int width;
    int height;
    int baseline;
} LineBox;

/* DOM Node Structure */
typedef struct html_node {
    int id_num;
    html_tag_t tag;
    const char *id;
    const char *class_name;
    const char *src;
    const char *href;
    const char *rel;
    const char *name;
    char *value;          /* Writable pointer for form controls, arena-backed */
    int value_cap;
    const char *placeholder;
    const char *input_type;
    const char *action;
    const char *method;
    const char *onclick;
    const char *for_id;
    const char *text;      /* Pointer to zero-copy or arena string */
    int text_len;

    /* Attributes for Form Controls */
    int  rows;             /* <textarea rows="N"> */
    int  cols;             /* <textarea cols="N"> */
    int  size;             /* <input size="N"> */
    u8   disabled;         /* 1 if control is disabled */
    u8   read_only;        /* 1 if control is readonly */

    /* Incremental Reflow Dirty Flags */
    u8   is_layout_dirty : 1;
    u8   subtree_layout_dirty : 1;
    u8   is_paint_dirty : 1;
    u8   script_executed : 1;

    html_style_t style;
    rect_t bounds;        /* Calculated layout bounds (x, y, w, h) */

    /* Image Asset */
    u32 *img_pixels;      /* Decoded 32-bit RGBA pixels */
    int img_w;
    int img_h;

    /* Video Context (for <video>) */
    void *video_ctx;

    /* Form Input State */
    u8 is_focused;
    int cursor_pos;

    /* Tree Hierarchy */
    struct html_node *parent;
    struct html_node *first_child;
    struct html_node *next_sibling;
} html_node_t;

/* Engine Lifecycle & API */
void html5_engine_init(void);
void html5_engine_init_with_allocator(const html5_allocator_t *allocator);
void html5_engine_destroy(void);
void html5_engine_reset(void);
html5_arena_t *html5_engine_get_arena(void);
html5_node_slab_t *html5_engine_get_node_slab(void);
int  html5_engine_get_node_count(void);
html_node_t *html5_engine_get_node(int index);

/* Node Text, Value & Dirty Reflow API */
void html5_node_set_text(html_node_t *node, const char *text);
void html5_node_set_text_len(html_node_t *node, const char *text, int len);
void html5_node_append_text(html_node_t *node, const char *text);
void html5_node_set_value(html_node_t *node, const char *value);
void html5_node_mark_dirty(html_node_t *node);
void html5_node_clear_dirty(html_node_t *node);

int  html5_parse_document(const char *html, int len, const char *base_url);
void html5_layout_document(int content_w, int content_h);
void html5_render_document(int content_x, int content_y, int content_w, int content_h, int scroll_y);
int  html5_handle_click(int content_x, int content_y, int scroll_y, int mx, int my, char *out_nav_url, int max_len);
int  html5_handle_key(unsigned char key, char ascii);
int  html5_focus_next_control(int direction);
void html5_tick(void);
const char *html5_take_navigation_request(void);
void html5_request_navigation(const char *url);
int  html5_get_hovered_link(int content_x, int content_y, int scroll_y, int mx, int my, char *out_url, int max_len);
void html5_set_status_callback(void (*cb)(const char *status));

int  html5_get_document_height(void);
const char *html5_get_document_title(void);
void html5_set_document_title(const char *title);
html_node_t *html5_get_element_by_id(const char *id);
html_node_t *html5_get_element_by_tag(html_tag_t tag);
html_node_t *html5_get_element_by_class(const char *cls);
html_node_t *html5_get_root_dom(void);
html_node_t *html5_find_node_at(int content_x, int content_y, int scroll_y, int mx, int my);
const char  *html5_tag_name(html_tag_t tag);
void         html5_get_node_breadcrumb(html_node_t *node, char *buf, int max_len);
int          html5_node_is_non_visual(const html_node_t *node);
int          html5_node_is_inside_body(const html_node_t *node);
int          html5_node_is_renderable_text(const html_node_t *node);

#endif /* HTML5_ENGINE_H */
