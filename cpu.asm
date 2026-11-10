[bits 64]
section .text
global cpu_halt
global cpu_halt_forever
global fpu_init

cpu_halt:
    hlt
    ret

cpu_halt_forever:
.loop:
    cli
    hlt
    jmp .loop

fpu_init:
    mov rax, cr0
    and rax, ~0x04      ; Clear EM (bit 2)
    or rax, 0x02        ; Set MP (bit 1)
    mov cr0, rax

    mov rax, cr4
    or rax, 0x600       ; Set OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
    mov cr4, rax

    fninit
    ret
