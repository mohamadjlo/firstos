#include "ps2.h"
#include "io.h"
#include "framebuffer.h"
#include "serial.h"

typedef unsigned char u8;

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64

static int mouse_x;
static int mouse_y;
static int mouse_left;
static int mouse_right;
static int mouse_middle;
static int mouse_wheel_delta;
static int mouse_packet_index;
static u8 mouse_packet[4];
static int mouse_packet_size = 4;
static u8 key_fifo[64];
static int key_head = 0;
static int key_tail = 0;

static void wait_input(void) {
    int i;
    for (i = 0; i < 100000; i++) {
        if ((inb(PS2_STATUS) & 2) == 0) return;
    }
}

static int can_read(void) {
    return (inb(PS2_STATUS) & 1) != 0;
}

static void write_cmd(u8 value) {
    wait_input();
    outb(PS2_COMMAND, value);
}

static void write_data(u8 value) {
    wait_input();
    outb(PS2_DATA, value);
}

static u8 read_data_blocking(void) {
    int i;
    for (i = 0; i < 100000; i++) {
        if (can_read()) return inb(PS2_DATA);
    }
    return 0;
}

void ps2_init(void) {
    u8 status;
    write_cmd(0xAD);
    write_cmd(0xA7);
    while (can_read()) (void)inb(PS2_DATA);

    write_cmd(0x20);
    status = read_data_blocking();
    status |= 0x03;
    status &= (u8)~0x20;
    write_cmd(0x60);
    write_data(status);

    write_cmd(0xA8);
    write_cmd(0xAE);

    write_cmd(0xD4);
    write_data(0xF3);
    (void)read_data_blocking();
    write_cmd(0xD4);
    write_data(200);
    (void)read_data_blocking();
    write_cmd(0xD4);
    write_data(0xF3);
    (void)read_data_blocking();
    write_cmd(0xD4);
    write_data(100);
    (void)read_data_blocking();
    write_cmd(0xD4);
    write_data(0xF3);
    (void)read_data_blocking();
    write_cmd(0xD4);
    write_data(80);
    (void)read_data_blocking();
    write_cmd(0xD4);
    write_data(0xF2);
    (void)read_data_blocking();
    (void)read_data_blocking();

    write_cmd(0xD4);
    write_data(0xF6);
    (void)read_data_blocking();

    write_cmd(0xD4);
    write_data(0xF4);
    (void)read_data_blocking();

    mouse_packet_index = 0;
    mouse_x = (int)fb.width / 2;
    mouse_y = (int)fb.height / 2;
    mouse_left = 0;
    mouse_right = 0;
    mouse_middle = 0;
    mouse_wheel_delta = 0;
    key_head = 0;
    key_tail = 0;
    serial_write("[ps2] keyboard and mouse initialized\n");
}

void ps2_poll(void) {
    int guard = 64;
    while (guard-- > 0 && can_read()) {
        u8 status = inb(PS2_STATUS);
        u8 data = inb(PS2_DATA);

        if (status & 0x20) {
            if (mouse_packet_index == 0 && (data & 0x08) == 0) {
                continue;
            }

            mouse_packet[mouse_packet_index++] = data;
            if (mouse_packet_index == mouse_packet_size) {
                int dx = (int)(signed char)mouse_packet[1];
                int dy = (int)(signed char)mouse_packet[2];

                if (mouse_packet[0] & 0x10) dx |= ~0xFF;
                if (mouse_packet[0] & 0x20) dy |= ~0xFF;

                if (mouse_sensitivity != 10 && mouse_sensitivity > 0) {
                    dx = (dx * mouse_sensitivity) / 10;
                    dy = (dy * mouse_sensitivity) / 10;
                }

                if (!(mouse_packet[0] & 0x40)) mouse_x += dx;
                if (!(mouse_packet[0] & 0x80)) mouse_y -= dy;

                if (mouse_x < 0) mouse_x = 0;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_x > (int)fb.width - 12) mouse_x = (int)fb.width - 12;
                if (mouse_y > (int)fb.height - 18) mouse_y = (int)fb.height - 18;

                int raw_left = (mouse_packet[0] & 0x01) ? 1 : 0;
                int raw_right = (mouse_packet[0] & 0x02) ? 1 : 0;
                if (mouse_swap_buttons) {
                    mouse_left = raw_right;
                    mouse_right = raw_left;
                } else {
                    mouse_left = raw_left;
                    mouse_right = raw_right;
                }
                mouse_middle = (mouse_packet[0] & 0x04) ? 1 : 0;
                if (mouse_packet_size >= 4) {
                    int wheel = (int)((signed char)(mouse_packet[3] & 0x0F));
                    if (wheel & 0x08) wheel |= ~0x0F;
                    if (mouse_scroll_multiplier > 1) wheel *= mouse_scroll_multiplier;
                    mouse_wheel_delta += wheel;
                }
                mouse_packet_index = 0;
            }
        } else {
            int next_head = (key_head + 1) % 64;
            if (next_head != key_tail) {
                key_fifo[key_head] = data;
                key_head = next_head;
            }
        }
    }
}

int mouse_swap_buttons = 0;
int mouse_sensitivity = 10;
int mouse_scroll_multiplier = 1;

void ps2_set_swap_buttons(int swap) { mouse_swap_buttons = swap ? 1 : 0; }
void ps2_set_sensitivity(int sens) { if (sens >= 2 && sens <= 30) mouse_sensitivity = sens; }
void ps2_set_scroll_multiplier(int mult) { if (mult >= 1 && mult <= 10) mouse_scroll_multiplier = mult; }

int ps2_mouse_x(void) { return mouse_x; }
int ps2_mouse_y(void) { return mouse_y; }
int ps2_mouse_left(void) { return mouse_left; }
int ps2_mouse_right(void) { return mouse_right; }
int ps2_mouse_middle(void) { return mouse_middle; }
unsigned char ps2_last_key(void) {
    if (key_head == key_tail) return 0;
    u8 k = key_fifo[key_tail];
    key_tail = (key_tail + 1) % 64;
    return k;
}

int ps2_mouse_wheel(void) { int d = mouse_wheel_delta; mouse_wheel_delta = 0; return d; }
