import re
with open('gui.c', 'r') as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    # If line has a literal newline inside a string (not really possible because python readlines splits by newline)
    # The issue is that the C file literally has unescaped newlines in strings, so they span multiple lines.
    pass

# actually, let's just look at the compile error!
