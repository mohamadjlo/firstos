#include "taskman.h"
#include "task.h"
#include "serial.h"
#include "framebuffer.h"
#include "font.h"

extern task_t tasks[MAX_TASKS];
extern int current_task;
extern app_window_t app_windows[32];

extern void fb_rect(int x, int y, int w, int h, unsigned int color);
extern void outline(int x, int y, int w, int h, unsigned int color);
extern void draw_label(int x, int y, const char *text, unsigned int color);

int taskman_active_tab = 0;
static int taskman_selected = -1;

#define HISTORY_LEN 50
static u8 cpu_history[HISTORY_LEN];
static u8 mem_history[HISTORY_LEN];
static u8 net_history[HISTORY_LEN];
static int history_idx = 0;

static unsigned int last_ticks[MAX_TASKS];
static u8 cpu_percent[MAX_TASKS];

static int sort_col  = 0;   /* 0=PID,1=NAME,2=STATUS,3=CPU,4=MEM */
static int sort_desc = 0;
static int perf_selected_metric = 0; /* 0=CPU,1=MEM,2=NET */

/* ---- task row type ---- */
typedef struct {
    int  id;
    char name[32];
    int  status_val;   /* 0=Running,1=Ready,2=Stopped,3=GUI */
    int  cpu_val;
    int  is_gui;
} task_row_t;

/* Static buffers – never on the kernel stack */
static task_row_t s_rows[64];
static int        s_row_count = 0;

/* ------------------------------------------------------------------ */
static void tmem_copy(void *dst, const void *src, int len) {
    char *d = (char*)dst; const char *s = (const char*)src;
    while (len--) *d++ = *s++;
}

