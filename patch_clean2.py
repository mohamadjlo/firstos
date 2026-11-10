with open('gui.c', 'r') as f:
    text = f.read()

bad = '''                        else if (entry->name[0] == 'T' && entry->name[1] == 'a' && entry->name[2] == 's') open_app(APP_TERMINAL);
    open_app(APP_FILES);
    open_app(APP_DISPLAY);
                        else if (entry->name[0] == 'T') open_app(APP_TEXT_EDIT);'''
good = '''                        else if (entry->name[0] == 'T' && entry->name[1] == 'a' && entry->name[2] == 's') open_app(APP_TASKMAN);
                        else if (entry->name[0] == 'T') open_app(APP_TEXT_EDIT);'''
text = text.replace(bad, good)

# Also check where open_app(APP_TASKMAN) actually was in main init
bad2 = '    dock_terminal = (rect_t){-400, -400, 0, 0, 0};\n    dock_files = (rect_t){-400, -400, 0, 0, 0};\n    open_app(APP_TASKMAN);'
good2 = '    dock_terminal = (rect_t){-400, -400, 0, 0, 0};\n    dock_files = (rect_t){-400, -400, 0, 0, 0};\n    open_app(APP_TERMINAL);\n    open_app(APP_FILES);\n    open_app(APP_DISPLAY);'
text = text.replace(bad2, good2)

with open('gui.c', 'w') as f:
    f.write(text)
