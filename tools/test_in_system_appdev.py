#!/usr/bin/env python3
"""
tools/test_in_system_appdev.py
Live QEMU verification of the FirstOS In-System AppDev Platform (App Studio v1).

Every verdict is derived from serial-log evidence. Nothing is hardcoded.
  1. App Studio subsystem initialized at boot
  2. Project created from template (unique name per run)
  3. In-system build produced build/app.elf
  4. Ring 3 run via process_spawn
  5. Explicit STOP of a *running* process (PARTIAL if it had already exited)
  6. Persistence: second QEMU boot re-opens the project and builds from disk
"""

import subprocess
import sys
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
ISO = REPO_ROOT / "firstos.iso"
FS = REPO_ROOT / "fs.img"
LOG_DIR = REPO_ROOT / "reports" / "appdev_logs"


class QemuSession:
    def __init__(self):
        cmd = [
            "qemu-system-x86_64",
            "-drive", f"file={ISO},format=raw,media=cdrom,readonly=on",
            "-boot", "d",
            "-drive", f"file={FS},format=raw,if=none,id=fsdisk",
            "-device", "virtio-blk-pci,drive=fsdisk",
            "-m", "2G", "-vga", "none",
            "-device", "virtio-gpu-pci,xres=1024,yres=768",
            "-serial", "stdio", "-display", "none",
        ]
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.lines = []
        self.lock = threading.Lock()
        self.ready = threading.Event()
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        for raw in iter(self.proc.stdout.readline, b""):
            s = raw.decode("latin1", errors="replace").rstrip("\r\n")
            if "[kernel] gui_draw done" in s:
                self.ready.set()
            with self.lock:
                self.lines.append(s)

    def send(self, cmd):
        self.proc.stdin.write((cmd + "\n").encode())
        self.proc.stdin.flush()

    def mark(self):
        with self.lock:
            return len(self.lines)

    def wait_for(self, needle, start=0, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end:
            with self.lock:
                for l in self.lines[start:]:
                    if needle in l:
                        return True
            time.sleep(0.05)
        return False

    def seen(self, needle, start=0):
        with self.lock:
            return any(needle in l for l in self.lines[start:])

    def close(self, log_path):
        try:
            self.proc.terminate()
            self.proc.wait(timeout=3)
        except Exception:
            self.proc.kill()
        log_path.parent.mkdir(parents=True, exist_ok=True)
        with self.lock:
            log_path.write_text("\n".join(self.lines))


def main():
    print("=" * 65)
    print("FirstOS In-System AppDev Platform: Live QEMU Verification Suite")
    print("=" * 65)
    proj = f"AppT{int(time.time()) % 100000}"
    pdir = f"/Projects/{proj}"
    results = []  # (id, name, verdict, evidence)

    def record(i, name, verdict, ev):
        results.append((i, name, verdict, ev))
        print(f"  [{verdict}] {i}. {name} -- {ev}")

    # ---------------- Boot 1 ----------------
    q = QemuSession()
    if not q.ready.wait(timeout=20):
        q.close(LOG_DIR / "boot1_serial.log")
        print("  [FAIL] desktop never reached '[kernel] gui_draw done'")
        return 1
    time.sleep(1.0)

    ok = q.seen("[app_studio] Initialized FirstOS App Studio v1 Platform.")
    record(1, "App Studio init", "PASS" if ok else "FAIL", "init line in boot log" if ok else "init line missing")

    m = q.mark(); q.send(f"APP_STUDIO_NEW:{proj}")
    ok = q.wait_for(f"[wizard] Created new project: {proj}", m)
    record(2, "Project wizard", "PASS" if ok else "FAIL", pdir)

    m = q.mark(); q.send("APP_STUDIO_BUILD")
    ok = q.wait_for(f"[build] Build successful! Artifact generated: {pdir}/build/app.elf", m, 8)
    record(3, "In-system build", "PASS" if ok else "FAIL", f"{pdir}/build/app.elf" if ok else "no build success line")

    m = q.mark(); q.send("APP_STUDIO_RUN")
    ok = q.wait_for("[runner] Spawn SUCCESS! Process assigned PID=", m)
    record(4, "Ring 3 run", "PASS" if ok else "FAIL", "process_spawn succeeded" if ok else "spawn failed")

    time.sleep(0.3)
    m = q.mark(); q.send("APP_STUDIO_STOP")
    got_cmd = q.wait_for("[serial_cmd] APP_STUDIO_STOP", m, 5)
    explicit = q.wait_for("[runner] Stopping process PID=", m, 3) and \
        q.wait_for("[runner] Process terminated cleanly. State: STOPPED.", m, 3)
    if explicit:
        record(5, "Stop running process", "PASS", "explicit STOP of live PID")
    elif got_cmd and q.seen("[runner] No active application running.", m):
        record(5, "Stop running process", "PARTIAL", "app had already exited before STOP")
    else:
        record(5, "Stop running process", "FAIL", "STOP not serviced (kernel loop starved?)")

    time.sleep(1.0)
    q.close(LOG_DIR / "boot1_serial.log")

    # ---------------- Boot 2 (persistence) ----------------
    q2 = QemuSession()
    if q2.ready.wait(timeout=20):
        time.sleep(1.0)
        m = q2.mark(); q2.send(f"APP_STUDIO_OPEN:{pdir}")
        opened = q2.wait_for(f"[project] Opened Project: {proj}", m)
        m = q2.mark(); q2.send("APP_STUDIO_BUILD")
        built = q2.wait_for(f"[build] Build successful! Artifact generated: {pdir}/build/app.elf", m, 8)
        missing = q2.seen("Source file not found", m)
        ok = opened and built and not missing
        record(6, "Persistence across reboot", "PASS" if ok else "FAIL",
               "project re-opened and built from fs.img after reboot" if ok else
               f"opened={opened} built={built} source_missing={missing}")
    else:
        record(6, "Persistence across reboot", "FAIL", "second boot did not reach desktop")
    q2.close(LOG_DIR / "boot2_serial.log")

    passed = sum(1 for r in results if r[2] == "PASS")
    total = len(results)
    print("=" * 65)
    print(f"Summary: {passed} / {total} PASS")
    print("=" * 65)

    rows = "\n".join(f"| APPDEV_0{i} | {n} | **{v}** | {e} |" for i, n, v, e in results)
    status = "PASS" if passed == total else "INCOMPLETE"
    (REPO_ROOT / "reports" / "in_system_appdev_verification.md").write_text(
        f"""# FirstOS In-System AppDev Platform: Verification Report

**Generated**: {time.strftime('%Y-%m-%d %H:%M:%S')}
**Status**: {status} ({passed}/{total})
**Environment**: QEMU x86_64 (two boots sharing fs.img)
**Project under test**: `{pdir}`

| Test ID | Check | Result | Evidence |
| :--- | :--- | :--- | :--- |
{rows}

Serial logs: `reports/appdev_logs/boot1_serial.log`, `reports/appdev_logs/boot2_serial.log`

## Known limitation
The "build" step in v1 validates the source and installs a pre-compiled template ELF
into `build/app.elf`; it does **not** compile the user's C source in-system yet.
""")
    (REPO_ROOT / "reports" / "in_system_appdev_test_matrix.csv").write_text(
        "Test_ID,Check,Result,Evidence\n" +
        "\n".join(f"APPDEV_0{i},{n},{v},\"{e}\"" for i, n, v, e in results) + "\n")
    return 0 if passed == total else 1


if __name__ == "__main__":
    sys.exit(main())
