#include "desktop.h"
#include "theme.h"
#include "font.h"
#include "framebuffer.h"
#include "fs.h"
#include "serial.h"
static int str_eq(const char *a, const char *b) { while (*a && *b) { if (*a++ != *b++) return 0; } return *a == 0 && *b == 0; }


typedef struct {
    rect_t icon_rect;
    char name[128];
    char path[256];
    int used;
    int is_dir;
    int rename_mode;
    int cursor;
    int last_click_ticks;
    int last_click;
    int custom_x;
    int custom_y;

    int has_custom_pos;
} desktop_item_t;

static desktop_item_t desktop_items[32];
static int desktop_item_count;
static int desktop_selected = -1;
static int desktop_context_index = -1;

static rect_t desktop_bar = {0, 0, 0, 28, 0x15314A};
static int desktop_start_button_visible = 0;
static rect_t start_button = {0, 0, 0, 0, 0};
static rect_t start_menu_rect = {14, 34, 262, 296, 0xD7DEE8};
static rect_t start_apps_item = {106, 102, 148, 22, 0xC8D5E3};
static rect_t start_explorer_item = {106, 154, 148, 22, 0xC8D5E3};
static rect_t start_settings_item = {106, 226, 148, 22, 0xC8D5E3};
static rect_t start_textedit_item = {106, 128, 148, 22, 0xD6C3B2};
static rect_t start_shutdown_item = {106, 278, 148, 22, 0xC88F8F};
static rect_t start_restart_item = {106, 252, 148, 22, 0xC8D5E3};

static u32 desktop_bg = 0x102033;
static u32 desktop_panel = 0x18304C;
static u32 desktop_shadow = 0x102030;

static int start_menu_open;
static int desktop_context_open;
static rect_t desktop_context_menu;
static rect_t desktop_ctx_refresh;
static rect_t desktop_ctx_change_wall;
static rect_t desktop_ctx_settings;
static rect_t desktop_ctx_textedit;
static rect_t desktop_ctx_new_folder;
static rect_t desktop_ctx_new_file;
static rect_t desktop_ctx_copy;
static rect_t desktop_ctx_paste;
static rect_t desktop_ctx_rename;
static rect_t desktop_ctx_delete;
static int desktop_context_target;

static u32 brand_ico_pixels[32 * 32];
static int brand_ico_width;
static int brand_ico_height;
static int brand_ico_loaded;

static u32 start_menu_ico_pixels[32 * 32];
static int start_menu_ico_width;
static int start_menu_ico_height;
static int start_menu_ico_loaded;

static void fill(rect_t r) {
    fb_rect(r.x, r.y, r.w, r.h, r.color);
}

static int menu_hover(rect_t r) {
    (void)r;
    return 0;
}

static void draw_desktop_button(rect_t r, u32 fill_color, u32 inner_color, const char *label, u32 text_color) {
    draw_soft_box(r, fill_color, 0x7A90AC, inner_color);
    if (label && label[0]) draw_label(r.x + 8, r.y + 6, label, text_color);
}

