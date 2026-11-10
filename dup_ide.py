import re

with open('gui.c', 'r') as f:
    text = f.read()

# For keyboard shortcuts:
shortcuts = [
    ('if (app && app->kind == APP_TEXT_EDIT) { text_edit_mark_saved(); needs_redraw = 1; }', 'if (app && app->kind == APP_IDE) { ide_mark_saved(); needs_redraw = 1; }'),
    ('if (app && app->kind == APP_TEXT_EDIT) { text_edit_copy_selection(); needs_redraw = 1; }', 'if (app && app->kind == APP_IDE) { ide_copy_selection(); needs_redraw = 1; }'),
    ('if (app && app->kind == APP_TEXT_EDIT) { text_edit_cut_selection(); needs_redraw = 1; }', 'if (app && app->kind == APP_IDE) { ide_cut_selection(); needs_redraw = 1; }'),
    ('if (app && app->kind == APP_TEXT_EDIT) { text_edit_paste_clipboard(); needs_redraw = 1; }', 'if (app && app->kind == APP_IDE) { ide_paste_clipboard(); needs_redraw = 1; }'),
    ('if (app && app->kind == APP_TEXT_EDIT) { text_edit_select_all(); needs_redraw = 1; }', 'if (app && app->kind == APP_IDE) { ide_select_all(); needs_redraw = 1; }')
]
for src, dst in shortcuts:
    text = text.replace(src, src + '\n        ' + dst)

# For keyboard typing:
# app_window_t *editor = app_slot(APP_TEXT_EDIT);
# We can just change the typing logic:
# Find: `if (foc == APP_TEXT_EDIT - 1) {`
# And duplicate it or add `APP_IDE`
# Let's check gui.c for `foc == APP_TEXT_EDIT - 1`
