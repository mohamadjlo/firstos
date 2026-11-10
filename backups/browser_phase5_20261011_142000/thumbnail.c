#include "thumbnail.h"
#include "image.h"
#include "heap.h"
#include "framebuffer.h"
#include "fs.h"
#include "task.h"

extern void fb_putpixel(int x, int y, u32 color);
extern void fb_rect(int x, int y, int w, int h, u32 color);
extern void fb_rect_alpha(int x, int y, int w, int h, u32 color, u8 alpha);
extern void fb_round_rect(int x, int y, int w, int h, int r, u32 color);
extern void fb_round_rect_border(int x, int y, int w, int h, int r, u32 color);
extern void fb_round_rect_alpha(int x, int y, int w, int h, int r, u32 color, u8 alpha);
extern void outline(int x, int y, int w, int h, u32 color);
extern void draw_label(int x, int y, const char *text, u32 color);

static file_thumbnail_t cache[MAX_THUMBNAILS];
static int cache_inited = 0;

static int t_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static void t_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    if (!dst || max <= 0) return;
    if (src) {
        while (src[i] && i < max - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static int t_ends_ci(const char *str, const char *ext) {
    if (!str || !ext) return 0;
    int slen = 0, elen = 0;
    while (str[slen]) slen++;
    while (ext[elen]) elen++;
    if (slen < elen) return 0;
    for (int i = 0; i < elen; i++) {
        char a = str[slen - elen + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

void thumbnail_init(void) {
    for (int i = 0; i < MAX_THUMBNAILS; i++) {
        cache[i].path[0] = 0;
        cache[i].w = 0;
        cache[i].h = 0;
        cache[i].pixels = 0;
        cache[i].is_video = 0;
        cache[i].last_used = 0;
    }
    cache_inited = 1;
}

static u32 *decode_image_file(const char *path, int *out_w, int *out_h) {
    if (t_ends_ci(path, ".raw")) {
        u32 *raw = (u32*)kmalloc(1024 * 768 * 4);
        if (!raw) return 0;
        if (fs_read_raw_at(path, (char*)raw, 1024 * 768 * 4) != 1024 * 768 * 4) {
            kfree(raw);
            return 0;
        }
        *out_w = 1024;
        *out_h = 768;
        return raw;
    }

    int w = 0, h = 0;
    u8 *img = image_load(path, &w, &h);
    if (!img) return 0;

    u32 *argb = (u32*)img;
    int count = w * h;
    for (int i = 0; i < count; i++) {
        u32 p = argb[i];
        argb[i] = (p & 0xFF00FF00) | ((p & 0xFF) << 16) | ((p >> 16) & 0xFF);
    }
    *out_w = w;
    *out_h = h;
    return argb;
}

static u32 *decode_avi_first_frame(const char *path, int *out_w, int *out_h) {
    int read_sz = 131072; // Read up to 128KB header & first chunk
    u8 *buf = (u8*)kmalloc(read_sz);
    if (!buf) return 0;

    int n = fs_read_at(path, (char*)buf, read_sz);
    if (n < 64) {
        kfree(buf);
        return 0;
    }

    if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' || buf[3] != 'F' ||
        buf[8] != 'A' || buf[9] != 'V' || buf[10] != 'I' || buf[11] != ' ') {
        kfree(buf);
        return 0;
    }

    int cur = 12;
    int chunk_offset = -1;
    int chunk_len = 0;

    while (cur + 8 < n) {
        u32 cid = *(u32*)(buf + cur);
        u32 csz = *(u32*)(buf + cur + 4);
        if (cid == 0x7473696C || cid == 0x5453494C) { // "LIST"
            cur += 12; // skip header + list type
            continue;
        }
        if (cid == 0x63643030 || cid == 0x62643030 || (cid & 0x0000FFFF) == 0x00006430) {
            chunk_offset = cur + 8;
            chunk_len = (int)csz;
            break;
        }
        cur += 8 + ((csz + 1) & ~1u);
    }

    u32 *res = 0;
    if (chunk_offset > 0 && chunk_offset + chunk_len <= n) {
        int dw = 0, dh = 0;
        u8 *dec = image_load_from_memory(buf + chunk_offset, chunk_len, &dw, &dh);
        if (dec) {
            res = (u32*)dec;
            *out_w = dw;
            *out_h = dh;
            int count = dw * dh;
            for (int i = 0; i < count; i++) {
                u32 p = res[i];
                res[i] = (p & 0xFF00FF00) | ((p & 0xFF) << 16) | ((p >> 16) & 0xFF);
            }
        }
    }

    kfree(buf);
    return res;
}

const file_thumbnail_t *thumbnail_get(const char *path, int target_w, int target_h) {
    if (!path || !path[0] || target_w <= 0 || target_h <= 0) return 0;
    if (!cache_inited) thumbnail_init();

    unsigned int now = get_system_ticks();

    // 1. Check cache
    for (int i = 0; i < MAX_THUMBNAILS; i++) {
        if (cache[i].pixels && t_streq(cache[i].path, path) && cache[i].w == target_w && cache[i].h == target_h) {
            cache[i].last_used = now;
            return &cache[i];
        }
    }

    // 2. Decode source
    int is_video = t_ends_ci(path, ".avi") || t_ends_ci(path, ".mp4");
    int src_w = 0, src_h = 0;
    u32 *src_pixels = 0;

    if (is_video) {
        src_pixels = decode_avi_first_frame(path, &src_w, &src_h);
    } else if (t_ends_ci(path, ".bmp") || t_ends_ci(path, ".png") || t_ends_ci(path, ".jpg") ||
               t_ends_ci(path, ".jpeg") || t_ends_ci(path, ".tif") || t_ends_ci(path, ".tiff") ||
               t_ends_ci(path, ".raw")) {
        src_pixels = decode_image_file(path, &src_w, &src_h);
    }

    if (!src_pixels || src_w <= 0 || src_h <= 0) {
        if (src_pixels) kfree(src_pixels);
        return 0;
    }

    // 3. Resample into target_w * target_h
    u32 *thumb_pixels = (u32*)kmalloc(target_w * target_h * sizeof(u32));
    if (!thumb_pixels) {
        kfree(src_pixels);
        return 0;
    }

    for (int ty = 0; ty < target_h; ty++) {
        int sy = (ty * src_h) / target_h;
        if (sy >= src_h) sy = src_h - 1;
        u32 *src_row = src_pixels + sy * src_w;
        u32 *dst_row = thumb_pixels + ty * target_w;
        for (int tx = 0; tx < target_w; tx++) {
            int sx = (tx * src_w) / target_w;
            if (sx >= src_w) sx = src_w - 1;
            dst_row[tx] = src_row[sx];
        }
    }
    kfree(src_pixels);

    // 4. Find slot (empty or LRU)
    int best_slot = 0;
    unsigned int oldest = 0xFFFFFFFF;
    for (int i = 0; i < MAX_THUMBNAILS; i++) {
        if (!cache[i].pixels) {
            best_slot = i;
            break;
        }
        if (cache[i].last_used < oldest) {
            oldest = cache[i].last_used;
            best_slot = i;
        }
    }

    if (cache[best_slot].pixels) {
        kfree(cache[best_slot].pixels);
        cache[best_slot].pixels = 0;
    }

    t_strcpy(cache[best_slot].path, path, sizeof(cache[best_slot].path));
    cache[best_slot].w = target_w;
    cache[best_slot].h = target_h;
    cache[best_slot].pixels = thumb_pixels;
    cache[best_slot].is_video = is_video;
    cache[best_slot].last_used = now;

    return &cache[best_slot];
}

void draw_thumbnail_card(int x, int y, int w, int h, const file_thumbnail_t *thumb, int selected) {
    if (!thumb || !thumb->pixels) return;

    // Card frame shadow
    fb_rect_alpha(x + 2, y + 3, w, h, 0x0A1018, 110);

    // Card background
    u32 bg_color = selected ? 0x243E60 : 0x182230;
    fb_round_rect(x, y, w, h, 4, bg_color);
    fb_round_rect_border(x, y, w, h, 4, selected ? 0x60A5FA : 0x3E526A);

    // Inner photo padding (2px)
    int px = x + (w - thumb->w) / 2;
    int py = y + (h - thumb->h) / 2;

    for (int row = 0; row < thumb->h; row++) {
        u32 *src = thumb->pixels + row * thumb->w;
        for (int col = 0; col < thumb->w; col++) {
            fb_putpixel(px + col, py + row, src[col]);
        }
    }

    // Border around photo
    outline(px - 1, py - 1, thumb->w + 2, thumb->h + 2, 0x223040);

    // Video badge
    if (thumb->is_video) {
        int vcx = x + w / 2;
        int vcy = y + h / 2;
        // Translucent dark circle in center
        fb_round_rect_alpha(vcx - 9, vcy - 9, 18, 18, 9, 0x0F172A, 190);
        fb_round_rect_border(vcx - 9, vcy - 9, 18, 18, 9, 0xE2E8F0);
        // Play triangle
        draw_label(vcx - 3, vcy - 6, ">", 0xFFFFFF);
    }
}
