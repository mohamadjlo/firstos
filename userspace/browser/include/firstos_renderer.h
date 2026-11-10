#ifndef _FIRSTOS_RENDERER_H
#define _FIRSTOS_RENDERER_H

#include <stddef.h>
#include <stdint.h>
#include "../../../include/firstos/uapi/browser_ipc.h"
#include "../../../include/firstos/accessibility/ax_tree.h"
#include "../../../html5_arena.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline size_t u_strlen(const char *s) { size_t l = 0; while (s && s[l]) l++; return l; }
static inline char *u_strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
static inline int u_strcmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && (*a == *b)) { a++; b++; }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}
static inline int u_strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) break;
    }
    return 0;
}
static inline char *u_strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    while (i < n && s && s[i]) { d[i] = s[i]; i++; }
    while (i < n) { d[i] = '\0'; i++; }
    return d;
}
static inline void u_memcpy(void *dst, const void *src, unsigned int n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    for (unsigned int i = 0; i < n; i++) d[i] = s[i];
}
static inline char *u_strchr(const char *s, int c) {
    if (!s) return NULL;
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    if ((char)c == '\0') return (char *)s;
    return NULL;
}
static inline char *u_strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && (*h == *n)) { h++; n++; }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}
#ifndef strlen
#define strlen u_strlen
#endif
#ifndef strcpy
#define strcpy u_strcpy
#endif
#ifndef strcmp
#define strcmp u_strcmp
#endif
#ifndef strncmp
#define strncmp u_strncmp
#endif
#ifndef strncpy
#define strncpy u_strncpy
#endif
#ifndef memcpy
#define memcpy u_memcpy
#endif
#ifndef strchr
#define strchr u_strchr
#endif
#ifndef strstr
#define strstr u_strstr
#endif

typedef enum {
    RENDER_OK = 0,
    RENDER_BAD_HTML = -1,
    RENDER_OUT_OF_MEMORY = -2,
    RENDER_TOO_DEEP = -3,
    RENDER_TOO_LARGE = -4,
    RENDER_UNSUPPORTED = -5,
    RENDER_OUT_OF_BUFFER = -6
} firstos_render_status_t;

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
    HTML_TAG_H1,
    HTML_TAG_H2,
    HTML_TAG_H3,
    HTML_TAG_P,
    HTML_TAG_HR,
    HTML_TAG_BR,
    HTML_TAG_A,
    HTML_TAG_SPAN,
    HTML_TAG_B,
    HTML_TAG_STRONG,
    HTML_TAG_I,
    HTML_TAG_EM,
    HTML_TAG_U,
    HTML_TAG_STYLE,
    HTML_TAG_SCRIPT,
    HTML_TAG_LINK,
    HTML_TAG_IMG,
    HTML_TAG_FORM,
    HTML_TAG_INPUT,
    HTML_TAG_BUTTON,
    HTML_TAG_TEXTAREA,
    HTML_TAG_SELECT,
    HTML_TAG_OPTION,
    HTML_TAG_LABEL,
    HTML_TAG_IFRAME,
    HTML_TAG_AUDIO,
    HTML_TAG_VIDEO,
    HTML_TAG_TEXT_NODE
} html_tag_t;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} rect_t;

typedef enum {
    CSS_DISPLAY_BLOCK = 0,
    CSS_DISPLAY_INLINE,
    CSS_DISPLAY_INLINE_BLOCK,
    CSS_DISPLAY_NONE,
    CSS_DISPLAY_FLEX,
    CSS_DISPLAY_GRID
} css_display_mode_t;

typedef enum {
    CSS_FLEX_DIR_ROW = 0,
    CSS_FLEX_DIR_ROW_REVERSE,
    CSS_FLEX_DIR_COLUMN,
    CSS_FLEX_DIR_COLUMN_REVERSE
} css_flex_direction_t;

typedef enum {
    CSS_FLEX_NOWRAP = 0,
    CSS_FLEX_WRAP
} css_flex_wrap_t;

typedef enum {
    CSS_JUSTIFY_START = 0,
    CSS_JUSTIFY_CENTER,
    CSS_JUSTIFY_END,
    CSS_JUSTIFY_SPACE_BETWEEN,
    CSS_JUSTIFY_SPACE_AROUND
} css_justify_content_t;

typedef enum {
    CSS_ALIGN_START = 0,
    CSS_ALIGN_CENTER,
    CSS_ALIGN_END,
    CSS_ALIGN_STRETCH
} css_align_items_t;

#define FIRSTOS_MAX_CSS_VARS_PER_NODE 8

typedef struct {
    char name[32];
    char value[48];
} css_var_entry_t;

typedef struct {
    css_var_entry_t entries[FIRSTOS_MAX_CSS_VARS_PER_NODE];
    uint8_t count;
} css_var_table_t;

