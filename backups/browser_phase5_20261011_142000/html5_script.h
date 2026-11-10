#ifndef HTML5_SCRIPT_H
#define HTML5_SCRIPT_H

#include "io.h"
#include "html5_engine.h"

void html5_script_init(void);
void html5_script_reset(void);
void html5_script_eval(const char *script_code, html_node_t *root_dom);
void html5_script_run_dom_scripts(html_node_t *root_dom, const char *base_url);
const char *html5_script_get_last_alert(void);
void html5_script_clear_alert(void);

#endif /* HTML5_SCRIPT_H */
