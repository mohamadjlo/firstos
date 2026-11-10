with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('int ide_saving_ui = 0;\\n#include "gui.h"', 'int ide_saving_ui = 0;\\n#include "gui.h"')
# If it is literal backslash n, it's '\\n'
text = text.replace('int ide_saving_ui = 0;\\\\n#include "gui.h"', 'int ide_saving_ui = 0;\\n#include "gui.h"')
with open('gui.c', 'w') as f:
    f.write(text)