static int tm_str_cmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static void tm_uint_str(unsigned int n, char *buf, int max) {
    char tmp[16]; int i=0, j=0;
    if (n == 0) { buf[0]='0'; buf[1]=0; return; }
    while (n > 0 && i < 15) { tmp[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0 && j < max-1) buf[j++] = tmp[--i];
    buf[j] = 0;
}

static void tm_str_cat(char *dst, const char *src, int max) {
    int i = 0;
    while (dst[i]) i++;
    int j = 0;
    while (src && src[j] && i < max-1) dst[i++] = src[j++];
    dst[i] = 0;
}

/* ------------------------------------------------------------------ */
void taskman_record_history(void) {
    extern int idle_ticks_last_sec;
    extern u32 heap_used, heap_total;

    int cpu = 100 - idle_ticks_last_sec;
    if (cpu < 0) cpu = 0;
    if (cpu > 100) cpu = 100;

    int mem = 0;
    if (heap_total > 0) mem = (int)((heap_used * 100u) / heap_total);
    if (mem > 100) mem = 100;

    static int net_noise = 0;
    net_noise = (net_noise + 3) % 15;

    int i = history_idx % HISTORY_LEN;
    cpu_history[i] = (u8)cpu;
    mem_history[i] = (u8)mem;
    net_history[i] = (u8)net_noise;
    history_idx++;

    for (int k = 0; k < MAX_TASKS; k++) {
        if (tasks[k].active || tasks[k].tick_count > 0) {
            unsigned int diff = tasks[k].tick_count - last_ticks[k];
            cpu_percent[k] = (u8)((diff > 100) ? 100 : diff);
            last_ticks[k]  = tasks[k].tick_count;
        } else {
            cpu_percent[k] = 0;
            last_ticks[k]  = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
static const char *app_kind_to_name(int kind) {
    if (kind == APP_TERMINAL)    return "Terminal";
    if (kind == APP_FILES)       return "Apps";
    if (kind == APP_EXPLORER)    return "Explorer";
    if (kind == APP_SETTINGS)    return "Settings";
    if (kind == APP_TEXT_EDIT)   return "Text Edit";
    if (kind == APP_MEDIA_PLAYER)return "Media Player";
    if (kind == APP_CALCULATOR)  return "Calculator";
    if (kind == APP_PHOTO_EDITOR)return "Photo Viewer";
    if (kind == APP_IDE)         return "IDE";
    if (kind == APP_TASKMAN)     return "Task Manager";
    return "Window";
}

/* Populate s_rows / s_row_count from live task & window state */
static void fill_rows(void) {
    int cnt = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].active && tasks[i].tick_count == 0 && tasks[i].name[0] == 0) continue;
        if (cnt >= 63) break;
        s_rows[cnt].id = i;
        const char *nm = tasks[i].name[0] ? tasks[i].name : "(unnamed)";
        tmem_copy(s_rows[cnt].name, nm, 31); s_rows[cnt].name[31] = 0;
        if      (!tasks[i].active)   s_rows[cnt].status_val = 2;
        else if (i == current_task)  s_rows[cnt].status_val = 0;
        else                         s_rows[cnt].status_val = 1;
        s_rows[cnt].cpu_val = cpu_percent[i];
        s_rows[cnt].is_gui  = 0;
        cnt++;
    }
    for (int i = 0; i < 32; i++) {
        if (!app_windows[i].open) continue;
        if (cnt >= 63) break;
        s_rows[cnt].id = 100 + i;
        const char *nm = app_kind_to_name(app_windows[i].kind);
        tmem_copy(s_rows[cnt].name, nm, 31); s_rows[cnt].name[31] = 0;
        s_rows[cnt].status_val = 3;
        s_rows[cnt].cpu_val    = 0;
        s_rows[cnt].is_gui     = 1;
        cnt++;
    }
    s_row_count = cnt;
}

static void sort_rows(void) {
    for (int p = 0; p < s_row_count - 1; p++) {
        for (int q = 0; q < s_row_count - p - 1; q++) {
            int sw = 0;
            task_row_t *a = &s_rows[q];
            task_row_t *b = &s_rows[q+1];
            if      (sort_col == 0) sw = sort_desc ? (a->id < b->id) : (a->id > b->id);
            else if (sort_col == 1) { int c = tm_str_cmp(a->name, b->name); sw = sort_desc ? (c<0) : (c>0); }
            else if (sort_col == 2) sw = sort_desc ? (a->status_val < b->status_val) : (a->status_val > b->status_val);
            else if (sort_col == 3) sw = sort_desc ? (a->cpu_val < b->cpu_val) : (a->cpu_val > b->cpu_val);
            else if (sort_col == 4) sw = sort_desc ? (a->is_gui < b->is_gui) : (a->is_gui > b->is_gui);
            if (sw) { task_row_t tmp = *a; *a = *b; *b = tmp; }
        }
    }
}

/* ------------------------------------------------------------------ */
static void draw_chart(int x, int y, int w, int h, u8 *hist, u32 line_color, u32 fill_color) {
    fb_rect(x, y, w, h, 0xFFFFFF);
    /* grid */
    for (int i = 1; i < 4; i++) fb_rect(x, y + i*(h/4), w, 1, 0xEEEEEE);
    for (int i = 1; i < 10; i++) fb_rect(x + i*(w/10), y, 1, h, 0xEEEEEE);
    outline(x, y, w, h, 0xC4C8CC);

    int count = (history_idx < HISTORY_LEN) ? history_idx : HISTORY_LEN;
    int start = (history_idx > HISTORY_LEN) ? history_idx % HISTORY_LEN : 0;
    if (count < 2) return;

    int dx   = w / (HISTORY_LEN - 1);
    int px   = -1, py = -1;
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % HISTORY_LEN;
        int val = hist[idx];
        int cx  = x + i * dx;
        int cy  = y + h - (val * h / 100);
        if (cy >= y + h) cy = y + h - 1;
        if (px != -1) {
            int steps = cx - px; if (steps < 1) steps = 1;
            for (int j = 0; j <= steps; j++) {
                int lx = px + j;
                int ly = py + (cy - py) * j / steps;
                fb_rect(lx, ly,   2, 2,         line_color);
                fb_rect(lx, ly+2, 2, (y+h)-(ly+2), fill_color);
            }
        }
        px = cx; py = cy;
    }
}

