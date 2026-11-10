import sys
content = open('photo_viewer.c').read()

if 'void photo_viewer_open(const char *path) {' in content:
    idx = content.find('void photo_viewer_open(const char *path) {')
    end_idx = content.find('static void pv_blit')
    new_open = """void photo_viewer_open(const char *path) {
    pv_error = 0;
    pv_scroll_x = 0;
    pv_scroll_y = 0;
    pv_zoom = 1;
    pv_str_copy(pv_path, path, sizeof(pv_path));
    serial_write("[photo_viewer] opening: ");
    serial_write(path);
    serial_write("\\n");
    if (pv_pixels) { extern void kfree(void*); kfree(pv_pixels); pv_pixels = 0; }
    
    pv_pixels = image_load(path, &pv_img_w, &pv_img_h);
    if (!pv_pixels) {
        serial_write("[photo_viewer] image_load failed\\n");
        pv_error = 1;
    } else {
        serial_write("[photo_viewer] loaded OK\\n");
    }
}

"""
    content = content[:idx] + new_open + content[end_idx:]

open('photo_viewer.c', 'w').write(content)
