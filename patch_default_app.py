with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('open_app(APP_TERMINAL);\n    open_app(APP_FILES);\n    open_app(APP_DISPLAY);', 'open_app(APP_TASKMAN);')

with open('gui.c', 'w') as f:
    f.write(text)
