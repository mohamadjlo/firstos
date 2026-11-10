#include "image.h"
#include "tiff.h"
#include "fs.h"
#include "heap.h"
#include "gui.h"
#include "serial.h"
#include <string.h>



int abs(int j) {
    return j < 0 ? -j : j;
}

double pow(double base, double exponent) {
    return 1.0; // Dummy
}

#define STBI_ASSERT(x)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_MALLOC kmalloc
#define STBI_FREE kfree
#define STBI_REALLOC krealloc
#include "stb_image.h"
u8* decode_ico_format(u8* buffer, u32 size, int* w, int* h);



u8 *image_load(const char *path, int *w, int *h) {
    int len = 0;
    while(path[len]) len++;
    
    // RAW Wallpaper format (pre-rasterized to 1024x768x4 by build_fs.py)
    if (len > 4 && path[len-4] == '.' && path[len-3] == 'r' && path[len-2] == 'a' && path[len-1] == 'w') {
        int sz = 1024 * 768 * 4;
        u8 *pix = (u8*)kmalloc(sz);
        if (!pix) return 0;
        int n = fs_read_at(path, (char*)pix, sz);
        if (n <= 0) { kfree(pix); return 0; }
        *w = 1024;
        *h = 768;
        return pix;
    }

        int file_size = 0;
    int fs_total = fs_count();
    for (int i = 0; i < fs_total; i++) {
        const fs_entry_t *e = fs_entry_at(i);
        if (e && e->path[0]) {
            int p1 = 0, p2 = 0;
            while(e->path[p1] && path[p2] && e->path[p1] == path[p2]) { p1++; p2++; }
            if (e->path[p1] == 0 && path[p2] == 0) {
                file_size = e->size;
                break;
            }
        }
    }
    
    if (file_size <= 0) file_size = 5 * 1024 * 1024; // Fallback to 5MB
    
    char *buf = (char*)kmalloc(file_size + 1024);
    if (!buf) return 0;
    int n = fs_read_at(path, buf, file_size + 1024);

    
    int channels;
    u8 *data = 0;
    
    // Check if it's an .ico file
    if (len > 4 && path[len-4] == '.' && path[len-3] == 'i' && path[len-2] == 'c' && path[len-1] == 'o') {
        data = decode_ico_format((u8*)buf, n, w, h);
    } else if (tiff_check_magic((const u8*)buf, n) ||
               (len > 4 && path[len-4] == '.' && (path[len-3] == 't' || path[len-3] == 'T') && (path[len-2] == 'i' || path[len-2] == 'I') && (path[len-1] == 'f' || path[len-1] == 'F')) ||
               (len > 5 && path[len-5] == '.' && (path[len-4] == 't' || path[len-4] == 'T') && (path[len-3] == 'i' || path[len-3] == 'I') && (path[len-2] == 'f' || path[len-2] == 'F') && (path[len-1] == 'f' || path[len-1] == 'F'))) {
        data = decode_tiff_format((const u8*)buf, n, w, h);
    } else {
        data = stbi_load_from_memory((u8*)buf, n, w, h, &channels, 4);
    }
    
    kfree(buf);
    return data;
}

u8 *image_load_from_memory(const u8 *buf, int len, int *w, int *h) {
    if (!buf || len <= 0) return 0;
    int channels;
    u8 *data = 0;
    if (tiff_check_magic(buf, len)) {
        data = decode_tiff_format(buf, len, w, h);
    } else if (len >= 4 && buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01 && buf[3] == 0x00) {
        data = decode_ico_format((u8*)buf, (u32)len, w, h);
    } else {
        data = stbi_load_from_memory((u8*)buf, len, w, h, &channels, 4);
    }
    return data;
}

void image_free(u8 *data) {
    if (data) kfree(data);
}

