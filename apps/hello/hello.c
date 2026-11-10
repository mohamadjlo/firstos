void sys_print(const char *text) {
    __asm__ volatile (
        "int $0x80\n"
        : 
        : "a" (0), "D" (text)
        : "rcx", "r11", "memory"
    );
}

void sys_yield(void) {
    __asm__ volatile("int $0x81");
}

void _start(void) {
    sys_print("HELLO FIRSTOS TASK (Yielding properly)!\n");
    while (1) {
        sys_yield();
    }
}
