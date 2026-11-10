with open('ide.c', 'r') as f:
    text = f.read()

externs = """
extern int ide_select_anchor;
extern int ide_select_active;
extern int ide_scroll_x;
extern int ide_scroll_y;
extern int ide_cursor;
extern int ide_saved;
extern int ide_length;
extern char ide_text[32768];
extern char ide_filename[32];
extern char ide_save_name[32];
extern int ide_save_dialog_open;
extern char ide_clipboard[512];
extern char ide_undo[32768];
extern rect_t ide_text_area;
"""
# remove include text_edit.h just in case? No, it needs rect_t. Wait, gui.h has rect_t.
# I will just inject these externs at the top.
text = text.replace('#include "text_edit.h"', '#include "text_edit.h"\\n' + externs)

with open('ide.c', 'w') as f:
    f.write(text.replace('\\n', '\n'))
