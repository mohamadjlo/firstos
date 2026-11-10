import sys
content = open('settings.c').read()

idx = content.find('static void draw_appearance_pane')
end_idx = content.find('}', idx) + 1

new_draw = """static void draw_appearance_pane(int cx, int cy, int cw, int ch) {
    draw_label(cx + 20, cy + 20, "Appearance Overview", 0x20344E);
    
    // Dock Size Slider Mock
    extern int dock_base_size;
    draw_label(cx + 20, cy + 60, "Dock Base Size:", 0x555555);
    fb_rect(cx + 140, cy + 65, 200, 4, 0xCCCCCC);
    
    // Calculate slider position
    int slider_x = cx + 140 + ((dock_base_size - 24) * 200) / 76; // min 24, max 100
    if (slider_x < cx + 140) slider_x = cx + 140;
    if (slider_x > cx + 340) slider_x = cx + 340;
    
    // Draw Thumb
    fb_rect(slider_x - 4, cy + 60, 8, 14, 0x3D78C0);
    
    // Dock Magnification Toggle Mock
    extern int dock_magnification_enabled;
    draw_label(cx + 20, cy + 100, "Magnification:", 0x555555);
    fb_rect(cx + 140, cy + 100, 20, 20, dock_magnification_enabled ? 0x3D78C0 : 0xFFFFFF);
    outline(cx + 140, cy + 100, 20, 20, 0xCCCCCC);
    
    // IPC Handle dragging the slider
    extern int ps2_mouse_left(void);
    extern int ps2_mouse_x(void);
    extern int ps2_mouse_y(void);
    int mx = ps2_mouse_x(), my = ps2_mouse_y();
    if (ps2_mouse_left() && mx >= cx + 140 && mx <= cx + 340 && my >= cy + 50 && my <= cy + 80) {
        int new_size = 24 + ((mx - (cx + 140)) * 76) / 200;
        if (new_size < 24) new_size = 24;
        if (new_size > 100) new_size = 100;
        dock_base_size = new_size;
        extern int needs_redraw; needs_redraw = 1;
    }
}
"""

content = content[:idx] + new_draw + content[end_idx:]

# Handle click for toggle
idx2 = content.find('handle_settings_click(app_window_t *app, int mx, int my)')
end_idx2 = content.find('}', idx2) + 1

new_click = """void handle_settings_click(app_window_t *app, int mx, int my) {
    int cx = app->frame.x + 160;
    int cy = app->frame.y + 42;
    int i;
    for (i = 0; i < 10; i++) {
        if (mx >= app->frame.x && mx <= app->frame.x + 160 && my >= app->frame.y + 42 + i * 36 && my <= app->frame.y + 42 + (i + 1) * 36) {
            current_settings_tab = i;
            extern int needs_redraw; needs_redraw = 1;
            return;
        }
    }
    
    if (current_settings_tab == 1) { // Appearance
        extern int dock_magnification_enabled;
        if (mx >= cx + 140 && mx <= cx + 160 && my >= cy + 100 && my <= cy + 120) {
            dock_magnification_enabled = !dock_magnification_enabled;
            extern int needs_redraw; needs_redraw = 1;
        }
    }
}"""

content = content[:idx2] + new_click + content[end_idx2:]

open('settings.c', 'w').write(content)
