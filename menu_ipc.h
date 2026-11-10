#ifndef MENU_IPC_H
#define MENU_IPC_H

#define MAX_MENU_ITEMS 10

typedef struct {
    char label[32];
    int action_id;
    int flags; // e.g., 0x1 = Disabled
} menu_item_t;

typedef struct {
    char title[32];
    int item_count;
    menu_item_t items[MAX_MENU_ITEMS];
    void (*callback_handler)(int action_id);
} menu_layout_t;

// Standard IPC Call for Apps to sync their menus with the Top Bar
void ipc_register_app_menu(const char* app_name, menu_layout_t* menus, int menu_count);

#endif
