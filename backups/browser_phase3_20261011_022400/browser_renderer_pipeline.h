#ifndef BROWSER_RENDERER_PIPELINE_H
#define BROWSER_RENDERER_PIPELINE_H

#include "io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RENDERER_PIPE_IDLE = 0,
    RENDERER_PIPE_PARSING,
    RENDERER_PIPE_DOM_READY,
    RENDERER_PIPE_LAYOUTING,
    RENDERER_PIPE_PAINT_READY,
    RENDERER_PIPE_ERROR
} browser_renderer_state_t;

typedef struct {
    browser_renderer_state_t state;
    u32 generation;
    int document_height;
    int node_count;
    int is_dirty;
    char url[512];
    char title[128];
} browser_renderer_pipeline_t;

/* Initialize Renderer Pipeline */
void browser_renderer_pipeline_init(void);

/* Complete Document Commit Pipeline:
 * Step 1: Parse HTML -> DOM
 * Step 2: Resources & Stylesheet cascade
 * Step 3: Layout calculation
 * Step 4: Paint preparation & Host notification
 * Returns 1 on success, 0 on failure. */
int  browser_renderer_pipeline_commit(const char *url, const char *body, int body_len, int view_w, int view_h, u32 generation);

/* Incremental reflow / repaint triggered by DOM mutations or resource loads */
void browser_renderer_pipeline_invalidate(const char *reason);
void browser_renderer_pipeline_reflow(int view_w, int view_h);

/* State query */
const browser_renderer_pipeline_t *browser_renderer_pipeline_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* BROWSER_RENDERER_PIPELINE_H */
