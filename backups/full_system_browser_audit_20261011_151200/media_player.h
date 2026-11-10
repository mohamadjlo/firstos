#ifndef MEDIA_PLAYER_H
#define MEDIA_PLAYER_H

#include "gui_internal.h"

typedef enum {
    MEDIA_TYPE_NONE = 0,
    MEDIA_TYPE_AUDIO,
    MEDIA_TYPE_VIDEO
} media_type_t;

typedef enum {
    STATE_STOPPED = 0,
    STATE_PLAYING,
    STATE_PAUSED
} media_state_t;

typedef media_state_t media_play_state_t;
#define MEDIA_STATE_STOPPED STATE_STOPPED
#define MEDIA_STATE_PLAYING STATE_PLAYING
#define MEDIA_STATE_PAUSED  STATE_PAUSED

typedef struct {
    media_state_t state;
    int volume; // 0-100
    int current_frame;
    int total_frames;
    int is_muted;
} media_context_t;

extern media_context_t active_media;

/* Playlist item structure */
typedef struct {
    char path[128];
    char title[64];
    int is_video;       /* 0 = Audio (.wav), 1 = Video (.avi) */
    int total_frames;   /* Total frames for video */
    int duration_sec;   /* Duration in seconds */
} media_playlist_item_t;

#define MEDIA_MAX_PLAYLIST 32

/* Context menu options */
enum {
    MEDIA_CTX_PLAY_NOW = 0,
    MEDIA_CTX_ADD,
    MEDIA_CTX_REMOVE,
    MEDIA_CTX_RENAME,
    MEDIA_CTX_DELETE,
    MEDIA_CTX_COUNT
};

/* Core Lifecycle & Rendering */
void media_player_init(void);
void media_player_draw(app_window_t *app);
void media_player_handle_click(app_window_t *app, int mx, int my);
void media_player_handle_right_click(app_window_t *app, int mx, int my);
void media_player_handle_key(int key);
void media_player_tick(void);

/* Transport Controls */
void media_player_play(void);
void media_player_pause(void);
void media_player_stop(void);
void media_player_rewind(void);
void media_player_prev(void);
void media_player_next(void);
void media_player_seek(int delta_percent);
void media_player_seek_to(int percent);
void media_player_set_volume(int delta);
void media_player_toggle_mute(void);

/* File, Playlist & Import */
int  media_player_open_file(const char *path);
int  media_player_import_file(const char *path);
void media_player_toggle_playlist(void);
void media_player_close(void);
void media_player_open_import_dialog(void);

/* App Menu dispatcher */
void media_player_handle_menu(int menu, int item);

/* State queries */
int  media_player_is_active_playing(void);
const char *media_player_get_current_title(void);

/* Legacy stubs */
void media_play(void);
void media_pause(void);
void media_stop(void);
void media_seek(int delta_frames);

#endif /* MEDIA_PLAYER_H */
