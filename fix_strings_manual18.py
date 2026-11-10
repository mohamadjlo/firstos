import re
with open('gui.c', 'r') as f:
    text = f.read()

search = """    const char *def = "class init:
  function Main:
    add button \\"Run Me\\" at (center, center)
  end
end";"""
replace = """    const char *def = "class init:\\n  function Main:\\n    add button \\"Run Me\\" at (center, center)\\n  end\\nend";"""

text = text.replace(search, replace)

search2 = """    serial_write("[gui] enter gui_init
");"""
replace2 = """    serial_write("[gui] enter gui_init\\n");"""
text = text.replace(search2, replace2)

with open('gui.c', 'w') as f:
    f.write(text)
