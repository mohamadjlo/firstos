with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('\\\\n', '\n')

with open('gui.c', 'w') as f:
    f.write(text)
