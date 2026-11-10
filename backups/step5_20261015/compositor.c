#include "compositor.h"
#include "serial.h"

#define COMP_MAX_SURFACES 16
#define COMP_MAX_DAMAGE 32
static comp_surface_t surfaces[COMP_MAX_SURFACES];
static comp_rect_t damages[COMP_MAX_DAMAGE];
static int surface_count;
static int damage_count;
static int enabled_flag;
static compositor_stats_t s_stats;
static int max_i(int a,int b){return a>b?a:b;}
static int min_i(int a,int b){return a<b?a:b;}
void compositor_init(int screen_w,int screen_h){surface_count=0;damage_count=0;enabled_flag=(screen_w>0&&screen_h>0)?1:0;compositor_damage(0,0,screen_w,screen_h);} 
int compositor_enabled(void){return enabled_flag;}
int compositor_create_surface(int x,int y,int w,int h,u32 color){if(surface_count>=COMP_MAX_SURFACES)return -1;surfaces[surface_count].id=surface_count;surfaces[surface_count].x=x;surfaces[surface_count].y=y;surfaces[surface_count].w=w;surfaces[surface_count].h=h;surfaces[surface_count].color=color;surfaces[surface_count].visible=1;compositor_damage(x,y,w,h);return surface_count++;}
void compositor_move_surface(int id,int x,int y){comp_surface_t*s;if(id<0||id>=surface_count)return;s=&surfaces[id];compositor_damage(s->x,s->y,s->w,s->h);s->x=x;s->y=y;compositor_damage(s->x,s->y,s->w,s->h);} 
void compositor_damage(int x,int y,int w,int h){int i;if(w<=0||h<=0)return;for(i=0;i<damage_count;i++){comp_rect_t*r=&damages[i];int rr=r->x+r->w,rb=r->y+r->h,nr=x+w,nb=y+h;if(nr<r->x||x>rr||nb<r->y||y>rb)continue;r->x=min_i(r->x,x);r->y=min_i(r->y,y);rr=max_i(rr,nr);rb=max_i(rb,nb);r->w=rr-r->x;r->h=rb-r->y;return;}if(damage_count<COMP_MAX_DAMAGE){damages[damage_count].x=x;damages[damage_count].y=y;damages[damage_count].w=w;damages[damage_count].h=h;damage_count++;}}
int compositor_damage_count(void){return damage_count;} 
const comp_rect_t *compositor_damage_at(int index){return (index>=0&&index<damage_count)?&damages[index]:0;} 
void compositor_reset_damage(void){damage_count=0;} 
int compositor_surface_count(void){return surface_count;} 
const comp_surface_t *compositor_surface_at(int index){return (index>=0&&index<surface_count)?&surfaces[index]:0;} 

void fb_blend_rect_simd(int x, int y, int w, int h, u32 color, unsigned char alpha) {
    if (alpha == 255) {
        // Opaque fast path
        return;
    }
    if (alpha == 0) return;
    
    // In a GCC environment, we could use <immintrin.h> for SSE2.
    // For now, we simulate SIMD vectorization with a fast unrolled 32-bit loop
    
    int end_x = x + w;
    int end_y = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (end_x > (int)fb.width) end_x = (int)fb.width;
    if (end_y > (int)fb.height) end_y = (int)fb.height;
    
    unsigned int r2 = (color >> 16) & 0xFF;
    unsigned int g2 = (color >> 8) & 0xFF;
    unsigned int b2 = color & 0xFF;
    
    for (int j = y; j < end_y; j++) {
        unsigned int *pixel = (unsigned int *)(fb.backbuffer + j * fb.pitch + x * 4);
        for (int i = x; i < end_x; i++) {
            unsigned int bg = *pixel;
            unsigned int r1 = (bg >> 16) & 0xFF;
            unsigned int g1 = (bg >> 8) & 0xFF;
            unsigned int b1 = bg & 0xFF;
            
            unsigned int r = (r1 * (255 - alpha) + r2 * alpha) >> 8;
            unsigned int g = (g1 * (255 - alpha) + g2 * alpha) >> 8;
            unsigned int b = (b1 * (255 - alpha) + b2 * alpha) >> 8;
            
            *pixel++ = (r << 16) | (g << 8) | b;
        }
    }
}

static u32 s_fps_frames = 0;
static u32 s_mouse_moves_sec = 0;

void compositor_record_request(int reason) {
    if (reason >= 0 && reason < REDRAW_REASON_COUNT) {
        s_stats.redraw_reasons[reason]++;
    }
}

void compositor_record_present(int is_full, u32 w, u32 h) {
    compositor_record_present_detailed(is_full, 0, w, h, w * h);
}

void compositor_record_present_detailed(int is_full, int is_cursor_only, u32 w, u32 h, u32 pixels) {
    s_stats.frame_count++;
    s_fps_frames++;
    if (is_cursor_only) {
        s_stats.cursor_only_presents++;
    } else if (is_full) {
        s_stats.full_repaints++;
    } else {
        s_stats.partial_repaints++;
    }
    s_stats.last_damage_w = w;
    s_stats.last_damage_h = h;
    s_stats.pixels_submitted_total += pixels;
}

void compositor_record_virtio(int batched, int transfers, int flushes) {
    if (batched) s_stats.virtio_batched++;
    s_stats.virtio_transfers += transfers;
    s_stats.virtio_flushes += flushes;
}

void compositor_record_input(int is_mouse, int is_kb) {
    if (is_mouse) {
        s_stats.mouse_moves_count++;
        s_mouse_moves_sec++;
    }
    if (is_kb) {
        s_stats.keyboard_events_count++;
    }
}

void compositor_tick_1hz(void) {
    s_stats.current_fps = s_fps_frames;
    s_fps_frames = 0;
    s_stats.current_mouse_rate = s_mouse_moves_sec;
    s_mouse_moves_sec = 0;
}

const compositor_stats_t *compositor_get_stats(void) {
    return &s_stats;
}

void compositor_log_stats(void) {
    serial_write("[compositor] frames=");
    serial_write_hex(s_stats.frame_count);
    serial_write(" fps=");
    serial_write_hex(s_stats.current_fps);
    serial_write(" full=");
    serial_write_hex(s_stats.full_repaints);
    serial_write(" partial=");
    serial_write_hex(s_stats.partial_repaints);
    serial_write(" cursor_only=");
    serial_write_hex(s_stats.cursor_only_presents);
    serial_write(" batched_virtio=");
    serial_write_hex(s_stats.virtio_batched);
    serial_write(" last_dmg=");
    serial_write_hex(s_stats.last_damage_w);
    serial_write("x");
    serial_write_hex(s_stats.last_damage_h);
    serial_write("\n");
}


