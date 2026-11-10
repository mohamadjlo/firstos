with open('gui.c', 'r') as f:
    text = f.read()

# fix the first line manually if possible, or just fix the newlines inside double quotes?
# Actually, the string 'function ui:\n' was replaced.
text = text.replace('int ide_saving_ui = 0;\n#include "gui.h"', 'int ide_saving_ui = 0;\\n#include "gui.h"')
# Wait, if sed inserted actual newlines, we can just replace newlines inside string literals.
