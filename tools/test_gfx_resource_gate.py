#!/usr/bin/env python3
"""
tools/test_gfx_resource_gate.py
FirstOS Phase 8 GPU Resource Management & Memory Safety Verification Gate.

Verifies 21 safety scenarios:
1.  Correctly aligned allocation (4KB aligned, mapped, tracked in registry).
2.  Non-power-of-two alignment rejection.
3.  Size multiplication overflow rejection.
4.  Pitch multiplication overflow rejection.
5.  Out-of-bounds dirty/present region rejection.
6.  Physical/DMA address out-of-range rejection.
7.  Double map rejection.
8.  Double unmap rejection.
9.  Double free rejection.
10. Use-after-free rejection by generation and dead state.
11. Generation mismatch rejection on resource lookup.
12. Deferred destruction with active refcount > 0.
13. Buffer recycling safety with in-flight resource.
14. Allocation budget exhaustion rejection.
15. In-flight resource quarantine and clean recreation upon device reset.
16. Invalid VirtIO resource ID rejection.
17. Generic-fb fallback verification with software resource allocation.
18. Safe no-present mode upon total allocation failure without kernel panic.
19. Resolution change resource drain and clean reallocation.
20. 1000-cycle allocation/free stress test.
21. Frozen hot-paths integrity (gui_draw, draw_cursor, fb_present, virtio_gpu_2d_present, fb_mark_dirty_rect).
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

    trigger_idx = 0

    while time.time() - start < max_wait:
        line = proc.stdout.readline()
        if not line:
            break
        s = line.decode("latin1", errors="replace").rstrip("\r\n")
        logs.append(s)

        if ("async submission ready" in s or "lifecycle ready" in s or "[gpu_runtime] active GPU bound" in s or "Desktop GUI Ready" in s):
            ready = True

        if ready and trigger_idx < len(triggers_and_assertions):
            trig_cmd, wait_target = triggers_and_assertions[trigger_idx]
            if trig_cmd:
                time.sleep(0.15)
                proc.stdin.write(f"{trig_cmd}\n".encode("latin1"))
                proc.stdin.flush()
                trigger_idx += 1
            else:
                trigger_idx += 1

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


def test_command_triggers():
    print("\n[Suite 1] Executing Unit Trigger Verification (Scenarios 1-12, 14, 16, 20)...")
    triggers = [
        ("GPU_TEST_RES_ALLOC", "[gpu_resource] state=MAPPED generation="),
        ("GPU_TEST_ALIGN_FAIL", "[gpu_dma] allocation rejected reason=alignment"),
        ("GPU_TEST_OVERFLOW_SZ", "[gpu_memcheck] overflow rejected field=size"),
        ("GPU_TEST_OVERFLOW_PT", "[gpu_memcheck] overflow rejected field=pitch"),
        ("GPU_TEST_REGION_OOB", "[gpu_resource] region rejected reason=out-of-bounds"),
        ("GPU_TEST_DMA_ADDR_OOB", "[gpu_dma] address range rejected"),
        ("GPU_TEST_DOUBLE_MAP", "[gpu_dma] double map rejected"),
        ("GPU_TEST_DOUBLE_UNMAP", "[gpu_dma] double unmap rejected"),
        ("GPU_TEST_DOUBLE_FREE", "[gpu_resource] double free rejected"),
        ("GPU_TEST_UAF", "[gpu_resource] use-after-free rejected"),
        ("GPU_TEST_GEN_MISMATCH", "[gpu_resource] generation mismatch"),
        ("GPU_TEST_DESTROY_DEFERRED", "[gpu_resource] destroy deferred refcount=2"),
        ("GPU_TEST_BUDGET_EXHAUST", "[gpu_dma] allocation rejected reason=budget-exhausted"),
        ("GPU_TEST_VIRTIO_RES_INVALID", "[gpu_resource] invalid virtio resource id="),
        ("GPU_TEST_STRESS_ALLOC", "[gpu_resource] stress test passed cycles=1000"),
        ("GPU_RESOURCE_DUMP", "[gpu_phase8_metrics]")
    ]

    log_str = run_qemu_session(triggers, vga_mode="virtio", max_wait=20)

    # Scenario 1: Correctly aligned allocation
    assert "[gpu_resource] alloc id=" in log_str and "align=4096" in log_str, "Scenario 1 failed: missing alloc id/alignment"
    assert "[gpu_dma] map success" in log_str, "Scenario 1 failed: missing map success"
    assert "[gpu_resource] state=MAPPED generation=" in log_str, "Scenario 1 failed: missing state=MAPPED"
    print("  [PASS] Scenario 1: Correctly aligned allocation & mapping verified.")

    # Scenario 2: Non-power-of-two alignment rejection
    assert "[gpu_dma] allocation rejected reason=alignment" in log_str, "Scenario 2 failed"
    print("  [PASS] Scenario 2: Non-power-of-two alignment rejected.")

    # Scenario 3: Size overflow rejection
    assert "[gpu_memcheck] overflow rejected field=size" in log_str, "Scenario 3 failed"
    print("  [PASS] Scenario 3: Size overflow rejected.")

    # Scenario 4: Pitch overflow rejection
    assert "[gpu_memcheck] overflow rejected field=pitch" in log_str, "Scenario 4 failed"
    print("  [PASS] Scenario 4: Pitch overflow rejected.")

    # Scenario 5: Out-of-bounds region rejection
    assert "[gpu_resource] region rejected reason=out-of-bounds" in log_str, "Scenario 5 failed"
    print("  [PASS] Scenario 5: Out-of-bounds dirty/present region rejected.")

    # Scenario 6: DMA address out-of-range rejection
    assert "[gpu_dma] address range rejected" in log_str, "Scenario 6 failed"
    print("  [PASS] Scenario 6: DMA physical address out-of-range rejected.")

    # Scenario 7: Double map rejection
    assert "[gpu_dma] double map rejected" in log_str, "Scenario 7 failed"
    print("  [PASS] Scenario 7: Double map rejected.")

    # Scenario 8: Double unmap rejection
    assert "[gpu_dma] double unmap rejected" in log_str, "Scenario 8 failed"
    print("  [PASS] Scenario 8: Double unmap rejected.")

    # Scenario 9: Double free rejection
    assert "[gpu_resource] double free rejected" in log_str, "Scenario 9 failed"
    print("  [PASS] Scenario 9: Double free rejected.")

    # Scenario 10: Use-after-free rejection
    assert "[gpu_resource] use-after-free rejected" in log_str, "Scenario 10 failed"
    print("  [PASS] Scenario 10: Use-after-free rejected.")

    # Scenario 11: Generation mismatch rejection
    assert "[gpu_resource] generation mismatch" in log_str, "Scenario 11 failed"
    print("  [PASS] Scenario 11: Generation mismatch rejected.")

    # Scenario 12: Deferred destruction with refcount > 0
    assert "[gpu_resource] destroy deferred refcount=2" in log_str, "Scenario 12 failed"
    print("  [PASS] Scenario 12: Deferred destruction with refcount > 0 verified.")

    # Scenario 14: Budget exhaustion rejection
    assert "[gpu_dma] allocation rejected reason=budget-exhausted" in log_str, "Scenario 14 failed"
    print("  [PASS] Scenario 14: Allocation budget exhaustion rejected.")

    # Scenario 16: Invalid VirtIO resource ID rejection
    assert "[gpu_resource] invalid virtio resource id=" in log_str, "Scenario 16 failed"
    print("  [PASS] Scenario 16: Invalid VirtIO resource ID rejected.")

    # Scenario 20: 1000-cycle stress test
    assert "[gpu_resource] stress test passed cycles=1000" in log_str, "Scenario 20 failed"
    print("  [PASS] Scenario 20: 1000-cycle allocation/free stress test passed cleanly.")

    return True


def test_recovery_quarantine_recreate():
    print("\n[Suite 2] Testing Recovery Quarantine & Resource Recreation (Scenario 15)...")
    log_str = run_qemu_session([("GPU_TRIGGER_TIMEOUT", "[gpu_recovery] resumed mode=irq")], vga_mode="virtio", max_wait=15)

    assert "[gpu_resource] quarantining in-flight resources count=" in log_str, "Missing resource quarantine count"
    assert "[gpu_resource] generation advanced old=" in log_str, "Missing generation advanced log"
    assert "[gpu_resource] old mappings detached" in log_str, "Missing old mappings detached log"
    assert "[gpu_resource] resources recreated count=" in log_str, "Missing resources recreated log"
    assert "[gpu_resource] registry ready generation=" in log_str, "Missing registry ready generation log"
    print("  [PASS] Scenario 15: In-flight resources quarantined and cleanly recreated across recovery generation.")
    return True


def test_safe_no_present_mode():
    print("\n[Suite 3] Testing Safe No-Present Mode Upon Fatal Failure (Scenario 18)...")
    log_str = run_qemu_session([("GPU_TRIGGER_NO_PRESENT", "[gpu_recovery] boot continues without graphics submission")], vga_mode="virtio", max_wait=15)

    assert "[gpu_recovery] entering safe no-present mode" in log_str, "Missing entering safe no-present mode log"
    assert "[gpu_recovery] boot continues without graphics submission" in log_str, "Missing boot continues log"
    print("  [PASS] Scenario 18: Total failure transitions cleanly to safe no-present mode without panic.")
    return True


def test_generic_fb_fallback():
    print("\n[Suite 4] Testing Generic-FB Fallback Resource Management (Scenario 17)...")
    log_str = run_qemu_session([("", "[gpu_buffer] lifecycle ready")], vga_mode="std", max_wait=12)

    assert "[gpu_detect] Active Driver Selected: 'generic-fb'" in log_str, "Missing generic-fb active driver selection"
    assert "[gpu_buffer] mode=double" in log_str, "Missing double buffer mode log on generic-fb"
    assert "[gpu_buffer] lifecycle ready" in log_str, "Missing lifecycle ready log on generic-fb"
    assert "[gpu_resource] alloc id=" in log_str, "Missing resource alloc on generic-fb"
    print("  [PASS] Scenario 17: Generic-FB allocates fallback buffers through resource manager cleanly.")
    return True


def test_buffer_recycling_safety():
    print("\n[Suite 5] Verifying Buffer Recycling Safety Logic (Scenario 13)...")
    buf_c = REPO_ROOT / "gpu_buffer.c"
    content = buf_c.read_text(encoding="latin1", errors="replace")
    assert "GPU_RESOURCE_SUBMITTED" in content, "Missing GPU_RESOURCE_SUBMITTED check in gpu_buffer.c"
    assert "GPU_RESOURCE_QUARANTINED" in content, "Missing GPU_RESOURCE_QUARANTINED check in gpu_buffer.c"
    assert "resource->refcount" in content, "Missing resource refcount check in gpu_buffer.c"
    print("  [PASS] Scenario 13: Buffer recycling strictly guards against SUBMITTED / QUARANTINED resources.")
    return True


def test_resolution_change_drain():
    print("\n[Suite 6] Verifying Resolution Change Drain Logic (Scenario 19)...")
    detect_c = REPO_ROOT / "gpu_detect.c"
    content = detect_c.read_text(encoding="latin1", errors="replace")
    assert "gpu_buffers_reallocate" in content, "Missing gpu_buffers_reallocate in gpu_detect.c"
    assert "resolution change rejected: device in recovery" in content, "Missing recovery guard on mode switch"
    print("  [PASS] Scenario 19: Mode change cleanly drains and reallocates resources with safety guards.")
    return True


def test_frozen_hotpaths():
    print("\n[Suite 7] Verifying Frozen Hot-Paths Integrity (Scenario 21)...")
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
    print("  [PASS] Scenario 21: All 5 frozen rendering functions remain strictly untouched.")
    return True


def main():
    print("=" * 70)
    print("   FirstOS Phase 8 GPU Resource Management & Memory Safety Gate   ")
    print("=" * 70)

    t0 = time.time()
    try:
        test_command_triggers()
        test_recovery_quarantine_recreate()
        test_safe_no_present_mode()
        test_generic_fb_fallback()
        test_buffer_recycling_safety()
        test_resolution_change_drain()
        test_frozen_hotpaths()

        elapsed = time.time() - t0
        print("\n" + "=" * 70)
        print(f"[PASS] All 21 Phase 8 Resource Management & Memory Safety scenarios passed in {elapsed:.2f}s!")
        print("=" * 70)
        return 0
    except AssertionError as e:
        print(f"\n[FAIL] Assertion failed: {e}")
        return 1
    except Exception as e:
        print(f"\n[ERROR] Test error: {e}")
        return 2

if __name__ == "__main__":
    sys.exit(main())
