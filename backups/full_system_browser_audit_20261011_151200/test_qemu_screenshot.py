#!/usr/bin/env python3
import os
import sys
import time
import socket
import subprocess
import threading

MON_SOCK = "/tmp/qemu_mon.sock"
SERIAL_LOG = "/tmp/qemu_serial_screenshot.log"

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

    print("[test] Starting QEMU with monitor socket...")
    proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    log_file = open(SERIAL_LOG, "w")
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
    print("[test] Waiting for boot...")
    start = time.time()
    booted = False
    while time.time() - start < 15.0:
        if any("new native Web Browser app initialized" in l or "gui_draw done" in l for l in all_lines):
            booted = True
            break
        time.sleep(0.2)

    if not booted:
        print("[test] Boot timeout!")
        proc.kill()
        return 1

    print("[test] Booted! Waiting 2s for UI idle...")
    time.sleep(2.0)

    # Connect to monitor socket
    mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    mon.connect(MON_SOCK)
    mon.settimeout(5.0)
    time.sleep(0.5)
    # Read greeting
    try:
        mon.recv(1024)
    except Exception:
        pass

    # Take screenshot of home
    print("[test] Taking screenshot of boot screen...")
    mon.sendall(b"screendump /tmp/firstos_boot.ppm\n")
    time.sleep(1.0)

    if os.path.exists("/tmp/firstos_boot.ppm"):
        print("[test] PPM screenshot created! Converting to PNG...")
        subprocess.run(["sips", "-s", "format", "png", "/tmp/firstos_boot.ppm", "--out", "/tmp/firstos_boot.png"], check=True)
        print(f"[test] PNG created: /tmp/firstos_boot.png, size: {os.path.getsize('/tmp/firstos_boot.png')} bytes")
    else:
        print("[test] screendump failed!")

    mon.close()
    proc.terminate()
    proc.wait(timeout=3)
    return 0

if __name__ == "__main__":
    sys.exit(main())
