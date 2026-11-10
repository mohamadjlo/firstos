#ifndef SERIAL_H
#define SERIAL_H

void serial_init(void);
void serial_write_char(char c);
void serial_write(const char* s);
void serial_write_hex(unsigned int value);
int serial_has_char(void);
char serial_read_char(void);

#endif
