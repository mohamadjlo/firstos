#ifndef STRING_H
#define STRING_H
void *memset(void *s, int c, unsigned int n);
void *memcpy(void *dest, const void *src, unsigned int n);
int strlen(const char *s);
int strncmp(const char *s1, const char *s2, unsigned int n);
char *strchr(const char *s, int c);
int sscanf(const char *str, const char *format, ...);
char *strncpy(char *dest, const char *src, unsigned int n);
int strcmp(const char *s1, const char *s2);
#endif
