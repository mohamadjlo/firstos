import sys
content = open('settings.c').read()

# Fix void void
content = content.replace('void void handle_settings_click', 'void handle_settings_click')

# Remove the extra inserted duplicate
if 'if (current_settings_tab == 1) { // Appearance\n        extern int dock_magnification_enabled;\n        if (mx >= cx + 140 && mx <= cx + 160 && my >= cy + 100 && my <= cy + 120) {\n            dock_magnification_enabled = !dock_magnification_enabled;\n            extern int needs_redraw; needs_redraw = 1;\n        }\n    }\n}\n        }\n    }\n' in content:
    content = content.replace('if (current_settings_tab == 1) { // Appearance\n        extern int dock_magnification_enabled;\n        if (mx >= cx + 140 && mx <= cx + 160 && my >= cy + 100 && my <= cy + 120) {\n            dock_magnification_enabled = !dock_magnification_enabled;\n            extern int needs_redraw; needs_redraw = 1;\n        }\n    }\n}\n        }\n    }\n', '')

# Insert the Appearance click check properly inside the EXISTING handle_settings_click
idx = content.find('if (current_settings_tab == 0) { // Account')
if idx != -1:
    patch = """if (current_settings_tab == 1) { // Appearance
        extern int dock_magnification_enabled;
        if (mx >= cx + 140 && mx <= cx + 160 && my >= cy + 100 && my <= cy + 120) {
            dock_magnification_enabled = !dock_magnification_enabled;
            extern int needs_redraw; needs_redraw = 1;
        }
    }
    
    if (current_settings_tab == 0) { // Account"""
    content = content[:idx] + patch + content[idx + 43:]

open('settings.c', 'w').write(content)
