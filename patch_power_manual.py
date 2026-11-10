import sys
content = open('gui.c').read()

idx = content.find('} else if (point_in_rect(mx, my, top_reboot_item)) {')
end_idx = content.find('    needs_redraw = 1;', idx)

if idx != -1 and end_idx != -1:
    robust = """} else if (point_in_rect(mx, my, top_reboot_item)) {
        extern void outb(unsigned short port, unsigned char data);
        extern unsigned char inb(unsigned short port);
        extern void serial_write(const char*);
        serial_write("[PWR] Reboot\\n");
        unsigned char good = 0x02;
        while (good & 0x02) good = inb(0x64);
        outb(0x64, 0xFE);
        outb(0x0CF9, 0x02);
        outb(0x0CF9, 0x06);
        while(1) __asm__ volatile("cli; hlt"); 
    } else if (point_in_rect(mx, my, top_shutdown_item)) {
        extern void outw(unsigned short port, unsigned short data);
        extern void serial_write(const char*);
        serial_write("[PWR] Shutdown\\n");
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000); 
        outw(0x4004, 0x3400);
        outw(0x8900, 0x5307);
        outw(0x8900, 0x5301);
        outw(0x8900, 0x2000);
        while(1) __asm__ volatile("cli; hlt"); 
    }
"""
    content = content[:idx] + robust + content[end_idx:]
    open('gui.c', 'w').write(content)
