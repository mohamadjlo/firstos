with open('gui.c', 'r') as f:
    text = f.read()

search1 = """char editor_text[32768] = "Welcome to FirstOS Text Edit.

Type your notes here.
Use Enter for a new line and Backspace to delete.
Use Save to keep this document in the current session.";"""
replace1 = """char editor_text[32768] = "Welcome to FirstOS Text Edit.\\n\\nType your notes here.\\nUse Enter for a new line and Backspace to delete.\\nUse Save to keep this document in the current session.";"""
text = text.replace(search1, replace1)

with open('gui.c', 'w') as f:
    f.write(text)
