import sys
content = open('settings.c').read()

if 'draw_button(' in content:
    content = content.replace('draw_button((rect_t){cx + 20, cy + 320, 120, 30, 0}, 0x3D78C0, 0x4D88D0, "Update Profile", 0xFFFFFF);',
    '''fb_rect(cx + 20, cy + 320, 120, 30, 0x3D78C0);
    outline(cx + 20, cy + 320, 120, 30, 0x4D88D0);
    draw_label(cx + 35, cy + 328, "Update Profile", 0xFFFFFF);''')

open('settings.c', 'w').write(content)
