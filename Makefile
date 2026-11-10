ISO_DIR = isodir
KERNEL = kernel.bin
ISO = firstos.iso
FS_IMAGE = fs.img
FS_IMAGE_SIZE = 100663296
AS = nasm

ifeq ($(shell command -v x86_64-elf-gcc >/dev/null 2>&1 && echo yes),yes)
TOOLPREFIX = x86_64-elf-
else ifeq ($(shell command -v i686-elf-gcc >/dev/null 2>&1 && echo yes),yes)
TOOLPREFIX = i686-elf-
else
TOOLPREFIX =
endif

CC = $(TOOLPREFIX)gcc
CXX = $(TOOLPREFIX)g++
LD = $(TOOLPREFIX)ld
GRUBMKRESCUE := $(shell command -v i686-elf-grub-mkrescue 2>/dev/null || command -v grub-mkrescue 2>/dev/null || command -v x86_64-elf-grub-mkrescue 2>/dev/null)
CFLAGS = -m64 -mcmodel=kernel -mno-red-zone -ffreestanding -fno-pic -fno-pie -fno-stack-protector -nostdlib -Wall -Wextra -O2 -fomit-frame-pointer -finline-functions -fno-common -fno-asynchronous-unwind-tables -fno-unwind-tables
LDFLAGS = -m elf_x86_64 -T linker.ld -L/opt/homebrew/Cellar/x86_64-elf-gcc/16.2.0/lib/gcc/x86_64-elf/16.2.0 -lgcc
OBJS = kernel_libc.o posix_syscalls.o posix_pthreads.o boot.o cpu.o io.o serial.o ktrace.o ps2.o kernel.o kernel_cmd.o framebuffer.o font.o explorer.o rtc.o fs.o storage.o memory.o heap.o virtio_blk.o pci.o hda.o compositor.o virtio_gpu.o virtio_gpu_2d.o gui.o gui_weather.o gui_calc.o gui_notifications.o gui_login.o gui_overlays.o gui_widgets.o app_framework.o settings.o text_edit.o desktop.o theme.o image.o tiff.o preview.o photo_edit.o terminal.o firstlang.o ide.o ui_builder.o idt.o isr.o gdt_asm.o faults.o gdt.o syscall.o elf.o paging.o paging_asm.o pic.o task.o process.o pipe.o shm.o futex.o fault.o taskman.o photo_viewer.o media_player.o spotlight.o dock.o hw_discovery.o keybindings.o widgets.o net.o cxx_runtime.o file_dialog.o user.o thumbnail.o sound.o browser_logger.o browser_core.o browser_network_service.o browser_renderer_pipeline.o browser_storage_service.o web_browser_app.o browser_host_ipc.o browser_security.o display_server.o linux_syscalls.o linux_elf.o virtio_net.o eth.o arp.o ip.o icmp.o udp.o dhcp.o dns.o tcp.o http_client.o tls.o cookies.o ports.o firewall.o net_socket.o http_v11.o download_manager.o browser_storage.o html5_arena.o html5_engine.o html5_resources.o html5_script.o html5_media.o text_engine.o bnet_error.o http_body.o data_url.o clock.o vfs_fd.o platform_chromium_base.o ring3_test.o userspace_loader.o userspace_hello_bin.o gpu_registry.o gpu_generic_fb.o gpu_virtio.o gpu_detect.o gpu_cursor.o gpu_present.o gpu_buffer.o gpu_event.o gpu_irq.o gpu_health.o gpu_reset.o gpu_recovery.o gpu_memcheck.o gpu_dma.o gpu_resource.o gpu_client.o gpu_arbitration.o gpu_handle.o gpu_user_client.o gpu_event_user.o gpu_syscall.o gpu_abi.o app_supervisor.o color_lcd.o cpu_smp.o

all: check-toolchain external-apps $(ISO)

external-apps:
	$(MAKE) -C apps/hello

