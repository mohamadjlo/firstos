#include "ui_builder.h"
#include "font.h"
#include "framebuffer.h"
#include "firstlang.h"
#include "fs.h"

#include "ide.h"
static void fl_itos(int v, char *b, int sz) {
    if (v == 0) { if(sz>1){b[0]='0'; b[1]=0;} return; }
    int i=0, j=0, n=v; char tmp[16];
    if (n<0) { if(sz>1){b[0]='-'; i++; j++; n=-n;} }
    while (n>0 && i<15) { tmp[i++] = (n % 10) + '0'; n /= 10; }
    while (i>j && j<sz-1) { b[j++] = tmp[--i]; }
    b[j] = 0;
}

static void fl_cpy(char *d, const char *s, int max) {
    int i=0; while(s[i] && i<max-1){d[i]=s[i]; i++;} d[i]=0;
}
static void fl_cat(char *d, const char *s, int max) {
    int len=0; while(d[len]) len++;
    int i=0; while(s[i] && len+i<max-1){d[len+i]=s[i]; i++;} d[len+i]=0;
}

int ide_designer_mode = 0;

typedef struct {
    int type; // 1: Button, 2: Label, 3: TextBox, 4: CheckBox, 5: Slider
    rect_t rect;
    char name[32];
    char text[64];
    char code[128];
} ui_widget_t;

static ui_widget_t widgets[32];
static int widget_count = 0;
static int selected_widget = -1;

static int dragging_tool = 0; // 0: none, 1-5: tool type
static int dragging_widget = -1; // index if moving existing
static int drag_off_x, drag_off_y;

static rect_t canvas_rect;
static rect_t toolbox_rect;
static rect_t props_rect;
static rect_t save_btn_rect;

static char prop_name_input[32] = "";
static char prop_text_input[64] = "";
static char prop_code_input[128] = "";
static int prop_focus = 0; // 1: name, 2: text, 3: code
static int prop_cursor = 0;

extern void gui_text_input_set(char *buffer, int capacity, int *cursor_ptr, int *focus_ptr);

extern int ide_saving_ui;
extern int ide_save_dialog_open;
extern int explorer_picker_mode;
extern char editor_save_name[32];
extern void open_app(int kind);

void ui_builder_generate_code(char *buf, int max_len) {
    buf[0] = 0;
    for (int i = 0; i < widget_count; i++) {
        ui_widget_t *w = &widgets[i];
        if (w->type == 1) fl_cat(buf, "create a button \"", max_len);
        else if (w->type == 2) fl_cat(buf, "create a label \"", max_len);
        else if (w->type == 3) fl_cat(buf, "create a textbox \"", max_len);
        else if (w->type == 4) fl_cat(buf, "create a checkbox \"", max_len);
        else if (w->type == 5) fl_cat(buf, "create a slider \"", max_len);
        fl_cat(buf, w->name, max_len);
        fl_cat(buf, "\"\n", max_len);
        if (w->text[0]) {
            fl_cat(buf, "set ", max_len); fl_cat(buf, w->name, max_len); fl_cat(buf, " to \"", max_len);
            fl_cat(buf, w->text, max_len); fl_cat(buf, "\"\n", max_len);
        }
        if (w->code[0]) {
            fl_cat(buf, "on click ", max_len); fl_cat(buf, w->name, max_len); fl_cat(buf, ":\n", max_len);
            fl_cat(buf, "    ", max_len); fl_cat(buf, w->code, max_len); fl_cat(buf, "\nend\n", max_len);
        }
        fl_cat(buf, "\n", max_len);
    }
}

static void save_ui_to_file(void) {
    ide_saving_ui = 1;
    ide_save_dialog_open = 1;
    explorer_picker_mode = 1;
    fl_cpy(editor_save_name, "userinterface.fl", 32);
    
    // Open explorer
    open_app(4); // APP_EXPLORER
    // Focus is handled by open_app and picker logic
}

static void draw_fake_button(int x, int y, int w, int h, const char *txt) {
    int rad = 6;
    fb_round_rect(x, y, w, h, rad, 0xE0E0E0);
    fb_round_rect_border(x, y, w, h, rad, 0x888888);
    draw_label(x+8, y+8, txt, 0);
}
static void draw_fake_label(int x, int y, const char *txt) {
    draw_label(x, y, txt, 0);
}
static void draw_fake_textbox(int x, int y, int w, int h, const char *txt) {
    int rad = 5;
    fb_round_rect(x, y, w, h, rad, 0xFFFFFF);
    fb_round_rect_border(x, y, w, h, rad, 0x888888);
    draw_label(x+4, y+6, txt, 0x999999);
}

