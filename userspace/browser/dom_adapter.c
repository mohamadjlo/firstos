#include "include/firstos_renderer.h"

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(__KERNEL__) && !defined(_FIRSTOS_USERSPACE)
extern void *malloc(size_t size);
extern void  free(void *ptr);
static void *u_alloc(size_t size, void *ud) { (void)ud; return malloc(size); }
static void  u_free(void *ptr, void *ud) { (void)ud; free(ptr); }
#else
#include "../../../userspace/include/firstos/syscall.h"
static void *u_alloc(size_t size, void *ud) { (void)ud; return firstos_malloc((unsigned int)size); }
static void  u_free(void *ptr, void *ud) { (void)ud; firstos_free(ptr); }
#endif

static const html5_allocator_t s_allocator = { u_alloc, u_free, NULL };

void firstos_renderer_init(firstos_renderer_context_t *ctx) {
    if (!ctx) return;
    html5_arena_init(&ctx->arena, &s_allocator, 32768);
    html5_node_slab_init(&ctx->node_slab, &s_allocator, sizeof(html_node_t));
    ctx->root_dom = NULL;
    ctx->origin[0] = '\0';
    ctx->doc_title[0] = '\0';
    ctx->doc_height = 0;
    ctx->node_count = 0;
    ctx->viewport_w = 800;
    ctx->viewport_h = 600;
    ctx->scroll_x = 0;
    ctx->scroll_y = 0;
    ctx->generation = 1;
    ctx->navigation_id = 1;
    ctx->frame_count = 0;
    ctx->needs_full_repaint = 1;
    ctx->damage_rect = (rect_t){0, 0, 800, 600};
    ctx->pending_res_count = 0;
    ctx->focus_state.focused_node_id = 0;
    ctx->focus_state.focus_generation = 1;
}

void firstos_renderer_reset(firstos_renderer_context_t *ctx) {
    if (!ctx) return;
    html5_arena_reset(&ctx->arena);
    html5_node_slab_reset(&ctx->node_slab);
    ctx->root_dom = NULL;
    ctx->origin[0] = '\0';
    ctx->doc_title[0] = '\0';
    ctx->doc_height = 0;
    ctx->node_count = 0;
    ctx->scroll_x = 0;
    ctx->scroll_y = 0;
    ctx->generation++;
    ctx->frame_count = 0;
    ctx->needs_full_repaint = 1;
    ctx->damage_rect = (rect_t){0, 0, (int)ctx->viewport_w, (int)ctx->viewport_h};
    ctx->pending_res_count = 0;
    ctx->focus_state.focused_node_id = 0;
    ctx->focus_state.focus_generation++;
}

void firstos_renderer_destroy(firstos_renderer_context_t *ctx) {
    if (!ctx) return;
    html5_arena_destroy(&ctx->arena);
    html5_node_slab_destroy(&ctx->node_slab);
    ctx->root_dom = NULL;
    ctx->node_count = 0;
    ctx->pending_res_count = 0;
}

