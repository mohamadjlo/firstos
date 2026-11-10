import subprocess
import time
import socket
import os
from PIL import Image

ARTIFACT_DIR = "/Users/mohamadjlo/.gemini/antigravity-cli/brain/c1784375-9c7f-4dde-a143-959a43958b0d"
FIRSTOS_DIR = "/Users/mohamadjlo/Desktop/FirstOS"
sock_path = "/tmp/qmp_switch.sock"
ppm_boot = "/tmp/switch_boot.ppm"
ppm_switched = "/tmp/switch_after.ppm"
png_boot = f"{ARTIFACT_DIR}/runtime_switch_1024.png"
png_switched = f"{ARTIFACT_DIR}/runtime_switch_1920.png"

for p in [sock_path, ppm_boot, ppm_switched]:
    if os.path.exists(p): os.unlink(p)

cmd = [
    'qemu-system-i386',
    '-cdrom', f'{FIRSTOS_DIR}/firstos.iso',
    '-m', '1024M',
    '-serial', 'stdio',
    '-vga', 'none',
    '-device', 'virtio-gpu-pci,xres=1024,yres=768',
    '-usb', '-device', 'usb-tablet',
    '-display', 'none',
    '-monitor', f'unix:{sock_path},server,nowait'
]

print("[TEST] Launching QEMU at 1024x768...")
p = subprocess.Popen(cmd, cwd=FIRSTOS_DIR, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
time.sleep(1)

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock_path)
s.settimeout(5.0)

# Wait until boot completes
start = time.time()
while time.time() - start < 15:
    line = p.stdout.readline().decode('utf-8', errors='ignore')
    if line and 'gui_draw done' in line:
        break

time.sleep(1)
# Screendump initial 1024x768
s.sendall(f"screendump {ppm_boot}\n".encode())
time.sleep(1)
s.recv(1024)
if os.path.exists(ppm_boot):
    im = Image.open(ppm_boot)
    im.save(png_boot)
    print(f"[TEST] Boot screenshot saved: {im.size[0]}x{im.size[1]}")
    os.unlink(ppm_boot)

# Open Settings with Alt+S
print("[TEST] Sending Alt+S to open Settings...")
s.sendall(b"sendkey alt-s\n")
time.sleep(1)
s.recv(1024)

# Click Tab 6 (Display) in sidebar at x=244, y=342
# In QEMU usb-tablet mode: mouse_move x y (0-32767 coordinates normalized)
# 244 / 1024 * 32767 = 7808, 342 / 768 * 32767 = 14592
print("[TEST] Clicking Tab 6 (Display)...")
s.sendall(b"mouse_move 7808 14592\n")
time.sleep(0.3)
s.sendall(b"mouse_button 1\n")
time.sleep(0.1)
s.sendall(b"mouse_button 0\n")
time.sleep(1)
s.recv(1024)

# Click 1920x1080 pill: x=770, y=220
# 770 / 1024 * 32767 = 24634, 220 / 768 * 32767 = 9385
print("[TEST] Clicking 1920x1080 resolution pill...")
s.sendall(b"mouse_move 24634 9385\n")
time.sleep(0.3)
s.sendall(b"mouse_button 1\n")
time.sleep(0.1)
s.sendall(b"mouse_button 0\n")
time.sleep(2)
s.recv(1024)

# Check serial log for resolution switch message
t_check = time.time()
resolution_updated = False
while time.time() - t_check < 3.0:
    line = p.stdout.readline().decode('utf-8', errors='ignore')
    if line:
        l = line.strip()
        if any(k in l.lower() for k in ['virtio', 'framebuffer', 'resolution', '2d']):
            print(f"  [RUNTIME SERIAL] {l}")
        if 'resolution updated ok' in l or 'fb_set_resolution: ok' in l:
            resolution_updated = True

# Screendump after switch
s.sendall(f"screendump {ppm_switched}\n".encode())
time.sleep(1)
s.recv(1024)

s.close()
p.kill()
if os.path.exists(sock_path): os.unlink(sock_path)

if os.path.exists(ppm_switched):
    im2 = Image.open(ppm_switched)
    im2.save(png_switched)
    print(f"[TEST] Switched screenshot saved: {im2.size[0]}x{im2.size[1]}")
    os.unlink(ppm_switched)
    print("\n[RESULT] Dynamic resolution switch:", "SUCCESS" if im2.size == (1920, 1080) else f"Size is {im2.size}")
else:
    print("[TEST] Failed to capture switched screenshot")
