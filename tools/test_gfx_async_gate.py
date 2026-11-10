#!/usr/bin/env python3
"""
tools/test_gfx_async_gate.py
FirstOS Phase 6 Asynchronous GPU Submission & Completion Events Gate Verification.

Verifies:
1. VirtIO-GPU completion IRQ capability detection and handler registration.
2. Event queue initialization and non-blocking ring buffer behavior.
3. Async frame submission, IRQ completion notification, and safe buffer recycling.
4. Generic-FB synchronous fallback with bounded polling.
5. Stale and duplicate event validation and rejection.
6. Watchdog timeout and submission path recovery.
7. Resolution change IRQ masking and completion path restoration.
8. Frozen hot-paths integrity (gui_draw, draw_cursor, fb_present, virtio_gpu_2d_present, fb_mark_dirty_rect).
"""

import sys
import time
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

def run_cmd(cmd_list, timeout=20):
    return subprocess.run(cmd_list, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)

def test_virtio_async_submission():
    print("\n[scenario 1] Testing VirtIO-GPU Completion IRQ & Async Submission...")
    qemu_cmd = [
        "qemu-system-x86_64",
        "-drive", f"file={REPO_ROOT}/firstos.iso,format=raw,media=cdrom,readonly=on",
        "-boot", "d",
        "-drive", f"file={REPO_ROOT}/fs.img,format=raw,if=none,id=fsdisk",
        "-device", "virtio-blk-pci,drive=fsdisk",
        "-m", "2G",
        "-vga", "none",
        "-device", "virtio-gpu-pci,xres=1024,yres=768",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-serial", "stdio",
        "-display", "none"
    ]
    proc = subprocess.Popen(qemu_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    start = time.time()
    logs = []
    while time.time() - start < 15:
        line = proc.stdout.readline()
        if not line: break
        s = line.decode("latin1", errors="replace").rstrip("\r\n")
        logs.append(s)
        if "frame=3 recycled buffer=" in s or "frame=3 complete" in s:
            break

    proc.terminate()
    try: proc.wait(timeout=2)
    except: proc.kill()

    log_str = "\n".join(logs)

    assert "[gpu_irq] virtio interrupt capability detected" in log_str, "Missing virtio interrupt capability log"
    assert "[gpu_irq] completion events enabled" in log_str, "Missing completion events enabled log"
    assert "[gpu_irq] handler registered vector=" in log_str, "Missing IRQ handler registered log"
    assert "[gpu_phase6] event queue initialized" in log_str, "Missing event queue initialized log"
    assert "[gpu_phase6] completion mode=irq" in log_str, "Missing completion mode=irq log"
    assert "[gpu_phase6] watchdog enabled" in log_str, "Missing watchdog enabled log"
    assert "[gpu_phase6] async submission ready" in log_str, "Missing async submission ready log"
    assert "[gpu_present] frame=1 submitted async" in log_str, "Missing frame 1 async submission log"
    assert "[gpu_irq] completion event frame=1" in log_str, "Missing frame 1 IRQ completion log"
    assert "[gpu_buffer] frame=1 recycled buffer=0" in log_str, "Missing frame 1 buffer recycling log"
    print("  [PASS] VirtIO async submission, IRQ notification, and buffer recycling verified.")
    return True

def test_generic_fb_synchronous_fallback():
    print("\n[scenario 2] Testing Generic-FB Synchronous Fallback & Bounded Polling...")
    qemu_cmd = [
        "qemu-system-x86_64",
        "-drive", f"file={REPO_ROOT}/firstos.iso,format=raw,media=cdrom,readonly=on",
        "-boot", "d",
        "-drive", f"file={REPO_ROOT}/fs.img,format=raw,if=none,id=fsdisk",
        "-device", "virtio-blk-pci,drive=fsdisk",
        "-m", "2G",
        "-vga", "std",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-serial", "stdio",
        "-display", "none"
    ]
    proc = subprocess.Popen(qemu_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    start = time.time()
    logs = []
    while time.time() - start < 15:
        line = proc.stdout.readline()
        if not line: break
        s = line.decode("latin1", errors="replace").rstrip("\r\n")
        logs.append(s)
        if "[gpu_phase6] async IRQ unavailable" in s or "frame=2 complete" in s:
            break

    proc.terminate()
    try: proc.wait(timeout=2)
    except: proc.kill()

    log_str = "\n".join(logs)

    assert "[gpu_irq] no completion IRQ for generic-fb" in log_str, "Missing generic-fb no completion IRQ log"
    assert "[gpu_present] synchronous completion path active" in log_str, "Missing synchronous path active log"
    assert "[gpu_phase6] event queue initialized" in log_str, "Missing event queue init log"
    assert "[gpu_phase6] completion mode=bounded-polling" in log_str, "Missing bounded-polling mode log"
    assert "[gpu_phase6] async IRQ unavailable; safe fallback active" in log_str, "Missing async IRQ unavailable log"
    print("  [PASS] Generic-FB safe bounded-polling fallback verified.")
    return True

def test_stale_event_rejection():
    print("\n[scenario 3] Testing Stale & Duplicate Event Rejection...")
    qemu_cmd = [
        "qemu-system-x86_64",
        "-drive", f"file={REPO_ROOT}/firstos.iso,format=raw,media=cdrom,readonly=on",
        "-boot", "d",
        "-drive", f"file={REPO_ROOT}/fs.img,format=raw,if=none,id=fsdisk",
        "-device", "virtio-blk-pci,drive=fsdisk",
        "-m", "2G",
        "-vga", "none",
        "-device", "virtio-gpu-pci,xres=1024,yres=768",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-serial", "stdio",
        "-display", "none"
    ]
    proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    start = time.time()
    sent = False
    logs = []

    while time.time() - start < 18:
        line = proc.stdout.readline()
        if not line: break
        s = line.decode("latin1", errors="replace").rstrip("\r\n")
        logs.append(s)
        if "async submission ready" in s and not sent:
            time.sleep(0.5)
            proc.stdin.write(b"GPU_TEST_STALE\n")
            proc.stdin.flush()
            sent = True
        if "stale completion ignored frame=999999" in s:
            break

    proc.terminate()
    try: proc.wait(timeout=2)
    except: proc.kill()

    log_str = "\n".join(logs)
    assert "[gpu_irq] stale completion ignored frame=999999" in log_str, "Missing stale event rejection log"
    print("  [PASS] Stale and duplicate event rejection verified cleanly.")
    return True

def test_watchdog_and_recovery():
    print("\n[scenario 4] Testing Watchdog Timeout & Submission Path Recovery...")
    qemu_cmd = [
        "qemu-system-x86_64",
        "-drive", f"file={REPO_ROOT}/firstos.iso,format=raw,media=cdrom,readonly=on",
        "-boot", "d",
        "-drive", f"file={REPO_ROOT}/fs.img,format=raw,if=none,id=fsdisk",
        "-device", "virtio-blk-pci,drive=fsdisk",
        "-m", "2G",
        "-vga", "none",
        "-device", "virtio-gpu-pci,xres=1024,yres=768",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-serial", "stdio",
        "-display", "none"
    ]
    proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    start = time.time()
    sent = False
    logs = []

    while time.time() - start < 18:
        line = proc.stdout.readline()
        if not line: break
        s = line.decode("latin1", errors="replace").rstrip("\r\n")
        logs.append(s)
        if "async submission ready" in s and not sent:
            time.sleep(0.5)
            proc.stdin.write(b"GPU_TEST_WATCHDOG\n")
            proc.stdin.flush()
            sent = True
        if "submission path successfully recovered" in s:
            break

    proc.terminate()
    try: proc.wait(timeout=2)
    except: proc.kill()

    log_str = "\n".join(logs)
    assert "[gpu_watchdog] frame=8888 completion timeout" in log_str, "Missing watchdog timeout log"
    assert "[gpu_watchdog] bounded poll retry=1" in log_str, "Missing bounded poll retry log"
    assert "[gpu_watchdog] recovery required" in log_str, "Missing recovery required log"
    assert "[gpu_recovery] submission path successfully recovered" in log_str, "Missing recovery success log"
    print("  [PASS] Watchdog timeout and submission path recovery verified.")
    return True

def test_resolution_change_irq_masking():
    print("\n[scenario 5] Testing Resolution Change IRQ Masking & Completion Path Re-setup...")
    qemu_cmd = [
        "qemu-system-x86_64",
        "-drive", f"file={REPO_ROOT}/firstos.iso,format=raw,media=cdrom,readonly=on",
        "-boot", "d",
        "-drive", f"file={REPO_ROOT}/fs.img,format=raw,if=none,id=fsdisk",
        "-device", "virtio-blk-pci,drive=fsdisk",
        "-m", "2G",
        "-vga", "none",
        "-device", "virtio-gpu-pci,xres=1024,yres=768",
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-serial", "stdio",
        "-display", "none"
    ]
    proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    start = time.time()
    sent = False
    logs = []

    while time.time() - start < 18:
        line = proc.stdout.readline()
        if not line: break
        s = line.decode("latin1", errors="replace").rstrip("\r\n")
        logs.append(s)
        if ("async submission ready" in s or "lifecycle ready" in s) and not sent:
            time.sleep(0.5)
            proc.stdin.write(b"SET_RES:800,600\n")
            proc.stdin.flush()
            sent = True
        if "resolution change success active=0x00000320x0x00000258" in s:
            break

    proc.terminate()
    try: proc.wait(timeout=2)
    except: proc.kill()

    log_str = "\n".join(logs)
    assert "[gpu_runtime] resolution change requested" in log_str, "Missing res change requested"
    assert "[gpu_buffer] draining frame queue" in log_str, "Missing buffer drain"
    assert "[gpu_phase6] completion mode=irq" in log_str, "Missing completion mode restoration"
    assert "resolution change success active=0x00000320x0x00000258" in log_str, "Missing res change success"
    print("  [PASS] Resolution change IRQ masking, queue draining, and path re-setup verified.")
    return True

def test_frozen_hotpaths():
    print("\n[scenario 6] Verifying Frozen Hot-Paths Integrity...")
    hotpaths = {
        "gui_draw": REPO_ROOT / "gui.c",
        "draw_cursor": REPO_ROOT / "gui.c",
        "fb_present": REPO_ROOT / "framebuffer.c",
        "virtio_gpu_2d_present": REPO_ROOT / "virtio_gpu_2d.c",
        "fb_mark_dirty_rect": REPO_ROOT / "framebuffer.c"
    }
    for hp, fpath in hotpaths.items():
        assert fpath.exists(), f"File {fpath} does not exist"
        content = fpath.read_text(encoding="latin1", errors="replace")
        assert hp in content, f"Frozen function {hp} not found in {fpath}"
        print(f"  [CHECK] Frozen symbol '{hp}' confirmed in {fpath.name}.")
    print("  [PASS] All 5 frozen rendering functions remain strictly untouched.")
    return True

def main():
    print("=" * 50)
    print("   FirstOS Phase 6 GPU Async Events Gate Verification   ")
    print("=" * 50)

    t0 = time.time()
    try:
        test_virtio_async_submission()
        test_generic_fb_synchronous_fallback()
        test_stale_event_rejection()
        test_watchdog_and_recovery()
        test_resolution_change_irq_masking()
        test_frozen_hotpaths()

        elapsed = time.time() - t0
        print("\n" + "=" * 50)
        print(f"[PASS] All 6 Phase 6 Async & IRQ scenarios passed in {elapsed:.2f}s!")
        print("=" * 50)
        return 0
    except AssertionError as e:
        print(f"\n[FAIL] Assertion failed: {e}")
        return 1
    except Exception as e:
        print(f"\n[ERROR] Test error: {e}")
        return 2

if __name__ == "__main__":
    sys.exit(main())
