with open('gui.c', 'r') as f:
    text = f.read()

# Fix the first line
text = text.replace('int ide_saving_ui = 0;\n#include "gui.h"', 'int ide_saving_ui = 0;\\n#include "gui.h"')
# Wait, it was actually changed to actual newline. So it is currently:
# int ide_saving_ui = 0;
# #include "gui.h"
# Which is VALID C!
# But what about the strings?
search_broken1 = """                gui_fl_cat(imp, "function ui:
    import \\"", 128);"""
replace_broken1 = """                gui_fl_cat(imp, "function ui:\\n    import \\"", 128);"""
text = text.replace(search_broken1, replace_broken1)

search_broken2 = """                gui_fl_cat(imp, "\\"
end
", 128);"""
replace_broken2 = """                gui_fl_cat(imp, "\\"\\nend\\n", 128);"""
text = text.replace(search_broken2, replace_broken2)

with open('gui.c', 'w') as f:
    f.write(text)

