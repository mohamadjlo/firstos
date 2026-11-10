#ifndef STORAGE_H
#define STORAGE_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

typedef struct {
    char path[32];
    char data[2048];
    u32 size;
    int used;
} storage_file_t;

void storage_init(void);
int storage_write_file(const char *path, const char *data, u32 size);
const storage_file_t *storage_get_file(const char *path);
int storage_file_count(void);
const storage_file_t *storage_file_at(int index);

#endif
