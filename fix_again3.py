import re
with open('gui.c', 'r') as f:
    text = f.read()

text = re.sub(r"'\n'", r"'\\n'", text)
text = re.sub(r'\n"\);', r'\\n");', text)
text = re.sub(r'\n",', r'\\n",', text)

with open('gui.c', 'w') as f:
    f.write(text)
