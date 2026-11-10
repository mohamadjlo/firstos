#ifndef PS2_H
#define PS2_H

void ps2_init(void);
void ps2_poll(void);
int ps2_mouse_x(void);
int ps2_mouse_y(void);
int ps2_mouse_left(void);
int ps2_mouse_right(void);
int ps2_mouse_middle(void);
int ps2_mouse_wheel(void);
unsigned char ps2_last_key(void);
extern int mouse_swap_buttons;
extern int mouse_sensitivity;
extern int mouse_scroll_multiplier;

void ps2_set_swap_buttons(int swap);
void ps2_set_sensitivity(int sens);
void ps2_set_scroll_multiplier(int mult);

int ps2_get_queue_depth(void);
unsigned int ps2_get_dropped_events(void);
unsigned int ps2_get_mouse_packets(void);
unsigned int ps2_get_mouse_sync_drops(void);
int ps2_has_input(void);

#endif

