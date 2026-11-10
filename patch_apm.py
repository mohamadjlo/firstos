import sys
content = open('gui.c').read()

if 'outw(0x4004, 0x3400);' in content:
    content = content.replace('outw(0x4004, 0x3400);', 'outw(0x4004, 0x3400);\n        // APM Fallback\n        outw(0x8900, 0x5307);\n        outw(0x8900, 0x5301);\n        outw(0x8900, 0x2000);')

open('gui.c', 'w').write(content)
