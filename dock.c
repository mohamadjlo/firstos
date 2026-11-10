#include "dock.h"
#include "gui_internal.h"
#include "framebuffer.h"
#include "font.h"
#include "firstlang.h"

int dock_base_size = 48;
int dock_magnification_enabled = 1;
int dock_animation_mode = DOCK_ANIM_MAGNIFY;
unsigned int dock_bg_color = 0x1A2D46;
unsigned char dock_bg_alpha = 195;
int dock_context_open = 0;
int dock_context_app_id = 0;

static dock_item_t dock_items[16];
static int num_dock_items = 0;
static int dock_hover_index = -1;
static int dock_anim_ticks = 0;
static float dock_slide_offset = 0.0f;

extern int load_ico_from_path(const char* path, u32* out_pixels, int* out_w, int* out_h);
extern void draw_icon_scaled(const u32 *pixels, int src_w, int src_h, int x, int y, int dst_w, int dst_h);

void dock_init(void) {
    num_dock_items = 0;
    
    // Add default pinned apps with their corresponding rich ICO paths
    dock_items[num_dock_items++] = (dock_item_t){APP_TERMINAL, ICON_TERMINAL, "Terminal", "/icons/terminal.ico", 0, 0, 1.0f, 0, 0, 0, 0, 0, {0}};
    dock_items[num_dock_items++] = (dock_item_t){APP_EXPLORER, ICON_EXPLORER, "Explorer", "/icons/explorer.ico", 0, 0, 1.0f, 0, 0, 0, 0, 0, {0}};
    dock_items[num_dock_items++] = (dock_item_t){APP_TEXT_EDIT, ICON_FILE, "Text Edit", "/icons/text_edit.ico", 0, 0, 1.0f, 0, 0, 0, 0, 0, {0}};
    dock_items[num_dock_items++] = (dock_item_t){APP_FILES, ICON_FILES, "Files", "/icons/files.ico", 0, 0, 1.0f, 0, 0, 0, 0, 0, {0}};
    dock_items[num_dock_items++] = (dock_item_t){APP_SETTINGS, ICON_SETTINGS, "Settings", "/icons/settings.ico", 0, 0, 1.0f, 0, 0, 0, 0, 0, {0}};
    dock_items[num_dock_items++] = (dock_item_t){APP_CALCULATOR, ICON_DISPLAY, "Calculator", "/icons/calculator.ico", 0, 0, 1.0f, 0, 0, 0, 0, 0, {0}};
    dock_items[num_dock_items++] = (dock_item_t){APP_TASKMAN, ICON_DISPLAY, "Task Mgr", "/icons/taskman.ico", 0, 0, 1.0f, 0, 0, 0, 0, 0, {0}};
    dock_items[num_dock_items++] = (dock_item_t){APP_IDE, ICON_FILE, "IDE", "/icons/ide.ico", 0, 0, 1.0f, 0, 0, 0, 0, 0, {0}};
    dock_items[num_dock_items++] = (dock_item_t){APP_MEDIA_PLAYER, ICON_DISPLAY, "Media", "/icons/audio_wav.ico", 0, 0, 1.0f, 0, 0, 0, 0, 0, {0}};
    dock_items[num_dock_items++] = (dock_item_t){APP_BROWSER, ICON_NETWORK, "Web Browser", "/icons/browser.ico", 0, 0, 1.0f, 0, 0, 0, 0, 0, {0}};
    dock_items[num_dock_items++] = (dock_item_t){0, ICON_APPS, "Launcher", "/icons/os.ico", 0, 0, 1.0f, 0, 0, 0, 0, 0, {0}};
}

extern app_window_t app_windows[32]; // from gui.c

static void update_running_state(void) {
    for (int i = 0; i < num_dock_items; i++) {
        dock_items[i].is_running = 0;
        dock_items[i].is_minimized = 0;
        if (dock_items[i].app_id > 0) {
            for (int j = 0; j < 32; j++) {
                if (app_windows[j].open && app_windows[j].kind == dock_items[i].app_id) {
                    dock_items[i].is_running = 1;
                    if (app_windows[j].minimized) dock_items[i].is_minimized = 1;
                    break;
                }
            }
        }
    }
}

