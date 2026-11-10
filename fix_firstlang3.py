with open('firstlang.c', 'r') as f:
    code = f.read()

# fix sign
code = code.replace('int val = 0; int sign = 1; int is_add=1;', 'int val = 0; int is_add=1;')

# fix l_len
code = code.replace('int l_len = (int)(fl_contains_sub(c, op) ? (const char*)0 : 0); /* hack */\n', '')

# fix while indentation inside "at (" parsing
old_at = "while(*p && *p!=',') xs[xi++] = *p++; xs[xi]=0; fl_rtrim(xs);"
new_at = "while(*p && *p!=',') xs[xi++] = *p++;\n                    xs[xi]=0; fl_rtrim(xs);"
code = code.replace(old_at, new_at)

old_at2 = "while(*p && *p!=')') ys[yi++] = *p++; ys[yi]=0; fl_rtrim(ys);"
new_at2 = "while(*p && *p!=')') ys[yi++] = *p++;\n                    ys[yi]=0; fl_rtrim(ys);"
code = code.replace(old_at2, new_at2)

# fix if indentation
old_if = "if(*p==',') p++; p=fl_ltrim(p);"
new_if = "if(*p==',') p++;\n                    p=fl_ltrim(p);"
code = code.replace(old_if, new_if)

with open('firstlang.c', 'w') as f:
    f.write(code)
print("Indents fixed")