check-toolchain:
	@command -v $(CC) >/dev/null 2>&1 || { echo "Missing compiler: $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null 2>&1 || { echo "Missing linker: $(LD)"; exit 1; }
	@command -v $(AS) >/dev/null 2>&1 || { echo "Missing assembler: $(AS)"; exit 1; }

check-iso-tools:
	@test -n "$(GRUBMKRESCUE)" || { echo "Missing grub-mkrescue"; exit 1; }

boot.o: boot.asm
	$(AS) -f elf64 boot.asm -o boot.o

isr.o: isr.asm
	$(AS) -f elf64 isr.asm -o isr.o

idt.o: idt.c idt.h
	$(CC) $(CFLAGS) -c idt.c -o idt.o



cpu.o: cpu.asm cpu.h
	$(AS) -f elf64 cpu.asm -o cpu.o

cpu_smp.o: cpu_smp.c cpu_smp.h serial.h
	$(CC) $(CFLAGS) -c cpu_smp.c -o cpu_smp.o

io.o: io.asm io.h
	$(AS) -f elf64 io.asm -o io.o

serial.o: serial.c serial.h io.h
	$(CC) $(CFLAGS) -c serial.c -o serial.o

ps2.o: ps2.c ps2.h io.h framebuffer.h serial.h
	$(CC) $(CFLAGS) -c ps2.c -o ps2.o

kernel.o: kernel.c framebuffer.h gui.h cpu.h serial.h ps2.h fs.h virtio_blk.h kernel_cmd.h ktrace.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

kernel_cmd.o: kernel_cmd.c kernel_cmd.h serial.h fs.h ktrace.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c kernel_cmd.c -o kernel_cmd.o

framebuffer.o: framebuffer.c framebuffer.h color_lcd.h
	$(CC) $(CFLAGS) -c framebuffer.c -o framebuffer.o

font.o: font.c font.h framebuffer.h
	$(CC) $(CFLAGS) -c font.c -o font.o

color_lcd.o: color_lcd.c color_lcd.h framebuffer.h font.h
	$(CC) $(CFLAGS) -c color_lcd.c -o color_lcd.o

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

gui.o: gui.c gui.h gui_internal.h gui_weather.h gui_calc.h gui_notifications.h gui_login.h gui_overlays.h gui_widgets.h app_framework.h settings.h text_edit.h terminal.h framebuffer.h ps2.h font.h explorer.h cpu.h rtc.h storage.h fs.h pci.h net.h app_supervisor.h color_lcd.h
	$(CC) $(CFLAGS) -c gui.c -o gui.o

gui_weather.o: gui_weather.c gui_weather.h gui_internal.h framebuffer.h fs.h rtc.h net.h http_client.h
	$(CC) $(CFLAGS) -c gui_weather.c -o gui_weather.o

gui_calc.o: gui_calc.c gui_calc.h gui_internal.h gui_widgets.h framebuffer.h font.h
	$(CC) $(CFLAGS) -c gui_calc.c -o gui_calc.o

gui_notifications.o: gui_notifications.c gui_notifications.h gui_internal.h gui_widgets.h framebuffer.h font.h ps2.h
	$(CC) $(CFLAGS) -c gui_notifications.c -o gui_notifications.o

gui_login.o: gui_login.c gui_login.h gui_internal.h framebuffer.h font.h ps2.h user.h terminal.h sound.h serial.h rtc.h fs.h settings.h io.h
	$(CC) $(CFLAGS) -c gui_login.c -o gui_login.o

gui_overlays.o: gui_overlays.c gui_overlays.h gui_internal.h gui_calc.h spotlight.h explorer.h framebuffer.h font.h ps2.h hda.h fs.h terminal.h io.h
	$(CC) $(CFLAGS) -c gui_overlays.c -o gui_overlays.o

gui_widgets.o: gui_widgets.c gui_widgets.h framebuffer.h font.h theme.h gui_internal.h ps2.h
	$(CC) $(CFLAGS) -c gui_widgets.c -o gui_widgets.o

app_framework.o: app_framework.c app_framework.h framebuffer.h font.h theme.h gui_internal.h gui_widgets.h
	$(CC) $(CFLAGS) -c app_framework.c -o app_framework.o

app_supervisor.o: app_supervisor.c app_supervisor.h framebuffer.h font.h gui_internal.h
	$(CC) $(CFLAGS) -c app_supervisor.c -o app_supervisor.o

settings.o: settings.c settings.h gui_internal.h framebuffer.h font.h net.h ports.h firewall.h color_lcd.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c settings.c -o settings.o


text_edit.o: text_edit.c text_edit.h gui_internal.h framebuffer.h font.h fs.h
	$(CC) $(CFLAGS) -c text_edit.c -o text_edit.o

terminal.o: terminal.c terminal.h firstlang.h fs.h framebuffer.h font.h serial.h rtc.h net.h ports.h firewall.h tcp.h udp.h net_socket.h http_v11.h download_manager.h ide.h app_framework.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c terminal.c -o terminal.o

firstlang.o: firstlang.c firstlang.h fs.h framebuffer.h font.h serial.h rtc.h
	$(CC) $(CFLAGS) -c firstlang.c -o firstlang.o

desktop.o: desktop.c desktop.h gui_internal.h framebuffer.h font.h explorer.h fs.h theme.h
	$(CC) $(CFLAGS) -c desktop.c -o desktop.o

theme.o: theme.c theme.h framebuffer.h
	$(CC) $(CFLAGS) -c theme.c -o theme.o

image.o: image.c image.h heap.h serial.h fs.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c image.c -o image.o

user.o: user.c user.h framebuffer.h fs.h
	$(CC) $(CFLAGS) -c user.c -o user.o

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $(KERNEL) $(OBJS)

$(FS_IMAGE): tools/build_fs.py $(USER_ELF) $(USER_CHILD_ELF) $(USER_EXEC_ELF) $(USER_BROWSER_ELF)
	python3 tools/build_fs.py $(FS_IMAGE) $(FS_IMAGE_SIZE)

$(ISO): $(KERNEL) $(FS_IMAGE) | check-iso-tools
	mkdir -p $(ISO_DIR)/boot/grub
	rm -f $(ISO_DIR)/boot/$(KERNEL) $(ISO_DIR)/boot/$(FS_IMAGE)
	cp -f $(KERNEL) $(ISO_DIR)/boot/$(KERNEL)
	cp -f $(FS_IMAGE) $(ISO_DIR)/boot/$(FS_IMAGE)
	printf '%s\n' 'set timeout=0' 'set default=0' 'set gfxmode=1920x1080x32,1600x900x32,1440x900x32,1280x800x32,1280x720x32,1024x768x32,auto' 'set gfxpayload=keep' 'menuentry "FirstOS" {' '    multiboot /boot/$(KERNEL)' '    module /boot/$(FS_IMAGE) fsimg' '    boot' '}' > $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUBMKRESCUE) -o $(ISO) $(ISO_DIR)