void dock_update_and_render(int mouse_x, int mouse_y, int screen_width, int screen_height) {
    update_running_state();
    dock_anim_ticks++;

    // Slide animation mode handling:
    float target_slide = 0.0f;
    if (dock_animation_mode == DOCK_ANIM_SLIDE) {
        if (mouse_y < screen_height - (dock_base_size + 30)) {
            target_slide = (float)(dock_base_size + 15); // slide down
        }
        dock_slide_offset += (target_slide - dock_slide_offset) * 0.25f;
    } else {
        dock_slide_offset = 0.0f;
    }

    float total_width = 0;
    dock_hover_index = -1;
    
    // Step 1: Calculate scales based on mouse distance and animation mode
    for (int i = 0; i < num_dock_items; i++) {
        float item_center = dock_items[i].current_x + ((dock_base_size * dock_items[i].current_scale) / 2.0f);
        
        float dx = mouse_x - item_center;
        if (dx < 0) dx = -dx;
        
        float scale = 1.0f;
        if (dock_magnification_enabled && (dock_animation_mode == DOCK_ANIM_MAGNIFY || dock_animation_mode == DOCK_ANIM_GENIE)) {
            if (mouse_y > screen_height - dock_base_size * DOCK_MAX_SCALE - 20 && dx < DOCK_RADIUS) {
                float factor = 1.0f - (dx / (float)DOCK_RADIUS);
                scale = 1.0f + ((DOCK_MAX_SCALE - 1.0f) * factor);
                if (scale > DOCK_MAX_SCALE) scale = DOCK_MAX_SCALE;
            }
        }
        dock_items[i].current_scale = scale;
        total_width += (dock_base_size * dock_items[i].current_scale) + 10; // 10px padding
    }
    
    // Total dock rect logic (for background and bounds)
    total_width += 10; // Extra edge padding
    float current_x = (screen_width - total_width) / 2.0f;
    float dock_y = screen_height - (dock_base_size + 20) + dock_slide_offset; // Base height line
    
    // Draw background with alpha blending
    int dock_x = (int)current_x - 10;
    int dock_y_pos = (int)dock_y - 10;
    int dock_w = (int)total_width + 20;
    int dock_h = dock_base_size + 30;

    if (dock_bg_alpha < 255) {
        fb_round_rect_alpha(dock_x, dock_y_pos, dock_w, dock_h, DOCK_CORNER_RADIUS, dock_bg_color, dock_bg_alpha);
    } else {
        fb_round_rect(dock_x, dock_y_pos, dock_w, dock_h, DOCK_CORNER_RADIUS, dock_bg_color);
    }
    fb_round_rect_border(dock_x, dock_y_pos, dock_w, dock_h, DOCK_CORNER_RADIUS, 0x3A5878);
    fb_rect(dock_x + DOCK_CORNER_RADIUS, dock_y_pos + 1, dock_w - DOCK_CORNER_RADIUS * 2, 1, 0x4A6888); // top sheen

    // Step 2: Reposition and render items
    current_x += 10;
    for (int i = 0; i < num_dock_items; i++) {
        float scaled_size = dock_base_size * dock_items[i].current_scale;
        
        // Ease the position for smooth transition
        float target_x = current_x;
        float target_y = screen_height - scaled_size - 10 + dock_slide_offset; // Bottom aligned

        // Mode-specific item offset
        if (dock_animation_mode == DOCK_ANIM_BOUNCE) {
            float ic = current_x + scaled_size / 2.0f;
            float dx = mouse_x - ic;
            if (dx < 0) dx = -dx;
            if (dx < DOCK_RADIUS && mouse_y > screen_height - dock_base_size * 2) {
                int bounce_cycle = (dock_anim_ticks * 2 + i * 3) % 12;
                int bounce_amp = bounce_cycle < 6 ? bounce_cycle * 2 : (12 - bounce_cycle) * 2;
                float dist_factor = 1.0f - (dx / (float)DOCK_RADIUS);
                target_y -= (float)bounce_amp * dist_factor * 1.5f;
            }
        } else if (dock_animation_mode == DOCK_ANIM_GENIE) {
            float ic = current_x + scaled_size / 2.0f;
            float dx = mouse_x - ic;
            if (dx < 0) dx = -dx;
            if (dx < DOCK_RADIUS && mouse_y > screen_height - dock_base_size * 2) {
                int wave_cycle = (dock_anim_ticks + i * 2) % 16;
                int wave_amp = wave_cycle < 8 ? wave_cycle : (16 - wave_cycle);
                float dist_factor = 1.0f - (dx / (float)DOCK_RADIUS);
                target_y -= (float)wave_amp * dist_factor * 1.2f;
            }
        }
        
        // If it's the first frame, snap. Else LERP.
        if (dock_items[i].current_x == 0) {
            dock_items[i].current_x = target_x;
            dock_items[i].current_y = target_y;
        } else {
            dock_items[i].current_x += (target_x - dock_items[i].current_x) * 0.35f;
            dock_items[i].current_y += (target_y - dock_items[i].current_y) * 0.35f;
        }
        
        int ix = (int)dock_items[i].current_x;
        int iy = (int)dock_items[i].current_y;
        int isz = (int)scaled_size;
        
        // Hover detection
        if (mouse_x >= ix && mouse_x <= ix + isz && mouse_y >= iy && mouse_y <= iy + isz) {
            dock_hover_index = i;
        }
        
        int hov = (dock_hover_index == i);
        
        // Highlight
        if (hov) {
            fb_round_rect_alpha(ix - 3, iy - 3, isz + 6, isz + 6, 8, 0x2A4A6A, 180);
            fb_round_rect_border(ix - 3, iy - 3, isz + 6, isz + 6, 8, 0x5A8AB0);
        }
        
        // Lazy load icon if not loaded yet
        if (!dock_items[i].icon_loaded) {
            dock_items[i].icon_loaded = 1;
            if (dock_items[i].icon_path[0]) {
                load_ico_from_path(dock_items[i].icon_path, dock_items[i].icon_pixels, &dock_items[i].icon_w, &dock_items[i].icon_h);
            }
        }
        
        // Render rich graphical icon if loaded, otherwise fallback to font_icon
        if (dock_items[i].icon_w > 0 && dock_items[i].icon_h > 0) {
            int icon_size = isz - 12;
            if (icon_size < 16) icon_size = 16;
            int icon_x = ix + (isz - icon_size) / 2;
            int icon_y = iy + (isz - icon_size) / 2;
            draw_icon_scaled(dock_items[i].icon_pixels, dock_items[i].icon_w, dock_items[i].icon_h, icon_x, icon_y, icon_size, icon_size);
            
            // Dim if minimized
            if (dock_items[i].is_minimized) {
                fb_round_rect_alpha(icon_x, icon_y, icon_size, icon_size, 6, 0x0A1520, 150);
            }
        } else {
            int icon_color = hov ? 0xFFFFFF : (dock_items[i].is_running ? 0xAADDFF : 0x7AAAC8);
            if (dock_items[i].is_minimized) icon_color = 0x666666; // Dim minimized apps
            font_icon(ix + (isz - 16) / 2, iy + (isz - 16) / 2, dock_items[i].icon_id, icon_color);
        }
        
        // Running dot
        if (dock_items[i].is_running) {
            fb_rect(ix + isz/2 - 2, iy + isz + 4, 4, 4, hov ? 0xFFFFFF : 0x6AA8D8);
        }
        
        // Tooltip
        if (hov) {
            extern void gui_tooltip_show(int x, int y, const char *text); // in gui.h
            gui_tooltip_show(ix + isz / 2, iy - 30, dock_items[i].name);
        }
        
        current_x += scaled_size + 10;
    }
}

