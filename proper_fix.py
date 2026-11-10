with open('gui.c', 'r') as f:
    text = f.read()

out = []
in_string = False
in_char = False
escape = False

for c in text:
    if not in_string and not in_char:
        if c == '"':
            in_string = True
            escape = False
        elif c == "'":
            in_char = True
            escape = False
        out.append(c)
    elif in_string:
        if escape:
            escape = False
            out.append(c)
        else:
            if c == '\\':
                escape = True
                out.append(c)
            elif c == '"':
                in_string = False
                out.append(c)
            elif c == '\n':
                out.append('\\\\n')
            else:
                out.append(c)
    elif in_char:
        if escape:
            escape = False
            out.append(c)
        else:
            if c == '\\':
                escape = True
                out.append(c)
            elif c == "'":
                in_char = False
                out.append(c)
            elif c == '\n':
                out.append('\\\\n')
            else:
                out.append(c)

with open('gui.c', 'w') as f:
    f.write(''.join(out))
