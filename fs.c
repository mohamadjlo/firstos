#include "fs.h"
#include "serial.h"
#include "virtio_blk.h"

#define FS_MAGIC 0x314F5346u
#define FS_VERSION 2u
#define FS_MAX_FILES 1024
#define FS_NAME_SIZE 128
#define FS_PATH_SIZE 256
#define FS_HEADER_SIZE 16

typedef struct { u32 magic; u32 version; u32 image_size; u32 file_count; } fs_header_t;
typedef struct __attribute__((packed)) {
    char name[FS_NAME_SIZE];
    char path[FS_PATH_SIZE];
    u32 offset;
    u32 size;
    u32 capacity;
    u32 used;
    u32 is_dir;
    int parent;
} fs_disk_entry_t;
#define FS_ENTRY_SIZE (sizeof(fs_disk_entry_t))

static u8 *fs_image;
static u32 fs_image_size;
static fs_header_t *fs_header;
static fs_disk_entry_t *fs_entries;
static int fs_is_ready;
static int fs_dirty;
static u32 fs_meta_min_sec = 0xFFFFFFFFu;
static u32 fs_meta_max_sec = 0;
static u32 fs_dirty_min_sec = 0xFFFFFFFFu;
static u32 fs_dirty_max_sec = 0;
static int fs_auto_sync_enabled;

static void fs_mark_dirty_range(u32 start_byte, u32 len) {
    fs_dirty = 1;
    if (len == 0) return;
    u32 s_start = start_byte / 512u;
    u32 s_end = (start_byte + len + 511u) / 512u;
    if (s_start < fs_dirty_min_sec) fs_dirty_min_sec = s_start;
    if (s_end > fs_dirty_max_sec) fs_dirty_max_sec = s_end;
}

static void fs_mark_entry_dirty(int entry_idx) {
    fs_dirty = 1;
    if (entry_idx >= 0 && entry_idx < FS_MAX_FILES) {
        u32 start_byte = FS_HEADER_SIZE + (u32)entry_idx * (u32)FS_ENTRY_SIZE;
        u32 s_start = start_byte / 512u;
        u32 s_end = (start_byte + (u32)FS_ENTRY_SIZE + 511u) / 512u;
        if (s_start < fs_meta_min_sec) fs_meta_min_sec = s_start;
        if (s_end > fs_meta_max_sec) fs_meta_max_sec = s_end;
    } else {
        if (0 < fs_meta_min_sec) fs_meta_min_sec = 0;
        if (1 > fs_meta_max_sec) fs_meta_max_sec = 1;
    }
}

static void fs_mark_metadata_dirty(void) {
    fs_dirty = 1;
    u32 meta_bytes = FS_HEADER_SIZE + (u32)(FS_ENTRY_SIZE * FS_MAX_FILES);
    u32 s_end = (meta_bytes + 511u) / 512u;
    fs_meta_min_sec = 0;
    if (s_end > fs_meta_max_sec) fs_meta_max_sec = s_end;
}

static int str_eq(const char *a, const char *b) { while (*a && *b) { if (*a++ != *b++) return 0; } return *a == 0 && *b == 0; }
static int str_eq_ci(const char *a, const char *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}
static void str_copy(char *dst, const char *src, u32 max) { u32 i=0; if (!max) return; while (src[i] && i+1<max) { dst[i]=src[i]; i++; } dst[i]=0; }
static u32 str_len(const char *s) { u32 n = 0; while (s && s[n]) n++; return n; }
static u32 align4(u32 x) { return (x + 3u) & ~3u; }

static void split_path(const char *path, char *dir, char *name) {
    int last = -1;
    int i;
    if (!path || !path[0]) {
        str_copy(dir, "/", 256);
        name[0] = 0;
        return;
    }
    for (i = 0; path[i]; i++) if (path[i] == '/') last = i;
    if (last < 0) {
        str_copy(dir, "/", 256);
        str_copy(name, path, 128);
        return;
    }
    if (last == 0) str_copy(dir, "/", 256);
    else {
        int j;
        if (last > 63) last = 63;
        for (j = 0; j < last; j++) dir[j] = path[j];
        dir[last] = 0;
    }
    str_copy(name, path + last + 1, 128);
}

