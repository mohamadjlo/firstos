import sys
content = open('gui.c').read()

if '#include "photo_viewer.h"' in content and '#include "media_player.h"' not in content:
    content = content.replace('#include "photo_viewer.h"', '#include "photo_viewer.h"\n#include "media_player.h"')

idx = content.find('static void draw_media_player_contents')
end_idx = content.find('}', idx) + 1

new_draw = """static void draw_media_player_contents(app_window_t *app) {
    int cx = app->frame.x + 10;
    int cy = app->frame.y + 42;
    int cw = app->frame.w - 20;
    int ch = app->frame.h - 52;
    int art_w = 128, art_h = 128;
    int art_x = cx + (cw - art_w) / 2;
    int art_y = cy + 20;
    
    fb_rect(cx, cy, cw, ch, 0xF8F9FA);
    outline(cx, cy, cw, ch, 0xC4D0DE);
    
    // Draw album art box
    fb_rect(art_x, art_y, art_w, art_h, 0xE9ECEF);
    outline(art_x, art_y, art_w, art_h, 0xDEE2E6);
    
    // Draw big icon
    if (!audiowav_ico_loaded) { audiowav_ico_loaded = 1; load_ico_from_path("/icons/audio_wav.ico", audiowav_ico_pixels, &audiowav_ico_width, &audiowav_ico_height); }
    if (audiowav_ico_width > 0) {
        draw_icon_scaled(audiowav_ico_pixels, audiowav_ico_width, audiowav_ico_height, art_x + (art_w - 64)/2, art_y + (art_h - 64)/2, 64, 64);
    } else {
        font_icon(art_x + (art_w - 16)/2, art_y + (art_h - 16)/2, ICON_FILE, 0xADB5BD);
    }
    
    // Draw File name
    if (media_player_current_file[0]) {
        font_text_fit_center(cx, art_y + art_h + 20, media_player_current_file, 0x212529, cw);
        font_text_fit_center(cx, art_y + art_h + 40, active_media.state == STATE_PLAYING ? "Playing" : (active_media.state == STATE_PAUSED ? "Paused" : "Stopped"), 0x6C757D, cw);
    } else {
        font_text_fit_center(cx, art_y + art_h + 20, "No file selected", 0x6C757D, cw);
        font_text_fit_center(cx, art_y + art_h + 40, "Open a .wav file from Explorer", 0xADB5BD, cw);
    }
    
    // Draw Controls
    int ctrl_y = art_y + art_h + 80;
    draw_button((rect_t){cx + cw/2 - 70, ctrl_y, 30, 30}, 0xE7ECF3, 0xD0D7E2, "|<", 0x20344E); // Skip Back
    draw_button((rect_t){cx + cw/2 - 30, ctrl_y, 60, 30}, 0xE7ECF3, 0xD0D7E2, active_media.state == STATE_PLAYING ? " || " : " |> ", 0x20344E); // Play/Pause
    draw_button((rect_t){cx + cw/2 + 40, ctrl_y, 30, 30}, 0xE7ECF3, 0xD0D7E2, ">|", 0x20344E); // Skip Forward

    // Draw Volume
    draw_label(cx + cw/2 - 50, ctrl_y + 48, "Vol:", 0x555555);
    draw_progress_bar((rect_t){cx + cw/2 - 10, ctrl_y + 50, 80, 10}, active_media.volume, 0, 100);
}"""

content = content[:idx] + new_draw + content[end_idx:]
open('gui.c', 'w').write(content)
