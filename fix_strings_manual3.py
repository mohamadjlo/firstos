with open('gui.c', 'r') as f:
    text = f.read()

search = """while (text[end] && text[end] != '
' && count < chars_per_line) {"""
replace = """while (text[end] && text[end] != '\\n' && count < chars_per_line) {"""
text = text.replace(search, replace)

search2 = """if (text[end] && text[end] != '
' && count >= chars_per_line && last_space >= start) {"""
replace2 = """if (text[end] && text[end] != '\\n' && count >= chars_per_line && last_space >= start) {"""
text = text.replace(search2, replace2)

with open('gui.c', 'w') as f:
    f.write(text)