html_node_t *firstos_dom_alloc_node(firstos_renderer_context_t *ctx, html_tag_t tag) {
    if (!ctx || ctx->node_count >= 512) return NULL;
    html_node_t *node = (html_node_t *)html5_node_slab_alloc(&ctx->node_slab);
    if (!node) return NULL;

    node->id_num = ++ctx->node_count;
    node->tag = tag;
    node->id = NULL;
    node->class_name = NULL;
    node->href = NULL;
    node->src = NULL;
    node->rel = NULL;
    node->req_width = 0;
    node->req_height = 0;
    node->image_id = 0;
    node->image_broken = 0;
    node->text = NULL;
    node->text_len = 0;

    /* Phase 4E: Form Controls & Interaction */
    node->name = NULL;
    node->value[0] = '\0';
    node->value_len = 0;
    node->placeholder = NULL;
    node->input_type = (tag == HTML_TAG_INPUT) ? "text" : NULL;
    node->action = NULL;
    node->method = "GET";
    node->checked = 0;
    node->disabled = 0;
    node->readonly_state = 0;
    node->is_focused = 0;
    node->cursor_pos = 0;
    node->onclick = NULL;
    node->frame_id = 0;
    node->frame_generation = 0;
    node->frame_title = NULL;

    /* Initialize default style */
    node->style.color = (tag == HTML_TAG_A) ? 0x1A0DAB : 0x202124;        /* Web standard dark text & blue links */
    node->style.bg_color = 0xFFFFFF;
    node->style.has_bg = 0;
    node->style.has_explicit_color = (tag == HTML_TAG_A) ? 1 : 0;
    node->style.has_explicit_align = 0;
    node->style.font_size = (tag == HTML_TAG_H1) ? 3 : (tag == HTML_TAG_H2) ? 2 : (tag == HTML_TAG_H3) ? 2 : 1;
    node->style.font_bold = (tag == HTML_TAG_H1 || tag == HTML_TAG_H2 || tag == HTML_TAG_H3 || tag == HTML_TAG_STRONG || tag == HTML_TAG_B) ? 1 : 0;
    node->style.font_italic = (tag == HTML_TAG_EM || tag == HTML_TAG_I) ? 1 : 0;
    node->style.font_underline = (tag == HTML_TAG_A || tag == HTML_TAG_U) ? 1 : 0;
    node->style.is_block = (tag == HTML_TAG_HTML || tag == HTML_TAG_BODY || tag == HTML_TAG_DIV ||
                            tag == HTML_TAG_SECTION || tag == HTML_TAG_ARTICLE || tag == HTML_TAG_HEADER ||
                            tag == HTML_TAG_FOOTER || tag == HTML_TAG_NAV || tag == HTML_TAG_H1 ||
                            tag == HTML_TAG_H2 || tag == HTML_TAG_H3 || tag == HTML_TAG_P ||
                            tag == HTML_TAG_HR || tag == HTML_TAG_FORM) ? 1 : 0;
    node->style.is_inline_block = (tag == HTML_TAG_INPUT || tag == HTML_TAG_BUTTON ||
                                   tag == HTML_TAG_SELECT || tag == HTML_TAG_TEXTAREA || tag == HTML_TAG_IFRAME) ? 1 : 0;
    node->style.display_none = (tag == HTML_TAG_HEAD || tag == HTML_TAG_TITLE || tag == HTML_TAG_STYLE || tag == HTML_TAG_SCRIPT) ? 1 : 0;
    node->style.text_align = 0;
    node->style.position = 0;
    node->style.margin_left_auto = 0;
    node->style.margin_right_auto = 0;
    node->style.margin_top = (tag == HTML_TAG_H1) ? 16 : (tag == HTML_TAG_H2) ? 12 : (tag == HTML_TAG_H3) ? 8 : (tag == HTML_TAG_P) ? 8 : 0;
    node->style.margin_bottom = (tag == HTML_TAG_H1) ? 16 : (tag == HTML_TAG_H2) ? 12 : (tag == HTML_TAG_H3) ? 8 : (tag == HTML_TAG_P) ? 8 : 0;
    node->style.margin_left = 0;
    node->style.margin_right = 0;
    node->style.padding_top = 0;
    node->style.padding_bottom = 0;
    node->style.padding_left = 0;
    node->style.padding_right = 0;
    node->style.border_width = (tag == HTML_TAG_IFRAME) ? 1 : 0;
    node->style.border_color = (tag == HTML_TAG_IFRAME) ? 0x808080 : 0;
    node->style.width = 0;
    node->style.height = 0;
    node->style.box_sizing = 0;
    node->style.visibility_hidden = 0;

    /* Phase 6C: Modern CSS defaults */
    node->var_table.count = 0;
    node->style.display_mode = node->style.display_none ? CSS_DISPLAY_NONE : (node->style.is_block ? CSS_DISPLAY_BLOCK : (node->style.is_inline_block ? CSS_DISPLAY_INLINE_BLOCK : CSS_DISPLAY_INLINE));
    node->style.flex_direction = CSS_FLEX_DIR_ROW;
    node->style.flex_wrap = CSS_FLEX_NOWRAP;
    node->style.justify_content = CSS_JUSTIFY_START;
    node->style.align_items = CSS_ALIGN_START;
    node->style.gap_row = 0;
    node->style.gap_col = 0;
    node->style.grid_columns = 0;
    node->style.flex_grow = 0;

    if (tag == HTML_TAG_A) {
        node->style.color = 0x4A9EFF; /* Link blue */
    }

    node->bounds.x = 0;
    node->bounds.y = 0;
    node->bounds.w = 0;
    node->bounds.h = 0;

    node->parent = NULL;
    node->first_child = NULL;
    node->next_sibling = NULL;

    return node;
}

static html_node_t *find_node_by_id_rec(html_node_t *curr, const char *id) {
    if (!curr || !id) return NULL;
    if (curr->id && strcmp(curr->id, id) == 0) return curr;
    for (html_node_t *child = curr->first_child; child; child = child->next_sibling) {
        html_node_t *found = find_node_by_id_rec(child, id);
        if (found) return found;
    }
    return NULL;
}

html_node_t *firstos_renderer_get_element_by_id(firstos_renderer_context_t *ctx, const char *id) {
    if (!ctx || !ctx->root_dom || !id) return NULL;
    return find_node_by_id_rec(ctx->root_dom, id);
}

static html_node_t *find_node_by_tag_rec(html_node_t *curr, html_tag_t tag) {
    if (!curr) return NULL;
    if (curr->tag == tag) return curr;
    for (html_node_t *child = curr->first_child; child; child = child->next_sibling) {
        html_node_t *found = find_node_by_tag_rec(child, tag);
        if (found) return found;
    }
    return NULL;
}

html_node_t *firstos_renderer_get_element_by_tag(firstos_renderer_context_t *ctx, html_tag_t tag) {
    if (!ctx || !ctx->root_dom) return NULL;
    return find_node_by_tag_rec(ctx->root_dom, tag);
}

static html_node_t *find_node_by_class_rec(html_node_t *curr, const char *cls) {
    if (!curr || !cls) return NULL;
    if (curr->class_name && strcmp(curr->class_name, cls) == 0) return curr;
    for (html_node_t *child = curr->first_child; child; child = child->next_sibling) {
        html_node_t *found = find_node_by_class_rec(child, cls);
        if (found) return found;
    }
    return NULL;
}

html_node_t *firstos_renderer_get_element_by_class(firstos_renderer_context_t *ctx, const char *cls) {
    if (!ctx || !ctx->root_dom || !cls) return NULL;
    return find_node_by_class_rec(ctx->root_dom, cls);
}

void firstos_renderer_set_origin(firstos_renderer_context_t *ctx, const char *origin) {
    if (!ctx) return;
    if (!origin) {
        ctx->origin[0] = '\0';
        return;
    }
    strncpy(ctx->origin, origin, sizeof(ctx->origin) - 1);
    ctx->origin[sizeof(ctx->origin) - 1] = '\0';
}

const char *firstos_renderer_get_origin(const firstos_renderer_context_t *ctx) {
    if (!ctx) return "";
    return ctx->origin;
}