int dock_handle_click(int mouse_x, int mouse_y, int right_click) {
    if (dock_hover_index >= 0 && dock_hover_index < num_dock_items) {
        int ix = (int)dock_items[dock_hover_index].current_x;
        int iy = (int)dock_items[dock_hover_index].current_y;
        int isz = (int)(dock_base_size * dock_items[dock_hover_index].current_scale);
        if (mouse_x < ix || mouse_x > ix + isz || mouse_y < iy || mouse_y > iy + isz) {
            return 0;
        }
        if (right_click) {
            dock_context_open = 1;
            dock_context_app_id = dock_items[dock_hover_index].app_id;
            dock_context_x = (int)dock_items[dock_hover_index].current_x + (dock_base_size * dock_items[dock_hover_index].current_scale) / 2;
            dock_context_y = (int)dock_items[dock_hover_index].current_y;
        } else {
            // Left click: Open or Restore
            int app_id = dock_items[dock_hover_index].app_id;
            if (app_id == 0) {
                // Launcher
                extern int launcher_open; // Need to expose or call launcher toggle
                // We'll handle this in gui.c
                return -1; // Special code for launcher
            }

            if (app_id == APP_BROWSER) {
                extern void web_browser_app_launch(const char *url);
                web_browser_app_launch(0);
                return 1;
            }
            
            // If already open, restore/focus it
            int found = 0;
            for (int j = 0; j < 32; j++) {
                if (app_windows[j].open && app_windows[j].kind == app_id) {
                    app_windows[j].minimized = 0;
                    extern void focus_app_window(int index);
                    focus_app_window(j);
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                extern void open_app(int kind);
                open_app(app_id);
            }
        }
        return 1;
    }
    return 0;
}

void dock_get_item_rect(int app_id, int *x, int *y, int *w, int *h) {
    for (int i = 0; i < num_dock_items; i++) {
        if (dock_items[i].app_id == app_id) {
            *x = (int)dock_items[i].current_x;
            *y = (int)dock_items[i].current_y;
            *w = (int)(dock_base_size * dock_items[i].current_scale);
            *h = *w;
            return;
        }
    }
    // Fallback if not pinned
    extern framebuffer_t fb;
    *x = fb.width / 2;
    *y = fb.height;
    *w = 10; *h = 10;
}

int dock_context_x = 0;
int dock_context_y = 0;

void dock_context_draw(void) {
    if (!dock_context_open) return;
    int tw = 150;
    int th = 30 * 4 + 10;
    int cx = dock_context_x - tw / 2;
    int cy = dock_context_y - th;
    
    fb_rect(cx + 4, cy + 4, tw, th, 0x050D18); // Shadow
    fb_rect(cx, cy, tw, th, 0x1A2D46);         // Background
    outline(cx, cy, tw, th, 0x3A5878);         // Border
    
    // Items
    draw_label(cx + 10, cy + 10, "Open / Restore", 0xFFFFFF);
    draw_label(cx + 10, cy + 40, "New Window", 0x7EBAFF);
    draw_label(cx + 10, cy + 70, "Close", 0xFFFFFF);
    draw_label(cx + 10, cy + 100, "Remove from Dock", 0xAAAAAA);
}

int dock_context_handle_click(int mx, int my) {
    if (!dock_context_open) return 0;
    
    int tw = 150;
    int th = 30 * 4 + 10;
    int cx = dock_context_x - tw / 2;
    int cy = dock_context_y - th;
    
    if (mx >= cx && mx <= cx + tw && my >= cy && my <= cy + th) {
        int index = (my - cy - 10) / 30;
        if (index == 0) { // Restore
            for (int j = 0; j < 32; j++) {
                if (app_windows[j].open && app_windows[j].kind == dock_context_app_id) {
                    app_windows[j].minimized = 0;
                    extern void focus_app_window(int index);
                    focus_app_window(j);
                    break;
                }
            }
        } else if (index == 1) { // New Window
            extern app_window_t* open_new_window(int kind);
            open_new_window(dock_context_app_id);
        } else if (index == 2) { // Close
            for (int j = 0; j < 32; j++) {
                if (app_windows[j].open && app_windows[j].kind == dock_context_app_id) {
                    app_windows[j].open = 0;
                    extern int active_app_index;
                    if (active_app_index == j) active_app_index = -1;
                }
            }
        }
        dock_context_open = 0;
        return 1;
    }
    dock_context_open = 0; // Clicked outside
    return 1; // Still consume click to close menu
}

void dock_reset_positions(void) {
    for (int i = 0; i < num_dock_items; i++) {
        dock_items[i].current_x = 0.0f;
        dock_items[i].current_y = 0.0f;
        dock_items[i].current_scale = 1.0f;
    }
}
