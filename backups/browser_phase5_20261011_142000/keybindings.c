#include "keybindings.h"
#include "gui_internal.h"
#include "fs.h"

keybinding_t keybindings[MAX_KEYBINDINGS];
int num_keybindings = 0;

static void str_cpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void keybindings_init(void) {
    num_keybindings = 0;
    
    // Default shortcuts
    keybindings_add("Spotlight Search", ACTION_SPOTLIGHT,    1, 0, 0, ' ', ""); // Ctrl+Space
    keybindings_add("Launchpad",        ACTION_LAUNCHER,     1, 0, 0, 'l', ""); // Ctrl+L
    keybindings_add("Settings",         ACTION_SETTINGS,     0, 1, 0, 's', ""); // Alt+S
    keybindings_add("Terminal",         ACTION_TERMINAL,     0, 1, 0, 't', ""); // Alt+T
    keybindings_add("File Explorer",    ACTION_EXPLORER,     1, 0, 0, 'e', ""); // Ctrl+E
    keybindings_add("Text Editor",      ACTION_TEXT_EDIT,    1, 0, 0, 'n', ""); // Ctrl+N
    keybindings_add("Media Player",     ACTION_MEDIA_PLAYER, 1, 0, 0, 'm', ""); // Ctrl+M
    keybindings_add("Snap Window Left", ACTION_SNAP_LEFT,    0, 1, 0, KEY_SNAP_LEFT, ""); // Alt+Left
    keybindings_add("Snap Window Right",ACTION_SNAP_RIGHT,   0, 1, 0, KEY_SNAP_RIGHT, ""); // Alt+Right
    keybindings_add("Browser",          ACTION_BROWSER,      0, 1, 0, 'b', ""); // Alt+B
    keybindings_add("Wi-Fi Menu",       ACTION_WIFI_MENU,    0, 1, 0, 'n', ""); // Alt+N
    keybindings_add("Start Menu",       ACTION_START_MENU,   0, 1, 0, 'z', ""); // Alt+Z
    keybindings_add("Start Menu",       ACTION_START_MENU,   1, 0, 0, 0x01, ""); // Ctrl+Esc
}

int keybindings_add(const char *name, int action_id, int ctrl, int alt, int shift, int key, const char *cmd) {
    if (num_keybindings >= MAX_KEYBINDINGS) return -1;
    keybinding_t *kb = &keybindings[num_keybindings++];
    str_cpy(kb->name, name, 32);
    kb->action_id = action_id;
    kb->mod_ctrl = ctrl;
    kb->mod_alt = alt;
    kb->mod_shift = shift;
    kb->key = key;
    str_cpy(kb->custom_cmd, cmd ? cmd : "", 32);
    return num_keybindings - 1;
}

int keybindings_rebind(int index, int ctrl, int alt, int shift, int key) {
    if (index < 0 || index >= num_keybindings) return 0;
    keybindings[index].mod_ctrl = ctrl;
    keybindings[index].mod_alt = alt;
    keybindings[index].mod_shift = shift;
    keybindings[index].key = key;
    return 1;
}

int keybindings_handle(int scancode, char ascii_char, int ctrl, int alt, int shift) {
    if (ascii_char >= 'A' && ascii_char <= 'Z') ascii_char += ('a' - 'A');

    for (int i = 0; i < num_keybindings; i++) {
        keybinding_t *kb = &keybindings[i];
        if (kb->mod_ctrl != ctrl) continue;
        if (kb->mod_alt != alt) continue;
        if (kb->mod_shift != shift) continue;

        int key_match = 0;
        if (kb->key > 0 && kb->key < 128) {
            char target = (char)kb->key;
            if (target >= 'A' && target <= 'Z') target += ('a' - 'A');
            if (ascii_char == target) key_match = 1;
        } else if (kb->key == scancode) {
            key_match = 1;
        }

        if (key_match) {
            switch (kb->action_id) {
                case ACTION_SPOTLIGHT: {
                    extern int spotlight_open;
                    extern int spotlight_qlen;
                    extern char spotlight_query[64];
                    extern void spotlight_search(const char *query);
                    extern void spotlight_close_dropdown(void);
                    extern int  spotlight_is_in_dropdown(void);
                    if (spotlight_open && spotlight_is_in_dropdown()) {
                        spotlight_close_dropdown();
                    } else {
                        spotlight_open = !spotlight_open;
                        if (spotlight_open) {
                            spotlight_close_dropdown();
                            spotlight_qlen = 0;
                            spotlight_query[0] = 0;
                            spotlight_search("");
                        } else {
                            spotlight_close_dropdown();
                        }
                    }
                    needs_redraw = 1;
                    return 1;
                }
                case ACTION_LAUNCHER: {
                    extern int launcher_open;
                    launcher_open = !launcher_open;
                    needs_redraw = 1;
                    return 1;
                }
                case ACTION_SETTINGS: {
                    open_app(APP_SETTINGS);
                    needs_redraw = 1;
                    return 1;
                }
                case ACTION_TERMINAL: {
                    open_app(APP_TERMINAL);
                    needs_redraw = 1;
                    return 1;
                }
                case ACTION_EXPLORER: {
                    open_app(APP_EXPLORER);
                    needs_redraw = 1;
                    return 1;
                }
                case ACTION_TEXT_EDIT: {
                    open_app(APP_TEXT_EDIT);
                    needs_redraw = 1;
                    return 1;
                }
                case ACTION_MEDIA_PLAYER: {
                    open_app(APP_MEDIA_PLAYER);
                    needs_redraw = 1;
                    return 1;
                }
                case ACTION_BROWSER: {
                    open_app(APP_BROWSER);
                    needs_redraw = 1;
                    return 1;
                }
                case ACTION_WIFI_MENU: {
                    extern void gui_toggle_wifi_menu(void);
                    gui_toggle_wifi_menu();
                    return 1;
                }
                case ACTION_START_MENU: {
                    extern void gui_toggle_start_menu(void);
                    gui_toggle_start_menu();
                    needs_redraw = 1;
                    return 1;
                }
                case ACTION_SNAP_LEFT: {
                    app_window_t *active = active_app_window();
                    if (active && !active->minimized) {
                        apply_snap_to_window(active, 1);
                        needs_redraw = 1;
                    }
                    return 1;
                }
                case ACTION_SNAP_RIGHT: {
                    app_window_t *active = active_app_window();
                    if (active && !active->minimized) {
                        apply_snap_to_window(active, 2);
                        needs_redraw = 1;
                    }
                    return 1;
                }
                case ACTION_MINIMIZE: {
                    app_window_t *active = active_app_window();
                    if (active && !active->minimized) {
                        active->minimized = 1;
                        needs_redraw = 1;
                    }
                    return 1;
                }
                case ACTION_CUSTOM: {
                    if (kb->custom_cmd[0]) {
                        if (kb->custom_cmd[0] == 't') open_app(APP_TERMINAL);
                        else if (kb->custom_cmd[0] == 'e') open_app(APP_EXPLORER);
                        else if (kb->custom_cmd[0] == 's') open_app(APP_SETTINGS);
                        else open_app(APP_TEXT_EDIT);
                        needs_redraw = 1;
                    }
                    return 1;
                }
                default:
                    break;
            }
        }
    }
    return 0;
}

void handle_global_keybinding(int scancode, int modifiers) {
    keybindings_handle(scancode, 0, (modifiers & 2) != 0, (modifiers & 1) != 0, (modifiers & 4) != 0);
}
