import sys
content = open('gui.c').read()

if 'outb(0x64, 0xFE);' in content:
    content = content.replace('outb(0x64, 0xFE);', 'outb(0x64, 0xFE);\n        while(1) __asm__ volatile("cli; hlt");')
if 'outw(0x604, 0x2000);' in content:
    content = content.replace('outw(0x604, 0x2000);', 'outw(0x604, 0x2000);\n        outw(0x4004, 0x3400);\n        while(1) __asm__ volatile("cli; hlt");')

open('gui.c', 'w').write(content)
