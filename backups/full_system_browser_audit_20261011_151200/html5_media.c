#include "html5_media.h"
#include "framebuffer.h"
#include "font.h"
#include "serial.h"
#include "heap.h"
#include "string.h"
#include "stdio.h"

#define MAX_ACTIVE_VIDEOS 8
static html5_video_ctx_t g_videos[MAX_ACTIVE_VIDEOS];
static int g_video_count = 0;

void html5_media_init(void) {
    memset(g_videos, 0, sizeof(g_videos));
    g_video_count = 0;
    serial_write("[html5_media] Embedded video subsystem initialized\n");
}

html5_video_ctx_t *html5_media_create_video_ctx(const char *src, int controls, int width, int height) {
    if (g_video_count >= MAX_ACTIVE_VIDEOS) return NULL;

    html5_video_ctx_t *v = &g_videos[g_video_count++];
    memset(v, 0, sizeof(html5_video_ctx_t));

    if (src) strncpy(v->src, src, sizeof(v->src) - 1);
    v->has_controls = controls;
    v->width = (width > 0) ? width : 480;
    v->height = (height > 0) ? height : (v->width * 9) / 16;
    v->is_playing = 0;
    v->curr_frame = 0;
    v->total_frames = 900;     /* 90 seconds @ 10fps */
    v->duration_sec = 90;
    v->curr_time_sec = 0;
    v->volume = 80;
    v->accent_color = 0x007AFF;

    return v;
}

void html5_media_destroy_video_ctx(html5_video_ctx_t *ctx) {
    if (!ctx) return;
    ctx->is_playing = 0;
    ctx->src[0] = 0;
}

void html5_media_render_video(html5_video_ctx_t *ctx, int x, int y, int w, int h) {
    if (!ctx) return;

    ctx->bounds = (rect_t){x, y, w, h, 0};

    /* Outer Video Container */
    fb_round_rect(x, y, w, h, 6, 0x0A0A10);
    fb_round_rect_border(x, y, w, h, 6, 0x2A2A3A);

    int ctrl_h = ctx->has_controls ? 32 : 0;
    int canvas_h = h - ctrl_h;

    /* Video Frame Display */
    if (canvas_h > 20) {
        fb_rect(x + 2, y + 2, w - 4, canvas_h - 4, 0x12121A);

        if (ctx->is_playing) {
            /* Animated Video Playback Synthesis */
            int anim_phase = (ctx->curr_frame * 4) % (w - 40);
            fb_rect(x + 20 + anim_phase, y + 20, 24, canvas_h - 40, 0x007AFF);
            fb_rect(x + w - 44 - anim_phase, y + 30, 16, canvas_h - 60, 0x30D158);

            /* Center Watermark & Live HUD */
            font_text(x + w/2 - 50, y + canvas_h/2 - 8, "FIRSTOS 4K", 0x30D158, 1);
            font_text(x + 16, y + 14, "HTML5 PLAYING", 0xFFFFFF, 1);
        } else {
            /* Paused / Poster State: Large Center Play Button */
            int pb_r = 28;
            int pb_cx = x + w / 2;
            int pb_cy = y + canvas_h / 2;

            fb_round_rect(pb_cx - pb_r, pb_cy - pb_r, pb_r * 2, pb_r * 2, pb_r, 0xCC0000);
            font_text(pb_cx - 6, pb_cy - 8, ">", 0xFFFFFF, 2);

            /* Title Overlay */
            font_text(x + 16, y + 14, "HTML5 VIDEO PLAYER", 0xDFDFE5, 1);
            if (ctx->src[0]) {
                font_text(x + 16, y + 30, ctx->src, 0x8E8E93, 1);
            }
        }
    }

    /* Bottom Control Bar */
    if (ctx->has_controls && ctrl_h > 0) {
        int cy = y + h - ctrl_h;
        fb_rect(x, cy, w, ctrl_h, 0x161620);
        fb_rect(x, cy, w, 1, 0x2E2E40);

        /* Play / Pause Button */
        ctx->play_btn_bounds = (rect_t){x + 8, cy + 4, 28, 24, 0};
        fb_round_rect(x + 8, cy + 4, 28, 24, 4, 0x242432);
        if (ctx->is_playing) {
            font_text(x + 16, cy + 8, "||", 0xFFFFFF, 1);
        } else {
            font_text(x + 18, cy + 8, ">", 0xFFFFFF, 1);
        }

        /* Timeline Scrubber Bar */
        int scrub_x = x + 44;
        int scrub_w = w - 170;
        if (scrub_w < 60) scrub_w = 60;
        ctx->scrub_bounds = (rect_t){scrub_x, cy + 12, scrub_w, 8, 0};

        fb_round_rect(scrub_x, cy + 12, scrub_w, 8, 4, 0x333344);
        int prog_w = (ctx->duration_sec > 0) ? (scrub_w * ctx->curr_time_sec) / ctx->duration_sec : 0;
        if (prog_w > scrub_w) prog_w = scrub_w;
        if (prog_w > 0) {
            fb_round_rect(scrub_x, cy + 12, prog_w, 8, 4, 0xFF0000);
            fb_round_rect(scrub_x + prog_w - 4, cy + 10, 8, 12, 4, 0xFFFFFF);
        }

        /* Timecode: MM:SS / MM:SS */
        int tc_x = scrub_x + scrub_w + 10;
        char tc_buf[32];
        sprintf(tc_buf, "%02d:%02d/%02d:%02d",
                ctx->curr_time_sec / 60, ctx->curr_time_sec % 60,
                ctx->duration_sec / 60, ctx->duration_sec % 60);
        font_text(tc_x, cy + 9, tc_buf, 0xAAAAAA, 1);

        /* Fullscreen button */
        ctx->fs_btn_bounds = (rect_t){x + w - 30, cy + 6, 22, 20, 0};
        font_text(x + w - 26, cy + 8, "[]", 0x888888, 1);
    }
}

