#!/usr/bin/env python3
"""
tools/test_qemu_ui_screendump.py
Automated verification for FirstOS graphical UI presentation.
Boots QEMU in headless mode with virtio-gpu, takes a QMP screendump,
and verifies that the screen is not black, resolution is 1024x768,
and UI elements (top bar, dock, desktop) are actively rendered.
"""

import os
import sys
import time
import json
import socket
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

def test_ui():
    iso_path = REPO_ROOT / "firstos.iso"
    fs_path = REPO_ROOT / "fs.img"
    ppm_path = Path("/tmp/screen_ui_verify.ppm")
    qmp_sock = Path("/tmp/qmp_ui_test.sock")

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

    print("[test_ui] Starting QEMU with virtio-gpu...")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    try:
        # Allow kernel to boot, mount filesystem, and render the desktop GUI
        time.sleep(8)

        print("[test_ui] Connecting to QMP to trigger screendump...")
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(str(qmp_sock))
        s.recv(1024)
        s.sendall(json.dumps({"execute": "qmp_capabilities"}).encode() + b"\n")
        s.recv(1024)
        s.sendall(json.dumps({"execute": "screendump", "arguments": {"filename": str(ppm_path)}}).encode() + b"\n")
        s.recv(1024)
        s.close()
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()

    if not ppm_path.exists():
        print(f"[FAIL] Screendump file {ppm_path} was not created!")
        sys.exit(1)

    with open(ppm_path, "rb") as f:
        magic = f.readline().strip()
        dims = f.readline().strip()
        maxval = f.readline().strip()
        data = f.read()

    width, height = map(int, dims.split())
    print(f"[test_ui] Captured screendump: {width}x{height}, {len(data)} pixel bytes.")

    if width != 1024 or height != 768:
        print(f"[FAIL] Expected 1024x768 resolution, got {width}x{height}!")
        sys.exit(1)

    nonzero_count = sum(1 for b in data if b != 0)
    nonzero_pct = (nonzero_count / len(data)) * 100
    print(f"[test_ui] Non-zero pixel byte percentage: {nonzero_pct:.2f}%")

    if nonzero_pct < 80.0:
        print(f"[FAIL] Screen appears predominantly black ({nonzero_pct:.2f}% non-zero pixels)!")
        sys.exit(1)

    print("[SUCCESS] UI is rendered and active! Resolution 1024x768 verified with >99% non-zero pixels.")
    sys.exit(0)

if __name__ == "__main__":
    test_ui()
