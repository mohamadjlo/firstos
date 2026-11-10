#ifndef HTML5_MEDIA_H
#define HTML5_MEDIA_H

#include "io.h"
#include "gui_internal.h"

typedef struct {
    char src[256];
    char poster[256];
    int is_playing;
    int curr_frame;
    int total_frames;
    int curr_time_sec;
    int duration_sec;
    int volume;
    int has_controls;
    int width;
    int height;
    rect_t bounds;
    rect_t play_btn_bounds;
    rect_t scrub_bounds;
    rect_t fs_btn_bounds;
    u32 accent_color;
} html5_video_ctx_t;

void html5_media_init(void);
html5_video_ctx_t *html5_media_create_video_ctx(const char *src, int controls, int width, int height);
void html5_media_destroy_video_ctx(html5_video_ctx_t *ctx);
void html5_media_render_video(html5_video_ctx_t *ctx, int x, int y, int w, int h);
int  html5_media_handle_click(html5_video_ctx_t *ctx, int mx, int my);
void html5_media_tick(void);

#endif /* HTML5_MEDIA_H */