static int find_entry_by_path(const char *path) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) if (fs_entries[i].used && str_eq_ci(fs_entries[i].path, path)) return i;
    return -1;
}

static int find_child(const char *dir, const char *name) {
    int i;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!fs_entries[i].used) continue;
        if (str_eq(fs_entries[i].name, name)) {
            char parent_dir[256], dummy[128];
            split_path(fs_entries[i].path, parent_dir, dummy);
            if (str_eq(parent_dir, dir)) return i;
        }
    }
    return -1;
}

static void make_full_path(const char *dir, const char *name, char *out) {
    u32 i = 0, j = 0;
    if (str_eq(dir, "/")) {
        out[i++] = '/';
    } else {
        while (dir[j] && i + 1 < FS_PATH_SIZE) out[i++] = dir[j++];
        if (i + 1 < FS_PATH_SIZE) out[i++] = '/';
    }
    j = 0;
    while (name[j] && i + 1 < FS_PATH_SIZE) out[i++] = name[j++];
    out[i] = 0;
}

int fs_format(void *image, u32 image_size) {
    int i;
    if (!image || image_size < FS_HEADER_SIZE + FS_ENTRY_SIZE * FS_MAX_FILES) return 0;
    fs_image = (u8*)image; fs_image_size = image_size; fs_header = (fs_header_t*)image; fs_entries = (fs_disk_entry_t*)(fs_image + FS_HEADER_SIZE);
    fs_header->magic = FS_MAGIC; fs_header->version = FS_VERSION; fs_header->image_size = image_size; fs_header->file_count = 0;
    for (i = 0; i < FS_MAX_FILES; i++) {
        fs_entries[i].used = 0; fs_entries[i].name[0] = 0; fs_entries[i].path[0] = 0;
        fs_entries[i].offset = 0; fs_entries[i].size = 0; fs_entries[i].capacity = 0; fs_entries[i].is_dir = 0; fs_entries[i].parent = -1;
    }
    fs_is_ready = 1;
    fs_mark_metadata_dirty();
    fs_dirty_min_sec = 0xFFFFFFFFu;
    fs_dirty_max_sec = 0;
    fs_auto_sync_enabled = 0;
    serial_write("[fs] formatted v2\n");
    return 1;
}

void fs_init(void *image, u32 image_size) {
    if (!image || image_size < FS_HEADER_SIZE + FS_ENTRY_SIZE * FS_MAX_FILES) { fs_is_ready = 0; return; }
    fs_image = (u8*)image; fs_image_size = image_size; fs_header = (fs_header_t*)image; fs_entries = (fs_disk_entry_t*)(fs_image + FS_HEADER_SIZE);
    fs_is_ready = (fs_header->magic == FS_MAGIC && fs_header->version == FS_VERSION && fs_header->image_size <= image_size);
    if (!fs_is_ready) {
        serial_write("[fs] init image invalid; formatting\n");
        if (!fs_format(image, image_size)) return;
    }
    fs_dirty = 0;
    fs_meta_min_sec = 0xFFFFFFFFu;
    fs_meta_max_sec = 0;
    fs_dirty_min_sec = 0xFFFFFFFFu;
    fs_dirty_max_sec = 0;
    fs_auto_sync_enabled = 0; // disabled until kernel enables it post-GUI
    serial_write("[fs] image mounted\n");
}

int fs_create_at(const char *path, int is_dir) {
    int i;
    char dir[256];
    char name[128];
    if (!fs_is_ready || !path || !path[0]) return 0;
    split_path(path, dir, name);
    if (!name[0]) return 0;
    if (!str_eq(dir, "/")) {
        int parent = find_entry_by_path(dir);
        if (parent < 0 || !fs_entries[parent].is_dir) return 0;
    }
    if (find_entry_by_path(path) >= 0 || find_child(dir, name) >= 0) return 0;
    for (i = 0; i < FS_MAX_FILES; i++) if (!fs_entries[i].used) {
        fs_entries[i].used = 1;
        fs_entries[i].is_dir = is_dir ? 1u : 0u;
        str_copy(fs_entries[i].name, name, FS_NAME_SIZE);
        str_copy(fs_entries[i].path, path, FS_PATH_SIZE);
        fs_entries[i].offset = 0;
        fs_entries[i].size = 0;
        fs_entries[i].capacity = 0;
        fs_entries[i].parent = str_eq(dir, "/") ? -1 : find_entry_by_path(dir);
        fs_header->file_count++;
        fs_mark_entry_dirty(i);
        if (fs_auto_sync_enabled && !fs_sync()) return 0;
        return 1;
    }
    return 0;
}

