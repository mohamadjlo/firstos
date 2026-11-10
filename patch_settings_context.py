import sys
content = open('settings.c').read()

replacement = """
            extern void desktop_open_context_menu(int mx, int my, int context_type);
            desktop_open_context_menu(mx, my, 2);
            extern int needs_redraw; needs_redraw = 1;
            return;
"""

# Replace the broken context variables with the new clean function call
if 'extern int desktop_context_open;' in content:
    idx1 = content.find('extern int desktop_context_open;')
    idx2 = content.find('needs_redraw = 1;', idx1)
    content = content[:idx1] + replacement + content[idx2 + 18:]

open('settings.c', 'w').write(content)
