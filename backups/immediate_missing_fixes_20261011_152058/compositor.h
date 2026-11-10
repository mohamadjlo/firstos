#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "framebuffer.h"

typedef unsigned int u32;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} comp_rect_t;

typedef struct {
    int id;
    int x;
    int y;
    int w;
    int h;
    u32 color;
    int visible;
} comp_surface_t;

void compositor_init(int screen_w, int screen_h);
int compositor_enabled(void);
int compositor_create_surface(int x, int y, int w, int h, u32 color);
void compositor_move_surface(int id, int x, int y);
void compositor_damage(int x, int y, int w, int h);
int compositor_damage_count(void);
const comp_rect_t *compositor_damage_at(int index);
void compositor_reset_damage(void);
int compositor_surface_count(void);
const comp_surface_t *compositor_surface_at(int index);

// New additions
void fb_blend_rect_simd(int x, int y, int w, int h, u32 color, unsigned char alpha);

#endif
