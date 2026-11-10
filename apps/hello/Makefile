CC = x86_64-elf-gcc
LD = x86_64-elf-ld
CFLAGS = -m64 -mcmodel=small -mno-red-zone -ffreestanding -fno-pic -fno-pie -fno-stack-protector -nostdlib -Wall -Wextra -O2

all: hello.elf

hello.elf: hello.c linker.ld
	$(CC) $(CFLAGS) -c hello.c -o hello.o
	$(LD) -m elf_x86_64 -T linker.ld hello.o -o hello.elf

clean:
	rm -f hello.o hello.elf
