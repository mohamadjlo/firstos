#!/usr/bin/env python3
import os
import sys
import time
import socket
import subprocess
import threading

MON_SOCK = "/tmp/qemu_mon.sock"
SERIAL_LOG = "/tmp/firstos_multi_site.log"

SITES = [
    ("wikipedia", "https://en.wikipedia.org"),
    ("microsoft", "https://microsoft.com"),
    ("apple", "https://apple.com"),
    ("google", "https://google.com"),
    ("facebook", "https://m.facebook.com"),
    ("youtube", "https://youtube.com"),
]

def main():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    iso_path = os.path.join(root_dir, "firstos.iso")

    if os.path.exists(MON_SOCK):
        os.remove(MON_SOCK)

    qemu_cmd = [
        "qemu-system-i386",
        "-drive", f"file={iso_path},format=raw,media=cdrom,readonly=on",
        "-m", "2G",
        "-vga", "none",
        "-device", "virtio-gpu-pci,xres=1024,yres=768",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-serial", "stdio",
        "-monitor", f"unix:{MON_SOCK},server,nowait",
        "-display", "none"
    ]

    print("[harness] Starting QEMU...")
    proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    log_file = open(SERIAL_LOG, "w", encoding="utf-8")
    all_lines = []

    def reader():
        while True:
            line = proc.stdout.readline()
            if not line:
                break
            txt = line.decode("latin1", errors="replace").rstrip("\r\n")
            log_file.write(txt + "\n")
            log_file.flush()
            all_lines.append(txt)

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    # Wait for boot
    print("[harness] Waiting for boot...")
    start = time.time()
    booted = False
    while time.time() - start < 15.0:
        if any("gui_draw done" in x for x in all_lines):
            booted = True
            break
        time.sleep(0.2)

    if not booted:
        print("[harness] Boot failed!")
        proc.kill()
        return 1

    time.sleep(2.0)

    # Connect to monitor
    mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    mon.connect(MON_SOCK)
    mon.settimeout(5.0)
    time.sleep(0.5)
    try:
        mon.recv(1024)
    except Exception:
        pass

    for name, url in SITES:
        print(f"\n=======================================================")
        print(f"[harness] Navigating to: {url} ({name})")
        print(f"=======================================================")

        mark = len(all_lines)
        cmd = f"NAV:{url}\n".encode("utf-8")
        proc.stdin.write(cmd)
        proc.stdin.flush()

        # Wait for render or timeout
        nav_start = time.time()
        rendered = False
        while time.time() - nav_start < 40.0:
            time.sleep(0.5)
            new_lines = all_lines[mark:]
            for l in new_lines:
                if any(k in l for k in ["[resource]", "[html5_script]", "[proxy_bridge] host bridge status", "Layout pass complete"]):
                    pass
            if any("response rendered by native HTML5 pipeline" in l or "native offline view" in l or "response failed" in l for l in new_lines):
                # Wait 3s more for subresources & rendering to settle
                time.sleep(3.5)
                rendered = True
                break

        print(f"[harness] Capturing screenshot for {name}...")
        ppm_file = f"/tmp/screenshot_{name}.ppm"
        png_file = f"/tmp/screenshot_{name}.png"
        mon.sendall(f"screendump {ppm_file}\n".encode("utf-8"))
        time.sleep(1.0)
        if os.path.exists(ppm_file):
            subprocess.run(["sips", "-s", "format", "png", ppm_file, "--out", png_file], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if os.path.exists(png_file):
                print(f"[harness] Saved screenshot: {png_file} ({os.path.getsize(png_file)} bytes)")

    mon.close()
    proc.terminate()
    proc.wait(timeout=3.0)
    log_file.close()
    print("\n[harness] Multi-site testing and screenshot capture complete!")
    return 0

if __name__ == "__main__":
    sys.exit(main())
