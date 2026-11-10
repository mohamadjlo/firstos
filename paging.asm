[bits 64]
section .text
global load_page_directory
global enable_paging
global flush_tlb
global invlpg_asm

load_page_directory: ; rdi = PML4 physical address
    mov cr3, rdi
    ret

enable_paging:
    mov rax, cr0
    bts rax, 31
    mov cr0, rax
    ret

flush_tlb:
    mov rax, cr3
    mov cr3, rax
    ret

invlpg_asm: ; rdi = virtual address
    invlpg [rdi]
    ret
