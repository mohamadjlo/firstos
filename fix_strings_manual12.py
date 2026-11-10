with open('gui.c', 'r') as f:
    text = f.read()

import re

search = r"ide_insert_char\('\\n'\n'\);"
replace = r"ide_insert_char('\\n');"
text = re.sub(search, replace, text)

with open('gui.c', 'w') as f:
    f.write(text)
