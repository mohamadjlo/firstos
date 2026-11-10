with open('gui.c', 'r') as f:
    text = f.read()

# find literal newline between single quotes.
# Python regex: \'\n\'
import re
text = re.sub(r"'\n'", r"'\\n'", text)

with open('gui.c', 'w') as f:
    f.write(text)