const char *image_detect_format_name(const char *path, const u8 *buf, unsigned int size) {
    if (buf && size >= 4) {
        if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) return "JPEG";
        if (buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47) return "PNG";
        if (buf[0] == 'B' && buf[1] == 'M') return "BMP";
        if (buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F') return "GIF";
        if ((buf[0] == 'I' && buf[1] == 'I' && buf[2] == 0x2A) ||
            (buf[0] == 'M' && buf[1] == 'M' && buf[3] == 0x2A)) return "TIFF";
        if (buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01 && buf[3] == 0x00) return "ICO";
    }
    if (path) {
        int len = 0;
        while (path[len]) len++;
        if (len > 4 && path[len-4] == '.' && path[len-3] == 'r' && path[len-2] == 'a' && path[len-1] == 'w') return "RAW";
        if (len > 4 && path[len-4] == '.' && (path[len-3] == 'j' || path[len-3] == 'J') && (path[len-2] == 'p' || path[len-2] == 'P') && (path[len-1] == 'g' || path[len-1] == 'G')) return "JPEG";
        if (len > 5 && path[len-5] == '.' && (path[len-4] == 'j' || path[len-4] == 'J') && (path[len-3] == 'p' || path[len-3] == 'P') && (path[len-2] == 'e' || path[len-2] == 'E') && (path[len-1] == 'g' || path[len-1] == 'G')) return "JPEG";
        if (len > 4 && path[len-4] == '.' && (path[len-3] == 'p' || path[len-3] == 'P') && (path[len-2] == 'n' || path[len-2] == 'N') && (path[len-1] == 'g' || path[len-1] == 'G')) return "PNG";
        if (len > 4 && path[len-4] == '.' && (path[len-3] == 'b' || path[len-3] == 'B') && (path[len-2] == 'm' || path[len-2] == 'M') && (path[len-1] == 'p' || path[len-1] == 'P')) return "BMP";
        if (len > 4 && path[len-4] == '.' && (path[len-3] == 'g' || path[len-3] == 'G') && (path[len-2] == 'i' || path[len-2] == 'I') && (path[len-1] == 'f' || path[len-1] == 'F')) return "GIF";
        if (len > 4 && path[len-4] == '.' && (path[len-3] == 't' || path[len-3] == 'T') && (path[len-2] == 'i' || path[len-2] == 'I') && (path[len-1] == 'f' || path[len-1] == 'F')) return "TIFF";
        if (len > 5 && path[len-5] == '.' && (path[len-4] == 't' || path[len-4] == 'T') && (path[len-3] == 'i' || path[len-3] == 'I') && (path[len-2] == 'f' || path[len-2] == 'F') && (path[len-1] == 'f' || path[len-1] == 'F')) return "TIFF";
        if (len > 4 && path[len-4] == '.' && path[len-3] == 'i' && path[len-2] == 'c' && path[len-1] == 'o') return "ICO";
    }
    return "IMAGE";
}


// ---------------------------------------------------------
// Phase 2: Vector & .ICO Subsystem Boilerplate
// ---------------------------------------------------------

/*
   To the USER: When implementing the SVG rasterizer, download 'nanosvg.h'
   and include it here. Ensure the following files exist in /usr/share/firstos/icons:
   - sys-power.svg, folder.svg, file-generic.svg, app-terminal.svg, app-settings.svg
   - favicon.ico
*/

u8* decode_ico_format(u8* buffer, u32 size, int* w, int* h) {
    if (size < 22) return 0; // Too small
    
    // Standard ICO Header (6 bytes)
    // u16 reserved = *(u16*)(buffer + 0);
    u16 type = *(u16*)(buffer + 2); // 1 = ICO
    if (type != 1) return 0;
    
    // Directory Entry (16 bytes per image)
    // We just take the first icon in the directory for now
    u32 image_offset = *(u32*)(buffer + 18);
    
    if (image_offset >= size) return 0;
    
    // Most modern .ico files embed a standard PNG or BMP payload at the image_offset.
    // We can safely hand this internal payload off to stb_image!
    return stbi_load_from_memory(buffer + image_offset, size - image_offset, w, h, NULL, 4);
}

u8* decode_svg_format(const char* svg_string, int target_w, int target_h) {
    // NSVGimage* image = nsvgParse(svg_string, "px", 96);
    // NSVGrasterizer* rast = nsvgCreateRasterizer();
    // Allocate buffer and rasterize using nsvgRasterize()
    // return buffer;
    (void)svg_string; (void)target_w; (void)target_h;
    return 0; // Mocked until nanosvg.h is available
}

