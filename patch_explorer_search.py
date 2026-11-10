import sys
content = open('explorer.c').read()

if 'char explorer_local_search[64]' not in content:
    content = content.replace('static char current_path[256];', 'static char current_path[256];\nchar explorer_local_search[64] = {0};')

rebuild_patch = """static void rebuild_dir_entries(const char *fs_dir, const char *display_path) {
    int count = fs_count_in_dir(fs_dir);
    int visible = 0;
    
    // Reset entry count for filtering
    current_entry_count = 0;
    for (int i = 0; i < count; i++) {
        if (visible >= 32) break;
        const fs_entry_t *entry = fs_entry_in_dir(fs_dir, i);
        if (!entry) continue;
        
        // Search Filtering
        if (explorer_local_search[0] != 0) {
            extern int sp_str_match(const char*, const char*);
            if (!sp_str_match(entry->name, explorer_local_search)) continue;
        }
        
        set_entry(visible, entry->name, entry->path, entry->is_dir);
        visible++;
    }
    current_entry_count = visible;
    str_copy(current_path, display_path, sizeof(current_path));
    parent_of(display_path, parent_path);
}"""

idx = content.find('static void rebuild_dir_entries')
if idx != -1:
    end_idx = content.find('}', idx) + 1
    content = content[:idx] + rebuild_patch + content[end_idx:]

open('explorer.c', 'w').write(content)
