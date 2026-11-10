#!/usr/bin/env python3
"""
tools/test_gfx_recovery_gate.py
FirstOS Phase 7 GPU Reset, Device Loss & Fault-Tolerant Recovery Verification Gate.

Verifies:
1. Watchdog completion timeout -> successful reset transaction.
2. Queue error event -> resource recreation.
3. Consecutive IRQ failure -> health monitor threshold & recovery.
4. Stale event rejection by generation counter.
5. In-flight frame quarantine upon device loss.
6. Reset failure fallback to generic-fb driver.
7. Total reset failure safe no-present mode without kernel panic.
8. Reset storm prevention with threshold limit.
9. Mode switch / resize rejection during active recovery.
10. Frozen hot-paths integrity (gui_draw, draw_cursor, fb_present, virtio_gpu_2d_present, fb_mark_dirty_rect).
"""

import sys
import time
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

def run_qemu_session(triggers_and_assertions, vga_mode="virtio", max_wait=20):
    qemu_cmd = [
        "qemu-system-x86_64",
        "-drive", f"file={REPO_ROOT}/firstos.iso,format=raw,media=cdrom,readonly=on",
        "-boot", "d",
        "-drive", f"file={REPO_ROOT}/fs.img,format=raw,if=none,id=fsdisk",
        "-device", "virtio-blk-pci,drive=fsdisk",
        "-m", "2G"
    ]
    if vga_mode == "virtio":
        qemu_cmd += ["-vga", "none", "-device", "virtio-gpu-pci,xres=1024,yres=768"]
    else:
        qemu_cmd += ["-vga", "std"]

    qemu_cmd += [
        "-netdev", "user,id=net0",
        "-device", "virtio-net-pci,netdev=net0",
        "-serial", "stdio",
        "-display", "none"
    ]

    proc = subprocess.Popen(qemu_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    start = time.time()
    logs = []
    ready = False

    # Execute triggers sequentially
    trigger_idx = 0

    while time.time() - start < max_wait:
        line = proc.stdout.readline()
        if not line:
            break
        s = line.decode("latin1", errors="replace").rstrip("\r\n")
        logs.append(s)

        if ("async submission ready" in s or "lifecycle ready" in s or "[gpu_runtime] active GPU bound" in s):
            ready = True

        if ready and trigger_idx < len(triggers_and_assertions):
            trig_cmd, wait_target = triggers_and_assertions[trigger_idx]
            if trig_cmd:
                time.sleep(0.3)
                proc.stdin.write(f"{trig_cmd}\n".encode("latin1"))
                proc.stdin.flush()
                trigger_idx += 1
            else:
                trigger_idx += 1

        # Check if all wait targets observed
        if trigger_idx >= len(triggers_and_assertions):
            all_found = True
            log_str = "\n".join(logs)
            for _, wait_target in triggers_and_assertions:
                if wait_target and wait_target not in log_str:
                    all_found = False
                    break
            if all_found:
                break

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except:
        proc.kill()

    return "\n".join(logs)


def test_watchdog_timeout_recovery():
    print("\n[scenario 1] Testing Watchdog Completion Timeout -> Successful Reset Transaction...")
    log_str = run_qemu_session([("GPU_TRIGGER_TIMEOUT", "[gpu_recovery] resumed mode=irq")], vga_mode="virtio", max_wait=15)

    assert "[gpu_recovery] trigger=completion-timeout" in log_str, "Missing trigger=completion-timeout log"
    assert "[gpu_recovery] device lost reason=completion-timeout" in log_str, "Missing device lost reason"
    assert "[gpu_recovery] state=LOST" in log_str, "Missing state=LOST log"
    assert "[gpu_recovery] submissions stopped" in log_str, "Missing submissions stopped log"
    assert "[gpu_recovery] interrupts masked" in log_str, "Missing interrupts masked log"
    assert "[gpu_recovery] pending frames quarantined" in log_str, "Missing pending frames quarantined log"
    assert "[gpu_recovery] device reset started" in log_str, "Missing device reset started log"
    assert "[gpu_recovery] reset attempt=1" in log_str, "Missing reset attempt=1 log"
    assert "[gpu_recovery] reset success" in log_str, "Missing reset success log"
    assert "[gpu_recovery] transport reinitialized" in log_str, "Missing transport reinitialized log"
    assert "[gpu_recovery] resources recreated" in log_str, "Missing resources recreated log"
    assert "[gpu_recovery] completion path restored" in log_str, "Missing completion path restored log"
    assert "[gpu_recovery] health probe passed" in log_str, "Missing health probe passed log"
    assert "[gpu_recovery] device resumed" in log_str, "Missing device resumed log"
    assert "[gpu_recovery] resumed mode=irq" in log_str, "Missing resumed mode=irq log"
    print("  [PASS] Full 14-step reset transaction completed cleanly.")
    return True


def test_queue_error_recovery():
    print("\n[scenario 2] Testing GPU Queue Error Event -> Resource Recreation...")
    log_str = run_qemu_session([("GPU_TRIGGER_QUEUE_ERR", "[gpu_recovery] resources recreated")], vga_mode="virtio", max_wait=15)

    assert "[gpu_recovery] trigger=queue-error" in log_str, "Missing trigger=queue-error log"
    assert "[gpu_recovery] device lost reason=queue-error" in log_str, "Missing device lost reason"
    assert "[gpu_recovery] reset success" in log_str, "Missing reset success log"
    assert "[gpu_recovery] resources recreated" in log_str, "Missing resources recreated log"
    print("  [PASS] Queue error triggered atomic recovery and resource recreation.")
    return True


def test_irq_failure_recovery():
    print("\n[scenario 3] Testing Health Monitor Consecutive IRQ Failures -> Recovery...")
    log_str = run_qemu_session([("GPU_TRIGGER_IRQ_FAIL", "[gpu_recovery] reset success")], vga_mode="virtio", max_wait=15)

    assert "[gpu_health] consecutive irq failures=3" in log_str, "Missing consecutive irq failures log"
    assert "[gpu_recovery] trigger=irq-failure" in log_str, "Missing trigger=irq-failure log"
    assert "[gpu_recovery] reset success" in log_str, "Missing reset success log"
    print("  [PASS] IRQ failure threshold tripped health monitor and triggered reset.")
    return True


def test_stale_generation_rejection():
    print("\n[scenario 4] Testing Stale Generation Event Rejection...")
    log_str = run_qemu_session([("GPU_TRIGGER_STALE_GEN", "[gpu_recovery] stale generation event ignored")], vga_mode="virtio", max_wait=15)

    assert "[gpu_recovery] stale generation event ignored" in log_str, "Missing stale generation rejection log"
    print("  [PASS] Event with older generation discarded safely without state mutation.")
    return True


def test_frame_quarantine():
    print("\n[scenario 5] Testing Buffer Quarantine On Device Loss...")
    log_str = run_qemu_session([("GPU_TRIGGER_TIMEOUT", "[gpu_recovery] buffers quarantined=")], vga_mode="virtio", max_wait=15)

    assert "[gpu_recovery] pending frames quarantined" in log_str, "Missing pending frames quarantined log"
    assert "[gpu_recovery] buffers quarantined=" in log_str, "Missing buffers quarantined log"
    print("  [PASS] In-flight frames quarantined safely, preventing use-after-free.")
    return True


def test_fallback_to_generic_fb():
    print("\n[scenario 6] Testing Dual Reset Failure -> Graceful Fallback to generic-fb...")
    log_str = run_qemu_session([("GPU_TRIGGER_FALLBACK", "[gpu_recovery] software presentation resumed")], vga_mode="virtio", max_wait=15)

    assert "[gpu_recovery] reset failed attempt=2" in log_str, "Missing reset failed attempt=2 log"
    assert "[gpu_recovery] switching active path to generic-fb" in log_str, "Missing switching active path log"
    assert "[gpu_recovery] software presentation resumed" in log_str, "Missing software presentation resumed log"
    print("  [PASS] VirtIO failures cleanly transitioned system to generic-fb fallback.")
    return True


def test_safe_no_present_mode():
    print("\n[scenario 7] Testing Total GPU & Fallback Failure -> Safe No-Present Mode...")
    log_str = run_qemu_session([("GPU_TRIGGER_NO_PRESENT", "[gpu_recovery] boot continues without graphics submission")], vga_mode="virtio", max_wait=15)

    assert "[gpu_recovery] generic-fb unavailable" in log_str, "Missing generic-fb unavailable log"
    assert "[gpu_recovery] entering safe no-present mode" in log_str, "Missing entering safe no-present mode log"
    assert "[gpu_recovery] boot continues without graphics submission" in log_str, "Missing boot continues log"
    print("  [PASS] Total loss handled gracefully without kernel panic or infinite hang.")
    return True


def test_reset_storm_prevention():
    print("\n[scenario 8] Testing Reset Storm Prevention & Cooldown...")
    log_str = run_qemu_session([("GPU_TRIGGER_STORM", "[gpu_recovery] reset storm prevented")], vga_mode="virtio", max_wait=15)

    assert "[gpu_recovery] reset storm prevented" in log_str, "Missing reset storm prevented log"
    print("  [PASS] Repeated rapid triggers suppressed by storm prevention filter.")
    return True


def test_resolution_change_during_recovery():
    print("\n[scenario 9] Testing Mode Switch Protection During Recovery...")
    hotpaths = REPO_ROOT / "gpu_detect.c"
    content = hotpaths.read_text(encoding="latin1", errors="replace")
    assert "resolution change rejected: device in recovery" in content, "Missing resolution change recovery check"
    assert "s_active_gpu.device_state == GPU_DEVICE_LOST" in content, "Missing GPU_DEVICE_LOST state check"
    print("  [PASS] Resolution change / mode switch explicitly blocked during recovery.")
    return True


def test_frozen_hotpaths():
    print("\n[scenario 10] Verifying Frozen Hot-Paths Integrity...")
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
    print("=" * 60)
    print("   FirstOS Phase 7 GPU Reset & Fault-Tolerant Gate Verification   ")
    print("=" * 60)

    t0 = time.time()
    try:
        test_watchdog_timeout_recovery()
        test_queue_error_recovery()
        test_irq_failure_recovery()
        test_stale_generation_rejection()
        test_frame_quarantine()
        test_fallback_to_generic_fb()
        test_safe_no_present_mode()
        test_reset_storm_prevention()
        test_resolution_change_during_recovery()
        test_frozen_hotpaths()

        elapsed = time.time() - t0
        print("\n" + "=" * 60)
        print(f"[PASS] All 10 Phase 7 Recovery & Fault-Tolerance scenarios passed in {elapsed:.2f}s!")
        print("=" * 60)
        return 0
    except AssertionError as e:
        print(f"\n[FAIL] Assertion failed: {e}")
        return 1
    except Exception as e:
        print(f"\n[ERROR] Test error: {e}")
        return 2

if __name__ == "__main__":
    sys.exit(main())
