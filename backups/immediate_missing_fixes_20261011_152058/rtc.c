#include "rtc.h"
#include "io.h"

static unsigned char cmos_read(unsigned char index) {
    outb(0x70, index);
    return inb(0x71);
}

static unsigned char bcd_to_bin(unsigned char value) {
    return (unsigned char)((value & 0x0F) + ((value >> 4) * 10));
}

static int update_in_progress(void) {
    outb(0x70, 0x0A);
    return (inb(0x71) & 0x80) != 0;
}

void rtc_read(rtc_time_t *time) {
    unsigned char status_b;
    if (!time) return;
    while (update_in_progress()) { }
    time->second = cmos_read(0x00);
    time->minute = cmos_read(0x02);
    time->hour = cmos_read(0x04);
    time->day = cmos_read(0x07);
    time->month = cmos_read(0x08);
    time->year = cmos_read(0x09);
    status_b = cmos_read(0x0B);
    if (!(status_b & 0x04)) {
        time->second = bcd_to_bin(time->second);
        time->minute = bcd_to_bin(time->minute);
        time->hour = bcd_to_bin((unsigned char)(time->hour & 0x7F));
        time->day = bcd_to_bin(time->day);
        time->month = bcd_to_bin(time->month);
        time->year = bcd_to_bin((unsigned char)time->year);
    }
    time->year = 2000 + time->year;
}
