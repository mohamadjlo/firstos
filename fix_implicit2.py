with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace("text_edit_insert_char('\n');", "text_edit_insert_char('\\n');")

with open('gui.c', 'w') as f:
    f.write(text)
