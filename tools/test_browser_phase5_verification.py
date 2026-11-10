#!/usr/bin/env python3
"""Phase 5 Verification Test for FirstOS Production-Oriented Chromium-like Browser Platform."""

import os
import sys
import time
import subprocess
import threading

SERIAL_LOG = "/tmp/firstos_phase5_test.log"

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
    print("[test_phase5] Starting HTTPS proxy bridge on 127.0.0.1:18080...")
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

    print("[test_phase5] Launching QEMU...")
    proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    reader = threading.Thread(target=reader_thread_func, args=(proc, log_file), daemon=True)
    reader.start()

    try:
        # Wait for boot
        print("[test_phase5] Waiting for FirstOS boot...")
        boot_start = time.time()
        booted = False
        while time.time() - boot_start < 25.0:
            with lock:
                if any("new native Web Browser app initialized" in l or "gui_draw done" in l for l in all_lines):
                    booted = True
                    break
            time.sleep(0.2)

        if not booted:
            print("[test_phase5] Boot timeout!")
            return 1

        print("[test_phase5] FirstOS booted successfully!")
        time.sleep(2.0)

        # 1. Test Navigation & Frame Presentation pipeline
        print("[test_phase5] 1. Testing navigation & present pipeline...")
        proc.stdin.write(b"NAV:about:test\n")
        proc.stdin.flush()
        time.sleep(2.0)

        # 2. Test Sandbox Capability Gating (file-open, network-direct, shm-attach)
        print("[test_phase5] 2. Testing renderer sandbox policy gating...")
        proc.stdin.write(b"SANDBOX_TEST\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # 3. Test Site Isolation & Partitioning Key
        print("[test_phase5] 3. Testing site isolation & partitioning key...")
        proc.stdin.write(b"SITE_TEST\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # 4. Test Scheduler Foreground Boost & Background Throttling
        print("[test_phase5] 4. Testing scheduler tab priority tuning...")
        proc.stdin.write(b"SCHED_TEST\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # 5. Test IPC Message Validation & Drop
        print("[test_phase5] 5. Testing IPC invalid message validation...")
        proc.stdin.write(b"IPC_TEST\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # 6. Test Surface Handoff & Stale Surface Dropping
        print("[test_phase5] 6. Testing surface lifecycle & stale dropping...")
        proc.stdin.write(b"SURFACE_TEST\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # 7. Test SHM Memory Cleanup
        print("[test_phase5] 7. Testing process SHM memory reclamation...")
        proc.stdin.write(b"SHM_CLEAN_TEST\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # 8. Test Partitioned Cache Behavior
        print("[test_phase5] 8. Testing cache hit/miss/eviction...")
        proc.stdin.write(b"CACHE_TEST\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # 9. Test Renderer Crash Containment & Process Respawn
        print("[test_phase5] 9. Testing crash containment & restart...")
        proc.stdin.write(b"TAB_CRASH:1\n")
        proc.stdin.flush()
        time.sleep(1.0)
        proc.stdin.write(b"TAB_RELOAD:1\n")
        proc.stdin.flush()
        time.sleep(2.0)

        # Inspect collected lines
        with lock:
            lines = list(all_lines)

        print("\n================== Phase 5 Verification Report ==================")
        required_patterns = [
            ("[policy] deny service=renderer action=file-open path=", False),
            ("[policy] deny service=renderer action=network-direct", False),
            ("[cap] role=renderer shm-attach denied", False),
            ("[ipc] invalid message dropped type=", False),
            ("[site] partition key=", False),
            ("[site] cross-site isolation enforced", False),
            ("[sched] foreground tab boost tab=", False),
            ("[sched] background throttled tab=", False),
            ("[mem] stale surface dropped tab=", False),
            ("[mem] shm cleanup pid=", False),
            ("[present] frame=", False),
            ("[present] stale frame dropped", False),
            ("[crash] renderer pid=", False),
            ("[recovery] renderer restart ok tab=", False),
            ("[perf] ready dt=", False),
            ("[cache] hit origin=", False),
            ("[cache] miss origin=", False),
            ("[cache] evict slot=", False),
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
            print(">>> ALL Phase 5 Production-Oriented Browser Platform Signatures Verified Successfully! <<<")
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
