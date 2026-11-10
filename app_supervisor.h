#ifndef APP_SUPERVISOR_H
#define APP_SUPERVISOR_H

#include "io.h"

#define MAX_SUPERVISED_APPS 16

/* Application Execution States */
#define APP_STATE_UNUSED    0
#define APP_STATE_RUNNING   1
#define APP_STATE_HUNG      2
#define APP_STATE_CRASHED   3
#define APP_STATE_RECOVERED 4

typedef struct {
    int app_kind;
    int state;
    u32 last_heartbeat_ticks;
    u32 crash_count;
    u32 fault_vector;
    u64 fault_rip;
    u64 fault_cr2;
    char error_msg[64];
    char name[32];
    int relaunch_btn_x;
    int relaunch_btn_y;
    int relaunch_btn_w;
    int relaunch_btn_h;
    int close_btn_x;
    int close_btn_y;
    int close_btn_w;
    int close_btn_h;
} app_proc_context_t;

void app_supervisor_init(void);
void app_supervisor_set_active_app(int app_kind);
int  app_supervisor_get_active_app(void);
void app_supervisor_heartbeat(int app_kind);
void app_supervisor_check_hangs(u32 current_ticks);
int  app_supervisor_handle_fault(int app_kind, u32 int_no, u64 rip, u64 cr2);
int  app_supervisor_is_crashed(int app_kind);
int  app_supervisor_is_hung(int app_kind);
const char *app_supervisor_get_error(int app_kind);
void app_supervisor_restart_app(int app_kind);
void app_supervisor_force_quit(int app_kind);
app_proc_context_t *app_supervisor_get_context(int app_kind);
void app_supervisor_draw_crash_card(int app_kind, int x, int y, int w, int h);

#endif /* APP_SUPERVISOR_H */