int html5_media_handle_click(html5_video_ctx_t *ctx, int mx, int my) {
    if (!ctx) return 0;

    /* Check play/pause button or main canvas click */
    if (point_in_rect(mx, my, ctx->play_btn_bounds) ||
        (point_in_rect(mx, my, ctx->bounds) && my < ctx->bounds.y + ctx->bounds.h - 32)) {
        ctx->is_playing = !ctx->is_playing;
        serial_write("[html5_media] Video play/pause toggled: ");
        serial_write(ctx->is_playing ? "PLAYING\n" : "PAUSED\n");
        return 1;
    }

    /* Check scrubber click to seek */
    if (point_in_rect(mx, my, ctx->scrub_bounds)) {
        int rel_x = mx - ctx->scrub_bounds.x;
        if (ctx->scrub_bounds.w > 0) {
            ctx->curr_time_sec = (rel_x * ctx->duration_sec) / ctx->scrub_bounds.w;
            if (ctx->curr_time_sec < 0) ctx->curr_time_sec = 0;
            if (ctx->curr_time_sec > ctx->duration_sec) ctx->curr_time_sec = ctx->duration_sec;
            ctx->curr_frame = ctx->curr_time_sec * 10;
        }
        return 1;
    }

    return 0;
}

void html5_media_tick(void) {
    for (int i = 0; i < g_video_count; i++) {
        html5_video_ctx_t *v = &g_videos[i];
        if (v->is_playing) {
            v->curr_frame++;
            if (v->curr_frame >= v->total_frames) {
                v->curr_frame = 0; /* Loop video */
            }
            v->curr_time_sec = v->curr_frame / 10;

            extern int needs_redraw;
            extern int gui_window_dirty_only;
            needs_redraw = 1;
            gui_window_dirty_only = 1;
        }
    }
}
