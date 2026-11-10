#include "css_cascade.h"
#include "css_tokenizer.h"
#include "bb_libc.h"

#define MAX_CSS_PROPS 40

typedef struct {
    bool         is_set;
    css_origin_t origin;
    uint32_t     specificity;
    uint32_t     source_order;
    char        *val;
} cascade_candidate_t;

static bool candidate_beats_current(const cascade_candidate_t *cand, const cascade_candidate_t *curr) {
    if (!curr->is_set) return true;

    /* 1. Origin (INLINE > AUTHOR > USER_AGENT) */
    if (cand->origin > curr->origin) return true;
    if (cand->origin < curr->origin) return false;

    /* 2. Specificity */
    if (cand->specificity > curr->specificity) return true;
    if (cand->specificity < curr->specificity) return false;

    /* 3. Source order (later rule wins ties) */
    if (cand->source_order >= curr->source_order) return true;

    return false;
}

static void update_candidate(cascade_candidate_t *table, css_property_id_t pid,
                             css_origin_t origin, uint32_t spec, uint32_t order,
                             const char *val) {
    if (pid <= 0 || pid >= MAX_CSS_PROPS || !val) return;

    cascade_candidate_t cand;
    cand.is_set = true;
    cand.origin = origin;
    cand.specificity = spec;
    cand.source_order = order;
    cand.val = (char *)val;

    if (candidate_beats_current(&cand, &table[pid])) {
        if (table[pid].val) bb_free(table[pid].val);
        table[pid].is_set = true;
        table[pid].origin = origin;
        table[pid].specificity = spec;
        table[pid].source_order = order;
        table[pid].val = bb_strdup(val);
    }
}

static void record_declaration(cascade_candidate_t *table, const css_declaration_t *decl,
                               css_origin_t origin, uint32_t spec, uint32_t order) {
    if (!decl || !decl->raw_val) return;

    if (decl->prop_id == CSS_PROP_MARGIN) {
        /* Expand margin shorthand */
        update_candidate(table, CSS_PROP_MARGIN, origin, spec, order, decl->raw_val);
    } else if (decl->prop_id == CSS_PROP_PADDING) {
        /* Expand padding shorthand */
        update_candidate(table, CSS_PROP_PADDING, origin, spec, order, decl->raw_val);
    } else if (decl->prop_id == CSS_PROP_BORDER) {
        update_candidate(table, CSS_PROP_BORDER, origin, spec, order, decl->raw_val);
    } else {
        update_candidate(table, decl->prop_id, origin, spec, order, decl->raw_val);
    }
}

