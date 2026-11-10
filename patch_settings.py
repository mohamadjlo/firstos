import sys
content = open('settings.c').read()

# Add Avatar click handler
if 'current_settings_tab == 0' in content:
    replacement = """if (current_settings_tab == 0) { // Account
        // Interactive profile picture
        if (my >= cy + 40 && my <= cy + 104 && mx >= cx + 20 && mx <= cx + 84) {
            extern int desktop_context_open;
            extern int desktop_context_x;
            extern int desktop_context_y;
            extern int desktop_context_is_explorer;
            desktop_context_open = 1;
            desktop_context_x = mx;
            desktop_context_y = my;
            desktop_context_is_explorer = 2; // Flag for Avatar context menu
            needs_redraw = 1;
            return;
        }
        
        // Mock profile update handling
        if (my >= cy + 150 && my <= cy + 180 && mx >= cx + 20 && mx <= cx + 180) {"""
    content = content.replace('if (current_settings_tab == 0) { // Account\n        // Mock profile update handling\n        if (my >= cy + 150 && my <= cy + 180 && mx >= cx + 20 && mx <= cx + 180) {', replacement)

open('settings.c', 'w').write(content)
