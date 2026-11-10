#ifndef TERMINAL_H
#define TERMINAL_H

extern int terminal_cursor_visible;
extern int terminal_focused;

void terminal_init(void);

/* ascii=0 if special key only */
void terminal_key(unsigned char ascii, int is_enter, int is_backspace,
                  int is_up, int is_down, int is_left, int is_right,
                  int is_ctrl_c, int is_tab);

void terminal_scroll(int delta);
void terminal_draw(int frame_x, int frame_y, int frame_w, int frame_h);

void terminal_clear(void);
void terminal_copy(void);
void terminal_paste(void);
void terminal_mouse_down(int mx, int my, int frame_x, int frame_y, int frame_w, int frame_h);
void terminal_mouse_drag(int mx, int my, int frame_x, int frame_y, int frame_w, int frame_h);
void terminal_mouse_up(void);
int  terminal_has_selection(void);
void terminal_run_script(const char *path);
void terminal_execute_line(const char *line);
int  terminal_is_superuser(void);
void terminal_set_active_user(const char *username, int is_root);

#endif
