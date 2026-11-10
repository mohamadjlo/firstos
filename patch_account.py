import sys
content = open('settings.c').read()

idx = content.find('static void draw_account_pane')
end_idx = content.find('}', idx) + 1

new_draw = """static void draw_account_pane(int cx, int cy, int cw, int ch) {
    draw_label(cx + 20, cy + 20, "Account Overview", 0x20344E);
    
    // Avatar
    fb_rect(cx + 20, cy + 50, 64, 64, 0xA0AAB5);
    draw_label(cx + 35, cy + 75, "PFP", 0xFFFFFF);
    
    draw_label(cx + 100, cy + 50, "Username:", 0x555555);
    fb_rect(cx + 100, cy + 70, 200, 30, 0xFFFFFF);
    outline(cx + 100, cy + 70, 200, 30, 0xCCCCCC);
    draw_label(cx + 110, cy + 78, account_username, 0x000000);
    
    // Passwords
    draw_label(cx + 20, cy + 130, "Current Password:", 0x555555);
    fb_rect(cx + 20, cy + 150, 280, 30, 0xFFFFFF);
    outline(cx + 20, cy + 150, 280, 30, 0xCCCCCC);
    draw_label(cx + 30, cy + 158, "********", 0x000000);
    
    draw_label(cx + 20, cy + 190, "New Password:", 0x555555);
    fb_rect(cx + 20, cy + 210, 280, 30, 0xFFFFFF);
    outline(cx + 20, cy + 210, 280, 30, 0xCCCCCC);
    
    draw_label(cx + 20, cy + 250, "Confirm Password:", 0x555555);
    fb_rect(cx + 20, cy + 270, 280, 30, 0xFFFFFF);
    outline(cx + 20, cy + 270, 280, 30, 0xCCCCCC);
    
    draw_button((rect_t){cx + 20, cy + 320, 120, 30, 0}, 0x3D78C0, 0x4D88D0, "Update Profile", 0xFFFFFF);
}"""

content = content[:idx] + new_draw + content[end_idx:]
open('settings.c', 'w').write(content)