static void desktop_copy_name(char *dst, int max, const char *src) {
    int i;
    for (i = 0; i < max - 1 && src && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

void desktop_apply_theme(void) {
    theme_apply();
    const theme_colors_t *tc = theme_get_colors();
    desktop_bg = tc->desktop_bg;
    desktop_panel = tc->top_bar;
    desktop_bar.color = tc->top_bar;
    apply_theme();
}

void desktop_sync_icons(void) {
    desktop_item_t old_items[32];
    int old_count = desktop_item_count;
    for(int k=0; k<old_count; k++) old_items[k] = desktop_items[k];
    const fs_entry_t *entry;
    int slot = 0;
    desktop_item_count = 0;
    
    // Virtual shortcut 1: Explorer
    desktop_items[desktop_item_count].used = 1;
    desktop_items[desktop_item_count].is_dir = 1; // Show as folder/app
    desktop_items[desktop_item_count].rename_mode = 0;
    desktop_items[desktop_item_count].cursor = 0;
    desktop_items[desktop_item_count].last_click = 0;
    desktop_items[desktop_item_count].last_click_ticks = 0;
    desktop_copy_name(desktop_items[desktop_item_count].name, sizeof(desktop_items[0].name), "Explorer");
    desktop_copy_name(desktop_items[desktop_item_count].path, sizeof(desktop_items[0].path), "/"); // Root
    desktop_items[desktop_item_count].has_custom_pos = 0;
    for (int k = 0; k < old_count; k++) {
        if (old_items[k].used && str_eq(old_items[k].path, "/")) {
            if (old_items[k].has_custom_pos) {
                desktop_items[desktop_item_count].has_custom_pos = 1;
                desktop_items[desktop_item_count].custom_x = old_items[k].custom_x;
                desktop_items[desktop_item_count].custom_y = old_items[k].custom_y;
            }
            break;
        }
    }
    desktop_item_count++;

    // Virtual shortcut 2: Trash
    desktop_items[desktop_item_count].used = 1;
    desktop_items[desktop_item_count].is_dir = 1;
    desktop_items[desktop_item_count].rename_mode = 0;
    desktop_items[desktop_item_count].cursor = 0;
    desktop_items[desktop_item_count].last_click = 0;
    desktop_items[desktop_item_count].last_click_ticks = 0;
    desktop_copy_name(desktop_items[desktop_item_count].name, sizeof(desktop_items[0].name), "Trash");
    desktop_copy_name(desktop_items[desktop_item_count].path, sizeof(desktop_items[0].path), "/Trash");
    desktop_items[desktop_item_count].has_custom_pos = 0;
    for (int k = 0; k < old_count; k++) {
        if (old_items[k].used && str_eq(old_items[k].path, "/Trash")) {
            if (old_items[k].has_custom_pos) {
                desktop_items[desktop_item_count].has_custom_pos = 1;
                desktop_items[desktop_item_count].custom_x = old_items[k].custom_x;
                desktop_items[desktop_item_count].custom_y = old_items[k].custom_y;
            }
            break;
        }
    }
    desktop_item_count++;

    while ((entry = fs_entry_at(slot++)) && desktop_item_count < 32) {
        if (!entry->used) continue;
        if (desktop_item_count >= 32) break;
        // Don't show Trash twice if it exists
        if (entry->name[0] == 'T' && entry->name[1] == 'r' && entry->name[2] == 'a' && entry->name[3] == 's' && entry->name[4] == 'h' && entry->name[5] == 0) continue;

        desktop_items[desktop_item_count].used = 1;
        desktop_items[desktop_item_count].is_dir = entry->is_dir;
        desktop_items[desktop_item_count].rename_mode = 0;
        desktop_items[desktop_item_count].cursor = 0;
        desktop_items[desktop_item_count].last_click = 0;
        desktop_items[desktop_item_count].last_click_ticks = 0;
        desktop_copy_name(desktop_items[desktop_item_count].name, sizeof(desktop_items[desktop_item_count].name), entry->name);
        desktop_copy_name(desktop_items[desktop_item_count].path, sizeof(desktop_items[desktop_item_count].path), entry->path);
        desktop_items[desktop_item_count].has_custom_pos = 0;
        for (int k = 0; k < old_count; k++) {
            if (old_items[k].used && str_eq(old_items[k].path, entry->path)) {
                if (old_items[k].has_custom_pos) {
                    desktop_items[desktop_item_count].has_custom_pos = 1;
                    desktop_items[desktop_item_count].custom_x = old_items[k].custom_x;
                    desktop_items[desktop_item_count].custom_y = old_items[k].custom_y;
                }
                break;
            }
        }
        desktop_item_count++;
    }
    if (desktop_selected >= desktop_item_count) desktop_selected = -1;
    if (desktop_context_index >= desktop_item_count) desktop_context_index = -1;
}

static int load_icon_file(const char *primary, const char *fallback, u32 *pixels, int *out_w, int *out_h) {
    unsigned char ico_data[4096];
    int bytes;
    unsigned int offset;
    unsigned int dib_size;
    int width;
    int bmp_height;
    int height;
    int stride;
    int row;
    int col;
    int src_y;
    int src_x;
    unsigned char *img;
    int outw;
    int outh;

    bytes = fs_read_at(primary, (char*)ico_data, sizeof(ico_data));
    if (bytes < 22 && fallback) bytes = fs_read_at(fallback, (char*)ico_data, sizeof(ico_data));
    if (bytes < 22) return 0;
    offset = *(unsigned int*)(ico_data + 18);
    if (offset + 16 >= (unsigned int)bytes) return 0;
    dib_size = *(unsigned int*)(ico_data + offset);
    width = *(int*)(ico_data + offset + 4);
    bmp_height = *(int*)(ico_data + offset + 8);
    if (width < 1 || bmp_height < 2) return 0;
    height = bmp_height / 2;
    if (height < 1) return 0;
    img = ico_data + offset + dib_size;
    stride = width * 4;
    if (offset + dib_size + (unsigned int)(stride * height) > (unsigned int)bytes) return 0;
    outw = width > 32 ? 32 : width;
    outh = height > 32 ? 32 : height;
    for (row = 0; row < outh; row++) {
        src_y = height - 1 - ((row * height) / outh);
        for (col = 0; col < outw; col++) {
            u32 a;
            u32 color;
            unsigned char *px;
            src_x = (col * width) / outw;
            px = img + src_y * stride + src_x * 4;
            a = px[3];
            color = ((u32)px[2] << 16) | ((u32)px[1] << 8) | (u32)px[0];
            if (a < 16) color = 0xFF00FF;
            pixels[row * 32 + col] = color;
        }
        for (; col < 32; col++) pixels[row * 32 + col] = 0xFF00FF;
    }
    for (; row < 32; row++) for (col = 0; col < 32; col++) pixels[row * 32 + col] = 0xFF00FF;
    *out_w = outw;
    *out_h = outh;
    return 1;
}

static int load_brand_ico(void) {
    if (brand_ico_loaded) return brand_ico_width > 0 && brand_ico_height > 0;
    brand_ico_loaded = 1;
    if (!load_icon_file("/firstOS.ico", "/icons/firstOS.ico", brand_ico_pixels, &brand_ico_width, &brand_ico_height) &&
        !load_icon_file("/icons/os.ico", "/icons/os.ico", brand_ico_pixels, &brand_ico_width, &brand_ico_height)) {
        brand_ico_width = 0;
        brand_ico_height = 0;
        return 0;
    }
    return 1;
}

static int load_start_menu_ico(void) {
    if (start_menu_ico_loaded) return start_menu_ico_width > 0 && start_menu_ico_height > 0;
    start_menu_ico_loaded = 1;
    if (!load_icon_file("/start_menu.ico", "/icons/start_menu.ico", start_menu_ico_pixels, &start_menu_ico_width, &start_menu_ico_height)) {
        start_menu_ico_width = 0;
        start_menu_ico_height = 0;
        return 0;
    }
    return 1;
}

static void draw_brand_icon(int x, int y) {
    if (!brand_ico_loaded) load_brand_ico();
    if (brand_ico_width > 0 && brand_ico_height > 0) {
        draw_icon_scaled(brand_ico_pixels, brand_ico_width, brand_ico_height, x, y, 16, 16);
        return;
    }
    // Fallback: exact 16x16 vector rendering from icons/os.svg
    static const u32 os_svg_fallback[16 * 16] = {
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x899090, 0x57605D, 0x3D4644, 0x3D4644, 0x57605D, 0x8C9090, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x878E8E, 0x4E5754, 0x2D3738, 0x404845, 0x404844, 0x2D3738, 0x4E5754, 0x878E8E, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x545D5A, 0x2F3939, 0x7E806C, 0xEAE0AD, 0xEAE0AD, 0x7E806C, 0x2F3939, 0x555D5A, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x3D4745, 0x3F4845, 0xEBDFAC, 0xFFF3B7, 0xFFF3B7, 0xEBDFAC, 0x3F4845, 0x3D4745, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x343E3D, 0x474F49, 0xECE0AA, 0xFAECB2, 0xFAECB2, 0xECE0AA, 0x474F49, 0x343E3D, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x343E3D, 0x474F49, 0xECE0AA, 0xFAECB2, 0xFAECB2, 0xECE0AA, 0x474F49, 0x343E3D, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x3D4745, 0x3F4845, 0xEBDFAC, 0xFFF3B7, 0xFFF3B7, 0xEBDFAC, 0x3F4845, 0x3D4745, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x555C5A, 0x2E3939, 0x7E7F6D, 0xE9DFAC, 0xE9DFAC, 0x7E7F6D, 0x2E3939, 0x555C5A, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x88908C, 0x4F5956, 0x2F3939, 0x414946, 0x414946, 0x2F3939, 0x4F5956, 0x88908C, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x838A8A, 0x555D5A, 0x3D4644, 0x3D4644, 0x555D5A, 0x868A8A, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x676F6F, 0x676F6F, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xFF00FF, 0xFF00FF, 0x9BA29E, 0x6D7474, 0x4C5552, 0x444C4B, 0x37413E, 0x37413E, 0x444C4B, 0x4C5552, 0x6D7474, 0x9BA29E, 0xFF00FF, 0xFF00FF, 0xFF00FF,
        0xFF00FF, 0xB1B8B8, 0x636A67, 0x434C4A, 0x3D4745, 0x3F4947, 0x48514F, 0x414A48, 0x414A48, 0x48514F, 0x3F4947, 0x3D4745, 0x434C4A, 0x636967, 0xB1B8B8, 0xFF00FF,
        0x8F9393, 0x525B59, 0x38423F, 0x424B49, 0x565D5B, 0x6B7270, 0x9AA29E, 0x9EA5A5, 0x9EA5A5, 0x9AA29E, 0x6B7272, 0x565D5B, 0x424B49, 0x38423F, 0x525B59, 0x8F9393,
        0x4D5653, 0x323C3A, 0x525B59, 0x868D8D, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0x868D8D, 0x525B59, 0x323C3A, 0x4D5653,
        0x4F5756, 0x505857, 0xB4BDBD, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xFF00FF, 0xB4BDBD, 0x505957, 0x4F5856
    };
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            u32 col = os_svg_fallback[r * 16 + c];
            if (col != 0xFF00FF) fb_putpixel(x + c, y + r, col);
        }
    }
}