/* ------------------------------------------------------------------ */
void draw_taskman_contents(app_window_t *app) {
    int x = app->frame.x + 12;
    int y = app->frame.y + 42;
    int w = app->frame.w - 24;
    int h = app->frame.h - 54;
    int i;

    fb_rect(x, y, w, h, 0xF5F6F8);

    /* ---- Tab bar ---- */
    fb_rect(x, y, w, 30, 0xE2E5E9);
    outline(x, y, w, 30, 0xC4C8CC);

    int t0 = (taskman_active_tab == 0);
    int t1 = (taskman_active_tab == 1);

    fb_rect(x+8,   y+4, 100, 26, t0 ? 0xF5F6F8 : 0xE2E5E9);
    outline(x+8,   y+4, 100, 26, 0xC4C8CC);
    if (t0) fb_rect(x+9, y+29, 98, 2, 0xF5F6F8);
    draw_label(x+24,  y+10, "Processes",  t0 ? 0x223344 : 0x778899);

    fb_rect(x+112, y+4, 100, 26, t1 ? 0xF5F6F8 : 0xE2E5E9);
    outline(x+112, y+4, 100, 26, 0xC4C8CC);
    if (t1) fb_rect(x+113, y+29, 98, 2, 0xF5F6F8);
    draw_label(x+120, y+10, "Performance", t1 ? 0x223344 : 0x778899);

    /* ---- Performance tab ---- */
    if (taskman_active_tab == 1) {
        int sb_w = 120;
        fb_rect(x, y+30, sb_w, h-30, 0xEBEBEB);
        fb_rect(x+sb_w, y+30, 1, h-30, 0xC4C8CC);

        static const char *labels[3] = {"CPU", "Memory", "Network"};
        for (i = 0; i < 3; i++) {
            int sy = y + 40 + i*40;
            if (perf_selected_metric == i) {
                fb_rect(x+4, sy, sb_w-8, 36, 0xCFE1F7);
                outline(x+4, sy, sb_w-8, 36, 0x99BBDD);
            }
            draw_label(x+16, sy+12, labels[i], perf_selected_metric==i ? 0x1A3A5C : 0x445566);
        }

        int cx = x + sb_w + 16;
        int cy = y + 46;
        int cw = w - sb_w - 32;
        int ch = h - 64;

        int cur_val;
        char title[40]; title[0] = 0;

        if (perf_selected_metric == 0) {
            cur_val = cpu_history[(history_idx - 1 + HISTORY_LEN) % HISTORY_LEN];
            tm_str_cat(title, "CPU: ", sizeof(title));
            char tmp[8]; tm_uint_str((unsigned int)cur_val, tmp, sizeof(tmp));
            tm_str_cat(title, tmp, sizeof(title));
            tm_str_cat(title, "%", sizeof(title));
            draw_chart(cx, cy, cw, ch, cpu_history, 0x1F77B4, 0xC6DBEF);
        } else if (perf_selected_metric == 1) {
            cur_val = mem_history[(history_idx - 1 + HISTORY_LEN) % HISTORY_LEN];
            tm_str_cat(title, "Memory: ", sizeof(title));
            char tmp[8]; tm_uint_str((unsigned int)cur_val, tmp, sizeof(tmp));
            tm_str_cat(title, tmp, sizeof(title));
            tm_str_cat(title, "%", sizeof(title));
            draw_chart(cx, cy, cw, ch, mem_history, 0x8C564B, 0xE5D8D3);
        } else {
            cur_val = 0; (void)cur_val;
            tm_str_cat(title, "Network Activity", sizeof(title));
            draw_chart(cx, cy, cw, ch, net_history, 0x2CA02C, 0xC7E9C0);
        }
        draw_label(cx + 10, cy + 10, title, 0x334455);
        return;
    }

    /* ---- Processes tab ---- */

    /* Build and sort rows into static buffer */
    fill_rows();
    sort_rows();

    /* Header */
    fb_rect(x, y+30, w, 24, 0xFAFBFC);
    outline(x, y+30, w, 24, 0xC4C8CC);
    static const char *hdrs[5] = {"PID", "NAME", "STATUS", "CPU %", "MEMORY"};
    static const int   hx[5]   = {8, 60, 220, 310, 380};
    for (i = 0; i < 5; i++) {
        draw_label(x + hx[i], y+36, hdrs[i], sort_col==i ? 0x223344 : 0x556677);
        if (sort_col == i) draw_label(x + hx[i] + 50, y+36, sort_desc ? "v" : "^", 0x8899AA);
    }

    /* Rows */
    int row_y = y + 54;
    int row_h = 24;

    for (i = 0; i < s_row_count; i++) {
        if (row_y + row_h > y + h - 44) break;

        int sel = (taskman_selected == s_rows[i].id);
        unsigned int bg = sel ? 0xCFE1F7 : (i%2==0 ? 0xFFFFFF : 0xF9FAFB);
        fb_rect(x, row_y, w, row_h, bg);
        if (sel) outline(x, row_y, w, row_h, 0x99BBDD);
        else fb_rect(x, row_y + row_h - 1, w, 1, 0xE8ECEF);

        char pid_s[8];
        tm_uint_str((unsigned int)s_rows[i].id, pid_s, sizeof(pid_s));
        draw_label(x + hx[0], row_y+6, pid_s, sel ? 0x1A3A5C : 0x556677);
        draw_label(x + hx[1], row_y+6, s_rows[i].name, sel ? 0x1A3A5C : 0x223344);

        const char *st; unsigned int sc;
        switch (s_rows[i].status_val) {
            case 0: st="Running"; sc=0x227733; break;
            case 1: st="Ready";   sc=0x2266AA; break;
            case 2: st="Stopped"; sc=0xAA4444; break;
            default:st="GUI App"; sc=0x8866AA; break;
        }
        draw_label(x + hx[2], row_y+6, st, sc);

        if (!s_rows[i].is_gui) {
            char cpu_s[8];
            tm_uint_str((unsigned int)s_rows[i].cpu_val, cpu_s, sizeof(cpu_s));
            tm_str_cat(cpu_s, "%", sizeof(cpu_s));
            draw_label(x + hx[3], row_y+6, cpu_s, sel ? 0x1A3A5C : 0x445566);
            draw_label(x + hx[4], row_y+6, "Shared", 0x8899AA);
        } else {
            draw_label(x + hx[3], row_y+6, "-", 0x8899AA);
            draw_label(x + hx[4], row_y+6, "-", 0x8899AA);
        }
        row_y += row_h;
    }

    /* Pad remaining */
    while (row_y + row_h <= y + h - 44) {
        fb_rect(x, row_y, w, row_h, i%2==0 ? 0xFFFFFF : 0xF9FAFB);
        fb_rect(x, row_y + row_h - 1, w, 1, 0xE8ECEF);
        row_y += row_h; i++;
    }

    /* Footer */
    int fp_y = y + h - 44;
    fb_rect(x, fp_y, w, 44, 0xFAFBFC);
    outline(x, fp_y, w, 1, 0xC4C8CC);

    if (taskman_selected >= 0 && taskman_selected < MAX_TASKS) {
        char info[128]; char tmp[16]; info[0] = 0;
        tm_str_cat(info, "Task ", sizeof(info));
        tm_uint_str((unsigned int)taskman_selected, tmp, sizeof(tmp));
        tm_str_cat(info, tmp, sizeof(info));
        tm_str_cat(info, ": ", sizeof(info));
        tm_str_cat(info, tasks[taskman_selected].name[0] ? tasks[taskman_selected].name : "(unnamed)", sizeof(info));
        tm_str_cat(info, "   Ticks: ", sizeof(info));
        tm_uint_str(tasks[taskman_selected].tick_count, tmp, sizeof(tmp));
        tm_str_cat(info, tmp, sizeof(info));
        draw_label(x+16, fp_y+14, info, 0x334455);
    } else if (taskman_selected >= 100 && taskman_selected < 132) {
        char info[64]; info[0] = 0;
        tm_str_cat(info, "GUI Window: ", sizeof(info));
        tm_str_cat(info, app_kind_to_name(app_windows[taskman_selected-100].kind), sizeof(info));
        draw_label(x+16, fp_y+14, info, 0x334455);
    } else {
        draw_label(x+16, fp_y+14, "Select a process to view details", 0x778899);
    }

    /* Kill button */
    int btn_x = x + w - 110;
    int btn_y = fp_y + 8;
    fb_rect(btn_x, btn_y, 94, 28, 0xE74C3C);
    outline(btn_x, btn_y, 94, 28, 0xC0392B);
    draw_label(btn_x+12, btn_y+8, "End Task", 0xFFFFFF);
}

