import sys
content = open('dock.c').read()

if 'float target_scale = hov ? DOCK_MAGNIFY_SCALE : 1.0f;' in content:
    content = content.replace('float target_scale = hov ? DOCK_MAGNIFY_SCALE : 1.0f;', 'float target_scale = (dock_magnification_enabled && hov) ? DOCK_MAGNIFY_SCALE : 1.0f;')

open('dock.c', 'w').write(content)
