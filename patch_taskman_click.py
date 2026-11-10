with open('kernel.c', 'r') as f:
    text = f.read()
text = text.replace('gui_draw();', 'gui_draw();\n    extern void handle_taskman_click(void*, int, int);\n    extern void* app_windows;\n    handle_taskman_click((char*)app_windows + 9 * 1024, 0, 0); // Fake click')
with open('kernel.c', 'w') as f:
    f.write(text)
