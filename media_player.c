#include "media_player.h"
#include "fs.h"
#include "heap.h"
#include "image.h"
#include "hda.h"
#include "font.h"
#include "gui.h"
#include "serial.h"
#include "file_dialog.h"

/* Compatibility legacy active_media */
media_context_t active_media = {
    .state = STATE_STOPPED,
    .volume = 75,
    .current_frame = 0,
    .total_frames = 100,
    .is_muted = 0
};

/* Global playback state */
static media_type_t current_media_type = MEDIA_TYPE_NONE;
static media_play_state_t current_play_state = MEDIA_STATE_STOPPED;
static char current_media_path[128] = "";
static char current_media_title[64] = "No Media Selected";
static int current_media_duration_sec = 0;
static int current_media_elapsed_sec = 0;
static int current_media_volume = 75;
static int current_media_muted = 0;
static int saved_volume_before_mute = 75;

/* Playlist state */
static media_playlist_item_t playlist[MEDIA_MAX_PLAYLIST];
static int playlist_count = 0;
static int playlist_selected = -1;
static int playlist_playing = -1;
static int playlist_open = 1;
static int playlist_scroll = 0;
static int playlist_inited = 0;

/* Context Menu state */
static int ctx_open = 0;
static int ctx_x = 0;
static int ctx_y = 0;
static int ctx_target_idx = -1;

/* About Dialog state */
static int about_open = 0;

/* Waveform & Spectrum animation state */
static int anim_ticks = 0;
static int spectrum_peaks[24];
static int spectrum_peak_hold[24];

/* AVI Video State */
static u8 *avi_buf = 0;
static int avi_file_size = 0;
static u32 avi_frame_offsets[512];
static u32 avi_frame_sizes[512];
static int avi_total_frames = 0;
static int avi_curr_frame = 0;
static int avi_fps = 24;
static int avi_native_w = 320;
static int avi_native_h = 240;
static u32 *avi_decoded_pixels = 0;
static int avi_decoded_w = 0;
static int avi_decoded_h = 0;
static int avi_decoded_frame_idx = -1;
static int avi_last_tick = 0;

/* Interactive UI Rectangles */
static rect_t rect_canvas;
static rect_t rect_playlist;
static rect_t rect_scrubber;
static rect_t rect_vol_slider;
static rect_t btn_prev_track;
static rect_t btn_rewind;
static rect_t btn_play_pause;
static rect_t btn_stop;
static rect_t btn_ffwd;
static rect_t btn_next_track;
static rect_t btn_mute;
static rect_t btn_toggle_playlist;
static rect_t btn_add_to_playlist;
static rect_t btn_about;
static rect_t btn_about_close;

/* Utility String and Math Helpers */
static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    if (!dst || !src || max <= 0) return;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int str_eq(const char *a, const char *b) {
    int i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return 0; i++; }
    return a[i] == b[i];
}