run: $(ISO)
	@python3 tools/scan_host_wifi.py 2>/dev/null || true
	sh tools/run_firstos.sh

run-fb: $(ISO)
	@if [ "$${FIRSTOS_VERBOSE:-0}" = "1" ]; then \
		qemu-system-x86_64 -kernel $(KERNEL) -initrd $(FS_IMAGE) -m 4G -serial stdio -vga std -display cocoa,zoom-to-fit=on; \
	else \
		qemu-system-x86_64 -kernel $(KERNEL) -initrd $(FS_IMAGE) -m 4G -serial stdio -vga std -display cocoa,zoom-to-fit=on 2>&1 | python3 -u tools/filter_errors.py; \
	fi

test-gfx-gate: $(ISO)
	python3 tools/gfx_regression_gate.py

test-gfx-buffer-gate: $(ISO)
	python3 tools/test_gfx_buffer_gate.py

test-gfx-async-gate: $(ISO)
	python3 tools/test_gfx_async_gate.py

test-gfx-recovery-gate: $(ISO)
	python3 tools/test_gfx_recovery_gate.py

test-gfx-resource-gate: $(ISO)
	python3 tools/test_gfx_resource_gate.py

test-gfx-arbitration-gate: $(ISO)
	python3 tools/test_gfx_arbitration_gate.py

test-gfx-abi-gate: $(ISO)
	python3 tools/test_gfx_abi_gate.py

clean:
	rm -rf $(OBJS) $(KERNEL) $(ISO) $(ISO_DIR) $(FS_IMAGE)

# Git Version Control & Safe Update Targets
.PHONY: git-status backup restore list-backups safe-update

ifneq ($(filter backup restore safe-update,$(firstword $(MAKECMDGOALS))),)
  MAKE_CMD_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
  $(eval $(MAKE_CMD_ARGS):;@:)
endif

git-status:
	@./tools/firstos_git.sh status

backup:
	@./tools/firstos_git.sh backup $(if $(LABEL),"$(LABEL)",$(if $(MAKE_CMD_ARGS),"$(MAKE_CMD_ARGS)",))

restore:
	@./tools/firstos_git.sh restore $(if $(TAG),"$(TAG)",$(if $(MAKE_CMD_ARGS),"$(MAKE_CMD_ARGS)",))

list-backups:
	@./tools/firstos_git.sh list

safe-update:
	@./tools/firstos_git.sh safe-update $(if $(DESC),"$(DESC)",$(word 1,$(MAKE_CMD_ARGS))) $(if $(CMD),"$(CMD)",$(word 2,$(MAKE_CMD_ARGS)))


compositor.o: compositor.c compositor.h
	$(CC) $(CFLAGS) -c compositor.c -o compositor.o

virtio_gpu.o: virtio_gpu.c virtio_gpu.h pci.h serial.h
	$(CC) $(CFLAGS) -c virtio_gpu.c -o virtio_gpu.o

virtio_gpu_2d.o: virtio_gpu_2d.c virtio_gpu_2d.h virtio_gpu_protocol.h
	$(CC) $(CFLAGS) -c virtio_gpu_2d.c -o virtio_gpu_2d.o

gpu_registry.o: gpu_registry.c gpu_registry.h gpu_driver.h serial.h
	$(CC) $(CFLAGS) -c gpu_registry.c -o gpu_registry.o

gpu_generic_fb.o: gpu_generic_fb.c gpu_generic_fb.h gpu_registry.h framebuffer.h serial.h
	$(CC) $(CFLAGS) -c gpu_generic_fb.c -o gpu_generic_fb.o

gpu_virtio.o: gpu_virtio.c gpu_virtio.h gpu_registry.h virtio_gpu.h virtio_gpu_2d.h framebuffer.h serial.h
	$(CC) $(CFLAGS) -c gpu_virtio.c -o gpu_virtio.o

gpu_detect.o: gpu_detect.c gpu_detect.h gpu_registry.h gpu_generic_fb.h pci.h serial.h
	$(CC) $(CFLAGS) -c gpu_detect.c -o gpu_detect.o

gpu_cursor.o: gpu_cursor.c gpu_cursor.h gpu_driver.h gpu_detect.h serial.h
	$(CC) $(CFLAGS) -c gpu_cursor.c -o gpu_cursor.o

gpu_present.o: gpu_present.c gpu_present.h gpu_cursor.h gpu_driver.h gpu_detect.h compositor.h framebuffer.h serial.h
	$(CC) $(CFLAGS) -c gpu_present.c -o gpu_present.o

gpu_buffer.o: gpu_buffer.c gpu_buffer.h gpu_driver.h gpu_detect.h framebuffer.h heap.h serial.h
	$(CC) $(CFLAGS) -c gpu_buffer.c -o gpu_buffer.o

gpu_event.o: gpu_event.c gpu_event.h gpu_detect.h serial.h
	$(CC) $(CFLAGS) -c gpu_event.c -o gpu_event.o

gpu_irq.o: gpu_irq.c gpu_irq.h gpu_detect.h serial.h
	$(CC) $(CFLAGS) -c gpu_irq.c -o gpu_irq.o

