with open('gui.c', 'r') as f:
    text = f.read()

import re

search = r"#define WC_FILL\(X0, Y0, W, H, COL\) do \{ \n    int _x0=\(X0\),_y0=\(Y0\),_x1=\(X0\)\+\(W\),_y1=\(Y0\)\+\(H\); \n    if\(_x0<0\)_x0=0; if\(_y0<0\)_y0=0; \n    if\(_x1>w\)_x1=w;  if\(_y1>h\)_y1=h; \n    for\(int _r=_y0;_r<_y1;_r\+\+\) \{ \n        u32 \*_row=wallpaper_cache\+_r\*\((u32|unsigned int)\)w\+_x0; \n        for\(int _c=0;_c<\(_x1-_x0\);_c\+\+\) _row\[_c\]=\(COL\); \n    \} \n\} while\(0\)"

replace = """#define WC_FILL(X0, Y0, W, H, COL) do { \\
    int _x0=(X0),_y0=(Y0),_x1=(X0)+(W),_y1=(Y0)+(H); \\
    if(_x0<0)_x0=0; if(_y0<0)_y0=0; \\
    if(_x1>w)_x1=w;  if(_y1>h)_y1=h; \\
    for(int _r=_y0;_r<_y1;_r++) { \\
        u32 *_row=wallpaper_cache+_r*(u32)w+_x0; \\
        for(int _c=0;_c<(_x1-_x0);_c++) _row[_c]=(COL); \\
    } \\
} while(0)"""

text = re.sub(search, replace, text)

# Also fix the terminal_print lines!
text = text.replace('terminal_print("--- IDE Output ---', 'terminal_print("--- IDE Output ---\\n"); //')
text = text.replace('terminal_print("--- FirstLang Debugger ---', 'terminal_print("--- FirstLang Debugger ---\\n"); //')
text = text.replace('terminal_print("Type run or quit.', 'terminal_print("Type run or quit.\\n"); //')


with open('gui.c', 'w') as f:
    f.write(text)
