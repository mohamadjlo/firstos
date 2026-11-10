#include "app_supervisor.h"
#include "serial.h"
#include "framebuffer.h"
#include "font.h"
#include "gui_internal.h"

extern u32 get_system_ticks(void);
extern void close_app_window(int index);
extern void open_app(int kind);

static app_proc_context_t s_contexts[MAX_SUPERVISED_APPS];
static int s_active_app = 0;

static const char *s_app_names[MAX_SUPERVISED_APPS] = {
    "Kernel Core",
    "Terminal",
    "Files",
    "Display",
    "Explorer",
    "Settings",
    "Text Editor",
    "Media Player",
    "Calculator",
    "Preview",
    "Photo Editor",
    "Firstlang IDE",
    "Task Manager",
    "Web Browser",
    "System App",
    "User Process"
};

void app_supervisor_init(void) {
    for (int i = 0; i < MAX_SUPERVISED_APPS; i++) {
        s_contexts[i].app_kind = i;
        s_contexts[i].state = APP_STATE_UNUSED;
        s_contexts[i].last_heartbeat_ticks = 0;
        s_contexts[i].crash_count = 0;
        s_contexts[i].fault_vector = 0;
        s_contexts[i].fault_rip = 0;
        s_contexts[i].fault_cr2 = 0;
        s_contexts[i].error_msg[0] = '\0';
        s_contexts[i].relaunch_btn_w = 0;
        s_contexts[i].close_btn_w = 0;

        const char *src = (i < 16) ? s_app_names[i] : "App";
        int j = 0;
        while (src[j] && j < 31) {
            s_contexts[i].name[j] = src[j];
            j++;
        }
        s_contexts[i].name[j] = '\0';
    }
    serial_write("[app_supervisor] Process isolation & crash supervisor initialized\n");
}

void app_supervisor_set_active_app(int app_kind) {
    if (app_kind >= 0 && app_kind < MAX_SUPERVISED_APPS) {
        s_active_app = app_kind;
        if (s_contexts[app_kind].state == APP_STATE_UNUSED) {
            s_contexts[app_kind].state = APP_STATE_RUNNING;
        }
        s_contexts[app_kind].last_heartbeat_ticks = get_system_ticks();
    }
}

int app_supervisor_get_active_app(void) {
    return s_active_app;
}

void app_supervisor_heartbeat(int app_kind) {
    if (app_kind > 0 && app_kind < MAX_SUPERVISED_APPS) {
        s_contexts[app_kind].last_heartbeat_ticks = get_system_ticks();
        if (s_contexts[app_kind].state == APP_STATE_HUNG) {
            s_contexts[app_kind].state = APP_STATE_RUNNING;
            serial_write("[app_supervisor] App recovered from hang: ");
            serial_write(s_contexts[app_kind].name);
            serial_write("\n");
        }
    }
}

void app_supervisor_check_hangs(u32 current_ticks) {
    for (int i = 1; i < MAX_SUPERVISED_APPS; i++) {
        if (s_contexts[i].state == APP_STATE_RUNNING) {
            /* 300 ticks ~ 3.0 seconds at 100Hz */
            if (current_ticks > s_contexts[i].last_heartbeat_ticks &&
                (current_ticks - s_contexts[i].last_heartbeat_ticks) > 300) {
                s_contexts[i].state = APP_STATE_HUNG;
                serial_write("[app_supervisor] Watchdog: App unresponsive (>3s): ");
                serial_write(s_contexts[i].name);
                serial_write("\n");
            }
        }
    }
}

int app_supervisor_handle_fault(int app_kind, u32 int_no, u64 rip, u64 cr2) {
    if (app_kind <= 0 || app_kind >= MAX_SUPERVISED_APPS) {
        return 0; /* Kernel core fault */
    }

    app_proc_context_t *ctx = &s_contexts[app_kind];
    ctx->state = APP_STATE_CRASHED;
    ctx->crash_count++;
    ctx->fault_vector = int_no;
    ctx->fault_rip = rip;
    ctx->fault_cr2 = cr2;

    const char *vec_name = "Exception";
    if (int_no == 0) vec_name = "Divide by Zero (#DE)";
    else if (int_no == 6) vec_name = "Invalid Opcode (#UD)";
    else if (int_no == 13) vec_name = "General Protection (#GP)";
    else if (int_no == 14) vec_name = "Page Fault (#PF)";

    int p = 0;
    while (vec_name[p] && p < 40) {
        ctx->error_msg[p] = vec_name[p];
        p++;
    }
    ctx->error_msg[p] = '\0';

    serial_write("\n[app_supervisor] ==========================================\n");
    serial_write("[app_supervisor] ZERO-PANIC CONTAINMENT: Isolated crash in: ");
    serial_write(ctx->name);
    serial_write("\n[app_supervisor] Vector: ");
    serial_write_hex(int_no);
    serial_write(" RIP: 0x");
    serial_write_hex((u32)(rip >> 32));
    serial_write_hex((u32)rip);
    serial_write(" CR2: 0x");
    serial_write_hex((u32)(cr2 >> 32));
    serial_write_hex((u32)cr2);
    serial_write("\n[app_supervisor] Kernel desktop & sibling applications preserved!\n");
    serial_write("[app_supervisor] ==========================================\n\n");

    return 1; /* Successfully isolated! */
}

int app_supervisor_is_crashed(int app_kind) {
    if (app_kind > 0 && app_kind < MAX_SUPERVISED_APPS) {
        return s_contexts[app_kind].state == APP_STATE_CRASHED;
    }
    return 0;
}