gpu_health.o: gpu_health.c gpu_health.h gpu_detect.h serial.h
	$(CC) $(CFLAGS) -c gpu_health.c -o gpu_health.o

gpu_reset.o: gpu_reset.c gpu_reset.h gpu_detect.h serial.h
	$(CC) $(CFLAGS) -c gpu_reset.c -o gpu_reset.o

gpu_recovery.o: gpu_recovery.c gpu_recovery.h gpu_health.h gpu_reset.h gpu_detect.h gpu_buffer.h gpu_irq.h gpu_cursor.h serial.h
	$(CC) $(CFLAGS) -c gpu_recovery.c -o gpu_recovery.o

gpu_memcheck.o: gpu_memcheck.c gpu_memcheck.h io.h serial.h
	$(CC) $(CFLAGS) -c gpu_memcheck.c -o gpu_memcheck.o

gpu_dma.o: gpu_dma.c gpu_dma.h gpu_memcheck.h heap.h serial.h
	$(CC) $(CFLAGS) -c gpu_dma.c -o gpu_dma.o

gpu_resource.o: gpu_resource.c gpu_resource.h gpu_dma.h gpu_memcheck.h gpu_driver.h serial.h
	$(CC) $(CFLAGS) -c gpu_resource.c -o gpu_resource.o

gpu_client.o: gpu_client.c gpu_client.h gpu_resource.h serial.h
	$(CC) $(CFLAGS) -c gpu_client.c -o gpu_client.o

gpu_arbitration.o: gpu_arbitration.c gpu_arbitration.h gpu_client.h gpu_resource.h gpu_memcheck.h gpu_detect.h serial.h
	$(CC) $(CFLAGS) -c gpu_arbitration.c -o gpu_arbitration.o

gpu_handle.o: gpu_handle.c gpu_handle.h gpu_resource.h gpu_abi.h serial.h
	$(CC) $(CFLAGS) -c gpu_handle.c -o gpu_handle.o

gpu_user_client.o: gpu_user_client.c gpu_user_client.h gpu_handle.h gpu_arbitration.h gpu_client.h serial.h
	$(CC) $(CFLAGS) -c gpu_user_client.c -o gpu_user_client.o

gpu_event_user.o: gpu_event_user.c gpu_event_user.h gpu_user_client.h serial.h
	$(CC) $(CFLAGS) -c gpu_event_user.c -o gpu_event_user.o

gpu_syscall.o: gpu_syscall.c gpu_syscall.h gpu_user_client.h gpu_abi.h serial.h
	$(CC) $(CFLAGS) -c gpu_syscall.c -o gpu_syscall.o

gpu_abi.o: gpu_abi.c gpu_abi.h gpu_handle.h gpu_user_client.h gpu_event_user.h gpu_syscall.h gpu_arbitration.h gpu_resource.h gpu_detect.h gpu_health.h serial.h
	$(CC) $(CFLAGS) -c gpu_abi.c -o gpu_abi.o

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
	$(AS) -f elf64 paging.asm -o paging_asm.o
paging.o: paging.c paging.h
	$(CC) $(CFLAGS) -c paging.c -o paging.o

pic.o: pic.c pic.h io.h
	$(CC) $(CFLAGS) -c pic.c -o pic.o

task.o: task.c task.h io.h serial.h
	$(CC) $(CFLAGS) -c task.c -o task.o
ktrace.o: ktrace.c ktrace.h serial.h io.h
	$(CC) $(CFLAGS) -c ktrace.c -o ktrace.o

fault.o: fault.c fault.h task.h process.h ktrace.h serial.h app_supervisor.h
	$(CC) $(CFLAGS) -c fault.c -o fault.o
gdt_asm.o: gdt.asm
	$(AS) -f elf64 gdt.asm -o gdt_asm.o
gdt.o: gdt.c
	$(CC) $(CFLAGS) -c gdt.c -o gdt.o
faults.o: faults.asm
	$(AS) -f elf64 faults.asm -o faults.o

