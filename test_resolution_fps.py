import subprocess
import time
import socket
import os
import sys
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

def run_test(boot_w=1280, boot_h=800, test_name="1280x800"):
    sock_path = f"qemu_monitor_{test_name}.sock"
    serial_log = f"serial_{test_name}.log"
    if os.path.exists(sock_path):
        try: os.unlink(sock_path)
        except: pass
    if os.path.exists(serial_log):
        try: os.unlink(serial_log)
        except: pass

    cmd = [
        'qemu-system-i386',
        '-cdrom', 'firstos.iso',
        '-m', '1024M',
        '-serial', f'file:{serial_log}',
        '-vga', 'none',
        '-device', f'virtio-gpu-pci,xres={boot_w},yres={boot_h}',
        '-display', 'none',
        '-monitor', f'unix:{sock_path},server,nowait'
    ]
    print(f"[TEST] Launching QEMU for {test_name} with command: {' '.join(cmd)}")
    p = subprocess.Popen(cmd, stderr=subprocess.PIPE)

    # Wait for socket to be ready
    connected = False
    s = None
    for _ in range(30):
        time.sleep(0.5)
        if os.path.exists(sock_path):
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(sock_path)
                connected = True
                break
            except Exception as e:
                pass

    if not connected or not s:
        print(f"[TEST] Failed to connect to QEMU monitor socket")
        p.kill()
        return False

    print(f"[TEST] Connected to QEMU monitor. Waiting 8s for OS boot...")
    time.sleep(8)

    # Read monitor banner
    s.settimeout(2.0)
    try:
        data = s.recv(4096)
    except:
        pass

    # Take initial screenshot
    ppm1 = f"screen_{test_name}_boot.ppm"
    png1 = f"screen_{test_name}_boot.png"
    s.sendall(f"screendump {ppm1}\n".encode())
    time.sleep(2)
    sz1 = convert_ppm_to_png(ppm1, png1)
    print(f"[TEST] Boot screenshot size: {sz1}")

    # Check serial log
    if os.path.exists(serial_log):
        with open(serial_log, 'r', errors='ignore') as f:
            lines = f.readlines()
        print(f"[TEST] Serial log had {len(lines)} lines")
        virtio_lines = [l.strip() for l in lines if 'virtio' in l.lower() or 'framebuffer' in l.lower() or 'fps' in l.lower() or 'resolution' in l.lower()]
        for l in virtio_lines[-20:]:
            print(f"  [LOG] {l}")

    s.close()
    p.kill()
    try: os.unlink(sock_path)
    except: pass
    return True

if __name__ == '__main__':
    print("=== TEST 1: Boot at 1280x800 ===")
    run_test(1280, 800, "1280x800")

    print("\n=== TEST 2: Boot at 1920x1080 ===")
    run_test(1920, 1080, "1920x1080")
