#ifndef STDIO_H
#define STDIO_H

#ifndef NULL
#define NULL 0
#endif

int sscanf(const char *str, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, unsigned int size, const char *format, ...);

#endif
