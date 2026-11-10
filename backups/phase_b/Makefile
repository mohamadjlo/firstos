ISO_DIR = isodir
KERNEL = kernel.bin
ISO = firstos.iso
FS_IMAGE = fs.img
FS_IMAGE_SIZE = 1073741824
AS = nasm

ifeq ($(shell command -v i686-elf-gcc >/dev/null 2>&1 && echo yes),yes)
TOOLPREFIX = i686-elf-
else ifeq ($(shell command -v x86_64-elf-gcc >/dev/null 2>&1 && echo yes),yes)
TOOLPREFIX = x86_64-elf-
else
TOOLPREFIX =
endif

CC = $(TOOLPREFIX)gcc
CXX = $(TOOLPREFIX)g++
LD = $(TOOLPREFIX)ld
GRUBMKRESCUE := $(shell command -v grub-mkrescue 2>/dev/null || command -v i686-elf-grub-mkrescue 2>/dev/null || command -v x86_64-elf-grub-mkrescue 2>/dev/null)
CFLAGS = -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -nostdlib -Wall -Wextra -O0 -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS = -m elf_i386 -T linker.ld -Lsysroot/i686-elf/lib -lc -lm
OBJS = posix_syscalls.o posix_pthreads.o boot.o cpu.o io.o serial.o ps2.o kernel.o framebuffer.o font.o explorer.o rtc.o fs.o storage.o memory.o heap.o virtio_blk.o pci.o hda.o compositor.o virtio_gpu.o virtio_gpu_2d.o gui.o settings.o text_edit.o desktop.o theme.o image.o tiff.o preview.o photo_edit.o terminal.o firstlang.o ide.o ui_builder.o idt.o isr.o gdt_asm.o faults.o gdt.o syscall.o elf.o paging.o paging_asm.o pic.o task.o fault.o taskman.o photo_viewer.o media_player.o spotlight.o dock.o hw_discovery.o keybindings.o widgets.o net.o cxx_runtime.o file_dialog.o user.o thumbnail.o sound.o browser_core.o web_browser_app.o display_server.o linux_syscalls.o linux_elf.o virtio_net.o eth.o arp.o ip.o icmp.o udp.o dhcp.o dns.o tcp.o http_client.o tls.o cookies.o ports.o firewall.o net_socket.o http_v11.o download_manager.o html5_arena.o html5_engine.o html5_resources.o html5_script.o html5_media.o text_engine.o bnet_error.o http_body.o

all: check-toolchain external-apps $(ISO)

external-apps:
	$(MAKE) -C apps/hello
	$(MAKE) -C baremetal_browser firstos

