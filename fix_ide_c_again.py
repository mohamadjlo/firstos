import re
with open('ide.c', 'r') as f:
    text = f.read()

# Fix the broken character literals
text = re.sub(r"'\n'", r"'\\n'", text)

# There is also a broken string literal at line 525:
# "Opened file in Text Edit.
#
# This is a sample loaded from the App Menu.
# Use Save to store your changes.";
# Let's just fix it manually.
sample_start = text.find('"Opened file in Text Edit.\\n')
if sample_start == -1:
    # it is broken
    broken_start = text.find('"Opened file in Text Edit.')
    broken_end = text.find('changes.";', broken_start)
    if broken_start != -1 and broken_end != -1:
        text = text[:broken_start] + '"Opened file in Text Edit.\\n\\nThis is a sample loaded from the App Menu.\\nUse Save to store your changes.";' + text[broken_end + 10:]

# Also fix `ide_undo` redeclaration:
# In ide.c:
# extern char ide_undo[32768];
# void ide_undo(void) {
text = text.replace('extern char ide_undo[32768];', 'extern char ide_undo_text[32768];')
text = text.replace('ide_undo[', 'ide_undo_text[')

with open('ide.c', 'w') as f:
    f.write(text)
