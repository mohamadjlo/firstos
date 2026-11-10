#ifndef PREVIEW_H
#define PREVIEW_H

#include "gui_internal.h"

void preview_init(void);
void preview_open(const char *path);
void preview_draw(app_window_t *app);
void preview_handle_click(app_window_t *app, int mx, int my);
void preview_handle_drag(app_window_t *app, int dx, int dy);
void preview_handle_key(int key);
void preview_close(void);

#endif /* PREVIEW_H */
