#ifndef STDLIB_H
#define STDLIB_H
#include "heap.h"
#define malloc kmalloc
#define free kfree
#define realloc krealloc
void *memset(void *s, int c, unsigned int n);
void *memcpy(void *dest, const void *src, unsigned int n);
int abs(int j);
double pow(double base, double exponent);
#endif
