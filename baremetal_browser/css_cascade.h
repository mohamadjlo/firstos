#ifndef CSS_CASCADE_H
#define CSS_CASCADE_H

#include "bb_types.h"
#include "bb_dom.h"
#include "css_parser.h"

typedef enum {
    CSS_ORIGIN_USER_AGENT = 0,
    CSS_ORIGIN_AUTHOR     = 1,
    CSS_ORIGIN_INLINE     = 2
} css_origin_t;

/* Matches all rules against node and applies winning declarations to node->computed_style */
void css_cascade_apply_to_node(bb_node_t *node, const css_parsed_sheet_t *sheet);

#endif /* CSS_CASCADE_H */
