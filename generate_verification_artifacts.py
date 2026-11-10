import subprocess
import time
import socket
import os
from PIL import Image

ARTIFACT_DIR = "/Users/mohamadjlo/.gemini/antigravity-cli/brain/c1784375-9c7f-4dde-a143-959a43958b0d"
FIRSTOS_DIR = "/Users/mohamadjlo/Desktop/FirstOS"

def capture_resolution(w, h, name):
    sock_path = f"/tmp/qmp_res_{name}.sock"
    ppm_path = f"/tmp/res_{name}.ppm"
    png_path = f"{ARTIFACT_DIR}/virtio_{name}.png"
    if os.path.exists(sock_path): os.unlink(sock_path)
    if os.path.exists(ppm_path): os.unlink(ppm_path)

    cmd = [
        'qemu-system-i386',
        '-cdrom', f'{FIRSTOS_DIR}/firstos.iso',
        '-m', '1024M',
        '-serial', 'stdio',
        '-vga', 'none',
        '-device', f'virtio-gpu-pci,xres={w},yres={h}',
        '-display', 'none',
        '-monitor', f'unix:{sock_path},server,nowait'
    ]

    print(f"\n=======================================================")
    print(f"[TEST] Launching QEMU for {name} ({w}x{h})...")
    p = subprocess.Popen(cmd, cwd=FIRSTOS_DIR, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(1)

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock_path)
    s.settimeout(5.0)

    # Wait until boot completes
    start = time.time()
    while time.time() - start < 15:
        line = p.stdout.readline().decode('utf-8', errors='ignore')
        if line:
            l = line.strip()
            if any(k in l.lower() for k in ['virtio', 'framebuffer', 'gui_draw done']):
                print(f"  {l}")
            if 'gui_draw done' in l:
                break

    # Now let it render frames for 2 seconds and measure frame rate
    print("[TEST] Measuring rendering performance for 2 seconds...")
    frame_count = 0
    t_measure_start = time.time()
    while time.time() - t_measure_start < 2.0:
        line = p.stdout.readline().decode('utf-8', errors='ignore')
        if line and 'frame end' in line:
            frame_count += 1
    t_measure_end = time.time()
    measured_fps = frame_count / (t_measure_end - t_measure_start)
    print(f"[TEST] Measured rendering rate: {measured_fps:.1f} FPS ({frame_count} frames in {t_measure_end - t_measure_start:.2f}s)")

    # Send screendump command to QEMU monitor
    s.sendall(f"screendump {ppm_path}\n".encode())
    time.sleep(1)
    s.recv(1024)

    s.close()
    p.kill()
    if os.path.exists(sock_path): os.unlink(sock_path)

    if os.path.exists(ppm_path):
        im = Image.open(ppm_path)
        im.save(png_path)
        print(f"[TEST] Created artifact: {png_path}")
        print(f"[TEST] Image size: {im.size[0]}x{im.size[1]}, File size: {os.path.getsize(png_path)} bytes")
        os.unlink(ppm_path)
        return im.size, measured_fps
    else:
        print(f"[TEST] Failed: {ppm_path} not created")
        return None, measured_fps

if __name__ == '__main__':
    r1, fps1 = capture_resolution(1280, 800, "1280x800")
    r2, fps2 = capture_resolution(1920, 1080, "1920x1080")
    print("\n================ FINAL RESULTS ================")
    print(f"1280x800: Resolution = {r1}, FPS = {fps1:.1f}")
    print(f"1920x1080: Resolution = {r2}, FPS = {fps2:.1f}")
