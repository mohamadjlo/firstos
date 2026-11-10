with open('kernel.c', 'r') as f:
    text = f.read()

text = text.replace('if (gui_needs_redraw()) {', 'if (gui_needs_redraw()) {\n            serial_write("[kernel] frame start\\n");')
text = text.replace('gui_draw();', 'gui_draw();\n            serial_write("[kernel] frame end\\n");')

with open('kernel.c', 'w') as f:
    f.write(text)
