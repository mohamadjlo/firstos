#include "storage.h"
#include "serial.h"

static storage_file_t storage_files[STORAGE_MAX_FILES];
static int storage_count;

static int str_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static void str_copy(char *dst, const char *src, u32 max) {
    u32 i = 0;
    if (max == 0) return;
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void storage_init(void) {
    int i;
    for (i = 0; i < STORAGE_MAX_FILES; i++) {
        storage_files[i].used = 0;
        storage_files[i].size = 0;
        storage_files[i].path[0] = 0;
        storage_files[i].data[0] = 0;
    }
    storage_count = 0;
    serial_write("[storage] memory-backed storage initialized\n");
}

int storage_write_file(const char *path, const char *data, u32 size) {
    int i;
    int slot = -1;
    if (!path || !data) return 0;
    if (size >= sizeof(storage_files[0].data)) size = sizeof(storage_files[0].data) - 1;
    for (i = 0; i < STORAGE_MAX_FILES; i++) {
        if (storage_files[i].used && str_eq(storage_files[i].path, path)) {
            slot = i;
            break;
        }
        if (!storage_files[i].used && slot < 0) slot = i;
    }
    if (slot < 0) return 0;
    if (!storage_files[slot].used) {
        storage_files[slot].used = 1;
        storage_count++;
    }
    str_copy(storage_files[slot].path, path, sizeof(storage_files[slot].path));
    for (i = 0; i < (int)size; i++) storage_files[slot].data[i] = data[i];
    storage_files[slot].data[size] = 0;
    storage_files[slot].size = size;
    serial_write("[storage] file saved\n");
    return 1;
}

const storage_file_t *storage_get_file(const char *path) {
    int i;
    for (i = 0; i < STORAGE_MAX_FILES; i++) {
        if (storage_files[i].used && str_eq(storage_files[i].path, path)) return &storage_files[i];
    }
    return 0;
}

int storage_file_count(void) {
    return storage_count;
}

const storage_file_t *storage_file_at(int index) {
    int i;
    int seen = 0;
    for (i = 0; i < STORAGE_MAX_FILES; i++) {
        if (!storage_files[i].used) continue;
        if (seen == index) return &storage_files[i];
        seen++;
    }
    return 0;
}