static void draw_start_menu_icon(int x, int y) {
    if (!start_menu_ico_loaded) load_start_menu_ico();
    if (start_menu_ico_width > 0 && start_menu_ico_height > 0) {
        draw_icon_scaled(start_menu_ico_pixels, start_menu_ico_width, start_menu_ico_height, x, y, 18, 18);
        return;
    }
    fb_rect(x, y, 18, 18, 0x1F5FBF);
    fb_rect(x + 1, y + 1, 16, 16, 0x2C74DC);
    fb_rect(x + 3, y + 3, 5, 5, 0xFFFFFF);
    fb_rect(x + 10, y + 3, 5, 5, 0xDCEBFF);
    fb_rect(x + 3, y + 10, 5, 5, 0xDCEBFF);
    fb_rect(x + 10, y + 10, 5, 5, 0xFFFFFF);
}

static void draw_desktop_items(void) {
    int i;
    int x = 28;
    int y = 50;
    int cell_w = 140;
    int cell_h = 72;
    int usable_w = (int)fb.width - 48;
    int cols = usable_w / cell_w;
    if (cols < 1) cols = 1;
    extern int desktop_dragging;
    extern int desktop_drag_index;
    extern int desktop_drag_moved;
    
    for (i = 0; i < desktop_item_count; i++) {
        int item_x, item_y;
        if (desktop_items[i].has_custom_pos) {
            item_x = desktop_items[i].custom_x;
            item_y = desktop_items[i].custom_y;
        } else {
            int col = i % cols;
            int row = i / cols;
            item_x = x + col * cell_w;
            item_y = y + row * cell_h;
        }
        if (desktop_dragging && desktop_drag_moved && i == desktop_drag_index) {
            extern int desktop_drag_mx;
            extern int desktop_drag_my;
            item_x = desktop_drag_mx - 24;
            item_y = desktop_drag_my - 24;
        }
        if (item_x + 110 > (int)fb.width - 12) item_x = (int)fb.width - 122;
        desktop_items[i].icon_rect = (rect_t){item_x, item_y, 54, 58, 0};
        
        // Draw selection background if selected
        extern unsigned char desktop_multi_selected[32];
        if (desktop_multi_selected[i] || desktop_selected == i) {
            fb_rect(item_x, item_y, 54, 58, 0x5D738E);
        }
        
        font_icon(item_x + 14, item_y + 4, desktop_items[i].is_dir ? ICON_FOLDER : ICON_FILE, desktop_items[i].is_dir ? 0xD9C58F : 0xE8D7B9);
        
        if (desktop_items[i].rename_mode) {
            fb_rect(item_x - 4, item_y + 58, 116, 18, 0xF6F1E7);
            outline(item_x - 4, item_y + 58, 116, 18, 0x6C92C0);
            font_text_fit(item_x, item_y + 64, desktop_items[i].name, 0x24384F, 108);
        } else {
            font_text_fit_center(item_x - 20, item_y + 64, desktop_items[i].name, 0xFFFFFF, 94);
        }
    }
}