int fs_create(const char *name) { char full[256]; make_full_path("/", name, full); return fs_create_at(full, 0); }
int fs_mkdir(const char *path) { return fs_create_at(path, 1); }

int fs_write_at(const char *path, const char *data, u32 size) {
    int i = find_entry_by_path(path);
    u32 data_start, off, end, j;
    if (!fs_is_ready || i < 0 || !data || fs_entries[i].is_dir) return 0;
    data_start = align4(FS_HEADER_SIZE + FS_ENTRY_SIZE * FS_MAX_FILES);
    if (fs_entries[i].offset >= data_start && size <= fs_entries[i].capacity &&
        fs_entries[i].offset + size <= fs_image_size) {
        off = fs_entries[i].offset;
    } else {
        off = data_start;
        for (j = 0; j < FS_MAX_FILES; j++) {
            if ((int)j != i && fs_entries[j].used && !fs_entries[j].is_dir &&
                fs_entries[j].offset + fs_entries[j].capacity > off) {
                off = align4(fs_entries[j].offset + fs_entries[j].capacity);
            }
        }
        end = off + size;
        if (end > fs_image_size) return 0;
        fs_entries[i].offset = off;
        fs_entries[i].capacity = size;
    }
    if (size > 0) {
        extern void *memcpy(void *dest, const void *src, unsigned long n);
        memcpy(fs_image + off, data, size);
        fs_mark_dirty_range(off, size);
    }
    fs_entries[i].size = size;
    fs_mark_entry_dirty(i);
    if (fs_auto_sync_enabled) fs_sync();
    return 1;
}

int fs_write(const char *name, const char *data, u32 size) { char full[256]; make_full_path("/", name, full); return fs_write_at(full, data, size); }

int fs_read_at(const char *path, char *out, u32 capacity) {
    int i = find_entry_by_path(path);
    u32 n;
    if (!fs_is_ready || i < 0 || fs_entries[i].is_dir) return -1;
    if (!out || capacity == 0) return (int)fs_entries[i].size;
    n = fs_entries[i].size < capacity - 1 ? fs_entries[i].size : capacity - 1;
    extern void *memcpy(void *dest, const void *src, unsigned long n);
    memcpy(out, fs_image + fs_entries[i].offset, n);
    out[n] = 0;
    return (int)n;
}

/* Binary read: does NOT append null terminator. Safe for large binary files like wallpapers. */
int fs_read_raw_at(const char *path, char *out, u32 capacity) {
    int i = find_entry_by_path(path);
    u32 n;
    if (!fs_is_ready || i < 0 || !out || !capacity || fs_entries[i].is_dir) return 0;
    n = fs_entries[i].size < capacity ? fs_entries[i].size : capacity;
    extern void *memcpy(void *dest, const void *src, unsigned long n);
    memcpy(out, fs_image + fs_entries[i].offset, n);
    return (int)n;
}

int fs_read(const char *name, char *out, u32 capacity) { char full[256]; make_full_path("/", name, full); return fs_read_at(full, out, capacity); }

int fs_rename_at(const char *old_path, const char *new_path) {
    int i;
    int parent;
    char new_dir[256], new_name[128];
    if (!fs_is_ready || !old_path || !new_path || !old_path[0] || !new_path[0]) return 0;
    i = find_entry_by_path(old_path);
    if (i < 0) return 0;
    if (find_entry_by_path(new_path) >= 0) return 0;
    split_path(new_path, new_dir, new_name);
    if (!new_name[0]) return 0;
    if (!str_eq(new_dir, "/")) {
        parent = find_entry_by_path(new_dir);
        if (parent < 0 || !fs_entries[parent].is_dir) return 0;
    } else parent = -1;
    if (find_child(new_dir, new_name) >= 0) return 0;
    str_copy(fs_entries[i].name, new_name, FS_NAME_SIZE);
    str_copy(fs_entries[i].path, new_path, FS_PATH_SIZE);
    fs_entries[i].parent = parent;
    fs_mark_entry_dirty(i);
    if (fs_auto_sync_enabled) fs_sync();
    return 1;
}

