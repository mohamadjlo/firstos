import sys
content = open('gui.c').read()

idx1 = content.find('int icon_kind = entry && entry->is_dir ? ICON_FOLDER : ICON_FILE;')
end1 = content.find('if (!entry) continue;')

patch = """int icon_kind = entry ? entry->cached_icon : ICON_FILE;
            int icon_color = entry && entry->is_dir ? 0xBC8F2C : 0x5E7FA8;
            if (entry && entry->is_dir && entry->folder_color != 0) {
                icon_color = entry->folder_color;
            } else if (entry && !entry->is_dir && ext > 0) {
                if (entry->name[ext] == 'a' && entry->name[ext+1] == 'p' && entry->name[ext+2] == 'p') {
                    icon_kind = ICON_DISPLAY; icon_color = 0x5A4D86;
                } else if (entry->name[ext] == 'f' && entry->name[ext+1] == 'l') {
                    icon_kind = ICON_FILE; icon_color = 0x1F4730;
                } else if (entry->name[ext] == 'j' && entry->name[ext+1] == 'p' && entry->name[ext+2] == 'g') {
                    icon_color = 0x4D7A95;
                }
            }
            """

if idx1 != -1:
    content = content[:idx1] + patch + content[end1:]

# Also replace the background folder rectangle color
idx2 = content.find('fb_rect(tile_x + 18, tile_y + 14, 32, 24, entry->is_dir ? 0xE1BF6A : 0xC6D8EA);')
if idx2 != -1:
    patch2 = """u32 bg_col = entry->is_dir ? 0xE1BF6A : 0xC6D8EA;
                    if (entry->is_dir && entry->folder_color != 0) {
                        bg_col = (entry->folder_color & 0xFEFEFE) >> 1; // Quick mock darken
                    }
                    fb_rect(tile_x + 18, tile_y + 14, 32, 24, bg_col);"""
    content = content[:idx2] + patch2 + content[idx2 + 78:]

# Now add the search bar rendering
idx3 = content.find('fb_rect(content_x, content_y + header_h, content_w, toolbar_h, 0xF3F6F3);')
if idx3 != -1:
    patch3 = """fb_rect(content_x, content_y + header_h, content_w, toolbar_h, 0xF3F6F3);
    
    // Draw Local Search Bar
    extern char explorer_local_search[64];
    int search_w = 140;
    int search_x = content_x + content_w - search_w - 10;
    int search_y = content_y + header_h + 6;
    fb_rect(search_x, search_y, search_w, 22, 0xFFFFFF);
    outline(search_x, search_y, search_w, 22, 0xBDC3C7);
    draw_label(search_x + 4, search_y + 4, explorer_local_search[0] ? explorer_local_search : "Search...", explorer_local_search[0] ? 0x000000 : 0x7F8C8D);
"""
    content = content[:idx3] + patch3 + content[idx3 + 75:]

open('gui.c', 'w').write(content)
