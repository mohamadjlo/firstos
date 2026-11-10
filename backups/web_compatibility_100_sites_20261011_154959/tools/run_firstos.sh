#!/bin/sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
proxy_log=${FIRSTOS_PROXY_LOG:-/tmp/firstos-webproxy.log}

python3 "$root_dir/tools/firstos_webproxy.py" --bind 127.0.0.1 --port 18080 >"$proxy_log" 2>&1 &
proxy_pid=$!

cleanup() {
    kill "$proxy_pid" 2>/dev/null || true
    wait "$proxy_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 0.2

qemu-system-x86_64 \
    -drive file="$root_dir/firstos.iso",format=raw,media=cdrom,readonly=on \
    -boot d \
    -m 2G \
    -serial stdio \
    -vga none \
    -device virtio-gpu-pci,xres=1024,yres=768 \
    -usb \
    -device usb-tablet \
    -audiodev coreaudio,id=snd0,out.frequency=44100 \
    -device intel-hda \
    -device hda-output,audiodev=snd0 \
    -drive file="$root_dir/fs.img",format=raw,if=none,id=fsdisk \
    -device virtio-blk-pci,drive=fsdisk \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -display cocoa,zoom-to-fit=on