int fs_rename(const char *old_name, const char *new_name) {
    char old_full[256], new_full[256];
    make_full_path("/", old_name, old_full);
    make_full_path("/", new_name, new_full);
    return fs_rename_at(old_full, new_full);
}

int fs_copy_at(const char *src_path, const char *dst_path) {
    int src_i;
    if (!fs_is_ready || !src_path || !dst_path || !src_path[0] || !dst_path[0]) return 0;
    if (str_eq(src_path, dst_path)) return 0;
    src_i = find_entry_by_path(src_path);
    if (src_i < 0) return 0;

    if (fs_entries[src_i].is_dir) {
        int i;
        if (!fs_create_at(dst_path, 1)) {
            int dst_i = find_entry_by_path(dst_path);
            if (dst_i < 0 || !fs_entries[dst_i].is_dir) return 0;
        }
        for (i = 0; i < FS_MAX_FILES; i++) {
            if (!fs_entries[i].used || fs_entries[i].parent != src_i) continue;
            char child_dst[FS_PATH_SIZE];
            make_full_path(dst_path, fs_entries[i].name, child_dst);
            fs_copy_at(fs_entries[i].path, child_dst);
        }
        if (fs_auto_sync_enabled) fs_sync();
        return 1;
    } else {
        u32 sz = fs_entries[src_i].size;
        u32 src_off = fs_entries[src_i].offset;

        int existing = find_entry_by_path(dst_path);
        if (existing >= 0) {
            if (fs_entries[existing].is_dir) return 0;
            fs_delete_at(dst_path);
        }

        if (!fs_create_at(dst_path, 0)) return 0;
        if (sz == 0) return 1;

        if (!fs_write_at(dst_path, (const char*)(fs_image + src_off), sz)) {
            fs_delete_at(dst_path);
            return 0;
        }
        return 1;
    }
}

int fs_copy(const char *src_name, const char *dst_name) {
    char src_full[256], dst_full[256];
    make_full_path("/", src_name, src_full);
    make_full_path("/", dst_name, dst_full);
    return fs_copy_at(src_full, dst_full);
}

int fs_delete_at(const char *path) {
    int i;
    int j;
    if (!fs_is_ready || !path || !path[0]) return 0;
    i = find_entry_by_path(path);
    if (i < 0) return 0;
    if (fs_entries[i].is_dir) {
        for (j = 0; j < FS_MAX_FILES; j++) {
            if (!fs_entries[j].used) continue;
            if (fs_entries[j].parent == i) return 0;
        }
    }
    fs_entries[i].used = 0;
    fs_entries[i].name[0] = 0;
    fs_entries[i].path[0] = 0;
    fs_entries[i].offset = 0;
    fs_entries[i].size = 0;
    fs_entries[i].capacity = 0;
    fs_entries[i].is_dir = 0;
    fs_entries[i].parent = -1;
    if (fs_header->file_count > 0) fs_header->file_count--;
    fs_mark_entry_dirty(i);
    if (fs_auto_sync_enabled) fs_sync();
    return 1;
}

int fs_delete(const char *name) {
    char full[256];
    make_full_path("/", name, full);
    return fs_delete_at(full);
}

const fs_entry_t *fs_entry_at(int index) {
    static fs_entry_t view;
    int seen = 0;
    int i;
    if (!fs_is_ready || index < 0) return 0;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!fs_entries[i].used) continue;
        if (seen == index) {
            str_copy(view.name, fs_entries[i].name, 128);
            str_copy(view.path, fs_entries[i].path, 256);
            view.offset = fs_entries[i].offset;
            view.size = fs_entries[i].size;
            view.capacity = fs_entries[i].capacity;
            view.used = 1;
            view.is_dir = (int)fs_entries[i].is_dir;
            view.parent = fs_entries[i].parent;
            return &view;
        }
        seen++;
    }
    return 0;
}

