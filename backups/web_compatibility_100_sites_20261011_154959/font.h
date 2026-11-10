#ifndef FONT_H
#define FONT_H

#include "framebuffer.h"

void font_text(int x, int y, const char *text, u32 color, int scale);
void font_text_rich(int x, int y, const char *text, u32 color, int scale, int bold, int italic, int underline);
void font_text_buffer(u32 *buf, int bw, int bh, int x, int y, const char *text, u32 color);
void font_icon(int x, int y, int icon, u32 color);

#define ICON_START 1
#define ICON_APPS 2
#define ICON_POWER 3
#define ICON_RESTART 4
#define ICON_LOGOUT 5
#define ICON_SETTINGS 6
#define ICON_TERMINAL 7
#define ICON_FILES 8
#define ICON_DISPLAY 9
#define ICON_NETWORK 10
#define ICON_EXPLORER 11
#define ICON_FOLDER 12
#define ICON_FILE 13
#define ICON_DISK 14
#define ICON_PHOTO 15

#endif

