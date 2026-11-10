#ifndef SOUND_H
#define SOUND_H

typedef enum {
    SOUND_EVENT_CLICK = 0,
    SOUND_EVENT_DRAG_DROP,
    SOUND_EVENT_SHUTDOWN,
    SOUND_EVENT_LOGOUT,
    SOUND_EVENT_NOTIFICATION,
    SOUND_EVENT_DIALOG_POPUP,
    SOUND_EVENT_COUNT
} sound_event_t;

void sound_init(void);
void sound_play_event(sound_event_t event);
int  sound_play_file(const char *path);
void sound_tick(void);
void sound_wait_finished(int max_ms);
int  decode_mp3_to_pcm(const unsigned char *mp3_data, int mp3_len, short *out_pcm, int max_shorts, int *out_hz, int *out_channels);

#endif
