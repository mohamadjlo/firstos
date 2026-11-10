#ifndef CPU_H
#define CPU_H

static inline void cpu_pause(void) {
    __asm__ volatile ("pause" ::: "memory");
}

void cpu_halt(void);
void cpu_halt_forever(void);
void fpu_init(void);

#endif
