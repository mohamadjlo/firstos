import subprocess
import time
import socket
import os

p = subprocess.Popen([
    'qemu-system-i386', '-cdrom', 'firstos.iso', '-m', '256M',
    '-serial', 'file:test_taskman.log', '-vga', 'none',
    '-device', 'virtio-gpu-pci,xres=1024,yres=768', '-display', 'none',
    '-d', 'guest_errors', '-monitor', 'unix:qemu-monitor.sock,server,nowait'
], stderr=subprocess.PIPE)

time.sleep(2)
# Connect to monitor
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('qemu-monitor.sock')
s.sendall(b'mouse_move 512 384\n') # center
time.sleep(1)
# Task manager button is roughly at bottom toolbar. Let's say x=400 y=750
s.sendall(b'mouse_move 400 750\n')
time.sleep(0.5)
s.sendall(b'mouse_button 1\n')
time.sleep(0.1)
s.sendall(b'mouse_button 0\n')
time.sleep(2)

p.kill()
err = p.stderr.read().decode()
open('taskman_err.log', 'w').write(err)
os.unlink('qemu-monitor.sock')
