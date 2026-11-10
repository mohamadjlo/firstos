[bits 64]
default rel

section .text
global gdt_flush
; RDI = pointer to gdt_ptr64
gdt_flush:
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 64-bit far return to reload CS with 0x08
    push qword 0x08
    lea rax, [.flush]
    push rax
    retfq
.flush:
    ret

global tss_flush
tss_flush:
    mov ax, 0x28
    ltr ax
    ret
