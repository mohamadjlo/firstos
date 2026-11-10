#ifndef EXPLORER_H
#define EXPLORER_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

typedef struct {
    const char* name;
    const char* path;
    int is_dir;
    int cached_icon;
    u32 folder_color;
} explorer_entry_t;

typedef struct {
    const char* label;
    int present;
} explorer_disk_t;

void explorer_init(void);
void explorer_open_path(const char *path);
const char* explorer_current_path(void);
const char* explorer_parent_path(void);
int explorer_entry_count(void);
const explorer_entry_t* explorer_entry_at(int index);
int explorer_is_virtual_path(const char *path);
int explorer_create_file_here(const char *name);
int explorer_create_folder_here(const char *name);
int explorer_disk_count(void);
const explorer_disk_t* explorer_disk_at(int index);

/* File type detection */
int explorer_is_image_file(const char *name);
int explorer_is_audio_file(const char *name);
int explorer_is_video_file(const char *name);
int explorer_is_text_file(const char *name);
int explorer_is_tml_file(const char *name);

#endif
