#ifndef DOCK_H
#define DOCK_H

extern int dock_base_size;
extern int dock_magnification_enabled;
extern int dock_animation_mode;
extern unsigned int dock_bg_color;
extern unsigned char dock_bg_alpha;

enum {
    DOCK_ANIM_MAGNIFY = 0,
    DOCK_ANIM_GENIE = 1,
    DOCK_ANIM_BOUNCE = 2,
    DOCK_ANIM_SLIDE = 3
};

#define DOCK_MAX_SCALE 1.5f
#define DOCK_RADIUS 120
#define DOCK_CORNER_RADIUS 16

typedef struct {
    int app_id;        // APP_TERMINAL, etc.
    int icon_id;       // ICON_TERMINAL, etc.
    char name[32];
    char icon_path[64];
    int is_running;
    int is_minimized;
    float current_scale;
    float current_x;
    float current_y;
    int icon_loaded;
    int icon_w;
    int icon_h;
    unsigned int icon_pixels[32 * 32];
} dock_item_t;

void dock_init(void);
void dock_update_and_render(int mouse_x, int mouse_y, int screen_width, int screen_height);
int dock_handle_click(int mouse_x, int mouse_y, int right_click);
void dock_get_item_rect(int app_id, int *x, int *y, int *w, int *h);
void dock_reset_positions(void);

extern int dock_context_open;
extern int dock_context_app_id;
extern int dock_context_x;
extern int dock_context_y;

void dock_context_draw(void);
int dock_context_handle_click(int mx, int my);

#endif
