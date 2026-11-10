import subprocess, time, socket, os, shutil
from PIL import Image

CWD = "/Users/mohamadjlo/Desktop/FirstOS"
ARTIFACT_DIR = "/Users/mohamadjlo/.gemini/antigravity-cli/brain/c1784375-9c7f-4dde-a143-959a43958b0d"

def do_capture(w, h, tag):
    sock = f"/tmp/sock_{tag}.sock"
    ppm = f"{CWD}/screen_{tag}.ppm"
    png_local = f"{CWD}/screen_{tag}.png"
    png_artifact = f"{ARTIFACT_DIR}/screen_{tag}.png"
    for f in [sock, ppm, png_local, png_artifact]:
        if os.path.exists(f):
            try: os.unlink(f)
            except: pass

    cmd = [
        'qemu-system-i386', '-cdrom', f'{CWD}/firstos.iso', '-m', '1024M',
        '-serial', 'stdio', '-vga', 'none',
        '-device', f'virtio-gpu-pci,xres={w},yres={h}',
        '-display', 'none',
        '-monitor', f'unix:{sock},server,nowait'
    ]
    p = subprocess.Popen(cmd, cwd=CWD, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(1)

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock)
    s.settimeout(10.0)
    s.recv(1024)

    start = time.time()
    while time.time() - start < 15:
        line = p.stdout.readline().decode('utf-8', errors='ignore')
        if 'gui_draw done' in line:
            break

    # Measure FPS
    frames = 0
    t0 = time.time()
    while time.time() - t0 < 1.0:
        line = p.stdout.readline().decode('utf-8', errors='ignore')
        if 'frame end' in line: frames += 1
    t1 = time.time()
    fps = frames / (t1 - t0)

    # Screendump
    s.sendall(f"screendump {ppm}\n".encode())
    resp = b""
    while b"(qemu)" not in resp:
        resp += s.recv(1024)

    s.close()
    p.kill()
    if os.path.exists(sock): os.unlink(sock)

    if os.path.exists(ppm):
        im = Image.open(ppm)
        im.save(png_local)
        shutil.copy2(png_local, png_artifact)
        os.unlink(ppm)
        print(f"[{tag}] OK: local={png_local} ({os.path.getsize(png_local)}B), artifact={png_artifact} ({os.path.getsize(png_artifact)}B), size={im.size}, FPS={fps:.1f}")
    else:
        print(f"[{tag}] FAILED: ppm not found")

do_capture(1280, 800, "1280x800")
do_capture(1920, 1080, "1920x1080")
