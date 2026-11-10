#!/usr/bin/env python3
import subprocess
import time
import sys

qemu_cmd = [
    'qemu-system-i386',
    '-drive', 'file=firstos.iso,format=raw,media=cdrom,readonly=on',
    '-drive', 'file=fs.img,format=raw,if=none,id=fsdisk',
    '-device', 'virtio-blk-pci,drive=fsdisk',
    '-m', '2G',
    '-vga', 'none',
    '-device', 'virtio-gpu-pci,xres=1024,yres=768',
    '-netdev', 'user,id=net0',
    '-device', 'virtio-net-pci,netdev=net0',
    '-serial', 'stdio',
    '-display', 'none'
]

proc = subprocess.Popen(qemu_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

log_lines = []
start = time.time()
while time.time() - start < 15:
    line = proc.stdout.readline()
    if not line:
        break
    s = line.decode('latin1', errors='replace').rstrip('\r\n')
    log_lines.append(s)
    # Check if we reached after exec_target or GUI
    if 'userspace_exec_target] Exiting cleanly' in s or 'PAGE FAULT' in s or '#PF' in s or 'gui_draw done' in s:
        # Collect a few more lines
        for _ in range(30):
            l = proc.stdout.readline()
            if l:
                log_lines.append(l.decode('latin1', errors='replace').rstrip('\r\n'))
        break

proc.terminate()
try:
    proc.wait(timeout=2)
except Exception:
    proc.kill()

with open('/tmp/firstos_phase3c_test.log', 'w') as f:
    for line in log_lines:
        f.write(line + '\n')

print(f"Captured {len(log_lines)} lines to /tmp/firstos_phase3c_test.log")
for line in log_lines[-50:]:
    print(line)
