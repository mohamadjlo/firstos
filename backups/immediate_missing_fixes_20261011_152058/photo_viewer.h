#ifndef PHOTO_VIEWER_H
#define PHOTO_VIEWER_H

#include "gui_internal.h"

void photo_viewer_open(const char *path);
void draw_photo_viewer(app_window_t *app);
void handle_photo_viewer_click(app_window_t *app, int mx, int my);

#endif
