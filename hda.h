#ifndef HDA_H
#define HDA_H
#include "io.h"
int hda_init(void);
int hda_play_audio(const char* path);
void hda_stop_audio(void);
void hda_pause_audio(void);
void hda_resume_audio(void);
void hda_tick(void);
void hda_set_volume(int percent);
int hda_is_playing(void);
int hda_is_paused(void);
unsigned int hda_get_current_offset(void);
unsigned int hda_get_total_length(void);
int hda_get_pcm_samples(short *out_samples, int max_count);
int hda_seek_byte_offset(unsigned int byte_offset);
int hda_play_pcm(const short *samples, int sample_count, int hz, int channels);
void hda_wait_completion(int max_ms);
#endif
