import subprocess
import time
import socket
import os
from PIL import Image

def convert_ppm_to_png(ppm_path, png_path):
    try:
        im = Image.open(ppm_path)
        im.save(png_path)
        print(f"[TEST] Saved {png_path} ({im.size[0]}x{im.size[1]})")
        return im.size
    except Exception as e:
        print(f"[TEST] Error converting {ppm_path} to {png_path}: {e}")
        return None

def test_resolution(w, h, name):
    sock_path = f"qmp_{name}.sock"
    serial_path = f"serial_{name}.log"
    if os.path.exists(sock_path): os.unlink(sock_path)
    if os.path.exists(serial_path): os.unlink(serial_path)

    cmd = [
        'qemu-system-i386',
        '-kernel', 'kernel.bin',
        '-initrd', 'fs.img',
        '-m', '1024M',
        '-serial', f'file:{serial_path}',
        '-vga', 'none',
        '-device', f'virtio-gpu-pci,xres={w},yres={h}',
        '-display', 'none',
        '-monitor', f'unix:{sock_path},server,nowait'
    ]
    p = subprocess.Popen(cmd)
    time.sleep(1)

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock_path)
    time.sleep(3)

    ppm = f"screen_{name}.ppm"
    png = f"screen_{name}.png"
    s.sendall(f"screendump {ppm}\n".encode())
    time.sleep(1)
    sz = convert_ppm_to_png(ppm, png)

    s.close()
    p.kill()
    if os.path.exists(sock_path): os.unlink(sock_path)
    
    with open(serial_path, 'r', errors='ignore') as f:
        log = f.read()
    print(f"[{name}] Log snippet:")
    for line in log.splitlines():
        if any(k in line.lower() for k in ['virtio', 'framebuffer', 'resolution', 'w=', 'h=']):
            print("  ", line)
    return sz

print("Testing 1280x800:")
test_resolution(1280, 800, "1280x800")

print("\nTesting 1920x1080:")
test_resolution(1920, 1080, "1920x1080")
