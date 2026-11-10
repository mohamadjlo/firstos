#include "pic.h"
#include "io.h"

void pit_init(unsigned int hz) {
    if (hz < 18) hz = 18;
    if (hz > 1000) hz = 1000;
    unsigned short divisor = (unsigned short)(1193182u / hz);
    outb(0x43, 0x36); // Channel 0, lobyte/hibyte, Mode 3 square wave
    outb(0x40, (unsigned char)(divisor & 0xFF));
    outb(0x40, (unsigned char)((divisor >> 8) & 0xFF));
}

void pic_init(void) {
    // ICW1: init + ICW4
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    
    // ICW2: map IRQ0-7 to INT 32-39, IRQ8-15 to INT 40-47
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    
    // ICW3: cascade
    outb(0x21, 4);
    outb(0xA1, 2);
    
    // ICW4: 8086 mode
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    // Mask all interrupts EXCEPT IRQ0 (Timer)
    // 0xFE = 11111110 (Bit 0 is 0, so IRQ0 is enabled)
    // 0xFF = 11111111 (All masked)
    outb(0x21, 0xFE);
    outb(0xA1, 0xFF);

    // Initialize PIT to 100 Hz (10ms per tick) to match FirstOS scheduler and clock calibration
    pit_init(100);
}
