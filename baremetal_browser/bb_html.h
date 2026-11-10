#ifndef BB_HTML_H
#define BB_HTML_H

#include "bb_types.h"
#include "bb_dom.h"

/* Parses an HTML string and returns the root document node */
bb_node_t *bb_html_parse(const char *html);

#endif /* BB_HTML_H */