void draw_ui_builder(rect_t bounds) {
    int cx = bounds.x, cy = bounds.y, cw = bounds.w, ch = bounds.h;
    
    props_rect = (rect_t){cx, cy, 200, ch, 0};
    toolbox_rect = (rect_t){cx + cw - 150, cy, 150, ch, 0};
    canvas_rect = (rect_t){cx + 200, cy, cw - 350, ch, 0};
    
    // Backgrounds
    fb_rect(props_rect.x, props_rect.y, props_rect.w, props_rect.h, 0xF5F5F5);
    fb_rect(toolbox_rect.x, toolbox_rect.y, toolbox_rect.w, toolbox_rect.h, 0xF5F5F5);
    fb_rect(canvas_rect.x, canvas_rect.y, canvas_rect.w, canvas_rect.h, 0xFFFFFF);
    outline(canvas_rect.x, canvas_rect.y, canvas_rect.w, canvas_rect.h, 0xCCCCCC);
    
    // Toolbox
    draw_label(toolbox_rect.x + 10, toolbox_rect.y + 10, "TOOLS (Drag)", 0x333333);
    draw_fake_button(toolbox_rect.x + 10, toolbox_rect.y + 40, 100, 30, "Button");
    draw_fake_label(toolbox_rect.x + 10, toolbox_rect.y + 80, "Label Text");
    draw_fake_textbox(toolbox_rect.x + 10, toolbox_rect.y + 110, 100, 24, "TextBox");
    
    // Canvas
    for (int i = 0; i < widget_count; i++) {
        ui_widget_t *w = &widgets[i];
        int wx = canvas_rect.x + w->rect.x;
        int wy = canvas_rect.y + w->rect.y;
        
        if (w->type == 1) draw_fake_button(wx, wy, w->rect.w, w->rect.h, w->text[0] ? w->text : w->name);
        if (w->type == 2) draw_fake_label(wx, wy, w->text[0] ? w->text : w->name);
        if (w->type == 3) draw_fake_textbox(wx, wy, w->rect.w, w->rect.h, w->text[0] ? w->text : w->name);
        
        if (selected_widget == i) {
            outline(wx-2, wy-2, w->rect.w+4, w->rect.h+4, 0x0000FF);
        }
    }
    
    // Properties
    draw_label(props_rect.x + 10, props_rect.y + 10, "PROPERTIES", 0x333333);
    if (selected_widget >= 0) {
        draw_label(props_rect.x + 10, props_rect.y + 40, "Name:", 0);
        fb_round_rect(props_rect.x + 10, props_rect.y + 60, 180, 24, 5, 0xFFFFFF);
        fb_round_rect_border(props_rect.x+10, props_rect.y+60, 180, 24, 5, prop_focus==1?0x0000FF:0xCCC);
        draw_label(props_rect.x + 14, props_rect.y + 66, prop_name_input, 0);
        
        draw_label(props_rect.x + 10, props_rect.y + 90, "Text:", 0);
        fb_round_rect(props_rect.x + 10, props_rect.y + 110, 180, 24, 5, 0xFFFFFF);
        fb_round_rect_border(props_rect.x+10, props_rect.y+110, 180, 24, 5, prop_focus==2?0x0000FF:0xCCC);
        draw_label(props_rect.x + 14, props_rect.y + 116, prop_text_input, 0);
        
        draw_label(props_rect.x + 10, props_rect.y + 140, "Code:", 0);
        fb_round_rect(props_rect.x + 10, props_rect.y + 160, 180, 60, 5, 0xFFFFFF);
        fb_round_rect_border(props_rect.x+10, props_rect.y+160, 180, 60, 5, prop_focus==3?0x0000FF:0xCCC);
        draw_label(props_rect.x + 14, props_rect.y + 166, prop_code_input, 0);
    }
    
    save_btn_rect = (rect_t){props_rect.x + 10, props_rect.y + props_rect.h - 40, 180, 30, 0};
    fb_round_rect(save_btn_rect.x, save_btn_rect.y, save_btn_rect.w, save_btn_rect.h, 6, 0x4CAF50);
    fb_round_rect_border(save_btn_rect.x, save_btn_rect.y, save_btn_rect.w, save_btn_rect.h, 6, 0x388E3C);
    draw_label(save_btn_rect.x + 60, save_btn_rect.y + 8, "SAVE UI", 0xFFFFFF);
    
    // Dragging preview
    if (dragging_tool > 0) {
        extern int ps2_mouse_x(void); extern int ps2_mouse_y(void);
        int mx = ps2_mouse_x(), my = ps2_mouse_y();
        if (dragging_tool == 1) draw_fake_button(mx - 50, my - 15, 100, 30, "Button");
        if (dragging_tool == 2) draw_fake_label(mx - 20, my - 10, "Label");
        if (dragging_tool == 3) draw_fake_textbox(mx - 50, my - 12, 100, 24, "TextBox");
    }
}