static void layout_desktop_context_menu(int mx, int my) {
    int y = my;
    if (y < desktop_bar.h + 8) y = desktop_bar.h + 8;
    desktop_context_menu = (rect_t){mx, y, 172, desktop_context_target ? 118 : 218, 0xD7DEE8};
    desktop_ctx_copy = (rect_t){mx + 10, y + 10, 152, 20, 0xB4C6D6};
    desktop_ctx_paste = (rect_t){mx + 10, y + 36, 152, 20, 0xB4C6D6};
    if (desktop_context_target) {
        desktop_ctx_rename = (rect_t){mx + 10, y + 62, 152, 20, 0xB4C6D6};
        desktop_ctx_delete = (rect_t){mx + 10, y + 88, 152, 20, 0xC99393};
        return;
    }
    desktop_ctx_refresh = (rect_t){mx + 10, y + 62, 152, 20, 0xBBC8D8};
    desktop_ctx_change_wall = (rect_t){mx + 10, y + 88, 152, 20, 0x98B8D8};
    desktop_ctx_settings = (rect_t){mx + 10, y + 114, 152, 20, 0xB4C6D6};
    desktop_ctx_textedit = (rect_t){mx + 10, y + 140, 152, 20, 0xB79F8C};
    desktop_ctx_new_folder = (rect_t){mx + 10, y + 166, 152, 20, 0x9DBB8D};
    desktop_ctx_new_file = (rect_t){mx + 10, y + 192, 152, 20, 0x90A9C8};
}

