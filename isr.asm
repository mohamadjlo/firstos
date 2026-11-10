[bits 64]
default rel

section .text
global idt_load
extern idtp

idt_load:
    lidt [idtp]
    ret

global isr80
extern syscall_handler

; Linux / FirstOS System Call (int 0x80)
; In x86_64, user registers passed to int 0x80:
; RAX = syscall num
; RDI = arg1, RSI = arg2, RDX = arg3, RCX = arg4, R8 = arg5, R9 = arg6
isr80:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Call syscall_handler(num, arg1, arg2, arg3, arg4, arg5, arg6)
    ; System V AMD64 ABI:
    ; RDI = num (from saved RAX)
    ; RSI = arg1 (from saved RDI)
    ; RDX = arg2 (from saved RSI)
    ; RCX = arg3 (from saved RDX)
    ; R8  = arg4 (from saved R10 or RCX)
    ; R9  = arg5 (from saved R8)
    ; We have saved registers on stack:
    ; [rsp + 112] = rax
    ; [rsp + 72]  = rdi
    ; [rsp + 80]  = rsi
    ; [rsp + 88]  = rdx
    ; [rsp + 96]  = rcx
    ; [rsp + 56]  = r8
    ; [rsp + 48]  = r9
    mov rdi, [rsp + 112] ; num
    mov rsi, [rsp + 72]  ; arg1
    mov rdx, [rsp + 80]  ; arg2
    mov rcx, [rsp + 88]  ; arg3
    mov r8,  [rsp + 56]  ; arg4
    mov r9,  [rsp + 48]  ; arg5

    call syscall_handler

    ; Store return value in saved RAX (offset 112)
    mov [rsp + 112], rax

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

global isr32
extern timer_handler
extern current_task
extern tasks

; Timer IRQ 0 (vector 32)
isr32:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp        ; pass current_rsp in RDI
    call timer_handler
    ; RAX = new task's rsp

    mov rdx, rax        ; save new rsp

    ; Read new task's cr3: sizeof(task_t) == 64
    ; current_task is 32-bit int
    movsxd rax, dword [current_task]
    shl rax, 6          ; multiply by 64
    lea rbx, [tasks]
    add rax, rbx
    mov rax, [rax + 8]  ; offset 8 is cr3

    ; Switch CR3 if needed
    mov rbx, cr3
    cmp rax, rbx
    je .skip_cr3
    test rax, rax
    jz .skip_cr3
    mov cr3, rax
.skip_cr3:
    mov rsp, rdx

    ; Send EOI to PIC (0x20 to port 0x20)
    mov al, 0x20
    out 0x20, al

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

global isr81
extern task_yield

; Process Yield (int 0x81)
isr81:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp        ; pass current_rsp in RDI
    call task_yield
    ; RAX = new task's rsp

    mov rdx, rax        ; save new rsp

    ; Read new task's cr3: sizeof(task_t) == 64
    movsxd rax, dword [current_task]
    shl rax, 6          ; multiply by 64
    lea rbx, [tasks]
    add rax, rbx
    mov rax, [rax + 8]  ; offset 8 is cr3

    mov rbx, cr3
    cmp rax, rbx
    je .skip_cr3_81
    test rax, rax
    jz .skip_cr3_81
    mov cr3, rax
.skip_cr3_81:
    mov rsp, rdx

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq
