with open('gui.c', 'r') as f:
    text = f.read()

idx = text.find('if (ide_saving_ui == 1) {')
if idx != -1:
    end_idx = text.find('ide_designer_mode = 0;\\n            }', idx)
    if end_idx != -1:
        end_idx += len('ide_designer_mode = 0;\\n            }')
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
        text = text[:idx] + replace + text[end_idx:]

with open('gui.c', 'w') as f:
    f.write(text)
