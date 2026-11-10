import sys
content = open('explorer.c').read()

idx = content.find('    str_copy(current_path, display_path, sizeof(current_path));\n    parent_of(display_path, parent_path);\n}\n    str_copy(current_path, display_path, sizeof(current_path));\n    parent_of(display_path, parent_path);\n}')

if idx != -1:
    content = content.replace('    str_copy(current_path, display_path, sizeof(current_path));\n    parent_of(display_path, parent_path);\n}\n    str_copy(current_path, display_path, sizeof(current_path));\n    parent_of(display_path, parent_path);\n}',
                              '    str_copy(current_path, display_path, sizeof(current_path));\n    parent_of(display_path, parent_path);\n}')

if 'extern char explorer_search_text[64];' not in content:
    content = content.replace('#include "gui_internal.h"', '#include "gui_internal.h"\nextern char explorer_search_text[64];')

open('explorer.c', 'w').write(content)
