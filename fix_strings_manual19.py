import re
with open('gui.c', 'r') as f:
    text = f.read()

search = """    serial_write("[gui] before explorer_init
");"""
replace = """    serial_write("[gui] before explorer_init\\n");"""
text = text.replace(search, replace)

search2 = """    serial_write("[gui] after explorer_init
");"""
replace2 = """    serial_write("[gui] after explorer_init\\n");"""
text = text.replace(search2, replace2)

search3 = """    serial_write("[gui] setting up apps
");"""
replace3 = """    serial_write("[gui] setting up apps\\n");"""
text = text.replace(search3, replace3)

search4 = """    serial_write("[gui] wallpaper pre-cache
");"""
replace4 = """    serial_write("[gui] wallpaper pre-cache\\n");"""
text = text.replace(search4, replace4)

search5 = """    serial_write("[gui] done
");"""
replace5 = """    serial_write("[gui] done\\n");"""
text = text.replace(search5, replace5)

with open('gui.c', 'w') as f:
    f.write(text)