static void draw_desktop_context_menu(void) {
    if (!desktop_context_open) return;
    fb_rect(desktop_context_menu.x + 6, desktop_context_menu.y + 6, desktop_context_menu.w, desktop_context_menu.h, desktop_shadow);
    fill(desktop_context_menu);
    outline(desktop_context_menu.x, desktop_context_menu.y, desktop_context_menu.w, desktop_context_menu.h, 0x5D738E);
    if (desktop_context_target) {
        draw_desktop_button(desktop_ctx_copy, 0xB4C6D6, 0xCFDCE7, "COPY", 0x20344E);
        draw_desktop_button(desktop_ctx_paste, 0xB4C6D6, 0xCFDCE7, "PASTE", 0x20344E);
        draw_desktop_button(desktop_ctx_rename, 0xB4C6D6, 0xCFDCE7, "RENAME", 0x20344E);
        draw_desktop_button(desktop_ctx_delete, 0xC99393, 0xDBB0B0, "DELETE", 0x4E1F1F);
        return;
    }
    draw_desktop_button(desktop_ctx_copy, 0xB4C6D6, 0xCFDCE7, "COPY", 0x20344E);
    draw_desktop_button(desktop_ctx_paste, 0xB4C6D6, 0xCFDCE7, "PASTE", 0x20344E);
    draw_desktop_button(desktop_ctx_refresh, 0xBBC8D8, 0xD5E1EC, "REFRESH", 0x20344E);
    draw_desktop_button(desktop_ctx_change_wall, 0x98B8D8, 0xC0D4E8, "CHANGE WALLPAPER", 0x18324E);
    draw_desktop_button(desktop_ctx_settings, 0xB4C6D6, 0xCFDCE7, "SETTINGS", 0x20344E);
    draw_desktop_button(desktop_ctx_textedit, 0xB79F8C, 0xD8C5B5, "TEXT EDIT", 0x3D291A);
    draw_desktop_button(desktop_ctx_new_folder, 0x9DBB8D, 0xC4D9BA, "NEW FOLDER", 0x24411D);
    draw_desktop_button(desktop_ctx_new_file, 0x90A9C8, 0xBED0E5, "NEW TXT FILE", 0x223A56);
}

static void draw_start_menu(void) {
    if (!start_menu_open) return;
    fb_rect(start_menu_rect.x + 12, start_menu_rect.y + 12, start_menu_rect.w, start_menu_rect.h, desktop_shadow);
    fb_rect(start_menu_rect.x, start_menu_rect.y, start_menu_rect.w, start_menu_rect.h, active_theme.start_menu_bg);
    outline(start_menu_rect.x, start_menu_rect.y, start_menu_rect.w, start_menu_rect.h, 0x8EA1B8);
    fb_rect(start_menu_rect.x, start_menu_rect.y, 74, start_menu_rect.h, 0x2F5EAA);
    fb_rect(start_menu_rect.x + 12, start_menu_rect.y + 14, start_menu_rect.w - 24, 46, 0xF4F7FB);
    outline(start_menu_rect.x + 12, start_menu_rect.y + 14, start_menu_rect.w - 24, 46, 0xC3CFDD);
    draw_start_menu_icon(start_menu_rect.x + 24, start_menu_rect.y + 27);
    draw_label(start_menu_rect.x + 58, start_menu_rect.y + 26, "Start", 0x182230);
    draw_label(start_menu_rect.x + 58, start_menu_rect.y + 40, "FirstOS quick access", 0x5C6C7E);
    draw_label(start_menu_rect.x + 92, start_menu_rect.y + 76, "Applications", 0x45576B);
    fb_rect(start_menu_rect.x + 92, start_menu_rect.y + 90, start_menu_rect.w - 110, 1, 0xC2CDDA);
    draw_desktop_button(start_apps_item, 0xDCE6F2, 0xEAF1F8, "", 0x20344E);
    font_icon(start_apps_item.x + 10, start_apps_item.y + 6, ICON_FILE, 0x20344E);
    font_text_fit(start_apps_item.x + 30, start_apps_item.y + 8, "TERMINAL", 0x20344E, start_apps_item.w - 38);
    draw_desktop_button(start_textedit_item, 0xE6DDD3, 0xF0E8DF, "", 0x3D291A);
    font_icon(start_textedit_item.x + 10, start_textedit_item.y + 6, ICON_FILE, 0x3D291A);
    font_text_fit(start_textedit_item.x + 30, start_textedit_item.y + 8, "TEXT EDIT", 0x3D291A, start_textedit_item.w - 38);
    draw_desktop_button(start_explorer_item, 0xD8E3EE, 0xE7EEF5, "", 0x20344E);
    font_icon(start_explorer_item.x + 10, start_explorer_item.y + 6, ICON_FOLDER, 0x20344E);
    font_text_fit(start_explorer_item.x + 30, start_explorer_item.y + 8, "EXPLORER", 0x20344E, start_explorer_item.w - 38);
    draw_label(start_menu_rect.x + 92, start_menu_rect.y + 200, "System", 0x45576B);
    fb_rect(start_menu_rect.x + 92, start_menu_rect.y + 214, start_menu_rect.w - 110, 1, 0xC2CDDA);
    draw_desktop_button(start_settings_item, 0xD7E1EC, 0xE4EBF3, "", 0x20344E);
    font_icon(start_settings_item.x + 10, start_settings_item.y + 6, ICON_SETTINGS, 0x20344E);
    font_text_fit(start_settings_item.x + 30, start_settings_item.y + 8, "SETTINGS", 0x20344E, start_settings_item.w - 38);
    draw_desktop_button(start_restart_item, 0xD3DEEA, 0xE2E9F1, "", 0x20344E);
    font_icon(start_restart_item.x + 10, start_restart_item.y + 6, ICON_DISPLAY, 0x20344E);
    font_text_fit(start_restart_item.x + 30, start_restart_item.y + 8, "RESTART", 0x20344E, start_restart_item.w - 38);
    draw_desktop_button(start_shutdown_item, 0xD4DCE8, 0xE3E9F1, "", 0x20344E);
    font_icon(start_shutdown_item.x + 10, start_shutdown_item.y + 6, ICON_DISK, 0x20344E);
    font_text_fit(start_shutdown_item.x + 30, start_shutdown_item.y + 8, "SHUTDOWN", 0x20344E, start_shutdown_item.w - 38);
}

