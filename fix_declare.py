with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('static int editor_mouse_selecting = 0;', 'static int editor_mouse_selecting = 0;\nstatic int ide_mouse_selecting = 0;')

with open('gui.c', 'w') as f:
    f.write(text)
