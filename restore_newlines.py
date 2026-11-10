with open('gui.c', 'r') as f:
    text = f.read()

# Revert my fix_c_strings.py: replace all '\\n' with actual newline
text = text.replace('\\\\n', '\\n')

with open('gui.c', 'w') as f:
    f.write(text)
