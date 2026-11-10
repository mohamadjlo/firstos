with open('gui.c', 'r') as f:
    text = f.read()

# Instead of regex, let's just find the exact block and replace it entirely!
# It's inside: if (ide_saving_ui == 1) { ... ide_designer_mode = 0; }

import re

search = r"if \(ide_saving_ui == 1\) \{\n                // Generate auto-detect import code\n                char imp\[128\]; imp\[0\] = 0;\n                gui_fl_cat\(imp, \"function ui:\n    import \\\"\", 128\);\\\n                gui_fl_cat\(imp, full_path, 128\);\\\n                gui_fl_cat\(imp, \"\\\"\nend\n\", 128\);\n                int im = 0; while\(imp\[im\]\) \{ ide_text\[im\] = imp\[im\]; im\+\+; \}\n                ide_text\[im\] = 0;\n                ide_length = im;\n                ide_cursor = im;\n                ide_saving_ui = 0;\n                ide_designer_mode = 0;\n            \}"

replace = """if (ide_saving_ui == 1) {
                // Generate auto-detect import code
                char imp[128]; imp[0] = 0;
                gui_fl_cat(imp, "function ui:\\n    import \\"", 128);
                gui_fl_cat(imp, full_path, 128);
                gui_fl_cat(imp, "\\"\\nend\\n", 128);
                int im = 0; while(imp[im]) { ide_text[im] = imp[im]; im++; }
                ide_text[im] = 0;
                ide_length = im;
                ide_cursor = im;
                ide_saving_ui = 0;
                ide_designer_mode = 0;
            }"""

text = re.sub(search, replace, text, flags=re.MULTILINE)

with open('gui.c', 'w') as f:
    f.write(text)
