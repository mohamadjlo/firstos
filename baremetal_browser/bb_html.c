/* bb_html.c — thin adapter keeping the existing bb_html.h API intact */
#include "bb_html.h"
#include "html_parser.h"
#include "bb_libc.h"

bb_node_t *bb_html_parse(const char *html) {
    if (!html) return NULL;
    return html_parse(html, bb_strlen(html));
}
