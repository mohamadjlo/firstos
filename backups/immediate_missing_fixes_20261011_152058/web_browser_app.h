#ifndef WEB_BROWSER_APP_H
#define WEB_BROWSER_APP_H

#include "gui_internal.h"

void web_browser_app_init(void);
void web_browser_app_launch(const char *url);
void web_browser_app_draw(app_window_t *app);
void web_browser_app_handle_click(app_window_t *app, int mx, int my);
void web_browser_app_handle_right_click(app_window_t *app, int mx, int my);
int  web_browser_app_is_context_menu_open(void);
void web_browser_app_close_context_menu(void);
void web_browser_app_handle_key(unsigned char key, char ascii);
void web_browser_app_handle_scroll(int delta);
void web_browser_app_tick(void);
void web_browser_reflow(void);
void web_browser_trigger_redraw(const char *reason);
u32  web_browser_get_generation(void);
void web_browser_navigate(const char *target);
int  browser_core_commit_loaded_page(const char *url, const char *body, int body_len, int view_w, int view_h, u32 generation);

#endif
