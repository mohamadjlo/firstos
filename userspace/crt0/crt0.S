[bits 64]
default rel

section .text
global _start
extern main
extern firstos_exit

_start:
    ; Terminate frame pointer chain for stack traces
    xor rbp, rbp

    ; System V AMD64 initial userspace stack layout:
    ; [rsp]     = argc
    ; [rsp + 8] = argv[0] pointer
    ; RDI = argc
    ; RSI = argv (char **)
    mov rdi, [rsp]          ; rdi = argc
    lea rsi, [rsp + 8]      ; rsi = argv

    ; Ensure 16-byte stack alignment for SysV ABI call
    and rsp, -16

    call main

    ; Pass return value of main to firstos_exit
    mov rdi, rax
    call firstos_exit

.hang:
    ; Emergency yield/hang if exit returns
    int 0x81
    jmp .hang