static void draw_start_submenus(void) {
    rect_t sub;
    if (!start_menu_open) return;
    if (menu_hover(start_apps_item)) {
        sub = (rect_t){start_menu_rect.x + start_menu_rect.w + 8, start_menu_rect.y + 68, 150, 102, 0xD7DEE8};
        fb_rect(sub.x + 6, sub.y + 6, sub.w, sub.h, desktop_shadow);
        fill(sub);
        outline(sub.x, sub.y, sub.w, sub.h, 0x5D738E);
        draw_label(sub.x + 14, sub.y + 12, "LAUNCH", 0x3D4F63);
        draw_desktop_button((rect_t){sub.x + 8, sub.y + 28, sub.w - 16, 18, 0}, 0xC6D3E0, 0xECF3F8, "TERMINAL", 0x20344E);
        draw_desktop_button((rect_t){sub.x + 8, sub.y + 48, sub.w - 16, 18, 0}, 0xC7B2A2, 0xEBDCCF, "TEXT EDIT", 0x3D291A);
        draw_desktop_button((rect_t){sub.x + 8, sub.y + 68, sub.w - 16, 18, 0}, 0xC6D3E0, 0xECF3F8, "EXPLORER", 0x20344E);
    }
    if (menu_hover(start_settings_item)) {
        sub = (rect_t){start_menu_rect.x + start_menu_rect.w + 8, start_menu_rect.y + 192, 158, 82, 0xD7DEE8};
        fb_rect(sub.x + 6, sub.y + 6, sub.w, sub.h, desktop_shadow);
        fill(sub);
        outline(sub.x, sub.y, sub.w, sub.h, 0x5D738E);
        draw_label(sub.x + 14, sub.y + 12, "SYSTEM", 0x3D4F63);
        draw_desktop_button((rect_t){sub.x + 8, sub.y + 28, sub.w - 16, 18, 0}, 0xC6D3E0, 0xECF3F8, "SETTINGS", 0x20344E);
        draw_desktop_button((rect_t){sub.x + 8, sub.y + 48, sub.w - 16, 18, 0}, 0xC6D3E0, 0xECF3F8, "RESTART / POWER", 0x20344E);
    }
}

void desktop_init(void) {
    desktop_item_count = 0;
    desktop_selected = -1;
    desktop_context_index = -1;
    start_menu_open = 0;
    desktop_context_open = 0;
    desktop_apply_theme();
    layout_desktop_context_menu(18, 42);
    serial_write("[desktop] desktop_init called\n");
}

void desktop_draw(void) {
    desktop_apply_theme();
    draw_desktop_items();
}

void desktop_render_shell(void) {
    desktop_apply_theme();
    fb_rect(0, 0, (int)fb.width, (int)fb.height, desktop_bg);
    fb_rect(desktop_bar.x, desktop_bar.y, (int)fb.width, desktop_bar.h, desktop_panel);
    if (desktop_start_button_visible) {
        start_button = (rect_t){10, 4, 74, 20, 0xE4EAF2};
        draw_desktop_button(start_button, 0xDDE6F1, 0xEEF3F8, "Start", 0x17314A);
        draw_brand_icon(start_button.x + 8, start_button.y + 2);
    } else {
        start_button = (rect_t){0, 0, 0, 0, 0};
    }
    draw_desktop_items();
    draw_start_menu();
    draw_start_submenus();
    draw_desktop_context_menu();
}

int desktop_get_selected(void) {
    return desktop_selected;
}

