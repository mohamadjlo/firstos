with open('gui.c', 'r') as f:
    code = f.read()

# 1. Add APP_IDE to app_kind_t
code = code.replace(
    'APP_CALC, APP_TERMINAL, APP_TEXT_EDIT, APP_PHOTO_EDIT',
    'APP_CALC, APP_TERMINAL, APP_TEXT_EDIT, APP_PHOTO_EDIT, APP_IDE'
)

# 2. Add app name
code = code.replace(
    'if (k == APP_TEXT_EDIT) return "FirstOS Text Edit";',
    'if (k == APP_TEXT_EDIT) return "FirstOS Text Edit";\n    if (k == APP_IDE) return "FirstLang IDE";'
)

# 3. Add global IDE variables in gui.c
code = code.replace(
    'char editor_text[32768]',
    'char editor_text[32768] = "Welcome to FirstOS Text Edit.\\n\\nType your notes here.\\nUse Enter for a new line and Backspace to delete.\\nUse Save to keep this document in the current session.";\n'
    'char ide_text[32768] = "class init: function Main.\\n\\nfunction Main:\\n  set x to 1\\n  add button \\"Run Me\\" at (center, center)\\n  on click \\"Run Me\\":\\n    set x to x + 1\\n  end\\nend";\n'
    'int ide_length = 138;\n'
    'int ide_cursor = 138;\n'
    'int ide_saved = 1;\n'
    'int ide_scroll_y = 0;\n'
    'int ide_scroll_x = 0;\n'
    'int ide_select_anchor = -1;\n'
    'int ide_select_active = -1;\n'
    'char ide_filename[32] = "main.fl";\n'
    'char ide_clipboard[512];\n'
    'char ide_save_name[32] = "main.fl";\n'
    'rect_t ide_text_area;\n'
    'int ide_save_dialog_open = 0;\n'
    '//'
)

# 4. Modify draw_app_window to draw APP_IDE
# We duplicate the TEXT_EDIT drawing block for IDE, adding syntax highlighting!
# Wait, let's just make APP_IDE share the same drawing logic but with syntax highlighting!
# Find the APP_TEXT_EDIT block:
old_draw = '''    } else if (app->kind == APP_TEXT_EDIT) {'''
new_draw = '''    } else if (app->kind == APP_TEXT_EDIT || app->kind == APP_IDE) {
        int is_ide = (app->kind == APP_IDE);
        char *txt = is_ide ? ide_text : editor_text;
        int tlen = is_ide ? ide_length : editor_length;
        int tcur = is_ide ? ide_cursor : editor_cursor;
        int tsav = is_ide ? ide_saved : editor_saved;
        rect_t *tarea = is_ide ? &ide_text_area : &editor_text_area;
        int tsel_start = is_ide ? (ide_select_anchor < ide_select_active ? ide_select_anchor : ide_select_active) : (editor_select_anchor < editor_select_active ? editor_select_anchor : editor_select_active);
        int tsel_end = is_ide ? (ide_select_anchor > ide_select_active ? ide_select_anchor : ide_select_active) : (editor_select_anchor > editor_select_active ? editor_select_anchor : editor_select_active);
        if(!is_ide) { tsel_start = text_edit_has_selection() ? text_edit_selection_start() : -1; tsel_end = text_edit_has_selection() ? text_edit_selection_end() : -1; }
        else { tsel_start = (ide_select_anchor >= 0 && ide_select_active >= 0 && ide_select_anchor != ide_select_active) ? tsel_start : -1; tsel_end = (ide_select_anchor >= 0 && ide_select_active >= 0 && ide_select_anchor != ide_select_active) ? tsel_end : -1; }
'''

# We need to replace all `editor_text`, `editor_length` with `txt`, `tlen` inside the APP_TEXT_EDIT block!
# But there are many occurrences!
# Let's extract the block and replace.
start_idx = code.find('} else if (app->kind == APP_TEXT_EDIT) {')
end_idx = code.find('} else if (app->kind == APP_PHOTO_EDIT) {')
if start_idx == -1 or end_idx == -1: print("Could not find APP_TEXT_EDIT block!"); exit(1)

block = code[start_idx:end_idx]
block = block.replace('} else if (app->kind == APP_TEXT_EDIT) {', new_draw)
block = block.replace('editor_text_area', '(*tarea)')
block = block.replace('editor_text', 'txt')
block = block.replace('editor_length', 'tlen')
block = block.replace('editor_cursor', 'tcur')
block = block.replace('editor_saved', 'tsav')
block = block.replace('text_edit_has_selection() ? text_edit_selection_start() : -1', 'tsel_start')
block = block.replace('text_edit_has_selection() ? text_edit_selection_end() : -1', 'tsel_end')
block = block.replace('draw_label(ex + 12, ey + 10, "notes.txt", 0x4B321E);', 'draw_label(ex + 12, ey + 10, is_ide ? "main.fl" : "notes.txt", 0x4B321E);')

# Now for syntax highlighting:
# Replace the drawing of `one`
syntax_old = '''            for (j = line_start; j < line_end; j++) {
                char one[2];
                one[0] = txt[j]; one[1] = 0;
                draw_label(text_x + (j - line_start) * 6, line_y, one, 0x3A2A1C);
            }'''

syntax_new = '''            for (j = line_start; j < line_end; j++) {
                char one[2];
                one[0] = txt[j]; one[1] = 0;
                u32 color = 0x3A2A1C;
                if(is_ide) {
                    /* basic coloring */
                    if(j>0 && j<line_end) {
                        /* keyword check by matching from j */
                        /* too slow to do per char, but simple enough */
                    }
                    /* simple coloring based on char */
                    if(one[0]=='"' || one[0]=='\\'') color = 0x1F8A4F;
                    else if(one[0]>='0' && one[0]<='9') color = 0x8A1F1F;
                    else if(one[0]==':' || one[0]=='(' || one[0]==')' || one[0]=='[' || one[0]==']') color = 0x805020;
                }
                draw_label(text_x + (j - line_start) * 6, line_y, one, color);
            }
            if(is_ide) {
                draw_button((rect_t){ex + ew - 262, ey + 5, 74, 18, 0}, 0x3B82F6, 0x60A5FA, "RUN", 0xFFFFFF);
            }'''
block = block.replace(syntax_old, syntax_new)

code = code[:start_idx] + block + code[end_idx:]

with open('gui.c', 'w') as f:
    f.write(code)
print("gui.c patched with IDE support")
