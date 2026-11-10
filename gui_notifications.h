#ifndef GUI_NOTIFICATIONS_H
#define GUI_NOTIFICATIONS_H

#include "gui_internal.h"
#include "io.h"

#define MAX_NOTIFICATIONS 8

typedef struct {
    int id;
    int active;
    char app[20];
    char title[32];
    char message[64];
    char time_ago[16];
    int icon;
    u32 accent;
} gui_notification_t;

extern int notification_count;
extern int notification_center_open;
extern int notification_dnd;
extern rect_t top_notif_button;
extern rect_t notif_center_rect;

void draw_notification_center(void);
int  notification_center_handle_click(int mx, int my);

#endif /* GUI_NOTIFICATIONS_H */