typedef struct html_style {
    uint32_t color;
    uint32_t bg_color;
    uint8_t  has_bg;
    uint8_t  has_explicit_color;
    uint8_t  has_explicit_align;
    uint8_t  font_size;    /* 1 = 14px, 2 = 20px, 3 = 24px */
    uint8_t  font_bold;
    uint8_t  font_italic;
    uint8_t  font_underline;
    uint8_t  is_block;
    uint8_t  is_inline_block;
    uint8_t  display_none;
    uint8_t  text_align;   /* 0 = left, 1 = center, 2 = right */
    uint8_t  position;     /* 0=static, 1=relative */
    uint8_t  margin_left_auto;
    uint8_t  margin_right_auto;
    int      margin_top;
    int      margin_bottom;
    int      margin_left;
    int      margin_right;
    int      padding_top;
    int      padding_bottom;
    int      padding_left;
    int      padding_right;
    int      border_width;
    uint32_t border_color;
    int      width;        /* <= 0 means auto */
    int      height;       /* <= 0 means auto */
    uint8_t  box_sizing;   /* 0 = content-box, 1 = border-box */
    uint8_t  visibility_hidden;

    /* Phase 6C: Modern CSS Additions */
    uint8_t  display_mode;         /* css_display_mode_t */
    uint8_t  flex_direction;       /* css_flex_direction_t */
    uint8_t  flex_wrap;            /* css_flex_wrap_t */
    uint8_t  justify_content;      /* css_justify_content_t */
    uint8_t  align_items;          /* css_align_items_t */
    int16_t  gap_row;              /* row-gap in px */
    int16_t  gap_col;              /* column-gap in px */
    uint8_t  grid_columns;         /* Number of columns (1..8) */
    uint8_t  flex_grow;            /* Flex grow factor for children */
} html_style_t;

typedef struct html_node {
    int id_num;
    html_tag_t tag;
    const char *id;
    const char *class_name;
    const char *href;
    const char *src;
    const char *rel;
    const char *download;  /* Phase 6A.1: <a download="..."> filename suggestion */
    int req_width;
    int req_height;
    uint32_t image_id;     /* Phase 4D: Decoded image ID in Host store */
    uint8_t  image_broken; /* Phase 4D: 1 if decoding or loading failed */
    const char *text;
    int text_len;

    /* Phase 4E: Form Controls & Interaction */
    const char *name;
    char        value[128];
    int         value_len;
    const char *placeholder;
    const char *input_type; /* "text", "password", "checkbox", "submit", "button" */
    const char *action;
    const char *method;     /* "GET", "POST" */
    uint8_t     checked;
    uint8_t     disabled;
    uint8_t     readonly_state;
    uint8_t     is_focused;
    int         cursor_pos;
    const char *onclick;
    /* Phase 5C: Iframe attributes */
    uint32_t    frame_id;
    uint32_t    frame_generation;
    const char *frame_title;

    /* Phase 6B: HTML5 Audio & Video attributes */
    uint8_t     media_controls;
    uint8_t     media_autoplay;
    uint8_t     media_loop;
    uint8_t     media_muted;
    uint8_t     media_playing;
    uint16_t    media_volume;      /* 0 - 100 */
    uint32_t    media_cur_time_sec;
    uint32_t    media_duration_sec;
    const char *media_poster;

    /* Phase 6C: CSS Custom Properties Dictionary */
    css_var_table_t var_table;

    html_style_t style;
    rect_t bounds;        /* Layout bounds relative to document */

    struct html_node *parent;
    struct html_node *first_child;
    struct html_node *next_sibling;
} html_node_t;

typedef struct {
    uint32_t request_id;
    firstos_resource_type_t type;
    const char *url;
    int status; /* 0=pending, 1=loaded, -1=failed */
    html_node_t *node; /* Phase 4D: Associated DOM node */
} firstos_pending_resource_t;

/* Phase 4E: Focus State */
typedef struct {
    int      focused_node_id;
    uint32_t focus_generation;
} firstos_focus_state_t;

/* Phase 4E: DOM Event Types & Structures */
typedef enum {
    DOM_EVENT_NONE = 0,
    DOM_EVENT_CLICK,
    DOM_EVENT_KEYDOWN,
    DOM_EVENT_KEYUP,
    DOM_EVENT_INPUT,
    DOM_EVENT_CHANGE,
    DOM_EVENT_SUBMIT,
    DOM_EVENT_FOCUS,
    DOM_EVENT_BLUR
} firstos_dom_event_type_t;

typedef struct {
    uint32_t type;
    int      target_node_id;
    int      current_node_id;
    uint32_t phase; /* 1=capture, 2=target, 3=bubble */
    uint8_t  default_prevented;
    uint8_t  propagation_stopped;
} firstos_dom_event_t;

typedef struct {
    html5_arena_t arena;
    html5_node_slab_t node_slab;
    html_node_t *root_dom;
    char doc_title[128];
    int doc_height;
    int node_count;

    /* Viewport & Scrolling */
    uint32_t viewport_w;
    uint32_t viewport_h;
    int32_t scroll_x;
    int32_t scroll_y;
    uint32_t generation;
    uint32_t navigation_id;
    uint32_t frame_count;
    uint8_t  needs_full_repaint;
    rect_t   damage_rect;

    /* Subresource discovery */
    firstos_pending_resource_t pending_res[16];
    int pending_res_count;

    /* Phase 4E: Focus Management */
    firstos_focus_state_t focus_state;

    /* Phase 5: Committed Origin Security Context */
    char origin[128];
} firstos_renderer_context_t;

