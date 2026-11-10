with open('gui.c', 'r') as f:
    text = f.read()

out = []
in_string = False
escape = False
for i, c in enumerate(text):
    if c == '"' and not escape:
        in_string = not in_string
        out.append(c)
    elif c == '\\':
        escape = not escape
        out.append(c)
    elif c == '\n':
        escape = False
        if in_string:
            out.append('\\n')
        else:
            out.append(c)
    else:
        escape = False
        out.append(c)

with open('gui.c', 'w') as f:
    f.write(''.join(out))
