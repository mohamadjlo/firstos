#ifndef GUI_INTERNAL_H
#define GUI_INTERNAL_H

#include "gui.h"
#include "framebuffer.h"
#include "font.h"
#include "fs.h"

typedef unsigned int u32;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    u32 color;
} rect_t;

#define MAX_WINDOW_TABS 4

typedef struct {
    char title[32];
    char path[128];
    int scroll;
    int cursor;
    int used;
} window_tab_t;

typedef struct {
    rect_t frame;
    rect_t title;
    rect_t close;
    rect_t minimize;
    rect_t maximize;
    rect_t resize;
    int open;
    int minimized;
    int maximized;
    int resizing;
    int snapped;
    int kind;
    int instance_id;
    int saved_x;
    int saved_y;
    int saved_w;
    int saved_h;
    int min_w;
    int min_h;
    int last_title_click_ticks;
    u32 title_color;

    // Window Tab Support
    int tab_count;
    int active_tab;
    window_tab_t tabs[MAX_WINDOW_TABS];
    rect_t tab_rects[MAX_WINDOW_TABS];
    rect_t tab_close_rects[MAX_WINDOW_TABS];
    rect_t tab_new_rect;

    // Instance specific state
    char current_path[128];
    int scroll_pos;
    int selected_item;
} app_window_t;

#define DESKTOP_SORT_NONE 0
#define DESKTOP_SORT_NAME 1
#define DESKTOP_SORT_DATE 2
#define DESKTOP_SORT_KIND 3
extern int desktop_sort_mode;
void sort_desktop_items(void);
app_window_t* open_new_window(int kind);
void gui_set_window_maximized(app_window_t *app, int enabled);

enum {
    WINDOW_CORNER_RADIUS = 10
};

enum {
    APP_TERMINAL = 1,
    APP_FILES = 2,
    APP_DISPLAY = 3,
    APP_EXPLORER = 4,
    APP_SETTINGS = 5,
    APP_TEXT_EDIT = 6,
    APP_MEDIA_PLAYER = 7,
    APP_CALCULATOR = 8,
    APP_PREVIEW = 9,
    APP_PHOTO_EDITOR = 10,
    APP_IDE = 11,
    APP_TASKMAN = 12,
    APP_BROWSER = 13
};

extern u32 system_font_color;
extern int theme_id;
extern int wallpaper_id;
extern int wallpaper_source;
extern char wallpaper_path[128];
extern int mouse_sensitivity;
extern int mouse_cursor_style;
extern int mouse_cursor_size;
extern u32 mouse_cursor_color;
extern int resolution_id;
extern int system_volume;
extern int titlebar_position;
extern int titlebar_autohide;
extern u32 titlebar_custom_color;
extern int titlebar_color_custom;
extern u8 titlebar_alpha;
extern u32 topbar_color;
extern int topbar_color_custom;
extern u8 topbar_alpha;
extern u32 wallpaper_solid_color;
extern int mouse_context_cursors_enabled;
extern int mouse_cursor_state;
enum {
    CURSOR_STATE_IDLE = 0,
    CURSOR_STATE_LOADING = 1,
    CURSOR_STATE_OPENING = 2,
    CURSOR_STATE_DRAGGING = 3
};
extern int needs_redraw;
extern char editor_filename[32];
extern char editor_text[32768];
extern char editor_style[32768];
extern int editor_current_style;
extern int editor_length;
extern int editor_saved;
extern int editor_save_dialog_open;
extern int explorer_picker_mode;
extern char editor_save_name[32];
extern int editor_save_name_cursor;
extern int editor_save_name_focus;
extern int editor_cursor;
extern int editor_select_anchor;
extern int editor_select_active;
extern char editor_clipboard[512];
extern int editor_mouse_selecting;
extern int editor_scroll_y;
extern int editor_scroll_x;
extern rect_t editor_text_area;

void apply_theme(void);
void draw_label(int x, int y, const char *text, u32 color);
void font_text_fit(int x, int y, const char *text, u32 color, int max_px);
void font_text_fit_center(int x, int y, const char *text, u32 color, int max_px);
void draw_soft_box(rect_t r, u32 fill_color, u32 edge_color, u32 inner_color);
void draw_scrollbar(int x, int y, int h, int total_items, int visible_items, int scroll);
void outline(int x, int y, int w, int h, u32 color);
int point_in_rect(int px, int py, rect_t r);
void draw_button(rect_t r, u32 base, u32 hi, const char *text, u32 text_color);
app_window_t* app_slot(int kind);
void open_app(int kind);
void focus_app_window(int index);

enum {
    SNAP_NONE = 0,
    SNAP_LEFT = 1,
    SNAP_RIGHT = 2,
    SNAP_FULLSCREEN = 3,
    SNAP_TOP_LEFT = 4,
    SNAP_TOP_RIGHT = 5,
    SNAP_BOTTOM_LEFT = 6,
    SNAP_BOTTOM_RIGHT = 7,
    SNAP_RESTORE = 8
};

app_window_t* active_app_window(void);
void apply_snap_to_window(app_window_t* app, int snap_mode);

#ifndef MAX_SCREEN_WIDTH
#define MAX_SCREEN_WIDTH 1024
#define MAX_SCREEN_HEIGHT 768
#endif
extern u32 wallpaper_cache[MAX_SCREEN_WIDTH * MAX_SCREEN_HEIGHT];
char scancode_to_ascii(unsigned char key);
void gui_desktop_on_logout(void);
void gui_trigger_post_boot_fs(void);

extern char cached_thumbnail_path[128];
extern u32 *cached_thumbnail_data;
extern int cached_thumbnail_w;
extern int cached_thumbnail_h;
void update_explorer_thumbnail(const char *path, const char *ext);
extern char text_preview_buf[1024];
void text_preview_update(const char *path);
extern int cursor_blink_visible;
extern int explorer_selected;
extern int explorer_anim_ticks;
int  load_ico_from_path(const char* path, u32* out_pixels, int* out_w, int* out_h);
void draw_icon_scaled(const u32 *pixels, int src_w, int src_h, int x, int y, int dst_w, int dst_h);

/* Weather definitions */
typedef enum {
    WEATHER_SUNNY = 0,
    WEATHER_CLOUDY = 1,
    WEATHER_RAINY = 2
} weather_cond_t;

typedef struct {
    char city[32];
    weather_cond_t condition;
    int temp_c;
    int humidity;
    int wind_kph;
    const char *desc;
} weather_info_t;

extern weather_info_t current_weather;
void draw_weather_icon(int cx, int cy, weather_cond_t cond, int size, u32 def_color);
void weather_tick(void);
void weather_set_city(const char *name);
int  weather_fetch_realtime(const char *city_override);
int  weather_is_live(void);

/* Desktop Icon Sizing */
enum {
    ICON_SIZE_SMALL = 0,
    ICON_SIZE_MEDIUM = 1,
    ICON_SIZE_LARGE = 2
};
extern int desktop_icon_size;

/* Widgets Window */
extern int widgets_window_open;
extern rect_t widgets_window_rect;

#endif

