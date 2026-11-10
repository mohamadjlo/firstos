#ifndef THUMBNAIL_H
#define THUMBNAIL_H

typedef unsigned char u8;
typedef unsigned int u32;

#define MAX_THUMBNAILS 32

typedef struct {
    char path[128];
    int w;
    int h;
    u32 *pixels;
    int is_video;
    unsigned int last_used;
} file_thumbnail_t;

void thumbnail_init(void);
const file_thumbnail_t *thumbnail_get(const char *path, int target_w, int target_h);
void draw_thumbnail_card(int x, int y, int w, int h, const file_thumbnail_t *thumb, int selected);

#endif /* THUMBNAIL_H */
