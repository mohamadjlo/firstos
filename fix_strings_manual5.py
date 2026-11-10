import re
with open('gui.c', 'r') as f:
    text = f.read()

# Fix characters
text = re.sub(r"== '\n'", r"== '\\n'", text)
text = re.sub(r"!= '\n'", r"!= '\\n'", text)
text = re.sub(r"== '\n", r"== '\\n", text)
text = re.sub(r"!= '\n", r"!= '\\n", text)
text = text.replace("== ' '", "== ' '") # not needed

# Also, there's an error at 1154!
# gui.c:1154:33: error: missing terminating " character
# gui.c:1155:5: error: 'import' undeclared (first use in this function)

search_broken1 = """                gui_fl_cat(imp, "function ui:
    import \\"", 128);"""
replace_broken1 = """                gui_fl_cat(imp, "function ui:\\n    import \\"", 128);"""
text = text.replace(search_broken1, replace_broken1)

search_broken2 = """                gui_fl_cat(imp, "\\"
end
", 128);"""
replace_broken2 = """                gui_fl_cat(imp, "\\"\\nend\\n", 128);"""
text = text.replace(search_broken2, replace_broken2)

with open('gui.c', 'w') as f:
    f.write(text)
