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
void compositor_damage(int x,int y,int w,int h){int i;if(w<=0||h<=0)return;extern void gpu_schedule_present(void);gpu_schedule_present();for(i=0;i<damage_count;i++){comp_rect_t*r=&damages[i];int rr=r->x+r->w,rb=r->y+r->h,nr=x+w,nb=y+h;if(nr<r->x||x>rr||nb<r->y||y>rb)continue;r->x=min_i(r->x,x);r->y=min_i(r->y,y);rr=max_i(rr,nr);rb=max_i(rb,nb);r->w=rr-r->x;r->h=rb-r->y;return;}if(damage_count<COMP_MAX_DAMAGE){damages[damage_count].x=x;damages[damage_count].y=y;damages[damage_count].w=w;damages[damage_count].h=h;damage_count++;}}
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
    extern void gpu_notify_present(void);
    gpu_notify_present();
    (void)w; (void)h;
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
    s_stats.mmio_notify_count += 1;
    s_stats.bytes_submitted_total += (u64)transfers * ((u64)s_stats.last_damage_w * s_stats.last_damage_h * 4);
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

#if FIRSTOS_GFX_TELEMETRY_V2
static u64 s_render_start_tsc = 0;

void gfx_telemetry_render_begin(void) {
    s_render_start_tsc = gfx_rdtsc();
}

void gfx_telemetry_render_end(void) {
    u64 now = gfx_rdtsc();
    if (now >= s_render_start_tsc) {
        s_stats.last_render_cycles = now - s_render_start_tsc;
    }
}

void gfx_telemetry_idle_wakeup(void) {
    s_stats.idle_wakeups_total++;
}

void gfx_telemetry_record_damage_rects(u32 count) {
    s_stats.damage_rect_count_last = count;
}

void gfx_telemetry_record_input_queue(u32 depth, u32 dropped) {
    s_stats.input_queue_depth = depth;
    s_stats.dropped_events_total += dropped;
}
#endif

#ifndef COMPOSITOR_SERIAL_LOG_1HZ
#define COMPOSITOR_SERIAL_LOG_1HZ 0
#endif

static u32 s_last_logged_frame_count = 0xFFFFFFFFu;

void compositor_tick_1hz(void) {
    s_stats.current_fps = s_fps_frames;
    s_fps_frames = 0;
    s_stats.current_mouse_rate = s_mouse_moves_sec;
    s_mouse_moves_sec = 0;
#if COMPOSITOR_SERIAL_LOG_1HZ
    if (s_stats.frame_count != s_last_logged_frame_count) {
        s_last_logged_frame_count = s_stats.frame_count;
        compositor_log_stats();
    }
#endif
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
    serial_write(" notify=");
    serial_write_hex(s_stats.mmio_notify_count);
    serial_write(" wakeups=");
    serial_write_hex(s_stats.idle_wakeups_total);
    serial_write(" qdepth=");
    serial_write_hex(s_stats.input_queue_depth);
    serial_write(" dropped=");
    serial_write_hex(s_stats.dropped_events_total);
    serial_write(" last_dmg=");
    serial_write_hex(s_stats.last_damage_w);
    serial_write("x");
    serial_write_hex(s_stats.last_damage_h);
    serial_write("\n");
}

int compositor_present(const void *backbuffer, u32 pitch) {
    extern int gpu_supports_partial_present(void);
    extern int gpu_present_rect_active(const void* backbuffer, u32 x, u32 y, u32 w, u32 h, u32 pitch);
    extern int gpu_present_active(const void* backbuffer, u32 width, u32 height, u32 pitch);
    extern u32 gpu_get_active_width(void);
    extern u32 gpu_get_active_height(void);

    if (!backbuffer || !enabled_flag) return -1;

    int dirty_count = fb_get_dirty_rect_count();
    if (dirty_count <= 0 && damage_count <= 0) {
        return 0; /* clean - skip unnecessary present */
    }

    u32 scr_w = gpu_get_active_width();
    u32 scr_h = gpu_get_active_height();
    if (scr_w == 0) scr_w = fb.width;
    if (scr_h == 0) scr_h = fb.height;

    u32 rx = 0, ry = 0, rw = 0, rh = 0;
    int has_rect = fb_get_dirty_rect(0, &rx, &ry, &rw, &rh);
    if (has_rect && dirty_count > 1) {
        u32 min_x = rx, min_y = ry, max_x = rx + rw, max_y = ry + rh;
        for (int i = 1; i < dirty_count; i++) {
            u32 drx = 0, dry = 0, drw = 0, drh = 0;
            if (fb_get_dirty_rect(i, &drx, &dry, &drw, &drh) && drw > 0 && drh > 0) {
                if (drx < min_x) min_x = drx;
                if (dry < min_y) min_y = dry;
                if (drx + drw > max_x) max_x = drx + drw;
                if (dry + drh > max_y) max_y = dry + drh;
            }
        }
        rx = min_x;
        ry = min_y;
        rw = max_x - min_x;
        rh = max_y - min_y;
    }
    if (rx + rw > scr_w) rw = scr_w > rx ? scr_w - rx : 0;
    if (ry + rh > scr_h) rh = scr_h > ry ? scr_h - ry : 0;
    int is_partial = (has_rect && rw > 0 && rh > 0 && (rw < scr_w || rh < scr_h));

    if (is_partial && gpu_supports_partial_present()) {
        static int s_logged_policy = 0;
        if (s_logged_policy < 5) {
            s_logged_policy++;
            serial_write("[compositor] dirty rect present x=");
            serial_write_hex(rx);
            serial_write(" y=");
            serial_write_hex(ry);
            serial_write(" w=");
            serial_write_hex(rw);
            serial_write(" h=");
            serial_write_hex(rh);
            serial_write("\n");
        }
        int rc = gpu_present_rect_active(backbuffer, rx, ry, rw, rh, pitch);
        if (rc < 0) {
            rc = gpu_present_active(backbuffer, scr_w, scr_h, pitch);
        }
        compositor_reset_damage();
        return rc;
    }

    int rc = gpu_present_active(backbuffer, scr_w, scr_h, pitch);
    compositor_reset_damage();
    return rc;
}


