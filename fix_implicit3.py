with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('int editor_mouse_selecting = 0;', 'int editor_mouse_selecting = 0;\\nint ide_mouse_selecting = 0;')

with open('gui.c', 'w') as f:
    f.write(text.replace('\\n', '\n'))
