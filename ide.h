#ifndef IDE_H
#define IDE_H

typedef struct {
    char text[32768];
    int length;
    int cursor;
    int scroll_y;
    int scroll_x;
    int saved;
    int select_anchor;
    int select_active;
    char filename[32];
    char undo_text[32768];
} ide_tab_t;

extern ide_tab_t ide_tabs[4];
extern int ide_active_tab;

#define ide_text (ide_tabs[ide_active_tab].text)
#define ide_length (ide_tabs[ide_active_tab].length)
#define ide_cursor (ide_tabs[ide_active_tab].cursor)
#define ide_scroll_y (ide_tabs[ide_active_tab].scroll_y)
#define ide_scroll_x (ide_tabs[ide_active_tab].scroll_x)
#define ide_saved (ide_tabs[ide_active_tab].saved)
#define ide_select_anchor (ide_tabs[ide_active_tab].select_anchor)
#define ide_select_active (ide_tabs[ide_active_tab].select_active)
#define ide_filename (ide_tabs[ide_active_tab].filename)
#define ide_undo_text (ide_tabs[ide_active_tab].undo_text)

extern void ide_init_tabs(void);
extern void ide_switch_tab(int index);

#include "app_framework.h"

/* FirstOS App Studio v1 Project & Build Subsystem */
#define APP_STUDIO_MAX_LOG 4096
#define APP_STUDIO_MAX_PROJECT_FILES 16

typedef struct {
    char name[64];
    char path[128];
    int is_dir;
} app_studio_file_t;

typedef struct {
    char project_name[64];
    char project_dir[128];
    char manifest_path[128];
    char main_src_path[128];
    char build_artifact[128];
    int template_id; // 0=HelloWindow, 1=TextViewer, 2=NetworkDemo
    int active_running_pid;
    int is_running;
    char logs[APP_STUDIO_MAX_LOG];
    int log_len;
    app_studio_file_t files[APP_STUDIO_MAX_PROJECT_FILES];
    int file_count;
    int selected_file_idx;
    app_output_pane_t output_pane;
    app_status_bar_t status_bar;
} app_studio_state_t;

extern app_studio_state_t g_app_studio;

void app_studio_init(void);
void app_studio_create_project(const char *name, int template_id);
void app_studio_open_project(const char *proj_dir);
void app_studio_build(void);
void app_studio_run(void);
void app_studio_stop(void);
void app_studio_append_log(const char *msg);
void app_studio_clear_logs(void);
void app_studio_scroll_output(int delta);

#endif
