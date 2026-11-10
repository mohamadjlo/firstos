#include "gui_calc.h"
#include "gui_widgets.h"
#include "framebuffer.h"
#include "font.h"

extern int needs_redraw;

static char calc_display[32] = "0";
static int calc_val = 0;
static int calc_prev = 0;
static int calc_op = 0; /* 0=none, 1=add, 2=sub, 3=mul, 4=div */
static int calc_new_num = 1;

static void calc_format_display(void) {
    int v = calc_val;
    int i = 0;
    int is_neg = 0;
    char rev[32];
    if (v == 0) {
        calc_display[0] = '0';
        calc_display[1] = 0;
        return;
    }
    if (v < 0) { is_neg = 1; v = -v; }
    while (v > 0 && i < 30) {
        rev[i++] = '0' + (v % 10);
        v /= 10;
    }
    if (is_neg && i < 31) rev[i++] = '-';
    for (int j = 0; j < i; j++) calc_display[j] = rev[i - 1 - j];
    calc_display[i] = 0;
}

static rect_t calc_btn_rect(app_window_t *app, int row, int col) {
    int bw = 40, bh = 40;
    int gap = 8;
    int cx = app->frame.x + 10;
    int cy = app->frame.y + 42 + 60; /* below display */
    return (rect_t){cx + col * (bw + gap), cy + row * (bh + gap), bw, bh, 0};
}

void draw_calculator_contents(app_window_t *app) {
    if (!app) return;
    int cx = app->frame.x + 10;
    int cy = app->frame.y + 42;
    int cw = app->frame.w - 20;
    int ch = app->frame.h - 52;
    int r, c;
    const char *btns[4][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"C", "0", "=", "+"}
    };

    fb_rect(cx, cy, cw, ch, 0xF0F4F8);
    outline(cx, cy, cw, ch, 0xC4D0DE);

    /* Display */
    fb_rect(cx + 8, cy + 8, cw - 16, 44, 0xFFFFFF);
    outline(cx + 8, cy + 8, cw - 16, 44, 0xA8B2BC);

    /* Text right-aligned */
    int len = 0;
    while (calc_display[len]) len++;
    font_text(cx + cw - 16 - len * 8, cy + 22, calc_display, 0x2A3C4D, 1);

    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            rect_t btn = calc_btn_rect(app, r, c);
            int is_op = (c == 3 || (r == 3 && (c == 0 || c == 2)));
            draw_button(btn, is_op ? 0xDEE6ED : 0xFFFFFF, is_op ? 0xEFF3F6 : 0xFFFFFF, btns[r][c], is_op ? 0x2C4662 : 0x2A3C4D);
        }
    }
}

int calc_handle_click(app_window_t *app, int mx, int my) {
    if (!app) return 0;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            rect_t btn = calc_btn_rect(app, r, c);
            if (point_in_rect(mx, my, btn)) {
                if (r == 3 && c == 0) { /* C */
                    calc_val = 0; calc_prev = 0; calc_op = 0; calc_new_num = 1;
                } else if (r == 3 && c == 2) { /* = */
                    if (calc_op == 1) calc_val = calc_prev + calc_val;
                    else if (calc_op == 2) calc_val = calc_prev - calc_val;
                    else if (calc_op == 3) calc_val = calc_prev * calc_val;
                    else if (calc_op == 4 && calc_val != 0) calc_val = calc_prev / calc_val;
                    calc_op = 0; calc_new_num = 1;
                } else if (c == 3) { /* ops */
                    if (r == 0) calc_op = 4;      /* / */
                    else if (r == 1) calc_op = 3; /* * */
                    else if (r == 2) calc_op = 2; /* - */
                    else if (r == 3) calc_op = 1; /* + */
                    calc_prev = calc_val;
                    calc_new_num = 1;
                } else { /* digits */
                    int d = -1;
                    if (r == 0 && c < 3) d = 7 + c;
                    else if (r == 1 && c < 3) d = 4 + c;
                    else if (r == 2 && c < 3) d = 1 + c;
                    else if (r == 3 && c == 1) d = 0;
                    if (d >= 0) {
                        if (calc_new_num) { calc_val = d; calc_new_num = 0; }
                        else { calc_val = calc_val * 10 + d; }
                    }
                }
                calc_format_display();
                needs_redraw = 1;
                return 1;
            }
        }
    }
    return 0;
}
