#ifndef IO_H
#define IO_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef signed long long s64;

#ifndef _UINTPTR_T_DECLARED
#define _UINTPTR_T_DECLARED
typedef unsigned long uintptr_t;
#endif

#ifndef _INTPTR_T_DECLARED
#define _INTPTR_T_DECLARED
typedef signed long intptr_t;
#endif

void outb(u16 port, u8 value);
void outw(u16 port, u16 value);
u8 inb(u16 port);

#endif
