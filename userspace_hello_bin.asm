section .rodata
global userspace_hello_elf_start
global userspace_hello_elf_end

userspace_hello_elf_start:
    incbin "userspace/apps/userspace_hello/userspace_hello.elf"
userspace_hello_elf_end:
