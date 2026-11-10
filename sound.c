#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sound.h"
#include "hda.h"
#include "fs.h"
#include "heap.h"
#include "serial.h"
#include "cpu.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_STDIO
#include "minimp3.h"

typedef struct {
    short *samples;
    int sample_count;
    int hz;
    int channels;
    int loaded;
} sound_cache_item_t;

static sound_cache_item_t sound_cache[SOUND_EVENT_COUNT];

int decode_mp3_to_pcm(const unsigned char *mp3_data, int mp3_len, short *out_pcm, int max_shorts, int *out_hz, int *out_channels) {
    if (!mp3_data || mp3_len <= 0 || !out_pcm || max_shorts <= 0) return 0;
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    mp3dec_init(&dec);

    int offset = 0;
    int total_shorts = 0;
    short frame_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    while (offset < mp3_len) {
        int samples = mp3dec_decode_frame(&dec, mp3_data + offset, mp3_len - offset, frame_pcm, &info);
        if (info.frame_bytes <= 0) break;
        offset += info.frame_bytes;
        if (samples > 0) {
            if (out_hz) *out_hz = info.hz;
            if (out_channels) *out_channels = info.channels;
            int shorts_to_copy = samples * info.channels;
            if (total_shorts + shorts_to_copy > max_shorts) {
                shorts_to_copy = max_shorts - total_shorts;
            }
            if (shorts_to_copy > 0) {
                memcpy(out_pcm + total_shorts, frame_pcm, shorts_to_copy * sizeof(short));
                total_shorts += shorts_to_copy;
            }
            if (total_shorts >= max_shorts) break;
        }
    }
    int ch = (out_channels && *out_channels > 0) ? *out_channels : 1;
    return total_shorts / ch;
}

static void load_cache_item(sound_event_t event, const char *p1, const char *p2) {
    u8 *tmp_mp3 = (u8*)kmalloc(128 * 1024);
    if (!tmp_mp3) return;

    int n = fs_read_at(p1, (char*)tmp_mp3, 128 * 1024);
    if (n <= 0 && p2) {
        n = fs_read_at(p2, (char*)tmp_mp3, 128 * 1024);
    }

    if (n > 0) {
        short *pcm = (short*)kmalloc(256 * 1024 * sizeof(short));
        if (pcm) {
            int hz = 44100, ch = 2;
            int frames = decode_mp3_to_pcm(tmp_mp3, n, pcm, 256 * 1024, &hz, &ch);
            if (frames > 0) {
                sound_cache[event].samples = pcm;
                sound_cache[event].sample_count = frames;
                sound_cache[event].hz = hz;
                sound_cache[event].channels = ch;
                sound_cache[event].loaded = 1;
                serial_write("[sound] pre-cached event ");
                serial_write_hex((u32)event);
                serial_write(" frames=");
                serial_write_hex((u32)frames);
                serial_write(" hz=");
                serial_write_hex((u32)hz);
                serial_write("\n");
            } else {
                kfree(pcm);
            }
        }
    }
    kfree(tmp_mp3);
}

void sound_init(void) {
    fpu_init();
    for (int i = 0; i < SOUND_EVENT_COUNT; i++) {
        sound_cache[i].samples = 0;
        sound_cache[i].sample_count = 0;
        sound_cache[i].hz = 44100;
        sound_cache[i].channels = 2;
        sound_cache[i].loaded = 0;
    }

    serial_write("[sound] initializing event audio cache...\n");
    load_cache_item(SOUND_EVENT_CLICK, "/audio/Click.mp3", "/audio/click.mp3");
    load_cache_item(SOUND_EVENT_DRAG_DROP, "/audio/Drag and drop.mp3", "/audio/drag and drop.mp3");
    load_cache_item(SOUND_EVENT_SHUTDOWN, "/audio/shutdown.mp3", "/audio/shutdown.mp3");

    /* Share shutdown sound for logout */
    if (sound_cache[SOUND_EVENT_SHUTDOWN].loaded) {
        sound_cache[SOUND_EVENT_LOGOUT] = sound_cache[SOUND_EVENT_SHUTDOWN];
        serial_write("[sound] pre-cached event 3 (LOGOUT -> shutdown.mp3)\n");
    } else {
        load_cache_item(SOUND_EVENT_LOGOUT, "/audio/shutdown.mp3", "/audio/shutdown.mp3");
    }

    load_cache_item(SOUND_EVENT_NOTIFICATION, "/audio/System notifications.mp3", "/audio/System notification.mp3");
    load_cache_item(SOUND_EVENT_DIALOG_POPUP, "/audio/Dialog pop up.mp3", "/audio/Dialog pop up.mp3");

    serial_write("[sound] event cache initialization complete\n");
}

void sound_play_event(sound_event_t event) {
    if (event < 0 || event >= SOUND_EVENT_COUNT) return;

    switch (event) {
        case SOUND_EVENT_CLICK:        break;
        case SOUND_EVENT_DRAG_DROP:    break;
        case SOUND_EVENT_SHUTDOWN:     serial_write("[sound] event: SHUTDOWN\n"); break;
        case SOUND_EVENT_LOGOUT:       serial_write("[sound] event: LOGOUT\n"); break;
        case SOUND_EVENT_NOTIFICATION: serial_write("[sound] event: NOTIFICATION\n"); break;
        case SOUND_EVENT_DIALOG_POPUP: serial_write("[sound] event: DIALOG_POPUP\n"); break;
        default: break;
    }

    if (sound_cache[event].loaded && sound_cache[event].samples) {
        hda_play_pcm(sound_cache[event].samples,
                     sound_cache[event].sample_count * sound_cache[event].channels,
                     sound_cache[event].hz,
                     sound_cache[event].channels);
        return;
    }

    /* Fallback on-demand load */
    const char *p = 0;
    if (event == SOUND_EVENT_CLICK) p = "/audio/Click.mp3";
    else if (event == SOUND_EVENT_DRAG_DROP) p = "/audio/Drag and drop.mp3";
    else if (event == SOUND_EVENT_SHUTDOWN || event == SOUND_EVENT_LOGOUT) p = "/audio/shutdown.mp3";
    else if (event == SOUND_EVENT_NOTIFICATION) p = "/audio/System notifications.mp3";
    else if (event == SOUND_EVENT_DIALOG_POPUP) p = "/audio/Dialog pop up.mp3";

    if (p) hda_play_audio(p);
}

int sound_play_file(const char *path) {
    return hda_play_audio(path);
}

void sound_tick(void) {
    hda_tick();
}

void sound_wait_finished(int max_ms) {
    hda_wait_completion(max_ms);
}
