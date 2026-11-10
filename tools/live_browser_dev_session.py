#!/usr/bin/env python3
"""
tools/live_browser_dev_session.py
Orchestrates the Live In-System Browser Development Session:
- Controls QEMU execution with VirtIO-GPU, VirtIO-Net, VirtIO-Blk, QMP, and serial stdio.
- Provides screendump extraction and PNG conversion via sips.
- Dispatches serial commands to the running FirstOS kernel (NAV, APP_STUDIO_*, etc.).
"""

import os
import sys
import time
import json
import socket
import subprocess
import threading
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
ISO_PATH = REPO_ROOT / "firstos.iso"
FS_PATH = REPO_ROOT / "fs.img"
REPORTS_DIR = REPO_ROOT / "reports"
QMP_SOCK = Path("/tmp/qmp_live_browser_dev.sock")

class LiveSession:
    def __init__(self, log_prefix="live_browser_dev"):
        self.log_prefix = log_prefix
        self.serial_log_path = REPORTS_DIR / f"{log_prefix}_serial.log"
        self.boot_log_path = REPORTS_DIR / f"{log_prefix}_boot.log"
        self.lines = []
        self.lock = threading.Lock()
        self.ready_event = threading.Event()
        self.proc = None
        self.qmp_socket = None

    def start_qemu(self):
        if QMP_SOCK.exists():
            QMP_SOCK.unlink()

        cmd = [
            "qemu-system-x86_64",
            "-drive", f"file={ISO_PATH},format=raw,media=cdrom,readonly=on",
            "-boot", "d",
            "-m", "2G",
            "-drive", f"file={FS_PATH},format=raw,if=none,id=fsdisk",
            "-device", "virtio-blk-pci,drive=fsdisk",
            "-vga", "none",
            "-device", "virtio-gpu-pci,xres=1024,yres=768",
            "-serial", "stdio",
            "-display", "none",
            "-qmp", f"unix:{QMP_SOCK},server,nowait"
        ]

        print(f"[live_session] Spawning QEMU: {' '.join(cmd)}")
        self.proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT
        )

        threading.Thread(target=self._reader, daemon=True).start()

        # Connect to QMP
        print("[live_session] Connecting to QMP monitor...")
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        connected = False
        for _ in range(60):
            if QMP_SOCK.exists():
                try:
                    s.connect(str(QMP_SOCK))
                    s.recv(1024)
                    s.sendall(json.dumps({"execute": "qmp_capabilities"}).encode() + b"\n")
                    s.recv(1024)
                    connected = True
                    break
                except Exception:
                    pass
            time.sleep(0.1)

        if not connected:
            raise RuntimeError("Failed to connect to QMP monitor socket.")
        self.qmp_socket = s
        print("[live_session] Connected to QMP monitor successfully.")

    def _reader(self):
        boot_done = False
        boot_lines = []
        for raw in iter(self.proc.stdout.readline, b""):
            line = raw.decode("latin1", errors="replace").rstrip("\r\n")
            with self.lock:
                self.lines.append(line)
                if not boot_done:
                    boot_lines.append(line)
            if "[kernel] gui_draw done" in line or "Desktop initialized" in line:
                self.ready_event.set()
                boot_done = True
                self.boot_log_path.write_text("\n".join(boot_lines) + "\n")

    def wait_for_boot(self, timeout=25.0):
        print(f"[live_session] Waiting for FirstOS boot (timeout={timeout}s)...")
        ok = self.ready_event.wait(timeout)
        if ok:
            print("[live_session] FirstOS reached GUI desktop!")
        else:
            print("[live_session] WARNING: Boot timed out waiting for gui_draw done.")
        return ok

    def send_serial(self, cmd_str):
        print(f"[live_session] Sending serial command: {cmd_str}")
        with self.lock:
            self.lines.append(f">>> {cmd_str}")
        self.proc.stdin.write((cmd_str + "\n").encode())
        self.proc.stdin.flush()

    def wait_for_line(self, needle, timeout=8.0):
        start_time = time.time()
        start_idx = len(self.lines)
        while time.time() - start_time < timeout:
            with self.lock:
                for l in self.lines[start_idx - 10:]:
                    if needle in l:
                        return True
            time.sleep(0.05)
        return False

    def screendump(self, out_png_path):
        tmp_ppm = Path(f"/tmp/qemu_dump_{int(time.time()*1000)}.ppm")
        try:
            req = json.dumps({"execute": "screendump", "arguments": {"filename": str(tmp_ppm)}}).encode() + b"\n"
            self.qmp_socket.sendall(req)
            resp = self.qmp_socket.recv(4096)
            time.sleep(0.2)

            if tmp_ppm.exists() and tmp_ppm.stat().st_size > 1000:
                out_png_path.parent.mkdir(parents=True, exist_ok=True)
                cmd = ["sips", "-s", "format", "png", str(tmp_ppm), "--out", str(out_png_path)]
                subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
                tmp_ppm.unlink(missing_ok=True)
                print(f"[live_session] Captured visual frame to: {out_png_path}")
                return True
            else:
                print(f"[live_session] Screendump failed or file empty.")
                return False
        except Exception as e:
            print(f"[live_session] Screendump error: {e}")
            return False

    def close(self):
        print("[live_session] Shutting down QEMU and saving logs...")
        if self.qmp_socket:
            try:
                self.qmp_socket.close()
            except Exception:
                pass
        if self.proc:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=3)
            except Exception:
                self.proc.kill()
        with self.lock:
            self.serial_log_path.write_text("\n".join(self.lines) + "\n")
        print(f"[live_session] Wrote serial log to: {self.serial_log_path}")

if __name__ == "__main__":
    session = LiveSession()
    try:
        session.start_qemu()
        if session.wait_for_boot():
            time.sleep(1.0)
            # Step 1: Exercise In-System Developer Tools (App Studio)
            session.send_serial("APP_STUDIO_NEW:LiveBrowserDev")
            time.sleep(1.0)
            session.send_serial("APP_STUDIO_BUILD")
            time.sleep(1.5)

            # Step 2: Launch Enhanced Browser to about:home
            session.send_serial("NAV:about:home")
            time.sleep(2.5)
            after_png = REPORTS_DIR / "live_browser_dev_after.png"
            session.screendump(after_png)

            # Step 3: Navigate to about:html5 to verify multi-page rendering with new toolbar
            session.send_serial("NAV:about:html5")
            time.sleep(2.0)
            html5_png = REPORTS_DIR / "live_browser_dev_html5.png"
            session.screendump(html5_png)

            # Step 4: Return to about:home
            session.send_serial("NAV:about:home")
            time.sleep(1.5)
    finally:
        session.close()
