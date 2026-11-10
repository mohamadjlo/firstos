import sys
content = open('gui.c').read()

if 'static rect_t explorer_ctx_delete;' in content:
    content = content.replace('static rect_t explorer_ctx_delete;', 'static rect_t explorer_ctx_delete;\nstatic rect_t explorer_ctx_terminal;\nstatic rect_t explorer_ctx_color;')

layout_patch = """static void layout_explorer_context_menu(int mx, int my) {
    int is_img = 0;
    int is_dir = 0;
    if (explorer_context_index >= 0) {
        const explorer_entry_t *e = explorer_entry_at(explorer_context_index);
        if (e && e->name) {
            if (e->is_dir) is_dir = 1;
            int len = 0; while (e->name[len]) len++;
            if (len > 4 && (
                (e->name[len-3] == 'p' && e->name[len-2] == 'n' && e->name[len-1] == 'g') ||
                (e->name[len-3] == 'j' && e->name[len-2] == 'p' && e->name[len-1] == 'g') ||
                (e->name[len-3] == 'r' && e->name[len-2] == 'a' && e->name[len-1] == 'w')
            )) is_img = 1;
        }
    }

    int extra_h = 0;
    if (is_img) extra_h += 26;
    if (is_dir) extra_h += 26; // for Color
    
    explorer_context_menu = (rect_t){mx, my, 154, 156 + extra_h, 0xD7DEE8};
    explorer_ctx_terminal = (rect_t){mx + 8, my + 8, 138, 20, 0xB4C6D6};
    explorer_ctx_copy = (rect_t){mx + 8, my + 34, 138, 20, 0xB4C6D6};
    explorer_ctx_cut = (rect_t){mx + 8, my + 60, 138, 20, 0xB4C6D6};
    explorer_ctx_paste = (rect_t){mx + 8, my + 86, 138, 20, 0xB4C6D6};
    explorer_ctx_rename = (rect_t){mx + 8, my + 112, 138, 20, 0xB4C6D6};
    explorer_ctx_delete = (rect_t){mx + 8, my + 138, 138, 20, 0xC99393};
    
    int cy = my + 164;
    if (is_img) {
        explorer_ctx_set_wall = (rect_t){mx + 8, cy, 138, 20, 0xB4C6D6};
        cy += 26;
    } else explorer_ctx_set_wall = (rect_t){0,0,0,0,0};
    
    if (is_dir) {
        explorer_ctx_color = (rect_t){mx + 8, cy, 138, 20, 0xB4C6D6};
        cy += 26;
    } else explorer_ctx_color = (rect_t){0,0,0,0,0};
}
"""
idx = content.find('static void layout_explorer_context_menu')
if idx != -1:
    end_idx = content.find('}', idx) + 1
    content = content[:idx] + layout_patch + content[end_idx:]

draw_patch = """static void draw_explorer_context_menu(void) {
    if (!explorer_context_open) return;
    fb_rect(explorer_context_menu.x + 5, explorer_context_menu.y + 5, explorer_context_menu.w, explorer_context_menu.h, shadow);
    fill(explorer_context_menu);
    outline(explorer_context_menu.x, explorer_context_menu.y, explorer_context_menu.w, explorer_context_menu.h, 0x5D738E);
    draw_button(explorer_ctx_terminal, 0xB4C6D6, 0xCFDCE7, "TERMINAL HERE", 0x20344E);
    draw_button(explorer_ctx_copy, 0xB4C6D6, 0xCFDCE7, "COPY", 0x20344E);
    draw_button(explorer_ctx_cut, 0xB4C6D6, 0xCFDCE7, "CUT", 0x20344E);
    draw_button(explorer_ctx_paste, 0xB4C6D6, 0xCFDCE7, "PASTE", 0x20344E);
    draw_button(explorer_ctx_rename, 0xB4C6D6, 0xCFDCE7, "RENAME", 0x20344E);
    draw_button(explorer_ctx_delete, 0xC99393, 0xDBB0B0, "DELETE", 0x4E1F1F);
    if (explorer_ctx_set_wall.w > 0) {
        draw_button(explorer_ctx_set_wall, 0xB4C6D6, 0xCFDCE7, "WALLPAPER", 0x20344E);
    }
    if (explorer_ctx_color.w > 0) {
        draw_button(explorer_ctx_color, 0xB4C6D6, 0xCFDCE7, "SET COLOR", 0x20344E);
    }
}"""
idx2 = content.find('static void draw_explorer_context_menu')
if idx2 != -1:
    end_idx2 = content.find('}', idx2) + 1
    content = content[:idx2] + draw_patch + content[end_idx2:]

# Now handle the clicks!
click_patch = """if (explorer_context_open && point_in_rect(mx, my, explorer_context_menu)) {
            if (point_in_rect(mx, my, explorer_ctx_terminal)) {
                extern char terminal_cwd[256];
                int n = 0; const char *cur = explorer_current_path();
                while (cur && cur[n] && n < 255) { terminal_cwd[n] = cur[n]; n++; }
                terminal_cwd[n] = 0;
                open_app(APP_TERMINAL);
            } else if (explorer_ctx_color.w > 0 && point_in_rect(mx, my, explorer_ctx_color)) {
                if (explorer_context_index >= 0) {
                    const explorer_entry_t *e = explorer_entry_at(explorer_context_index);
                    if (e && e->is_dir) {
                        char meta_path[256];
                        int m = 0; while(e->path[m]) { meta_path[m] = e->path[m]; m++; }
                        meta_path[m++] = '/';
                        const char *m_name = ".firstos_folder";
                        int n = 0; while(m_name[n]) { meta_path[m++] = m_name[n++]; }
                        meta_path[m] = 0;
                        
                        // Cycle colors: Green -> Red -> Blue -> Orange
                        unsigned char c[4] = {0};
                        if (fs_read_at(meta_path, (char*)c, 4) != 4) { c[0]=0x50; c[1]=0xC8; c[2]=0x78; c[3]=0xFF; } // Start Green
                        else if (c[0] == 0x50) { c[0]=0xC8; c[1]=0x50; c[2]=0x50; c[3]=0xFF; } // To Red
                        else if (c[0] == 0xC8) { c[0]=0x50; c[1]=0x50; c[2]=0xC8; c[3]=0xFF; } // To Blue
                        else if (c[0] == 0x50) { c[0]=0xFF; c[1]=0xA5; c[2]=0x00; c[3]=0xFF; } // To Orange
                        fs_write_at(meta_path, (char*)c, 4);
                        
                        // Force Explorer Refresh
                        explorer_open_path(explorer_current_path());
                    }
                }
            } else if (point_in_rect(mx, my, explorer_ctx_copy)) {"""

idx3 = content.find('if (explorer_context_open && point_in_rect(mx, my, explorer_context_menu)) {\n            if (point_in_rect(mx, my, explorer_ctx_copy)) {')
if idx3 != -1:
    content = content[:idx3] + click_patch + content[idx3 + 142:]

open('gui.c', 'w').write(content)
