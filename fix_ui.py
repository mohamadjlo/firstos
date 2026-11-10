with open('gui.c', 'r') as f:
    text = f.read()

text = text.replace('char *txt = is_ide ? ide_text : txt;', 'char *txt = is_ide ? ide_text : editor_text;')
text = text.replace('int tlen = is_ide ? ide_length : tlen;', 'int tlen = is_ide ? ide_length : editor_length;')
text = text.replace('int tcur = is_ide ? ide_cursor : tcur;', 'int tcur = is_ide ? ide_cursor : editor_cursor;')
text = text.replace('int tsav = is_ide ? ide_saved : tsav;', 'int tsav = is_ide ? ide_saved : editor_saved;')
text = text.replace('rect_t *tarea = is_ide ? &ide_text_area : &(*tarea);', 'rect_t *tarea = is_ide ? &ide_text_area : &editor_text_area;')
text = text.replace('if(!is_ide) { tsel_start = tsel_start; tsel_end = tsel_end; }', 'if(!is_ide) { tsel_start = text_edit_has_selection() ? text_edit_selection_start() : -1; tsel_end = text_edit_has_selection() ? text_edit_selection_end() : -1; }')

with open('gui.c', 'w') as f:
    f.write(text)
