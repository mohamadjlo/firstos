import sys
content = open('explorer.c').read()

mime_parser = """#include "gui_internal.h"

static int determine_mime_icon(const char* absolute_path, int is_dir) {
    if (is_dir) return ICON_FOLDER;
    if (explorer_is_virtual_path(absolute_path)) return ICON_FILE;
    
    // Read first 4 bytes of a file to determine type
    unsigned char magic[4] = {0};
    int read = fs_read_at(absolute_path, (char*)magic, 4);
    if (read <= 0) return ICON_FILE; // fallback
    
    // PNG: 89 50 4E 47
    if (magic[0] == 0x89 && magic[1] == 0x50 && magic[2] == 0x4E && magic[3] == 0x47) return ICON_PHOTO;
    // BMP: 42 4D (BM)
    if (magic[0] == 0x42 && magic[1] == 0x4D) return ICON_PHOTO;
    // ELF Executable: 7F 45 4C 46
    if (magic[0] == 0x7F && magic[1] == 0x45 && magic[2] == 0x4C && magic[3] == 0x46) return ICON_TERMINAL;
    // ICO Format: 00 00 01 00
    if (magic[0] == 0x00 && magic[1] == 0x00 && magic[2] == 0x01 && magic[3] == 0x00) return ICON_PHOTO;
    
    return ICON_FILE;
}

static void set_entry(int index, const char *name, const char *path, int is_dir) {
"""

if 'static int determine_mime_icon' not in content:
    content = content.replace('static void set_entry(int index, const char *name, const char *path, int is_dir) {', mime_parser)

set_entry_patch = """
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
"""

idx = content.find('current_entries[index].is_dir = is_dir;')
if idx != -1:
    end_idx = content.find('}', idx) + 1
    content = content[:idx] + set_entry_patch + content[end_idx:]

open('explorer.c', 'w').write(content)
