qemu-system-i386 -kernel kernel.bin -initrd fs.img -m 4G -serial stdio -vga none -device virtio-gpu-pci,xres=1024,yres=768 -usb -device usb-tablet -audiodev coreaudio,id=snd0 -device intel-hda -device hda-output,audiodev=snd0 -drive file=fs.img,format=raw,if=none,id=fsdisk -device virtio-blk-pci,drive=fsdisk -display cocoa,zoom-to-fit=on -d guest_errors -trace "hda*" &
PID=$!
sleep 5
kill $PID
