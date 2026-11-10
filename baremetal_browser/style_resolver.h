#ifndef STYLE_RESOLVER_H
#define STYLE_RESOLVER_H

#include "bb_types.h"
#include "bb_dom.h"
#include "css_parser.h"

void style_resolver_init_default(bb_style_t *style);
void style_resolver_apply_ua_defaults(bb_node_t *node, bb_style_t *style);
void style_resolver_inherit(bb_style_t *child_style, const bb_style_t *parent_style);
void style_resolve_tree(bb_node_t *root, const css_parsed_sheet_t *sheet);

#endif /* STYLE_RESOLVER_H */