void desktop_set_selected(int index) {
    desktop_selected = index;
}

int desktop_get_item_count(void) {
    return desktop_item_count;
}

int desktop_get_bar_height(void) {
    return desktop_bar.h;
}

u32 desktop_get_background_color(void) {
    return desktop_bg;
}

u32 desktop_get_panel_color(void) {
    return desktop_panel;
}

int desktop_is_start_menu_open(void) {
    return start_menu_open;
}

void desktop_close_menus(void) {
    start_menu_open = 0;
    desktop_context_open = 0;
    desktop_context_target = 0;
}

int desktop_point_in_context_menu(int mx, int my) {
    if (!desktop_context_open) return 0;
    return mx >= desktop_context_menu.x && mx < desktop_context_menu.x + desktop_context_menu.w && my >= desktop_context_menu.y && my < desktop_context_menu.y + desktop_context_menu.h;
}

void desktop_handle_right_click(int mx, int my, int *selected_index, const rect_t *dock_rect) {
    int i;
    if (my <= desktop_bar.y + desktop_bar.h || (dock_rect && my >= dock_rect->y)) return;
    desktop_context_index = -1;
    desktop_context_target = 0;
    extern int desktop_dragging;
    extern int desktop_drag_index;
    extern int desktop_drag_moved;
    
    for (i = 0; i < desktop_item_count; i++) {
        if (desktop_items[i].used && mx >= desktop_items[i].icon_rect.x && mx < desktop_items[i].icon_rect.x + desktop_items[i].icon_rect.w && my >= desktop_items[i].icon_rect.y && my < desktop_items[i].icon_rect.y + desktop_items[i].icon_rect.h) {
            desktop_context_index = i;
            desktop_context_target = 1;
            desktop_selected = i;
            if (selected_index) *selected_index = i;
            break;
        }
    }
    layout_desktop_context_menu(mx, my);
    desktop_context_open = 1;
}



void desktop_layout_context_menu(int mx, int my) {
    int y = my;
    if (y < 36) y = 36;
    desktop_context_menu = (rect_t){mx, y, 172, desktop_context_target ? 118 : 218, 0xD7DEE8};
    desktop_ctx_copy = (rect_t){mx + 10, y + 10, 152, 20, 0xB4C6D6};
    desktop_ctx_paste = (rect_t){mx + 10, y + 36, 152, 20, 0xB4C6D6};
    if (desktop_context_target) {
        desktop_ctx_rename = (rect_t){mx + 10, y + 62, 152, 20, 0xB4C6D6};
        desktop_ctx_delete = (rect_t){mx + 10, y + 88, 152, 20, 0xC99393};
        return;
    }
    desktop_ctx_refresh = (rect_t){mx + 10, y + 62, 152, 20, 0xBBC8D8};
    desktop_ctx_change_wall = (rect_t){mx + 10, y + 88, 152, 20, 0x98B8D8};
    desktop_ctx_settings = (rect_t){mx + 10, y + 114, 152, 20, 0xB4C6D6};
    desktop_ctx_textedit = (rect_t){mx + 10, y + 140, 152, 20, 0xB79F8C};
    desktop_ctx_new_folder = (rect_t){mx + 10, y + 166, 152, 20, 0x9DBB8D};
    desktop_ctx_new_file = (rect_t){mx + 10, y + 192, 152, 20, 0x90A9C8};
}

