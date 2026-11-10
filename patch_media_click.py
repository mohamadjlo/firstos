import sys
content = open('gui.c').read()

if 'void handle_media_player_click(int mx, int my)' not in content:
    idx = content.find('static void draw_media_player_contents')
    new_click = """static void handle_media_player_click(int mx, int my) {
    app_window_t *app = app_slot(APP_MEDIA_PLAYER);
    if (!app) return;
    int cx = app->frame.x + 10;
    int cy = app->frame.y + 42;
    int cw = app->frame.w - 20;
    int ch = app->frame.h - 52;
    int art_w = 128, art_h = 128;
    int art_y = cy + 20;
    int ctrl_y = art_y + art_h + 80;
    
    // Play/Pause
    if (point_in_rect(mx, my, (rect_t){cx + cw/2 - 30, ctrl_y, 60, 30})) {
        media_pause();
        extern int needs_redraw; needs_redraw = 1;
    }
    // Skip Back
    if (point_in_rect(mx, my, (rect_t){cx + cw/2 - 70, ctrl_y, 30, 30})) {
        media_seek(-100);
        extern int needs_redraw; needs_redraw = 1;
    }
    // Skip Forward
    if (point_in_rect(mx, my, (rect_t){cx + cw/2 + 40, ctrl_y, 30, 30})) {
        media_seek(100);
        extern int needs_redraw; needs_redraw = 1;
    }
    // Volume
    rect_t vol_rect = (rect_t){cx + cw/2 - 10, ctrl_y + 50, 80, 10};
    if (point_in_rect(mx, my, vol_rect)) {
        int percent = ((mx - vol_rect.x) * 100) / vol_rect.w;
        active_media.volume = percent;
        extern int needs_redraw; needs_redraw = 1;
    }
}
"""
    content = content[:idx] + new_click + content[idx:]

# Now inject it into the click loop
if 'app_window_t *media_app = app_slot(APP_MEDIA_PLAYER);' not in content:
    idx2 = content.find('app_window_t *settings_app = app_slot(APP_SETTINGS);')
    new_dispatch = """app_window_t *media_app = app_slot(APP_MEDIA_PLAYER);
        if (media_app && media_app->open && !media_app->minimized && top_hit == APP_MEDIA_PLAYER - 1) {
            if (point_in_rect(mx, my, media_app->close)) { media_app->open = 0; needs_redraw = 1; goto finish; }
            if (point_in_rect(mx, my, media_app->maximize)) { focus_app_window(APP_MEDIA_PLAYER - 1); set_app_window_maximized(media_app, !media_app->maximized); goto finish; }
            if (point_in_rect(mx, my, media_app->minimize)) { begin_window_transition(APP_MEDIA_PLAYER - 1, 1); media_app->minimized = 1; needs_redraw = 1; goto finish; }
            if (point_in_rect(mx, my, media_app->title)) {
                focus_app_window(APP_MEDIA_PLAYER - 1);
                window_dragging = 1; window_drag_index = APP_MEDIA_PLAYER - 1;
                drag_offset_x = mx - media_app->frame.x; drag_offset_y = my - media_app->frame.y;
                goto finish;
            }
            if (point_in_rect(mx, my, media_app->frame)) {
                handle_media_player_click(mx, my);
                goto finish;
            }
        }
        """
    content = content[:idx2] + new_dispatch + content[idx2:]

open('gui.c', 'w').write(content)
