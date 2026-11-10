#!/usr/bin/env python3
"""Phase 3 Verification Test for FirstOS Multi-Service Browser Platform."""

import os
import sys
import time
import subprocess
import threading

SERIAL_LOG = "/tmp/firstos_phase3_test.log"

all_lines = []
lock = threading.Lock()

def reader_thread_func(proc, log_file):
    while True:
        line = proc.stdout.readline()
        if not line:
            break
        text = line.decode("latin1", errors="replace").rstrip("\r\n")
        log_file.write(text + "\n")
        log_file.flush()
        with lock:
            all_lines.append(text)

def main():
    root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    iso_path = os.path.join(root_dir, "firstos.iso")
    fs_path = os.path.join(root_dir, "fs.img")

    if not os.path.exists(iso_path):
        print(f"Error: {iso_path} not found")
        return 1

    log_file = open(SERIAL_LOG, "w", encoding="utf-8")

    # Start proxy bridge
    webproxy_script = os.path.join(root_dir, "tools", "firstos_webproxy.py")
    print("[test_phase3] Starting HTTPS proxy bridge on 127.0.0.1:18080...")
    proxy_proc = subprocess.Popen([sys.executable, webproxy_script, "--bind", "127.0.0.1", "--port", "18080"],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.5)

    qemu_cmd = [
        "qemu-system-x86_64",
        "-drive", f"file={iso_path},format=raw,media=cdrom,readonly=on",
        "-boot", "d",
        "-drive", f"file={fs_path},format=raw,if=none,id=fsdisk",
        "-device", "virtio-blk-pci,drive=fsdisk",
        "-m", "2G",
        "-vga", "none",
        "-device", "virtio-gpu-pci,xres=1024,yres=768",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-serial", "stdio",
        "-display", "none"
    ]

    print("[test_phase3] Launching QEMU...")
    proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    reader = threading.Thread(target=reader_thread_func, args=(proc, log_file), daemon=True)
    reader.start()

    try:
        # Wait for boot
        print("[test_phase3] Waiting for FirstOS boot...")
        boot_start = time.time()
        booted = False
        while time.time() - boot_start < 25.0:
            with lock:
                if any("new native Web Browser app initialized" in l or "gui_draw done" in l for l in all_lines):
                    booted = True
                    break
            time.sleep(0.2)

        if not booted:
            print("[test_phase3] Boot timeout!")
            return 1

        print("[test_phase3] FirstOS booted successfully!")
        time.sleep(2.0)

        # Step 1: Navigate to http://example.com to trigger network service & cookie query
        print("[test_phase3] 1. Testing navigation to http://example.com...")
        proc.stdin.write(b"NAV:http://example.com\n")
        proc.stdin.flush()
        time.sleep(6.0)

        # Step 2: Create a child frame (iframe) on the tab
        print("[test_phase3] 2. Testing child frame creation...")
        proc.stdin.write(b"FRAME_NEW:0:https://malicious.org\n")
        proc.stdin.flush()
        time.sleep(1.5)

        # Step 3: Test Cross-Origin Access Denial check
        print("[test_phase3] 3. Testing cross-origin access control check...")
        proc.stdin.write(b"FRAME_ACCESS:1:2\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # Step 4: Test Crash Containment on Child Frame (parent survives)
        print("[test_phase3] 4. Testing crash containment on child frame...")
        proc.stdin.write(b"FRAME_CRASH:2\n")
        proc.stdin.flush()
        time.sleep(1.5)

        # Step 5: Test Tab Crash & Recovery
        print("[test_phase3] 5. Testing Tab Crash and Recovery...")
        proc.stdin.write(b"TAB_CRASH:1\n")
        proc.stdin.flush()
        time.sleep(1.0)
        proc.stdin.write(b"TAB_RELOAD:1\n")
        proc.stdin.flush()
        time.sleep(2.0)

        # Inspect collected lines
        with lock:
            lines = list(all_lines)

        print("\n================== Phase 3 Verification Report ==================")
        required_patterns = [
            ("[browser_host] tab create id=", False),
            ("[browser_host] frame create id=", False),
            ("[browser_host] navigate tab=", False),
            ("[network_svc] request start id=", False),
            ("[network_svc] response ready id=", False),
            ("[renderer_svc] create id=", False),
            ("[renderer_svc] frame ready id=", False),
            ("[renderer_svc] crash id=", False),
            ("[security] origin check denied", False),
            ("[storage] cookie query origin=", False),
            ("[ipc] msg type=", False),
            ("[present] surface ready tab=", False),
            ("[browser_host] recovery after renderer crash ok", False),
        ]

        verified_patterns = []
        for pat, _ in required_patterns:
            found = any(pat in line for line in lines)
            verified_patterns.append((pat, found))
            status = "PASS [OK]" if found else "FAIL [MISSING]"
            print(f"  {status}: {pat}")

        all_passed = all(found for _, found in verified_patterns)
        print("=================================================================\n")
        if all_passed:
            print(">>> ALL Phase 3 Lifecycle Log Signatures Verified Successfully! <<<")
            return 0
        else:
            print("Some signatures were missing. Check serial log.")
            return 2

    finally:
        try:
            proc.terminate()
            proc.wait(timeout=2)
        except Exception:
            proc.kill()
        try:
            proxy_proc.terminate()
            proxy_proc.wait(timeout=2)
        except Exception:
            proxy_proc.kill()

if __name__ == "__main__":
    sys.exit(main())
