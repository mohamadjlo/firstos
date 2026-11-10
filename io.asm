[bits 64]
section .text
global outb
global outw
global outl
global inb
global inw
global inl

outb:
    mov dx, di
    mov al, sil
    out dx, al
    ret

outw:
    mov dx, di
    mov ax, si
    out dx, ax
    ret

outl:
    mov dx, di
    mov eax, esi
    out dx, eax
    ret

inb:
    mov dx, di
    xor eax, eax
    in al, dx
    ret

inw:
    mov dx, di
    xor eax, eax
    in ax, dx
    ret

inl:
    mov dx, di
    in eax, dx
    ret
