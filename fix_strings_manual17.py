import re
with open('gui.c', 'r') as f:
    text = f.read()

search = """                terminal_print("--- IDE Output ---
"); //
");
                terminal_print(out);
                terminal_print("
");"""
# Wait, it looks like:
search2 = """                terminal_print("--- IDE Output ---\\n"); //
");
                terminal_print(out);
                terminal_print("
");"""
replace2 = """                terminal_print("--- IDE Output ---\\n");
                terminal_print(out);
                terminal_print("\\n");"""

text = text.replace(search2, replace2)

search3 = """                terminal_print("--- FirstLang Debugger ---\\n"); //
");
                terminal_print("Type run or quit.\\n"); //
");"""
replace3 = """                terminal_print("--- FirstLang Debugger ---\\n");
                terminal_print("Type run or quit.\\n");"""

text = text.replace(search3, replace3)

with open('gui.c', 'w') as f:
    f.write(text)
