with open('gui.c', 'r') as f:
    text = f.read()

import re

# We need to find the broken char arrays and fix them.
# The broken string starts at `char editor_text[32768] = "Welcome` and ends at `session.";`
ed_start = text.find('char editor_text[32768]')
ed_end = text.find('session.";\n', ed_start)
if ed_start != -1 and ed_end != -1:
    text = text[:ed_start] + 'char editor_text[32768] = "Welcome to FirstOS Text Edit.\\n\\nType your notes here.\\nUse Enter for a new line and Backspace to delete.\\nUse Save to keep this document in the current session.";\n' + text[ed_end + 11:]

id_start = text.find('char ide_text[32768]')
id_end = text.find('end";\n', id_start)
if id_start != -1 and id_end != -1:
    text = text[:id_start] + 'char ide_text[32768] = "class init: function Main.\\n\\nfunction Main:\\n  set x to 1\\n  add button \\"Run Me\\" at (center, center)\\n  on click \\"Run Me\\":\\n    set x to x + 1\\n  end\\nend";\n' + text[id_end + 6:]

with open('gui.c', 'w') as f:
    f.write(text)
