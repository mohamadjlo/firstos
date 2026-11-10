with open('Makefile', 'r') as f:
    lines = f.readlines()

new_lines = []
for line in lines:
    if line.startswith('OBJS = '):
        new_lines.append('OBJS = boot.o cpu.o io.o serial.o ps2.o kernel.o framebuffer.o font.o explorer.o rtc.o fs.o storage.o memory.o heap.o virtio_blk.o pci.o hda.o compositor.o virtio_gpu.o virtio_gpu_2d.o gui.o settings.o text_edit.o desktop.o theme.o image.o photo_edit.o terminal.o firstlang.o ide.o ui_builder.o idt.o isr.o syscall.o elf.o paging.o paging_asm.o pic.o task.o fault.o\n')
    elif 'task.c -o task.o' in line:
        new_lines.append('\t$(CC) $(CFLAGS) -c task.c -o task.o\n')
    elif 'paging_asm.o:' in line and 'pic.o' in line:
        continue
    else:
        new_lines.append(line)

with open('Makefile', 'w') as f:
    f.writelines(new_lines)