media_player.o: media_player.c media_player.h
	$(CC) $(CFLAGS) -c media_player.c -o media_player.o

kernel_libc.o: kernel_libc.c
	$(CC) $(CFLAGS) -I. -I sysroot/i686-elf/include -c kernel_libc.c -o kernel_libc.o

posix_syscalls.o: posix/syscalls.c
	$(CC) $(CFLAGS) -I. -I sysroot/i686-elf/include -c posix/syscalls.c -o posix_syscalls.o

posix_pthreads.o: posix/pthreads_stub.c
	$(CC) $(CFLAGS) -I. -I sysroot/i686-elf/include -c posix/pthreads_stub.c -o posix_pthreads.o

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

browser_logger.o: browser_logger.c browser_logger.h serial.h task.h io.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c browser_logger.c -o browser_logger.o

web_browser_app.o: web_browser_app.c web_browser_app.h browser_core.h framebuffer.h font.h serial.h gui.h gui_internal.h html5_engine.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c web_browser_app.c -o web_browser_app.o

browser_core.o: browser_core.c browser_core.h http_v11.h serial.h
	$(CC) $(NET_CFLAGS) -c browser_core.c -o browser_core.o

browser_network_service.o: browser_network_service.c browser_network_service.h browser_storage_service.h http_v11.h net_socket.h
	$(CC) $(NET_CFLAGS) -c browser_network_service.c -o browser_network_service.o

browser_renderer_pipeline.o: browser_renderer_pipeline.c browser_renderer_pipeline.h html5_engine.h browser_host_ipc.h
	$(CC) $(NET_CFLAGS) -c browser_renderer_pipeline.c -o browser_renderer_pipeline.o

browser_storage_service.o: browser_storage_service.c browser_storage_service.h cookies.h download_manager.h browser_storage.h
	$(CC) $(NET_CFLAGS) -c browser_storage_service.c -o browser_storage_service.o

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

data_url.o: data_url.c data_url.h io.h
	$(CC) $(NET_CFLAGS) -c data_url.c -o data_url.o

browser_storage.o: browser_storage.c browser_storage.h fs.h serial.h clock.h cookies.h browser_host_ipc.h
	$(CC) $(NET_CFLAGS) -I. -c browser_storage.c -o browser_storage.o

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

clock.o: clock.c clock.h io.h serial.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c clock.c -o clock.o

vfs_fd.o: vfs_fd.c vfs_fd.h fs.h heap.h serial.h clock.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c vfs_fd.c -o vfs_fd.o

platform_chromium_base.o: platform_chromium_base.c include/firstos/platform/chromium_port_base.h clock.h heap.h vfs_fd.h framebuffer.h compositor.h task.h
	$(CC) $(CFLAGS) -I. -I sysroot/i686-elf/include -c platform_chromium_base.c -o platform_chromium_base.o

ring3_test.o: ring3_test.c ring3_test.h task.h paging.h heap.h serial.h
	$(CC) $(CFLAGS) -I sysroot/i686-elf/include -c ring3_test.c -o ring3_test.o

USER_APP_DIR = userspace/apps/userspace_hello
USER_ELF = $(USER_APP_DIR)/userspace_hello.elf

USER_CHILD_DIR = userspace/apps/userspace_child
USER_CHILD_ELF = $(USER_CHILD_DIR)/userspace_child.elf

USER_EXEC_DIR = userspace/apps/userspace_exec_target
USER_EXEC_ELF = $(USER_EXEC_DIR)/userspace_exec_target.elf

USER_CFLAGS = -m64 -mcmodel=small -mno-red-zone -ffreestanding -fno-pic -fno-pie -fno-stack-protector -nostdlib -Wall -Wextra -O2

userspace/build/crt0.o: userspace/crt0/crt0.S
	@mkdir -p userspace/build
	$(AS) -f elf64 userspace/crt0/crt0.S -o userspace/build/crt0.o