int app_supervisor_is_hung(int app_kind) {
    if (app_kind > 0 && app_kind < MAX_SUPERVISED_APPS) {
        return s_contexts[app_kind].state == APP_STATE_HUNG;
    }
    return 0;
}

const char *app_supervisor_get_error(int app_kind) {
    if (app_kind > 0 && app_kind < MAX_SUPERVISED_APPS) {
        return s_contexts[app_kind].error_msg;
    }
    return "";
}

app_proc_context_t *app_supervisor_get_context(int app_kind) {
    if (app_kind >= 0 && app_kind < MAX_SUPERVISED_APPS) {
        return &s_contexts[app_kind];
    }
    return (void*)0;
}

void app_supervisor_restart_app(int app_kind) {
    if (app_kind > 0 && app_kind < MAX_SUPERVISED_APPS) {
        serial_write("[app_supervisor] Relaunching isolated app: ");
        serial_write(s_contexts[app_kind].name);
        serial_write("\n");

        s_contexts[app_kind].state = APP_STATE_RUNNING;
        s_contexts[app_kind].fault_vector = 0;
        s_contexts[app_kind].fault_rip = 0;
        s_contexts[app_kind].fault_cr2 = 0;
        s_contexts[app_kind].error_msg[0] = '\0';
        s_contexts[app_kind].last_heartbeat_ticks = get_system_ticks();

        /* Re-initialize application state cleanly */
        open_app(app_kind);
    }
}

void app_supervisor_force_quit(int app_kind) {
    if (app_kind > 0 && app_kind < MAX_SUPERVISED_APPS) {
        serial_write("[app_supervisor] Force quitting isolated app: ");
        serial_write(s_contexts[app_kind].name);
        serial_write("\n");

        s_contexts[app_kind].state = APP_STATE_UNUSED;
        s_contexts[app_kind].error_msg[0] = '\0';
        app_window_t *app = app_slot(app_kind);
        if (app) {
            app->open = 0;
            needs_redraw = 1;
        }
    }
}

void app_supervisor_draw_crash_card(int app_kind, int x, int y, int w, int h) {
    if (app_kind <= 0 || app_kind >= MAX_SUPERVISED_APPS) return;
    app_proc_context_t *ctx = &s_contexts[app_kind];

    /* Fill background with clean modern dark slate */
    fb_rect(x, y, w, h, 0x1E222A);

    /* Center modal crash card */
    int card_w = (w > 480) ? 440 : (w - 32);
    int card_h = 240;
    int card_x = x + (w - card_w) / 2;
    int card_y = y + (h - card_h) / 2;
    if (card_y < y + 8) card_y = y + 8;

    /* Card background and subtle shadow */
    fb_round_rect(card_x, card_y, card_w, card_h, 8, 0x2A2E39);
    fb_round_rect_border(card_x, card_y, card_w, card_h, 8, 0x3F4452);

    /* Warning Icon / Header */
    int icon_cx = card_x + 36;
    int icon_cy = card_y + 36;
    fb_round_rect(icon_cx - 14, icon_cy - 14, 28, 28, 14, 0xEF4444);
    font_text(icon_cx - 3, icon_cy - 7, "!", 0xFFFFFF, 2);

    /* Header text */
    font_text(card_x + 64, card_y + 20, "Application Safely Isolated", 0xF1F5F9, 1);
    font_text(card_x + 64, card_y + 38, ctx->name, 0x94A3B8, 1);

    /* Divider */
    fb_rect(card_x + 20, card_y + 60, card_w - 40, 1, 0x3F4452);

    /* Explanation message */
    font_text(card_x + 24, card_y + 76, "This application encountered an error and was stopped", 0xE2E8F0, 1);
    font_text(card_x + 24, card_y + 94, "to protect the operating system and other running apps.", 0xE2E8F0, 1);

    /* Diagnostic details */
    font_text(card_x + 24, card_y + 124, "Fault Vector:", 0x64748B, 1);
    font_text(card_x + 130, card_y + 124, ctx->error_msg[0] ? ctx->error_msg : "Memory Protection", 0xF87171, 1);

    font_text(card_x + 24, card_y + 144, "Protection Mode:", 0x64748B, 1);
    font_text(card_x + 130, card_y + 144, "FirstOS Zero-Panic Sandbox", 0x38BDF8, 1);

    /* Buttons */
    int btn_w = 140;
    int btn_h = 28;
    int btn_y = card_y + card_h - 44;

    /* Relaunch Button */
    int b1_x = card_x + 24;
    fb_round_rect(b1_x, btn_y, btn_w, btn_h, 6, 0x2563EB);
    fb_round_rect_border(b1_x, btn_y, btn_w, btn_h, 6, 0x3B82F6);
    font_text(b1_x + 14, btn_y + 8, "Relaunch App", 0xFFFFFF, 1);

    /* Close Window Button */
    int b2_x = card_x + card_w - btn_w - 24;
    fb_round_rect(b2_x, btn_y, btn_w, btn_h, 6, 0x374151);
    fb_round_rect_border(b2_x, btn_y, btn_w, btn_h, 6, 0x4B5563);
    font_text(b2_x + 20, btn_y + 8, "Close Window", 0xE5E7EB, 1);

    /* Store interactive button rectangles */
    ctx->relaunch_btn_x = b1_x;
    ctx->relaunch_btn_y = btn_y;
    ctx->relaunch_btn_w = btn_w;
    ctx->relaunch_btn_h = btn_h;

    ctx->close_btn_x = b2_x;
    ctx->close_btn_y = btn_y;
    ctx->close_btn_w = btn_w;
    ctx->close_btn_h = btn_h;
}
