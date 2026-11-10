import sys
content = open('explorer.h').read()

if 'int cached_icon;' not in content:
    content = content.replace('int is_dir;\n} explorer_entry_t;', 'int is_dir;\n    int cached_icon;\n    u32 folder_color;\n} explorer_entry_t;')

open('explorer.h', 'w').write(content)
