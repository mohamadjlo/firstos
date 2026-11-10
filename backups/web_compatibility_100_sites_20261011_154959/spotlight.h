#ifndef SPOTLIGHT_H
#define SPOTLIGHT_H

#define SPOTLIGHT_MAX_RESULTS 12

/* View modes */
enum {
    SPOTLIGHT_VIEW_SEARCH = 0,
    SPOTLIGHT_VIEW_APP_DROPDOWN = 1,
    SPOTLIGHT_VIEW_MEDIA_PREVIEW = 2
};

/* Media classification */
enum {
    SPOTLIGHT_MEDIA_NONE = 0,
    SPOTLIGHT_MEDIA_IMAGE = 1,
    SPOTLIGHT_MEDIA_VIDEO = 2,
    SPOTLIGHT_MEDIA_AUDIO = 3
};

typedef struct {
    char title[64];
    char subtitle[64];
    int icon_id;
    int score;
    int plugin_id;
    char action_arg[128];
    int media_type;
    int app_kind;
} spotlight_result_t;

typedef struct {
    const char *name;
    void (*init)(void);
    int (*query)(const char *text, spotlight_result_t *results, int max_results);
    void (*execute)(spotlight_result_t *result);
} spotlight_plugin_t;

// State exposed to GUI
extern spotlight_result_t spotlight_results[SPOTLIGHT_MAX_RESULTS];
extern int spotlight_num_results;
extern int spotlight_selected_index;
extern int spotlight_view_mode;
extern int spotlight_active_app_kind;
extern int spotlight_media_type;
extern char spotlight_media_path[256];
extern char spotlight_media_title[64];
extern int spotlight_media_playing;
extern int spotlight_media_anim_ticks;

// Hotkey configuration
extern int spotlight_hotkey_ctrl;
extern int spotlight_hotkey_alt;
extern int spotlight_hotkey_shift;
extern unsigned char spotlight_hotkey_char;

void spotlight_init(void);
void spotlight_search(const char *query);
void spotlight_execute(int index);
int  sl_str_contains_ci(const char *haystack, const char *needle);
int  sl_strlen(const char *s);
int  sl_strcmp(const char *s1, const char *s2);
void int_to_str(int val, char *buf);

/* Dropdown & Media Previews */
void spotlight_open_dropdown_app(int app_kind);
void spotlight_open_media_preview(const char *path, int media_type, const char *title);
void spotlight_close_dropdown(void);
int  spotlight_is_in_dropdown(void);
void spotlight_toggle_media_playback(void);
void spotlight_stop_media_playback(void);
void spotlight_media_tick(void);

#include "explorer.h"

// Dropdown Explorer state
extern char spotlight_explorer_path[128];
extern int spotlight_explorer_selected;
extern int spotlight_explorer_count;
extern explorer_entry_t spotlight_explorer_entries[32];

void spotlight_open_dropdown_explorer(const char *initial_path);
void spotlight_explorer_navigate(const char *path);
void spotlight_explorer_up(void);
void spotlight_explorer_enter(void);
void spotlight_explorer_move(int delta);
void spotlight_explorer_refresh(void);

#endif

