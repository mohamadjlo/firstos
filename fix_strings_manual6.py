with open('gui.c', 'r') as f:
    text = f.read()

import re

text = re.sub(r'gui_fl_cat\(imp, "function ui:\n    import \\"", 128\);\\', 'gui_fl_cat(imp, "function ui:\\\\n    import \\\\"", 128);', text)
text = re.sub(r'gui_fl_cat\(imp, "\\"\nend\n", 128\);', 'gui_fl_cat(imp, "\\\\"\\\\nend\\\\n", 128);', text)

with open('gui.c', 'w') as f:
    f.write(text)