const fs_entry_t *fs_entry_in_dir(const char *dir, int visible_index) {
    static fs_entry_t view;
    int seen = 0;
    int i;
    char parent_dir[256], dummy[128];
    if (!fs_is_ready || visible_index < 0) return 0;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!fs_entries[i].used) continue;
        split_path(fs_entries[i].path, parent_dir, dummy);
        if (!str_eq_ci(parent_dir, dir)) continue;
        if (seen == visible_index) {
            str_copy(view.name, fs_entries[i].name, 128);
            str_copy(view.path, fs_entries[i].path, 256);
            view.offset = fs_entries[i].offset;
            view.size = fs_entries[i].size;
            view.capacity = fs_entries[i].capacity;
            view.used = 1;
            view.is_dir = (int)fs_entries[i].is_dir;
            view.parent = fs_entries[i].parent;
            return &view;
        }
        seen++;
    }
    return 0;
}

int fs_count(void) { return fs_is_ready ? (int)fs_header->file_count : 0; }

int fs_count_in_dir(const char *dir) {
    int i;
    int count = 0;
    char parent_dir[256], dummy[128];
    if (!fs_is_ready) return 0;
    for (i = 0; i < FS_MAX_FILES; i++) {
        if (!fs_entries[i].used) continue;
        split_path(fs_entries[i].path, parent_dir, dummy);
        if (str_eq_ci(parent_dir, dir)) count++;
    }
    return count;
}

int fs_ready(void) { return fs_is_ready; }

int fs_is_dirty(void) { return fs_dirty; }
int fs_set_auto_sync(int enabled) { fs_auto_sync_enabled = enabled ? 1 : 0; return fs_auto_sync_enabled; }

static int fs_sync_in_progress = 0;

static int fs_sync_sector_range(u32 start_sec, u32 end_sec, u32 total_sectors) {
    const u32 chunk_sectors = 128u;
    if (start_sec >= total_sectors || end_sec <= start_sec) return 1;
    if (end_sec > total_sectors) end_sec = total_sectors;
    for (u32 sector = start_sec; sector < end_sec; sector += chunk_sectors) {
        u32 remaining = end_sec - sector;
        u32 count = (remaining < chunk_sectors) ? remaining : chunk_sectors;
        if (!virtio_blk_write(sector, fs_image + sector * 512u, count)) {
            serial_write("[fs] sync chunk failed at sector=");
            serial_write_hex(sector);
            serial_write(" count=");
            serial_write_hex(count);
            serial_write("\n");
            return 0;
        }
    }
    return 1;
}

int fs_sync(void) {
    int ok = 1;
    if (!fs_is_ready) return 0;
    if (!fs_dirty) return 1;
    if (fs_sync_in_progress) {
        serial_write("[fs] sync already in progress, skipping\n");
        return 1;
    }
    fs_sync_in_progress = 1;
    if (!virtio_blk_ready()) {
        serial_write("[fs] sync deferred: no virtio block device\n");
        fs_sync_in_progress = 0;
        return 0;
    }

    u32 total_sectors = (fs_image_size + 511u) / 512u;
    if (fs_meta_min_sec < fs_meta_max_sec) {
        if (fs_meta_min_sec > 0 && !fs_sync_sector_range(0, 1, total_sectors)) {
            ok = 0;
        }
        if (ok && !fs_sync_sector_range(fs_meta_min_sec, fs_meta_max_sec, total_sectors)) {
            ok = 0;
        }
    }
    if (ok && fs_dirty_min_sec < fs_dirty_max_sec) {
        if (!fs_sync_sector_range(fs_dirty_min_sec, fs_dirty_max_sec, total_sectors)) {
            ok = 0;
        }
    }
    if (!ok) {
        serial_write("[fs] sync failed\n");
        fs_sync_in_progress = 0;
        return 0;
    }
    fs_dirty = 0;
    fs_meta_min_sec = 0xFFFFFFFFu;
    fs_meta_max_sec = 0;
    fs_dirty_min_sec = 0xFFFFFFFFu;
    fs_dirty_max_sec = 0;
    fs_sync_in_progress = 0;
    serial_write("[fs] synced to disk\n");
    return 1;
}