/* Public Userspace Renderer Lifecycle */
void firstos_renderer_init(firstos_renderer_context_t *ctx);
void firstos_renderer_reset(firstos_renderer_context_t *ctx);
void firstos_renderer_destroy(firstos_renderer_context_t *ctx);
void firstos_renderer_set_origin(firstos_renderer_context_t *ctx, const char *origin);
const char *firstos_renderer_get_origin(const firstos_renderer_context_t *ctx);

/* Document Construction & Pipeline */
int  firstos_renderer_parse_html(firstos_renderer_context_t *ctx, const char *html, int len);
void firstos_renderer_apply_styles(firstos_renderer_context_t *ctx);
int  firstos_renderer_add_stylesheet(firstos_renderer_context_t *ctx, const char *css_text, int len);
void firstos_renderer_layout(firstos_renderer_context_t *ctx, uint32_t view_w, uint32_t view_h);
int  firstos_renderer_generate_commands(firstos_renderer_context_t *ctx,
                                        firstos_render_command_t *out_cmds,
                                        uint32_t max_cmds,
                                        char *out_strings,
                                        uint32_t str_capacity,
                                        uint32_t *out_num_cmds);

/* Interactive Events & Focus */
int  firstos_renderer_handle_scroll(firstos_renderer_context_t *ctx, int delta_y);
int  firstos_renderer_handle_click(firstos_renderer_context_t *ctx, int click_x, int click_y,
                                   char *out_nav_url, int max_url,
                                   char *out_submit_action, int max_action,
                                   char *out_query, int max_query);
void firstos_renderer_handle_resize(firstos_renderer_context_t *ctx, uint32_t new_w, uint32_t new_h);
int  firstos_renderer_set_focus(firstos_renderer_context_t *ctx, html_node_t *node);
int  firstos_renderer_focus_next(firstos_renderer_context_t *ctx, int reverse);
html_node_t *firstos_renderer_get_focused(firstos_renderer_context_t *ctx);
int  firstos_renderer_handle_key(firstos_renderer_context_t *ctx, int key, char ascii,
                                 char *out_submit_action, int max_action,
                                 char *out_query, int max_query);

/* Form Controls */
int  firstos_form_handle_char(firstos_renderer_context_t *ctx, char c);
int  firstos_form_handle_backspace(firstos_renderer_context_t *ctx);
int  firstos_form_toggle_checkbox(html_node_t *node);
int  firstos_form_collect_query(firstos_renderer_context_t *ctx, html_node_t *form, char *out_buf, int max_len);
html_node_t *firstos_form_find_parent_form(html_node_t *node);

/* DOM Event Dispatch */
int  firstos_dom_dispatch_event(firstos_renderer_context_t *ctx, html_node_t *target, firstos_dom_event_t *ev);

/* Safe Bounded JavaScript Engine */
void firstos_js_init(void);
void firstos_js_reset(void);
int  firstos_js_eval(firstos_renderer_context_t *ctx, const char *script_code);
int  firstos_js_dispatch_event(firstos_renderer_context_t *ctx, html_node_t *node, const char *event_name);
int  firstos_js_run_dom_scripts(firstos_renderer_context_t *ctx);
int  firstos_js_get_instruction_count(void);
int  firstos_js_get_call_depth(void);

/* Phase 5C: postMessage bridge (Host remains the sole origin authority) */
int  firstos_js_take_outbound_message(int *out_target_kind, char *out_data, int data_max,
                                      char *out_target_origin, int origin_max);
int  firstos_js_outbound_dropped(void);
int  firstos_js_deliver_message(firstos_renderer_context_t *ctx, const char *data, const char *origin);

/* Node Queries */
html_node_t *firstos_renderer_get_element_by_id(firstos_renderer_context_t *ctx, const char *id);
html_node_t *firstos_renderer_get_element_by_tag(firstos_renderer_context_t *ctx, html_tag_t tag);
html_node_t *firstos_renderer_get_element_by_class(firstos_renderer_context_t *ctx, const char *cls);

/* Phase 6A.3: Accessibility Tree Builder */
void firstos_ax_tree_init(firstos_ax_tree_t *tree, uint32_t tab_id, uint32_t frame_id);
const firstos_ax_node_t *firstos_ax_tree_get_node(const firstos_ax_tree_t *tree, uint32_t ax_id);
int  firstos_ax_tree_build(firstos_renderer_context_t *ctx, firstos_ax_tree_t *out_tree);
int  firstos_ax_tree_dump(const firstos_ax_tree_t *tree, char *out_buf, int max_len);

#ifdef __cplusplus
}
#endif

#endif /* _FIRSTOS_RENDERER_H */
