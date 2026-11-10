#ifndef APP_FRAMEWORK_H
#define APP_FRAMEWORK_H

#include "framebuffer.h"
#include "font.h"
#include "theme.h"
#include "gui_internal.h"
#include "gui_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Common Actions */
typedef enum {
    APP_ACTION_NONE = 0,
    APP_ACTION_NEW = 1,
    APP_ACTION_OPEN = 2,
    APP_ACTION_SAVE = 3,
    APP_ACTION_CLOSE = 4,
    APP_ACTION_FIND = 5,
    APP_ACTION_BUILD = 6,
    APP_ACTION_RUN = 7,
    APP_ACTION_STOP = 8
} app_action_t;

/* Status Bar */
typedef enum {
    APP_STATUS_INFO = 0,
    APP_STATUS_SUCCESS = 1,
    APP_STATUS_WARNING = 2,
    APP_STATUS_ERROR = 3
} app_status_type_t;

typedef struct {
    char text[64];
    char details[64];
    app_status_type_t type;
    int dirty;
} app_status_bar_t;

void app_status_bar_init(app_status_bar_t *bar);
void app_status_bar_set(app_status_bar_t *bar, app_status_type_t type, const char *text, const char *details);
void app_draw_status_bar(rect_t r, const app_status_bar_t *bar);

/* Output & Diagnostics Console */
#define APP_OUTPUT_MAX_BYTES 8192
#define APP_OUTPUT_MAX_LINES 256

typedef struct {
    char buffer[APP_OUTPUT_MAX_BYTES];
    int length;
    int line_starts[APP_OUTPUT_MAX_LINES];
    int line_count;
    int scroll_y;
    int auto_scroll;
    char title[32];
} app_output_pane_t;

void app_output_pane_init(app_output_pane_t *pane, const char *title);
void app_output_pane_append(app_output_pane_t *pane, const char *str);
void app_output_pane_clear(app_output_pane_t *pane);
void app_output_pane_scroll(app_output_pane_t *pane, int delta_lines);
void app_output_pane_draw(rect_t r, const app_output_pane_t *pane);

/* Shortcut Helper */
int app_handle_common_shortcuts(unsigned char key, int ctrl, int shift);

#ifdef __cplusplus
}
#endif

#endif /* APP_FRAMEWORK_H */
