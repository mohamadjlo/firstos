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
void html5_script_set_origin(const char *origin);
const char *html5_script_get_origin(void);

/* Event Loop & Microtask Queue API */
typedef void (*html5_task_fn_t)(void *arg);
int  html5_script_queue_microtask(html5_task_fn_t fn, void *arg, const char *desc);
int  html5_script_queue_deferred(const char *script_code, html_node_t *root_dom);
void html5_script_drain_microtasks(void);
void html5_script_event_loop_tick(void);

#endif /* HTML5_SCRIPT_H */
