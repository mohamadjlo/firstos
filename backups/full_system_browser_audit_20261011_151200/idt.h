#ifndef IDT_H
#define IDT_H

void idt_init(void);
void register_interrupt_handler(int num, void *handler);

#endif