check-toolchain:
	@command -v $(CC) >/dev/null 2>&1 || { echo "Missing compiler: $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null 2>&1 || { echo "Missing linker: $(LD)"; exit 1; }
	@command -v $(AS) >/dev/null 2>&1 || { echo "Missing assembler: $(AS)"; exit 1; }

check-iso-tools:
	@test -n "$(GRUBMKRESCUE)" || { echo "Missing grub-mkrescue"; exit 1; }

boot.o: boot.asm
	$(AS) -f elf32 boot.asm -o boot.o

isr.o: isr.asm
	$(AS) -f elf32 isr.asm -o isr.o

idt.o: idt.c idt.h
	$(CC) $(CFLAGS) -c idt.c -o idt.o



cpu.o: cpu.asm cpu.h
	$(AS) -f elf32 cpu.asm -o cpu.o

io.o: io.asm io.h
	$(AS) -f elf32 io.asm -o io.o

serial.o: serial.c serial.h io.h
	$(CC) $(CFLAGS) -c serial.c -o serial.o

ps2.o: ps2.c ps2.h io.h framebuffer.h serial.h
	$(CC) $(CFLAGS) -c ps2.c -o ps2.o

kernel.o: kernel.c framebuffer.h gui.h cpu.h serial.h ps2.h fs.h virtio_blk.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

framebuffer.o: framebuffer.c framebuffer.h
	$(CC) $(CFLAGS) -c framebuffer.c -o framebuffer.o

font.o: font.c font.h framebuffer.h
	$(CC) $(CFLAGS) -c font.c -o font.o

explorer.o: explorer.c explorer.h io.h
	$(CC) $(CFLAGS) -c explorer.c -o explorer.o

rtc.o: rtc.c rtc.h io.h
	$(CC) $(CFLAGS) -c rtc.c -o rtc.o

storage.o: storage.c storage.h serial.h
	$(CC) $(CFLAGS) -c storage.c -o storage.o

memory.o: memory.c memory.h
	$(CC) $(CFLAGS) -c memory.c -o memory.o

fs.o: fs.c fs.h storage.h serial.h
	$(CC) $(CFLAGS) -c fs.c -o fs.o

virtio_blk.o: virtio_blk.c virtio_blk.h io.h serial.h
	$(CC) $(CFLAGS) -c virtio_blk.c -o virtio_blk.o

pci.o: pci.c pci.h io.h serial.h


	$(CC) $(CFLAGS) -c pci.c -o pci.o

hda.o: hda.c hda.h pci.h io.h serial.h
	$(CC) $(CFLAGS) -c hda.c -o hda.o

gui.o: gui.c gui.h gui_internal.h settings.h text_edit.h terminal.h framebuffer.h ps2.h font.h explorer.h cpu.h rtc.h storage.h fs.h pci.h net.h
	$(CC) $(CFLAGS) -c gui.c -o gui.o

settings.o: settings.c settings.h gui_internal.h framebuffer.h font.h net.h ports.h firewall.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c settings.c -o settings.o


text_edit.o: text_edit.c text_edit.h gui_internal.h framebuffer.h font.h fs.h
	$(CC) $(CFLAGS) -c text_edit.c -o text_edit.o

terminal.o: terminal.c terminal.h firstlang.h fs.h framebuffer.h font.h serial.h rtc.h net.h ports.h firewall.h tcp.h udp.h net_socket.h http_v11.h download_manager.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c terminal.c -o terminal.o

firstlang.o: firstlang.c firstlang.h fs.h framebuffer.h font.h serial.h rtc.h
	$(CC) $(CFLAGS) -c firstlang.c -o firstlang.o

desktop.o: desktop.c desktop.h gui_internal.h framebuffer.h font.h explorer.h fs.h theme.h
	$(CC) $(CFLAGS) -c desktop.c -o desktop.o

theme.o: theme.c theme.h framebuffer.h
	$(CC) $(CFLAGS) -c theme.c -o theme.o

image.o: image.c image.h heap.h serial.h fs.h
	$(CC) $(CFLAGS) -c image.c -o image.o

user.o: user.c user.h framebuffer.h fs.h
	$(CC) $(CFLAGS) -c user.c -o user.o

$(KERNEL): $(OBJS) linker.ld
	$(LD) -m elf_i386 -T linker.ld -o $(KERNEL) $(OBJS) -Lsysroot/i686-elf/lib -lc -lm

$(FS_IMAGE): tools/build_fs.py
	python3 tools/build_fs.py $(FS_IMAGE) $(FS_IMAGE_SIZE)

$(ISO): check-iso-tools $(KERNEL) $(FS_IMAGE)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/$(KERNEL)
	cp $(FS_IMAGE) $(ISO_DIR)/boot/$(FS_IMAGE)
	printf '%s\n' 'set timeout=0' 'set default=0' 'set gfxmode=1920x1080x32,1600x900x32,1440x900x32,1280x800x32,1280x720x32,1024x768x32,auto' 'set gfxpayload=keep' 'menuentry "FirstOS" {' '    multiboot /boot/$(KERNEL)' '    module /boot/$(FS_IMAGE) fsimg' '    boot' '}' > $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUBMKRESCUE) -o $(ISO) $(ISO_DIR)

run: $(ISO)
	@python3 tools/scan_host_wifi.py 2>/dev/null || true
	sh tools/run_firstos.sh

run-fb: $(ISO)
	qemu-system-i386 -kernel $(KERNEL) -initrd $(FS_IMAGE) -m 4G -serial stdio -vga std -display cocoa,zoom-to-fit=on

clean:
	rm -rf $(OBJS) $(KERNEL) $(ISO) $(ISO_DIR) $(FS_IMAGE)

compositor.o: compositor.c compositor.h
	$(CC) $(CFLAGS) -c compositor.c -o compositor.o

virtio_gpu.o: virtio_gpu.c virtio_gpu.h pci.h serial.h
	$(CC) $(CFLAGS) -c virtio_gpu.c -o virtio_gpu.o

virtio_gpu_2d.o: virtio_gpu_2d.c virtio_gpu_2d.h virtio_gpu_protocol.h
	$(CC) $(CFLAGS) -c virtio_gpu_2d.c -o virtio_gpu_2d.o

heap.o: heap.c heap.h
	$(CC) $(CFLAGS) -c heap.c -o heap.o
photo_edit.o: photo_edit.c
	$(CC) $(CFLAGS) -c photo_edit.c -o photo_edit.o

thumbnail.o: thumbnail.c thumbnail.h image.h heap.h framebuffer.h fs.h task.h
	$(CC) $(CFLAGS) -c thumbnail.c -o thumbnail.o

syscall.o: syscall.c syscall.h idt.h
	$(CC) $(CFLAGS) -c syscall.c -o syscall.o

elf.o: elf.c elf.h
	$(CC) $(CFLAGS) -c elf.c -o elf.o

