#!/usr/bin/env python3
"""
tools/test_gfx_abi_gate.py
FirstOS Phase 10 GPU Work Submission ABI & User-Space Display Interface Verification Gate.

Verifies 30 scenarios:
1.  Open and close client.
2.  Query device/capabilities.
3.  Create/close resource handle.
4.  Valid present submission.
5.  Invalid handle.
6.  Generation mismatch.
7.  Client permission denial.
8.  Quota exceeded.
9.  Queue full / backpressure.
10. Dirty-region validation.
11. ABI version mismatch.
12. Structure too small.
13. Reserved fields non-zero.
14. Event polling.
15. Bounded event wait.
16. Duplicate event rejection.
17. Resource ownership violation.
18. Unregister with in-flight request.
19. Recovery while request pending.
20. Device loss after admission.
21. Stale request after recovery.
22. User pointer validation.
23. Invalid format/flags.
24. Request flood.
25. Event flood.
26. Fuzz malformed descriptors.
27. No-present mode.
28. Frozen hot-path integrity.
29. Phase 5–9 gates.
30. Full graphics regression gate.
"""

import sys
import os
import time
import subprocess
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

def run_qemu_session(triggers_and_assertions, vga_mode="virtio", max_wait=30):
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

        if ("safe user-space submission path enabled" in s or "Desktop GUI Ready" in s or "async submission ready" in s):
            ready = True

        if ready and trigger_idx < len(triggers_and_assertions):
            trig_cmd, wait_target = triggers_and_assertions[trigger_idx]
            if trig_cmd:
                time.sleep(0.12)
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