static int str_ends_ci(const char *s, const char *suffix) {
    int sl = 0, el = 0;
    if (!s || !suffix) return 0;
    while (s[sl]) sl++;
    while (suffix[el]) el++;
    if (sl < el) return 0;
    for (int i = 0; i < el; i++) {
        char a = s[sl - el + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

static void extract_filename(const char *path, char *out, int max) {
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    const char *fn = (last_slash >= 0) ? (path + last_slash + 1) : path;
    str_copy(out, fn, max);
}

static void clean_title(const char *filename, char *out, int max) {
    int i = 0, dot = -1;
    while (filename[i] && i < max - 1) {
        if (filename[i] == '.') dot = i;
        out[i] = filename[i];
        if (out[i] == '_') out[i] = ' ';
        i++;
    }
    if (dot > 0 && dot < max) out[dot] = 0;
    else out[i] = 0;
}

static void int_to_str(int n, char *buf, int max) {
    char tmp[16];
    int i = 0, j = 0;
    if (n < 0) {
        if (max > 1) { buf[0] = '-'; j = 1; }
        n = -n;
    }
    if (n == 0) {
        if (j < max - 1) buf[j++] = '0';
        buf[j] = 0;
        return;
    }
    while (n > 0 && i < 15) { tmp[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0 && j < max - 1) buf[j++] = tmp[--i];
    buf[j] = 0;
}

static void format_time(int total_sec, char *buf, int max) {
    if (total_sec < 0) total_sec = 0;
    int m = total_sec / 60;
    int s = total_sec % 60;
    char mb[16], sb[16];
    int_to_str(m, mb, sizeof(mb));
    int_to_str(s, sb, sizeof(sb));

    int idx = 0;
    if (m < 10 && idx < max - 1) buf[idx++] = '0';
    for (int k = 0; mb[k] && idx < max - 1; k++) buf[idx++] = mb[k];
    if (idx < max - 1) buf[idx++] = ':';
    if (s < 10 && idx < max - 1) buf[idx++] = '0';
    for (int k = 0; sb[k] && idx < max - 1; k++) buf[idx++] = sb[k];
    buf[idx] = 0;
}

/* AVI Video Parser and Decoder */
static void avi_free(void) {
    if (avi_buf) { kfree(avi_buf); avi_buf = 0; }
    if (avi_decoded_pixels) { image_free((u8*)avi_decoded_pixels); avi_decoded_pixels = 0; }
    avi_total_frames = 0;
    avi_curr_frame = 0;
    avi_decoded_frame_idx = -1;
    avi_decoded_w = 0;
    avi_decoded_h = 0;
}

static int avi_open(const char *path) {
    avi_free();
    int sz = 0;
    int total_fs = fs_count();
    for (int i = 0; i < total_fs; i++) {
        const fs_entry_t *e = fs_entry_at(i);
        if (e && str_eq(e->path, path)) { sz = e->size; break; }
    }
    if (sz <= 0) sz = 4 * 1024 * 1024;
    avi_buf = (u8*)kmalloc(sz + 1024);
    if (!avi_buf) return -1;

    int n = fs_read_at(path, (char*)avi_buf, sz + 1024);
    if (n < 64) { avi_free(); return -1; }
    avi_file_size = n;

    /* Verify RIFF ... AVI */
    if (avi_buf[0] != 'R' || avi_buf[1] != 'I' || avi_buf[2] != 'F' || avi_buf[3] != 'F' ||
        avi_buf[8] != 'A' || avi_buf[9] != 'V' || avi_buf[10] != 'I' || avi_buf[11] != ' ') {
        avi_free();
        return -1;
    }

    /* Scan for avih and movi */
    avi_fps = 24;
    avi_native_w = 320;
    avi_native_h = 240;
    avi_total_frames = 0;

    int pos = 12;
    int movi_start = -1;
    int movi_size = 0;

    while (pos + 8 < n) {
        u32 cid = *(u32*)(avi_buf + pos);
        u32 csz = *(u32*)(avi_buf + pos + 4);
        pos += 8;
        if (cid == 0x5453494C /* 'LIST' */) {
            u32 ltype = *(u32*)(avi_buf + pos);
            if (ltype == 0x69766F6D /* 'movi' */) {
                movi_start = pos + 4;
                movi_size = csz - 4;
                break;
            }
            pos += 4;
            continue;
        } else if (cid == 0x68697661 /* 'avih' */ && csz >= 40) {
            u32 us_per_frame = *(u32*)(avi_buf + pos);
            if (us_per_frame > 0) avi_fps = 1000000 / us_per_frame;
            if (avi_fps <= 0 || avi_fps > 60) avi_fps = 24;
            avi_native_w = *(u32*)(avi_buf + pos + 32);
            avi_native_h = *(u32*)(avi_buf + pos + 36);
        }
        pos += (csz + 1) & ~1u;
    }

    if (movi_start < 0 || movi_start + movi_size > n) {
        /* Fallback: scan whole file for '00dc' or '00db' */
        movi_start = 12;
        movi_size = n - 12;
    }

    /* Index video frames in movi */
    int cur = movi_start;
    int end = movi_start + movi_size;
    while (cur + 8 < end && avi_total_frames < 512) {
        u32 cid = *(u32*)(avi_buf + cur);
        u32 csz = *(u32*)(avi_buf + cur + 4);
        cur += 8;
        /* '00dc' (video compressed/JPEG) or '00db' (video raw DIB) */
        if (cid == 0x63643030 || cid == 0x62643030 || (cid & 0x0000FFFF) == 0x00006430) {
            avi_frame_offsets[avi_total_frames] = cur;
            avi_frame_sizes[avi_total_frames] = csz;
            avi_total_frames++;
        }
        cur += (csz + 1) & ~1u;
    }

    if (avi_total_frames == 0) {
        avi_free();
        return -1;
    }

    avi_curr_frame = 0;
    /* Decode frame 0 */
    u8 *chunk = avi_buf + avi_frame_offsets[0];
    int chunk_len = avi_frame_sizes[0];
    int dw = 0, dh = 0;
    u8 *dec = image_load_from_memory(chunk, chunk_len, &dw, &dh);
    if (dec) {
        avi_decoded_pixels = (u32*)dec;
        avi_decoded_w = dw;
        avi_decoded_h = dh;
        avi_decoded_frame_idx = 0;
    }
    return 0;
}

static void avi_decode_current_frame(void) {
    if (!avi_buf || avi_curr_frame < 0 || avi_curr_frame >= avi_total_frames) return;
    if (avi_decoded_frame_idx == avi_curr_frame && avi_decoded_pixels) return;

    u8 *chunk = avi_buf + avi_frame_offsets[avi_curr_frame];
    int chunk_len = avi_frame_sizes[avi_curr_frame];
    int dw = 0, dh = 0;
    u8 *dec = image_load_from_memory(chunk, chunk_len, &dw, &dh);
    if (dec) {
        if (avi_decoded_pixels) image_free((u8*)avi_decoded_pixels);
        avi_decoded_pixels = (u32*)dec;
        avi_decoded_w = dw;
        avi_decoded_h = dh;
        avi_decoded_frame_idx = avi_curr_frame;
    }
}

/* Playlist Management */
void media_player_init(void) {
    if (playlist_inited) return;
    playlist_inited = 1;
    playlist_count = 0;
    playlist_selected = -1;
    playlist_playing = -1;
    playlist_open = 1;
    current_media_type = MEDIA_TYPE_NONE;
    current_play_state = MEDIA_STATE_STOPPED;
    current_media_path[0] = 0;
    str_copy(current_media_title, "No Media Selected", sizeof(current_media_title));
    current_media_volume = 75;
    current_media_muted = 0;

    /* Auto scan /audio and /movies */
    int total_fs = fs_count();
    for (int i = 0; i < total_fs && playlist_count < MEDIA_MAX_PLAYLIST; i++) {
        const fs_entry_t *e = fs_entry_at(i);
        if (!e || !e->path[0] || e->is_dir) continue;
        if (str_ends_ci(e->name, ".wav") || str_ends_ci(e->name, ".avi") || str_ends_ci(e->name, ".mov")) {
            str_copy(playlist[playlist_count].path, e->path, 128);
            clean_title(e->name, playlist[playlist_count].title, 64);
            playlist[playlist_count].is_video = str_ends_ci(e->name, ".avi") || str_ends_ci(e->name, ".mov");
            playlist[playlist_count].total_frames = 100;
            playlist[playlist_count].duration_sec = playlist[playlist_count].is_video ? 10 : 30;
            playlist_count++;
        }
    }
    if (playlist_count > 0) {
        playlist_selected = 0;
    }
}

int media_player_import_file(const char *path) {
    if (!path || !path[0]) return -1;
    /* Check if already in playlist */
    int found = -1;
    for (int i = 0; i < playlist_count; i++) {
        if (str_eq(playlist[i].path, path)) { found = i; break; }
    }
    if (found < 0) {
        if (playlist_count < MEDIA_MAX_PLAYLIST) {
            found = playlist_count;
            str_copy(playlist[found].path, path, 128);
            char fn[64];
            extract_filename(path, fn, sizeof(fn));
            clean_title(fn, playlist[found].title, 64);
            playlist[found].is_video = str_ends_ci(path, ".avi") || str_ends_ci(path, ".mov");
            playlist[found].total_frames = 100;
            playlist[found].duration_sec = 20;
            playlist_count++;
        } else {
            found = playlist_count - 1;
        }
    }
    playlist_selected = found;
    return media_player_open_file(path);
}

int media_player_open_file(const char *path) {
    if (!path || !path[0]) return -1;
    media_player_stop();

    str_copy(current_media_path, path, sizeof(current_media_path));
    char fn[64];
    extract_filename(path, fn, sizeof(fn));
    clean_title(fn, current_media_title, sizeof(current_media_title));

    /* Sync playlist selection */
    for (int i = 0; i < playlist_count; i++) {
        if (str_eq(playlist[i].path, path)) {
            playlist_selected = i;
            playlist_playing = i;
            break;
        }
    }

    if (str_ends_ci(path, ".avi") || str_ends_ci(path, ".mov")) {
        current_media_type = MEDIA_TYPE_VIDEO;
        if (avi_open(path) == 0) {
            current_media_duration_sec = (avi_fps > 0) ? (avi_total_frames / avi_fps) : 10;
            active_media.total_frames = avi_total_frames;
            media_player_play();
            return 0;
        } else {
            current_media_type = MEDIA_TYPE_NONE;
            return -1;
        }
    } else if (str_ends_ci(path, ".wav")) {
        current_media_type = MEDIA_TYPE_AUDIO;
        if (hda_play_audio(path) == 0) {
            u32 total_bytes = hda_get_total_length();
            current_media_duration_sec = total_bytes > 0 ? (total_bytes / (44100 * 4)) : 30;
            current_play_state = MEDIA_STATE_PLAYING;
            active_media.state = STATE_PLAYING;
            return 0;
        } else {
            current_media_type = MEDIA_TYPE_NONE;
            return -1;
        }
    }
    return -1;
}

void media_player_play(void) {
    if (current_play_state == MEDIA_STATE_PLAYING) return;
    if (current_media_type == MEDIA_TYPE_NONE) {
        if (playlist_count > 0) {
            int sel = playlist_selected >= 0 ? playlist_selected : 0;
            media_player_open_file(playlist[sel].path);
        }
        return;
    }

    if (current_media_type == MEDIA_TYPE_AUDIO) {
        if (current_play_state == MEDIA_STATE_PAUSED) {
            hda_resume_audio();
        } else {
            hda_play_audio(current_media_path);
        }
    }
    current_play_state = MEDIA_STATE_PLAYING;
    active_media.state = STATE_PLAYING;
    extern int needs_redraw; needs_redraw = 1;
}

void media_player_pause(void) {
    if (current_play_state == MEDIA_STATE_PLAYING) {
        if (current_media_type == MEDIA_TYPE_AUDIO) {
            hda_pause_audio();
        }
        current_play_state = MEDIA_STATE_PAUSED;
        active_media.state = STATE_PAUSED;
    } else if (current_play_state == MEDIA_STATE_PAUSED) {
        media_player_play();
    }
    extern int needs_redraw; needs_redraw = 1;
}

void media_player_stop(void) {
    if (current_media_type == MEDIA_TYPE_AUDIO) {
        hda_stop_audio();
    } else if (current_media_type == MEDIA_TYPE_VIDEO) {
        avi_curr_frame = 0;
        avi_decode_current_frame();
    }
    current_play_state = MEDIA_STATE_STOPPED;
    active_media.state = STATE_STOPPED;
    current_media_elapsed_sec = 0;
    extern int needs_redraw; needs_redraw = 1;
}

void media_player_rewind(void) {
    if (current_media_type == MEDIA_TYPE_AUDIO) {
        hda_seek_byte_offset(0);
    } else if (current_media_type == MEDIA_TYPE_VIDEO) {
        avi_curr_frame = 0;
        avi_decode_current_frame();
    }
    current_media_elapsed_sec = 0;
    extern int needs_redraw; needs_redraw = 1;
}

void media_player_seek_to(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (current_media_type == MEDIA_TYPE_AUDIO) {
        u32 tot = hda_get_total_length();
        if (tot > 0) {
            u32 off = (tot * percent) / 100;
            hda_seek_byte_offset(off);
        }
    } else if (current_media_type == MEDIA_TYPE_VIDEO && avi_total_frames > 0) {
        avi_curr_frame = (avi_total_frames * percent) / 100;
        if (avi_curr_frame >= avi_total_frames) avi_curr_frame = avi_total_frames - 1;
        avi_decode_current_frame();
    }
    extern int needs_redraw; needs_redraw = 1;
}

void media_player_seek(int delta_pct) {
    int cur_pct = 0;
    if (current_media_type == MEDIA_TYPE_AUDIO) {
        u32 cur = hda_get_current_offset();
        u32 tot = hda_get_total_length();
        if (tot > 0) cur_pct = (cur * 100) / tot;
    } else if (current_media_type == MEDIA_TYPE_VIDEO && avi_total_frames > 0) {
        cur_pct = (avi_curr_frame * 100) / avi_total_frames;
    }
    media_player_seek_to(cur_pct + delta_pct);
}

void media_player_prev(void) {
    if (playlist_count <= 0) return;
    int idx = (playlist_selected > 0) ? (playlist_selected - 1) : (playlist_count - 1);
    playlist_selected = idx;
    media_player_open_file(playlist[idx].path);
}

void media_player_next(void) {
    if (playlist_count <= 0) return;
    int idx = (playlist_selected + 1 < playlist_count) ? (playlist_selected + 1) : 0;
    playlist_selected = idx;
    media_player_open_file(playlist[idx].path);
}

void media_player_set_volume(int delta) {
    current_media_volume += delta;
    if (current_media_volume < 0) current_media_volume = 0;
    if (current_media_volume > 100) current_media_volume = 100;
    current_media_muted = 0;
    hda_set_volume(current_media_volume);
    active_media.volume = current_media_volume;
    extern int needs_redraw; needs_redraw = 1;
}

void media_player_toggle_mute(void) {
    if (current_media_muted) {
        current_media_volume = saved_volume_before_mute;
        current_media_muted = 0;
    } else {
        saved_volume_before_mute = current_media_volume;
        current_media_volume = 0;
        current_media_muted = 1;
    }
    hda_set_volume(current_media_volume);
    active_media.is_muted = current_media_muted;
    extern int needs_redraw; needs_redraw = 1;
}

void media_player_toggle_playlist(void) {
    playlist_open = !playlist_open;
    extern int needs_redraw; needs_redraw = 1;
}

void media_player_close(void) {
    media_player_stop();
    avi_free();
    app_window_t *app = app_slot(APP_MEDIA_PLAYER);
    if (app) app->open = 0;
    extern int needs_redraw; needs_redraw = 1;
}

void media_player_open_import_dialog(void) {
    file_dialog_show(APP_MEDIA_PLAYER, FILE_DIALOG_IMPORT, "", "/Audio");
    extern int needs_redraw; needs_redraw = 1;
}

void media_player_handle_menu(int menu, int item) {
    /* menu: 0 = App, 1 = File, 2 = Play, 3 = Help */
    if (menu == 1) { /* File */
        if (item == 0) media_player_open_import_dialog();
        else if (item == 1) media_player_close();
    } else if (menu == 2) { /* Play */
        if (item == 0) media_player_play();
        else if (item == 1) media_player_stop();
        else if (item == 2) media_player_pause();
        else if (item == 3) media_player_rewind();
    } else if (menu == 3 || menu == 0) { /* Help / App */
        about_open = !about_open;
        extern int needs_redraw; needs_redraw = 1;
    }
}

int media_player_is_active_playing(void) {
    return current_play_state == MEDIA_STATE_PLAYING;
}

const char *media_player_get_current_title(void) {
    return current_media_title;
}

/* Periodic Engine Tick */
void media_player_tick(void) {
    anim_ticks++;
    if (current_play_state == MEDIA_STATE_PLAYING) {
        if (current_media_type == MEDIA_TYPE_VIDEO) {
            int frame_interval = (avi_fps > 0) ? (100 / avi_fps) : 4;
            if (frame_interval <= 0) frame_interval = 1;
            if (anim_ticks - avi_last_tick >= frame_interval) {
                avi_last_tick = anim_ticks;
                avi_curr_frame++;
                if (avi_curr_frame >= avi_total_frames) {
                    avi_curr_frame = 0; /* loop video */
                }
                avi_decode_current_frame();
                extern int needs_redraw; needs_redraw = 1;
            }
        } else if (current_media_type == MEDIA_TYPE_AUDIO) {
            /* Check if audio finished */
            if (!hda_is_playing() && !hda_is_paused()) {
                current_play_state = MEDIA_STATE_STOPPED;
                active_media.state = STATE_STOPPED;
            }
            /* Visualizer decay & animation */
            for (int b = 0; b < 24; b++) {
                if (spectrum_peak_hold[b] > 0) spectrum_peak_hold[b]--;
                else if (spectrum_peaks[b] > 0) spectrum_peaks[b]--;
            }
            if (anim_ticks % 2 == 0) {
                extern int needs_redraw; needs_redraw = 1;
            }
        }
    }
}

/* Helper to render scaled image inside canvas maintaining aspect ratio */
static void draw_scaled_image(const u32 *pixels, int img_w, int img_h, rect_t dest) {
    if (!pixels || img_w <= 0 || img_h <= 0 || dest.w <= 0 || dest.h <= 0) return;

    /* Compute aspect ratio preserving fit */
    int draw_w = dest.w;
    int draw_h = (img_h * dest.w) / img_w;
    if (draw_h > dest.h) {
        draw_h = dest.h;
        draw_w = (img_w * dest.h) / img_h;
    }
    int start_x = dest.x + (dest.w - draw_w) / 2;
    int start_y = dest.y + (dest.h - draw_h) / 2;

    /* Use hardware-optimized blit with damage tracking and precomputed scaling */
    fb_blit_scaled(start_x, start_y, draw_w, draw_h, pixels, img_w, img_h);
}

/* Rendering */
void media_player_draw(app_window_t *app) {
    if (!app || !app->open) return;
    media_player_init();

    int cx = app->frame.x + 8;
    int cy = app->frame.y + 36;
    int cw = app->frame.w - 16;
    int ch = app->frame.h - 44;
    if (cw < 200 || ch < 150) return;

    /* App Base Background */
    fb_rect(cx, cy, cw, ch, 0x141820);
    outline(cx, cy, cw, ch, 0x2A3240);

    /* 1. Header Toolbar (Height: 28px) */
    int tb_h = 28;
    fb_rect(cx, cy, cw, tb_h, 0x1B202C);
    fb_rect(cx, cy + tb_h - 1, cw, 1, 0x2E3646);

    /* Studio Title and Mode Badge */
    draw_label(cx + 10, cy + 8, "MEDIA STUDIO", 0x8AB4F8);
    const char *badge = (current_media_type == MEDIA_TYPE_VIDEO) ? "[VIDEO - AVI]" :
                        (current_media_type == MEDIA_TYPE_AUDIO) ? "[AUDIO - WAV]" : "[IDLE]";
    u32 badge_color = (current_media_type == MEDIA_TYPE_VIDEO) ? 0xFF8A50 :
                      (current_media_type == MEDIA_TYPE_AUDIO) ? 0x81C784 : 0x758095;
    draw_label(cx + 110, cy + 8, badge, badge_color);

    /* Header Action Buttons (About and Playlist Toggle) */
    btn_about = (rect_t){cx + cw - 26, cy + 4, 20, 20, 0};
    draw_button(btn_about, 0x252C3C, 0x353F54, "?", 0xCCD6E8);

    btn_toggle_playlist = (rect_t){cx + cw - 110, cy + 4, 78, 20, 0};
    draw_button(btn_toggle_playlist, playlist_open ? 0x243E63 : 0x252C3C, 0x354F78,
                playlist_open ? "Hide List" : "Show List", 0xE0E6F0);

    /* 2. Responsive Geometry Calculations */
    int ctrl_h = 58;
    int body_y = cy + tb_h;
    int body_h = ch - tb_h - ctrl_h;

    int pw = 0;
    if (playlist_open) {
        pw = cw > 640 ? 200 : (cw > 480 ? 170 : 140);
        if (cw - pw < 220) pw = cw - 220;
    }
    int canvas_w = cw - pw - (pw > 0 ? 4 : 0);
    rect_canvas = (rect_t){cx, body_y, canvas_w, body_h, 0};
    rect_playlist = (rect_t){cx + canvas_w + 4, body_y, pw, body_h, 0};

    /* 3. Canvas Viewport */
    fb_rect(rect_canvas.x, rect_canvas.y, rect_canvas.w, rect_canvas.h, 0x0C0E14);
    outline(rect_canvas.x, rect_canvas.y, rect_canvas.w, rect_canvas.h, 0x222834);

    if (current_media_type == MEDIA_TYPE_VIDEO && avi_decoded_pixels) {
        draw_scaled_image(avi_decoded_pixels, avi_decoded_w, avi_decoded_h, rect_canvas);
        /* Subtle On-Screen-Display Badge */
        if (current_play_state == MEDIA_STATE_PAUSED) {
            int pw_card = 80, ph_card = 28;
            int px = rect_canvas.x + (rect_canvas.w - pw_card) / 2;
            int py = rect_canvas.y + (rect_canvas.h - ph_card) / 2;
            fb_rect(px, py, pw_card, ph_card, 0x1A202C);
            outline(px, py, pw_card, ph_card, 0x4D5C76);
            font_text_fit_center(px, py + 8, "PAUSED", 0xFFFFFF, pw_card);
        }
    } else if (current_media_type == MEDIA_TYPE_AUDIO) {
        /* Visualizer Studio Viewport */
        int vcx = rect_canvas.x;
        int vcy = rect_canvas.y;
        int vcw = rect_canvas.w;
        int vch = rect_canvas.h;

        /* Vinyl Disc Artwork Centerpiece */
        int disc_r = vch > 180 ? 42 : 30;
        int disc_x = vcx + vcw / 2;
        int disc_y = vcy + vch / 3;

        /* Disc rings */
        for (int r = disc_r; r >= disc_r - 20 && r > 4; r -= 4) {
            for (int a = 0; a < 360; a += 12) {
                int px = disc_x + (r * (a - 180)) / 180;
                int py = disc_y + (r * ((a % 90) - 45)) / 90;
                if (px >= vcx && px < vcx + vcw && py >= vcy && py < vcy + vch) {
                    fb_putpixel(px, py, (r == disc_r) ? 0x334055 : 0x1C2430);
                }
            }
        }
        /* Center Vinyl Label */
        fb_rect(disc_x - 14, disc_y - 14, 28, 28, 0x1A73E8);
        outline(disc_x - 14, disc_y - 14, 28, 28, 0x8AB4F8);
        font_icon(disc_x - 8, disc_y - 8, ICON_DISK, 0xFFFFFF);

        /* Real-time Oscilloscope Wave */
        short audio_samples[128];
        int sample_count = hda_get_pcm_samples(audio_samples, 128);
        int wave_y = vcy + (vch * 60) / 100;
        int wave_h = vch / 4;
        if (wave_h < 16) wave_h = 16;

        /* Horizontal Guide Line */
        fb_rect(vcx + 10, wave_y, vcw - 20, 1, 0x182638);

        for (int x = 2; x < vcw - 2; x++) {
            int s_idx = (x * 128) / vcw;
            int s_val = (sample_count > 0 && current_play_state == MEDIA_STATE_PLAYING) ? audio_samples[s_idx] : 0;
            /* Synthetic subtle idle harmonic if stopped/paused */
            if (current_play_state != MEDIA_STATE_PLAYING) {
                int phase = (x * 8 + anim_ticks * 3) % 360;
                s_val = (phase < 180 ? (phase - 90) : (270 - phase)) * 80;
            }
            int dy = (s_val * wave_h) / 32768;
            int py = wave_y - dy;
            if (py >= vcy && py < vcy + vch) {
                fb_putpixel(vcx + x, py, 0x00E5FF);
                if (py + 1 < vcy + vch) fb_putpixel(vcx + x, py + 1, 0x0099CC);
            }
        }

        /* 24-Band Frequency Equalizer Spectrum Bars */
        int eq_bars = 24;
        int bar_gap = 2;
        int bar_w = (vcw - 40 - (eq_bars * bar_gap)) / eq_bars;
        if (bar_w < 2) bar_w = 2;
        int eq_base_y = vcy + vch - 14;
        int max_bar_h = vch / 3;

        for (int b = 0; b < eq_bars; b++) {
            int bx = vcx + 20 + b * (bar_w + bar_gap);
            int mag = 0;
            if (current_play_state == MEDIA_STATE_PLAYING && sample_count > 0) {
                int si = (b * 128) / eq_bars;
                int v = audio_samples[si];
                if (v < 0) v = -v;
                mag = (v * max_bar_h) / 32768;
                /* Dynamic Harmonic Phase */
                int harmonic = ((anim_ticks * 4 + b * 25) % 100);
                mag = (mag * (70 + harmonic / 3)) / 100;
            } else if (current_play_state == MEDIA_STATE_PAUSED) {
                mag = (max_bar_h * ((b * 17) % 30)) / 100;
            }
            if (mag > max_bar_h) mag = max_bar_h;
            if (mag > spectrum_peaks[b]) {
                spectrum_peaks[b] = mag;
                spectrum_peak_hold[b] = 8;
            }

            /* Draw Equalizer Bar Columns with Color Gradients */
            for (int bh = 0; bh < mag; bh++) {
                u32 col = (bh > (max_bar_h * 75) / 100) ? 0xFF5252 :
                          (bh > (max_bar_h * 45) / 100) ? 0xFFD740 : 0x00E676;
                fb_rect(bx, eq_base_y - bh, bar_w, 1, col);
            }
            /* Falling Peak Indicator */
            if (spectrum_peaks[b] > 0) {
                int peak_y = eq_base_y - spectrum_peaks[b];
                fb_rect(bx, peak_y, bar_w, 2, 0xFFFFFF);
            }
        }

        /* Current Track Title & Metadata */
        font_text_fit_center(vcx + 10, vcy + 14, current_media_title, 0xFFFFFF, vcw - 20);
        char meta_info[64];
        meta_info[0] = 0;
        int mpos = 0;
        const char *mfmt = "PCM Stereo 44.1kHz 16-Bit";
        while (mfmt[mpos]) { meta_info[mpos] = mfmt[mpos]; mpos++; }
        meta_info[mpos] = 0;
        font_text_fit_center(vcx + 10, vcy + 30, meta_info, 0x8AB4F8, vcw - 20);
    } else {
        /* Blank/Idle Viewport */
        font_text_fit_center(rect_canvas.x, rect_canvas.y + rect_canvas.h / 2 - 16,
                             "NO MEDIA PLAYING", 0x566275, rect_canvas.w);
        font_text_fit_center(rect_canvas.x, rect_canvas.y + rect_canvas.h / 2 + 6,
                             "Select a track from the playlist or use File -> Open", 0x3E4857, rect_canvas.w);
    }

    /* 4. Playlist Sidebar Viewport (if visible) */
    if (playlist_open && pw > 0) {
        fb_rect(rect_playlist.x, rect_playlist.y, rect_playlist.w, rect_playlist.h, 0x161A23);
        outline(rect_playlist.x, rect_playlist.y, rect_playlist.w, rect_playlist.h, 0x28303F);

        /* Playlist Header */
        fb_rect(rect_playlist.x, rect_playlist.y, rect_playlist.w, 24, 0x1D2330);
        char pl_title[32];
        char count_str[8];
        int_to_str(playlist_count, count_str, 8);
        str_copy(pl_title, "PLAYLIST (", sizeof(pl_title));
        int pidx = 0; while (pl_title[pidx]) pidx++;
        int cidx = 0; while (count_str[cidx] && pidx < 28) pl_title[pidx++] = count_str[cidx++];
        pl_title[pidx++] = ')'; pl_title[pidx] = 0;
        draw_label(rect_playlist.x + 8, rect_playlist.y + 6, pl_title, 0xCCD6E8);

        /* Add [+] Button */
        btn_add_to_playlist = (rect_t){rect_playlist.x + rect_playlist.w - 22, rect_playlist.y + 3, 18, 18, 0};
        draw_button(btn_add_to_playlist, 0x2A3446, 0x3B4A62, "+", 0x8AB4F8);

        /* Playlist Entries */
        int row_h = 24;
        int list_y = rect_playlist.y + 26;
        int max_visible = (rect_playlist.h - 30) / row_h;

        for (int i = 0; i < max_visible && (i + playlist_scroll) < playlist_count; i++) {
            int item_idx = i + playlist_scroll;
            int ry = list_y + i * row_h;
            int is_sel = (item_idx == playlist_selected);
            int is_ply = (item_idx == playlist_playing);

            u32 row_bg = is_sel ? 0x243E63 : ((i % 2 == 0) ? 0x181D27 : 0x161A23);
            fb_rect(rect_playlist.x + 2, ry, rect_playlist.w - 4, row_h - 1, row_bg);

            /* Format Badge */
            const char *fbadge = playlist[item_idx].is_video ? "VID" : "AUD";
            u32 fcolor = playlist[item_idx].is_video ? 0xFFAB91 : 0xA5D6A7;
            draw_label(rect_playlist.x + 6, ry + 5, fbadge, fcolor);

            /* Track Title */
            u32 text_col = is_ply ? 0x00E5FF : (is_sel ? 0xFFFFFF : 0xC0C8D6);
            int text_w = rect_playlist.w - 46;
            font_text_fit(rect_playlist.x + 36, ry + 5, playlist[item_idx].title, text_col, text_w);

            /* Playing indicator dot/arrow */
            if (is_ply) {
                draw_label(rect_playlist.x + rect_playlist.w - 14, ry + 5, ">", 0x00E5FF);
            }
        }
    }

    /* 5. Bottom Transport & Control Bar (Height: 58px) */
    int ctrl_y = cy + ch - ctrl_h;
    fb_rect(cx, ctrl_y, cw, ctrl_h, 0x171B24);
    fb_rect(cx, ctrl_y, cw, 1, 0x2A3240);

    /* Progress Timeline Scrubber */
    int scrubber_pad = 120;
    rect_scrubber = (rect_t){cx + 10, ctrl_y + 8, cw - scrubber_pad, 10, 0};
    fb_rect(rect_scrubber.x, rect_scrubber.y, rect_scrubber.w, rect_scrubber.h, 0x242B38);
    outline(rect_scrubber.x, rect_scrubber.y, rect_scrubber.w, rect_scrubber.h, 0x3A4456);

    /* Calculate Progress % */
    int cur_progress_pct = 0;
    int elapsed_sec = 0;
    int total_sec = current_media_duration_sec > 0 ? current_media_duration_sec : 1;

    if (current_media_type == MEDIA_TYPE_AUDIO) {
        u32 cur_bytes = hda_get_current_offset();
        u32 tot_bytes = hda_get_total_length();
        if (tot_bytes > 0) {
            cur_progress_pct = (cur_bytes * 100) / tot_bytes;
            elapsed_sec = cur_bytes / (44100 * 4);
        }
    } else if (current_media_type == MEDIA_TYPE_VIDEO && avi_total_frames > 0) {
        cur_progress_pct = (avi_curr_frame * 100) / avi_total_frames;
        elapsed_sec = (avi_fps > 0) ? (avi_curr_frame / avi_fps) : 0;
    }

    /* Fill Bar */
    int fill_w = (rect_scrubber.w * cur_progress_pct) / 100;
    if (fill_w > 0) {
        fb_rect(rect_scrubber.x, rect_scrubber.y, fill_w, rect_scrubber.h, 0x1A73E8);
    }
    /* Scrubber Thumb */
    int thumb_x = rect_scrubber.x + fill_w - 3;
    if (thumb_x < rect_scrubber.x) thumb_x = rect_scrubber.x;
    if (thumb_x > rect_scrubber.x + rect_scrubber.w - 6) thumb_x = rect_scrubber.x + rect_scrubber.w - 6;
    fb_rect(thumb_x, rect_scrubber.y - 2, 6, rect_scrubber.h + 4, 0xFFFFFF);

    /* Time Stamp (Elapsed / Total) */
    char t_elapsed[16], t_total[16], time_display[36];
    format_time(elapsed_sec, t_elapsed, sizeof(t_elapsed));
    format_time(total_sec, t_total, sizeof(t_total));
    str_copy(time_display, t_elapsed, sizeof(time_display));
    int td_pos = 0; while (time_display[td_pos]) td_pos++;
    time_display[td_pos++] = ' '; time_display[td_pos++] = '/'; time_display[td_pos++] = ' ';
    for (int k = 0; t_total[k] && td_pos < 34; k++) time_display[td_pos++] = t_total[k];
    time_display[td_pos] = 0;
    draw_label(cx + cw - scrubber_pad + 16, ctrl_y + 8, time_display, 0xA8B4C8);

    /* Transport Buttons */
    int btn_y = ctrl_y + 24;
    btn_prev_track = (rect_t){cx + 10, btn_y, 26, 24, 0};
    draw_button(btn_prev_track, 0x262D3B, 0x363F52, "|<", 0xCCD6E8);

    btn_rewind = (rect_t){cx + 40, btn_y, 26, 24, 0};
    draw_button(btn_rewind, 0x262D3B, 0x363F52, "<<", 0xCCD6E8);

    btn_play_pause = (rect_t){cx + 70, btn_y, 40, 24, 0};
    const char *play_lbl = (current_play_state == MEDIA_STATE_PLAYING) ? " || " : " |> ";
    draw_button(btn_play_pause, 0x1A73E8, 0x2A85F8, play_lbl, 0xFFFFFF);

    btn_stop = (rect_t){cx + 114, btn_y, 26, 24, 0};
    draw_button(btn_stop, 0x262D3B, 0x363F52, "[]", 0xCCD6E8);

    btn_ffwd = (rect_t){cx + 144, btn_y, 26, 24, 0};
    draw_button(btn_ffwd, 0x262D3B, 0x363F52, ">>", 0xCCD6E8);

    btn_next_track = (rect_t){cx + 174, btn_y, 26, 24, 0};
    draw_button(btn_next_track, 0x262D3B, 0x363F52, ">|", 0xCCD6E8);

    /* Volume Slider and Mute Toggle */
    btn_mute = (rect_t){cx + cw - 180, btn_y, 26, 24, 0};
    draw_button(btn_mute, 0x262D3B, 0x363F52, current_media_muted ? "M" : "V", current_media_muted ? 0xFF5252 : 0x8AB4F8);

    rect_vol_slider = (rect_t){cx + cw - 148, btn_y + 8, 80, 8, 0};
    fb_rect(rect_vol_slider.x, rect_vol_slider.y, rect_vol_slider.w, rect_vol_slider.h, 0x242B38);
    outline(rect_vol_slider.x, rect_vol_slider.y, rect_vol_slider.w, rect_vol_slider.h, 0x3A4456);

    int v_fill = (rect_vol_slider.w * current_media_volume) / 100;
    if (v_fill > 0) {
        fb_rect(rect_vol_slider.x, rect_vol_slider.y, v_fill, rect_vol_slider.h, current_media_muted ? 0x758095 : 0x00E676);
    }
    char vol_pct_str[8];
    int_to_str(current_media_volume, vol_pct_str, 8);
    int vl = 0; while (vol_pct_str[vl]) vl++;
    vol_pct_str[vl++] = '%'; vol_pct_str[vl] = 0;
    draw_label(cx + cw - 60, btn_y + 6, vol_pct_str, 0xA8B4C8);

    /* 6. Context Menu Overlay (if opened on playlist item) */
    if (ctx_open && ctx_target_idx >= 0 && ctx_target_idx < playlist_count) {
        int mw = 148;
        int mh = MEDIA_CTX_COUNT * 24 + 8;
        int mx = ctx_x;
        int my = ctx_y;
        if (mx + mw > (int)fb.width - 4) mx = (int)fb.width - mw - 4;
        if (my + mh > (int)fb.height - 30) my = (int)fb.height - mh - 30;

        /* Shadow & Body */
        fb_rect(mx + 3, my + 3, mw, mh, 0x080A0E);
        fb_rect(mx, my, mw, mh, 0x1E2430);
        outline(mx, my, mw, mh, 0x424E64);

        const char *ctx_labels[MEDIA_CTX_COUNT] = {
            "Play it now",
            "Add to playlist",
            "Remove from playlist",
            "Rename",
            "Delete file"
        };
        u32 ctx_colors[MEDIA_CTX_COUNT] = {
            0x00E5FF, 0x8AB4F8, 0xD0D8E4, 0xD0D8E4, 0xFF7070
        };

        for (int c = 0; c < MEDIA_CTX_COUNT; c++) {
            int cy_row = my + 4 + c * 24;
            rect_t row_r = (rect_t){mx + 4, cy_row, mw - 8, 22, 0};
            draw_button(row_r, 0x1E2430, 0x2A3446, ctx_labels[c], ctx_colors[c]);
        }
    }

    /* 7. About Modal Dialog (if active) */
    if (about_open) {
        int aw = 380;
        int ah = 220;
        int ax = cx + (cw - aw) / 2;
        int ay = cy + (ch - ah) / 2;

        /* Dim overlay */
        fb_rect(ax - 4, ay - 4, aw + 8, ah + 8, 0x0A0D12);
        fb_rect(ax, ay, aw, ah, 0x1B202C);
        outline(ax, ay, aw, ah, 0x3E4E68);

        /* Dialog Header */
        fb_rect(ax, ay, aw, 30, 0x252C3C);
        draw_label(ax + 14, ay + 8, "About FirstOS Media Studio", 0x8AB4F8);

        /* Info Lines */
        draw_label(ax + 20, ay + 44, "FirstOS Media Player V2.0", 0xFFFFFF);
        draw_label(ax + 20, ay + 66, "High-Fidelity Audio & Movie Player for FirstOS", 0xA0ACBE);
        draw_label(ax + 20, ay + 92, "* Audio: 44.1kHz 16-Bit Stereo (Intel HDA)", 0x81C784);
        draw_label(ax + 20, ay + 112, "* Video: RIFF AVI Motion-JPEG & DIB Stream Decoder", 0xFFAB91);
        draw_label(ax + 20, ay + 132, "* Visualizer: Real-Time Oscilloscope & 24-Band EQ", 0x00E5FF);
        draw_label(ax + 20, ay + 152, "* Shortcuts: Space (Play/Pause), Left/Right (Seek)", 0xCCD6E8);

        btn_about_close = (rect_t){ax + aw / 2 - 40, ay + ah - 34, 80, 22, 0};
        draw_button(btn_about_close, 0x1A73E8, 0x2A85F8, "OK", 0xFFFFFF);
    }
}

/* Mouse Clicks & Interaction */
void media_player_handle_click(app_window_t *app, int mx, int my) {
    if (!app || !app->open) return;

    /* Handle About Dialog clicks */
    if (about_open) {
        if (point_in_rect(mx, my, btn_about_close)) {
            about_open = 0;
            extern int needs_redraw; needs_redraw = 1;
        }
        return;
    }

    /* Handle Context Menu Clicks */
    if (ctx_open) {
        int mw = 148;
        int mh = MEDIA_CTX_COUNT * 24 + 8;
        int cmx = ctx_x;
        int cmy = ctx_y;
        if (cmx + mw > (int)fb.width - 4) cmx = (int)fb.width - mw - 4;
        if (cmy + mh > (int)fb.height - 30) cmy = (int)fb.height - mh - 30;

        if (mx >= cmx && mx < cmx + mw && my >= cmy && my < cmy + mh) {
            int sel_opt = (my - cmy - 4) / 24;
            int target = ctx_target_idx;
            ctx_open = 0;

            if (target >= 0 && target < playlist_count) {
                if (sel_opt == MEDIA_CTX_PLAY_NOW) {
                    playlist_selected = target;
                    media_player_open_file(playlist[target].path);
                } else if (sel_opt == MEDIA_CTX_ADD) {
                    media_player_open_import_dialog();
                } else if (sel_opt == MEDIA_CTX_REMOVE) {
                    for (int k = target; k < playlist_count - 1; k++) {
                        playlist[k] = playlist[k + 1];
                    }
                    playlist_count--;
                    if (playlist_selected >= playlist_count) playlist_selected = playlist_count - 1;
                } else if (sel_opt == MEDIA_CTX_RENAME) {
                    /* Append (Renamed) to title */
                    int tlen = 0; while (playlist[target].title[tlen]) tlen++;
                    if (tlen < 50) {
                        const char *tag = " *";
                        int k = 0; while (tag[k]) playlist[target].title[tlen++] = tag[k++];
                        playlist[target].title[tlen] = 0;
                    }
                } else if (sel_opt == MEDIA_CTX_DELETE) {
                    fs_delete(playlist[target].path);
                    for (int k = target; k < playlist_count - 1; k++) {
                        playlist[k] = playlist[k + 1];
                    }
                    playlist_count--;
                    if (playlist_selected >= playlist_count) playlist_selected = playlist_count - 1;
                }
            }
            extern int needs_redraw; needs_redraw = 1;
            return;
        } else {
            ctx_open = 0;
            extern int needs_redraw; needs_redraw = 1;
        }
    }

    /* Header Controls */
    if (point_in_rect(mx, my, btn_about)) {
        about_open = 1;
        extern int needs_redraw; needs_redraw = 1;
        return;
    }
    if (point_in_rect(mx, my, btn_toggle_playlist)) {
        media_player_toggle_playlist();
        return;
    }

    /* Transport Buttons */
    if (point_in_rect(mx, my, btn_prev_track)) { media_player_prev(); return; }
    if (point_in_rect(mx, my, btn_rewind))     { media_player_seek(-5); return; }
    if (point_in_rect(mx, my, btn_play_pause)) { media_player_pause(); return; }
    if (point_in_rect(mx, my, btn_stop))       { media_player_stop(); return; }
    if (point_in_rect(mx, my, btn_ffwd))       { media_player_seek(5); return; }
    if (point_in_rect(mx, my, btn_next_track)) { media_player_next(); return; }
    if (point_in_rect(mx, my, btn_mute))       { media_player_toggle_mute(); return; }

    /* Timeline Scrubber Seek */
    if (point_in_rect(mx, my, rect_scrubber)) {
        int pct = ((mx - rect_scrubber.x) * 100) / (rect_scrubber.w > 0 ? rect_scrubber.w : 1);
        media_player_seek_to(pct);
        return;
    }

    /* Volume Slider */
    if (point_in_rect(mx, my, rect_vol_slider)) {
        int pct = ((mx - rect_vol_slider.x) * 100) / (rect_vol_slider.w > 0 ? rect_vol_slider.w : 1);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        current_media_volume = pct;
        current_media_muted = 0;
        hda_set_volume(pct);
        active_media.volume = pct;
        extern int needs_redraw; needs_redraw = 1;
        return;
    }

    /* Playlist Interaction */
    if (playlist_open && point_in_rect(mx, my, rect_playlist)) {
        if (point_in_rect(mx, my, btn_add_to_playlist)) {
            media_player_open_import_dialog();
            return;
        }
        int list_y = rect_playlist.y + 26;
        int row_h = 24;
        if (my >= list_y) {
            int clicked_slot = (my - list_y) / row_h;
            int item_idx = clicked_slot + playlist_scroll;
            if (item_idx >= 0 && item_idx < playlist_count) {
                if (playlist_selected == item_idx) {
                    /* Double-click behavior: play immediately */
                    media_player_open_file(playlist[item_idx].path);
                } else {
                    playlist_selected = item_idx;
                }
                extern int needs_redraw; needs_redraw = 1;
            }
        }
        return;
    }

    /* Canvas Click (Toggle Play/Pause) */
    if (point_in_rect(mx, my, rect_canvas)) {
        media_player_pause();
        return;
    }
}

/* Right-Click Context Menu Trigger */
void media_player_handle_right_click(app_window_t *app, int mx, int my) {
    if (!app || !app->open) return;
    if (playlist_open && point_in_rect(mx, my, rect_playlist)) {
        int list_y = rect_playlist.y + 26;
        int row_h = 24;
        if (my >= list_y) {
            int clicked_slot = (my - list_y) / row_h;
            int item_idx = clicked_slot + playlist_scroll;
            if (item_idx >= 0 && item_idx < playlist_count) {
                playlist_selected = item_idx;
                ctx_open = 1;
                ctx_x = mx;
                ctx_y = my;
                ctx_target_idx = item_idx;
                extern int needs_redraw; needs_redraw = 1;
            }
        }
    }
}

/* Keyboard Shortcuts */
void media_player_handle_key(int key) {
    if (key == ' ') {
        media_player_pause();
    } else if (key == 's' || key == 'S') {
        media_player_stop();
    } else if (key == 'r' || key == 'R') {
        media_player_rewind();
    } else if (key == 'm' || key == 'M') {
        media_player_toggle_mute();
    } else if (key == 'l' || key == 'L') {
        media_player_toggle_playlist();
    } else if (key == 0x11 || key == 27) { /* Escape */
        if (about_open) { about_open = 0; extern int needs_redraw; needs_redraw = 1; }
        else if (ctx_open) { ctx_open = 0; extern int needs_redraw; needs_redraw = 1; }
    }
}

/* Legacy stubs */
void media_play(void) { media_player_play(); }
void media_pause(void) { media_player_pause(); }
void media_stop(void) { media_player_stop(); }
void media_seek(int delta_frames) { media_player_seek(delta_frames > 0 ? 5 : -5); }

