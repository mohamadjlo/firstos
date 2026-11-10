import sys
content = open('photo_viewer.c').read()

if 'int pv_error = 0;' not in content:
    content = content.replace('static u8 *pv_pixels = 0;', 'static u8 *pv_pixels = 0;\nstatic int pv_error = 0;')

# Replace pv_open
if 'void pv_open(const char *path) {' in content:
    idx = content.find('void pv_open(const char *path) {')
    end_idx = content.find('}', idx) + 1
    new_open = """void pv_open(const char *path) {
    pv_error = 0;
    pv_zoom = 1;
    pv_str_copy(pv_path, path, sizeof(pv_path));
    serial_write("[photo_viewer] opening: ");
    serial_write(path);
    serial_write("\\n");
    if (pv_pixels) { kfree(pv_pixels); pv_pixels = 0; }
    
    pv_pixels = image_load(path, &pv_img_w, &pv_img_h);
    if (!pv_pixels) {
        serial_write("[photo_viewer] image_load failed\\n");
        pv_error = 1;
    } else {
        serial_write("[photo_viewer] loaded OK\\n");
    }
}"""
    content = content[:idx] + new_open + content[end_idx:]

# Replace drawing logic
if 'if (pv_pixels) {' in content:
    idx = content.find('if (pv_pixels) {')
    end_idx = content.find('}', idx) + 1
    new_draw = """if (pv_error) {
        draw_label(ia_x + ia_w/2 - 100, ia_y + ia_h/2 - 10, "Corrupt or Unsupported Image", 0xD97C7C);
        draw_label(ia_x + ia_w/2 - 90, ia_y + ia_h/2 + 10, "Please try a standard format", 0x7A1F1F);
    } else if (pv_pixels) {
        pv_blit(ia_x, ia_y, ia_w, ia_h, pv_scroll_x, pv_scroll_y, pv_zoom);
    }"""
    content = content[:idx] + new_draw + content[end_idx:]

open('photo_viewer.c', 'w').write(content)