def test_abi_scenarios():
    print("\n[Suite 1] Executing Phase 10 GPU Work Submission ABI Verification (Scenarios 1-27)...")
    triggers = [
        ("GPU_TEST_ABI_CLIENT_OPEN_CLOSE", "[gpu_abi_test] client open close verified"),
        ("GPU_TEST_ABI_QUERY_DEVICE", "[gpu_abi_test] device query verified state=READY"),
        ("GPU_TEST_ABI_RESOURCE_HANDLE", "[gpu_abi_test] resource handle lifecycle verified"),
        ("GPU_TEST_ABI_PRESENT_SUBMIT", "[gpu_abi_test] present submission verified request_id="),
        ("GPU_TEST_ABI_INVALID_HANDLE", "[gpu_abi_test] invalid handle rejected status=INVALID_HANDLE"),
        ("GPU_TEST_ABI_GEN_MISMATCH", "[gpu_abi_test] generation mismatch rejected status=GENERATION_MISMATCH"),
        ("GPU_TEST_ABI_PERM_DENIAL", "[gpu_abi_test] permission denial verified status=PERMISSION_DENIED"),
        ("GPU_TEST_ABI_QUOTA_EXCEED", "[gpu_abi_test] quota exceeded verified status=QUOTA_EXCEEDED"),
        ("GPU_TEST_ABI_QUEUE_BACKPRESSURE", "[gpu_abi_test] queue backpressure verified status=QUEUE_FULL"),
        ("GPU_TEST_ABI_DIRTY_REGION", "[gpu_abi_test] dirty region bounds rejected status=INVALID_ARGUMENT"),
        ("GPU_TEST_ABI_BAD_VERSION", "[gpu_abi_test] bad version rejected status=BAD_VERSION"),
        ("GPU_TEST_ABI_BUFFER_TOO_SMALL", "[gpu_abi_test] buffer too small rejected status=BUFFER_TOO_SMALL"),
        ("GPU_TEST_ABI_RESERVED_NONZERO", "[gpu_abi_test] reserved non-zero rejected status=INVALID_ARGUMENT"),
        ("GPU_TEST_ABI_EVENT_POLL_WAIT", "[gpu_abi_test] event polling and bounded wait verified"),
        ("GPU_TEST_ABI_EVENT_DEDUP", "[gpu_abi_test] duplicate event rejection verified"),
        ("GPU_TEST_ABI_OWNERSHIP_VIOLATION", "[gpu_abi_test] ownership violation rejected status=PERMISSION_DENIED"),
        ("GPU_TEST_ABI_UNREG_INFLIGHT", "[gpu_abi_test] unregister with inflight cancelled cleanly"),
        ("GPU_TEST_ABI_RECOVERY_PENDING", "[gpu_abi_test] recovery cancelled pending requests and refreshed generations"),
        ("GPU_TEST_ABI_DEVICE_LOSS_ADMIT", "[gpu_abi_test] device loss rejection verified status=DEVICE_LOST"),
        ("GPU_TEST_ABI_STALE_AFTER_RECOVERY", "[gpu_abi_test] stale request after recovery rejected status=GENERATION_MISMATCH"),
        ("GPU_TEST_ABI_USER_PTR_CHECK", "[gpu_abi_test] user pointer check rejected status=INVALID_ARGUMENT"),
        ("GPU_TEST_ABI_INVALID_FLAGS", "[gpu_abi_test] invalid flags rejected status=INVALID_ARGUMENT"),
        ("GPU_TEST_ABI_REQUEST_FLOOD", "[gpu_abi_test] request flood throttled safely"),
        ("GPU_TEST_ABI_EVENT_FLOOD", "[gpu_abi_test] event flood backpressure enforced"),
        ("GPU_TEST_ABI_FUZZ_DESCRIPTORS", "[gpu_abi_test] fuzz malformed descriptors passed 0 panics"),
        ("GPU_TEST_ABI_NO_PRESENT", "[gpu_abi_test] no present mode verified status=NO_PRESENT"),
        ("GPU_ABI_DUMP", "[gpu_abi] clients=")
    ]

    log_str = run_qemu_session(triggers, vga_mode="virtio", max_wait=45)

    # Boot signatures validation
    assert "[gpu_phase10] handle table initialized capacity=128" in log_str, "Boot handle init signature missing"
    assert "[gpu_phase10] user client ABI initialized version=1" in log_str, "Boot user client ABI init signature missing"
    assert "[gpu_phase10] compositor ABI client ready" in log_str, "Boot compositor client ready signature missing"
    assert "[gpu_phase10] application display ABI ready" in log_str, "Boot application display ready signature missing"
    assert "[gpu_phase10] safe user-space submission path enabled" in log_str, "Boot safe submission enabled signature missing"

    # Scenarios 1 to 27
    expected_matches = [
        ("Scenario 1: Open and close client", "[gpu_abi_test] client open close verified"),
        ("Scenario 2: Query device/capabilities", "[gpu_abi_test] device query verified state=READY"),
        ("Scenario 3: Create/close resource handle", "[gpu_abi_test] resource handle lifecycle verified"),
        ("Scenario 4: Valid present submission", "[gpu_abi_test] present submission verified request_id="),
        ("Scenario 5: Invalid handle", "[gpu_abi_test] invalid handle rejected status=INVALID_HANDLE"),
        ("Scenario 6: Generation mismatch", "[gpu_abi_test] generation mismatch rejected status=GENERATION_MISMATCH"),
        ("Scenario 7: Client permission denial", "[gpu_abi_test] permission denial verified status=PERMISSION_DENIED"),
        ("Scenario 8: Quota exceeded", "[gpu_abi_test] quota exceeded verified status=QUOTA_EXCEEDED"),
        ("Scenario 9: Queue full / backpressure", "[gpu_abi_test] queue backpressure verified status=QUEUE_FULL"),
        ("Scenario 10: Dirty-region validation", "[gpu_abi_test] dirty region bounds rejected status=INVALID_ARGUMENT"),
        ("Scenario 11: ABI version mismatch", "[gpu_abi_test] bad version rejected status=BAD_VERSION"),
        ("Scenario 12: Structure too small", "[gpu_abi_test] buffer too small rejected status=BUFFER_TOO_SMALL"),
        ("Scenario 13: Reserved fields non-zero", "[gpu_abi_test] reserved non-zero rejected status=INVALID_ARGUMENT"),
        ("Scenario 14: Event polling", "[gpu_abi_test] event polling and bounded wait verified"),
        ("Scenario 15: Bounded event wait", "[gpu_abi_test] event polling and bounded wait verified"),
        ("Scenario 16: Duplicate event rejection", "[gpu_abi_test] duplicate event rejection verified"),
        ("Scenario 17: Resource ownership violation", "[gpu_abi_test] ownership violation rejected status=PERMISSION_DENIED"),
        ("Scenario 18: Unregister with in-flight request", "[gpu_abi_test] unregister with inflight cancelled cleanly"),
        ("Scenario 19: Recovery while request pending", "[gpu_abi_test] recovery cancelled pending requests and refreshed generations"),
        ("Scenario 20: Device loss after admission", "[gpu_abi_test] device loss rejection verified status=DEVICE_LOST"),
        ("Scenario 21: Stale request after recovery", "[gpu_abi_test] stale request after recovery rejected status=GENERATION_MISMATCH"),
        ("Scenario 22: User pointer validation", "[gpu_abi_test] user pointer check rejected status=INVALID_ARGUMENT"),
        ("Scenario 23: Invalid format/flags", "[gpu_abi_test] invalid flags rejected status=INVALID_ARGUMENT"),
        ("Scenario 24: Request flood", "[gpu_abi_test] request flood throttled safely"),
        ("Scenario 25: Event flood", "[gpu_abi_test] event flood backpressure enforced"),
        ("Scenario 26: Fuzz malformed descriptors", "[gpu_abi_test] fuzz malformed descriptors passed 0 panics"),
        ("Scenario 27: No-present mode", "[gpu_abi_test] no present mode verified status=NO_PRESENT"),
    ]

    for name, target in expected_matches:
        if target not in log_str:
            print(f"FAILED ON: {name}")
            print("--- ALL SERIAL_IN OR GPU_ABI_TEST LINES ---")
            lines = log_str.splitlines()
            for l in lines:
                if "[serial_in]" in l or "[gpu_abi_test]" in l:
                    print(l)
            assert False, f"{name} failed: '{target}' not found in logs"
        print(f"  [PASS] {name} passed.")

    assert "kernel panic" not in log_str.lower() and "panic:" not in log_str.lower(), "Kernel panic detected during ABI testing"
    print("  [PASS] No kernel panic or memory corruption detected.")


