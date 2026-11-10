#!/usr/bin/env python3
import os
import sys
import time
import json
import socket
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
iso_path = REPO_ROOT / "firstos.iso"
fs_path = REPO_ROOT / "fs.img"
ppm_path = Path("/tmp/browser_screen_diag.ppm")
qmp_sock = Path("/tmp/qmp_diag.sock")

if qmp_sock.exists():
    qmp_sock.unlink()
if ppm_path.exists():
    ppm_path.unlink()

cmd = [
    "qemu-system-x86_64",
    "-drive", f"file={iso_path},format=raw,media=cdrom,readonly=on",
    "-boot", "d",
    "-m", "2G",
    "-serial", "stdio",
    "-display", "none",
    "-vga", "none",
    "-device", "virtio-gpu-pci,xres=1024,yres=768",
    "-drive", f"file={fs_path},format=raw,if=none,id=fsdisk",
    "-device", "virtio-blk-pci,drive=fsdisk",
    "-netdev", "user,id=net0",
    "-device", "virtio-net-pci,netdev=net0",
    "-qmp", f"unix:{qmp_sock},server,nowait",
    "-no-reboot"
]

print("[diag] Starting QEMU...")
proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=False)

nav_sent = False
rendered = False
start_t = time.time()

while time.time() - start_t < 25:
    line = proc.stdout.readline()
    if not line: break
    txt = line.decode('latin1', errors='replace')
    sys.stdout.write(txt)
    sys.stdout.flush()

    if ('gui_draw done' in txt or 'userspace_exec_target] Exiting cleanly' in txt) and not nav_sent:
        print("\n[diag] FirstOS GUI ready. Sending NAV:about:test...")
        try:
            proc.stdin.write(b"NAV:about:test\n")
            proc.stdin.flush()
            nav_sent = True
        except Exception as e:
            print(f"[diag] stdin error: {e}")

    if 'validated and executed render commands' in txt and nav_sent:
        print("\n[diag] Render commands executed by Host! Waiting 1s for compositing...")
        rendered = True
        time.sleep(1.0)
        break

print("[diag] Taking screendump via QMP...")
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(str(qmp_sock))
s.recv(1024)
s.sendall(json.dumps({"execute": "qmp_capabilities"}).encode() + b"\n")
s.recv(1024)
s.sendall(json.dumps({"execute": "screendump", "arguments": {"filename": str(ppm_path)}}).encode() + b"\n")
s.recv(1024)
s.close()

proc.terminate()
try:
    proc.wait(timeout=2)
except Exception:
    proc.kill()

if not ppm_path.exists():
    print("[FAIL] PPM screendump not created!")
    sys.exit(1)

with open(ppm_path, "rb") as f:
    magic = f.readline().strip()
    dims = f.readline().strip()
    maxval = f.readline().strip()
    pixel_data = f.read()

w, h = map(int, dims.split())
print(f"[diag] Screen size: {w}x{h}")

# The browser window is centered or positioned on screen.
# Desktop is 1024x768.
# Let's inspect rows from y=100 to y=600, x=50 to x=800.
# In PPM, each pixel is 3 bytes (R, G, B).
# Let's sample the center of the screen (e.g. x=500, y=300).
sample_colors = {}
for y in range(150, 500, 20):
    for x in range(100, 700, 20):
        idx = (y * w + x) * 3
        r = pixel_data[idx]
        g = pixel_data[idx+1]
        b = pixel_data[idx+2]
        col = (r << 16) | (g << 8) | b
        sample_colors[col] = sample_colors.get(col, 0) + 1

print("[diag] Sampled colors in browser area:")
for col, cnt in sorted(sample_colors.items(), key=lambda item: item[1], reverse=True)[:10]:
    print(f"  Color 0x{col:06X}: count {cnt}")

