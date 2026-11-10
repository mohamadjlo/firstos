#include "serial.h"
#include "io.h"

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

int serial_has_char(void) {
    return inb(COM1 + 5) & 1;
}

char serial_read_char(void) {
    while (serial_has_char() == 0) {}
    return inb(COM1);
}

void serial_write_char(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) {}
    outb(COM1, (u8)c);
}

void serial_write(const char* s) {
    while (*s) {
        if (*s == '\n') serial_write_char('\r');
        serial_write_char(*s++);
    }
}

void serial_write_hex(u32 value) {
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    serial_write("0x");
    for (shift = 28; shift >= 0; shift -= 4) {
        serial_write_char(digits[(value >> shift) & 0xF]);
    }
}
