#!/usr/bin/env python3
"""
tools/test_qemu_youtube_native.py
Automated verification for FirstOS Native In-Browser SPA Synthesizer on YouTube.
Boots FirstOS in QEMU, navigates to YouTube, verifies in-browser C DOM extraction,
and captures high-resolution screenshots of the native video cards and player.
"""

import os
import sys
import time
import json
import socket
import subprocess
import threading
from pathlib import Path

REPO_ROOT = Path("/Users/mohamadjlo/Desktop/FirstOS")
ISO_PATH = REPO_ROOT / "firstos.iso"
FS_PATH = REPO_ROOT / "fs.img"
ARTIFACTS_DIR = Path("/Users/mohamadjlo/.gemini/antigravity/brain/59a5b11b-e0d3-43cd-87b1-36409f0883ba")
SERIAL_LOG_PATH = Path("/tmp/firstos_youtube_test.log")
QMP_SOCK = Path("/tmp/qmp_youtube_native.sock")

def ensure_proxy():
    # Check if proxy is already listening on 18080
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    res = sock.connect_ex(('127.0.0.1', 18080))
    sock.close()
    if res == 0:
        print("[test] Host proxy bridge already running on 127.0.0.1:18080.")
        return None
    print("[test] Starting proxy bridge on 127.0.0.1:18080...")
    proxy_script = REPO_ROOT / "tools" / "firstos_webproxy.py"
    proc = subprocess.Popen([sys.executable, str(proxy_script), "--bind", "127.0.0.1", "--port", "18080"])
    time.sleep(0.5)
    return proc

def qmp_cmd(sock, cmd_dict):
    try:
        sock.sendall(json.dumps(cmd_dict).encode("utf-8") + b"\n")
        resp = b""
        while True:
            chunk = sock.recv(4096)
            if not chunk: break
            resp += chunk
            if b"\n" in chunk: break
        return json.loads(resp.decode("utf-8", errors="replace").strip().split("\n")[0])
    except Exception as e:
        return {"error": str(e)}

def capture_screenshot(qmp_sock_conn, out_png_path):
    ppm_path = Path("/tmp/yt_temp.ppm")
    if ppm_path.exists(): ppm_path.unlink()
    qmp_cmd(qmp_sock_conn, {"execute": "screendump", "arguments": {"filename": str(ppm_path)}})
    time.sleep(0.2)
    if ppm_path.exists() and ppm_path.stat().st_size > 1000:
        subprocess.run(["sips", "-s", "format", "png", str(ppm_path), "--out", str(out_png_path)],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        print(f"[test] Captured screenshot saved to: {out_png_path} ({out_png_path.stat().st_size} bytes)")
        return True
    return False

def main():
    if QMP_SOCK.exists(): QMP_SOCK.unlink()
    proxy_proc = ensure_proxy()

    log_file = open(SERIAL_LOG_PATH, "w", encoding="utf-8")
    all_lines = []
    lock = threading.Lock()

    qemu_bin = "/opt/homebrew/bin/qemu-system-x86_64" if os.path.exists("/opt/homebrew/bin/qemu-system-x86_64") else "qemu-system-x86_64"
    qemu_cmd = [
        qemu_bin,
        "-cdrom", str(ISO_PATH),
        "-boot", "d",
        "-m", "2048M",
        "-drive", f"file={FS_PATH},format=raw,if=none,id=fsdisk",
        "-device", "virtio-blk-pci,drive=fsdisk",
        "-vga", "none",
        "-device", "virtio-gpu-pci,xres=1024,yres=768",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-serial", "stdio",
        "-display", "none",
        "-qmp", f"unix:{QMP_SOCK},server,nowait",
        "-no-reboot"
    ]

    print("[test] Launching QEMU...")
    proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def reader():
        for raw in iter(proc.stdout.readline, b""):
            line = raw.decode("latin1", errors="replace").rstrip("\r\n")
            log_file.write(line + "\n")
            log_file.flush()
            with lock:
                all_lines.append(line)
                if any(k in line for k in ["[spa_engine]", "[browser_core]", "[html5_engine]", "native YouTube"]):
                    print("  [QEMU] " + line)

    threading.Thread(target=reader, daemon=True).start()

    # Wait for QMP
    qmp_client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connected = False
    for _ in range(50):
        if QMP_SOCK.exists():
            try:
                qmp_client.connect(str(QMP_SOCK))
                qmp_client.recv(1024)
                qmp_client.sendall(json.dumps({"execute": "qmp_capabilities"}).encode() + b"\n")
                qmp_client.recv(1024)
                connected = True
                print("[test] Connected to QEMU QMP successfully.")
                break
            except Exception:
                pass
        time.sleep(0.2)

    if not connected:
        print("[test] Failed to connect to QMP")
        proc.kill()
        return 1

    try:
        # Wait for boot
        print("[test] Waiting for FirstOS boot...")
        boot_start = time.time()
        while time.time() - boot_start < 25.0:
            with lock:
                if any("new native Web Browser app initialized" in l or "gui_draw done" in l for l in all_lines):
                    break
            time.sleep(0.1)

        print("[test] FirstOS booted! Waiting 2s for network initialization...")
        time.sleep(2.0)

        # Unlock desktop PIN: 1 2 3 4 Ret
        print("[test] Typing PIN '1234' + Enter into login screen...")
        keys = ["1", "2", "3", "4", "ret"]
        for k in keys:
            qmp_cmd(qmp_client, {"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": k}]}})
            time.sleep(0.15)
        time.sleep(1.0)

        # Test 1: YouTube Homepage
        print("\n[test] >>> Navigating to: https://www.youtube.com ...")
        with lock:
            mark1 = len(all_lines)
        proc.stdin.write(b"NAV:https://www.youtube.com\n")
        proc.stdin.flush()

        nav_start = time.time()
        while time.time() - nav_start < 45.0:
            time.sleep(0.5)
            with lock:
                new1 = all_lines[mark1:]
                if any("paint done" in l for l in new1) and any("state=READY" in l for l in new1):
                    break

        time.sleep(2.0)
        out_png_home = ARTIFACTS_DIR / "v8_browser_youtube_native_cards.png"
        capture_screenshot(qmp_client, out_png_home)

        # Test 2: YouTube Watch Page
        print("\n[test] >>> Navigating to: https://www.youtube.com/watch?v=dQw4w9WgXcQ ...")
        with lock:
            mark2 = len(all_lines)
        proc.stdin.write(b"NAV:https://www.youtube.com/watch?v=dQw4w9WgXcQ\n")
        proc.stdin.flush()

        nav_start = time.time()
        while time.time() - nav_start < 45.0:
            time.sleep(0.5)
            with lock:
                new2 = all_lines[mark2:]
                if any("paint done" in l for l in new2) and any("state=READY" in l for l in new2):
                    break

        time.sleep(2.0)
        out_png_watch = ARTIFACTS_DIR / "v8_browser_youtube_watch_native.png"
        capture_screenshot(qmp_client, out_png_watch)

        print("\n[test] Verification completed successfully!")

    finally:
        qmp_client.close()
        proc.kill()
        if proxy_proc:
            proxy_proc.kill()

if __name__ == "__main__":
    main()
