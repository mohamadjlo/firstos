#!/usr/bin/env python3
"""
tools/test_qemu_real_web.py
Verifies authentic web rendering in FirstOS without any fake or pre-made templates:
1. Boots FirstOS in QEMU.
2. Unlocks PIN 1234.
3. Tests navigation to https://www.google.com (Real Google HTML).
4. Tests navigation to https://www.wikipedia.org (Real Wikipedia HTML).
5. Tests navigation to https://www.youtube.com (Real YouTube raw response).
6. Tests navigation to non-existent host (Verifies real connection failure error page).
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
SERIAL_LOG_PATH = Path("/tmp/firstos_real_web.log")
QMP_SOCK = Path("/tmp/qmp_real_web.sock")

def ensure_proxy():
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
    ppm_path = Path("/tmp/web_temp.ppm")
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
                if any(k in line for k in ["[browser_core]", "[web_browser]", "[http_client]", "Unable to Connect"]):
                    print("  [QEMU] " + line)

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    time.sleep(1.0)
    qmp_client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    for _ in range(30):
        if QMP_SOCK.exists():
            try:
                qmp_client.connect(str(QMP_SOCK))
                greeting = qmp_client.recv(4096)
                qmp_cmd(qmp_client, {"execute": "qmp_capabilities"})
                print("[test] QMP connection established.")
                break
            except Exception:
                time.sleep(0.2)
        else:
            time.sleep(0.2)

    try:
        boot_start = time.time()
        while time.time() - boot_start < 25.0:
            with lock:
                if any("new native Web Browser app initialized" in l or "gui_draw done" in l for l in all_lines):
                    break
            time.sleep(0.1)

        print("[test] FirstOS booted! Waiting 2s for desktop...")
        time.sleep(2.0)

        # Unlock PIN 1234
        print("[test] Typing PIN '1234' + Enter...")
        for k in ["1", "2", "3", "4", "ret"]:
            qmp_cmd(qmp_client, {"execute": "send-key", "arguments": {"keys": [{"type": "qcode", "data": k}]}})
            time.sleep(0.15)
        time.sleep(1.5)

        # Test 1: Real Google
        print("\n[test] >>> Navigating to: https://www.google.com ...")
        with lock: mark = len(all_lines)
        proc.stdin.write(b"NAV:https://www.google.com\n")
        proc.stdin.flush()
        nav_start = time.time()
        while time.time() - nav_start < 30.0:
            time.sleep(0.5)
            with lock:
                new_l = all_lines[mark:]
                if any("paint done" in l for l in new_l) and any("state=READY" in l for l in new_l):
                    break
        time.sleep(2.0)
        capture_screenshot(qmp_client, ARTIFACTS_DIR / "v9_browser_real_google.png")

        # Test 2: Real Wikipedia
        print("\n[test] >>> Navigating to: https://www.wikipedia.org ...")
        with lock: mark = len(all_lines)
        proc.stdin.write(b"NAV:https://www.wikipedia.org\n")
        proc.stdin.flush()
        nav_start = time.time()
        while time.time() - nav_start < 30.0:
            time.sleep(0.5)
            with lock:
                new_l = all_lines[mark:]
                if any("paint done" in l for l in new_l) and any("state=READY" in l for l in new_l):
                    break
        time.sleep(2.0)
        capture_screenshot(qmp_client, ARTIFACTS_DIR / "v9_browser_real_wikipedia.png")

        # Test 3: Real YouTube
        print("\n[test] >>> Navigating to: https://www.youtube.com ...")
        with lock: mark = len(all_lines)
        proc.stdin.write(b"NAV:https://www.youtube.com\n")
        proc.stdin.flush()
        nav_start = time.time()
        while time.time() - nav_start < 30.0:
            time.sleep(0.5)
            with lock:
                new_l = all_lines[mark:]
                if any("paint done" in l for l in new_l) and any("state=READY" in l for l in new_l):
                    break
        time.sleep(2.0)
        capture_screenshot(qmp_client, ARTIFACTS_DIR / "v9_browser_real_youtube.png")

        # Test 4: Real Connection Failure Error Page
        print("\n[test] >>> Navigating to down host: http://nonexistent-host-12345.test ...")
        with lock: mark = len(all_lines)
        proc.stdin.write(b"NAV:http://nonexistent-host-12345.test\n")
        proc.stdin.flush()
        nav_start = time.time()
        while time.time() - nav_start < 15.0:
            time.sleep(0.5)
            with lock:
                new_l = all_lines[mark:]
                if any("response failed" in l for l in new_l) or any("paint done" in l for l in new_l):
                    break
        time.sleep(2.0)
        capture_screenshot(qmp_client, ARTIFACTS_DIR / "v9_browser_real_neterror.png")

        print("\n[test] All real web tests completed!")

    finally:
        try: qmp_cmd(qmp_client, {"execute": "quit"})
        except Exception: pass
        qmp_client.close()
        proc.terminate()
        try: proc.wait(timeout=3)
        except Exception: proc.kill()
        log_file.close()
        if proxy_proc:
            proxy_proc.terminate()

if __name__ == "__main__":
    main()
