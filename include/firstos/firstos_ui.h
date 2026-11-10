#ifndef _FIRSTOS_UI_H
#define _FIRSTOS_UI_H

#include "firstos_app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x, y, w, h;
    uint32_t color;
} firstos_ui_rect_t;

typedef struct {
    char title[64];
    int width;
    int height;
    int resizable;
} firstos_ui_window_config_t;

static inline void firstos_ui_init_window(firstos_ui_window_config_t *cfg, const char *title, int w, int h) {
    if (!cfg) return;
    int i = 0;
    while (title && title[i] && i < 63) { cfg->title[i] = title[i]; i++; }
    cfg->title[i] = '\0';
    cfg->width = (w > 0) ? w : 640;
    cfg->height = (h > 0) ? h : 480;
    cfg->resizable = 1;
}

#ifdef __cplusplus
}
#endif

#endif /* _FIRSTOS_UI_H */
