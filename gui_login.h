#ifndef GUI_LOGIN_H
#define GUI_LOGIN_H

#include "gui_internal.h"
#include "user.h"

typedef enum {
    GUI_MODE_LOGIN,
    GUI_MODE_DESKTOP
} gui_mode_t;

typedef struct {
    int selected_user_index;
    char password[32];
    int password_len;
    int focused_field; /* 0 = password box, 1 = terminal pin box */
    char error[64];
    int show_hint;
    int show_recovery;
    int show_terminal_modal;
    char term_pin[16];
    int term_pin_len;
    char term_error[64];
} login_state_t;

extern gui_mode_t gui_mode;
extern login_state_t login_state;
extern rect_t login_card_rect;

void login_draw(void);
void login_handle_key(unsigned char key);
void login_handle_mouse(int mx, int my, int pressed_now, int left, int right);
void gui_unlock_to_desktop(void);
void gui_logout(void);
void gui_draw_avatar(int x, int y, int diameter, const user_profile_t *user);
void gui_invalidate_avatar_cache(void);

#endif /* GUI_LOGIN_H */
