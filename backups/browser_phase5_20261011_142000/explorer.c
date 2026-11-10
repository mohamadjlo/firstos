#include "explorer.h"
#include "fs.h"
#include "serial.h"

typedef struct {
    const char *name;
    const char *path;
    int is_dir;
} local_entry_t;

static local_entry_t root_entries[] = {
    {"Apps",       "FirstOS/Apps",       1},
    {"Audio",      "FirstOS/Audio",      1},
    {"Desktop",    "FirstOS/Desktop",    1},
    {"Movies",     "FirstOS/Movies",     1},
    {"System",     "FirstOS/System",     1},
    {"Trash",      "FirstOS/Trash",      1},
    {"Wallpapers", "FirstOS/Wallpapers", 1}
};

static local_entry_t apps_entries[] = {
    {"Terminal.app", "FirstOS/Apps/Terminal.app", 0},
    {"Settings.app", "FirstOS/Apps/Settings.app", 0},
    {"TextEdit.app", "FirstOS/Apps/TextEdit.app", 0},
    {"Explorer.app", "FirstOS/Apps/Explorer.app", 0},
    {"TaskMan.app", "FirstOS/Apps/TaskMan.app", 0}
};

static local_entry_t system_entries[] = {
    {"kernel.bin", "FirstOS/System/kernel.bin", 0},
    {"firstos.iso", "FirstOS/System/firstos.iso", 0},
    {"fs.img", "FirstOS/System/fs.img", 0},
    {"linker.ld", "FirstOS/System/linker.ld", 0},
    {"Makefile", "FirstOS/System/Makefile", 0}
};

