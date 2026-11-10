with open('gui.c', 'r') as f:
    text = f.read()

search = """if (text[end] == '
') {"""
replace = """if (text[end] == '\\n') {"""
text = text.replace(search, replace)

with open('gui.c', 'w') as f:
    f.write(text)
