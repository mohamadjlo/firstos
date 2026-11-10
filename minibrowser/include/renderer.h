#ifndef RENDERER_H
#define RENDERER_H

#include "window.h"
#include <SDL.h>

/* Paints the entire window: layout tree (offset by scroll_y), link highlights, and modal overlay */
void renderer_paint_window(BrowserWindow *win, WindowManager *wm);

/* Renders window and saves screenshot to BMP file before flipping buffers */
void renderer_save_screenshot(BrowserWindow *win, WindowManager *wm, const char *bmp_path);

#endif /* RENDERER_H */
