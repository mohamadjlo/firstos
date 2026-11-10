import sys
content = open('photo_viewer.c').read()

idx = content.find('void photo_viewer_open(const char *path) {')
end_idx = content.find('}', idx) + 1

new_open = """void photo_viewer_open(const char *path) {
    pv_error = 0;
    pv_scroll_x = 0;
    pv_scroll_y = 0;
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

idx2 = content.find('if (pv_pixels) {\n        pv_blit(ia_x, ia_y, ia_w, ia_h, pv_scroll_x, pv_scroll_y, pv_zoom);\n    }')
end_idx2 = content.find('}', idx2) + 1

new_draw = """if (pv_error) {
        draw_label(ia_x + ia_w/2 - 100, ia_y + ia_h/2 - 10, "Corrupt or Unsupported Image", 0xD97C7C);
        draw_label(ia_x + ia_w/2 - 90, ia_y + ia_h/2 + 10, "Please try a standard format", 0x7A1F1F);
    } else if (pv_pixels) {
        pv_blit(ia_x, ia_y, ia_w, ia_h, pv_scroll_x, pv_scroll_y, pv_zoom);
    }"""

if idx2 != -1:
    content = content[:idx2] + new_draw + content[end_idx2:]

open('photo_viewer.c', 'w').write(content)
