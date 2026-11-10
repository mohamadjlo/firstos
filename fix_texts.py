with open('gui.c', 'r') as f:
    text = f.read()

import re

# We need to find `char editor_text[32768] = "Welcome...` and replace it entirely!
# It spans lines 447 to 451.
# Let's just find `char editor_text` and `char ide_text` definitions and replace them.

ed_start = text.find('char editor_text[32768]')
ed_end = text.find('int editor_length = ', ed_start)
if ed_start != -1 and ed_end != -1:
    text = text[:ed_start] + 'char editor_text[32768] = "Welcome to FirstOS Text Edit.\\n\\nType your notes here.\\nUse Enter for a new line and Backspace to delete.\\nUse Save to keep this document in the current session.";\n' + text[ed_end:]

id_start = text.find('char ide_text[32768]')
id_end = text.find('int ide_length = ', id_start)
if id_start != -1 and id_end != -1:
    text = text[:id_start] + 'char ide_text[32768] = "class init: function Main.\\n\\nfunction Main:\\n  set x to 1\\n  add button \\"Run Me\\" at (center, center)\\n  on click \\"Run Me\\":\\n    set x to x + 1\\n  end\\nend";\n' + text[id_end:]

# also fix line 4867 and 6384 which had literal \n in python patch.
# Wait, sed 's/\\n/\n/' already replaced them with actual newlines!
# So line 4867 is now properly:
# else if (...) { ... }
#     else if (...) { ... }
# And 6384 is properly split into two lines!

with open('gui.c', 'w') as f:
    f.write(text)
