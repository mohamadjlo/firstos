import re
with open('gui.c', 'r') as f:
    text = f.read()

# Fix the newline literal in ide_insert_char
text = text.replace("ide_insert_char('\n');", "ide_insert_char('\\n');")

# Add missing externs for ide.c functions
externs = """
extern void ide_insert_char(char c);
extern void ide_backspace(void);
extern void ide_mark_saved(void);
extern void ide_copy_selection(void);
extern void ide_cut_selection(void);
extern void ide_paste_clipboard(void);
extern void ide_select_all(void);
extern void ide_undo(void);
extern int ide_position_from_point(int mx, int my);
"""

# inject at the top after includes
text = text.replace('#include "firstlang.h"', '#include "firstlang.h"\n' + externs)

with open('gui.c', 'w') as f:
    f.write(text)