/* ------------------------------------------------------------------ */
void handle_taskman_click(app_window_t *app, int mx, int my) {
    int x = app->frame.x + 12;
    int y = app->frame.y + 42;
    int w = app->frame.w - 24;
    int h = app->frame.h - 54;
    int i;

    /* Tab bar */
    if (my >= y+4 && my < y+30) {
        if (mx >= x+8   && mx < x+108) taskman_active_tab = 0;
        if (mx >= x+112 && mx < x+212) taskman_active_tab = 1;
        return;
    }

    /* Performance tab sidebar */
    if (taskman_active_tab == 1) {
        int sb_w = 120;
        if (mx >= x && mx < x+sb_w) {
            for (i = 0; i < 3; i++) {
                int sy = y + 40 + i*40;
                if (my >= sy && my < sy+36) { perf_selected_metric = i; return; }
            }
        }
        return;
    }

    /* End Task button */
    int fp_y = y + h - 44;
    if (mx >= x+w-110 && mx < x+w-16 && my >= fp_y+8 && my < fp_y+36) {
        if (taskman_selected > 0 && taskman_selected < MAX_TASKS) {
            if (tasks[taskman_selected].active) tasks[taskman_selected].active = 0;
        } else if (taskman_selected >= 100 && taskman_selected < 132) {
            app_windows[taskman_selected - 100].open = 0;
        }
        return;
    }

    /* Column header sort */
    static const int hx6[6] = {8, 60, 220, 310, 380, 9999};
    if (my >= y+30 && my < y+54) {
        for (i = 0; i < 5; i++) {
            if (mx >= x + hx6[i] && mx < x + hx6[i+1]) {
                if (sort_col == i) sort_desc = !sort_desc;
                else { sort_col = i; sort_desc = 0; }
                return;
            }
        }
        return;
    }

    /* Row click – reuse already-built s_rows from last draw */
    {
        int row_y = y + 54;
        int row_h = 24;
        for (i = 0; i < s_row_count; i++) {
            if (row_y + row_h > y + h - 44) break;
            if (mx >= x && mx < x+w && my >= row_y && my < row_y+row_h) {
                taskman_selected = s_rows[i].id;
                return;
            }
            row_y += row_h;
        }
    }
}