static char path_buf[32][256];
static char name_buf[32][32];
static explorer_entry_t current_entries[32];
static char current_path[64] = "FirstOS";
static char parent_path[64] = "FirstOS";
static int current_entry_count;

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
    if (!max) return;
    while (src && src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void join_path(const char *base, const char *name, char *out, u32 max) {
    u32 i = 0;
    u32 j = 0;
    if (!base || !base[0]) { str_copy(out, name, max); return; }
    while (base[i] && i + 1 < max) { out[i] = base[i]; i++; }
    if (i > 0 && out[i - 1] != '/' && i + 1 < max) out[i++] = '/';
    while (name[j] && i + 1 < max) out[i++] = name[j++];
    out[i] = 0;
}

static void parent_of(const char *path, char *out) {
    int last = -1;
    int i;
    for (i = 0; path && path[i]; i++) if (path[i] == '/') last = i;
    if (last <= 0) { str_copy(out, "FirstOS", 64); return; }
    if (last > 63) last = 63;
    for (i = 0; i < last; i++) out[i] = path[i];
    out[last] = 0;
}

#include "gui_internal.h"
extern char explorer_search_text[32];

static int determine_mime_icon(const char* absolute_path, int is_dir) {
    if (is_dir) return ICON_FOLDER;
    if (explorer_is_virtual_path(absolute_path)) return ICON_FILE;
    
    // Read first 4 bytes of a file to determine type
    unsigned char magic[4] = {0};
    int read = fs_read_at(absolute_path, (char*)magic, 4);
    if (read <= 0) return ICON_FILE; // fallback
    
    // PNG: 89 50 4E 47
    if (magic[0] == 0x89 && magic[1] == 0x50 && magic[2] == 0x4E && magic[3] == 0x47) return ICON_PHOTO;
    // JPEG: FF D8 FF
    if (magic[0] == 0xFF && magic[1] == 0xD8 && magic[2] == 0xFF) return ICON_PHOTO;
    // BMP: 42 4D (BM)
    if (magic[0] == 0x42 && magic[1] == 0x4D) return ICON_PHOTO;
    // GIF: 47 49 46 38 ("GIF8")
    if (magic[0] == 'G' && magic[1] == 'I' && magic[2] == 'F') return ICON_PHOTO;
    // TIFF: II* (49 49 2A 00) or MM* (4D 4D 00 2A)
    if ((magic[0] == 'I' && magic[1] == 'I' && magic[2] == 0x2A) ||
        (magic[0] == 'M' && magic[1] == 'M' && magic[3] == 0x2A)) return ICON_PHOTO;
    // ELF Executable: 7F 45 4C 46
    if (magic[0] == 0x7F && magic[1] == 0x45 && magic[2] == 0x4C && magic[3] == 0x46) return ICON_TERMINAL;
    // ICO Format: 00 00 01 00
    if (magic[0] == 0x00 && magic[1] == 0x00 && magic[2] == 0x01 && magic[3] == 0x00) return ICON_PHOTO;
    
    if (explorer_is_image_file(absolute_path)) return ICON_PHOTO;
    
    return ICON_FILE;
}


static void set_entry(int index, const char *name, const char *path, int is_dir) {

    if (index < 0 || index >= 32) return;
    str_copy(name_buf[index], name, sizeof(name_buf[index]));
    str_copy(path_buf[index], path, sizeof(path_buf[index]));
    current_entries[index].name = name_buf[index];
    current_entries[index].path = path_buf[index];
    
    current_entries[index].name = name_buf[index];
    current_entries[index].path = path_buf[index];
    current_entries[index].is_dir = is_dir;
    
    // Resolve MIME Type Magic Number
    current_entries[index].cached_icon = determine_mime_icon(path, is_dir);
    
    // Resolve Folder Color Metadata
    current_entries[index].folder_color = 0; // Default none
    if (is_dir) {
        char meta_path[256];
        int m = 0; while(path[m]) { meta_path[m] = path[m]; m++; }
        meta_path[m++] = '/';
        const char *m_name = ".firstos_folder";
        int n = 0; while(m_name[n]) { meta_path[m++] = m_name[n++]; }
        meta_path[m] = 0;
        
        unsigned char col_buf[4] = {0};
        if (fs_read_at(meta_path, (char*)col_buf, 4) == 4) {
            current_entries[index].folder_color = (col_buf[0] << 24) | (col_buf[1] << 16) | (col_buf[2] << 8) | col_buf[3];
        }
    }
}


static void set_static_entries(local_entry_t *src, int count, const char *path) {
    int i;
    if (count > 32) count = 32;
    current_entry_count = count;
    for (i = 0; i < count; i++) set_entry(i, src[i].name, src[i].path, src[i].is_dir);
    str_copy(current_path, path, sizeof(current_path));
    parent_of(path, parent_path);
}

static void rebuild_dir_entries(const char *fs_dir, const char *display_path) {
    int count = fs_count_in_dir(fs_dir);
    int visible = 0;
    
    // Reset entry count for filtering
    current_entry_count = 0;
    for (int i = 0; i < count; i++) {
        if (visible >= 32) break;
        const fs_entry_t *entry = fs_entry_in_dir(fs_dir, i);
        if (!entry) continue;
        
        // Search Filtering
        if (explorer_search_text[0] != 0) {
            extern int sp_str_match(const char*, const char*);
            if (!sp_str_match(entry->name, explorer_search_text)) continue;
        }
        
        set_entry(visible, entry->name, entry->path, entry->is_dir);
        visible++;
    }
    current_entry_count = visible;
    str_copy(current_path, display_path, sizeof(current_path));
    parent_of(display_path, parent_path);
}

int explorer_is_virtual_path(const char *path) {
    return str_eq(path, "FirstOS") || str_eq(path, "FirstOS/Apps") || str_eq(path, "FirstOS/System");
}

void explorer_init(void) {
    serial_write("[explorer] enter explorer_init\n");
    serial_write("[explorer] before fs_ready\n");
    if (fs_ready()) {
        serial_write("[explorer] fs_ready=1 before fs_mkdir\n");
        fs_mkdir("/Desktop");
        fs_mkdir("/Trash");
        serial_write("[explorer] after fs_mkdir\n");
    } else {
        serial_write("[explorer] fs_ready=0\n");
    }
    serial_write("[explorer] before explorer_open_path\n");
    explorer_open_path("FirstOS");
    serial_write("[explorer] after explorer_open_path\n");
    serial_write("[explorer] exit explorer_init\n");
}

void explorer_open_path(const char *path) {
    if (!path || !path[0] || str_eq(path, "FirstOS")) {
        set_static_entries(root_entries, (int)(sizeof(root_entries) / sizeof(root_entries[0])), "FirstOS");
        return;
    }
    if (str_eq(path, "Apps") || str_eq(path, "FirstOS/Apps")) {
        set_static_entries(apps_entries, (int)(sizeof(apps_entries) / sizeof(apps_entries[0])), "FirstOS/Apps");
        return;
    }
    if (str_eq(path, "Desktop") || str_eq(path, "FirstOS/Desktop")) {
        rebuild_dir_entries("/Desktop", "FirstOS/Desktop");
        return;
    }
    if (str_eq(path, "Wallpapers") || str_eq(path, "FirstOS/Wallpapers")) {
        rebuild_dir_entries("/wallpapers", "FirstOS/Wallpapers");
        return;
    }
    if (str_eq(path, "Audio") || str_eq(path, "FirstOS/Audio")) {
        rebuild_dir_entries("/audio", "FirstOS/Audio");
        return;
    }
    if (str_eq(path, "Movies") || str_eq(path, "FirstOS/Movies")) {
        rebuild_dir_entries("/movies", "FirstOS/Movies");
        return;
    }
    if (str_eq(path, "Trash") || str_eq(path, "FirstOS/Trash")) {
        rebuild_dir_entries("/Trash", "FirstOS/Trash");
        return;
    }
    if (str_eq(path, "System") || str_eq(path, "FirstOS/System")) {
        set_static_entries(system_entries, (int)(sizeof(system_entries) / sizeof(system_entries[0])), "FirstOS/System");
        return;
    }
    if (path[0] == '/') {
        rebuild_dir_entries(path, path);
        return;
    }
    set_static_entries(root_entries, (int)(sizeof(root_entries) / sizeof(root_entries[0])), "FirstOS");
}

const char* explorer_current_path(void) { return current_path; }
const char* explorer_parent_path(void) { return parent_path; }
int explorer_entry_count(void) { return current_entry_count; }
const explorer_entry_t* explorer_entry_at(int index) { if (index < 0 || index >= current_entry_count) return 0; return &current_entries[index]; }

static int explorer_create_unique_here(const char *name, int is_dir) {
    char stem[48];
    char ext[16];
    char candidate[64];
    const char *base = str_eq(current_path, "FirstOS/Desktop") ? "/Desktop" : current_path;
    int name_len = 0;
    int dot = -1;
    int suffix;
    int i;
    if (!name || !name[0]) return 0;
    if (explorer_is_virtual_path(current_path) && !str_eq(current_path, "FirstOS/Desktop")) return 0;
    while (name[name_len] && name_len < (int)sizeof(stem) - 1) {
        if (name[name_len] == '.') dot = name_len;
        name_len++;
    }
    if (dot > 0 && !is_dir) {
        for (i = 0; i < dot && i + 1 < (int)sizeof(stem); i++) stem[i] = name[i];
        stem[i] = 0;
        i = 0;
        while (name[dot + i] && i + 1 < (int)sizeof(ext)) { ext[i] = name[dot + i]; i++; }
        ext[i] = 0;
    } else {
        for (i = 0; i < name_len && i + 1 < (int)sizeof(stem); i++) stem[i] = name[i];
        stem[i] = 0;
        ext[0] = 0;
    }
    for (suffix = 0; suffix < 1000; suffix++) {
        char numbered[64];
        int p = 0;
        int n = 0;
        int value = suffix;
        int digits[10];
        int digit_count = 0;
        while (stem[n] && p + 1 < (int)sizeof(numbered)) numbered[p++] = stem[n++];
        if (suffix > 0) {
            while (value > 0 && digit_count < 10) { digits[digit_count++] = value % 10; value /= 10; }
            while (digit_count > 0 && p + 1 < (int)sizeof(numbered)) numbered[p++] = (char)('0' + digits[--digit_count]);
        }
        n = 0;
        while (ext[n] && p + 1 < (int)sizeof(numbered)) numbered[p++] = ext[n++];
        numbered[p] = 0;
        join_path(base, numbered, candidate, sizeof(candidate));
        if (fs_create_at(candidate, is_dir)) return 1;
    }
    return 0;
}

int explorer_create_file_here(const char *name) {
    return explorer_create_unique_here(name, 0);
}

int explorer_create_folder_here(const char *name) {
    return explorer_create_unique_here(name, 1);
}

static const explorer_disk_t system_disks[2] = {
    {"FirstOS HD (virtio)", 1},
    {"System Volume", 1}
};

int explorer_disk_count(void) {
    return 2;
}

const explorer_disk_t* explorer_disk_at(int index) {
    if (index >= 0 && index < 2) return &system_disks[index];
    return 0;
}

static int explorer_str_ends_ci(const char *s, const char *suffix) {
    int sl = 0, el = 0;
    while(s[sl]) sl++;
    while(suffix[el]) el++;
    if(sl < el) return 0;
    for(int i=0; i<el; i++) {
        char a = s[sl-el+i], b = suffix[i];
        if(a>='A'&&a<='Z') a+=(char)('a'-'A');
        if(b>='A'&&b<='Z') b+=(char)('a'-'A');
        if(a!=b) return 0;
    }
    return 1;
}

int explorer_is_image_file(const char *name) {
    return explorer_str_ends_ci(name,".jpg")  || explorer_str_ends_ci(name,".jpeg") ||
           explorer_str_ends_ci(name,".png")  || explorer_str_ends_ci(name,".bmp")  ||
           explorer_str_ends_ci(name,".gif")  || explorer_str_ends_ci(name,".tif")  ||
           explorer_str_ends_ci(name,".tiff") || explorer_str_ends_ci(name,".raw");
}

int explorer_is_audio_file(const char *name) {
    return explorer_str_ends_ci(name,".mp3") || explorer_str_ends_ci(name,".wav") ||
           explorer_str_ends_ci(name,".ogg") || explorer_str_ends_ci(name,".pcm");
}
int explorer_is_video_file(const char *name) {
    return explorer_str_ends_ci(name,".mp4") || explorer_str_ends_ci(name,".mov") ||
           explorer_str_ends_ci(name,".avi") || explorer_str_ends_ci(name,".mkv");
}
int explorer_is_text_file(const char *name) {
    return explorer_str_ends_ci(name,".txt") || explorer_str_ends_ci(name,".c") ||
           explorer_str_ends_ci(name,".h")   || explorer_str_ends_ci(name,".fl") ||
           explorer_str_ends_ci(name,".md")  || explorer_str_ends_ci(name,".cfg") ||
           explorer_str_ends_ci(name,".asm") || explorer_str_ends_ci(name,".ini") ||
           explorer_str_ends_ci(name,".json")|| explorer_str_ends_ci(name,".log");
}

int explorer_is_tml_file(const char *name) {
    return explorer_str_ends_ci(name, ".tml");
}