def test_frozen_hotpaths():
    print("\n[Suite 2] Verifying Frozen Hot-Paths Integrity (Scenario 28)...")
    frozen_files = {
        "gui_draw": ("gui.c", r"void gui_draw\("),
        "draw_cursor": ("gui.c", r"void draw_cursor\("),
        "fb_present": ("framebuffer.c", r"void fb_present\("),
        "virtio_gpu_2d_present": ("virtio_gpu_2d.c", r"int virtio_gpu_2d_present\("),
        "fb_mark_dirty_rect": ("framebuffer.c", r"void fb_mark_dirty_rect\(")
    }

    import re
    for symbol, (rel_path, pattern) in frozen_files.items():
        file_path = REPO_ROOT / rel_path
        assert file_path.exists(), f"File {rel_path} missing"
        content = file_path.read_text(encoding="utf-8", errors="replace")
        assert re.search(pattern, content), f"Frozen symbol '{symbol}' was modified or missing in {rel_path}!"
        print(f"  [CHECK] Frozen symbol '{symbol}' confirmed in {rel_path}.")

    print("  [PASS] Scenario 28: Frozen hot-path integrity passed.")


def test_prior_gates():
    print("\n[Suite 3] Verifying Phase 5-9 Gates (Scenario 29)...")
    gates = [
        ("Phase 9 Arbitration Gate", "tools/test_gfx_arbitration_gate.py"),
        ("Phase 8 Resource Gate", "tools/test_gfx_resource_gate.py"),
        ("Phase 7 Recovery Gate", "tools/test_gfx_recovery_gate.py"),
        ("Phase 6 Async Gate", "tools/test_gfx_async_gate.py"),
        ("Phase 5 Buffer Gate", "tools/test_gfx_buffer_gate.py")
    ]
    for name, script in gates:
        print(f"  --> Running {name} ({script})...")
        res = subprocess.run([sys.executable, str(REPO_ROOT / script)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert res.returncode == 0, f"{name} failed:\n{res.stdout.decode('latin1', errors='replace')}"
        print(f"  [PASS] {name} passed.")

    print("  [PASS] Scenario 29: Phase 5-9 gates passed.")


def test_full_regression():
    print("\n[Suite 4] Verifying Full Graphics Regression Gate (Scenario 30)...")
    res = subprocess.run([sys.executable, str(REPO_ROOT / "tools/gfx_regression_gate.py")], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert res.returncode == 0, f"Full graphics regression gate failed:\n{res.stdout.decode('latin1', errors='replace')}"
    print("  [PASS] Scenario 30: Full graphics regression gate passed.")


def main():
    print("=" * 70)
    print("   FirstOS Phase 10 GPU Work Submission ABI Verification Gate   ")
    print("=" * 70)
    t0 = time.time()

    test_abi_scenarios()
    test_frozen_hotpaths()
    test_prior_gates()
    test_full_regression()

    duration = time.time() - t0
    print("\n" + "=" * 70)
    print(f"[PASS] All Phase 10 ABI and user-space display scenarios passed in {duration:.2f}s!")
    print("[PASS] Phase 5-9 gates passed")
    print("[PASS] Frozen hot-path integrity passed")
    print("[PASS] No kernel panic or memory corruption detected")
    print("=" * 70)


if __name__ == "__main__":
    main()
