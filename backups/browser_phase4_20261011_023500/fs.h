#ifndef FS_H
#define FS_H

typedef unsigned char u8;
typedef unsigned int u32;

typedef struct {
    char name[128];
    char path[256];
    u32 offset;
    u32 size;
    u32 capacity;
    int used;
    int is_dir;
    int parent;
} fs_entry_t;

void fs_init(void *image, u32 image_size);
int fs_format(void *image, u32 image_size);
int fs_create(const char *name);
int fs_create_at(const char *path, int is_dir);
int fs_mkdir(const char *path);
int fs_write(const char *name, const char *data, u32 size);
int fs_write_at(const char *path, const char *data, u32 size);
int fs_read(const char *name, char *out, u32 capacity);
int fs_read_at(const char *path, char *out, u32 capacity);
int fs_read_raw_at(const char *path, char *out, u32 capacity);
int fs_rename(const char *old_name, const char *new_name);
int fs_rename_at(const char *old_path, const char *new_path);
int fs_copy(const char *src_name, const char *dst_name);
int fs_copy_at(const char *src_path, const char *dst_path);
int fs_delete(const char *name);
int fs_delete_at(const char *path);
const fs_entry_t *fs_entry_at(int index);
const fs_entry_t *fs_entry_in_dir(const char *dir, int visible_index);
int fs_count(void);
int fs_count_in_dir(const char *dir);
int fs_ready(void);
int fs_sync(void);
int fs_is_dirty(void);
int fs_set_auto_sync(int enabled);

#endif
