import re
with open('gui.c', 'r') as f:
    text = f.read()

search = """serial_write("
");"""
replace = """serial_write("\\n");"""
text = text.replace(search, replace)

search2 = """serial_write("[wallpaper] raw loaded ok
");"""
replace2 = """serial_write("[wallpaper] raw loaded ok\\n");"""
text = text.replace(search2, replace2)

search3 = """        gui_fl_cat(imp, "function ui:
    import \\"", 128);
                gui_fl_cat(imp, full_path, 128);
                gui_fl_cat(imp, "\\"
end
", 128);"""
replace3 = """        gui_fl_cat(imp, "function ui:\\n    import \\"", 128);
                gui_fl_cat(imp, full_path, 128);
                gui_fl_cat(imp, "\\"\\nend\\n", 128);"""
text = text.replace(search3, replace3)

with open('gui.c', 'w') as f:
    f.write(text)
