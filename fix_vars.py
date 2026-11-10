with open('gui.c', 'r') as f:
    text = f.read()

# find editor_text definition and inject ide_ variables right after it.
start = text.find('char editor_text[32768] = ')
end = text.find('int editor_length = ', start)

# just inject after editor_saved
saved_pos = text.find('int editor_saved = 1;')
if saved_pos != -1:
    inject = '''
char ide_text[32768] = "class init: function Main.\\n\\nfunction Main:\\n  set x to 1\\n  add button \\"Run Me\\" at (center, center)\\n  on click \\"Run Me\\":\\n    set x to x + 1\\n  end\\nend";
int ide_length = 138;
int ide_cursor = 138;
int ide_saved = 1;
int ide_scroll_y = 0;
int ide_scroll_x = 0;
int ide_select_anchor = -1;
int ide_select_active = -1;
char ide_filename[32] = "main.fl";
char ide_clipboard[512];
char ide_save_name[32] = "main.fl";
rect_t ide_text_area;
int ide_save_dialog_open = 0;
'''
    if 'char ide_text' not in text:
        text = text[:saved_pos+21] + inject + text[saved_pos+21:]

with open('gui.c', 'w') as f:
    f.write(text)
