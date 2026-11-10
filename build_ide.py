with open('ide.c', 'r') as f:
    code = f.read()

# Replace text_edit prefix with ide
code = code.replace('text_edit_', 'ide_')
code = code.replace('TEXT_EDIT', 'IDE')

# In ide.c, we have a toolbar.
# Let's add a RUN button to the toolbar!
# Search for the toolbar drawing logic.
# In text_edit.c it draws buttons like "New", "Open", "Save".
# We will add "Run".
code = code.replace(
    'fl_draw_button(wx+140, wy+32, 60, "Save");',
    'fl_draw_button(wx+140, wy+32, 60, "Save");\n    fl_draw_button(wx+210, wy+32, 60, "Run");'
)

# And in ide_mouse_click, handle the Run button:
mouse_click_old = '''    if (my >= wy+32 && my <= wy+56) {
        if (mx >= wx+10 && mx <= wx+70) {
            ide_new_file(); return 1;
        }
        if (mx >= wx+75 && mx <= wx+135) {
            ide_open_dialog = 1; return 1;
        }
        if (mx >= wx+140 && mx <= wx+200) {
            ide_save_file(); return 1;
        }
    }'''

mouse_click_new = '''    if (my >= wy+32 && my <= wy+56) {
        if (mx >= wx+10 && mx <= wx+70) {
            ide_new_file(); return 1;
        }
        if (mx >= wx+75 && mx <= wx+135) {
            ide_open_dialog = 1; return 1;
        }
        if (mx >= wx+140 && mx <= wx+200) {
            ide_save_file(); return 1;
        }
        if (mx >= wx+210 && mx <= wx+270) {
            ide_save_file();
            ide_run_script();
            return 1;
        }
    }'''

code = code.replace(mouse_click_old, mouse_click_new)

# Add ide_run_script function
run_func = '''
extern int firstlang_run(const char *source, char *output_buf, int output_sz);
static void ide_run_script(void) {
    if (!ide_current_path[0]) {
        fl_cpy(ide_status_msg, "Save file first before running!", 64);
        return;
    }
    char out[1024]; out[0]=0;
    firstlang_run(ide_buffer, out, 1024);
    fl_cpy(ide_status_msg, "Script executed!", 64);
}
'''
code = code.replace('static void ide_save_file(void)', run_func + '\\nstatic void ide_save_file(void)')

# Let's add simple syntax highlighting to ide_draw!
# In text_edit.c, drawing text:
# `font_text(bx, by, c, 0x111111);` or something.
# Wait, drawing text is done char by char or line by line?
# Let's see how it's drawn. We will write the file first and then check.
with open('ide.c', 'w') as f:
    f.write(code)
print("ide.c created")
