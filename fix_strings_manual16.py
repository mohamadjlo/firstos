import re
with open('gui.c', 'r') as f:
    text = f.read()

search = """                        terminal_print("
");"""
replace = """                        terminal_print("\\n");"""
text = text.replace(search, replace)

search2 = """                if (!firstlang_has_ui() && out[0]) {
                    open_app(APP_TERMINAL);
                    extern void terminal_print(const char *text);
                    terminal_print(out);
                    terminal_print("
");
                    focus_app_window(APP_TERMINAL - 1);
                }"""
replace2 = """                if (!firstlang_has_ui() && out[0]) {
                    open_app(APP_TERMINAL);
                    extern void terminal_print(const char *text);
                    terminal_print(out);
                    terminal_print("\\n");
                    focus_app_window(APP_TERMINAL - 1);
                }"""
text = text.replace(search2, replace2)

with open('gui.c', 'w') as f:
    f.write(text)