ui_builder.o: ui_builder.c ui_builder.h font.h firstlang.h fs.h
	$(CC) $(CFLAGS) -c ui_builder.c -o ui_builder.o

paging_asm.o: paging.asm
	$(AS) -f elf32 paging.asm -o paging_asm.o
paging.o: paging.c paging.h
	$(CC) $(CFLAGS) -c paging.c -o paging.o

pic.o: pic.c pic.h io.h
	$(CC) $(CFLAGS) -c pic.c -o pic.o

task.o: task.c task.h io.h serial.h
	$(CC) $(CFLAGS) -c task.c -o task.o
fault.o: fault.c
	$(CC) $(CFLAGS) -c fault.c -o fault.o
gdt_asm.o: gdt.asm
	nasm -f elf32 gdt.asm -o gdt_asm.o
gdt.o: gdt.c
	i686-elf-gcc $(CFLAGS) -c gdt.c -o gdt.o
faults.o: faults.asm
	nasm -f elf32 faults.asm -o faults.o

media_player.o: media_player.c media_player.h
	$(CC) $(CFLAGS) -c media_player.c -o media_player.o

posix_syscalls.o: posix/syscalls.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c posix/syscalls.c -o posix_syscalls.o

posix_pthreads.o: posix/pthreads_stub.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c posix/pthreads_stub.c -o posix_pthreads.o

tiff.o: tiff.c tiff.h heap.h serial.h
	$(CC) $(CFLAGS) -c tiff.c -o tiff.o

preview.o: preview.c preview.h image.h framebuffer.h font.h fs.h heap.h serial.h
	$(CC) $(CFLAGS) -c preview.c -o preview.o

widgets.o: widgets.c widgets.h framebuffer.h font.h rtc.h theme.h gui_internal.h
	$(CC) $(CFLAGS) -c widgets.c -o widgets.o

cxx_runtime.o: cxx_runtime.cpp cxx_runtime.h heap.h serial.h
	$(CXX) $(CFLAGS) -fno-exceptions -fno-rtti -c cxx_runtime.cpp -o cxx_runtime.o

file_dialog.o: file_dialog.c file_dialog.h fs.h framebuffer.h font.h gui_internal.h
	$(CC) $(CFLAGS) -c file_dialog.c -o file_dialog.o

sound.o: sound.c sound.h hda.h fs.h heap.h cpu.h minimp3.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c sound.c -o sound.o

web_browser_app.o: web_browser_app.c web_browser_app.h browser_core.h framebuffer.h font.h serial.h gui.h gui_internal.h html5_engine.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c web_browser_app.c -o web_browser_app.o

browser_core.o: browser_core.c browser_core.h http_v11.h serial.h
	$(CC) $(NET_CFLAGS) -c browser_core.c -o browser_core.o

display_server.o: display_server.c display_server.h virtio_gpu.h virtio_gpu_2d.h framebuffer.h heap.h serial.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c display_server.c -o display_server.o

linux_syscalls.o: linux_syscalls.c linux_syscalls.h display_server.h fs.h heap.h serial.h rtc.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c linux_syscalls.c -o linux_syscalls.o

linux_elf.o: linux_elf.c linux_elf.h task.h fs.h heap.h serial.h paging.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c linux_elf.c -o linux_elf.o

# TCP/IP networking stack
NET_CFLAGS = $(CFLAGS) -I sysroot/i686-elf/include

bnet_error.o: bnet_error.c bnet_error.h
	$(CC) $(NET_CFLAGS) -c bnet_error.c -o bnet_error.o

http_body.o: http_body.c http_body.h serial.h
	$(CC) $(NET_CFLAGS) -c http_body.c -o http_body.o

virtio_net.o: virtio_net.c virtio_net.h pci.h io.h serial.h
	$(CC) $(NET_CFLAGS) -c virtio_net.c -o virtio_net.o

eth.o: eth.c eth.h virtio_net.h io.h
	$(CC) $(NET_CFLAGS) -c eth.c -o eth.o

arp.o: arp.c arp.h eth.h io.h
	$(CC) $(NET_CFLAGS) -c arp.c -o arp.o

ip.o: ip.c ip.h eth.h arp.h io.h
	$(CC) $(NET_CFLAGS) -c ip.c -o ip.o

icmp.o: icmp.c icmp.h ip.h eth.h io.h
	$(CC) $(NET_CFLAGS) -c icmp.c -o icmp.o

udp.o: udp.c udp.h ip.h eth.h io.h
	$(CC) $(NET_CFLAGS) -c udp.c -o udp.o

dhcp.o: dhcp.c dhcp.h udp.h eth.h io.h
	$(CC) $(NET_CFLAGS) -c dhcp.c -o dhcp.o

dns.o: dns.c dns.h udp.h eth.h io.h
	$(CC) $(NET_CFLAGS) -c dns.c -o dns.o