userspace/build/syscall.o: userspace/lib/syscall.c userspace/include/firstos/syscall.h include/firstos/uapi/syscall.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -c userspace/lib/syscall.c -o userspace/build/syscall.o

userspace/build/main.o: $(USER_APP_DIR)/main.c userspace/include/firstos/syscall.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -c $(USER_APP_DIR)/main.c -o userspace/build/main.o

userspace/build/child_main.o: $(USER_CHILD_DIR)/main.c userspace/include/firstos/syscall.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -c $(USER_CHILD_DIR)/main.c -o userspace/build/child_main.o

userspace/build/exec_main.o: $(USER_EXEC_DIR)/main.c userspace/include/firstos/syscall.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -c $(USER_EXEC_DIR)/main.c -o userspace/build/exec_main.o

userspace/build/malloc.o: userspace/lib/malloc.c userspace/include/firstos/syscall.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -c userspace/lib/malloc.c -o userspace/build/malloc.o

$(USER_ELF): userspace/build/crt0.o userspace/build/syscall.o userspace/build/malloc.o userspace/build/main.o $(USER_APP_DIR)/linker.ld
	$(LD) -m elf_x86_64 -T $(USER_APP_DIR)/linker.ld userspace/build/crt0.o userspace/build/syscall.o userspace/build/malloc.o userspace/build/main.o -o $(USER_ELF)

$(USER_CHILD_ELF): userspace/build/crt0.o userspace/build/syscall.o userspace/build/malloc.o userspace/build/child_main.o $(USER_APP_DIR)/linker.ld
	$(LD) -m elf_x86_64 -T $(USER_APP_DIR)/linker.ld userspace/build/crt0.o userspace/build/syscall.o userspace/build/malloc.o userspace/build/child_main.o -o $(USER_CHILD_ELF)

$(USER_EXEC_ELF): userspace/build/crt0.o userspace/build/syscall.o userspace/build/malloc.o userspace/build/exec_main.o $(USER_APP_DIR)/linker.ld
	$(LD) -m elf_x86_64 -T $(USER_APP_DIR)/linker.ld userspace/build/crt0.o userspace/build/syscall.o userspace/build/malloc.o userspace/build/exec_main.o -o $(USER_EXEC_ELF)

USER_BROWSER_DIR = userspace/apps/browser_renderer
USER_BROWSER_ELF = $(USER_BROWSER_DIR)/browser_renderer.elf

USER_BROWSER_OBJS = \
	userspace/build/browser_renderer_main.o \
	userspace/build/browser_dom_adapter.o \
	userspace/build/browser_html_adapter.o \
	userspace/build/browser_css_adapter.o \
	userspace/build/browser_layout_adapter.o \
	userspace/build/browser_render_adapter.o \
	userspace/build/browser_event_adapter.o \
	userspace/build/browser_form_adapter.o \
	userspace/build/browser_js_adapter.o \
	userspace/build/browser_ax_adapter.o \
	userspace/build/html5_arena_user.o

userspace/build/browser_renderer_main.o: $(USER_BROWSER_DIR)/main.c userspace/include/firstos/syscall.h include/firstos/uapi/browser_ipc.h userspace/browser/include/firstos_renderer.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -I. -Iuserspace/browser/include -c $(USER_BROWSER_DIR)/main.c -o userspace/build/browser_renderer_main.o

userspace/build/browser_dom_adapter.o: userspace/browser/dom_adapter.c userspace/browser/include/firstos_renderer.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -I. -Iuserspace/browser/include -c userspace/browser/dom_adapter.c -o userspace/build/browser_dom_adapter.o

userspace/build/browser_html_adapter.o: userspace/browser/html_adapter.c userspace/browser/include/firstos_renderer.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -I. -Iuserspace/browser/include -c userspace/browser/html_adapter.c -o userspace/build/browser_html_adapter.o

userspace/build/browser_css_adapter.o: userspace/browser/css_adapter.c userspace/browser/include/firstos_renderer.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -I. -Iuserspace/browser/include -c userspace/browser/css_adapter.c -o userspace/build/browser_css_adapter.o

