with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace("ide_insert_char('", "ide_insert_char('\\n'") # Wait, no!
text = text.replace("ide_insert_char('\\n');", "ide_insert_char('\\\\n');")
text = text.replace("text_edit_insert_char('\\n');", "text_edit_insert_char('\\\\n');")

search1 = """ide_insert_char('
');"""
replace1 = """ide_insert_char('\\n');"""
text = text.replace(search1, replace1)

search2 = """text_edit_insert_char('
');"""
replace2 = """text_edit_insert_char('\\n');"""
text = text.replace(search2, replace2)

with open('gui.c', 'w') as f:
    f.write(text)