tcp.o: tcp.c tcp.h ip.h eth.h arp.h io.h
	$(CC) $(NET_CFLAGS) -c tcp.c -o tcp.o

http_client.o: http_client.c http_client.h http_v11.h tcp.h tls.h cookies.h dns.h eth.h io.h
	$(CC) $(NET_CFLAGS) -c http_client.c -o http_client.o

tls.o: tls.c tls.h tcp.h rtc.h serial.h
	$(CC) $(NET_CFLAGS) -c tls.c -o tls.o

cookies.o: cookies.c cookies.h fs.h serial.h
	$(CC) $(NET_CFLAGS) -c cookies.c -o cookies.o

net.o: net.c net.h virtio_net.h eth.h arp.h ip.h icmp.h udp.h dhcp.h dns.h tcp.h http_client.h ports.h firewall.h
	$(CC) $(NET_CFLAGS) -c net.c -o net.o

ports.o: ports.c ports.h tcp.h udp.h io.h serial.h
	$(CC) $(NET_CFLAGS) -c ports.c -o ports.o

firewall.o: firewall.c firewall.h fs.h rtc.h serial.h io.h
	$(CC) $(NET_CFLAGS) -c firewall.c -o firewall.o

net_socket.o: net_socket.c net_socket.h tcp.h tls.h dns.h io.h serial.h
	$(CC) $(NET_CFLAGS) -c net_socket.c -o net_socket.o

http_v11.o: http_v11.c http_v11.h net_socket.h io.h serial.h
	$(CC) $(NET_CFLAGS) -c http_v11.c -o http_v11.o

download_manager.o: download_manager.c download_manager.h http_v11.h fs.h rtc.h serial.h io.h
	$(CC) $(NET_CFLAGS) -c download_manager.c -o download_manager.o

html5_arena.o: html5_arena.c html5_arena.h
	$(CC) $(NET_CFLAGS) -c html5_arena.c -o html5_arena.o

html5_engine.o: html5_engine.c html5_engine.h html5_arena.h html5_resources.h html5_script.h html5_media.h http_client.h image.h framebuffer.h font.h heap.h serial.h
	$(CC) $(NET_CFLAGS) -c html5_engine.c -o html5_engine.o

html5_resources.o: html5_resources.c html5_resources.h html5_engine.h http_client.h image.h heap.h serial.h
	$(CC) $(NET_CFLAGS) -c html5_resources.c -o html5_resources.o

html5_script.o: html5_script.c html5_script.h html5_engine.h http_client.h serial.h heap.h
	$(CC) $(NET_CFLAGS) -c html5_script.c -o html5_script.o

html5_media.o: html5_media.c html5_media.h
	$(CC) $(NET_CFLAGS) -c html5_media.c -o html5_media.o

text_engine.o: text_engine.c text_engine.h io.h framebuffer.h font.h
	$(CC) $(CFLAGS) -c text_engine.c -o text_engine.o

# Baremetal Browser Subsystem Objects
bb_libc.o: baremetal_browser/bb_libc.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/bb_libc.c -o bb_libc.o

bb_rasterizer.o: baremetal_browser/bb_rasterizer.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/bb_rasterizer.c -o bb_rasterizer.o

bb_font.o: baremetal_browser/bb_font.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/bb_font.c -o bb_font.o

bb_image.o: baremetal_browser/bb_image.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/bb_image.c -o bb_image.o

bb_dom.o: baremetal_browser/bb_dom.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/bb_dom.c -o bb_dom.o

html_entities.o: baremetal_browser/html_entities.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/html_entities.c -o html_entities.o

html_tokenizer.o: baremetal_browser/html_tokenizer.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/html_tokenizer.c -o html_tokenizer.o

html_parser.o: baremetal_browser/html_parser.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/html_parser.c -o html_parser.o

bb_html.o: baremetal_browser/bb_html.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/bb_html.c -o bb_html.o

css_tokenizer.o: baremetal_browser/css_tokenizer.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/css_tokenizer.c -o css_tokenizer.o

css_selectors.o: baremetal_browser/css_selectors.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/css_selectors.c -o css_selectors.o

css_parser.o: baremetal_browser/css_parser.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/css_parser.c -o css_parser.o

css_cascade.o: baremetal_browser/css_cascade.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/css_cascade.c -o css_cascade.o

style_resolver.o: baremetal_browser/style_resolver.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/style_resolver.c -o style_resolver.o

bb_css.o: baremetal_browser/bb_css.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/bb_css.c -o bb_css.o

painter.o: baremetal_browser/painter.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/painter.c -o painter.o

hit_test.o: baremetal_browser/hit_test.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/hit_test.c -o hit_test.o

bb_layout.o: baremetal_browser/bb_layout.c
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -I baremetal_browser -c baremetal_browser/bb_layout.c -o bb_layout.o