int handle_ui_builder_click(int mx, int my) {
    if (point_in_rect(mx, my, save_btn_rect)) {
        save_ui_to_file();
        return 1;
    }
    
    // Tools
    if (point_in_rect(mx, my, (rect_t){toolbox_rect.x+10, toolbox_rect.y+40, 100, 30, 0})) { dragging_tool = 1; return 1; }
    if (point_in_rect(mx, my, (rect_t){toolbox_rect.x+10, toolbox_rect.y+80, 100, 20, 0})) { dragging_tool = 2; return 1; }
    if (point_in_rect(mx, my, (rect_t){toolbox_rect.x+10, toolbox_rect.y+110, 100, 24, 0})) { dragging_tool = 3; return 1; }
    
    // Props focus
    if (selected_widget >= 0) {
        if (point_in_rect(mx, my, (rect_t){props_rect.x+10, props_rect.y+60, 180, 24, 0})) { prop_focus = 1; gui_text_input_set(prop_name_input, 32, &prop_cursor, &prop_focus); return 1; }
        if (point_in_rect(mx, my, (rect_t){props_rect.x+10, props_rect.y+110, 180, 24, 0})) { prop_focus = 2; gui_text_input_set(prop_text_input, 64, &prop_cursor, &prop_focus); return 1; }
        if (point_in_rect(mx, my, (rect_t){props_rect.x+10, props_rect.y+160, 180, 60, 0})) { prop_focus = 3; gui_text_input_set(prop_code_input, 128, &prop_cursor, &prop_focus); return 1; }
    }
    
    // Select widget on canvas
    if (point_in_rect(mx, my, canvas_rect)) {
        int cx = mx - canvas_rect.x;
        int cy = my - canvas_rect.y;
        for (int i = widget_count - 1; i >= 0; i--) {
            ui_widget_t *w = &widgets[i];
            if (cx >= w->rect.x && cx <= w->rect.x + w->rect.w && cy >= w->rect.y && cy <= w->rect.y + w->rect.h) {
                selected_widget = i;
                fl_cpy(prop_name_input, w->name, 32);
                fl_cpy(prop_text_input, w->text, 64);
                fl_cpy(prop_code_input, w->code, 128);
                prop_focus = 0;
                dragging_widget = i;
                drag_off_x = cx - w->rect.x;
                drag_off_y = cy - w->rect.y;
                return 1;
            }
        }
        selected_widget = -1;
        prop_focus = 0;
    }
    
    return 1;
}

int handle_ui_builder_mouse_up(int mx, int my) {
    if (dragging_tool > 0) {
        if (point_in_rect(mx, my, canvas_rect) && widget_count < 32) {
            ui_widget_t *w = &widgets[widget_count];
            w->type = dragging_tool;
            w->rect.x = mx - canvas_rect.x - 50;
            w->rect.y = my - canvas_rect.y - 15;
            w->rect.w = (dragging_tool == 2) ? 60 : 100;
            w->rect.h = (dragging_tool == 2) ? 20 : (dragging_tool == 3 ? 24 : 30);
            
            char num[8];             fl_itos(widget_count, num, 8);
            fl_cpy(w->name, "widget", 32); fl_cat(w->name, num, 32);
            w->text[0] = 0; w->code[0] = 0;
            
            selected_widget = widget_count;
            fl_cpy(prop_name_input, w->name, 32);
            fl_cpy(prop_text_input, w->text, 64);
            fl_cpy(prop_code_input, w->code, 128);
            prop_focus = 0;
            
            widget_count++;
        }
        dragging_tool = 0;
    }
    if (dragging_widget >= 0) {
        dragging_widget = -1;
    }
    return 1;
}

int handle_ui_builder_mouse_move(int mx, int my) {
    if (dragging_widget >= 0) {
        ui_widget_t *w = &widgets[dragging_widget];
        w->rect.x = mx - canvas_rect.x - drag_off_x;
        w->rect.y = my - canvas_rect.y - drag_off_y;
    }
    return 1;
}

void handle_ui_builder_keypress(char c) {
    (void)c;
    if (selected_widget >= 0 && prop_focus > 0) {
        ui_widget_t *w = &widgets[selected_widget];
        if (prop_focus == 1) fl_cpy(w->name, prop_name_input, 32);
        if (prop_focus == 2) fl_cpy(w->text, prop_text_input, 64);
        if (prop_focus == 3) fl_cpy(w->code, prop_code_input, 128);
    }
}
