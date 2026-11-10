#ifndef HTML_PARSER_H
#define HTML_PARSER_H

#include "bb_types.h"
#include "bb_dom.h"

/*
 * Parse HTML source of `len` bytes into a DOM tree.
 * Returns the root BB_NODE_DOCUMENT node.
 * Caller owns the returned tree and must free it via bb_node_free_tree().
 */
bb_node_t *html_parse(const char *src, size_t len);

#endif /* HTML_PARSER_H */