userspace/build/browser_layout_adapter.o: userspace/browser/layout_adapter.c userspace/browser/include/firstos_renderer.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -I. -Iuserspace/browser/include -c userspace/browser/layout_adapter.c -o userspace/build/browser_layout_adapter.o

userspace/build/browser_render_adapter.o: userspace/browser/render_adapter.c userspace/browser/include/firstos_renderer.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -I. -Iuserspace/browser/include -c userspace/browser/render_adapter.c -o userspace/build/browser_render_adapter.o

userspace/build/browser_event_adapter.o: userspace/browser/event_adapter.c userspace/browser/include/firstos_renderer.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -I. -Iuserspace/browser/include -c userspace/browser/event_adapter.c -o userspace/build/browser_event_adapter.o

userspace/build/browser_form_adapter.o: userspace/browser/form_adapter.c userspace/browser/include/firstos_renderer.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -I. -Iuserspace/browser/include -c userspace/browser/form_adapter.c -o userspace/build/browser_form_adapter.o

userspace/build/browser_js_adapter.o: userspace/browser/js_adapter.c userspace/browser/include/firstos_renderer.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -I. -Iuserspace/browser/include -c userspace/browser/js_adapter.c -o userspace/build/browser_js_adapter.o

userspace/build/browser_ax_adapter.o: userspace/browser/ax_adapter.c userspace/browser/include/firstos_renderer.h include/firstos/accessibility/ax_tree.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -I. -Iuserspace/browser/include -c userspace/browser/ax_adapter.c -o userspace/build/browser_ax_adapter.o

userspace/build/html5_arena_user.o: html5_arena.c html5_arena.h
	@mkdir -p userspace/build
	$(CC) $(USER_CFLAGS) -DFIRSTOS_USERSPACE -I. -c html5_arena.c -o userspace/build/html5_arena_user.o

$(USER_BROWSER_ELF): userspace/build/crt0.o userspace/build/syscall.o userspace/build/malloc.o $(USER_BROWSER_OBJS) $(USER_BROWSER_DIR)/linker.ld
	$(LD) -m elf_x86_64 -T $(USER_BROWSER_DIR)/linker.ld userspace/build/crt0.o userspace/build/syscall.o userspace/build/malloc.o $(USER_BROWSER_OBJS) -o $(USER_BROWSER_ELF)

userspace_hello_bin.o: userspace_hello_bin.asm $(USER_ELF)
	$(AS) -f elf64 userspace_hello_bin.asm -o userspace_hello_bin.o

process.o: process.c process.h task.h vfs_fd.h heap.h fs.h serial.h userspace_loader.h pipe.h
	$(CC) $(CFLAGS) -I. -I sysroot/i686-elf/include -c process.c -o process.o

pipe.o: pipe.c pipe.h serial.h
	$(CC) $(CFLAGS) -I. -I sysroot/i686-elf/include -c pipe.c -o pipe.o

shm.o: shm.c shm.h process.h paging.h heap.h serial.h
	$(CC) $(CFLAGS) -I. -I sysroot/i686-elf/include -c shm.c -o shm.o

futex.o: futex.c futex.h serial.h
	$(CC) $(CFLAGS) -I. -I sysroot/i686-elf/include -c futex.c -o futex.o

browser_host_ipc.o: browser_host_ipc.c browser_host_ipc.h include/firstos/uapi/browser_ipc.h shm.h process.h framebuffer.h font.h serial.h
	$(CC) $(CFLAGS) -I. -I sysroot/i686-elf/include -c browser_host_ipc.c -o browser_host_ipc.o

browser_security.o: browser_security.c include/firstos/security/browser_security.h
	$(CC) $(CFLAGS) -I. -I sysroot/i686-elf/include -c browser_security.c -o browser_security.o

userspace_loader.o: userspace_loader.c userspace_loader.h task.h paging.h heap.h serial.h
	$(CC) $(CFLAGS) -I. -I sysroot/i686-elf/include -c userspace_loader.c -o userspace_loader.o

