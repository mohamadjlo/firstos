import subprocess
import time
import socket
import os
import sys
from PIL import Image

CWD = "/Users/mohamadjlo/Desktop/FirstOS"

def run_qemu_test(w, h, name):
    sock = f"/tmp/qmp_{name}.sock"
    if os.path.exists(sock): os.unlink(sock)
    ppm = f"{CWD}/virtio_{name}.ppm"
    png = f"{CWD}/virtio_{name}.png"
    if os.path.exists(ppm): os.unlink(ppm)
    if os.path.exists(png): os.unlink(png)

    cmd = [
        'qemu-system-i386',
        '-cdrom', f'{CWD}/firstos.iso',
        '-m', '1024M',
        '-serial', 'stdio',
        '-vga', 'none',
        '-device', f'virtio-gpu-pci,xres={w},yres={h}',
        '-display', 'none',
        '-monitor', f'unix:{sock},server,nowait'
    ]
    print(f"\n=======================================================")
    print(f"[TEST] Starting QEMU for {name} ({w}x{h})...")
    p = subprocess.Popen(cmd, cwd=CWD, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(1)

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock)

    # Read serial lines until gui_draw done
    start = time.time()
    booted = False
    fps_reports = []
    while time.time() - start < 15:
        line = p.stdout.readline().decode('utf-8', errors='ignore')
        if line:
            l = line.strip()
            if any(k in l.lower() for k in ['virtio', 'framebuffer', 'gui_draw done', 'display info ok', '2d init']):
                print(f"  [SERIAL] {l}")
            if 'gui_draw done' in l:
                booted = True
                break

    if not booted:
        print("[TEST] Warning: did not see gui_draw done within 15s, taking screenshot anyway...")

    time.sleep(2)
    s.sendall(f"screendump {ppm}\n".encode())
    time.sleep(2)

    s.close()
    p.kill()
    if os.path.exists(sock): os.unlink(sock)

    if os.path.exists(ppm):
        im = Image.open(ppm)
        im.save(png)
        print(f"[TEST] SUCCESS: {png} created! Dimensions: {im.size[0]}x{im.size[1]}, File size: {os.path.getsize(png)} bytes")
        return im.size
    else:
        print(f"[TEST] FAILED: {ppm} was not generated!")
        return None

if __name__ == '__main__':
    sz1 = run_qemu_test(1280, 800, "1280x800")
    sz2 = run_qemu_test(1920, 1080, "1920x1080")
    print("\n================ SUMMARY ================")
    print("1280x800 test result:", sz1)
    print("1920x1080 test result:", sz2)
