; FirstOS 64-bit Bootloader & Long Mode Trampoline
; Multiboot 1 entry in 32-bit protected mode -> Transitions to 64-bit Long Mode

section .multiboot
align 4
    dd 0x1BADB002               ; Multiboot magic
    dd 0x00000003               ; Flags (align modules + mem info)
    dd -(0x1BADB002 + 0x00000003) ; Checksum

section .bss
alignb 4096
global early_pml4
global stack_bottom
global stack_top

early_pml4:
    resb 4096
early_pdpt:
    resb 4096
early_pd:
    resb 4096 * 4               ; 4 Page Directories (maps 4GB via 2MB pages)

multiboot_magic_save:
    resd 1
multiboot_info_save:
    resd 1

alignb 16
stack_bottom:
    resb 1048576                ; 1MB Kernel Stack
stack_top:

section .rodata
align 16
early_gdt:
    dq 0x0000000000000000       ; 0x00: Null Descriptor
    dq 0x00209A0000000000       ; 0x08: 64-bit Kernel Code (L=1, D=0, Access=0x9A)
    dq 0x0000920000000000       ; 0x10: 64-bit Kernel Data (Access=0x92)
early_gdt_end:

early_gdt_desc:
    dw (early_gdt_end - early_gdt - 1)
    dd early_gdt

section .text
[bits 32]
global start
extern kernel_main

start:
    cli

    ; Save multiboot parameters immediately before clobbering eax/ebx
    mov [multiboot_magic_save], eax
    mov [multiboot_info_save], ebx

    ; 1. Check if CPUID is supported
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21            ; Toggle ID bit
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    jz .no_long_mode

    ; 2. Check for Long Mode support via extended CPUID
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29           ; Test Long Mode (LM) bit
    jz .no_long_mode

    ; 3. Build early 4-level identity page tables (0..4GB)
    ; Clear tables
    mov edi, early_pml4
    xor eax, eax
    mov ecx, (4096 + 4096 + 4096 * 4) / 4
    rep stosd

    ; early_pml4[0] -> early_pdpt (flags: Present=1, RW=2 => 0x03)
    mov eax, early_pdpt
    or eax, 0x03
    mov [early_pml4], eax

    ; early_pdpt[0..3] -> early_pd[0..3]
    mov eax, early_pd
    or eax, 0x03
    mov [early_pdpt + 0], eax

    mov eax, early_pd + 4096
    or eax, 0x03
    mov [early_pdpt + 8], eax

    mov eax, early_pd + 8192
    or eax, 0x03
    mov [early_pdpt + 16], eax

    mov eax, early_pd + 12288
    or eax, 0x03
    mov [early_pdpt + 24], eax

    ; Fill 4 Page Directories with 2048 2MB pages (maps 0..4GB)
    ; Entry format: (page_index * 2MB) | 0x83 (Present, RW, PageSize=2MB)
    mov edi, early_pd
    xor ebx, ebx                ; Physical address (starts at 0)
    mov ecx, 2048               ; 2048 entries * 2MB = 4096MB (4GB)

.map_pd_loop:
    mov eax, ebx
    or eax, 0x83                ; Present + Read/Write + 2MB Page Size
    mov [edi], eax              ; Low 32 bits
    mov dword [edi + 4], 0      ; High 32 bits (0 for < 4GB)
    add ebx, 0x200000           ; Advance 2MB
    add edi, 8                  ; Next 64-bit entry
    dec ecx
    jnz .map_pd_loop

    ; 4. Load CR3 with PML4
    mov eax, early_pml4
    mov cr3, eax

    ; 5. Enable PAE in CR4 (bit 5)
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; 6. Enable Long Mode in EFER MSR (MSR 0xC0000080, bit 8 = LME)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8              ; LME
    wrmsr

    ; 7. Enable Paging in CR0 (bit 31 = PG, bit 0 = PE)
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax

    ; 8. Load 64-bit GDT
    lgdt [early_gdt_desc]

    ; 9. Far jump to 64-bit Long Mode
    jmp 0x08:long_mode_entry

.no_long_mode:
    ; Print red error message to VGA text mode
    mov esi, msg_no_lm
    mov edi, 0xB8000
.err_loop:
    lodsb
    test al, al
    jz .hang
    mov ah, 0x4F                ; White on Red
    mov [edi], ax
    add edi, 2
    jmp .err_loop

.hang:
    hlt
    jmp .hang

section .data
msg_no_lm:
    db "FirstOS FATAL: 64-bit x86_64 CPU with Long Mode required!", 0

section .text
[bits 64]
default rel
long_mode_entry:
    ; Set 64-bit data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; Initialize 64-bit stack pointer
    mov rsp, stack_top

    ; Enable SSE/FPU in CR0 and CR4 for 64-bit mode
    mov rax, cr0
    and rax, ~0x04              ; Clear EM (bit 2)
    or rax, 0x02                ; Set MP (bit 1)
    mov cr0, rax

    mov rax, cr4
    or rax, 0x600               ; Set OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
    mov cr4, rax

    fninit

    ; Pass Multiboot arguments according to System V AMD64 ABI:
    ; RDI = multiboot_magic (32-bit unsigned extended)
    ; RSI = multiboot_info_addr (32-bit physical pointer extended)
    mov edi, [multiboot_magic_save]
    mov esi, [multiboot_info_save]

    ; Call 64-bit kernel_main
    call kernel_main

.hang64:
    cli
    hlt
    jmp .hang64
