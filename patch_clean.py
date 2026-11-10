with open('kernel.c', 'r') as f:
    text = f.read()

text = text.replace('if (gui_needs_redraw()) {\n            serial_write("[kernel] frame start\\n");', 'if (gui_needs_redraw()) {')
text = text.replace('gui_draw();\n            serial_write("[kernel] frame end\\n");', 'gui_draw();')

with open('kernel.c', 'w') as f:
    f.write(text)

with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('open_app(APP_TASKMAN);', 'open_app(APP_TERMINAL);\n    open_app(APP_FILES);\n    open_app(APP_DISPLAY);', 1)

with open('gui.c', 'w') as f:
    f.write(text)