int desktop_handle_context_click(int mx, int my) {
    int action = 0;
    if (!desktop_context_open) return 0;
    if (desktop_context_target && mx >= desktop_ctx_copy.x && mx < desktop_ctx_copy.x + desktop_ctx_copy.w && my >= desktop_ctx_copy.y && my < desktop_ctx_copy.y + desktop_ctx_copy.h) { action = 2001; }
    else if (mx >= desktop_ctx_paste.x && mx < desktop_ctx_paste.x + desktop_ctx_paste.w && my >= desktop_ctx_paste.y && my < desktop_ctx_paste.y + desktop_ctx_paste.h) { action = 2002; }
    else if (desktop_context_target && mx >= desktop_ctx_rename.x && mx < desktop_ctx_rename.x + desktop_ctx_rename.w && my >= desktop_ctx_rename.y && my < desktop_ctx_rename.y + desktop_ctx_rename.h) { action = 2003; }
    else if (desktop_context_target && mx >= desktop_ctx_delete.x && mx < desktop_ctx_delete.x + desktop_ctx_delete.w && my >= desktop_ctx_delete.y && my < desktop_ctx_delete.y + desktop_ctx_delete.h) { action = 2004; }
    else if (mx >= desktop_ctx_refresh.x && mx < desktop_ctx_refresh.x + desktop_ctx_refresh.w && my >= desktop_ctx_refresh.y && my < desktop_ctx_refresh.y + desktop_ctx_refresh.h) { action = 2005; }
    else if (mx >= desktop_ctx_change_wall.x && mx < desktop_ctx_change_wall.x + desktop_ctx_change_wall.w && my >= desktop_ctx_change_wall.y && my < desktop_ctx_change_wall.y + desktop_ctx_change_wall.h) {
        extern void settings_set_tab(int);
        extern void open_app(int);
        extern void focus_app_window(int);
        settings_set_tab(0);
        open_app(APP_SETTINGS);
        focus_app_window(APP_SETTINGS - 1);
        action = 2010;
    }
    else if (mx >= desktop_ctx_settings.x && mx < desktop_ctx_settings.x + desktop_ctx_settings.w && my >= desktop_ctx_settings.y && my < desktop_ctx_settings.y + desktop_ctx_settings.h) { action = 2006; }
    else if (mx >= desktop_ctx_textedit.x && mx < desktop_ctx_textedit.x + desktop_ctx_textedit.w && my >= desktop_ctx_textedit.y && my < desktop_ctx_textedit.y + desktop_ctx_textedit.h) { action = 2007; }
    else if (mx >= desktop_ctx_new_folder.x && mx < desktop_ctx_new_folder.x + desktop_ctx_new_folder.w && my >= desktop_ctx_new_folder.y && my < desktop_ctx_new_folder.y + desktop_ctx_new_folder.h) { action = 2008; }
    else if (mx >= desktop_ctx_new_file.x && mx < desktop_ctx_new_file.x + desktop_ctx_new_file.w && my >= desktop_ctx_new_file.y && my < desktop_ctx_new_file.y + desktop_ctx_new_file.h) { action = 2009; }
    if (action) { desktop_context_open = 0; desktop_context_target = 0; desktop_context_index = -1; }
    return action;
}

int desktop_handle_start_menu_click(int mx, int my) {
    if (!start_menu_open) return 0;
    if (mx >= start_apps_item.x && mx < start_apps_item.x + start_apps_item.w && my >= start_apps_item.y && my < start_apps_item.y + start_apps_item.h) { start_menu_open = 0; return APP_TERMINAL; }
    if (mx >= start_explorer_item.x && mx < start_explorer_item.x + start_explorer_item.w && my >= start_explorer_item.y && my < start_explorer_item.y + start_explorer_item.h) { start_menu_open = 0; return APP_EXPLORER; }
    if (mx >= start_settings_item.x && mx < start_settings_item.x + start_settings_item.w && my >= start_settings_item.y && my < start_settings_item.y + start_settings_item.h) { start_menu_open = 0; return APP_SETTINGS; }
    if (mx >= start_textedit_item.x && mx < start_textedit_item.x + start_textedit_item.w && my >= start_textedit_item.y && my < start_textedit_item.y + start_textedit_item.h) { start_menu_open = 0; return 1001; }
    if (mx >= start_shutdown_item.x && mx < start_shutdown_item.x + start_shutdown_item.w && my >= start_shutdown_item.y && my < start_shutdown_item.y + start_shutdown_item.h) { start_menu_open = 0; return 1002; }
    if (mx >= start_restart_item.x && mx < start_restart_item.x + start_restart_item.w && my >= start_restart_item.y && my < start_restart_item.y + start_restart_item.h) { start_menu_open = 0; return 1003; }
    return 0;
}

int desktop_handle_left_click(int mx, int my) {
    if (desktop_context_open && !desktop_point_in_context_menu(mx, my)) {
        desktop_context_open = 0;
        return 1;
    }
    if (desktop_start_button_visible && start_button.w > 0 &&
        mx >= start_button.x && mx < start_button.x + start_button.w &&
        my >= start_button.y && my < start_button.y + start_button.h) {
        start_menu_open = !start_menu_open;
        desktop_context_open = 0;
        return 1;
    }
    if (start_menu_open) return desktop_handle_start_menu_click(mx, my);
    return 0;
}

void desktop_set_item_pos(int index, int cx, int cy) {
    if (index >= 0 && index < desktop_item_count) {
        desktop_items[index].has_custom_pos = 1;
        desktop_items[index].custom_x = cx;
        desktop_items[index].custom_y = cy;
    }
}

unsigned char desktop_multi_selected[32] = {0};

void desktop_update_marquee_selection(int rx, int ry, int rw, int rh) {
    int i;
    for (i = 0; i < desktop_item_count; i++) {
        rect_t r = desktop_items[i].icon_rect;
        if (rx < r.x + r.w && rx + rw > r.x && ry < r.y + r.h && ry + rh > r.y) {
            desktop_multi_selected[i] = 1;
        } else {
            desktop_multi_selected[i] = 0;
        }
    }
}

void desktop_clear_multi_selected(void) {
    int i;
    for (i = 0; i < 32; i++) desktop_multi_selected[i] = 0;
}