void css_cascade_apply_to_node(bb_node_t *node, const css_parsed_sheet_t *sheet) {
    if (!node || node->type != BB_NODE_ELEMENT || !node->computed_style) return;

    cascade_candidate_t table[MAX_CSS_PROPS];
    bb_memset(table, 0, sizeof(table));

    /* 1. Author Stylesheet Rules */
    if (sheet) {
        const css_rule_t *rule = sheet->rules;
        while (rule) {
            if (rule->selector && css_selector_match(rule->selector, node)) {
                uint32_t spec = rule->selector->specificity;
                uint32_t order = rule->source_order;
                const css_declaration_t *decl = rule->declarations;
                while (decl) {
                    record_declaration(table, decl, CSS_ORIGIN_AUTHOR, spec, order);
                    decl = decl->next;
                }
            }
            rule = rule->next;
        }
    }

    /* 2. Inline style="..." Attribute (Origin: INLINE, Specificity: 1,000,000) */
    const char *inline_style = bb_node_get_attribute(node, "style");
    if (inline_style && inline_style[0]) {
        /* Parse inline declarations */
        css_parsed_sheet_t tmp_sheet;
        bb_memset(&tmp_sheet, 0, sizeof(tmp_sheet));
        /* Parse into style using helper */
        bb_style_t tmp_style;
        bb_memset(&tmp_style, 0, sizeof(tmp_style));
        
        /* Direct declaration scan for cascade table */
        const char *p = inline_style;
        uint32_t inline_order = 0;

        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (!*p) break;

            const char *prop_start = p;
            while (*p && *p != ':' && *p != ';') p++;
            if (*p != ':') {
                while (*p && *p != ';') p++;
                if (*p == ';') p++;
                continue;
            }

            size_t prop_len = (size_t)(p - prop_start);
            while (prop_len > 0 && (prop_start[prop_len - 1] == ' ' || prop_start[prop_len - 1] == '\t')) {
                prop_len--;
            }

            char prop_buf[64];
            if (prop_len >= sizeof(prop_buf)) prop_len = sizeof(prop_buf) - 1;
            bb_memcpy(prop_buf, prop_start, prop_len);
            prop_buf[prop_len] = '\0';

            p++; /* skip ':' */
            while (*p == ' ' || *p == '\t') p++;

            const char *val_start = p;
            while (*p && *p != ';') p++;
            size_t val_len = (size_t)(p - val_start);
            while (val_len > 0 && (val_start[val_len - 1] == ' ' || val_start[val_len - 1] == '\t' ||
                                   val_start[val_len - 1] == '\r' || val_start[val_len - 1] == '\n')) {
                val_len--;
            }

            char val_buf[128];
            if (val_len >= sizeof(val_buf)) val_len = sizeof(val_buf) - 1;
            bb_memcpy(val_buf, val_start, val_len);
            val_buf[val_len] = '\0';

            css_declaration_t d;
            d.prop_id = CSS_PROP_UNKNOWN;
            if (bb_strcasecmp(prop_buf, "display") == 0) d.prop_id = CSS_PROP_DISPLAY;
            else if (bb_strcasecmp(prop_buf, "position") == 0) d.prop_id = CSS_PROP_POSITION;
            else if (bb_strcasecmp(prop_buf, "top") == 0) d.prop_id = CSS_PROP_TOP;
            else if (bb_strcasecmp(prop_buf, "right") == 0) d.prop_id = CSS_PROP_RIGHT;
            else if (bb_strcasecmp(prop_buf, "bottom") == 0) d.prop_id = CSS_PROP_BOTTOM;
            else if (bb_strcasecmp(prop_buf, "left") == 0) d.prop_id = CSS_PROP_LEFT;
            else if (bb_strcasecmp(prop_buf, "width") == 0) d.prop_id = CSS_PROP_WIDTH;
            else if (bb_strcasecmp(prop_buf, "height") == 0) d.prop_id = CSS_PROP_HEIGHT;
            else if (bb_strcasecmp(prop_buf, "margin") == 0) d.prop_id = CSS_PROP_MARGIN;
            else if (bb_strcasecmp(prop_buf, "margin-top") == 0) d.prop_id = CSS_PROP_MARGIN_TOP;
            else if (bb_strcasecmp(prop_buf, "margin-right") == 0) d.prop_id = CSS_PROP_MARGIN_RIGHT;
            else if (bb_strcasecmp(prop_buf, "margin-bottom") == 0) d.prop_id = CSS_PROP_MARGIN_BOTTOM;
            else if (bb_strcasecmp(prop_buf, "margin-left") == 0) d.prop_id = CSS_PROP_MARGIN_LEFT;
            else if (bb_strcasecmp(prop_buf, "padding") == 0) d.prop_id = CSS_PROP_PADDING;
            else if (bb_strcasecmp(prop_buf, "padding-top") == 0) d.prop_id = CSS_PROP_PADDING_TOP;
            else if (bb_strcasecmp(prop_buf, "padding-right") == 0) d.prop_id = CSS_PROP_PADDING_RIGHT;
            else if (bb_strcasecmp(prop_buf, "padding-bottom") == 0) d.prop_id = CSS_PROP_PADDING_BOTTOM;
            else if (bb_strcasecmp(prop_buf, "padding-left") == 0) d.prop_id = CSS_PROP_PADDING_LEFT;
            else if (bb_strcasecmp(prop_buf, "border-width") == 0) d.prop_id = CSS_PROP_BORDER_WIDTH;
            else if (bb_strcasecmp(prop_buf, "border-color") == 0) d.prop_id = CSS_PROP_BORDER_COLOR;
            else if (bb_strcasecmp(prop_buf, "border-radius") == 0) d.prop_id = CSS_PROP_BORDER_RADIUS;
            else if (bb_strcasecmp(prop_buf, "border") == 0) d.prop_id = CSS_PROP_BORDER;
            else if (bb_strcasecmp(prop_buf, "color") == 0) d.prop_id = CSS_PROP_COLOR;
            else if (bb_strcasecmp(prop_buf, "background-color") == 0 || bb_strcasecmp(prop_buf, "background") == 0) d.prop_id = CSS_PROP_BACKGROUND_COLOR;
            else if (bb_strcasecmp(prop_buf, "font-size") == 0) d.prop_id = CSS_PROP_FONT_SIZE;
            else if (bb_strcasecmp(prop_buf, "font-weight") == 0) d.prop_id = CSS_PROP_FONT_WEIGHT;
            else if (bb_strcasecmp(prop_buf, "text-align") == 0) d.prop_id = CSS_PROP_TEXT_ALIGN;
            else if (bb_strcasecmp(prop_buf, "line-height") == 0) d.prop_id = CSS_PROP_LINE_HEIGHT;
            else if (bb_strcasecmp(prop_buf, "white-space") == 0) d.prop_id = CSS_PROP_WHITE_SPACE;
            else if (bb_strcasecmp(prop_buf, "overflow") == 0) d.prop_id = CSS_PROP_OVERFLOW;
            else if (bb_strcasecmp(prop_buf, "visibility") == 0) d.prop_id = CSS_PROP_VISIBILITY;
            else if (bb_strcasecmp(prop_buf, "z-index") == 0) d.prop_id = CSS_PROP_Z_INDEX;

            if (d.prop_id != CSS_PROP_UNKNOWN) {
                d.raw_val = val_buf;
                record_declaration(table, &d, CSS_ORIGIN_INLINE, 1000000, inline_order++);
            }

            if (*p == ';') p++;
        }
    }

    /* 3. Apply winning properties to node->computed_style */
    for (int i = 1; i < MAX_CSS_PROPS; i++) {
        if (table[i].is_set && table[i].val) {
            css_apply_declaration(node->computed_style, (css_property_id_t)i, table[i].val);
            bb_free(table[i].val);
        }
    }
}
