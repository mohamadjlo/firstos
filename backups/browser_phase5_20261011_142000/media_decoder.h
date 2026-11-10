#ifndef MEDIA_DECODER_H
#define MEDIA_DECODER_H

typedef struct {
    int width;
    int height;
    int channels;
    unsigned char* pixels;
} media_image_t;

// Wraps stb_image in the future
media_image_t decode_image(const char* filepath);
void free_image(media_image_t img);

// Wraps pl_mpeg in the future
void play_video_stream(const char* filepath, int target_x, int target_y);

#endif
