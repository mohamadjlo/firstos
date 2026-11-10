with open('gui.c', 'r') as f:
    text = f.read()

search = """if (text[i] == '
') {"""
replace = """if (text[i] == '\\n') {"""
text = text.replace(search, replace)

search2 = """while (line_end < tlen && txt[line_end] != '
') line_end++;"""
replace2 = """while (line_end < tlen && txt[line_end] != '\\n') line_end++;"""
text = text.replace(search2, replace2)

with open('gui.c', 'w') as f:
    f.write(text)
