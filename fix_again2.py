import re
with open('gui.c', 'r') as f:
    text = f.read()

text = re.sub(r"ide_insert_char\('\n'\);", r"ide_insert_char('\\n');", text)
text = re.sub(r"text_edit_insert_char\('\n'\);", r"text_edit_insert_char('\\n');", text)

with open('gui.c', 'w') as f:
    f.write(text)
