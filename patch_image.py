import sys
content = open('image.c').read()

patch = """u8 *image_load(const char *path, int *w, int *h) {
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

    char *buf = (char*)kmalloc(512 * 1024); // Support up to 512KB for JPEGs/PNGs
    if (!buf) return 0;
    int n = fs_read_at(path, buf, 512 * 1024);
    if (n <= 0) { kfree(buf); return 0; }
    
    int channels;
    u8 *data = 0;
    
    // Check if it's an .ico file
    if (len > 4 && path[len-4] == '.' && path[len-3] == 'i' && path[len-2] == 'c' && path[len-1] == 'o') {
        data = decode_ico_format((u8*)buf, n, w, h);
    } else {
        data = stbi_load_from_memory((u8*)buf, n, w, h, &channels, 4);
    }
    
    kfree(buf);
    
    if (!data) return 0;
    u8 *pix = (u8*)kmalloc(*w * *h * 4);
    if (!pix) { kfree(data); return 0; }
    extern void memcpy(void*, const void*, int);
    memcpy(pix, data, *w * *h * 4);
    kfree(data);
    return pix;
}"""

idx = content.find('u8 *image_load(const char *path, int *w, int *h) {')
end = content.find('void image_free(u8 *data) {')
if idx != -1 and end != -1:
    content = content[:idx] + patch + '\n\n' + content[end:]
    open('image.c', 'w').write(content)
