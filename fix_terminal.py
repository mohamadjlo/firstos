with open('terminal.c', 'r') as f:
    code = f.read()

bad_doc = """        if (fl_starts(op, "doc") || fl_starts(op, "help")) {
            tprint("\\n=== FirstLang Documentation ===\\n");
            tprint("Syntax:    class init: function Main.\\n");
            tprint("           function Main:\\n");
            tprint("Variables: set x to 10 | set name to \\"Ali\\"\\n");
            tprint("Arrays:    set list[0] to 5 | set item to list[0]\\n");
            tprint("Loops:     while x < 5: ... end\\n");
            tprint("If:        if x == 10: ... end\\n");
            tprint("UI:        add button \\"Click Me\\" at (center, 10)\\n");
            tprint("           add label \\"Text\\" at (10, 50)\\n");
            tprint("           add slider \\"Vol\\" at (left, bottom)\\n");
            tprint("Events:    on click \\"Click Me\\": ... end\\n");
            tprint("Functions: call OtherFunction\\n");
            tprint("File I/O:  write \\"Hello\\" to \\"/test.txt\\"\\n");
            tprint("           read \\"/test.txt\\" to myVar\\n");
            tprint("===============================\\n");
            return;
        }"""
code = code.replace(bad_doc, '')

good_doc = """    if(teq(subcmd,"doc") || teq(subcmd,"help")) {
        ti("=== FirstLang Documentation ===");
        ti("Syntax:    class init: function Main.");
        ti("           function Main:");
        ti("Variables: set x to 10 | set name to \\"Ali\\"");
        ti("Arrays:    set list[0] to 5 | set item to list[0]");
        ti("Loops:     while x < 5: ... end");
        ti("If:        if x == 10: ... end");
        ti("UI:        add button \\"Click Me\\" at (center, 10)");
        ti("           add label \\"Text\\" at (10, 50)");
        ti("           add slider \\"Vol\\" at (left, bottom)");
        ti("Events:    on click \\"Click Me\\": ... end");
        ti("Functions: call OtherFunction");
        ti("File I/O:  write \\"Hello\\" to \\"/test.txt\\"");
        ti("           read \\"/test.txt\\" to myVar");
        ti("===============================");
        return;
    }"""
code = code.replace('if(teq(subcmd,"run")||teq(subcmd,"exec")) {', good_doc + '\\n\\n    if(teq(subcmd,"run")||teq(subcmd,"exec")) {')

with open('terminal.c', 'w') as f:
    f.write(code)
print("fixed")
