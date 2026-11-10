with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('terminal_print("\\n");', 'terminal_print("\\\\n");') # wait!
# I need to match the literal newline in the string:
search3 = '''                        terminal_print("
");'''
replace3 = '''                        terminal_print("\\n");'''
text = text.replace(search3, replace3)

search4 = '''                terminal_print("--- IDE Output ---
");'''
replace4 = '''                terminal_print("--- IDE Output ---\\n");'''
text = text.replace(search4, replace4)

search5 = '''                terminal_print("
");'''
replace5 = '''                terminal_print("\\n");'''
text = text.replace(search5, replace5)

with open('gui.c', 'w') as f:
    f.write(text)

