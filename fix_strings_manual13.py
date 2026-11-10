with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('extern int ide_saved;', '')

with open('gui.c', 'w') as f:
    f.write(text)
