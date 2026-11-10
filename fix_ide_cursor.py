with open('ide.c', 'r') as f:
    text = f.read()

search = r'''        extern volatile unsigned int time_ticks;
        if (i == ide_cursor && (time_ticks % 100) < 50 && draw_line >= 0 && draw_line < ide_visible_lines() && draw_col >= 0 && draw_col < visible_cols) {'''
replace = r'''        if (i == ide_cursor && draw_line >= 0 && draw_line < ide_visible_lines() && draw_col >= 0 && draw_col < visible_cols) {'''

text = text.replace(search, replace)
with open('ide.c', 'w') as f:
    f.write(text)

