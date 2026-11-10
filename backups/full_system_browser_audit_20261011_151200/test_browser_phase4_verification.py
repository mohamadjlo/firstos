#!/usr/bin/env python3
"""Phase 4 Verification Test for FirstOS Performance, Caching, Security & Robustness Browser Platform."""

import os
import sys
import time
import subprocess
import threading

SERIAL_LOG = "/tmp/firstos_phase4_test.log"

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
    print("[test_phase4] Starting HTTPS proxy bridge on 127.0.0.1:18080...")
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

    print("[test_phase4] Launching QEMU...")
    proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    reader = threading.Thread(target=reader_thread_func, args=(proc, log_file), daemon=True)
    reader.start()

    try:
        # Wait for boot
        print("[test_phase4] Waiting for FirstOS boot...")
        boot_start = time.time()
        booted = False
        while time.time() - boot_start < 25.0:
            with lock:
                if any("new native Web Browser app initialized" in l or "gui_draw done" in l for l in all_lines):
                    booted = True
                    break
            time.sleep(0.2)

        if not booted:
            print("[test_phase4] Boot timeout!")
            return 1

        print("[test_phase4] FirstOS booted successfully!")
        time.sleep(2.0)

        # Step 1: Test Navigation & Performance Metrics pipeline
        print("[test_phase4] 1. Testing navigation & performance timing...")
        proc.stdin.write(b"NAV:about:test\n")
        proc.stdin.flush()
        time.sleep(2.0)

        # Step 2: Test Partitioned Cache (miss, hit, evict)
        print("[test_phase4] 2. Testing partitioned cache behavior...")
        proc.stdin.write(b"CACHE_TEST\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # Step 3: Test Security Disallowed Scheme
        print("[test_phase4] 3. Testing disallowed scheme security policy...")
        proc.stdin.write(b"SCHEME_TEST:javascript:alert(1)\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # Step 4: Test Cross-Origin Frame Access Blocking
        print("[test_phase4] 4. Testing cross-origin frame access blocking...")
        proc.stdin.write(b"FRAME_NEW:0:https://untrusted-site.com\n")
        proc.stdin.flush()
        time.sleep(1.0)
        proc.stdin.write(b"FRAME_ACCESS:1:2\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # Step 5: Test Stale Work Dropping
        print("[test_phase4] 5. Testing stale generation dropping...")
        proc.stdin.write(b"STALE_TEST\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # Step 6: Test Partial Repaint / Dirty Region Handoff
        print("[test_phase4] 6. Testing partial repaint / damage rect handoff...")
        proc.stdin.write(b"DAMAGE_TEST\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # Step 7: Test Network Request Cancellation
        print("[test_phase4] 7. Testing network request cancellation...")
        proc.stdin.write(b"CANCEL_TEST\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # Step 8: Test Crash Containment & Recovery
        print("[test_phase4] 8. Testing renderer crash containment & reloadable state...")
        proc.stdin.write(b"TAB_CRASH:1\n")
        proc.stdin.flush()
        time.sleep(1.0)
        proc.stdin.write(b"TAB_RELOAD:1\n")
        proc.stdin.flush()
        time.sleep(2.0)

        # Inspect collected lines
        with lock:
            lines = list(all_lines)

        print("\n================== Phase 4 Verification Report ==================")
        required_patterns = [
            ("[perf] nav start tab=", False),
            ("[perf] dns done dt=", False),
            ("[perf] connect done dt=", False),
            ("[perf] body ready len=", False),
            ("[perf] parse done dt=", False),
            ("[perf] first paint dt=", False),
            ("[perf] ready dt=", False),
            ("[cache] hit origin=", False),
            ("[cache] miss origin=", False),
            ("[cache] evict slot=", False),
            ("[security] blocked scheme=", False),
            ("[security] blocked cross-origin frame access", False),
            ("[renderer] stale result dropped", False),
            ("[renderer] partial repaint x=", False),
            ("[network] canceled req=", False),
            ("[crash] renderer tab=", False),
            ("[recovery] tab=", False),
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
            print(">>> ALL Phase 4 Hardening & Performance Log Signatures Verified Successfully! <<<")
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
