import sys
content = open('gui.c').read()

idx = content.find('else if (point_in_rect(mx, my, start_shutdown_item)) { power_off_requested = 1; start_menu_open = 0; }')
end = content.find('else if (point_in_rect(mx, my, start_restart_item)) { main_window_open = 1; main_window_minimized = 0; open_app(APP_TERMINAL); open_app(APP_FILES); open_app(APP_DISPLAY); open_app(APP_EXPLORER); open_app(APP_SETTINGS); start_menu_open = 0; }')

if idx != -1:
    patch = """else if (point_in_rect(mx, my, start_restart_item)) {
        extern void outb(unsigned short port, unsigned char data);
        extern unsigned char inb(unsigned short port);
        unsigned char good = 0x02;
        while (good & 0x02) good = inb(0x64);
        outb(0x64, 0xFE);
        outb(0x0CF9, 0x02);
        outb(0x0CF9, 0x06);
        while(1) __asm__ volatile("cli; hlt"); 
    } else if (point_in_rect(mx, my, start_shutdown_item)) {
        extern void outw(unsigned short port, unsigned short data);
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000); 
        outw(0x4004, 0x3400);
        outw(0x8900, 0x5307);
        outw(0x8900, 0x5301);
        outw(0x8900, 0x2000);
        while(1) __asm__ volatile("cli; hlt"); 
    }"""
    content = content[:idx] + patch + content[end + 252:]

open('gui.c', 'w').write(content)
