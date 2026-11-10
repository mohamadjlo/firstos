import sys
content = open('gui.c').read()

idx = content.find('} else if (point_in_rect(mx, my, top_reboot_item)) {')
end_idx = content.find('} else if (point_in_rect(mx, my, top_shutdown_item)) {')
end2 = content.find('    needs_redraw = 1;\n    gui_click_sound();', end_idx)

robust_power = """} else if (point_in_rect(mx, my, top_reboot_item)) {
        extern void outb(unsigned short port, unsigned char data);
        extern unsigned char inb(unsigned short port);
        extern void serial_write(const char*);
        serial_write("[PWR] Attempting Reboot...\\n");
        
        // 1. Try 8042 Keyboard Controller Reset
        unsigned char good = 0x02;
        while (good & 0x02) good = inb(0x64);
        outb(0x64, 0xFE);
        
        // 2. Try PCI Reset (QEMU/Bochs fast reboot)
        outb(0x0CF9, 0x02);
        outb(0x0CF9, 0x06);
        
        while(1) __asm__ volatile("cli; hlt"); 
    } else if (point_in_rect(mx, my, top_shutdown_item)) {
        extern void outw(unsigned short port, unsigned short data);
        extern void serial_write(const char*);
        serial_write("[PWR] Attempting Shutdown...\\n");
        
        // QEMU modern ACPI shutdown
        outw(0x604, 0x2000);
        // Bochs / Old QEMU ACPI shutdown
        outw(0xB004, 0x2000); 
        // VirtualBox ACPI shutdown
        outw(0x4004, 0x3400);
        
        while(1) __asm__ volatile("cli; hlt"); 
"""

if idx != -1 and end2 != -1:
    content = content[:idx] + robust_power + content[end2:]

open('gui.c', 'w').write(content)
