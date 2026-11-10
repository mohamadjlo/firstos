with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('dock_terminal = (rect_t){-400, -400, 0, 0, 0};\n    dock_files = (rect_t){-400, -400, 0, 0, 0};', 'dock_terminal = (rect_t){-400, -400, 0, 0, 0};\n    dock_files = (rect_t){-400, -400, 0, 0, 0};\n    open_app(12);')

with open('gui.c', 'w') as f:
    f.write(text)
