#ifndef IMAGE_H
#define IMAGE_H

typedef unsigned char u8;

u8 *image_load(const char *path, int *w, int *h);
u8 *image_load_from_memory(const u8 *buf, int len, int *w, int *h);
void image_free(u8 *data);
const char *image_detect_format_name(const char *path, const u8 *buf, unsigned int size);

#endif

