#ifndef KEYBINDINGS_H
#define KEYBINDINGS_H

#define KEY_SNAP_LEFT  0xCB // Left Arrow (Extended)
#define KEY_SNAP_RIGHT 0xCD // Right Arrow (Extended)
#define KEY_MAXIMIZE   0xC8 // Up Arrow (Extended)
#define KEY_MINIMIZE   0xD0 // Down Arrow (Extended)
#define KEY_ALT_TAB    0x0F // Tab (when Alt is held)

enum {
    ACTION_NONE = 0,
    ACTION_SPOTLIGHT = 1,
    ACTION_LAUNCHER = 2,
    ACTION_SETTINGS = 3,
    ACTION_TERMINAL = 4,
    ACTION_EXPLORER = 5,
    ACTION_TEXT_EDIT = 6,
    ACTION_MEDIA_PLAYER = 7,
    ACTION_SNAP_LEFT = 8,
    ACTION_SNAP_RIGHT = 9,
    ACTION_MINIMIZE = 10,
    ACTION_CUSTOM = 11,
    ACTION_BROWSER = 12,
    ACTION_WIFI_MENU = 13,
    ACTION_START_MENU = 15
};

typedef struct {
    char name[32];
    int action_id;
    int mod_ctrl;
    int mod_alt;
    int mod_shift;
    int key; // ASCII char (e.g. 'T') or special scancode
    char custom_cmd[32];
} keybinding_t;

#define MAX_KEYBINDINGS 32
extern keybinding_t keybindings[MAX_KEYBINDINGS];
extern int num_keybindings;

void keybindings_init(void);
int  keybindings_handle(int scancode, char ascii_char, int ctrl, int alt, int shift);
int  keybindings_rebind(int index, int ctrl, int alt, int shift, int key);
int  keybindings_add(const char *name, int action_id, int ctrl, int alt, int shift, int key, const char *cmd);

void handle_global_keybinding(int scancode, int modifiers);

#endif
